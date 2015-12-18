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
 * udp.c - Unreliable Datagram Protocol (UDP) Support
 */

#include <ix/stddef.h>
#include <ix/byteorder.h>
#include <ix/errno.h>
#include <ix/log.h>
#include <ix/syscall.h>
#include <ix/uaccess.h>
#include <ix/vm.h>
#include <ix/kstats.h>
#include <ix/cfg.h>
#include <ix/mempool.h>
#include <asm/chksum.h>

#include <net/ip.h>
#include <net/udp.h>

#include "net.h"

#define UDP_PKT_SIZE		  \
	(sizeof(struct eth_hdr) + \
	 sizeof(struct ip_hdr)  + \
	 sizeof(struct udp_hdr))

#define UDP_MAX_LEN \
	(ETH_MTU - sizeof(struct ip_hdr) - sizeof(struct udp_hdr))

void udp_input(struct mbuf *pkt, struct ip_hdr *iphdr, struct udp_hdr *udphdr)
{
	void *data = mbuf_nextd(udphdr, void *);
	uint16_t len = ntoh16(udphdr->len);
	struct ip_tuple *id;

	if (unlikely(!mbuf_enough_space(pkt, udphdr, len))) {
		mbuf_free(pkt);
		return;
	}

#ifdef DEBUG
	struct ip_addr addr;
	char src[IP_ADDR_STR_LEN];
	char dst[IP_ADDR_STR_LEN];

	addr.addr = ntoh32(iphdr->src_addr.addr);
	ip_addr_to_str(&addr, src);
	addr.addr = ntoh32(iphdr->dst_addr.addr);
	ip_addr_to_str(&addr, dst);

	log_debug("udp: got UDP packet from '%s' to '%s',"
		  "source port %d, dest port %d, len %d\n",
		  src, dst, ntoh16(udphdr->src_port),
		  ntoh16(udphdr->dst_port), ntoh16(udphdr->len));
#endif /* DEBUG */

	/* reuse part of the header memory */
	id = mbuf_mtod(pkt, struct ip_tuple *);
	id->src_ip = ntoh32(iphdr->src_addr.addr);
	id->dst_ip = ntoh32(iphdr->dst_addr.addr);
	id->src_port = ntoh16(udphdr->src_port);
	id->dst_port = ntoh16(udphdr->dst_port);
	pkt->done = (void *) 0xDEADBEEF;

	usys_udp_recv(mbuf_to_iomap(pkt, data), len, mbuf_to_iomap(pkt, id));
}

static void udp_mbuf_done(struct mbuf *pkt)
{
	int i;

	for (i = 0; i < pkt->nr_iov; i++)
		mbuf_iov_free(&pkt->iovs[i]);

	usys_udp_sent(pkt->done_data);
	mbuf_free(pkt);
}

static int udp_output(struct mbuf *__restrict pkt,
		      struct ip_tuple *__restrict id, size_t len)
{
	struct eth_hdr *ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
	struct ip_hdr *iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
	struct udp_hdr *udphdr = mbuf_nextd(iphdr, struct udp_hdr *);
	size_t full_len = len + sizeof(struct udp_hdr);
	struct ip_addr dst_addr;
	int ret;

	dst_addr.addr = id->dst_ip;
	if (arp_lookup_mac(&dst_addr, &ethhdr->dhost))
		return -RET_AGAIN;

	ethhdr->shost = CFG.mac;
	ethhdr->type = hton16(ETHTYPE_IP);

	ip_setup_header(iphdr, IPPROTO_UDP,
			CFG.host_addr.addr, id->dst_ip, full_len);
	iphdr->chksum = chksum_internet((void *) iphdr, sizeof(struct ip_hdr));

	udphdr->src_port = hton16(id->src_port);
	udphdr->dst_port = hton16(id->dst_port);
	udphdr->len = hton16(full_len);
	udphdr->chksum = 0;

	/* No TX checksum offload for UDP since only using context 0 for IP + TCP chekcusm */
	pkt->ol_flags = 0;

	pkt->len = UDP_PKT_SIZE;

	/* FIXME: cur_fg makes no sense in the context of a UDP datagram send.
	 *        For single-device interfaces, this is trivial (there's only one dev_ix)
	 *        For multi-device bonds, the interface needs to be provided implicitly or explicitly
	 */

	ret = 0;
	if (eth_dev_count > 1)
		panic("udp_send not implemented for bonded interfaces\n");
	else
		ret = eth_send(percpu_get(eth_txqs)[0], pkt);

	if (ret)
		return ret;

	return 0;
}

/**
 * bsys_udp_send - send a UDP packet
 * @addr: the user-level payload address in memory
 * @len: the length of the payload
 * @id: the IP destination
 * @cookie: a user-level tag for the request
 *
 * Returns the number of bytes sent, or < 0 if fail.
 */
long bsys_udp_send(void __user *__restrict vaddr, size_t len,
		   struct ip_tuple __user *__restrict id,
		   unsigned long cookie)
{
	struct ip_tuple tmp;
	struct mbuf *pkt;
	struct mbuf_iov *iovs;
	struct sg_entry ent;
	void *addr;
	int ret;
	int i;

	KSTATS_VECTOR(bsys_udp_send);

	/* validate user input */
	if (unlikely(len > UDP_MAX_LEN))
		return -RET_INVAL;

	if (unlikely(copy_from_user(id, &tmp, sizeof(struct ip_tuple))))
		return -RET_FAULT;

	if (unlikely(!uaccess_zc_okay(vaddr, len)))
		return -RET_FAULT;

	addr = (void *) vm_lookup_phys(vaddr, PGSIZE_2MB);
	if (unlikely(!addr))
		return -RET_FAULT;

	addr = (void *)((uintptr_t) addr + PGOFF_2MB(vaddr));

	pkt = mbuf_alloc_local();
	if (unlikely(!pkt))
		return -RET_NOBUFS;

	iovs = mbuf_mtod_off(pkt, struct mbuf_iov *,
			     align_up(UDP_PKT_SIZE, sizeof(uint64_t)));
	pkt->iovs = iovs;
	ent.base = addr;
	ent.len = len;
	len = mbuf_iov_create(&iovs[0], &ent);
	pkt->nr_iov = 1;

	/*
	 * Handle the case of a crossed page boundary. There
	 * can only be one because of the MTU size.
	 */
	BUILD_ASSERT(UDP_MAX_LEN < PGSIZE_2MB);
	if (ent.len != len) {
		ent.base = (void *)((uintptr_t) ent.base + len);
		ent.len -= len;
		iovs[1].base = ent.base;
		iovs[1].maddr = page_get(ent.base);
		iovs[1].len = ent.len;
		pkt->nr_iov = 2;
	}

	pkt->done = &udp_mbuf_done;
	pkt->done_data = cookie;

	ret = udp_output(pkt, &tmp, len);
	if (unlikely(ret)) {
		for (i = 0; i < pkt->nr_iov; i++)
			mbuf_iov_free(&pkt->iovs[i]);
		mbuf_free(pkt);
		return ret;
	}

	return 0;
}

long bsys_udp_sendv(struct sg_entry __user *ents, unsigned int nrents,
		    struct ip_tuple __user *id, unsigned long cookie)
{
	KSTATS_VECTOR(bsys_udp_sendv);

	return -RET_NOSYS;
}

#define MAX_MBUF_PAGE_OFF	(PGSIZE_2MB - (PGSIZE_2MB % MBUF_LEN))

/**
 * bsys_udp_recv_done - inform the kernel done using a UDP packet buffer
 * @iomap: a pointer anywhere inside the mbuf
 */
long bsys_udp_recv_done(void *iomap)
{
	struct mempool *pool = &percpu_get(mbuf_mempool);
	struct mbuf *m;
	void *addr = iomap_to_mbuf(pool, iomap);
	size_t off = PGOFF_2MB(addr);

	KSTATS_VECTOR(bsys_udp_recv_done);


#if 0
	// EdB: deprecated; rely on mempool sanity

	/* validate the address */
	if (unlikely((uintptr_t) addr < (uintptr_t) pool->buf ||
		     (uintptr_t) addr >=
		     (uintptr_t) pool->buf + pool->nr_pages * PGSIZE_2MB ||
		     off >= MAX_MBUF_PAGE_OFF)) {
		log_err("udp: user tried to free an invalid mbuf"
			"at address %p\n", addr);
		return -RET_FAULT;
	}
#endif
	//FIXME: should support the conversion in mempool.h for nostraddle==1 pools

	m = (struct mbuf *)(PGADDR_2MB(addr) + (off / MBUF_LEN) * MBUF_LEN);

	MEMPOOL_SANITY_ACCESS(addr);


	if (unlikely(m->done != (void *) 0xDEADBEEF)) {
		log_err("udp: user tried to free an already free mbuf\n");
		return -RET_INVAL;
	}

	m->done = NULL;
	mbuf_free(m);
	return 0;
}

