#include "ixev_timer.h"
#include "syscall.h"

int ixev_timer_init(struct ixev_timer *t, ixev_timer_handler_t h, void *arg)
{
	t->handler = h;
	t->arg = arg;
	t->timer_id = sys_timer_init(t);

	return t->timer_id != -1;
}

int ixev_timer_add(struct ixev_timer *t, struct timeval tv)
{
	uint64_t delay;

	delay = tv.tv_sec * 1000000 + tv.tv_usec;

	return sys_timer_ctl(t->timer_id, delay);
}
