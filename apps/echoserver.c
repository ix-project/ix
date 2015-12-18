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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>

#include <ixev.h>
#include <mempool.h>

#define ROUND_UP(num, multiple) ((((num) + (multiple) - 1) / (multiple)) * (multiple))

struct pp_conn {
	struct ixev_ctx ctx;
	size_t bytes_left;
	char data[];
};

static size_t msg_size;

static struct mempool_datastore pp_conn_datastore;
static __thread struct mempool pp_conn_pool;

static void pp_main_handler(struct ixev_ctx *ctx, unsigned int reason);

static void pp_stream_handler(struct ixev_ctx *ctx, unsigned int reason)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
	size_t bytes_so_far = msg_size - conn->bytes_left;
	ssize_t ret;

	ret = ixev_send(ctx, &conn->data[bytes_so_far], conn->bytes_left);
	if (ret < 0) {
		if (ret != -EAGAIN)
			ixev_close(ctx);
		return;
	}

	conn->bytes_left -= ret;
	if (!conn->bytes_left) {
		conn->bytes_left = msg_size;
		ixev_set_handler(ctx, IXEVIN, &pp_main_handler);
	}
}

static void pp_main_handler(struct ixev_ctx *ctx, unsigned int reason)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
	ssize_t ret;

	while (1) {
		size_t bytes_so_far = msg_size - conn->bytes_left;

		ret = ixev_recv(ctx, &conn->data[bytes_so_far],
				conn->bytes_left);
		if (ret <= 0) {
			if (ret != -EAGAIN)
				ixev_close(ctx);
			return;
		}

		conn->bytes_left -= ret;
		if (conn->bytes_left)
			return;

		conn->bytes_left = msg_size;
		ret = ixev_send(ctx, &conn->data[0], conn->bytes_left);
		if (ret == -EAGAIN)
			ret = 0;
		if (ret < 0) {
			ixev_close(ctx);
			return;
		}

		conn->bytes_left -= ret;
		if (conn->bytes_left) {
			ixev_set_handler(ctx, IXEVOUT, &pp_stream_handler);
			return;
		}

		conn->bytes_left = msg_size;
	}
}

static struct ixev_ctx *pp_accept(struct ip_tuple *id)
{
	/* NOTE: we accept everything right now, did we want a port? */
	struct pp_conn *conn = mempool_alloc(&pp_conn_pool);
	if (!conn)
		return NULL;

	conn->bytes_left = msg_size;
	ixev_ctx_init(&conn->ctx);
	ixev_set_handler(&conn->ctx, IXEVIN, &pp_main_handler);

	return &conn->ctx;
}

static void pp_release(struct ixev_ctx *ctx)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);

	mempool_free(&pp_conn_pool, conn);
}

static struct ixev_conn_ops pp_conn_ops = {
	.accept		= &pp_accept,
	.release	= &pp_release,
};

static void *pp_main(void *arg)
{
	int ret;

	ret = ixev_init_thread();
	if (ret) {
		fprintf(stderr, "unable to init IXEV\n");
		return NULL;
	};

	ret = mempool_create(&pp_conn_pool, &pp_conn_datastore);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}

	while (1) {
		ixev_wait();
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	int i, nr_cpu;
	pthread_t tid;
	int ret;
	unsigned int pp_conn_pool_entries;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s MSG_SIZE [MAX_CONNECTIONS]\n", argv[0]);
		return -1;
	}

	msg_size = atol(argv[1]);

	if (argc >= 3)
		pp_conn_pool_entries = atoi(argv[2]);
	else
		pp_conn_pool_entries = 16 * 4096;

	pp_conn_pool_entries = ROUND_UP(pp_conn_pool_entries, MEMPOOL_DEFAULT_CHUNKSIZE);

	ret = ixev_init(&pp_conn_ops);
	if (ret) {
		fprintf(stderr, "failed to initialize ixev\n");
		return ret;
	}

	ret = mempool_create_datastore(&pp_conn_datastore, pp_conn_pool_entries, sizeof(struct pp_conn) + msg_size, 0, MEMPOOL_DEFAULT_CHUNKSIZE, "pp_conn");
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return ret;
	}

	nr_cpu = sys_nrcpus();
	if (nr_cpu < 1) {
		fprintf(stderr, "got invalid cpu count %d\n", nr_cpu);
		exit(-1);
	}
	nr_cpu--; /* don't count the main thread */

	sys_spawnmode(true);

	for (i = 0; i < nr_cpu; i++) {
		if (pthread_create(&tid, NULL, pp_main, NULL)) {
			fprintf(stderr, "failed to spawn thread %d\n", i);
			exit(-1);
		}
	}

	pp_main(NULL);
	return 0;
}

