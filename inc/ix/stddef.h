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
 * stddef.h - standard definitions
 */

#pragma once

#include <stddef.h>
#include <ix/compiler.h>
#include <ix/types.h>

/*
 * NOTE: Some code in this file is derived from the public domain CCAN project.
 * http://ccodearchive.net/
 */

/**
 * BUILD_ASSERT - assert a build-time dependency.
 * @cond: the compile-time condition which must be true.
 *
 * Your compile will fail if the condition isn't true, or can't be evaluated
 * by the compiler.  This can only be used within a function.
 */
#define BUILD_ASSERT(cond) \
	do { (void) sizeof(char [1 - 2*!(cond)]); } while(0)

/**
 * BUILD_ASSERT_OR_ZERO - assert a build-time dependency, as an expression.
 * @cond: the compile-time condition which must be true.
 *
 * Your compile will fail if the condition isn't true, or can't be evaluated
 * by the compiler.  This can be used in an expression: its value is "0".
 */
#define BUILD_ASSERT_OR_ZERO(cond) \
	(sizeof(char [1 - 2*!(cond)]) - 1)

#define check_type(expr, type)                  \
	((typeof(expr) *)0 != (type *)0)
#define check_types_match(expr1, expr2)         \
	((typeof(expr1) *)0 != (typeof(expr2) *)0)

/**
 * container_of - get pointer to enclosing structure
 * @member_ptr: pointer to the structure member
 * @containing_type: the type this member is within
 * @member: the name of this member within the structure.
 *
 * Given a pointer to a member of a structure, this macro does pointer
 * subtraction to return the pointer to the enclosing type.
 */
#define container_of(member_ptr, containing_type, member)               \
	((containing_type *)                                            \
	 ((char *)(member_ptr)                                          \
	  - offsetof(containing_type, member))                          \
	  + check_types_match(*(member_ptr), ((containing_type *)0)->member))

/**
 * container_of_var - get pointer to enclosing structure using a variable
 * @member_ptr: pointer to the structure member
 * @container_var: a pointer of same type as this member's container
 * @member: the name of this member within the structure.
 */
#define container_of_var(member_ptr, container_var, member)             \
	container_of(member_ptr, typeof(*container_var), member)

#define _array_size_chk(arr)                                            \
	BUILD_ASSERT_OR_ZERO(!__builtin_types_compatible_p(typeof(arr), \
							typeof(&(arr)[0])))

/**
 * ARRAY_SIZE - get the number of elements in a visible array
 * @arr: the array whose size you want.
 *
 * This does not work on pointers, or arrays declared as [], or
 * function parameters.  With correct compiler support, such usage
 * will cause a build error (see build_assert).
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + _array_size_chk(arr))

/**
 * max - picks the maximum of two expressions
 *
 * Arguments @a and @b are evaluated exactly once
 */
#define max(a, b) \
	({typeof(a) _a = (a); \
	  typeof(b) _b = (b); \
	  _a > _b ? _a : _b; })

/**
 * min - picks the minimum of two expressions
 *
 * Arguments @a and @b are evaluated exactly once
 */
#define min(a, b) \
	({typeof(a) _a = (a); \
	  typeof(b) _b = (b); \
	  _a < _b ? _a : _b; })

/**
 * align_up - rounds a value up to an alignment
 * @x: the value
 * @a: the alignment (must be power of 2)
 *
 * Returns an aligned value.
 */
#define align_up(x, a)	((((x) - 1) | ((typeof(x)) (a) - 1)) + 1)

/**
 * align_down - rounds a value down to an alignment
 * @x: the value
 * @a: the alignment (must be power of 2)
 *
 * Returns an aligned value.
 */
#define align_down(x, a) ((x) & ~((typeof(x)) (a) - 1))

/**
 * div_up - divides two numbers, rounding up to an integer
 * @x: the dividend
 * @d: the divisor
 *
 * Returns a rounded-up quotient.
 */
#define div_up(x, d) ((((x) + (d) - 1)) / (d))

