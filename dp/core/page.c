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
 * page.c - page-level memory management
 *
 * FIXME: Performance and address-space conservation could be
 * improved by maintaining free-lists of previously allocated
 * pages.
 *
 * FIXME: Need to add support for 1GB pages. May also consider
 * 4KB pages.
 */

#include <ix/stddef.h>
#include <ix/mem.h>
#include <ix/atomic.h>
#include <ix/page.h>
#include <ix/log.h>

#define NUM_PAGES PGN_2MB(MEM_USER_START - MEM_PHYS_BASE_ADDR)

struct page_ent page_tbl[NUM_PAGES];
DEFINE_PERCPU(int32_t, page_refs[NUM_PAGES]);

static atomic64_t page_pos = ATOMIC_INIT(MEM_PHYS_BASE_ADDR);

static inline struct page_ent *addr_to_page_ent(void *addr)
{
	return &page_tbl[PAGE_NUM(addr)];
}

/**
 * __page_put_slow - the slow path for decrementing page refernces
 * @addr: the address
 *
 * This function actually frees the page (if possible).
 */
void __page_put_slow(void *addr)
{
	bool no_refs;
	struct page_ent *ent = addr_to_page_ent(addr);

	no_refs = atomic_dec_and_test(&ent->refcnt);

	/* can we free the page yet? */
	if (!no_refs || !(ent->flags & PAGE_FLAG_CAN_FREE))
		return;

	mem_free_page((void *) PGADDR_2MB(addr), PGSIZE_2MB);
}

/**
 * page_alloc_contig_on_node - allocates a guest-physically contiguous set of 2MB pages
 * @nr: the number of pages
 * @numa_node: the numa node
 *
 * Returns an address, or NULL if fail.
 */
void *page_alloc_contig_on_node(unsigned int nr, int numa_node)
{
	int ret, i;

	void *base = (void *) atomic64_fetch_and_add(&page_pos, nr * PGSIZE_2MB);

	if ((uintptr_t) base >= MEM_USER_START)
		return NULL;


	base = __mem_alloc_pages_onnode(base, nr, PGSIZE_2MB, numa_node);
	if (!base)
		return NULL;

	for (i = 0; i < nr; i++) {

		void *pos = (void *)((uintptr_t) base + i * PGSIZE_2MB);
		struct page_ent *ent = addr_to_page_ent(pos);
		*((int *) pos) = 0; /* force a fault */
		ret = mem_lookup_page_machine_addr(pos, PGSIZE_2MB, &ent->maddr);
		if (ret) {
			mem_free_pages(base, nr, PGSIZE_2MB);
			log_err("page: failed to get machine address for %p\n", pos);
			return NULL;
		}
	}

	return base;
}

/**
 * page_free - frees a page
 * @addr: the address of (or within) the page
 */
void page_free(void *addr)
{
	struct page_ent *ent = addr_to_page_ent(addr);

	ent->flags |= PAGE_FLAG_WILL_FREE;

	/* FIXME: need RCU infrastructure to complete this. */
}

/**
 * page_free_contig - frees a contiguous group of pages
 * @addr: the address of (or within) the first page
 * @nr: the number of pages
 */
void page_free_contig(void *addr, unsigned int nr)
{
	int i;

	for (i = 0; i < nr; i++) {
		page_free((void *)((uintptr_t) addr + PGSIZE_2MB * i));
	}
}

