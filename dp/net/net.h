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
 * net.h - the network stack local header
 */

#pragma once

#include <ix/types.h>
#include <ix/mbuf.h>
#include <ix/ethdev.h>

#include <net/arp.h>
#include <net/icmp.h>
#include <net/udp.h>

/* Address Resolution Protocol (ARP) definitions */
extern int arp_lookup_mac(struct ip_addr *addr, struct eth_addr *mac);
extern int arp_insert(struct ip_addr *addr, struct eth_addr *mac);
extern void arp_input(struct mbuf *pkt, struct arp_hdr *hdr);
extern int arp_init(void);

/* Internet Control Message Protocol (ICMP) definitions */
extern void icmp_input(struct eth_fg *, struct mbuf *pkt, struct icmp_hdr *hdr, int len);

/* Unreliable Datagram Protocol (UDP) definitions */
extern void udp_input(struct mbuf *pkt, struct ip_hdr *iphdr,
		      struct udp_hdr *udphdr);

/* Transmission Control Protocol (TCP) definitions */
/* FIXME: change when we integrate better with LWIP */
extern void tcp_input_tmp(struct eth_fg *, struct mbuf *pkt, struct ip_hdr *iphdr, void *tcphdr);
extern int tcp_api_init(void);
extern int tcp_api_init_fg(void);

/**
 * ip_setup_header - outputs a typical IP header
 * @iphdr: a pointer to the header
 * @proto: the protocol
 * @saddr: the source address
 * @daddr: the destination address
 * @l4len: the length of the L4 (e.g. UDP or TCP) header and data.
 */
static inline void ip_setup_header(struct ip_hdr *iphdr, uint8_t proto,
				   uint32_t saddr, uint32_t daddr,
				   uint16_t l4len)
{
	iphdr->header_len = sizeof(struct ip_hdr) / 4;
	iphdr->version = 4;
	iphdr->tos = 0;
	iphdr->len = hton16(sizeof(struct ip_hdr) + l4len);
	iphdr->id = 0;
	iphdr->off = 0;
	iphdr->ttl = 64;
	iphdr->proto = proto;
	iphdr->chksum = 0;
	iphdr->src_addr.addr = hton32(saddr);
	iphdr->dst_addr.addr = hton32(daddr);
}

int ip_send_one(struct eth_fg *cur_fg, struct ip_addr *dst_addr, struct mbuf *pkt, size_t len);
int arp_add_pending_pkt(struct ip_addr *dst_addr, struct eth_fg *fg, struct mbuf *mbuf, size_t len);
