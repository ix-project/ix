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
 * mem.c - memory management
 */

#include <ix/stddef.h>
#include <ix/mem.h>
#include <ix/errno.h>
#include <ix/atomic.h>
#include <ix/lock.h>
#include <ix/vm.h>
#include <ix/log.h>

#include <dune.h>

#include <stdlib.h>
#include <sys/mman.h>
#include <asm/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include  <signal.h>


#if !defined(MAP_HUGE_2MB) || !defined(MAP_HUGE_1GB)
#warning "Your system does not support MAP_HUGETLB page sizes"
#endif

/*
 * Current Mapping Strategy:
 * 2 megabyte and 1 gigabyte pages grow down from MEM_PHYS_BASE_ADDR
 * 4 kilobyte pages are allocated from the standard mmap region
 */
static DEFINE_SPINLOCK(mem_lock);
static uintptr_t mem_pos = MEM_PHYS_BASE_ADDR;


void sigbus_error(int sig)
{
	log_err("FATAL - mbind is tricking you ... no numa pages ... aborting\n");
	abort();
}

void *__mem_alloc_pages(void *base, int nr, int size,
			struct bitmask *mask, int numa_policy)
{
	void *vaddr;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	size_t len = nr * size;

	switch (size) {
	case PGSIZE_4KB:
		break;
	case PGSIZE_2MB:
		flags |= MAP_HUGETLB | MAP_FIXED;
#ifdef MAP_HUGE_2MB
		flags |= MAP_HUGE_2MB;
#endif
		break;
	case PGSIZE_1GB:
#ifdef MAP_HUGE_1GB
		flags |= MAP_HUGETLB | MAP_HUGE_1GB | MAP_FIXED;
#else
		return MAP_FAILED;
#endif
		break;
	default: /* fail on other sizes */
		return MAP_FAILED;
	}

	vaddr = mmap(base, len, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (vaddr == MAP_FAILED)
		return MAP_FAILED;

	if (mbind(vaddr, len, numa_policy, mask ? mask->maskp : NULL,
		  mask ? mask->size : 0, MPOL_MF_STRICT))
		goto fail;

	if (vm_map_phys((physaddr_t) vaddr, (virtaddr_t) vaddr, nr, size,
			VM_PERM_R | VM_PERM_W))
		goto fail;

	sighandler_t s = signal(SIGBUS, sigbus_error);
	*(uint64_t *)vaddr = 0;
	signal(SIGBUS, s);

	return vaddr;

fail:
	munmap(vaddr, len);
	return MAP_FAILED;
}

void *__mem_alloc_pages_onnode(void *base, int nr, int size, int node)
{
	void *vaddr;
	struct bitmask *mask = numa_allocate_nodemask();

	numa_bitmask_setbit(mask, node);
	vaddr = __mem_alloc_pages(base, nr, size, mask, MPOL_BIND);
	numa_bitmask_free(mask);

	return vaddr;
}

/**
 * mem_alloc_pages - allocates pages of memory
 * @nr: the number of pages to allocate
 * @size: the page size (4KB, 2MB, or 1GB)
 * @mask: the numa node mask
 * @numa_policy: the numa policy
 *
 * Returns a pointer (virtual address) to a page, or NULL if fail.
 */
void *mem_alloc_pages(int nr, int size, struct bitmask *mask, int numa_policy)
{
	void *base;

	switch (size) {
	case PGSIZE_4KB:
		base = NULL;
	case PGSIZE_2MB:
		spin_lock(&mem_lock);
		mem_pos -= PGSIZE_2MB * nr;
		base = (void *) mem_pos;
		spin_unlock(&mem_lock);
		break;
	case PGSIZE_1GB:
		spin_lock(&mem_lock);
		mem_pos = align_down(mem_pos - PGSIZE_1GB * nr, PGSIZE_1GB);
		base = (void *) mem_pos;
		spin_unlock(&mem_lock);
		break;
	default:
		return MAP_FAILED;
	}

	return __mem_alloc_pages(base, nr, size, mask, numa_policy);
}

/**
 * mem_alloc_pages_onnode - allocates pages on a given numa node
 * @nr: the number of pages
 * @size: the page size (4KB, 2MB, or 1GB)
 * @numa_node: the numa node to allocate the pages from
 * @numa_policy: how strictly to take @numa_node
 *
 * Returns a pointer (virtual address) to a page or NULL if fail.
 */
void *mem_alloc_pages_onnode(int nr, int size, int node, int numa_policy)
{
	void *vaddr;
	struct bitmask *mask = numa_allocate_nodemask();

	numa_bitmask_setbit(mask, node);
	vaddr = mem_alloc_pages(nr, size, mask, numa_policy);
	numa_bitmask_free(mask);

	return vaddr;
}

/**
 * mem_free_pages - frees pages of memory
 * @addr: a pointer to the start of the pages
 * @nr: the number of pages
 * @size: the page size (4KB, 2MB, or 1GB)
 */
void mem_free_pages(void *addr, int nr, int size)
{
	vm_unmap(addr, nr, size);
	munmap(addr, nr * size);
}

#define PAGEMAP_PGN_MASK	0x7fffffffffffffULL
#define PAGEMAP_FLAG_PRESENT	(1ULL << 63)
#define PAGEMAP_FLAG_SWAPPED	(1ULL << 62)
#define PAGEMAP_FLAG_FILE	(1ULL << 61)
#define PAGEMAP_FLAG_SOFTDIRTY	(1ULL << 55)

/**
 * mem_lookup_page_machine_addrs - determines the machine address of pages
 * @addr: a pointer to the start of the pages (must be @size aligned)
 * @nr: the number of pages
 * @size: the page size (4KB, 2MB, or 1GB)
 * @maddrs: a pointer to an array of machine addresses (of @nr elements)
 *
 * @maddrs is filled with each page machine address.
 *
 * Returns 0 if successful, otherwise failure.
 */
int mem_lookup_page_machine_addrs(void *addr, int nr, int size,
				  machaddr_t *maddrs)
{
	int fd, i, ret = 0;
	uint64_t tmp;

	/*
	 * 4 KB pages could be swapped out by the kernel, so it is not
	 * safe to get a machine address. If we later decide to support
	 * 4KB pages, then we need to mlock() the page first.
	 */
	if (size == PGSIZE_4KB)
		return -EINVAL;

	fd = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0)
		return -EIO;

	for (i = 0; i < nr; i++) {
		if (lseek(fd, (((uintptr_t) addr + (i * size)) /
			       PGSIZE_4KB) * sizeof(uint64_t), SEEK_SET) ==
		    (off_t) - 1) {
			ret = -EIO;
			goto out;
		}

		if (read(fd, &tmp, sizeof(uint64_t)) <= 0) {
			ret = -EIO;
			goto out;
		}

		if (!(tmp & PAGEMAP_FLAG_PRESENT)) {
			ret = -ENODEV;
			goto out;
		}

		maddrs[i] = (tmp & PAGEMAP_PGN_MASK) * PGSIZE_4KB;
	}

out:
	close(fd);
	return ret;
}

