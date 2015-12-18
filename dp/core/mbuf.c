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
 * mbuf.c - buffer management for network packets
 *
 * TODO: add support for mapping into user-level address space...
 */

#include <ix/stddef.h>
#include <ix/mem.h>
#include <ix/mempool.h>
#include <ix/mbuf.h>
#include <ix/cpu.h>

/* Capacity should be at least RX queues per CPU * ETH_DEV_RX_QUEUE_SZ */
#define MBUF_CAPACITY	(768*1024)

static struct mempool_datastore mbuf_datastore;

DEFINE_PERCPU(struct mempool, mbuf_mempool __attribute__((aligned(64))));

void mbuf_default_done(struct mbuf *m)
{
	mbuf_free(m);
}

/**
 * mbuf_init_cpu - allocates the core-local mbuf region
 *
 * Returns 0 if successful, otherwise failure.
 */

int mbuf_init_cpu(void)
{
	struct mempool *m = &percpu_get(mbuf_mempool);
	return mempool_create(m, &mbuf_datastore, MEMPOOL_SANITY_PERCPU, percpu_get(cpu_id));
}

/**
 * mbuf_init - allocate global mbuf
 */

int mbuf_init(void)
{
	int ret;
	struct mempool_datastore *m = &mbuf_datastore;
	BUILD_ASSERT(sizeof(struct mbuf) <= MBUF_HEADER_LEN);

	ret = mempool_create_datastore(m, MBUF_CAPACITY, MBUF_LEN, 1, MEMPOOL_DEFAULT_CHUNKSIZE, "mbuf");
	if (ret) {
		assert(0);
		return ret;
	}
	ret = mempool_pagemem_map_to_user(m);
	if (ret) {
		assert(0);
		mempool_pagemem_destroy(m);
		return ret;
	}
	return 0;
}

/**
 * mbuf_exit_cpu - frees the core-local mbuf region
 */
void mbuf_exit_cpu(void)
{
	mempool_pagemem_destroy(&mbuf_datastore);
}

