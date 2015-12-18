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
 * mem.h - memory management
 */

#pragma once

#include <ix/types.h>
#include <ix/lock.h>

#include <numaif.h>
#include <numa.h>

enum {
	PGSHIFT_4KB = 12,
	PGSHIFT_2MB = 21,
	PGSHIFT_1GB = 30,
};

enum {
	PGSIZE_4KB = (1 << PGSHIFT_4KB), /* 4096 bytes */
	PGSIZE_2MB = (1 << PGSHIFT_2MB), /* 2097152 bytes */
	PGSIZE_1GB = (1 << PGSHIFT_1GB), /* 1073741824 bytes */
};

#define PGMASK_4KB	(PGSIZE_4KB - 1)
#define PGMASK_2MB	(PGSIZE_2MB - 1)
#define PGMASK_1GB	(PGSIZE_1GB - 1)

/* page numbers */
#define PGN_4KB(la)	(((uintptr_t) (la)) >> PGSHIFT_4KB)
#define PGN_2MB(la)	(((uintptr_t) (la)) >> PGSHIFT_2MB)
#define PGN_1GB(la)	(((uintptr_t) (la)) >> PGSHIFT_1GB)

#define PGOFF_4KB(la)	(((uintptr_t) (la)) & PGMASK_4KB)
#define PGOFF_2MB(la)	(((uintptr_t) (la)) & PGMASK_2MB)
#define PGOFF_1GB(la)	(((uintptr_t) (la)) & PGMASK_1GB)

#define PGADDR_4KB(la)	(((uintptr_t) (la)) & ~((uintptr_t) PGMASK_4KB))
#define PGADDR_2MB(la)	(((uintptr_t) (la)) & ~((uintptr_t) PGMASK_2MB))
#define PGADDR_1GB(la)	(((uintptr_t) (la)) & ~((uintptr_t) PGMASK_1GB))

/*
 * numa policy values: (defined in numaif.h)
 * MPOL_DEFAULT - use the process' global policy
 * MPOL_BIND - force the numa mask
 * MPOL_PREFERRED - use the numa mask but fallback on other memory
 * MPOL_INTERLEAVE - interleave nodes in the mask (good for throughput)
 */

typedef unsigned long machaddr_t; /* host physical addresses */
typedef unsigned long physaddr_t; /* guest physical addresses */
typedef unsigned long virtaddr_t; /* guest virtual addresses */

#define MEM_IX_BASE_ADDR		0x70000000   /* the IX ELF is loaded here */
#define MEM_PHYS_BASE_ADDR		0x4000000000 /* memory is allocated here (2MB going up, 1GB going down) */
#define MEM_USER_DIRECT_BASE_ADDR	0x7000000000 /* start of direct user mappings (P = V) */
#define MEM_USER_DIRECT_END_ADDR	0x7F00000000 /* end of direct user mappings (P = V) */
#define MEM_USER_IOMAPM_BASE_ADDR	0x8000000000 /* user mappings controlled by IX */
#define MEM_USER_IOMAPM_END_ADDR	0x100000000000 /* end of user mappings controlled by IX */
#define MEM_USER_IOMAPK_BASE_ADDR	0x100000000000 /* batched system calls and network mbuf's */
#define MEM_USER_IOMAPK_END_ADDR	0x101000000000 /* end of batched system calls and network mbuf's */

#define MEM_USER_START			MEM_USER_DIRECT_BASE_ADDR
#define MEM_USER_END			MEM_USER_IOMAPM_END_ADDR

#define MEM_ZC_USER_START		MEM_USER_IOMAPM_BASE_ADDR
#define MEM_ZC_USER_END			MEM_USER_IOMAPK_END_ADDR

#ifndef MAP_FAILED
#define MAP_FAILED	((void *) -1)
#endif

#ifdef __KERNEL__

extern void *
__mem_alloc_pages(void *base, int nr, int size, struct bitmask *mask, int numa_policy);
extern void *__mem_alloc_pages_onnode(void *base, int nr, int size, int node);

extern void *
mem_alloc_pages(int nr, int size, struct bitmask *mask, int numa_policy);
extern void *
mem_alloc_pages_onnode(int nr, int size, int node, int numa_policy);
extern void mem_free_pages(void *addr, int nr, int size);
extern int mem_lookup_page_machine_addrs(void *addr, int nr, int size,
		machaddr_t *maddrs);

/**
 * mem_alloc_page - allocates a page of memory
 * @size: the page size (4KB, 2MB, or 1GB)
 * @numa_node: the numa node to allocate the page from
 * @numa_policy: how strictly to take @numa_node
 *
 * Returns a pointer (virtual address) to a page or NULL if fail.
 */
static inline void *mem_alloc_page(int size, int numa_node, int numa_policy)
{
	return mem_alloc_pages_onnode(1, size, numa_node, numa_policy);
}

/**
 * mem_alloc_page_local - allocates a page of memory on the local numa node
 * @size: the page size (4KB, 2MB, or 1GB)
 *
 * NOTE: This variant is best effort only. If no pages are available on the
 * local node, a page from another NUMA node could be used instead. If you
 * need stronger assurances, use mem_alloc_page().
 *
 * Returns a pointer (virtual address) to a page or NULL if fail.
 */
static inline void *mem_alloc_page_local(int size)
{
	return mem_alloc_pages(1, size, NULL, MPOL_PREFERRED);
}

/**
 * mem_free_page - frees a page of memory
 * @addr: a pointer to the page
 * @size: the page size (4KB, 2MB, or 1GB)
 */
static inline void mem_free_page(void *addr, int size)
{
	return mem_free_pages(addr, 1, size);
}

/**
 * mem_lookup_page_machine_addr - determines the machine address of a page
 * @addr: a pointer to the page
 * @size: the page size (4KB, 2MB, or 1GB)
 * @maddr: a pointer to store the result
 *
 * Returns 0 if successful, otherwise failure.
 */
static inline int
mem_lookup_page_machine_addr(void *addr, int size, machaddr_t *maddr)
{
	return mem_lookup_page_machine_addrs(addr, 1, size, maddr);
}


/** mem_prefetch
 */
static inline void mem_prefetch(void *addr)
{
	__builtin_prefetch(addr, 1);
}

#endif /* __KERNEL__ */

