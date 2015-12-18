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
 * lock.h - locking primitives
 */

#pragma once

#include <asm/cpu.h>
#include <ix/types.h>

#define SPINLOCK_INITIALIZER {.locked = 0}
#define DEFINE_SPINLOCK(name) \
	spinlock_t name = SPINLOCK_INITIALIZER
#define DECLARE_SPINLOCK(name) \
	extern spinlock_t name

/**
 * spin_lock_init - prepares a spin lock for use
 * @l: the spin lock
 */
static inline void spin_lock_init(spinlock_t *l)
{
	l->locked = 0;
}

/**
 * spin_lock - takes a spin lock
 * @l: the spin lock
 */
static inline void spin_lock(spinlock_t *l)
{
	while (__sync_lock_test_and_set(&l->locked, 1)) {
		while (l->locked) {
			cpu_relax();
		}
	}
}

/**
 * spin_try_lock- takes a spin lock, but only if it is available
 * @l: the spin lock
 *
 * Returns 1 if successful, otherwise 0
 */
static inline bool spin_try_lock(spinlock_t *l)
{
	return !(__sync_lock_test_and_set(&l->locked, 1));
}

/**
 * spin_unlock - releases a spin lock
 * @l: the spin lock
 */
static inline void spin_unlock(spinlock_t *l)
{
	__sync_lock_release(&l->locked);
}

