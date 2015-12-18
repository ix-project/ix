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
 * compiler.h - useful compiler hints, intrinsics, and attributes
 */

#pragma once

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define unreachable() __builtin_unreachable()

#define prefetch0(x) __builtin_prefetch((x), 0, 3)
#define prefetch1(x) __builtin_prefetch((x), 0, 2)
#define prefetch2(x) __builtin_prefetch((x), 0, 1)
#define prefetchnta(x) __builtin_prefetch((x), 0, 0)
#define prefetch() prefetch0()

#define clz64(x) __builtin_clzll(x)

#define __packed __attribute__((packed))
#define __notused __attribute__((unused))
#define __aligned(x) __attribute__((aligned(x)))

#define GCC_VERSION (__GNUC__ * 10000        \
		     + __GNUC_MINOR__ * 100  \
		     + __GNUC_PATCHLEVEL__)

#if GCC_VERSION >= 40800
#define HAS_BUILTIN_BSWAP 1
#endif

