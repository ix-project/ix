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
 * vm.h - virtual memory management support
 *
 * NOTE: right now we mostly use Dune for these operations, so this file
 * serves only to augment those capabilities.
 */

#pragma once

#define VM_PERM_R	0x1
#define VM_PERM_W	0x2
#define VM_PERM_X	0x4
#define VM_PERM_U	0x8

#ifdef __KERNEL__

#include <ix/mem.h>
#include <ix/lock.h>

/* FIXME: this should be defined in inc/asm */
#include <mmu-x86.h>

DECLARE_SPINLOCK(vm_lock);

/* FIXME: a bunch of gross hacks until we can better integrate libdune */
#define UINT64(x) ((uint64_t) x)
#define CAST64(x) ((uint64_t) x)
typedef uint64_t ptent_t;
#define NPTBITS	9
#ifndef pgroot
extern ptent_t *pgroot;
#endif

/**
 * vm_lookup_phys - determine a physical address from a virtual address
 * @virt: the virtual address
 * @pgsize: the size of the page at the address (must be correct).
 *
 * Returns a physical address.
 */
static inline physaddr_t vm_lookup_phys(void *virt, int pgsize)
{
	ptent_t *dir = pgroot;
	ptent_t pte;

	pte = dir[PDX(3, virt)];
	if (!(PTE_FLAGS(pte) & PTE_P))
		return 0;

	dir = (ptent_t *) PTE_ADDR(pte);
	pte = dir[PDX(2, virt)];
	if (!(PTE_FLAGS(pte) & PTE_P))
		return 0;
	if (pgsize == PGSIZE_1GB)
		return (physaddr_t) PTE_ADDR(pte);

	dir = (ptent_t *) PTE_ADDR(pte);
	pte = dir[PDX(1, virt)];
	if (!(PTE_FLAGS(pte) & PTE_P))
		return 0;
	if (pgsize == PGSIZE_2MB)
		return (physaddr_t) PTE_ADDR(pte);

	dir = (ptent_t *) PTE_ADDR(pte);
	pte = dir[PDX(0, virt)];
	if (!(PTE_FLAGS(pte) & PTE_P))
		return 0;
	return (physaddr_t) PTE_ADDR(pte);
}

extern int __vm_map_phys(physaddr_t pa, virtaddr_t va,
			 int nr, int size, int perm);
extern bool __vm_is_mapped(void *addr, size_t len);

extern int vm_map_phys(physaddr_t pa, virtaddr_t va,
		       int nr, int size, int perm);
extern void *vm_map_to_user(void *kern_addr, int nr, int size, int perm);
extern void vm_unmap(void *addr, int nr, int size);

#endif /* __KERNEL__ */

