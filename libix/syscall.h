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
 * syscall.h - system call support
 */

#pragma once

#include <ix/syscall.h>
#include <ix/mem.h>
#include <ix/vm.h>
#include "syscall_raw.h"

static inline int sys_bpoll(struct bsys_desc *d, unsigned int nr)
{
	return (int) SYSCALL(SYS_BPOLL, d, nr);
}

static inline int sys_bcall(struct bsys_desc *d, unsigned int nr)
{
	return (int) SYSCALL(SYS_BCALL, d, nr);
}

static inline void *sys_baddr(void)
{
	return (struct bsys_arr *) SYSCALL(SYS_BADDR);
}

static inline int sys_mmap(void *addr, int nr, int size, int perm)
{
	return (int) SYSCALL(SYS_MMAP, addr, nr, size, perm);
}

static inline int sys_unmap(void *addr, int nr, int size)
{
	return (int) SYSCALL(SYS_MUNMAP, addr, nr, size);
}

static inline int sys_spawnmode(bool spawn_cores)
{
	return (int) SYSCALL(SYS_SPAWNMODE, spawn_cores);
}

static inline int sys_nrcpus(void)
{
	return (int) SYSCALL(SYS_NRCPUS);
}

static inline int sys_timer_init(void * addr)
{
	return (int) SYSCALL(SYS_TIMER_INIT, addr);
}

static inline int sys_timer_ctl(int timer_id, uint64_t delay)
{
	return (int) SYSCALL(SYS_TIMER_CTL, timer_id, delay);
}
