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
 * atomic.h - utilities for atomically manipulating memory
 */

#pragma once

#include <ix/types.h>

/**
 * mb - a memory barrier
 *
 * Ensures all loads and stores before the barrier complete
 * before all loads and stores after the barrier.
 */
#define mb() _mm_mfence()

/**
 * rmb - a read memory barrier
 *
 * Ensures all loads before the barrier complete before
 * all loads after the barrier.
 */
#define rmb() _mm_lfence()

/**
 * wmb - a write memory barrier
 *
 * Ensures all stores before the barrier complete before
 * all stores after the barrier.
 */
#define wmb() _mm_sfence()

#include <emmintrin.h>

#define ATOMIC_INIT(val) {(val)}

static inline int atomic_read(const atomic_t *a)
{
	return *((volatile int *) &a->cnt);
}

static inline void atomic_write(atomic_t *a, int val)
{
	a->cnt = val;
}

static inline int atomic_fetch_and_add(atomic_t *a, int val)
{
	return __sync_fetch_and_add(&a->cnt, val);
}

static inline int atomic_fetch_and_sub(atomic_t *a, int val)
{
	return __sync_fetch_and_add(&a->cnt, val);
}

static inline int atomic_add_and_fetch(atomic_t *a, int val)
{
	return __sync_add_and_fetch(&a->cnt, val);
}

static inline int atomic_sub_and_fetch(atomic_t *a, int val)
{
	return __sync_sub_and_fetch(&a->cnt, val);
}

static inline void atomic_inc(atomic_t *a)
{
	atomic_fetch_and_add(a, 1);
}

static inline bool atomic_dec_and_test(atomic_t *a)
{
	return (atomic_sub_and_fetch(a, 1) == 0);
}

static inline bool atomic_cmpxchg(atomic_t *a, int old, int new)
{
	return __sync_bool_compare_and_swap(&a->cnt, old, new);
}

static inline long atomic64_read(const atomic64_t *a)
{
	return *((volatile long *) &a->cnt);
}

static inline void atomic64_write(atomic64_t *a, long val)
{
	a->cnt = val;
}

static inline long atomic64_fetch_and_add(atomic64_t *a, long val)
{
	return __sync_fetch_and_add(&a->cnt, val);
}

static inline long atomic64_fetch_and_sub(atomic64_t *a, long val)
{
	return __sync_fetch_and_add(&a->cnt, val);
}

static inline long atomic64_add_and_fetch(atomic64_t *a, long val)
{
	return __sync_add_and_fetch(&a->cnt, val);
}

static inline long atomic64_sub_and_fetch(atomic64_t *a, long val)
{
	return __sync_sub_and_fetch(&a->cnt, val);
}

static inline void atomic64_inc(atomic64_t *a)
{
	atomic64_fetch_and_add(a, 1);
}

static inline bool atomic64_dec_and_test(atomic64_t *a)
{
	return (atomic64_sub_and_fetch(a, 1) == 0);
}

static inline bool atomic64_cmpxchg(atomic64_t *a, long old, long new)
{
	return __sync_bool_compare_and_swap(&a->cnt, old, new);
}

