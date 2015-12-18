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
 * net.c - the main file for the network subsystem
 */

#include <ix/stddef.h>
#include <ix/log.h>
#include <ix/cfg.h>

#include "net.h"

static void net_dump_cfg(void)
{
	char str[IP_ADDR_STR_LEN];
	struct ip_addr mask = {CFG.mask};

	log_info("net: using the following configuration:\n");

	ip_addr_to_str((struct ip_addr *)&CFG.host_addr, str);
	log_info("\thost IP:\t%s\n", str);
	ip_addr_to_str((struct ip_addr *)&CFG.broadcast_addr, str);
	log_info("\tbroadcast IP:\t%s\n", str);
	ip_addr_to_str((struct ip_addr *)&CFG.gateway_addr, str);
	log_info("\tgateway IP:\t%s\n", str);
	ip_addr_to_str(&mask, str);
	log_info("\tsubnet mask:\t%s\n", str);
}

/**
 * net_init - initializes the network stack
 *
 * Returns 0 if successful, otherwise fail.
 */
int net_init(void)
{
	int ret;

	ret = arp_init();
	if (ret) {
		log_err("net: failed to initialize arp\n");
		return ret;
	}

	return 0;
}

/**
 * net_cfg - load the network configuration parameters
 *
 * Returns 0 if successful, otherwise fail.
 */
int net_cfg(void)
{
	net_dump_cfg();

	return 0;
}

