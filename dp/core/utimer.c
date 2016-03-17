#include <ix/syscall.h>
#include <ix/timer.h>

/* max number of supported user level timers */
#define UTIMER_COUNT 32

struct utimer {
	struct timer t;
	void *cookie;
};

struct utimer_list {
	struct utimer arr[UTIMER_COUNT];
};

DEFINE_PERCPU(struct utimer_list, utimers);

void generic_handler(struct timer *t, struct eth_fg *unused)
{
	struct utimer *ut;
	ut = container_of(t, struct utimer, t);
	usys_timer((unsigned long) ut->cookie);
}

static int find_available(struct utimer_list *tl)
{
	static int next;

	if (next >= UTIMER_COUNT)
		return -1;

	return next++;
}

int utimer_init(struct utimer_list *tl, void *udata)
{
	struct utimer *ut;
	int index;

	index = find_available(tl);
	if (index < 0)
		return -1;

	ut = &tl->arr[index];
	ut->cookie = udata;
	timer_init_entry(&ut->t, generic_handler);

	return index;
}

int utimer_arm(struct utimer_list *tl, int timer_id, uint64_t delay)
{
	struct timer *t;
	t = &tl->arr[timer_id].t;
	return timer_add(t, NULL, delay);
}
