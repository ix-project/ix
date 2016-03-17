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
 * cpu.h - support for multicore and percpu data.
 */

#pragma once

#include <ix/stddef.h>
#include <asm/cpu.h>

#define NCPU	128
extern int cpu_count; /* the number of available CPUs */
extern int cpus_active; /* the number of in-use CPUs */

/* used to define percpu variables */
#define DEFINE_PERCPU(type, name) \
	typeof(type) name __attribute__((section(".percpu,\"\",@nobits#")))

/* used to make percpu variables externally available */
#define DECLARE_PERCPU(type, name) \
	extern DEFINE_PERCPU(type, name)

extern void *percpu_offsets[NCPU];

/**
 * percpu_get_remote - get a percpu variable on a specific core
 * @var: the percpu variable
 * @cpu: the cpu core number
 *
 * Returns a percpu variable.
 */
#define percpu_get_remote(var, cpu)				\
	(*((typeof(var) *) ((uintptr_t) &var +			\
			    (uintptr_t) percpu_offsets[(cpu)])))

static inline void *__percpu_get(void *key)
{
	void *offset;

	asm("mov %%gs:0, %0" : "=r"(offset));

	return (void *)((uintptr_t) key + (uintptr_t) offset);
}

/**
 * percpu_get_addr - get the local percpu variable's address
 * @var: the percpu variable
 *
 * Returns a percpu variable address.
 */
#define percpu_get_addr(var)						\
	((typeof(var) *) (__percpu_get(&var)))

/**
 * percpu_get - get the local percpu variable
 * @var: the percpu variable
 *
 * Returns a percpu variable.
 */
#define percpu_get(var)						\
	(*percpu_get_addr(var))

/**
 * cpu_is_active - is the CPU being used?
 * @cpu: the cpu number
 *
 * Returns true if yes, false if no.
 */
#define cpu_is_active(cpu)					\
	(percpu_offsets[(cpu)] != NULL)

static inline unsigned int __cpu_next_active(unsigned int cpu)
{
	while (cpu < cpu_count) {
		cpu++;

		if (cpu_is_active(cpu))
			return cpu;
	}

	return cpu;
}

/**
 * for_each_active_cpu - iterates over each active (used by IX) CPU
 * @cpu: an integer to store the cpu
 */
#define for_each_active_cpu(cpu)				\
	for ((cpu) = -1; (cpu) = __cpu_next_active(cpu); (cpu) < cpu_count)

DECLARE_PERCPU(unsigned int, cpu_numa_node);
DECLARE_PERCPU(unsigned int, cpu_id);
DECLARE_PERCPU(unsigned int, cpu_nr);

extern void cpu_do_bookkeeping(void);

typedef void (*cpu_func_t)(void *data);
extern int cpu_run_on_one(cpu_func_t func, void *data,
			  unsigned int cpu);

extern int cpu_init_one(unsigned int cpu);
extern int cpu_init(void);

