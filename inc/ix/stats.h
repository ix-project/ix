#pragma once

#include <ix/config.h>
#include <ix/cpu.h>

#define STATS \
	COUNTER(llc_load_misses)

#if CONFIG_STATS

struct ix_stats {
	int cpus;
	struct ix_stats_percpu {
		volatile char reset;
		volatile char poll;
#define HISTOGRAM(name, min, max, buckets) \
	struct { \
		long sum; \
		unsigned int bucket[buckets]; \
	} __attribute__((packed)) name;
#define COUNTER(name) int name;
STATS
#undef HISTOGRAM
#undef COUNTER
	} __attribute__((packed, aligned(64))) percpu[NCPU];
};

int stats_init(void);
int stats_init_cpu(void);

extern struct ix_stats *stats;

static inline int stats_histogram_bucket(int value, int min, int max, int buckets)
{
	int b;
	b = value - min;
	b = b * buckets / (max - min);
	if (b < 0)
		b = 0;
	else if (b > buckets - 1)
		b = buckets - 1;
	return b;
}

#define HISTOGRAM(name, min, max, buckets) \
	static inline void stats_histogram_ ## name(int value) { \
		stats->percpu[percpu_get(cpu_nr)].name.sum += value; \
		stats->percpu[percpu_get(cpu_nr)].name.bucket[stats_histogram_bucket(value, min, max, buckets)]++; \
	}
#define COUNTER(name) \
	static inline void stats_counter_ ## name(int delta) { \
		stats->percpu[percpu_get(cpu_nr)].name += delta; \
	}
STATS
#undef HISTOGRAM
#undef COUNTER

void stats_check_reset(void);

#else

#define HISTOGRAM(name, min, max, buckets) static inline void stats_histogram_ ## name(int value) { }
#define COUNTER(name) static inline void stats_counter_ ## name(int delta) { }
STATS
#undef HISTOGRAM
#undef COUNTER

#endif
