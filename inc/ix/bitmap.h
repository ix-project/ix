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
 * bitmap.h - a library for bit array manipulation
 */

#pragma once

#include <string.h>

#include <ix/stddef.h>

#define BITS_PER_LONG	(sizeof(long) * 8)
#define BITMAP_LONG_SIZE(nbits) \
	div_up(nbits, BITS_PER_LONG)

#define DEFINE_BITMAP(name, nbits) \
	unsigned long name[BITMAP_LONG_SIZE(nbits)]

typedef unsigned long *bitmap_ptr;

#define BITMAP_POS_IDX(pos)	((pos) / BITS_PER_LONG)
#define BITMAP_POS_SHIFT(pos)	((pos) & (BITS_PER_LONG - 1))

/**
 * bitmap_set - sets a bit in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 */
static inline void bitmap_set(unsigned long *bits, int pos)
{
	bits[BITMAP_POS_IDX(pos)] |= (1ul << BITMAP_POS_SHIFT(pos));
}

/**
 * bitmap_clear - clears a bit in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 */
static inline void bitmap_clear(unsigned long *bits, int pos)
{
	bits[BITMAP_POS_IDX(pos)] &= ~(1ul << BITMAP_POS_SHIFT(pos));
}

/**
 * bitmap_test - tests if a bit is set in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 *
 * Returns true if the bit is set, otherwise false.
 */
static inline bool bitmap_test(unsigned long *bits, int pos)
{
	return (bits[BITMAP_POS_IDX(pos)] & (1ul << BITMAP_POS_SHIFT(pos))) != 0;
}

/**
 * bitmap_init - initializes a bitmap
 * @bits: the bitmap
 * @nbits: the number of total bits
 * @state: if true, all bits are set, otherwise all bits are cleared
 */
static inline void bitmap_init(unsigned long *bits, int nbits, bool state)
{
	memset(bits, state ? 0xff : 0x00, BITMAP_LONG_SIZE(nbits) * sizeof(long));
}

