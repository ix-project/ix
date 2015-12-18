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
 * timer.c - timer event infrastructure
 *
 * The design is inspired by "Hashed and Hierarchical Timing Wheels: Data
 * Structures for the Efficient Implementation of a Timer Facility" by
 * George Varghese and Tony Lauck. SOSP 87.
 *
 * Specificially, we use Scheme 7 described in the paper, where
 * hierarchical sets of buckets are used.
 */

#define DEBUG_TIMER

#include <ix/timer.h>
#include <ix/errno.h>
#include <ix/log.h>
#include <ix/cpu.h>
#include <ix/kstats.h>
#include <ix/ethfg.h>
#include <assert.h>
#include <time.h>
#include <ix/log.h>
#include <stdio.h>

#define WHEEL_SHIFT_LOG2	3
#define WHEEL_SHIFT		(1 << WHEEL_SHIFT_LOG2)
#define WHEEL_SIZE		(1 << WHEEL_SHIFT)
#define WHEEL_MASK		(WHEEL_SIZE - 1)
#define WHEEL_COUNT		3

#define MIN_DELAY_SHIFT		4
#define MIN_DELAY_US		(1 << MIN_DELAY_SHIFT)
#define MIN_DELAY_MASK		(MIN_DELAY_US - 1)
#define MAX_DELAY_US \
	(MIN_DELAY_US * (1 << (WHEEL_COUNT * WHEEL_SHIFT)))

#define WHEEL_IDX_TO_SHIFT(idx) \
	((idx) * WHEEL_SHIFT + MIN_DELAY_SHIFT)
#define WHEEL_OFFSET(val, idx) \
	(((val) >> WHEEL_IDX_TO_SHIFT(idx)) & WHEEL_MASK)

/*
 * NOTE: these parameters may need to be tweaked.
 *
 * Right now we have the following wheels:
 *
 * high precision wheel: 256 x 16 us increments
 * medium precision wheel: 256 x 4 ms increments
 * low precision wheel: 256 x 1 second increments
 *
 * Total range 0 to 256 seconds...
 */



struct timerwheel {
	uint64_t now_us;
	uint64_t timer_pos;
	struct hlist_head wheels[WHEEL_COUNT][WHEEL_SIZE];

};


static DEFINE_PERCPU(struct timerwheel, timer_wheel_cpu);


int cycles_per_us __aligned(64);

/**
 * __timer_delay_us - spins the CPU for the specified delay
 * @us: the delay in microseconds
 */
void __timer_delay_us(uint64_t us)
{
	uint64_t cycles = us * cycles_per_us;
	unsigned long start = rdtsc();

	while (rdtsc() - start < cycles)
		cpu_relax();
}

static inline bool timer_expired(struct timerwheel *tw, struct timer *t)
{
	return (t->expires <= tw->now_us);
}


static void timer_insert(struct eth_fg *cur_fg, struct timerwheel *tw, struct timer *t)
{
	uint64_t expire_us, delay_us;
	int index, offset;

	/*
	 * Round up to the next bucket because part of the time
	 * between buckets has likely already passed.
	 */
	expire_us = t->expires + MIN_DELAY_US;
	delay_us = expire_us - tw->now_us;

	/*
	 * This code looks a little strange because it was optimized to
	 * calculate the correct bucket placement without using any
	 * branch instructions, instead using count last zero (CLZ).
	 *
	 * NOTE: This assumes MIN_DELAY_US was added above. Otherwise
	 * the index might be calculated as negative, so be careful
	 * about this constraint when modifying this code.
	 */
	index = ((63 - clz64(delay_us) - MIN_DELAY_SHIFT)
		 >> WHEEL_SHIFT_LOG2);
	offset = WHEEL_OFFSET(expire_us, index);

	hlist_add_head(&tw->wheels[index][offset], &t->link);

	if (cur_fg)
		t->fg_id = cur_fg->fg_id;
	else
		t->fg_id = -1;

}

/**
 * timer_add - adds a timer
 * @l: the timer
 * @usecs: the time interval from present to fire the timer
 *
 * Returns 0 if successful, otherwise failure.
 */
static int __timer_add(struct eth_fg *cur_fg, struct timerwheel *tw, struct timer *t, uint64_t delay_us)
{
	uint64_t expires;
	assert(delay_us > 0);
	if (unlikely(delay_us >= MAX_DELAY_US)) {
		panic("__timer_add out of range\n");
		return -EINVAL;
	}
	assert(tw);
	/*
	 * Make sure the expiration time is rounded
	 * up past the current bucket.
	 */
	/* NOTE: Don't use the cached now_us. It might be far in the past! */
	expires = rdtsc() / cycles_per_us + delay_us;
	t->expires = expires;
	timer_insert(cur_fg, tw, t);

	return 0;
}

int timer_add(struct timer *t, struct eth_fg *cur_fg, uint64_t usecs)
{
	struct timerwheel *tw = &percpu_get(timer_wheel_cpu);
	return __timer_add(cur_fg, tw, t, usecs);

}

/**
 *  timer_add_abs -- use absoute time (in usecs)
 */
void timer_add_abs(struct timer *t, struct eth_fg *cur_fg, uint64_t abs_usecs)
{
	struct timerwheel *tw = &percpu_get(timer_wheel_cpu);
	assert(abs_usecs > tw->timer_pos);
	assert(!timer_pending(t));
	t->expires = abs_usecs;
	timer_insert(cur_fg, tw, t);
}

uint64_t timer_now(void)
{
	/* NOTE: Don't use the cached now_us. It might be far in the past! */
	return rdtsc() / cycles_per_us;
}
/**
 * timer_add_for_next_tick - adds a timer with the shortest possible delay
 * @t: the timer
 *
 * The timer is added to the nearest bucket and will fire the next
 * time timer_run() is called, assuming MIN_DELAY_US has elapsed.
 */
void timer_add_for_next_tick(struct timer *t, struct eth_fg *cur_fg)
{
	struct timerwheel *tw = &percpu_get(timer_wheel_cpu);
	uint64_t expire_us = tw->now_us + MIN_DELAY_US;
	t->expires = expire_us;
	timer_insert(cur_fg, tw, t);
}


static void timer_run_bucket(struct timerwheel *tw, struct hlist_head *h)
{
	struct hlist_node *n, *tmp;
	struct timer *t;
#ifdef ENABLE_KSTATS
	kstats_accumulate save;
#endif

	hlist_for_each_safe(h, n, tmp) {
		t = hlist_entry(n, struct timer, link);
		__timer_del(t);
		KSTATS_PUSH(timer_handler, &save);
		if (t->fg_id >= 0)
			eth_fg_set_current(fgs[t->fg_id]);
		t->handler(t, fgs[t->fg_id]);
		KSTATS_POP(&save);
	}
	h->head = NULL;
}

static int timer_reinsert_bucket(struct timerwheel *tw, struct hlist_head *h, uint64_t now_us)
{
	struct hlist_node *x, *tmp;
	struct timer *t;
#ifdef ENABLE_KSTATS
	kstats_accumulate save;
#endif
	int count = 0;

	hlist_for_each_safe(h, x, tmp) {
		t = hlist_entry(x, struct timer, link);
		__timer_del(t);
		count++;
		if (timer_expired(tw, t)) {
			KSTATS_PUSH(timer_handler, &save);
			if (t->fg_id >= 0)
				eth_fg_set_current(fgs[t->fg_id]);
			t->handler(t, fgs[t->fg_id]);
			KSTATS_POP(&save);
			continue;
		}
		timer_insert(get_ethfg_from_id(t->fg_id), tw, t) ;
	}
	return count;
}

/**
 * timer_collapse - collapselonger-term buckets into shorter-term buckets
*/


static int timer_collapse(uint64_t pos)
{
	struct timerwheel *tw;
	int wheel;
	int count;
	KSTATS_VECTOR(timer_collapse);


	for (wheel = 1; wheel < WHEEL_COUNT; wheel++) {
		int off = WHEEL_OFFSET(pos, wheel);

		tw = &percpu_get(timer_wheel_cpu);
		count = timer_reinsert_bucket(tw, &tw->wheels[wheel][off], pos);

		// only need to go to the next wheel if offset is zero
		if (off)
			break;
	}
	return count;
}


/**
 * timer_run - the main timer processing pass
 *
 * Call this once per device polling pass.
 */

void timer_run(void)
{

	struct timerwheel *tw = &percpu_get(timer_wheel_cpu);
	uint64_t pos = tw->timer_pos;
	tw->now_us = rdtsc() / cycles_per_us;

	for (; pos <= tw->now_us; pos += MIN_DELAY_US) {
		int high_off = WHEEL_OFFSET(pos, 0);

		if (!high_off)
			timer_collapse(pos);

		timer_run_bucket(tw, &tw->wheels[0][high_off]);
	}
	tw->timer_pos = pos;
	unset_current_fg();
}


/**
 * timer_deadline - determine the time remaining until the next deadline
 * @max_deadline_us: the maximum amount of time to look into the future
 *
 * NOTE: A short @max_deadline_us will reduce the overhead of calling
 * this function. However, even very large values will yield acceptable
 * performance.
 *
 * NOTE: If a timer is about to be cascaded, this function could
 * underestimate the next deadline.
 *
 * Returns time in microseconds until the next timer expires or
 * @max_deadline_us, whichever is smaller.
 */


uint64_t
timer_deadline(uint64_t max_deadline_us)
{
	struct timerwheel *tw = &percpu_get(timer_wheel_cpu);
	uint64_t now_us = tw->now_us;
	uint64_t future_us = now_us + max_deadline_us;
	int idx;

	for (idx = 0; idx < WHEEL_COUNT; idx++) {
		uint64_t start = (now_us >> WHEEL_IDX_TO_SHIFT(idx));
		uint64_t end = (future_us >> WHEEL_IDX_TO_SHIFT(idx));

		if (start == end)
			break;

		end = min(end, start + WHEEL_SIZE);
		for (start++; start <= end; start++) {
			if (!hlist_empty(&tw->wheels[idx][start & WHEEL_MASK])) {
				uint64_t deadline_us = (start << WHEEL_IDX_TO_SHIFT(idx));
				uint64_t tsc_us = rdtsc() / cycles_per_us;
				if (deadline_us <= tsc_us)
					return 0;
				else
					return deadline_us - tsc_us;
			}
		}
	}

	return max_deadline_us;

}

/**
 * timer_collect_fgs -- collect all pending timer events corresponding to a set of fg_id
 * @fg_vector -- vector of all flow group ids to collect --must be of size ETH_MAX_NUM_FG * eth_dev_count (in bytes)
 * @list -- out-parmeter of all matching events
 @ @time_pos - out-parameter.  timer_pos at the time of extraction.
 */

//NOT TESTED. EdB
int
timer_collect_fgs(uint8_t *fg_vector, struct hlist_head *list, uint64_t *timer_pos)
{
	struct timerwheel *tw = &percpu_get(timer_wheel_cpu);
	int wheel, pos, count = 0;
	struct hlist_node *x, *tmp;
	struct timer *t;

	*timer_pos = tw->timer_pos;

	for (wheel = 0; wheel < WHEEL_COUNT; wheel++)
		for (pos = 0; pos < WHEEL_SIZE; pos++)
			hlist_for_each_safe(&tw->wheels[wheel][pos], x, tmp) {
			t = hlist_entry(x, struct timer, link);
			if (t->fg_id >= 0 && fg_vector[t->fg_id]) {
				hlist_del(&t->link);
				hlist_add_head(list, &t->link);
				count++;
			}
		}

	return count;
}

/**
 * timer_reinject_fg -- insert collected pending timer events on destination CPU
 * @list - list of timers
 * @timer_pos -- count (in usec) of the collection time
 */

// NOT TESTED
void
timer_reinject_fgs(struct hlist_head *list, uint64_t timer_pos)
{
	struct timerwheel *tw = &percpu_get(timer_wheel_cpu);
	struct hlist_node *x, *tmp;
	struct timer *t;
	uint64_t t_base;

	if (timer_pos >= tw->timer_pos)
		t_base = tw->timer_pos;
	else
		t_base = timer_pos;

	hlist_for_each_safe(list, x, tmp) {
		t = hlist_entry(x, struct timer, link);
		assert(t->fg_id >= 0);
		int64_t delay = t->expires - t_base;
		if (delay <= 0)
			timer_add_for_next_tick(t, fgs[t->fg_id]);
		else
			__timer_add(fgs[t->fg_id], tw, t, delay);
	}
}




/* derived from DPDK */
static int
timer_calibrate_tsc(void)
{
	struct timespec sleeptime = {.tv_nsec = 5E8 }; /* 1/2 second */
	struct timespec t_start, t_end;

	cpu_serialize();
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_start) == 0) {
		uint64_t ns, end, start;
		double secs;

		start = rdtsc();
		nanosleep(&sleeptime, NULL);
		clock_gettime(CLOCK_MONOTONIC_RAW, &t_end);
		end = rdtscp(NULL);
		ns = ((t_end.tv_sec - t_start.tv_sec) * 1E9);
		ns += (t_end.tv_nsec - t_start.tv_nsec);

		secs = (double)ns / 1000;
		cycles_per_us = (uint64_t)((end - start) / secs);
		log_info("timer: detected %d ticks per US\n",
			 cycles_per_us);
		return 0;
	}

	return -1;
}

/**
 * timer_init_fg - initializes the timer service for a core
 */
void timer_init_fg(void)
{
	;
}

int timer_init_cpu(void)
{
	struct timerwheel *tw = &percpu_get(timer_wheel_cpu);
	tw->now_us = rdtsc() / cycles_per_us;
	tw->timer_pos = tw->now_us;
	return 0;
}
/**
 * timer_init - global timer initialization
 *
 * Returns 0 if successful, otherwise fail.
 */
int timer_init(void)
{
	int ret;

	ret = timer_calibrate_tsc();
	if (ret)
		return ret;

	return 0;
}

