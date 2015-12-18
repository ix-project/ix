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
 * mempool.h - a fast fixed-sized memory pool allocator
 */

#pragma once

#include <ix/stddef.h>
#include <ix/mem.h>
#include <assert.h>
#include <ix/cpu.h>
#include <ix/ethfg.h>
#include <ix/log.h>


#ifndef __KERNEL__
#error "wrong include .. this is for IX kernel only"
#endif


#define MEMPOOL_DEFAULT_CHUNKSIZE 128


#undef  DEBUG_MEMPOOL

#ifdef DEBUG_MEMPOOL
#define MEMPOOL_INITIAL_OFFSET (sizeof(void *))
#else
#define MEMPOOL_INITIAL_OFFSET (0)
#endif

struct mempool_hdr {
	struct mempool_hdr *next;
	struct mempool_hdr *next_chunk;
} __packed;


// one per data type
struct mempool_datastore {
	uint64_t                 magic;
	spinlock_t               lock;
	struct mempool_hdr      *chunk_head;
	void			*buf;
	int			nr_pages;
	uint32_t                nr_elems;
	size_t                  elem_len;
	int                     nostraddle;
	int                     chunk_size;
	int                     num_chunks;
	int                     free_chunks;
	int64_t                 num_locks;
	const char             *prettyname;
	struct mempool_datastore *next_ds;
#ifdef __KERNEL__
	void 			*iomap_addr;
	uintptr_t		iomap_offset;
#endif
};


struct mempool {
	// hot fields:
	struct mempool_hdr	*head;
	int                     num_free;
	size_t                  elem_len;

	uint64_t                 magic;
	void			*buf;
	struct mempool_datastore *datastore;
	struct mempool_hdr      *private_chunk;
//	int			nr_pages;
	int                     sanity;
	uint32_t                nr_elems;
	int                     nostraddle;
	int                     chunk_size;
#ifdef __KERNEL__
	void 			*iomap_addr;
	uintptr_t		iomap_offset;
#endif
};
#define MEMPOOL_MAGIC   0x12911776


/*
 * mempool sanity macros ensures that pointers between mempool-allocated objects are of identical type
 */

#define MEMPOOL_SANITY_GLOBAL    0
#define MEMPOOL_SANITY_PERCPU    1

#ifdef DEBUG_MEMPOOL


#define MEMPOOL_SANITY_OBJECT(_a) do {\
	struct mempool **hidden = (struct mempool **)_a;\
	assert(hidden[-1] && hidden[-1]->magic == MEMPOOL_MAGIC); \
	} while (0);

static inline int __mempool_get_sanity(void *a)
{
	struct mempool **hidden = (struct mempool **)a;
	struct mempool *p = hidden[-1];
	return p->sanity;
}


#define MEMPOOL_SANITY_ACCESS(_obj)   do { \
	MEMPOOL_SANITY_OBJECT(_obj);\
	} while (0);

#define  MEMPOOL_SANITY_LINK(_a, _b) do {\
	MEMPOOL_SANITY_ACCESS(_a);\
	MEMPOOL_SANITY_ACCESS(_b);\
	int sa = __mempool_get_sanity(_a);\
	int sb = __mempool_get_sanity(_b);\
	assert (sa == sb);\
	}  while (0);

#else
#define MEMPOOL_SANITY_ISPERFG(_a)
#define MEMPOOL_SANITY_ACCESS(_a)
#define MEMPOOL_SANITY_LINK(_a, _b)
#endif


/**
 * mempool_alloc - allocates an element from a memory pool
 * @m: the memory pool
 *
 * Returns a pointer to the allocated element or NULL if unsuccessful.
 */
extern void *mempool_alloc_2(struct mempool *m);
static inline void *mempool_alloc(struct mempool *m)
{
	struct mempool_hdr *h = m->head;

	if (likely(h)) {
		m->head = h->next;
		m->num_free--;
		return (void *) h;
	} else {
		return mempool_alloc_2(m);
	}
}

/**
 * mempool_free - frees an element back in to a memory pool
 * @m: the memory pool
 * @ptr: the element
 *
 * NOTE: Must be the same memory pool that it was allocated from
 */
extern void mempool_free_2(struct mempool *m, void *ptr);
static inline void mempool_free(struct mempool *m, void *ptr)
{
	struct mempool_hdr *elem = (struct mempool_hdr *) ptr;
	MEMPOOL_SANITY_ACCESS(ptr);

	if (likely(m->num_free < m->chunk_size)) {
		m->num_free++;
		elem->next = m->head;
		m->head = elem;
	} else
		mempool_free_2(m, ptr);
}

static inline void *mempool_idx_to_ptr(struct mempool *m, uint32_t idx, int elem_len)
{
	void *p;
	assert(idx < m->nr_elems);
	assert(!m->nostraddle);
	p = m->buf + elem_len * idx + MEMPOOL_INITIAL_OFFSET;
	MEMPOOL_SANITY_ACCESS(p);
	return p;
}

static inline uintptr_t mempool_ptr_to_idx(struct mempool *m, void *p, int elem_len)
{
	uintptr_t x = (uintptr_t)p - (uintptr_t)m->buf - MEMPOOL_INITIAL_OFFSET;
	x = x / elem_len;
	assert(x < m->nr_elems);
	return x;
}


extern int mempool_create_datastore(struct mempool_datastore *m, int nr_elems, size_t elem_len, int nostraddle, int chunk_size, const char *prettyname);
extern int mempool_create(struct mempool *m, struct mempool_datastore *mds, int16_t sanity_type, int16_t sanity_id);
extern void mempool_destroy(struct mempool *m);


#ifdef __KERNEL__

/**
 * mempool_pagemem_to_iomap - get the IOMAP address of a mempool entry
 * @m: the mempool
 * @ptr: a pointer to the target entry
 *
 * Returns an IOMAP address.
 */
static inline void *mempool_pagemem_to_iomap(struct mempool *m, void *ptr)
{
	assert(m->iomap_offset);
	return (void *)((uintptr_t) ptr + m->iomap_offset);
}

static inline void *mempool_iomap_to_ptr(struct mempool *m, void *ioptr)
{
	assert(m->iomap_offset);
	return ((void *)((uintptr_t)(ioptr) - m->iomap_offset));
}


//extern int
//mempool_pagemem_create(struct mempool *m, int nr_elems, size_t elem_len,int16_t sanity_type, int16_t sanity_id);
extern int mempool_pagemem_map_to_user(struct mempool_datastore *m);
extern void mempool_pagemem_destroy(struct mempool_datastore *m);

#endif /* __KERNEL__ */

