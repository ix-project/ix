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

#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "syscall.h"
#include "ix.h"

static __thread bsysfn_t usys_tbl[USYS_NR];
static __thread struct bsys_arr *uarr;

__thread struct bsys_arr *karr;

/**
 * ix_poll - flush pending commands and check for new commands
 *
 * Returns the number of new commands received.
 */
int ix_poll(void)
{
	int ret;

	ret = sys_bpoll(karr->descs, karr->len);
	if (ret) {
		printf("libix: encountered a fatal memory fault\n");
		exit(-1);
	}

	return uarr->len;
}

void ix_handle_events(void)
{
	int i;

	for (i = 0; i < uarr->len; i++) {
		struct bsys_desc d = uarr->descs[i];
		usys_tbl[d.sysnr](d.arga, d.argb, d.argc, d.argd);
	}
}

/**
 * ix_flush - send pending commands
 */
void ix_flush(void)
{
	int ret;

	ret = sys_bcall(karr->descs, karr->len);
	if (ret) {
		printf("libix: encountered a fatal memory fault\n");
		exit(-1);
	}

	karr->len = 0;
}

static void
ix_default_udp_recv(void *addr, size_t len, struct ip_tuple *id)
{
	ix_udp_recv_done(addr);
}

static void
ix_default_tcp_knock(int handle, struct ip_tuple *id)
{
	ix_tcp_reject(handle);
}

/**
 * ix_init - initializes libIX
 * @ops: user-provided event handlers
 * @batch_depth: the maximum number of outstanding requests to the kernel
 *
 * Returns 0 if successful, otherwise fail.
 */
int ix_init(struct ix_ops *ops, int batch_depth)
{
	if (!ops)
		return -EINVAL;

	/* unpack the ops into a more efficient representation */
	usys_tbl[USYS_UDP_RECV]		= (bsysfn_t) ops->udp_recv;
	usys_tbl[USYS_UDP_SENT]		= (bsysfn_t) ops->udp_sent;
	usys_tbl[USYS_TCP_CONNECTED]	= (bsysfn_t) ops->tcp_connected;
	usys_tbl[USYS_TCP_KNOCK]	= (bsysfn_t) ops->tcp_knock;
	usys_tbl[USYS_TCP_RECV]		= (bsysfn_t) ops->tcp_recv;
	usys_tbl[USYS_TCP_SENT]		= (bsysfn_t) ops->tcp_sent;
	usys_tbl[USYS_TCP_DEAD]		= (bsysfn_t) ops->tcp_dead;
	usys_tbl[USYS_TIMER]		= (bsysfn_t) ops->timer_event;

	/* provide sane defaults so we don't leak memory */
	if (!ops->udp_recv)
		usys_tbl[USYS_UDP_RECV] = (bsysfn_t) ix_default_udp_recv;
	if (!ops->tcp_knock)
		usys_tbl[USYS_TCP_KNOCK] = (bsysfn_t) ix_default_tcp_knock;

	uarr = sys_baddr();
	if (!uarr)
		return -EFAULT;

	karr = malloc(sizeof(struct bsys_arr) +
		      sizeof(struct bsys_desc) * batch_depth);
	if (!karr)
		return -ENOMEM;

	karr->len = 0;
	karr->max_len = batch_depth;

	return 0;
}

