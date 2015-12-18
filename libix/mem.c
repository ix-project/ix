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
 * mem.c - memory management routines
 *
 * FIXME: this is really only a placeholder right now.
 */

#include <ix/stddef.h>
#include <pthread.h>
#include "ix.h"

static uintptr_t ixmem_pos = MEM_USER_IOMAPM_BASE_ADDR;
static pthread_mutex_t ixmem_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * ixmem_alloc_pages - allocates 2MB pages
 * @nrpages: the number of pages
 *
 * Returns a start address, or NULL if fail.
 */
void *ix_alloc_pages(int nrpages)
{
	void *addr;
	int ret;

	pthread_mutex_lock(&ixmem_mutex);
	addr = (void *) ixmem_pos;
	ixmem_pos += PGSIZE_2MB * nrpages;
	pthread_mutex_unlock(&ixmem_mutex);

	ret = sys_mmap(addr, nrpages, PGSIZE_2MB, VM_PERM_R | VM_PERM_W);
	if (ret)
		return NULL;

	return addr;
}

/**
 * ixmem_free_pages - frees 2MB pages
 * @addr: the start address
 * @nrpages: the number of pages
 */
void ix_free_pages(void *addr, int nrpages)
{
	sys_unmap(addr, nrpages, PGSIZE_2MB);
}

