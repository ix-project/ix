#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <ix/perf.h>

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

long read_perf_event(int fd)
{
	int ret;
	long long value;

	ret = read(fd, &value, sizeof(long));
	if (ret != sizeof(long))
		value = -1;
	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	return value;
}

int init_perf_event(struct perf_event_attr *attr)
{
	int fd;

	attr->size = sizeof(struct perf_event_attr);
	fd = perf_event_open(attr, 0, -1, -1, 0);
	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	return fd;
}
