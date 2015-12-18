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
 * buf.h = transmit data buffer management
 */

#pragma once

#include <ix/stddef.h>
#include <mempool.h>

#include <pthread.h>
#include <string.h>


#define BUF_SIZE	1460 * 4

extern __thread struct mempool ixev_buf_pool;

struct ixev_buf {
	uint32_t len;
	uint32_t pad;
	struct ixev_ref ref;
	char payload[BUF_SIZE];
};

static inline void ixev_buf_release(struct ixev_ref *ref)
{
	struct ixev_buf *buf = container_of(ref, struct ixev_buf, ref);
	mempool_free(&ixev_buf_pool, buf);
}

/**
 * ixev_buf_alloc - allocates a buffer
 *
 * The initial refcount is set to one.
 *
 * Returns a buffer, or NULL if out of memory.
 */
static inline struct ixev_buf *ixev_buf_alloc(void)
{
	struct ixev_buf *buf = mempool_alloc(&ixev_buf_pool);

	if (unlikely(!buf))
		return NULL;

	buf->len = 0;
	buf->ref.cb = &ixev_buf_release;

	return buf;
}

/**
 * ixev_buf_store - store data inside a buffer
 * @buf: the buffer
 * @addr: the start address of the data
 * @len: the length of the data
 *
 * Returns the numbers of bytes successfully stored in the buffer,
 * or zero if the buffer is full.
 */
static inline size_t ixev_buf_store(struct ixev_buf *buf, void *addr, size_t len)
{
	size_t avail = min(len, BUF_SIZE - buf->len);

	if (!avail)
		return 0;

	memcpy(&buf->payload[buf->len], addr, avail);
	buf->len += avail;

	return avail;
}

/**
 * ixev_is_buf_full - determines if the buffer is full
 * @buf: the buffer
 *
 * Returns true if the buffer is full, otherwise false.
 */
static inline bool ixev_is_buf_full(struct ixev_buf *buf)
{
	return buf->len == BUF_SIZE;
}


