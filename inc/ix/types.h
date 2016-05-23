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
 * types.h - primitive type definitions
 */

#pragma once

#include <asm/cpu.h>
#include <stdbool.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;

#ifndef __WORD_SIZE
#error __WORD_SIZE is undefined
#endif

#if __WORD_SIZE == __64BIT_WORDS

typedef unsigned long uint64_t;
typedef signed long int64_t;

#else /* __WORDSIZE == __64BIT_WORDS */

typedef unsigned long long uint64_t;
typedef signed long long int64_t;

#endif /* __WORDSIZE == __64BIT_WORDS */

typedef unsigned long	uintptr_t;
typedef long		off_t;
typedef unsigned long	size_t;
typedef long		ssize_t;

typedef struct {
	volatile int locked;
} spinlock_t;

typedef struct {
	int cnt;
} atomic_t;

typedef struct {
	long cnt;
} atomic64_t;

