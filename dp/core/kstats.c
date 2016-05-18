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
 * kstats.c -- "kstats" (tree-based) statistics
 *    reports min/avg/max latency and occupancy for the various IX services
 *    printfs at regular intervals, which reset the counters.
 */



#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <ix/kstats.h>
#include <ix/log.h>
#include <ix/timer.h>

#define KSTATS_INTERVAL (5 * ONE_SECOND)

DEFINE_PERCPU(kstats, _kstats);
DEFINE_PERCPU(kstats_accumulate, _kstats_accumulate);
DEFINE_PERCPU(int, _kstats_packets);
DEFINE_PERCPU(int, _kstats_batch_histogram[KSTATS_BATCH_HISTOGRAM_SIZE]);
DEFINE_PERCPU(int, _kstats_backlog_histogram[KSTATS_BACKLOG_HISTOGRAM_SIZE]);
DEFINE_PERCPU(int, llc_load_misses_fd);
DEFINE_PERCPU(int, hw_instructions_fd);

static DEFINE_PERCPU(struct timer, _kstats_timer);

void kstats_enter(kstats_distr *n, kstats_accumulate *saved_accu)
{
	kstats_distr *old = percpu_get(_kstats_accumulate).cur;
	uint64_t now = rdtsc();
	if (old && saved_accu) {
		*saved_accu = percpu_get(_kstats_accumulate);
		saved_accu->cur = old;
		saved_accu->accum_time = now - saved_accu->start_occ;
	}

	percpu_get(_kstats_accumulate).cur = n;
	percpu_get(_kstats_accumulate).start_lat = now;
	percpu_get(_kstats_accumulate).start_occ = now;
	percpu_get(_kstats_accumulate).accum_time = 0;
}

void kstats_leave(kstats_accumulate *saved_accu)
{
	kstats_accumulate *acc = &percpu_get(_kstats_accumulate);
	uint64_t now = rdtsc();
	uint64_t diff_lat = now - acc->start_lat;
	uint64_t diff_occ = now - acc->start_occ + acc->accum_time;
	kstats_distr *cur = acc->cur;

	if (cur)  {
		cur->tot_lat += diff_lat;
		cur->tot_occ += diff_occ;
		if (cur->count == 0) {
			cur->min_lat = diff_lat;
			cur->max_lat = diff_lat;
			cur->min_occ = diff_occ;
			cur->max_occ = diff_occ;
		} else {
			if (cur->min_lat > diff_lat)
				cur->min_lat = diff_lat;
			if (cur->max_lat < diff_lat)
				cur->max_lat = diff_lat;
			if (cur->min_occ > diff_occ)
				cur->min_occ = diff_occ;
			if (cur->max_occ < diff_occ)
				cur->max_occ = diff_occ;
		}
		cur->count++;
		if (saved_accu) {
			*acc = *saved_accu;
			acc->start_occ = now;
		}
	}
}

static void kstats_printone(kstats_distr *d, const char *name, long total_cycles)
{
	if (d->count) {
		log_info("kstat: %2d %-30s %9lu %2d%% latency %7lu | %7lu | %7lu "
			 "occupancy %6lu | %6lu | %6lu\n",
			 percpu_get(cpu_id),
			 name,
			 d->count,
			 100 * d->tot_occ / total_cycles,
			 d->min_lat,
			 d->tot_lat / d->count,
			 d->max_lat,
			 d->min_occ,
			 d->tot_occ / d->count,
			 d->max_occ);
	}
}

static void histogram_to_str(int *histogram, int size, char *buffer, int *avg)
{
	int i, avg_, sum;
	char *target;

	target = buffer;
	buffer[0] = 0;
	avg_ = 0;
	sum = 0;
	for (i = 0; i < size; i++) {
		if (histogram[i]) {
			target += sprintf(target, "%d:%d ", i, histogram[i]);
			avg_ += histogram[i] * i;
			sum += histogram[i];
		}
	}
	if (sum)
		*avg = avg_ / sum;
	else
		*avg = -1;
}

static long long read_perf_event(int fd)
{
	int ret;
	long long value;

	ret = read(fd, &value, sizeof(long long));
	if (ret != sizeof(long long))
		value = -1;
	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	return value;
}

/*
 * print and reinitialize
 */
static void kstats_print(struct timer *t, struct eth_fg *none)
{
	uint64_t total_cycles = (uint64_t) cycles_per_us * KSTATS_INTERVAL;
	char batch_histogram[2048], backlog_histogram[2048];
	int avg_batch, avg_backlog;

	histogram_to_str(percpu_get(_kstats_batch_histogram), KSTATS_BATCH_HISTOGRAM_SIZE, batch_histogram, &avg_batch);
	histogram_to_str(percpu_get(_kstats_backlog_histogram), KSTATS_BACKLOG_HISTOGRAM_SIZE, backlog_histogram, &avg_backlog);

	kstats *ks = &(percpu_get(_kstats));
	log_info("--- BEGIN KSTATS --- %ld%% idle, %ld%% user, %ld%% sys, non idle cycles=%lld, HW instructions=%lld, LLC load misses=%lld (%d pkts, avg batch=%d [%s], avg backlog=%d [%s])\n",
		 ks->idle.tot_lat * 100 / total_cycles,
		 ks->user.tot_lat * 100 / total_cycles,
		 max(0, (int64_t)(total_cycles - ks->idle.tot_lat - ks->user.tot_lat)) * 100 / total_cycles,
		 max(0, (int64_t)(total_cycles - ks->idle.tot_lat)),
		 read_perf_event(percpu_get(hw_instructions_fd)),
		 read_perf_event(percpu_get(llc_load_misses_fd)),
		 percpu_get(_kstats_packets),
		 avg_batch,
		 batch_histogram,
		 avg_backlog,
		 backlog_histogram);
#undef DEF_KSTATS
#define DEF_KSTATS(_c)  kstats_printone(&ks->_c, # _c, total_cycles);
#include <ix/kstatvectors.h>
	log_info("\n");

	KSTATS_VECTOR(print_kstats);
	bzero(ks, sizeof(*ks));
	bzero(percpu_get(_kstats_batch_histogram), sizeof(*percpu_get(_kstats_batch_histogram))*KSTATS_BATCH_HISTOGRAM_SIZE);
	bzero(percpu_get(_kstats_backlog_histogram), sizeof(*percpu_get(_kstats_backlog_histogram))*KSTATS_BACKLOG_HISTOGRAM_SIZE);
	percpu_get(_kstats_packets) = 0;

	timer_add(&percpu_get(_kstats_timer), NULL, KSTATS_INTERVAL);
}

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static int init_perf_event(struct perf_event_attr *attr)
{
	int fd;

	attr->size = sizeof(struct perf_event_attr);
	fd = perf_event_open(attr, 0, -1, -1, 0);
	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	return fd;
}

int kstats_init_cpu(void)
{
	struct perf_event_attr llc_load_misses_attr = {.type = PERF_TYPE_HW_CACHE, .config = (PERF_COUNT_HW_CACHE_LL) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)};
	struct perf_event_attr hw_instructions_attr = {.type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_INSTRUCTIONS};

	timer_init_entry(&percpu_get(_kstats_timer), kstats_print);
	timer_add(&percpu_get(_kstats_timer), NULL, KSTATS_INTERVAL);

	percpu_get(llc_load_misses_fd) = init_perf_event(&llc_load_misses_attr);
	percpu_get(hw_instructions_fd) = init_perf_event(&hw_instructions_attr);
	return 0;
}

