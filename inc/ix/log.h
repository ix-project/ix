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
 * log.h - the logging service
 */

#pragma once

#include <ix/types.h>

extern __thread bool log_is_early_boot;

extern void logk(int level, const char *fmt, ...);

extern int max_loglevel;

enum {
	LOG_EMERG	= 0, /* system is dead */
	LOG_CRIT	= 1, /* critical */
	LOG_ERR	   	= 2, /* error */
	LOG_WARN	= 3, /* warning */
	LOG_INFO	= 4, /* informational */
	LOG_DEBUG	= 5, /* debug */
};

#define log_emerg(fmt, ...) logk(LOG_EMERG, fmt, ##__VA_ARGS__)
#define log_crit(fmt, ...) logk(LOG_CRIT, fmt, ##__VA_ARGS__)
#define log_err(fmt, ...) logk(LOG_ERR, fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) logk(LOG_WARN, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) logk(LOG_INFO, fmt, ##__VA_ARGS__)

#ifdef DEBUG
#define log_debug(fmt, ...) logk(LOG_DEBUG, fmt, ##__VA_ARGS__)
#else
#define log_debug(fmt, ...)
#endif

#define panic(fmt, ...) \
do {logk(LOG_EMERG, fmt, ##__VA_ARGS__); exit(-1); } while (0)

