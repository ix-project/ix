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

/* For struct sockaddr */
#include <sys/socket.h>

/* For pipe */
#include <unistd.h>

/* General DPDK includes */
#include <rte_config.h>
#include <rte_ethdev.h>

/* Defines from IX override DPDK */
#undef EAGAIN
#undef EBADMSG
#undef EDEADLK
#undef EMULTIHOP
#undef ENAMETOOLONG
#undef ENOLCK
#undef ENOLINK
#undef ENOSYS
#undef ENOTEMPTY
#undef EPROTO
#undef ETH_DCB_NONE
#undef ETH_DCB_RX
#undef ETH_DCB_TX
#undef ETH_LINK_FULL_DUPLEX
#undef ETH_LINK_HALF_DUPLEX
#undef ETH_LINK_SPEED_AUTONEG
#undef ETH_RSS
#undef ETH_RSS_IPV4
#undef ETH_RSS_IPV6
#undef ETH_RSS_IPV6_EX
#undef ETH_RSS_IPV6_TCP_EX
#undef ETH_RSS_IPV6_UDP_EX
#undef ETH_VMDQ_DCB_TX
#undef IXGBE_MIN_RING_DESC
#undef LIST_HEAD
#undef PKT_TX_IP_CKSUM
#undef PKT_TX_TCP_CKSUM
#undef VMDQ_DCB
#undef likely
#undef mb
#undef min
#undef prefetch
#undef rmb
#undef unlikely
#undef wmb

/* IX includes */
#include <ix/byteorder.h>
#include <ix/cfg.h>
#include <ix/ethdev.h>
#include <ix/dpdk.h>
#include <ix/drivers.h>

struct rte_eth_rxconf rx_conf;
struct rte_eth_txconf tx_conf;

struct drv_init_tble {
	char *name;
	int (*init_fn)(struct ix_rte_eth_dev *dev, const char *driver_name);
};

struct drv_init_tble drv_init_tbl[] = {
	{ "rte_ixgbe_pmd", ixgbe_init },
	{ "rte_ixgbevf_pmd", ixgbe_init },
	{ NULL, NULL }
};

static int rte_eal_pci_probe_one_driver(struct rte_pci_driver *dr, struct rte_pci_device *dev)
{
	const struct rte_pci_id *id_table;
	int pipefd[2];

	for (id_table = dr->id_table; id_table->vendor_id != 0; id_table++) {

		/* check if device's identifiers match the driver's ones */
		if (id_table->vendor_id != dev->id.vendor_id &&
			id_table->vendor_id != PCI_ANY_ID)
			continue;
		if (id_table->device_id != dev->id.device_id &&
			id_table->device_id != PCI_ANY_ID)
			continue;
		if (id_table->subsystem_vendor_id != dev->id.subsystem_vendor_id &&
			id_table->subsystem_vendor_id != PCI_ANY_ID)
			continue;
		if (id_table->subsystem_device_id != dev->id.subsystem_device_id &&
			id_table->subsystem_device_id != PCI_ANY_ID)
			continue;

		struct rte_pci_addr *loc = &dev->addr;

		RTE_LOG(DEBUG, EAL, "PCI device "PCI_PRI_FMT" on NUMA socket %i\n",
			loc->domain, loc->bus, loc->devid, loc->function,
			dev->numa_node);

		RTE_LOG(DEBUG, EAL, "  probe driver: %x:%x %s\n", dev->id.vendor_id,
			dev->id.device_id, dr->name);

		/* reference driver structure */
		dev->driver = dr;

		/* use a fake source for uio (interrupts) */
		if (pipe(pipefd))
			return 0;

		dev->intr_handle.fd = pipefd[1];
		dev->intr_handle.type = RTE_INTR_HANDLE_UIO;

		/* call the driver devinit() function */
		return dr->devinit(dr, dev);
	}
	/* return positive value if driver is not found */
	return 1;
}

static int dpdk_devinit(struct pci_dev *pci_dev, struct rte_pci_driver **found_driver)
{
	int ret;
	struct rte_pci_device *dpdk_pci_dev = NULL;
	struct rte_pci_addr addr;
	struct rte_pci_driver *driver;

	addr.domain = pci_dev->addr.domain;
	addr.bus = pci_dev->addr.bus;
	addr.devid = pci_dev->addr.slot;
	addr.function = pci_dev->addr.func;

	*found_driver = NULL;

	TAILQ_FOREACH(dpdk_pci_dev, &pci_device_list, next) {
		if (rte_eal_compare_pci_addr(&dpdk_pci_dev->addr, &addr))
			continue;

		dpdk_pci_dev->mem_resource[0].addr = pci_map_mem_bar(pci_dev, &pci_dev->bars[0], 0);

		TAILQ_FOREACH(driver, &pci_driver_list, next) {
			ret = rte_eal_pci_probe_one_driver(driver, dpdk_pci_dev);

			if (ret < 0) {
				/* negative value is an error */
				return -1;
			} else if (ret > 0) {
				/* positive value means driver not found */
				continue;
			}

			/* driver found */
			*found_driver = driver;
			return 0;
		}

		/* driver not found */
		return -1;
	}

	/* device not found */
	return -1;
}

static enum rte_eth_rx_mq_mode translate_conf_rxmode_mq_mode(enum ix_rte_eth_rx_mq_mode in)
{
	switch (in) {
	case IX_ETH_MQ_RX_RSS:
		return ETH_MQ_RX_RSS;
	default:
		assert(false);
	}
}

static enum rte_eth_tx_mq_mode translate_conf_txmode_mq_mode(enum ix_rte_eth_tx_mq_mode in)
{
	switch (in) {
	case IX_ETH_MQ_TX_NONE:
		return ETH_MQ_TX_NONE;
	default:
		assert(false);
	}
}

static uint16_t translate_conf_rss_hf(uint16_t in)
{
	uint16_t out = 0;

#define COPY_AND_RESET(dst, src, bit_out, bit_in) \
	if ((src) & (bit_in)) { \
		(dst) |= (bit_out); \
		(src) &= ~(bit_in); \
	}

	COPY_AND_RESET(out, in, ETH_RSS_NONFRAG_IPV4_TCP, ETH_RSS_IPV4_TCP);
	COPY_AND_RESET(out, in, ETH_RSS_NONFRAG_IPV4_UDP, ETH_RSS_IPV4_UDP);

#undef COPY_AND_RESET

	assert(in == 0);

	return out;
}

static void translate_conf(struct rte_eth_conf *out, struct ix_rte_eth_conf *in)
{
	char *p;

	memset(out, 0, sizeof(struct rte_eth_conf));

#define COPY_AND_RESET(field) \
	do { \
		out->field = in->field; \
		in->field = 0; \
	} while (0)

#define COPY_AND_RESET2(field, f) \
	do { \
		out->field = f(in->field); \
		in->field = 0; \
	} while (0)

	COPY_AND_RESET2(rx_adv_conf.rss_conf.rss_hf, translate_conf_rss_hf);
	COPY_AND_RESET(rxmode.header_split);
	COPY_AND_RESET(rxmode.hw_ip_checksum);
	COPY_AND_RESET(rxmode.hw_strip_crc);
	COPY_AND_RESET(rxmode.hw_vlan_filter);
	COPY_AND_RESET(rxmode.jumbo_frame);
	COPY_AND_RESET2(rxmode.mq_mode, translate_conf_rxmode_mq_mode);
	COPY_AND_RESET(rxmode.split_hdr_size);
	COPY_AND_RESET2(txmode.mq_mode, translate_conf_txmode_mq_mode);

#undef COPY_AND_RESET
#undef COPY_AND_RESET2

	for (p = (char *) in; p < (char *) in + sizeof(*in); p++)
		assert(!*p);
}

int driver_init(struct pci_dev *pci_dev, struct ix_rte_eth_dev **ethp)
{
	int i;
	int ret;
	struct ix_rte_eth_dev *dev;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_conf conf;
	struct rte_pci_driver *driver;
	struct drv_init_tble *dpdk_drv;
	uint8_t port;

	port = rte_eth_dev_find_free_port();

	ret = dpdk_devinit(pci_dev, &driver);
	if (ret < 0)
		return ret;

	rte_eth_dev_info_get(port, &dev_info);

	rx_conf = dev_info.default_rxconf;
	tx_conf = dev_info.default_txconf;

	/* We don't need a dev_private, thus a 1-byte allocation. */
	dev = eth_dev_alloc(1);
	dev->port = port;
	spin_lock_init(&dev->lock);

	translate_conf(&conf, &dev->data->dev_conf);

	conf.fdir_conf.mode = RTE_FDIR_MODE_PERFECT;
	conf.fdir_conf.pballoc = RTE_FDIR_PBALLOC_256K;
	conf.fdir_conf.status = RTE_FDIR_REPORT_STATUS;
	conf.fdir_conf.mask.vlan_tci_mask = 0x0;
	conf.fdir_conf.mask.ipv4_mask.src_ip = 0xFFFFFFFF;
	conf.fdir_conf.mask.ipv4_mask.dst_ip = 0xFFFFFFFF;
	conf.fdir_conf.mask.src_port_mask = 0xFFFF;
	conf.fdir_conf.mask.dst_port_mask = 0xFFFF;
	conf.fdir_conf.mask.mac_addr_byte_mask = 0;
	conf.fdir_conf.mask.tunnel_type_mask = 0;
	conf.fdir_conf.mask.tunnel_id_mask = 0;
	conf.fdir_conf.drop_queue = 127;
	conf.fdir_conf.flex_conf.nb_payloads = 0;
	conf.fdir_conf.flex_conf.nb_flexmasks = 0;

	ret = rte_eth_dev_configure(port, dev_info.max_rx_queues, dev_info.max_tx_queues, &conf);
	if (ret < 0)
		return ret;

	for (dpdk_drv = drv_init_tbl; dpdk_drv->name != NULL; dpdk_drv++) {
		if (strcmp(driver->name, dpdk_drv->name))
			continue;
		ret = dpdk_drv->init_fn(dev, dpdk_drv->name);
		if (ret < 0)
			return ret;
		break;
	}

	if (dpdk_drv->name == NULL)
		panic("No suitable DPDK driver found\n");

	dev->data->mac_addrs = calloc(1, ETH_ADDR_LEN);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		dev->data->mac_addrs[0].addr[i] = rte_eth_devices[port].data->mac_addrs[0].addr_bytes[i];
	*ethp = dev;

	return 0;
}
