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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <ixev.h>
#include <net/ip.h>

static unsigned long count;
static unsigned int msg_size;

enum {
	CLIENT_MODE_RECV,
	CLIENT_MODE_SEND,
};

struct client_conn {
	struct ixev_ctx ctx;
	struct ip_tuple id;
	int mode;
	size_t bytes_recvd;
	size_t bytes_sent;
	char *data;
};

static struct client_conn *c;

static void client_die(void)
{
	ixev_close(&c->ctx);
	fprintf(stderr, "remote connection was closed\n");
	exit(-1);
}

static void print_stats(void)
{
	ssize_t ret;
	char buf;
	static unsigned long prv_count;

	if (count - prv_count > 640000 / msg_size) {
		prv_count = count;
		ret = read(STDIN_FILENO, &buf, 1);
		if (ret == 0) {
			fprintf(stderr, "Error: EOF on STDIN.\n");
			exit(1);
		} else if (ret == -1 && errno == EAGAIN) {
		} else if (ret == -1) {
			perror("read");
			exit(1);
		} else {
			printf("%d %ld\n", msg_size, count);
			fflush(stdout);
		}
	}
}

static void main_handler(struct ixev_ctx *ctx, unsigned int reason)
{
	ssize_t ret;

	while (1) {
		if (c->mode == CLIENT_MODE_SEND) {
			ret = ixev_send_zc(ctx, &c->data[c->bytes_sent],
					   msg_size - c->bytes_sent);
			if (ret <= 0) {
				if (ret != -EAGAIN)
					client_die();
				return;
			}

			c->bytes_sent += ret;
			if (c->bytes_sent < msg_size)
				return;

			c->bytes_recvd = 0;
			ixev_set_handler(ctx, IXEVIN, &main_handler);
			c->mode = CLIENT_MODE_RECV;
		} else {
			ret = ixev_recv(ctx, &c->data[c->bytes_recvd],
					msg_size - c->bytes_recvd);
			if (ret <= 0) {
				if (ret != -EAGAIN)
					client_die();
				return;
			}

			c->bytes_recvd += ret;
			if (c->bytes_recvd < msg_size)
				return;

			count++;
			print_stats();

			c->bytes_sent = 0;
			ixev_set_handler(ctx, IXEVOUT, &main_handler);
			c->mode = CLIENT_MODE_SEND;
		}
	}
}

static struct ixev_ctx *client_accept(struct ip_tuple *id)
{
	return NULL;
}

static void client_release(struct ixev_ctx *ctx) { }

static void client_dialed(struct ixev_ctx *ctx, long ret)
{
	if (ret)
		fprintf(stderr, "failed to connect, ret = %ld\n", ret);

	c->mode = CLIENT_MODE_SEND;
	c->bytes_sent = 0;

	puts("ready");
	fflush(stdout);

	ixev_set_handler(ctx, IXEVOUT, &main_handler);
	main_handler(&c->ctx, IXEVOUT);
}

struct ixev_conn_ops stream_conn_ops = {
	.accept		= &client_accept,
	.release	= &client_release,
	.dialed		= &client_dialed,
};

static int parse_ip_addr(const char *str, uint32_t *addr)
{
	unsigned char a, b, c, d;

	if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
		return -EINVAL;

	*addr = MAKE_IP_ADDR(a, b, c, d);
	return 0;
}

int main(int argc, char *argv[])
{
	int ret;
	int flags;

	c = malloc(sizeof(struct client_conn));
	if (!c)
		exit(-1);

	if (argc != 4) {
		fprintf(stderr, "Usage: IP PORT MSG_SIZE\n");
		return -1;
	}

	if (parse_ip_addr(argv[1], &c->id.dst_ip)) {
		fprintf(stderr, "Bad IP address '%s'", argv[1]);
		exit(1);
	}

	c->id.dst_port = atoi(argv[2]);

	msg_size = atoi(argv[3]);
	c->data = malloc(msg_size);
	if (!c->data)
		exit(-1);

	ixev_init(&stream_conn_ops);

	ret = ixev_init_thread();
	if (ret) {
		fprintf(stderr, "unable to init IXEV\n");
		exit(ret);
	}

	flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	ixev_dial(&c->ctx, &c->id);

	while (1)
		ixev_wait();

	return 0;
}
