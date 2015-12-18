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
 * page.h - page-level memory management
 */

#pragma once

#include <ix/stddef.h>
#include <ix/mem.h>
#include <ix/atomic.h>
#include <ix/cpu.h>

struct page_ent {
	machaddr_t maddr;
	atomic_t refcnt;
	uint32_t flags;
};

#define	PAGE_FLAG_WILL_FREE	0x1
#define	PAGE_FLAG_CAN_FREE	0x2

extern struct page_ent page_tbl[];
DECLARE_PERCPU(int32_t, page_refs[]);

#define PAGE_NUM(addr) \
	PGN_2MB((uintptr_t) (addr) - (uintptr_t) MEM_PHYS_BASE_ADDR)

/**
 * is_page - determines an address is inside page memory
 * @addr: the address
 *
 * Returns true if the address is inside page memory.
 */
static inline bool is_page(void *addr)
{
	return ((uintptr_t) addr >= MEM_PHYS_BASE_ADDR &&
		(uintptr_t) addr < MEM_USER_START);
}

/**
 * is_page_region - determines if an address range is inside page memory
 * @addr: the base address
 * @len: the length of the region
 *
 * Returns true if the region is inside page memory, otherwise false.
 */
static inline bool is_page_region(void *addr, size_t len)
{
	if (len > MEM_USER_START - MEM_PHYS_BASE_ADDR ||
	    (uintptr_t) addr < MEM_PHYS_BASE_ADDR ||
	    (uintptr_t) addr + len > MEM_USER_START)
		return false;

	return true;
}

/**
 * page_machaddr - gets the machine address of a page
 * @addr: the address of (or in) the page
 *
 * NOTE: This variant is unsafe if a reference to the page is not already held.
 *
 * Returns the machine address of the page plus the offset within the page.
 */
static inline machaddr_t page_machaddr(void *addr)
{
	return page_tbl[PAGE_NUM(addr)].maddr + PGOFF_2MB(addr);
}

/**
 * page_get - pins a memory page
 * @addr: the address of (or in) the page
 *
 * Returns the machine address of the page plus the offset within the page.
 */
static inline machaddr_t page_get(void *addr)
{
	unsigned long idx = PAGE_NUM(addr);
	struct page_ent *ent = &page_tbl[idx];

	if (unlikely(ent->flags & PAGE_FLAG_WILL_FREE))
		atomic_inc(&ent->refcnt);
	else
		percpu_get(page_refs[idx])++;

	return ent->maddr + PGOFF_2MB(addr);
}

extern void __page_put_slow(void *addr);

/**
 * page_put - unpins an iomap memory page
 * @addr: the address of (or in) the page
 */
static inline void page_put(void *addr)
{
	unsigned long idx = PAGE_NUM(addr);
	struct page_ent *ent = &page_tbl[idx];

	if (unlikely(ent->flags & PAGE_FLAG_WILL_FREE))
		__page_put_slow(addr);
	else
		percpu_get(page_refs[idx])--;
}

extern void *
page_alloc_contig_on_node(unsigned int nr, int numa_node);
extern void page_free(void *addr);
extern void page_free_contig(void *addr, unsigned int nr);

/**
 * page_alloc_contig - allocate contiguous pages
 * @nr: the number of pages to allocate
 *
 * Returns an address, or NULL if fail.
 */
static inline void *page_alloc_contig(unsigned int nr)
{
	return page_alloc_contig_on_node(nr, percpu_get(cpu_numa_node));
}

/**
 * page_alloc_on_node - allocates a page on the given numa node
 * @numa_node: the target numa node
 *
 * If @numa_node is -1, then any numa node can be used.
 *
 * Returns an address, or NULL if fail.
 */
static inline void *page_alloc_on_node(int numa_node)
{
	return page_alloc_contig_on_node(1, numa_node);
}

/**
 * page_alloc - allocates a page
 *
 * Returns an address, or NULL if fail.
 */
static inline void *page_alloc(void)
{
	return page_alloc_contig_on_node(1, percpu_get(cpu_numa_node));
}

