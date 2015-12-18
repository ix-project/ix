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
 */

#include <ix/stddef.h>
#include <ix/mem.h>
#include <ix/vm.h>
#include <ix/errno.h>

#include <dune.h>

DEFINE_SPINLOCK(vm_lock);
static uintptr_t vm_iomapk_pos = MEM_USER_IOMAPK_BASE_ADDR;

extern int __dune_vm_page_walk(ptent_t *dir, void *start_va, void *end_va,
			       page_walk_cb cb, const void *arg, int level,
			       int create);

struct vm_arg {
	ptent_t perm;
	physaddr_t pa;
	virtaddr_t va;
};

static void __vm_unmap(void *addr, int nr, int size)
{
	dune_vm_unmap(pgroot, addr, (size_t) nr * (size_t) size);
}

/**
 * vm_unmap - unmaps virtual memory
 * @addr: the starting address
 * @nr: the number of pages
 * @size: the size of each page
 */
void vm_unmap(void *addr, int nr, int size)
{
	spin_lock(&vm_lock);
	__vm_unmap(addr, nr, size);
	spin_unlock(&vm_lock);
}

static int __vm_map_phys_helper(const void *arg, ptent_t *pte, void *va)
{
	struct vm_arg *args = (struct vm_arg *)arg;
	size_t pos = (virtaddr_t) va - args->va;

	*pte = PTE_ADDR(args->pa + pos) | args->perm;
	return 0;
}

int __vm_map_phys(physaddr_t pa, virtaddr_t va,
		  int nr, int size, int perm)
{
	int ret;
	struct vm_arg args;
	size_t len = nr * size;
	int create;

	if (!(perm & VM_PERM_R))
		return -EINVAL;

	args.perm = PTE_P;

	switch (size) {
	case PGSIZE_4KB:
		create = CREATE_NORMAL;
		break;
	case PGSIZE_2MB:
		create = CREATE_BIG;
		args.perm |= PTE_PS;
		break;
	case PGSIZE_1GB:
		create = CREATE_BIG_1GB;
		args.perm |= PTE_PS;
		break;
	default:
		return -EINVAL;
	}

	if (perm & VM_PERM_W)
		args.perm |= PTE_W;
	if (!(perm & VM_PERM_X))
		args.perm |= PTE_NX;
	if (perm & VM_PERM_U)
		args.perm |= PTE_U;

	args.pa = pa;
	args.va = va;

	ret = __dune_vm_page_walk(pgroot, (void *) va,
				  (void *)(va + len - 1),
				  &__vm_map_phys_helper,
				  (void *) &args, 3, create);

	/* cleanup partial mappings */
	if (ret)
		__vm_unmap((void *) va, nr, len);

	return ret;
}

/**
 * vm_map_phys - map physical memory to a virtual address
 * @pa: the physical address
 * @va: the virtual address
 * @nr: the number of pages
 * @size: the page size
 * @perm: the mapping permissions
 *
 * Returns zero if successful, otherwise fail.
 */
int vm_map_phys(physaddr_t pa, virtaddr_t va,
		int nr, int size, int perm)
{
	int ret;
	spin_lock(&vm_lock);
	ret = __vm_map_phys(pa, va, nr, size, perm);
	spin_unlock(&vm_lock);

	return ret;
}

/**
 * vm_map_to_user - makes kernel memory available to the user
 * @kern_addr: a pointer to kernel memory (must be aligned)
 * @nr: the number of pages
 * @size: the size of each page
 * @perm: the permission of the new mapping
 *
 * NOTE: vm_lock must be held.
 *
 * Returns an address in the IOMAP region if successful, otherwise fail.
 */
void *vm_map_to_user(void *kern_addr, int nr, int size, int perm)
{
	int ret;
	virtaddr_t va;

	perm |= VM_PERM_U;

	spin_lock(&vm_lock);
	va = align_up(vm_iomapk_pos, size);

	ret = __vm_map_phys((physaddr_t) kern_addr, va, nr, size, perm);
	if (ret) {
		spin_unlock(&vm_lock);
		return NULL;
	}

	vm_iomapk_pos = va + size * nr;
	spin_unlock(&vm_lock);

	return (void *) va;
}

static int __vm_is_mapped_helper(const void *arg, ptent_t *pte, void *va)
{
	return 1;
}

/**
 * __vm_is_mapped - determines if there are any mappings in the region
 * @addr: the start address
 * @len: the length of the region.
 *
 * Returns true if a mapping exists, otherwise false.
 */
bool __vm_is_mapped(void *addr, size_t len)
{
	char *pos = (char *) addr;
	return  __dune_vm_page_walk(pgroot, pos, pos + len - 1,
				    &__vm_is_mapped_helper,
				    (void *) NULL, 3, CREATE_NONE) > 0;
}

