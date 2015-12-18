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
 * log.c - the logging system
 *
 * FIXME: Should we direct logs to a file?
 */

#include <ix/stddef.h>
#include <ix/log.h>
#include <ix/cpu.h>

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#define MAX_LOG_LEN	1024

__thread bool log_is_early_boot = true;

int max_loglevel = LOG_DEBUG;

void logk(int level, const char *fmt, ...)
{
	va_list ptr;
	char buf[MAX_LOG_LEN];
	time_t ts;
	off_t off = 0;

	if (level > max_loglevel)
		return;

	if (!log_is_early_boot) {
		snprintf(buf, 9, "CPU %02d| ", percpu_get(cpu_id));
		off = strlen(buf);
	}

	time(&ts);
	off += strftime(buf + off, 32, "%H:%M:%S ", localtime(&ts));

	snprintf(buf + off, 6, "<%d>: ", level);
	off = strlen(buf);

	va_start(ptr, fmt);
	vsnprintf(buf + off, MAX_LOG_LEN - off, fmt, ptr);
	va_end(ptr);

	printf("%s", buf);
}

