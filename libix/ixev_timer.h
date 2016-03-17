#pragma once

#include <sys/time.h>

typedef void (*ixev_timer_handler_t)(void *arg);

struct ixev_timer {
	ixev_timer_handler_t handler;
	void *arg;
	int timer_id;
};

int ixev_timer_init(struct ixev_timer *t, ixev_timer_handler_t h, void *arg);

int ixev_timer_add(struct ixev_timer *t, struct timeval tv);
