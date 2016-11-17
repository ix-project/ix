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

#pragma once

#include <ix/ethdev.h>

/* driver-independent initialization function */
int driver_init(struct pci_dev *dev, struct ix_rte_eth_dev **eth);

/* driver-independent configuration */
extern struct rte_eth_rxconf rx_conf;
extern struct rte_eth_txconf tx_conf;

/* driver specific initialization functions */
int ixgbe_init(struct ix_rte_eth_dev *dev, const char *driver_name);
int i40e_init(struct ix_rte_eth_dev *dev, const char *driver_name);

/* driver-independent eth_dev_ops */
void generic_allmulticast_enable(struct ix_rte_eth_dev *dev);
void generic_dev_infos_get(struct ix_rte_eth_dev *dev, struct ix_rte_eth_dev_info *dev_info);
int generic_link_update(struct ix_rte_eth_dev *dev, int wait_to_complete);
void generic_promiscuous_disable(struct ix_rte_eth_dev *dev);
int generic_fdir_add_perfect_filter(struct ix_rte_eth_dev *dev, struct rte_fdir_filter *fdir_ftr, uint16_t soft_id, uint8_t rx_queue, uint8_t drop);
int generic_fdir_remove_perfect_filter(struct ix_rte_eth_dev *dev, struct rte_fdir_filter *fdir_ftr, uint16_t soft_id);
int generic_rss_hash_conf_get(struct ix_rte_eth_dev *dev, struct ix_rte_eth_rss_conf *ix_reta_conf);
void generic_mac_addr_add(struct ix_rte_eth_dev *dev, struct eth_addr *mac_addr, uint32_t index, uint32_t vmdq);
