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
 * uaccess.h - routines for safely accessing user memory
 */

#pragma once

#include <ix/errno.h>

#define ASM_REGISTER_FIXUP(fault_addr, fix_addr) \
	".pushsection \"__fixup_tbl\",\"a\"\n"	 \
	".balign 16\n"				 \
	".quad(" #fault_addr ")\n"		 \
	".quad(" #fix_addr ")\n"		 \
	".popsection\n"

#define ASM_START_FIXUP	".section .fixup,\"ax\"\n"
#define ASM_END_FIXUP	".previous\n"

extern volatile int uaccess_fault;

/**
 * uaccess_peekq - safely read a 64-bit word of memory
 * @addr: the address
 *
 * Returns the value.
 */
static inline uint64_t uaccess_peekq(const uint64_t *addr)
{
	uint64_t ret;

	asm volatile("1: movq (%2), %0\n"
		     "2:\n"
		     ASM_START_FIXUP
		     "3: movl $1, %1\n"
		     "jmp 2b\n"
		     ASM_END_FIXUP
		     ASM_REGISTER_FIXUP(1b, 3b) :
		     "=r"(ret), "=m"(uaccess_fault) :
		     "r"(addr) : "memory", "cc");

	return ret;
}

/**
 * uaccess_pokeq - safely writes a 64-bit word of memory
 * @addr: the address
 * @val: the value to write
 */
static inline void uaccess_pokeq(uint64_t *addr, uint64_t val)
{
	asm volatile("1: movq %1, (%2)\n"
		     "2:\n"
		     ASM_START_FIXUP
		     "3: movl $1, %0\n"
		     "jmp 2b\n"
		     ASM_END_FIXUP
		     ASM_REGISTER_FIXUP(1b, 3b) :
		     "=m"(uaccess_fault) :
		     "r"(val), "r"(addr) : "memory", "cc");
}

/**
 * uaccess_check_fault - determines if a peek or poke caused a fault
 *
 * Returns true if there was a fault, otherwise false.
 */
static inline bool uaccess_check_fault(void)
{
	if (uaccess_fault) {
		uaccess_fault = 0;
		return true;
	}

	return false;
}

/**
 * uaccess_copy_user - copies memory from or to the user safely
 * @src: the source address
 * @dst: the destination address
 * @len: the number of bytes to copy
 *
 * Returns 0 if successful, otherwise -EFAULT if there was a bad address.
 */
static inline int uaccess_copy_user(const char *src, char *dst, int len)
{
	int ret;

	asm volatile("1: rep\n"
		     "movsb\n"
		     "xorl %0, %0\n"
		     "2:\n"
		     ASM_START_FIXUP
		     "3:movl %c[errno], %0\n"
		     "jmp 2b\n"
		     ASM_END_FIXUP
		     ASM_REGISTER_FIXUP(1b, 3b) :
		     "=r"(ret), "+S"(src), "+D"(dst), "+c"(len) :
		     [errno]"i"(-EFAULT) : "memory");

	return ret;
}

