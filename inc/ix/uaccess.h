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

#include <ix/types.h>
#include <ix/mem.h>
#include <ix/errno.h>

#include <asm/uaccess.h>

/**
 * uaccess_okay - determines if a memory object lies in userspace
 * @addr: the address
 * @len: the length of the memory object
 *
 * Returns true if the memory is user-level, otherwise false.
 */
static inline bool uaccess_okay(void *addr, size_t len)
{
	if (len > MEM_USER_END - MEM_USER_START ||
	    (uintptr_t) addr < MEM_USER_START ||
	    (uintptr_t) addr + len > MEM_USER_END)
		return false;
	return true;
}

/**
 * uaccess_zc_okay - determines if a memory object is safe to zero-copy
 * @addr: the address
 * @len: the length of the memory object
 *
 * Returns true if the memory is safe for zero-copy, otherwise false.
 */
static inline bool uaccess_zc_okay(void *addr, size_t len)
{
	if (len > MEM_ZC_USER_END - MEM_ZC_USER_START ||
	    (uintptr_t) addr < MEM_ZC_USER_START ||
	    (uintptr_t) addr + len > MEM_ZC_USER_END)
		return false;
	return true;
}

/**
 * copy_from_user - safely copies user memory to kernel memory
 * @user_src: the user source memory
 * @kern_dst: the kernel destination memory
 * @len: the number of bytes to copy
 *
 * Returns 0 if successful, otherwise -EFAULT if unsafe.
 */
static inline int copy_from_user(void *user_src, void *kern_dst, size_t len)
{
	if (!uaccess_okay(user_src, len))
		return -EFAULT;

	if (__builtin_constant_p(len)) {
		if (len == sizeof(uint64_t)) {
			*((uint64_t *) kern_dst) = uaccess_peekq(user_src);
			return uaccess_check_fault() ? -EFAULT : 0;
		}
	}

	return uaccess_copy_user(user_src, kern_dst, len);
}

/**
 * copy_to_user - safely copies kernel memory to user memory
 * @kern_src: the kernel source memory
 * @user_dst: the user destination memory
 * @len: the number of bytes to copy
 *
 * Returns 0 if successful, otherwise -EFAULT if unsafe.
 */
static inline int copy_to_user(void *kern_src, void *user_dst, size_t len)
{
	if (!uaccess_okay(user_dst, len))
		return -EFAULT;

	if (__builtin_constant_p(len)) {
		if (len == sizeof(uint64_t)) {
			uaccess_pokeq(user_dst, *((uint64_t *) kern_src));
			return uaccess_check_fault() ? -EFAULT : 0;
		}
	}

	return uaccess_copy_user(kern_src, user_dst, len);
}

