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
 * cpu.h - definitions for x86_64 CPUs
 */

#pragma once

/*
 * Endianness
 */

#define __LITTLE_ENDIAN	1234
#define __BIG_ENDIAN	4321

#define __BYTE_ORDER	__LITTLE_ENDIAN


/*
 * Word Size
 */

#define __32BIT_WORDS	32
#define __64BIT_WORDS	64

#define __WORD_SIZE	__64BIT_WORDS

#define CACHE_LINE_SIZE	64

#define MSR_PKG_ENERGY_STATUS 0x00000611

#define cpu_relax() asm volatile("pause")

#define cpu_serialize() \
	asm volatile("cpuid" : : : "%rax", "%rbx", "%rcx", "%rdx")

static inline unsigned long rdtsc(void)
{
	unsigned int a, d;
	asm volatile("rdtsc" : "=a"(a), "=d"(d));
	return ((unsigned long) a) | (((unsigned long) d) << 32);
}

static inline unsigned long rdtscp(unsigned int *aux)
{
	unsigned int a, d, c;
	asm volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c));
	if (aux)
		*aux = c;
	return ((unsigned long) a) | (((unsigned long) d) << 32);
}

static inline unsigned long rdmsr(unsigned int msr)
{
	unsigned low, high;

	asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
	return low | ((unsigned long)high << 32);
}
