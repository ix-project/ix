#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include <ix/config.h>
#include <ix/stats.h>

// static void show_stat(const char *name, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

// static void show_stat(const char *name, const char *fmt, ...)
// {
	// va_list ap;

	// printf("%s ", name);
	// va_start(ap, fmt);
	// vprintf(fmt, ap);
	// va_end(ap);
	// puts("");
// }

#if !CONFIG_STATS

int main(int argc, char **argv)
{
	fprintf(stderr, "%s: Error: CONFIG_STATS was disabled during compilation.\n", argv[0]);
	return 1;
}

#else

#define cpu_relax() asm volatile("pause")

static void __attribute__((unused)) show_histogram(const char *name, int min, int max, int buckets, long sum, unsigned int *bucket_arr)
{
	int i, from, to;
	long count = 0;
	double perc;

	/* prevent bug in gcc 5.4 https://godbolt.org/g/Mhfv83 */
	unsigned int copy[256];
	assert(buckets < sizeof(copy)/sizeof(copy[0]));
	memcpy(copy, bucket_arr, buckets * sizeof(int));

	for (i = 0; i < buckets; i++)
		count += (long) copy[i];

	printf("%s count %ld avg %.1f ", name, count, 1.0 * sum / count);
	for (i = 0; i < buckets; i++) {
		from = (i * (max - min) + buckets - 1) / buckets + min;
		to = ((i + 1) * (max - min) + buckets - 1) / buckets + min;
		perc = 100.0 * bucket_arr[i] / count;
		if (i == 0)
			printf("(-inf-%d): %.1f%% ", to, perc);
		else if (i == buckets - 1)
			printf("[%d-inf): %.1f%% ", from, perc);
		else
			printf("[%d-%d): %.1f%% ", from, to, perc);
	}
	puts("");
}

static void __attribute__((unused)) show_counter(const char *name, int value)
{
	printf("%s count %d\n", name, value);
}

static void show_stats(struct ix_stats *stats)
{
	int i;
	struct ix_stats_percpu acc = {0};

	for (i = 0; i < stats->cpus; i++)
		stats->percpu[i].poll = 1;

	for (i = 0; i < stats->cpus; i++)
		while (stats->percpu[i].poll)
			cpu_relax();

	for (i = 0; i < stats->cpus; i++) {
#define HISTOGRAM(name, min, max, buckets) \
	acc.name.sum += stats->percpu[i].name.sum; \
	for (int j = 0; j < buckets; j++) { \
		acc.name.bucket[j] += stats->percpu[i].name.bucket[j]; \
	}
#define COUNTER(name) acc.name += stats->percpu[i].name;
STATS
#undef HISTOGRAM
#undef COUNTER
	}

#define HISTOGRAM(name, min, max, buckets) show_histogram(#name, min, max, buckets, acc.name.sum, acc.name.bucket);
#define COUNTER(name) show_counter(#name, acc.name);
STATS
#undef HISTOGRAM
}

int main(int argc, char **argv)
{
	int fd, i;
	int reset = 0;
	struct ix_stats *stats;

	if (argc > 1 && !strcmp(argv[1], "--reset"))
		reset = 1;

	fd = shm_open("/ix-stats", O_RDWR, 0);
	if (fd == -1) {
		perror("shm_open");
		exit(1);
	}

	stats = mmap(NULL, sizeof(struct ix_stats), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (stats == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	// bzero(stats, sizeof(*stats));
	// STATS_HISTOGRAM(ethqueue, -999);
	// for (int i = IX_STATS_HISTOGRAM_MIN; i < IX_STATS_HISTOGRAM_MAX; i++) {
		// printf("%d ", i);
		// STATS_HISTOGRAM(ethqueue, i);
		// show_stats(stats);
	// }
	// STATS_HISTOGRAM(ethqueue, 999);

	show_stats(stats);

	if (reset)
		for (i = 0; i < stats->cpus; i++)
			stats->percpu[i].reset = 1;

	return 0;
}

#endif /* CONFIG_STATS */
