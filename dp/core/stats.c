#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <ix/cfg.h>
#include <ix/stats.h>
#include <ix/perf.h>

#if CONFIG_STATS

static DEFINE_PERCPU(int, llc_load_misses_fd);

struct ix_stats *stats;

int stats_init(void)
{
	int fd, ret;
	void *vaddr;

	fd = shm_open("/ix-stats", O_RDWR | O_CREAT | O_TRUNC, 0660);
	if (fd == -1)
		return 1;

	ret = ftruncate(fd, sizeof(struct ix_stats));
	if (ret)
		return ret;

	vaddr = mmap(NULL, sizeof(struct ix_stats), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (vaddr == MAP_FAILED)
		return 1;

	stats = vaddr;

	bzero((void *)stats, sizeof(struct ix_stats));

	stats->cpus = CFG.num_cpus;
	return 0;
}

int stats_init_cpu(void)
{
	struct perf_event_attr llc_load_misses_attr = {
		.type = PERF_TYPE_HW_CACHE,
		.config = (PERF_COUNT_HW_CACHE_LL) |
			  (PERF_COUNT_HW_CACHE_OP_READ << 8) |
			  (PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
	};

	percpu_get(llc_load_misses_fd) = init_perf_event(&llc_load_misses_attr);

	return 0;
}

void stats_check_reset(void)
{
	struct ix_stats_percpu *s = &stats->percpu[percpu_get(cpu_nr)];

	if (s->poll) {
		s->llc_load_misses = read_perf_event(percpu_get(llc_load_misses_fd));
		asm volatile("":::"memory");
		s->poll = 0;
	}

	if (s->reset)
		bzero(stats->percpu, sizeof(stats->percpu));
}

#endif
