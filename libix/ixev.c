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
 * ixev.h - the IX high-level event library
 */

#include <ix/stddef.h>
#include <mempool.h>
#include <stdio.h>
#include <errno.h>

#include "ixev.h"
#include "buf.h"
#include "ixev_timer.h"

#define CMD_BATCH_SIZE	4096

/* FIXME: implement automatic TCP Buffer Tuning, Jeffrey Semke et. al. */
#define IXEV_SEND_WIN_SIZE	65536

static __thread uint64_t ixev_generation;
static struct ixev_conn_ops ixev_global_ops;

static struct mempool_datastore ixev_buf_datastore;
__thread struct mempool ixev_buf_pool;

static inline void __ixev_check_generation(struct ixev_ctx *ctx)
{
	if (ixev_generation != ctx->generation) {
		ctx->generation = ixev_generation;
		ctx->recv_done_desc = NULL;
		ctx->sendv_desc = NULL;
	}
}

static inline void
__ixev_recv_done(struct ixev_ctx *ctx, size_t len)
{
	__ixev_check_generation(ctx);

	if (!ctx->recv_done_desc) {
		ctx->recv_done_desc = __bsys_arr_next(karr);
		ixev_check_hacks(ctx);
		ksys_tcp_recv_done(ctx->recv_done_desc, ctx->handle, len);
	} else {
		ctx->recv_done_desc->argb += (uint64_t) len;
	}
}

static inline void
__ixev_sendv(struct ixev_ctx *ctx, struct sg_entry *ents, unsigned int nrents)
{
	__ixev_check_generation(ctx);

	if (!ctx->sendv_desc) {
		ctx->sendv_desc = __bsys_arr_next(karr);
		ixev_check_hacks(ctx);
		ksys_tcp_sendv(ctx->sendv_desc, ctx->handle, ents, nrents);
	} else {
		ctx->sendv_desc->argb = (uint64_t) ents;
		ctx->sendv_desc->argc = (uint64_t) nrents;
	}
}

static inline void
__ixev_close(struct ixev_ctx *ctx)
{
	struct bsys_desc *d = __bsys_arr_next(karr);
	ixev_check_hacks(ctx);
	ksys_tcp_close(d, ctx->handle);
}

static void ixev_tcp_connected(hid_t handle, unsigned long cookie, long ret)
{
	struct ixev_ctx *ctx = (struct ixev_ctx *) cookie;

	ixev_global_ops.dialed(ctx, ret);
}

static void ixev_tcp_knock(hid_t handle, struct ip_tuple *id)
{
	struct ixev_ctx *ctx = ixev_global_ops.accept(id);

	if (!ctx) {
		ix_tcp_reject(handle);
		return;
	}

	ctx->handle = handle;
	ix_tcp_accept(handle, (unsigned long) ctx);
}

static void ixev_tcp_dead(hid_t handle, unsigned long cookie)
{
	struct ixev_ctx *ctx = (struct ixev_ctx *) cookie;

	if (!ctx)
		return;

	ctx->is_dead = true;
	if (ctx->en_mask & IXEVHUP)
		ctx->handler(ctx, IXEVHUP);
	else if (ctx->en_mask & IXEVIN)
		ctx->handler(ctx, IXEVIN | IXEVHUP);
	else
		ctx->trig_mask |= IXEVHUP;

	ctx->en_mask = 0;
}

static void ixev_tcp_recv(hid_t handle, unsigned long cookie,
			  void *addr, size_t len)
{
	struct ixev_ctx *ctx = (struct ixev_ctx *) cookie;
	uint16_t pos = ((ctx->recv_tail) & (IXEV_RECV_DEPTH - 1));
	struct sg_entry *ent;

	if (unlikely(ctx->recv_tail - ctx->recv_head + 1 >= IXEV_RECV_DEPTH)) {
		/*
		 * FIXME: We've run out of space for received packets. This
		 * will probably not happen unless the application is
		 * misbehaving and does not accept new packets for long
		 * periods of time or if a remote host sends a lot of too
		 * small packets.
		 *
		 * Possible solutions:
		 * 1.) Start copying into a buffer and accept up to the kernel
		 * receive window size in data.
		 * 2.) Allocate more descriptors and leave memory in mbufs, but
		 * this could waste memory.
		 */
		printf("ixev: ran out of receive memory\n");
		exit(-1);
	}

	ent = &ctx->recv[pos];
	ent->base = addr;
	ent->len = len;
	ctx->recv_tail++;

	if (ctx->en_mask & IXEVIN)
		ctx->handler(ctx, IXEVIN);
	else
		ctx->trig_mask |= IXEVIN;
}

static void ixev_tcp_sent(hid_t handle, unsigned long cookie, size_t len)
{
	struct ixev_ctx *ctx = (struct ixev_ctx *) cookie;
	struct ixev_ref *ref = ctx->ref_head;

	ctx->sent_total += len;

	while (ref && ref->send_pos <= ctx->sent_total) {
		ref->cb(ref);
		ref = ref->next;
	}

	ctx->ref_head = ref;
	if (!ctx->ref_head)
		ctx->cur_buf = NULL;

	/* if there is pending data, make sure we try again to send it */
	if (ctx->send_count)
		__ixev_sendv(ctx, ctx->send, ctx->send_count);

	if (ctx->en_mask & IXEVOUT)
		ctx->handler(ctx, IXEVOUT);
	else
		ctx->trig_mask |= IXEVOUT;
}

static void ixev_timer_event(unsigned long cookie)
{
	struct ixev_timer *t = (struct ixev_timer *) cookie;

	t->handler(t->arg);
}

static struct ix_ops ixev_ops = {
	.tcp_connected	= ixev_tcp_connected,
	.tcp_knock	= ixev_tcp_knock,
	.tcp_dead	= ixev_tcp_dead,
	.tcp_recv	= ixev_tcp_recv,
	.tcp_sent	= ixev_tcp_sent,
	.timer_event	= ixev_timer_event,
};

/**
 * ixev_recv - read data with copying
 * @ctx: the context
 * @buf: a buffer to store the data
 * @len: the length to read
 *
 * Returns the number of bytes read, or 0 if there wasn't any data.
 */
ssize_t ixev_recv(struct ixev_ctx *ctx, void *buf, size_t len)
{
	size_t pos = 0;
	char *cbuf = (char *) buf;

	if (ctx->is_dead)
		return -EIO;

	while (ctx->recv_head != ctx->recv_tail) {
		struct sg_entry *ent =
			&ctx->recv[ctx->recv_head & (IXEV_RECV_DEPTH - 1)];
		size_t left = len - pos;

		if (!left)
			break;

		if (left >= ent->len) {
			memcpy(cbuf + pos, ent->base, ent->len);
			pos += ent->len;
			ctx->recv_head++;
		} else {
			memcpy(cbuf + pos, ent->base, left);
			ent->base = (char *) ent->base + left;
			ent->len -= left;
			pos += left;
			break;
		}
	}

	if (!pos)
		return -EAGAIN;

	__ixev_recv_done(ctx, pos);
	return pos;
}

/**
 * ixev_recv_zc - read an exact amount of data without copying
 * @ctx: the context
 * @len: the length to read
 *
 * If a buffer is returned, it must be used immediately. Similar
 * to alloca(), the buffer is no longer safe to access after the
 * caller returns. Moreover, the buffer is no longer safe to
 * access after the next command is issued to this library, except
 * if that command is another recv on the same flow.
 *
 * Returns a pointer if the requested amount of data is available
 * and contiguous, otherwise NULL. It may still be possible to
 * read some or all of the data with ixev_recv() if the return
 * value is NULL.
 */
void *ixev_recv_zc(struct ixev_ctx *ctx, size_t len)
{
	struct sg_entry *ent;
	void *buf;

	if (ctx->is_dead)
		return NULL;

	ent = &ctx->recv[ctx->recv_head & (IXEV_RECV_DEPTH - 1)];
	if (len > ent->len)
		return NULL;

	buf = ent->base;
	ent->base = (char *) ent->base + len;
	ent->len -= len;
	if (!ent->len)
		ctx->recv_head++;

	__ixev_recv_done(ctx, len);
	return buf;
}

static struct sg_entry *ixev_next_entry(struct ixev_ctx *ctx)
{
	struct sg_entry *ent = &ctx->send[ctx->send_count];

	ctx->send_count++;
	__ixev_sendv(ctx, ctx->send, ctx->send_count);

	return ent;
}

static void ixev_update_send_stats(struct ixev_ctx *ctx, size_t len)
{
	ctx->send_total += len;
}

static void __ixev_add_sent_cb(struct ixev_ctx *ctx, struct ixev_ref *ref)
{
	ref->next = NULL;

	if (!ctx->ref_head) {
		ctx->ref_head = ref;
		ctx->ref_tail = ref;
	} else {
		ctx->ref_tail->next = ref;
		ctx->ref_tail = ref;
	}
}

static size_t ixev_window_len(struct ixev_ctx *ctx, size_t len)
{
	size_t win_left = IXEV_SEND_WIN_SIZE -
			  ctx->send_total + ctx->sent_total;

	return min(win_left, len);
}

/*
 * ixev_send - send data using copying
 * @ctx: the context
 * @addr: the address of the data
 * @len: the length of the data
 *
 * This variant is easier to use but is slower because it must first
 * copy the data to a buffer.
 *
 * Returns the number of bytes sent, or <0 if there was an error.
 */
ssize_t ixev_send(struct ixev_ctx *ctx, void *addr, size_t len)
{
	size_t actual_len = ixev_window_len(ctx, len);
	struct sg_entry *ent;
	char *caddr = (char *) addr;
	ssize_t ret, so_far = 0;

	if (ctx->is_dead)
		return -EIO;

	if (!actual_len)
		return -EAGAIN;

	/* hot path: is there already a buffer? */
	if (ctx->send_count && ctx->cur_buf) {
		ret = ixev_buf_store(ctx->cur_buf, caddr, actual_len);
		ent = &ctx->send[ctx->send_count - 1];
		ent->len += ret;

		actual_len -= ret;
		caddr += ret;
		so_far += ret;

		ctx->cur_buf->ref.send_pos = ctx->send_total + so_far;
	}

	/* cold path: allocate and fill new buffers */
	while (actual_len) {
		if (ctx->send_count >= IXEV_SEND_DEPTH)
			goto out;

		ctx->cur_buf = ixev_buf_alloc();
		if (unlikely(!ctx->cur_buf))
			goto out;

		ret = ixev_buf_store(ctx->cur_buf, caddr, actual_len);
		ent = ixev_next_entry(ctx);
		ent->base = ctx->cur_buf->payload;
		ent->len = ret;

		actual_len -= ret;
		caddr += ret;
		so_far += ret;

		__ixev_add_sent_cb(ctx, &ctx->cur_buf->ref);
		ctx->cur_buf->ref.send_pos = ctx->send_total + so_far;
	}

out:
	if (!so_far)
		return -EAGAIN;

	ixev_update_send_stats(ctx, so_far);
	return so_far;
}

/*
 * ixev_send_zc - send data using zero-copy
 * @ctx: the context
 * @addr: the address of the data
 * @len: the length of the data
 *
 * Note that the data buffer must be properly reference counted. Use
 * ixev_add_sent_cb() to register a completion callback after the data
 * has been fully accepted by ixev_send_zc().
 *
 * Returns the number of bytes sent, or <0 if there was an error.
 */
ssize_t ixev_send_zc(struct ixev_ctx *ctx, void *addr, size_t len)
{
	struct sg_entry *ent;
	size_t actual_len = ixev_window_len(ctx, len);

	if (ctx->is_dead)
		return -EIO;
	if (!actual_len)
		return -EAGAIN;
	if (ctx->send_count >= IXEV_SEND_DEPTH)
		return -EAGAIN;

	ctx->cur_buf = NULL;

	ent = ixev_next_entry(ctx);
	ent->base = addr;
	ent->len = actual_len;

	ixev_update_send_stats(ctx, actual_len);
	return actual_len;
}

/**
 * ixev_add_sent_cb - registers a callback for when all current sends complete
 * @ctx: the context
 * @ref: the callback handle
 *
 * This function is intended for freeing references to zero-copy memory.
 */
void ixev_add_sent_cb(struct ixev_ctx *ctx, struct ixev_ref *ref)
{
	ref->send_pos = ctx->send_total;
	__ixev_add_sent_cb(ctx, ref);
}

/**
 * ixev_close - closes a context
 * @ctx: the context
 */
void ixev_close(struct ixev_ctx *ctx)
{
	ctx->en_mask = 0;
	__ixev_close(ctx);
}

/**
 * ixev_ctx_init - prepares a context for use
 * @ctx: the context
 * @ops: the event ops
 */
void ixev_ctx_init(struct ixev_ctx *ctx)
{
	ctx->en_mask = 0;
	ctx->trig_mask = 0;
	ctx->recv_head = 0;
	ctx->recv_tail = 0;
	ctx->send_count = 0;
	ctx->recv_done_desc = NULL;
	ctx->sendv_desc = NULL;
	ctx->generation = 0;
	ctx->is_dead = false;

	ctx->send_total = 0;
	ctx->sent_total = 0;
	ctx->ref_head = NULL;
	ctx->cur_buf = NULL;
}

static void ixev_bad_ret(struct ixev_ctx *ctx, uint64_t sysnr, long ret)
{
	printf("ixev: unexpected failure return code %ld\n", ret);
	printf("ixev: sysnr %ld, ctx %p\n", sysnr, ctx);
	printf("ixev: error is fatal, terminating...\n");

	exit(-1);
}

static void ixev_shift_sends(struct ixev_ctx *ctx, int shift)
{
	int i;

	ctx->send_count -= shift;

	for (i = 0; i < ctx->send_count; i++)
		ctx->send[i] = ctx->send[i + shift];
}

static void ixev_handle_sendv_ret(struct ixev_ctx *ctx, long ret)
{
	int i;

	if (ret < 0) {
		ctx->is_dead = true;
		return;
	}

	for (i = 0; i < ctx->send_count; i++) {
		struct sg_entry *ent = &ctx->send[i];
		if (ret < ent->len) {
			ent->len -= ret;
			ent->base = (char *) ent->base + ret;
			break;
		}

		ret -= ent->len;
	}

	ixev_shift_sends(ctx, i);
}

static void ixev_handle_close_ret(struct ixev_ctx *ctx, long ret)
{
	struct ixev_ref *ref = ctx->ref_head;

	if (unlikely(ret < 0)) {
		printf("ixev: failed to close handle, ret = %ld\n", ret);
		return;
	}

	while (ref) {
		ref->cb(ref);
		ref = ref->next;
	}

	ixev_global_ops.release(ctx);
}

static void ixev_handle_one_ret(struct bsys_ret *r)
{
	struct ixev_ctx *ctx = (struct ixev_ctx *) r->cookie;
	uint64_t sysnr = r->sysnr;
	long ret = r->ret;

	switch (sysnr) {
	case KSYS_TCP_CONNECT:
		ctx->handle = ret;
		/*
		 * FIXME: need to propogate this error to the app, but we
		 * can't safely do so here because the app might make an
		 * unsafe batched call during return processing. We need
		 * a mechanism to defer failure reporting until after
		 * return processing.
		 */
		if (unlikely(ret < 0)) {
			printf("ixev: connect failed with %ld\n", ret);
		}
		break;

	case KSYS_TCP_SENDV:
		ixev_handle_sendv_ret(ctx, ret);
		break;

	case KSYS_TCP_CLOSE:
		ixev_handle_close_ret(ctx, ret);
		break;

	default:
		if (unlikely(ret))
			ixev_bad_ret(ctx, sysnr, ret);
	}
}

/**
 * ixev_wait - wait for new events
 */
void ixev_wait(void)
{
	int i;

	/*
	 * FIXME: don't use the low-level library,
	 * just make system calls directly.
	 */

	ix_poll();
	ixev_generation++;

	/* WARNING: return handlers should not enqueue new comamnds */
	for (i = 0; i < karr->len; i++)
		ixev_handle_one_ret((struct bsys_ret *) &karr->descs[i]);
	karr->len = 0;

	ix_handle_events();
}


/**
 * ixev_set_handler - sets the event handler and which events trigger it
 * @ctx: the context
 * @mask: the event types that trigger
 * @handler: the handler
 */
void ixev_set_handler(struct ixev_ctx *ctx, unsigned int mask,
		      ixev_handler_t handler)
{
	ctx->en_mask = mask;
	ctx->handler = handler;
}

/**
 * ixev_init_thread - thread-local initializer
 *
 * Call once per thread.
 *
 * Returns zero if successful, otherwise fail.
 */
int ixev_init_thread(void)
{
	int ret;

	ret = mempool_create(&ixev_buf_pool, &ixev_buf_datastore);
	if (ret)
		return ret;

	ret = ix_init(&ixev_ops, CMD_BATCH_SIZE);
	if (ret) {
		mempool_destroy(&ixev_buf_pool);
		return ret;
	}

	return 0;
}

/**
 * ixev_init - global initializer
 * @conn_ops: operations for establishing new connections
 *
 * Call once.
 *
 * Returns zero if successful, otherwise fail.
 */
int ixev_init(struct ixev_conn_ops *ops)
{
	/* FIXME: check if running inside IX */
	int ret;

	ret = mempool_create_datastore(&ixev_buf_datastore, 131072, sizeof(struct ixev_buf), 0, MEMPOOL_DEFAULT_CHUNKSIZE, "ixev_buf");
	if (ret)
		return ret;

	ixev_global_ops = *ops;
	return 0;
}

