#pragma once

#include <ix/stddef.h>

struct utimer_list;

DECLARE_PERCPU(struct utimer_list, utimers);

int utimer_init(struct utimer_list *tl, void *udata);

int utimer_arm(struct utimer_list *tl, int timer_id, uint64_t delay);
