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
 * ixev.h - a library that behaves mostly like libevent.
 */

#pragma once

#include "ix.h"
#include <stdio.h>

/* FIXME: we won't need recv depth when i get a chance to fix the kernel */
#define IXEV_RECV_DEPTH	128
#define IXEV_SEND_DEPTH	16

struct ixev_ctx;
struct ixev_ref;
struct ixev_buf;

/*
 * FIXME: right now we only support some libevent features
 * - add level triggered support (now only edge triggered)
 * - add one fire events (now only persistent)
 * - add timeout support for events
 */

/* IX event types */
#define IXEVHUP		0x1 /* the connection was closed (or failed) */
#define IXEVIN		0x2 /* new data is available for reading */
#define IXEVOUT		0x4 /* more space is available for writing */

struct ixev_conn_ops {
	struct ixev_ctx *(*accept)(struct ip_tuple *id);
	void (*release)(struct ixev_ctx *ctx);
	void (*dialed)(struct ixev_ctx *ctx, long ret);
};

/*
 * Use this callback to receive network event notifications
 */
typedef void (*ixev_handler_t)(struct ixev_ctx *ctx, unsigned int reason);

/*
 * Use this callback to free memory after zero copy sends
 */
typedef void (*ixev_sent_cb_t)(struct ixev_ref *ref);

struct ixev_ref {
	ixev_sent_cb_t	cb;	  /* the decrement ref callback function */
	size_t		send_pos; /* the number of bytes sent before safe to free */
	struct ixev_ref	*next;    /* the next ref in the sequence */
};

struct ixev_ctx {
	hid_t		handle;			/* the IX flow handle */
	unsigned long	user_data;		/* application data */
	uint64_t	generation;		/* generation number */
	ixev_handler_t	handler;		/* the event handler */
	unsigned int	en_mask;		/* a mask of enabled events */
	unsigned int	trig_mask;		/* a mask of triggered events */
	uint16_t	recv_head;		/* received data SG head */
	uint16_t	recv_tail;		/* received data SG tail */
	uint16_t	send_count;		/* the current send SG count */
	uint16_t	is_dead: 1;		/* is the connection dead? */

	size_t		send_total;		/* the total requested bytes */
	size_t		sent_total;		/* the total completed bytes */
	struct ixev_ref	*ref_head;		/* list head of references */
	struct ixev_ref *ref_tail;		/* list tail of references */
	struct ixev_buf *cur_buf;		/* current buffer */

	struct bsys_desc *recv_done_desc;	/* the current recv_done bsys descriptor */
	struct bsys_desc *sendv_desc;		/* the current sendv bsys descriptor */

	struct sg_entry	recv[IXEV_RECV_DEPTH];	/* receieve SG array */
	struct sg_entry send[IXEV_SEND_DEPTH];	/* send SG array */
};

static inline void ixev_check_hacks(struct ixev_ctx *ctx)
{
	/*
	 * Temporary hack:
	 *
	 * FIXME: we need to flush commands in batches to limit our
	 * command buffer size. Then this restriction can be lifted.
	 */
	if (unlikely(karr->len >= karr->max_len)) {
		printf("ixev: ran out of command space\n");
		exit(-1);
	}
}

extern ssize_t ixev_recv(struct ixev_ctx *ctx, void *addr, size_t len);
extern void *ixev_recv_zc(struct ixev_ctx *ctx, size_t len);
extern ssize_t ixev_send(struct ixev_ctx *ctx, void *addr, size_t len);
extern ssize_t ixev_send_zc(struct ixev_ctx *ctx, void *addr, size_t len);
extern void ixev_add_sent_cb(struct ixev_ctx *ctx, struct ixev_ref *ref);

extern void ixev_close(struct ixev_ctx *ctx);

/**
 * ixev_dial - open a connection
 * @ctx: a freshly allocated and initialized context
 * @id: the address and port
 * @cb: the completion callback
 *
 * The completion returns a handle, or <0 if there was
 * an error.
 */
static inline void
ixev_dial(struct ixev_ctx *ctx, struct ip_tuple *id)
{
	struct bsys_desc *d = __bsys_arr_next(karr);
	ixev_check_hacks(ctx);

	ksys_tcp_connect(d, id, (unsigned long) ctx);
}

extern void ixev_ctx_init(struct ixev_ctx *ctx);
extern void ixev_wait(void);

extern void ixev_set_handler(struct ixev_ctx *ctx, unsigned int mask,
			     ixev_handler_t handler);

extern int ixev_init_thread(void);
extern int ixev_init(struct ixev_conn_ops *ops);

