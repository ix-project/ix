/*
 * Copyright 2013-16 Board of Trustees of Stanford University
 * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * mempool.c - A fast memory pool implementation
 *
 * Similar to ix/core/mempool.c
 */

#include <stdio.h>

#include <ix/errno.h>

#include "ix.h"
#include "mempool.h"

static struct mempool_datastore *mempool_all_datastores;

#define log_err(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define panic(fmt, ...) do {printf(fmt, ##__VA_ARGS__); exit(-1); } while (0)

/**
 * mempool_alloc_2  -- second stage allocator; may spinlock
 * @m: mempool
 */

void *mempool_alloc_2(struct mempool *m)
{

	struct mempool_hdr *h;
	assert(m->magic == MEMPOOL_MAGIC);
	assert(m->head == NULL);

	if (m->private_chunk) {
		h = m->private_chunk;
		m->head = h->next;
		m->private_chunk = NULL;
		return h;
	}

	struct mempool_datastore *mds = m->datastore;

	assert(mds);
	spin_lock(&mds->lock);
	h = mds->chunk_head;
	if (likely(h)) {
		mds->chunk_head = h->next_chunk;
		m->head = h->next;
		mds->free_chunks--;
		mds->num_locks++;
	}
	spin_unlock(&mds->lock);
	return h;

}

/**
 * mempool_free_2 -- second stage free
 * @m: mempool
 * @ptr: ptr
 */

void mempool_free_2(struct mempool *m, void *ptr)
{
	struct mempool_hdr *elem = (struct mempool_hdr *) ptr;
	assert(m->num_free == m->chunk_size);

	elem->next = NULL;

	if (m->private_chunk != NULL) {
		struct mempool_datastore *mds = m->datastore;
		spin_lock(&mds->lock);
		m->private_chunk->next_chunk = mds->chunk_head;
		mds->chunk_head = m->private_chunk;
		mds->free_chunks++;
		mds->num_locks++;
		spin_unlock(&mds->lock);
	}
	m->private_chunk = m->head;
	m->head = elem;
}


/**
 * mempool_init_buf_with_pages - creates the object and puts them in the doubly-linked list
 * @m: datastore
 *
 */
int mempool_init_buf_with_pages(struct mempool_datastore *mds, int elems_per_page, int nr_pages,
				size_t elem_len)
{
	int i, j, chunk_count = 0;
	struct mempool_hdr *cur, *head = NULL, *prev = NULL;

	for (i = 0; i < nr_pages; i++) {
		cur = (struct mempool_hdr *)
		      ((uintptr_t) mds->buf + i * PGSIZE_2MB + MEMPOOL_INITIAL_OFFSET);
		for (j = 0; j < elems_per_page; j++) {
			if (prev == NULL)
				head = cur;
			else
				prev->next = cur;

			chunk_count++;
			if (chunk_count == mds->chunk_size) {
				if (mds->chunk_head == NULL) {
					mds->chunk_head = head;
				} else {
					head->next_chunk = mds->chunk_head;
					mds->chunk_head = head;
				}
				head = NULL;
				prev = NULL;
				chunk_count = 0;
				mds->num_chunks++;
				mds->free_chunks++;
			} else {
				prev = cur;
			}
			cur = (struct mempool_hdr *)
			      ((uintptr_t) cur + elem_len);
		}
	}

	return 0;
}


/**
 * mempool_create_datastore - initializes a memory pool datastore
 * @nr_elems: the minimum number of elements in the total pool
 * @elem_len: the length of each element
 * @nostraddle: (bool) 1 == objects cannot straddle 2MB pages
 * @chunk_size: the number of elements in a chunk (allocated to a mempool)
 *
 * NOTE: mempool_createdatastore() will create a pool with at least @nr_elems,
 * but possibily more depending on page alignment.
 *
 * There should be one datastore per C data type (in general).
 * Each core, flow-group or unit of concurrency will create a distinct mempool leveraging the datastore
 *
 * Returns 0 if successful, otherwise fail.
 */

int mempool_create_datastore(struct mempool_datastore *mds, int nr_elems, size_t elem_len, int nostraddle, int chunk_size, const char *name)
{
	int nr_pages;

	assert(mds->magic == 0);
	assert((chunk_size & (chunk_size - 1)) == 0);
	assert(((nr_elems / chunk_size) * chunk_size) == nr_elems);


	if (!elem_len || !nr_elems)
		return -EINVAL;

	mds->magic = MEMPOOL_MAGIC;
	mds->prettyname = name;
	elem_len = align_up(elem_len, sizeof(long)) + MEMPOOL_INITIAL_OFFSET;

	if (nostraddle) {
		int elems_per_page = PGSIZE_2MB / elem_len;
		nr_pages = div_up(nr_elems, elems_per_page);
		mds->buf = ix_alloc_pages(nr_pages);
		assert(mds->buf);
	} else {
		nr_pages = PGN_2MB(nr_elems * elem_len + PGMASK_2MB);
		nr_elems = nr_pages * PGSIZE_2MB / elem_len;
		mds->buf = ix_alloc_pages(nr_pages);
	}

	mds->nr_pages = nr_pages;
	mds->nr_elems = nr_elems;
	mds->elem_len = elem_len;
	mds->chunk_size = chunk_size;
	mds->nostraddle = nostraddle;

	spin_lock_init(&mds->lock);

	if (mds->buf == MAP_FAILED || mds->buf == 0) {
		log_err("mempool alloc failed\n");
		printf("Unable to create mempool datastore %s\n", name);

		panic("unable to create mempool datstore for %s\n", name);
		return -ENOMEM;
	}

	if (nostraddle) {
		int elems_per_page = PGSIZE_2MB / elem_len;
		mempool_init_buf_with_pages(mds, elems_per_page, nr_pages, elem_len);
	} else
		mempool_init_buf_with_pages(mds, nr_elems, 1, elem_len);

	mds->next_ds = mempool_all_datastores;
	mempool_all_datastores = mds;

	return 0;
}


/**
 * mempool_create - initializes a memory pool
 * @nr_elems: the minimum number of elements in the pool
 * @elem_len: the length of each element
 *
 * NOTE: mempool_create() will create a pool with at least @nr_elems,
 * but possibily more depending on page alignment.
 *
 * Returns 0 if successful, otherwise fail.
 */
int mempool_create(struct mempool *m, struct mempool_datastore *mds)
{

	if (mds->magic != MEMPOOL_MAGIC)
		panic("mempool_create when datastore does not exist\n");

	assert(mds->magic == MEMPOOL_MAGIC);

	if (m->magic != 0)
		panic("mempool_create attempt to call twice (ds=%s)\n", mds->prettyname);

	assert(m->magic == 0);
	m->magic = MEMPOOL_MAGIC;
	m->buf = mds->buf;
	m->datastore = mds;
	m->head = NULL;
	m->nr_elems = mds->nr_elems;
	m->elem_len = mds->elem_len;
	m->nostraddle = mds->nostraddle;
	m->chunk_size = mds->chunk_size;
	return 0;
}

/**
 * mempool_destroy - cleans up a memory pool and frees memory
 * @m: the memory pool
 */
void mempool_destroy(struct mempool *m)
{
	/* TODO: implement me */
}
