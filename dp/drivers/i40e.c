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
#include <rte_sctp.h>
#include <rte_tcp.h>
#include <rte_udp.h>

/* DPDK i40e includes */
#define X722_SUPPORT
#include <base/i40e_type.h>
#include <i40e_ethdev.h>
#include <i40e_rxtx.h>

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
#undef container_of
#undef ARRAY_SIZE
#undef max

/* IX includes */
#include <ix/byteorder.h>
#include <ix/cfg.h>
#include <ix/dpdk.h>
#include <ix/drivers.h>
#include <ix/ethdev.h>
#include <ix/log.h>

#define I40E_RING_BASE_ALIGN 128
#define I40E_RDT_THRESH 32
#define I40E_TX_MAX_BURST  32
#define DEFAULT_TX_FREE_THRESH 32
#define DEFAULT_TX_RS_THRESH 32

struct rx_entry {
	struct mbuf *mbuf;
};

struct rx_queue {
	struct eth_rx_queue	erxq;
	void			*ring;
	machaddr_t		ring_physaddr;
	struct rx_entry		*ring_entries;

	volatile uint32_t 	*rdt_reg_addr;
	uint16_t 		reg_idx;

	uint16_t 		head;
	uint16_t 		tail;
	uint16_t		len;
};

#define eth_rx_queue_to_drv(rxq) container_of(rxq, struct rx_queue, erxq)

struct tx_entry {
	struct mbuf *mbuf;
};

struct tx_queue {
	struct eth_tx_queue	etxq;
	void			*ring;
	machaddr_t		ring_physaddr;
	struct tx_entry		*ring_entries;

	volatile uint32_t	*tdt_reg_addr;
	uint16_t		reg_idx;
	uint16_t		queue_id;

	uint16_t		head;
	uint16_t		tail;
	uint16_t		len;

	uint16_t 		nb_tx_used;
	uint16_t 		nb_tx_free;
	uint16_t 		last_desc_cleaned;

	uint16_t 		tx_rs_thresh;
	uint16_t 		tx_free_thresh;

	uint16_t 		nb_tx_desc;
	uint16_t 		tx_next_dd;
	uint16_t 		tx_next_rs;
};

#define eth_tx_queue_to_drv(txq) container_of(txq, struct tx_queue, etxq)

static int i40e_alloc_rx_mbufs(struct rx_queue *rxq)
{
	int i;

	for (i = 0; i < rxq->len; i++) {
		machaddr_t maddr;
		struct mbuf *b = mbuf_alloc_local();
		if (!b)
			goto fail;

		maddr = mbuf_get_data_machaddr(b);
		rxq->ring_entries[i].mbuf = b;
		((volatile union i40e_rx_desc *)rxq->ring)[i].read.hdr_addr = 0;
		((volatile union i40e_rx_desc *)rxq->ring)[i].read.pkt_addr = rte_cpu_to_le_64(maddr);
	}

	return 0;

fail:
	for (i--; i >= 0; i--)
		mbuf_free(rxq->ring_entries[i].mbuf);
	return -ENOMEM;
}

static int dev_start(struct ix_rte_eth_dev *dev)
{
	int ret = 0;
	int i;
	struct eth_rx_queue *erxq;
	struct eth_tx_queue *etxq;
	struct i40e_rx_queue *drxq;
	struct i40e_tx_queue *dtxq;
	struct i40e_hmc_obj_rxq rx_ctx;
	struct i40e_hmc_obj_txq tx_ctx;
	struct i40e_hw *hw;
	uint16_t pf_q;

	ret = rte_eth_dev_start(dev->port);
	if (ret < 0)
		return ret;

	/* Init the RX/TX queues in hardware to use our mbufs instead of dpdk's*/
	for (i = 0; i < dev->data->nb_rx_queues; i++){
		erxq = (struct eth_rx_queue *)dev->data->rx_queues[i];
		struct rx_queue *rxq = eth_rx_queue_to_drv(erxq);
		drxq = rte_eth_devices[dev->port].data->rx_queues[i];
		/* because these values are not initialized until dev_start
		 * we save them at this point :*/
		rxq->reg_idx = drxq->reg_idx;
		rxq->rdt_reg_addr = (uint32_t*) drxq->qrx_tail;

		pf_q = drxq->reg_idx;
		hw = I40E_VSI_TO_HW(drxq->vsi);

		ret = i40e_switch_rx_queue(hw, pf_q, FALSE);
		if (ret < 0) {
			log_err("Failed to switch off LAN RX queue\n");
			return ret;
		}

		/* Clear the context structure first */
		memset(&rx_ctx, 0, sizeof(struct i40e_hmc_obj_rxq));
		rx_ctx.dbuff = 0x1ff;
		rx_ctx.hbuff = 0;

		#ifndef RTE_LIBRTE_I40E_16BYTE_RX_DESC
		rx_ctx.dsize = 1;
		#endif
		rx_ctx.dtype = 0;
		rx_ctx.hsplit_0 = I40E_HEADER_SPLIT_NONE;
		rx_ctx.rxmax = 0x5ee;
		rx_ctx.tphrdesc_ena = 1;
		rx_ctx.tphwdesc_ena = 1;
		rx_ctx.tphdata_ena = 1;
		rx_ctx.tphhead_ena = 1;
		rx_ctx.lrxqthresh = 2;
		rx_ctx.crcstrip = 1;
		rx_ctx.l2tsel = 1;
		/* showiv indicates if inner VLAN is stripped inside of tunnel
		* packet. When set it to 1, vlan information is stripped from
		* the inner header, but the hardware does not put it in the
		* descriptor. So set it zero by default.
		*/
		rx_ctx.showiv = 0;
		rx_ctx.prefena = 1;

		ret = i40e_alloc_rx_mbufs(rxq);
		if (ret) {
			log_err("failed to allocate RX mbuffs.\n");
			return ret;
		}
		/* write to our own buffs*/
		rx_ctx.base = rxq->ring_physaddr / I40E_QUEUE_BASE_ADDR_UNIT;
		rx_ctx.qlen = rxq->len;

		ret = i40e_clear_lan_rx_queue_context(hw, pf_q);
		if (ret != I40E_SUCCESS) {
			PMD_DRV_LOG(ERR, "Failed to clear LAN RX queue context");
			return ret;
		}

		ret = i40e_set_lan_rx_queue_context(hw, pf_q, &rx_ctx);
		if (ret != I40E_SUCCESS) {
			log_err("Failed to set LAN RX queue context.\n");
			return ret;
		}
		/*init the tail register*/
		I40E_PCI_REG_WRITE(rxq->rdt_reg_addr, rxq->len - 1);

		ret = i40e_get_lan_rx_queue_context(hw, pf_q, &rx_ctx);
		if (ret < 0) {
			log_err("Failed to get LAN RX queue context.\n");
			return ret;
		}

		ret = i40e_switch_rx_queue(hw, pf_q, TRUE);
		if (ret < 0) {
			log_err("Failed to switch on LAN RX queue\n");
			return ret;
		}

	}

	for (i = 0; i < dev->data->nb_tx_queues; i++) {
		etxq = (struct eth_tx_queue *)dev->data->tx_queues[i];
		struct tx_queue *txq = eth_tx_queue_to_drv(etxq);
		dtxq = rte_eth_devices[dev->port].data->tx_queues[i];
		txq->reg_idx = dtxq->reg_idx;
		txq->tdt_reg_addr = (uint32_t *)dtxq->qtx_tail;
		pf_q = dtxq->reg_idx;
		hw = I40E_VSI_TO_HW(dtxq->vsi);


		ret = i40e_switch_tx_queue(hw, pf_q, FALSE);
		if (ret < 0) {
			log_err("Failed to switch on LAN RX queue\n");
			return ret;
		}

		/* clear the context structure first */
		memset(&tx_ctx, 0, sizeof(tx_ctx));
		tx_ctx.new_context = 1;
		#ifdef RTE_LIBRTE_IEEE1588
			tx_ctx.timesync_ena = 1;
		#endif
		tx_ctx.rdylist = 1;
		tx_ctx.fd_ena = TRUE;
		tx_ctx.base = txq->ring_physaddr / I40E_QUEUE_BASE_ADDR_UNIT;
		tx_ctx.qlen = txq->len;

		ret = i40e_set_lan_tx_queue_context(hw, pf_q, &tx_ctx);
		if (ret != I40E_SUCCESS) {
			log_err("Failed to set LAN TX queue context.\n");
			return ret;
		}

		ret = i40e_switch_tx_queue(hw, pf_q, TRUE);
		if (ret < 0) {
			log_err("Failed to switch on LAN RX queue\n");
			return ret;
		}

		rte_eth_dev_start(dev->port);
	}

	return 0;
}

static DEFINE_SPINLOCK(i40e_dev_lock);

static int reta_update(struct ix_rte_eth_dev *dev, struct rte_eth_rss_reta *reta_conf)
{
	int ret, i, j;
	struct rte_eth_rss_reta_entry64 r_reta_conf[dev->data->nb_rx_fgs / 64];
	struct rte_eth_dev r_dev = rte_eth_devices[dev->port];

	/* first convert reta_conf to dpdk format*/
	for (i = 0; i < dev->data->nb_rx_fgs / 64; i++) {
		r_reta_conf[i].mask = reta_conf->mask[BITMAP_POS_IDX(i * 64)] & 0xFFFFFFFF;
		r_reta_conf[i].mask = (reta_conf->mask[BITMAP_POS_IDX(i * 64 + 32)]
				>> BITMAP_POS_SHIFT(32)) & 0xFFFFFFFF;
	}

	for (i = 0; i < dev->data->nb_rx_fgs / 64; ++i)
		for (j = 0; j < 64; j++)
			r_reta_conf[i].reta[j] = reta_conf->reta[i * 64 + j];

	spin_lock(&i40e_dev_lock);
	ret = r_dev.dev_ops->reta_update(&r_dev, r_reta_conf, dev->data->nb_rx_fgs);
	spin_unlock(&i40e_dev_lock);
	if (ret) {
		log_err("i40e: unable to update receive side scaling rerouting table (RETA): %i\n", ret);
		return ret;
	}
	return 0;
}

static int i40e_rx_poll(struct eth_rx_queue *rx)
{
	struct rx_queue *rxq = eth_rx_queue_to_drv(rx);
	volatile union i40e_rx_desc *rxdp;
	union i40e_rx_desc rxd;
	uint64_t qword1;
	uint64_t error_bits;
	uint32_t rx_status;
	struct mbuf *b, *new_b;
	struct rx_entry *rxqe;
	machaddr_t maddr;
	int nb_descs = 0;
	bool valid_checksum;
	int local_fg_id;
	long timestamp;

	timestamp = rdtsc();
	while (1) {
		rxdp = &((volatile union i40e_rx_desc *)rxq->ring)[rxq->head & (rxq->len - 1)];
		qword1 = rte_le_to_cpu_64(rxdp->wb.qword1.status_error_len);
		rx_status = (qword1 & I40E_RXD_QW1_STATUS_MASK) >> I40E_RXD_QW1_STATUS_SHIFT;

		valid_checksum = true;
		/* this check that there is at least one packet to receive :*/
		if (!(rx_status & (1 << I40E_RX_DESC_STATUS_DD_SHIFT))) {
			break;
		}
		rxd = *rxdp;
		rxqe = &rxq->ring_entries[rxq->head & (rxq->len - 1)];

		error_bits = (qword1 >> I40E_RXD_QW1_ERROR_SHIFT);
		/* Check IP checksum calculated by hardware (if applicable) */
		if (unlikely(error_bits & (1 << I40E_RX_DESC_ERROR_IPE_SHIFT))) {
			log_err("i40e: IP RX checksum error, dropping pkt\n");
			valid_checksum = false;
		}

		/* Check TCP checksum calculated by hardware (if applicable) */
		if (unlikely(error_bits & (1 << I40E_RX_DESC_ERROR_L4E_SHIFT))) {
			log_err("i40e: TCP RX checksum error, dropping pkt\n");
			valid_checksum = false;
		}

		/* translate descriptor info into mbuf parameters */
		b = rxqe->mbuf;
		b->len = ((qword1 & I40E_RXD_QW1_LENGTH_PBUF_MASK) >> I40E_RXD_QW1_LENGTH_PBUF_SHIFT);

		if (qword1 & (1 << I40E_RX_DESC_STATUS_FLM_SHIFT)) {
			b->fg_id = MBUF_INVALID_FG_ID;
		} else {
			local_fg_id = (le32_to_cpu(rxd.wb.qword0.hi_dword.rss) & (rx->dev->data->nb_rx_fgs - 1));
			b->fg_id = rx->dev->data->rx_fgs[local_fg_id].fg_id;
		}
		b->timestamp = timestamp;

		new_b = mbuf_alloc_local();
		if (unlikely(!new_b)) {
			log_err("i40e: unable to allocate RX mbuf\n");
			goto out;
		}

		maddr = mbuf_get_data_machaddr(new_b);
		rxqe->mbuf = new_b;
		rxdp->read.hdr_addr = rte_cpu_to_le_64(maddr);
		rxdp->read.pkt_addr = rte_cpu_to_le_64(maddr);

		if (unlikely(!valid_checksum || eth_recv(rx, b))) {
			log_info("i40e: dropping packet\n");
			mbuf_free(b);
		}

		rxq->head++;
		nb_descs++;
	}

out:

	/*
	 * We threshold updates to the RX tail register because when it
	 * is updated too frequently (e.g. when written to on multiple
	 * cores even through separate queues) PCI performance
	 * bottlnecks have been observed.
	 */
	if ((uint16_t)(rxq->len - (rxq->tail + 1 - rxq->head)) >= I40E_RDT_THRESH) {
		rxq->tail = rxq->head + rxq->len - 1;

		/* inform HW that more descriptors have become available */
		I40E_PCI_REG_WRITE(rxq->rdt_reg_addr, (rxq->tail & (rxq->len - 1)));
	}

	return nb_descs;
}

static bool i40e_rx_ready(struct eth_rx_queue *rx)
{
	volatile union i40e_rx_desc *rxdp;
	struct rx_queue *rxq;
	uint64_t qword1;
	uint32_t rx_status;

	rxq = eth_rx_queue_to_drv(rx);
	rxdp = &((volatile union i40e_rx_desc *)rxq->ring)[rxq->head & (rxq->len - 1)];
	qword1 = rte_le_to_cpu_64(rxdp->wb.qword1.status_error_len);
	rx_status = (qword1 & I40E_RXD_QW1_STATUS_MASK) >> I40E_RXD_QW1_STATUS_SHIFT;
	return rx_status & (1 << I40E_RX_DESC_STATUS_DD_SHIFT);
}

/**
 * rx_queue_setup - prepares an RX queue
 * @dev: the ethernet device
 * @queue_idx: the queue number
 * @numa_node: the desired NUMA affinity, or -1 for no preference
 * @nb_desc: the number of descriptors to create for the ring
 *
 * Returns 0 if successful, otherwise failure.
 */
static int rx_queue_setup(struct ix_rte_eth_dev *dev, int queue_idx, int numa_node, uint16_t nb_desc)
{
	void *page;
	machaddr_t page_phys;
	int ret;
	struct rx_queue *rxq;

	/*
	 * The number of receive descriptors must not exceed hardware
	 * maximum and must be a multiple of I40E_RING_BASE_ALIGN.
	 */
	BUILD_ASSERT(align_up(sizeof(struct rx_queue), I40E_RING_BASE_ALIGN) +
			align_up(sizeof(union i40e_rx_desc) * I40E_MAX_RING_DESC, I40E_RING_BASE_ALIGN) +
			sizeof(struct rx_entry) * I40E_MAX_RING_DESC < PGSIZE_2MB);

	/*
	 * Additionally, for purely software performance optimization reasons,
	 * we require the number of descriptors to be a power of 2.
	 */
	if (nb_desc & (nb_desc - 1))
		return -EINVAL;

	/* NOTE: This is a hack, but it's the only way to support late/lazy
	 * queue setup in DPDK; a feature that IX depends on. */
	rte_eth_devices[dev->port].data->nb_rx_queues = queue_idx + 1;

	ret = rte_eth_rx_queue_setup(dev->port, queue_idx, nb_desc, numa_node, NULL, dpdk_pool);
	if (ret < 0)
		return ret;
	if (numa_node == -1) {
		page = mem_alloc_page_local(PGSIZE_2MB);
		if (page == MAP_FAILED)
			return -ENOMEM;
	} else {
		page = mem_alloc_page(PGSIZE_2MB, numa_node, MPOL_BIND);
		if (page == MAP_FAILED)
			return -ENOMEM;
	}
	memset(page, 0, PGSIZE_2MB);

	/* hijack dpdk usual memory with our own from bigpages:*/
	rxq = (struct rx_queue *) page;

	rxq->ring  = (void *)((uintptr_t) page +
		align_up(sizeof(struct rx_queue), I40E_RING_BASE_ALIGN));
	rxq->ring_entries = (struct rx_entry *)((uintptr_t) rxq->ring +
		align_up(sizeof(union i40e_rx_desc) * nb_desc, I40E_RING_BASE_ALIGN));

	rxq->len = nb_desc;
	rxq->head = 0;
	rxq->tail = rxq->len - 1;

	ret = mem_lookup_page_machine_addr(page, PGSIZE_2MB, &page_phys);
	if (ret)
		goto err;

	rxq->ring_physaddr = page_phys + align_up(sizeof(struct rx_queue), I40E_RING_BASE_ALIGN);
	log_err("queue_setup : phys %p\n", rxq->ring_physaddr);
	/* TODO: this is now done in dev_start (need access to dpdk's queue) */
	//rxq->reg_idx = drxq->reg_idx;
	//rxq->rdt_reg_addr = (uint32_t*) drxq->qrx_tail;

	rxq->erxq.poll = i40e_rx_poll;
	rxq->erxq.ready = i40e_rx_ready;
	dev->data->rx_queues[queue_idx] = &rxq->erxq;
	/* release the dpdk memory location and all its buffers*/
	//i40e_dev_rx_queue_release(drxq);
	return 0;

err:
	mem_free_page(page, PGSIZE_2MB);
	return ret;
}

static void i40_reset_tx_queue(struct tx_queue *txq)
{
	int i;

	for (i = 0; i < txq->len; i++) {
		txq->ring_entries[i].mbuf = NULL;
	}

	txq->head = 0;
	txq->tail = 0;
	txq->tx_next_dd = (uint16_t)(txq->tx_rs_thresh - 1);
	txq->tx_next_rs = (uint16_t)(txq->tx_rs_thresh - 1);

	txq->nb_tx_used = 0;

	txq->last_desc_cleaned = (uint16_t)(txq->nb_tx_desc - 1);
	txq->nb_tx_free = (uint16_t)(txq->nb_tx_desc - 1);
}

/* Construct the tx flags */
static inline uint64_t i40e_build_ctob(uint32_t td_cmd, uint32_t td_offset, unsigned int size, uint32_t td_tag)
{
	return rte_cpu_to_le_64(I40E_TX_DESC_DTYPE_DATA |
			((uint64_t)td_cmd  << I40E_TXD_QW1_CMD_SHIFT) |
			((uint64_t)td_offset << I40E_TXD_QW1_OFFSET_SHIFT) |
			((uint64_t)size  << I40E_TXD_QW1_TX_BUF_SZ_SHIFT) |
			((uint64_t)td_tag  << I40E_TXD_QW1_L2TAG1_SHIFT));
}

static int i40e_tx_reclaim(struct eth_tx_queue *tx)
{
	struct tx_queue *txq = eth_tx_queue_to_drv(tx);
	struct tx_entry *txe;
	volatile struct i40e_tx_desc *txdp;
	int idx = 0, nb_desc = 0;

	while ((uint16_t)(txq->head + idx) != txq->tail) {
		txe = &txq->ring_entries[(txq->head + idx) & (txq->len - 1)];

		if (!txe->mbuf) {
			idx++;
			continue;
		}

		txdp = &((volatile struct i40e_tx_desc *)txq->ring)[(txq->head + idx) & (txq->len - 1)];
		if ((txdp->cmd_type_offset_bsz &
				rte_cpu_to_le_64(I40E_TXD_QW1_DTYPE_MASK)) !=
				rte_cpu_to_le_64(I40E_TX_DESC_DTYPE_DESC_DONE))
			break;

		mbuf_xmit_done(txe->mbuf);
		txe->mbuf = NULL;
		idx++;
		nb_desc = idx;
	}

	txq->head += nb_desc;
	return (uint16_t)(txq->len + txq->head - txq->tail);
}

static inline int __attribute__((always_inline))
i40e_tx_free_bufs(struct tx_queue *txq)
{
	struct tx_entry *txep;
	uint16_t i;
	volatile struct i40e_tx_desc *txdp = ((volatile struct i40e_tx_desc *)txq->ring);

	if ((txdp[txq->tx_next_dd].cmd_type_offset_bsz &
			rte_cpu_to_le_64(I40E_TXD_QW1_DTYPE_MASK)) !=
			rte_cpu_to_le_64(I40E_TX_DESC_DTYPE_DESC_DONE))
		return 0;

	txep = &(txq->ring_entries[txq->tx_next_dd - (txq->tx_rs_thresh - 1)]);

	for (i = 0; i < txq->tx_rs_thresh; i++)
		rte_prefetch0((txep + i)->mbuf);

	for (i = 0; i < txq->tx_rs_thresh; ++i, ++txep) {
		mbuf_free(txep->mbuf);
		txep->mbuf = NULL;
	}

	txq->nb_tx_free = (uint16_t)(txq->nb_tx_free + txq->tx_rs_thresh);
	txq->tx_next_dd = (uint16_t)(txq->tx_next_dd + txq->tx_rs_thresh);
	if (txq->tx_next_dd >= txq->nb_tx_desc)
		txq->tx_next_dd = (uint16_t)(txq->tx_rs_thresh - 1);

	return txq->tx_rs_thresh;
}

static inline void
i40e_txd_enable_checksum(uint64_t ol_flags,
			uint32_t *td_cmd,
			uint32_t *td_offset,
			union i40e_tx_offload tx_offload,
			uint32_t *cd_tunneling)
{
		*td_cmd |= I40E_TX_DESC_CMD_L4T_EOFT_TCP;
		*td_offset |= (sizeof(struct tcp_hdr) >> 2) <<
				I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;

		*td_cmd |= I40E_TX_DESC_CMD_IIPT_IPV4_CSUM;
		*td_offset |= (20 >> 2) << I40E_TX_DESC_LENGTH_IPLEN_SHIFT;
		*td_offset |= (ETH_HDR_LEN  >> 1) << I40E_TX_DESC_LENGTH_MACLEN_SHIFT;
}

/* Populate 4 descriptors with data from 4 mbufs */
static inline void
tx4(volatile struct i40e_tx_desc *txdp, struct mbuf **pkts)
{
	uint64_t dma_addr;
	uint32_t i;

	for (i = 0; i < 4; i++, txdp++, pkts++) {

		/* Always enable CRC offload insertion */
		uint32_t td_cmd = I40E_TD_CMD | I40E_TX_DESC_CMD_ICRC;
		uint32_t td_offset = 0;
		uint64_t ol_flags = (*pkts)->ol_flags;
		union i40e_tx_offload tx_offload;
		memset(&tx_offload, 0, sizeof(tx_offload));

		/* Enable checksum offloading */
		uint32_t cd_tunneling_params = 0;
		if (ol_flags & PKT_TX_TCP_CKSUM) {
			i40e_txd_enable_checksum(ol_flags, &td_cmd, &td_offset, tx_offload, &cd_tunneling_params);
		}


		dma_addr = mbuf_get_data_machaddr(*pkts);
		txdp->buffer_addr = rte_cpu_to_le_64(dma_addr);
		txdp->cmd_type_offset_bsz =
			i40e_build_ctob((uint32_t)td_cmd, td_offset,
					(*pkts)->len, 0);
	}
}

/* Populate 1 descriptor with data from 1 mbuf */
static inline void
tx1(volatile struct i40e_tx_desc *txdp, struct mbuf **pkts)
{
	uint64_t dma_addr;

	/* Always enable CRC offload insertion */
	uint32_t td_cmd = I40E_TD_CMD | I40E_TX_DESC_CMD_ICRC;
	uint32_t td_offset = 0;
	uint64_t ol_flags = (*pkts)->ol_flags;
	union i40e_tx_offload tx_offload;
	memset(&tx_offload, 0, sizeof(tx_offload));

	/* Enable checksum offloading */
	uint32_t cd_tunneling_params = 0;
	if (ol_flags & PKT_TX_TCP_CKSUM) {
		i40e_txd_enable_checksum(ol_flags, &td_cmd, &td_offset, tx_offload, &cd_tunneling_params);
	}

	dma_addr = mbuf_get_data_machaddr(*pkts);
	txdp->buffer_addr = rte_cpu_to_le_64(dma_addr);
	txdp->cmd_type_offset_bsz =
		i40e_build_ctob((uint32_t)td_cmd, td_offset,
				(*pkts)->len, 0);
}

/* Fill hardware descriptor ring with mbuf data */
static inline void
i40e_tx_fill_hw_ring(struct tx_queue *txq,
		     struct mbuf **pkts,
		     uint16_t nb_pkts)
{
	volatile struct i40e_tx_desc *txdp = &(((volatile struct i40e_tx_desc *)txq->ring)[txq->tail]);
	struct tx_entry *txep = &(txq->ring_entries[txq->tail]);
	const int N_PER_LOOP = 4;
	const int N_PER_LOOP_MASK = N_PER_LOOP - 1;
	int mainpart, leftover;
	int i, j;

	mainpart = (nb_pkts & ((uint32_t) ~N_PER_LOOP_MASK));
	leftover = (nb_pkts & ((uint32_t)  N_PER_LOOP_MASK));
	for (i = 0; i < mainpart; i += N_PER_LOOP) {
		for (j = 0; j < N_PER_LOOP; ++j) {
			(txep + i + j)->mbuf = *(pkts + i + j);
		}
		tx4(txdp + i, pkts + i);
	}
	if (unlikely(leftover > 0)) {
		for (i = 0; i < leftover; ++i) {
			(txep + mainpart + i)->mbuf = *(pkts + mainpart + i);
			tx1(txdp + mainpart + i, pkts + mainpart + i);
		}
	}
}

static inline uint16_t
tx_xmit_pkts(struct tx_queue *txq,
	     struct mbuf **tx_pkts,
	     uint16_t nb_pkts)
{
	volatile struct i40e_tx_desc *txr = (volatile struct i40e_tx_desc *)txq->ring;
	uint16_t n = 0;

	/**
	 * Begin scanning the H/W ring for done descriptors when the number
	 * of available descriptors drops below tx_free_thresh. For each done
	 * descriptor, free the associated buffer.
	 */
	if (txq->nb_tx_free < txq->tx_free_thresh)
		i40e_tx_free_bufs(txq);

	/* Use available descriptor only */
	nb_pkts = (uint16_t)RTE_MIN(txq->nb_tx_free, nb_pkts);
	if (unlikely(!nb_pkts))
		return 0;

	txq->nb_tx_free = (uint16_t)(txq->nb_tx_free - nb_pkts);
	if ((txq->tail + nb_pkts) > txq->nb_tx_desc) {
		n = (uint16_t)(txq->nb_tx_desc - txq->tail);
		i40e_tx_fill_hw_ring(txq, tx_pkts, n);
		txr[txq->tx_next_rs].cmd_type_offset_bsz |=
			rte_cpu_to_le_64(((uint64_t)I40E_TX_DESC_CMD_RS) <<
						I40E_TXD_QW1_CMD_SHIFT);
		txq->tx_next_rs = (uint16_t)(txq->tx_rs_thresh - 1);
		txq->tail = 0;
	}

	/* Fill hardware descriptor ring with mbuf data */
	i40e_tx_fill_hw_ring(txq, tx_pkts + n, (uint16_t)(nb_pkts - n));
	txq->tail = (uint16_t)(txq->tail + (nb_pkts - n));

	/* Determin if RS bit needs to be set */
	if (txq->tail > txq->tx_next_rs) {
		txr[txq->tx_next_rs].cmd_type_offset_bsz |=
			rte_cpu_to_le_64(((uint64_t)I40E_TX_DESC_CMD_RS) <<
						I40E_TXD_QW1_CMD_SHIFT);
		txq->tx_next_rs =
			(uint16_t)(txq->tx_next_rs + txq->tx_rs_thresh);
		if (txq->tx_next_rs >= txq->nb_tx_desc)
			txq->tx_next_rs = (uint16_t)(txq->tx_rs_thresh - 1);
	}

	if (txq->tail >= txq->nb_tx_desc)
		txq->tail = 0;

	/* Update the tx tail register */
	rte_wmb();
	I40E_PCI_REG_WRITE(txq->tdt_reg_addr, txq->tail);

	return nb_pkts;
}

static int i40e_tx_xmit(struct eth_tx_queue *tx, int nr, struct mbuf **tx_pkts)
{
	struct tx_queue *tx_queue = eth_tx_queue_to_drv(tx);
	int nb_pkts = nr;
	uint16_t nb_tx = 0;

	if (likely(nb_pkts <= I40E_TX_MAX_BURST))
		return tx_xmit_pkts((struct tx_queue *)tx_queue,
						tx_pkts, nb_pkts);

	while (nb_pkts) {
		uint16_t ret, num = (uint16_t)RTE_MIN(nb_pkts,
						I40E_TX_MAX_BURST);

		ret = tx_xmit_pkts((struct tx_queue *)tx_queue,
						&tx_pkts[nb_tx], num);
		nb_tx = (uint16_t)(nb_tx + ret);
		nb_pkts = (uint16_t)(nb_pkts - ret);
		if (ret < num)
			break;
	}

	return nb_tx;
}


static int tx_queue_setup(struct ix_rte_eth_dev *dev, int queue_idx, int numa_node, uint16_t nb_desc)
{
	void *page;
	machaddr_t page_phys;
	int ret;
	struct tx_queue *txq;

	/*
	 * The number of receive descriptors must not exceed hardware
	 * maximum and must be a multiple of IXGBE_ALIGN.
	 */
	BUILD_ASSERT(align_up(sizeof(struct tx_queue), I40E_RING_BASE_ALIGN) +
		        align_up(sizeof(struct i40e_tx_desc) * nb_desc, I40E_RING_BASE_ALIGN) +
			sizeof(struct tx_entry) * I40E_MAX_RING_DESC < PGSIZE_2MB);

	/*
	 * Additionally, for purely software performance optimization reasons,
	 * we require the number of descriptors to be a power of 2.
	 */
	if (nb_desc & (nb_desc - 1))
		return -EINVAL;

	/* NOTE: This is a hack, but it's the only way to support late/lazy
	 * queue setup in DPDK; a feature that IX depends on. */
	rte_eth_devices[dev->port].data->nb_tx_queues = queue_idx + 1;

	ret = rte_eth_tx_queue_setup(dev->port, queue_idx, nb_desc, numa_node, NULL);
	if (ret < 0)
		return ret;

	if (numa_node == -1) {
		page = mem_alloc_page_local(PGSIZE_2MB);
		if (page == MAP_FAILED)
			return -ENOMEM;
	} else {
		page = mem_alloc_page(PGSIZE_2MB, numa_node, MPOL_BIND);
		if (page == MAP_FAILED)
			return -ENOMEM;
	}
	memset(page, 0, PGSIZE_2MB);

	/* hijack dpdk usual memory with our own from bigpages:*/
	txq = (struct tx_queue *) page;
	txq->ring = (void *)((uintptr_t) page +
			align_up(sizeof(struct tx_queue), I40E_RING_BASE_ALIGN));
	txq->ring_entries = (struct tx_entry *)((uintptr_t) txq->ring +
			align_up(sizeof(struct i40e_tx_desc) * nb_desc, I40E_RING_BASE_ALIGN));
	txq->len = nb_desc;

	ret = mem_lookup_page_machine_addr(page, PGSIZE_2MB, &page_phys);
	if (ret)
		goto err;
	txq->ring_physaddr = page_phys +
			     align_up(sizeof(struct tx_queue), I40E_RING_BASE_ALIGN);

	/* these two fields are initialized in dev_start using DPDK :
	txq->reg_idx = dtxq->reg_idx;
	txq->tdt_reg_addr = (uint32_t *)dtxq->qtx_tail; */

	txq->tx_free_thresh = DEFAULT_TX_FREE_THRESH;
	txq->nb_tx_desc = nb_desc;
	txq->tx_rs_thresh = DEFAULT_TX_RS_THRESH;

	txq->etxq.reclaim = i40e_tx_reclaim;
	txq->etxq.xmit = i40e_tx_xmit;
	i40_reset_tx_queue(txq);
	dev->data->tx_queues[queue_idx] = &txq->etxq;
	/* release the dpdk memory location and all its buffers*/
	//i40e_dev_tx_queue_release(dtxq);
	return 0;

err:
	mem_free_page(page, PGSIZE_2MB);
	return ret;

}

static struct ix_eth_dev_ops eth_dev_ops = {
	.allmulticast_enable = generic_allmulticast_enable,
	.dev_infos_get = generic_dev_infos_get,
	.dev_start = dev_start,
	.link_update = generic_link_update,
	.promiscuous_disable = generic_promiscuous_disable,
	.reta_update = reta_update,
	.rx_queue_setup = rx_queue_setup,
	.tx_queue_setup = tx_queue_setup,
	.fdir_add_perfect_filter = generic_fdir_add_perfect_filter,
	.fdir_remove_perfect_filter = generic_fdir_remove_perfect_filter,
	.rss_hash_conf_get = generic_rss_hash_conf_get,
	.mac_addr_add = generic_mac_addr_add,
};

int i40e_init(struct ix_rte_eth_dev *dev, const char *driver_name)
{
	assert(!strcmp(driver_name, "rte_i40e_pmd"));
	dev->dev_ops = &eth_dev_ops;
	return 0;
}
