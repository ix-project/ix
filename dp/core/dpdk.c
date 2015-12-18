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

/* For memmove and size_t */
#include <string.h>

/* For optind */
#include <unistd.h>

/* For struct sockaddr */
#include <sys/socket.h>

/* Prevent inclusion of rte_memcpy.h */
#define _RTE_MEMCPY_X86_64_H_
inline void *rte_memcpy(void *dst, const void *src, size_t n);

/* General DPDK includes */
#include <rte_config.h>
#include <eal_internal_cfg.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

/* IX includes */
#include <ix/log.h>
#include <ix/dpdk.h>

struct rte_mempool *dpdk_pool;

enum {
	DEV_DETACHED = 0,
	DEV_ATTACHED
};

int dpdk_init(void)
{
	int ret;
	char *argv[] = { "./ix", NULL };
	const int pool_buffer_size = 0;
	const int pool_cache_size = 0;
	const int pool_size = 8192;

	optind = 0;
	internal_config.no_hugetlbfs = 1;
	ret = rte_eal_init(1, argv);
	if (ret < 0)
		return ret;

	dpdk_pool = rte_pktmbuf_pool_create("mempool", pool_size, pool_cache_size, 0, pool_buffer_size, rte_socket_id());
	if (dpdk_pool == NULL)
		panic("Cannot create DPDK pool\n");

	return 0;
}

uint8_t rte_eth_dev_find_free_port(void)
{
	unsigned i;

	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		if (rte_eth_devices[i].attached == DEV_DETACHED)
			return i;
	}
	return RTE_MAX_ETHPORTS;
}
