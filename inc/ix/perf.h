#pragma once

long read_perf_event(int fd);

int init_perf_event(struct perf_event_attr *attr);
