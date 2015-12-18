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
 * cfg.h - configuration parameters
 */

#pragma once

#include <ix/pci.h>
#include <net/ethernet.h>


#define CFG_MAX_PORTS    16
#define CFG_MAX_CPU     128
#define CFG_MAX_ETHDEV   16


struct cfg_ip_addr {
	uint32_t addr;
};

struct cfg_parameters {
	struct cfg_ip_addr host_addr;
	struct cfg_ip_addr broadcast_addr;
	struct cfg_ip_addr gateway_addr;
	uint32_t mask;

	struct eth_addr mac;

	int num_cpus;
	unsigned int cpu[CFG_MAX_CPU];

	int num_ethdev;
	struct pci_addr ethdev[CFG_MAX_ETHDEV];

	int num_ports;
	uint16_t ports[CFG_MAX_PORTS];

	char loader_path[256];
};

extern struct cfg_parameters CFG;




extern int cfg_init(int argc, char *argv[], int *args_parsed);

