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



#include <ix/mbuf.h>
#include <ix/timer.h>
#include <ix/ethfg.h>

#include <net/ip.h>

#include <lwip/memp.h>
#include <lwip/pbuf.h>
#include <ix/ethfg.h>

static struct netif {
	char unused[32];
} netif;

struct ip_globals
{
	char unused[20];
	struct ip_addr current_iphdr_src;
	struct ip_addr current_iphdr_dest;
};

void tcp_input(struct eth_fg *cur_fg,struct pbuf *p, struct ip_addr *src, struct ip_addr *dest);

//DEFINE_PERCPU(struct ip_globals, ip_data);


struct netif *ip_route(struct ip_addr *dest)
{
	return &netif;
}

void tcp_input_tmp(struct eth_fg *cur_fg, struct mbuf *pkt, struct ip_hdr *iphdr, void *tcphdr)
{
	struct pbuf *pbuf;

	pbuf = pbuf_alloc(PBUF_RAW, ntoh16(iphdr->len) - iphdr->header_len * 4, PBUF_ROM);
	pbuf->payload = tcphdr;
	pbuf->mbuf = pkt;
//	percpu_get(ip_data).current_iphdr_dest.addr = iphdr->dst_addr.addr;
//	percpu_get(ip_data).current_iphdr_src.addr = iphdr->src_addr.addr;
	tcp_input(cur_fg,pbuf, &iphdr->src_addr,&iphdr->dst_addr);
}


static struct mempool_datastore  pbuf_ds;
static struct mempool_datastore  pbuf_with_payload_ds;
static struct mempool_datastore  tcp_pcb_ds;
static struct mempool_datastore  tcp_seg_ds;

DEFINE_PERCPU(struct mempool, pbuf_mempool __attribute__ ((aligned (64))));
DEFINE_PERCPU(struct mempool, pbuf_with_payload_mempool __attribute__ ((aligned (64))));
DEFINE_PERCPU(struct mempool, tcp_pcb_mempool __attribute__ ((aligned (64))));
DEFINE_PERCPU(struct mempool, tcp_pcb_listen_mempool __attribute__ ((aligned (64))));
DEFINE_PERCPU(struct mempool, tcp_seg_mempool __attribute__ ((aligned (64))));

#define MEMP_SIZE (256*1024)
#define PBUF_CAPACITY (768*1024)

#define PBUF_WITH_PAYLOAD_SIZE 4096

static int init_mempool(struct mempool_datastore *m, int nr_elems, size_t elem_len, const char *prettyname)
{
	return mempool_create_datastore(m, nr_elems, elem_len, 0, MEMPOOL_DEFAULT_CHUNKSIZE,prettyname);
}

int memp_init(void)
{
	if (init_mempool(&pbuf_ds, PBUF_CAPACITY, memp_sizes[MEMP_PBUF],"pbuf"))
		return 1;

	if (init_mempool(&pbuf_with_payload_ds, MEMP_SIZE, PBUF_WITH_PAYLOAD_SIZE,"pbuf_payload"))
		return 1;

	if (init_mempool(&tcp_pcb_ds, MEMP_SIZE, memp_sizes[MEMP_TCP_PCB],"tcp_pcb"))
		return 1;

	if (init_mempool(&tcp_seg_ds, MEMP_SIZE, memp_sizes[MEMP_TCP_SEG],"tcp_seg"))
		return 1;
	return 0;
}

int memp_init_cpu(void)
{
	int cpu = percpu_get(cpu_id);

	if (mempool_create(&percpu_get(pbuf_mempool),&pbuf_ds,MEMPOOL_SANITY_PERCPU, cpu))
		return 1;

	if (mempool_create(&percpu_get(pbuf_with_payload_mempool), &pbuf_with_payload_ds, MEMPOOL_SANITY_PERCPU, cpu))
		return 1;

	if (mempool_create(&percpu_get(tcp_pcb_mempool), &tcp_pcb_ds, MEMPOOL_SANITY_PERCPU, cpu))
		return 1;

	if (mempool_create(&percpu_get(tcp_seg_mempool), &tcp_seg_ds, MEMPOOL_SANITY_PERCPU, cpu))
		return 1;

	return 0;
}

void *mem_malloc(size_t size)
{
	LWIP_ASSERT("mem_malloc", size <= PBUF_WITH_PAYLOAD_SIZE);
	return mempool_alloc(&percpu_get(pbuf_with_payload_mempool));
}

void mem_free(void *ptr)
{
	mempool_free(&percpu_get(pbuf_with_payload_mempool), ptr);
}
