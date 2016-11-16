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
 * tcp_api.c - plumbing between the TCP and userspace
 */

#include <assert.h>
#include <ix/stddef.h>
#include <ix/errno.h>
#include <ix/syscall.h>
#include <ix/log.h>
#include <ix/uaccess.h>
#include <ix/ethdev.h>
#include <ix/kstats.h>
#include <ix/cfg.h>

#include <lwip/tcp.h>

int ip_send_one(struct eth_fg *cur_fg, struct ip_addr *dst_addr, struct mbuf *pkt, size_t len);

#define MAX_PCBS	(512*1024)
#define DEFAULT_PORT 8000

/* FIXME: this should be probably per queue */
static DEFINE_PERCPU(struct tcp_pcb_listen[CFG_MAX_PORTS], listen_ports);

static DEFINE_PERCPU(uint16_t, local_port);
/* FIXME: this should be more adaptive to various configurations */
#define PORTS_PER_CPU (65536 / 32)

/*
 * FIXME: LWIP and IX have different lifetime rules so we have to maintain
 * a seperate pcb. Otherwise, we'd be plagued by use-after-free problems.
 */
struct tcpapi_pcb {
	unsigned long alive; /* FIXME: this overlaps with mempool_hdr so
			      * we can tell if this pcb is allocated or not. */
	struct tcp_pcb *pcb;
	unsigned long cookie;
	struct ip_tuple *id;
	hid_t handle;
	struct pbuf *recvd;
	struct pbuf *recvd_tail;
	int queue;
	bool accepted;
};

#define TCPAPI_PCB_SIZE 64

static struct mempool_datastore pcb_datastore;
static struct mempool_datastore id_datastore;

static DEFINE_PERCPU(struct mempool, pcb_mempool __attribute__((aligned(64))));
static DEFINE_PERCPU(struct mempool, id_mempool __attribute__((aligned(64))));

static void remove_fdir_filter(struct ip_tuple *id);

/**
 * handle_to_tcpapi - converts a handle to a PCB
 * @handle: the input handle
 *
 * Return a PCB, or NULL if the handle is invalid.
 */
static inline struct tcpapi_pcb *handle_to_tcpapi(hid_t handle, struct eth_fg **new_cur_fg)
{
	struct mempool *p;
	struct tcpapi_pcb *api;
	int fg = ((handle >> 48) & 0xffff);
	unsigned long idx = (handle & 0xffffffffffff);

	if (unlikely(fg >= ETH_MAX_TOTAL_FG + NCPU))
		return NULL;
	if (unlikely(idx >= MAX_PCBS))
		return NULL;

	*new_cur_fg = fgs[fg];
	eth_fg_set_current(fgs[fg]);
	p = &percpu_get(pcb_mempool);

	api = (struct tcpapi_pcb *) mempool_idx_to_ptr(p, idx, TCPAPI_PCB_SIZE);
	MEMPOOL_SANITY_ACCESS(api);

	/* check if the handle is actually allocated */
	if (unlikely(api->alive > 1))
		return NULL;

	percpu_get(syscall_cookie) = api->cookie;

	return api;
}

/**
 * tcpapi_to_handle - converts a PCB to a handle
 * @pcb: the PCB.
 *
 * Returns a handle.
 */
static inline hid_t tcpapi_to_handle(struct eth_fg *cur_fg, struct tcpapi_pcb *pcb)
{
	struct mempool *p = &percpu_get(pcb_mempool);
	MEMPOOL_SANITY_ACCESS(pcb);
	hid_t hid = mempool_ptr_to_idx(p, pcb, TCPAPI_PCB_SIZE) | ((uintptr_t)(cur_fg->fg_id) << 48);
	return hid;
}

static void recv_a_pbuf(struct tcpapi_pcb *api, struct pbuf *p)
{
	struct mbuf *pkt;
	MEMPOOL_SANITY_LINK(api, p);

	/* Walk through the full receive chain */
	do {
		pkt = p->mbuf;
		pkt->len = p->len; /* repurpose len for recv_done */
		usys_tcp_recv(api->handle, api->cookie,
			      mbuf_to_iomap(pkt, p->payload), p->len);

		p = p->next;
	} while (p);
}

long bsys_tcp_accept(hid_t handle, unsigned long cookie)
{
	/*
	 * FIXME: this function is sort of a placeholder since we have no
	 * choice but to have already accepted the connection under LWIP's
	 * synchronous API.
	 */

	struct eth_fg *cur_fg;
	struct tcpapi_pcb *api = handle_to_tcpapi(handle, &cur_fg);
	struct pbuf *tmp;

	KSTATS_VECTOR(bsys_tcp_accept);

	log_debug("tcpapi: bsys_tcp_accept() - handle %lx, cookie %lx\n",
		  handle, cookie);

	if (unlikely(!api)) {
		log_debug("tcpapi: invalid handle\n");
		return -RET_BADH;
	}

	if (api->id) {
		mempool_free(&percpu_get(id_mempool), api->id);
		api->id = NULL;
	}

	api->cookie = cookie;
	api->accepted = true;

	tmp = api->recvd;
	while (tmp) {
		recv_a_pbuf(api, tmp);
		tmp = tmp->tcp_api_next;
	}

	return RET_OK;
}

long bsys_tcp_reject(hid_t handle)
{
	/*
	 * FIXME: LWIP's synchronous handling of accepts
	 * makes supporting this call impossible.
	 */

	KSTATS_VECTOR(bsys_tcp_reject);

	log_err("tcpapi: bsys_tcp_reject() is not implemented\n");

	return -RET_NOTSUP;
}

ssize_t bsys_tcp_send(hid_t handle, void *addr, size_t len)
{
	KSTATS_VECTOR(bsys_tcp_send);

	log_debug("tcpapi: bsys_tcp_send() - addr %p, len %lx\n",
		  addr, len);

	return -RET_NOTSUP;
}

ssize_t bsys_tcp_sendv(hid_t handle, struct sg_entry __user *ents,
		       unsigned int nrents)
{
	struct eth_fg *cur_fg;
	struct tcpapi_pcb *api = handle_to_tcpapi(handle, &cur_fg);
	int i;
	size_t len_xmited = 0;

	KSTATS_VECTOR(bsys_tcp_sendv);

	log_debug("tcpapi: bsys_tcp_sendv() - handle %lx, ents %p, nrents %ld\n",
		  handle, ents, nrents);

	if (unlikely(!api)) {
		log_debug("tcpapi: invalid handle\n");
		return -RET_BADH;
	}

	if (unlikely(!api->alive))
		return -RET_CLOSED;

	if (unlikely(!uaccess_okay(ents, nrents * sizeof(struct sg_entry))))
		return -RET_FAULT;

	nrents = min(nrents, MAX_SG_ENTRIES);
	for (i = 0; i < nrents; i++) {
		err_t err;
		void *base = (void *) uaccess_peekq((uint64_t *) &ents[i].base);
		size_t len = uaccess_peekq(&ents[i].len);
		bool buf_full = len > min(api->pcb->snd_buf, 0xFFFF);

		if (unlikely(!uaccess_okay(base, len)))
			break;

		/*
		 * FIXME: hacks to deal with LWIP's send buffering
		 * design when handling large send requests. LWIP
		 * buffers send data but in IX we don't want any
		 * buffering in the kernel at all. Thus, the real
		 * limit here should be the TCP cwd. Unfortunately
		 * tcp_out.c needs to be completely rewritten to
		 * support this.
		 */
		if (buf_full)
			len = min(api->pcb->snd_buf, 0xFFFF);
		if (!len)
			break;

		/*
		 * FIXME: Unfortunately LWIP's TX path is compeletely
		 * broken in terms of zero-copy. It's also somewhat
		 * broken in terms of large write requests. Here's a
		 * hacky placeholder until we can rewrite this path.
		 */
		err = tcp_write(api->pcb, base, len, 0);
		if (err != ERR_OK)
			break;

		len_xmited += len;
		if (buf_full)
			break;
	}

	if (len_xmited)
		tcp_output(cur_fg, api->pcb);

	return len_xmited;
}

long bsys_tcp_recv_done(hid_t handle, size_t len)
{
	struct eth_fg *cur_fg;
	struct tcpapi_pcb *api = handle_to_tcpapi(handle, &cur_fg);
	struct pbuf *recvd, *next;

	KSTATS_VECTOR(bsys_tcp_recv_done);

	log_debug("tcpapi: bsys_tcp_recv_done - handle %lx, len %ld\n",
		  handle, len);

	if (unlikely(!api)) {
		log_debug("tcpapi: invalid handle\n");
		return -RET_BADH;
	}

	recvd = api->recvd;

	if (api->pcb)
		tcp_recved(cur_fg, api->pcb, len);
	while (recvd) {
		if (len < recvd->len)
			break;

		len -= recvd->len;
		next = recvd->tcp_api_next;
		pbuf_free(recvd);
		recvd = next;
	}

	api->recvd = recvd;
	return RET_OK;
}

long bsys_tcp_close(hid_t handle)
{
	struct eth_fg *cur_fg;
	struct tcpapi_pcb *api = handle_to_tcpapi(handle, &cur_fg);
	struct pbuf *recvd, *next;

	KSTATS_VECTOR(bsys_tcp_close);

	log_debug("tcpapi: bsys_tcp_close - handle %lx\n", handle);

	if (unlikely(!api)) {
		log_debug("tcpapi: invalid handle\n");
		return -RET_BADH;
	}

	if (api->pcb) {
		tcp_close_with_reset(cur_fg, api->pcb);
	}

	recvd = api->recvd;
	while (recvd) {
		next = recvd->tcp_api_next;
		pbuf_free(recvd);
		recvd = next;
	}

	if (api->id) {
		remove_fdir_filter(api->id);
		mempool_free(&percpu_get(id_mempool), api->id);
	}

	mempool_free(&percpu_get(pcb_mempool), api);
	return RET_OK;
}

static void mark_dead(struct tcpapi_pcb *api, unsigned long cookie)
{
	if (!api) {
		usys_tcp_dead(0, cookie);
		return;
	}

	if (api->id)
		remove_fdir_filter(api->id);

	api->alive = false;
	usys_tcp_dead(api->handle, api->cookie);
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct tcpapi_pcb *api;

	log_debug("tcpapi: on_recv - arg %p, pcb %p, pbuf %p, err %d\n",
		  arg, pcb, p, err);

	api = (struct tcpapi_pcb *) arg;

	/* FIXME: It's not really clear what to do with "err" */

	/* Was the connection closed? */
	if (!p) {
		mark_dead(api, api->cookie);
		return ERR_OK;
	}

	if (!api->recvd) {
		api->recvd = p;
		api->recvd_tail = p;
	} else {
		api->recvd_tail->tcp_api_next = p;
		api->recvd_tail = p;
	}
	p->tcp_api_next = NULL;

	/*
	 * FIXME: This is a pretty annoying hack. LWIP accepts connections
	 * synchronously while we have to wait for the app to accept the
	 * connection. As a result, we have no choice but to assume the
	 * connection will be accepted. Thus, we may start receiving data
	 * packets before the app has allocated a recieve context and set
	 * the appropriate cookie value. For now we wait for the app to
	 * accept the connection before we allow receive events to be
	 * sent. Clearly, the receive path needs to be rewritten.
	 */
	if (!api->accepted)
		goto done;

	recv_a_pbuf(api, p);

done:
	return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
	struct tcpapi_pcb *api;
	unsigned long cookie;

	log_debug("tcpapi: on_err - arg %p err %d\n", arg, err);

	/* Because we use the LWIP_EVENT_API, LWIP can invoke on_err before we
	 * invoke tcp_arg, thus arg will be NULL. This happens, e.g., if we
	 * receive a RST after sending a SYN+ACK. */
	if (!arg)
		return;

	api = (struct tcpapi_pcb *) arg;
	cookie = api->cookie;

	if (err == ERR_ABRT || err == ERR_RST || err == ERR_CLSD) {
		mark_dead(api, cookie);
		api->pcb = NULL;
	}
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
	struct tcpapi_pcb *api;

	log_debug("tcpapi: on_sent - arg %p, pcb %p, len %hd\n",
		  arg, pcb, len);

	api = (struct tcpapi_pcb *) arg;
	usys_tcp_sent(api->handle, api->cookie, len);

	return ERR_OK;
}

static err_t on_accept(struct eth_fg *cur_fg, void *arg, struct tcp_pcb *pcb, err_t err)
{
	struct tcpapi_pcb *api;
	struct ip_tuple *id;
	hid_t handle;


	log_debug("tcpapi: on_accept - arg %p, pcb %p, err %d\n",
		  arg, pcb, err);

	api = mempool_alloc(&percpu_get(pcb_mempool));
	if (unlikely(!api))
		return ERR_MEM;
	id = mempool_alloc(&percpu_get(id_mempool));
	if (unlikely(!id)) {
		mempool_free(&percpu_get(pcb_mempool), api);
		return ERR_MEM;
	}

	api->pcb = pcb;
	api->alive = true;
	api->cookie = 0;
	api->recvd = NULL;
	api->recvd_tail = NULL;
	api->accepted = false;

	tcp_nagle_disable(pcb);
	tcp_arg(pcb, api);

#if  LWIP_CALLBACK_API
	tcp_recv(pcb, on_recv);
	tcp_err(pcb, on_err);
	tcp_sent(pcb, on_sent);
#endif

	id->src_ip = 0; /* FIXME: LWIP doesn't provide this information :( */
	id->dst_ip = CFG.host_addr.addr;
	id->src_port = pcb->local_port;
	id->dst_port = pcb->remote_port;
	api->id = id;
	handle = tcpapi_to_handle(cur_fg, api);
	api->handle = handle;
	id = (struct ip_tuple *)
	     mempool_pagemem_to_iomap(&percpu_get(id_mempool), id);

	usys_tcp_knock(handle, id);
	return ERR_OK;
}

static err_t on_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
	struct tcpapi_pcb *api = (struct tcpapi_pcb *) arg;

	if (err != ERR_OK) {
		log_err("tcpapi: connection failed, ret %d\n", err);
		/* FIXME: free memory and mark handle dead */
		usys_tcp_connected(api->handle, api->cookie, RET_CONNREFUSED);
		return err;
	}

	usys_tcp_connected(api->handle, api->cookie, RET_OK);
	return ERR_OK;
}



/**
 * lwip_tcp_event -- "callback from the LWIP library
 */

err_t
lwip_tcp_event(struct eth_fg *cur_fg, void *arg, struct tcp_pcb *pcb,
	       enum lwip_event event,
	       struct pbuf *p,
	       u16_t size,
	       err_t err)
{
	switch (event) {
	case LWIP_EVENT_ACCEPT:
		return on_accept(cur_fg, arg, pcb, err);
		break;
	case LWIP_EVENT_SENT:
		return on_sent(arg, pcb, size);
		break;
	case LWIP_EVENT_RECV:
		return on_recv(arg, pcb, p, err);
		break;
	case LWIP_EVENT_CONNECTED:
		return on_connected(arg, pcb, err);
		break;
	case LWIP_EVENT_ERR:
		on_err(arg, err);
		return 0;
		break;

	case LWIP_EVENT_POLL:
		return ERR_OK;
	default:
		assert(0);
	}
	return ERR_OK;

}

/* FIXME: we should maintain a bitmap to hold the available TCP ports */

/* FIXME:
   -- this is totally broken with flow-group migration.  The match should be based on a matching fgid for that device
   -- for multi-device bonds, need to also figure out (and reverse) the L3+L4 bond that is in place.
   -- performance will be an issue as well with 1/128 probability of success (from 1/16).

   -- short version: need to fix this by using flow director for all outbound connections.  (EdB 2014-11-17)
*/

static uint32_t compute_toeplitz_hash(const uint8_t *key, uint32_t src_addr, uint32_t dst_addr, uint16_t src_port, uint16_t dst_port)
{
	int i, j;
	uint8_t input[12];
	uint32_t result = 0;
	uint32_t key_part = htonl(((uint32_t *)key)[0]);

	memcpy(&input[0], &src_addr, 4);
	memcpy(&input[4], &dst_addr, 4);
	memcpy(&input[8], &src_port, 2);
	memcpy(&input[10], &dst_port, 2);

	for (i = 0; i < 12; i++) {
		for (j = 128; j; j >>= 1) {
			if (input[i] & j)
				result ^= key_part;
			key_part <<= 1;
			if (key[i + 4] & j)
				key_part |= 1;
		}
	}

	return result;
}

static void remove_fdir_filter(struct ip_tuple *id)
{
	struct rte_fdir_filter fdir_ftr;
	struct ix_rte_eth_dev *dev;

	fdir_ftr.iptype = RTE_FDIR_IPTYPE_IPV4;
	fdir_ftr.l4type = RTE_FDIR_L4TYPE_TCP;
	fdir_ftr.ip_src.ipv4_addr = id->dst_ip;
	fdir_ftr.ip_dst.ipv4_addr = id->src_ip;
	fdir_ftr.port_src = id->dst_port;
	fdir_ftr.port_dst = id->src_port;
	dev = percpu_get(eth_rxqs[0])->dev;
	dev->dev_ops->fdir_remove_perfect_filter(dev, &fdir_ftr, 0);
}

static struct eth_fg *get_port_with_fdir(struct ip_tuple *id)
{
	int ret;
	struct rte_fdir_filter fdir_ftr;
	struct ix_rte_eth_dev *dev;
	struct eth_rx_queue *queue;

	fdir_ftr.iptype = RTE_FDIR_IPTYPE_IPV4;
	fdir_ftr.l4type = RTE_FDIR_L4TYPE_TCP;
	fdir_ftr.ip_src.ipv4_addr = id->dst_ip;
	fdir_ftr.ip_dst.ipv4_addr = id->src_ip;
	fdir_ftr.port_src = id->dst_port;
	fdir_ftr.port_dst = id->src_port;

	queue = percpu_get(eth_rxqs[0]);
	dev = queue->dev;

	ret = dev->dev_ops->fdir_add_perfect_filter(dev, &fdir_ftr, 0, queue->queue_idx, 0);
	if (ret < 0)
		return NULL;

	eth_fg_set_current(outbound_fg());
	return outbound_fg();
}

struct eth_fg *get_local_port_and_set_queue(struct ip_tuple *id)
{
	int ret;
	uint32_t hash;
	uint32_t fg_idx;
	struct eth_fg *fg;
	struct ix_rte_eth_dev *dev;
	struct ix_rte_eth_rss_conf rss_conf;

	if (eth_dev_count > 1)
		panic("tcp_connect not implemented for bonded interfaces\n");

	if (!percpu_get(local_port))
		percpu_get(local_port) = percpu_get(cpu_id) * PORTS_PER_CPU;

	percpu_get(local_port)++;
	id->src_port = percpu_get(local_port);

	fg = get_port_with_fdir(id);
	if (fg)
		return fg;

	dev = percpu_get(eth_rxqs[0])->dev;
	ret = dev->dev_ops->rss_hash_conf_get(dev, &rss_conf);
	if (ret < 0)
		return NULL;

	while (1) {
		if (percpu_get(local_port) >= (percpu_get(cpu_id) + 1) * PORTS_PER_CPU)
			percpu_get(local_port) = percpu_get(cpu_id) * PORTS_PER_CPU + 1;
		hash = compute_toeplitz_hash(rss_conf.rss_key, htonl(id->dst_ip), htonl(id->src_ip), htons(id->dst_port), htons(id->src_port));
		fg_idx = hash & (dev->data->nb_rx_fgs - 1);
		if (percpu_get(eth_rxqs[0])->dev->data->rx_fgs[fg_idx].cur_cpu == percpu_get(cpu_id)) {
			//set_current_queue(percpu_get(eth_rxqs)[0]);

			// this will fail with eth_dev_count >1
			assert(&percpu_get(eth_rxqs[0])->dev->data->rx_fgs[fg_idx] == fgs[fg_idx]);
			eth_fg_set_current(&percpu_get(eth_rxqs[0])->dev->data->rx_fgs[fg_idx]);

			return fgs[fg_idx];
		}
		percpu_get(local_port)++;
		id->src_port = percpu_get(local_port);
	}

	return 0;
}

long bsys_tcp_connect(struct ip_tuple __user *id, unsigned long cookie)
{
	err_t err;
	struct ip_tuple tmp;
	struct ip_addr addr;
	struct tcp_pcb *pcb;
	struct tcpapi_pcb *api;

	KSTATS_VECTOR(bsys_tcp_connect);

	log_debug("tcpapi: bsys_tcp_connect() - id %p, cookie %lx\n",
		  id, cookie);

	percpu_get(syscall_cookie) = cookie;

	if (unlikely(copy_from_user(id, &tmp, sizeof(struct ip_tuple)))) {
		return -RET_FAULT;
	}

	tmp.src_ip = CFG.host_addr.addr;

	struct eth_fg *cur_fg = get_local_port_and_set_queue(&tmp);
	if (unlikely(!cur_fg))
		return -RET_FAULT;


	pcb = tcp_new(cur_fg);
	if (unlikely(!pcb))
		goto pcb_fail;
	tcp_nagle_disable(pcb);

	api = mempool_alloc(&percpu_get(pcb_mempool));
	if (unlikely(!api)) {
		goto connect_fail;
	}

	api->pcb = pcb;
	api->alive = true;
	api->cookie = cookie;
	api->recvd = NULL;
	api->recvd_tail = NULL;
	api->accepted = true;

	tcp_arg(pcb, api);

	api->handle = tcpapi_to_handle(cur_fg, api);

#if  LWIP_CALLBACK_API
	tcp_recv(pcb, on_recv);
	tcp_err(pcb, on_err);
	tcp_sent(pcb, on_sent);
#endif

	addr.addr = hton32(tmp.src_ip);

	err = tcp_bind(cur_fg, pcb, &addr, tmp.src_port);
	if (unlikely(err != ERR_OK))
		goto connect_fail;

	addr.addr = hton32(tmp.dst_ip);

	err = tcp_connect(cur_fg, pcb, &addr, tmp.dst_port, on_connected);
	if (unlikely(err != ERR_OK))
		goto connect_fail;

	return api->handle;

connect_fail:
	tcp_abort(cur_fg, pcb);
pcb_fail:

	return -RET_NOMEM;
}



/* derived from ip_output_hinted; a mess because of conflicts between LWIP and IX */
extern int arp_lookup_mac(struct ip_addr *addr, struct eth_addr *mac);

int tcp_output_packet(struct eth_fg *cur_fg, struct tcp_pcb *pcb, struct pbuf *p)
{
	int ret;
	struct mbuf *pkt;
	struct eth_hdr *ethhdr;
	struct ip_hdr *iphdr;
	unsigned char *payload;
	struct pbuf *curp;
	struct ip_addr dst_addr;

	pkt = mbuf_alloc_local();
	if (unlikely(!pkt))
		return -ENOMEM;

	ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
	iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
	payload = mbuf_nextd(iphdr, unsigned char *);

	dst_addr.addr = ntoh32(pcb->remote_ip.addr);

	/* setup IP hdr */
	IPH_VHL_SET(iphdr, 4, sizeof(struct ip_hdr) / 4);
	//iphdr->header_len = sizeof(struct ip_hdr) / 4;
	//iphdr->version = 4;
	iphdr->_len = hton16(sizeof(struct ip_hdr) + p->tot_len);
	iphdr->_id = 0;
	iphdr->_offset = 0;
	iphdr->_proto = IP_PROTO_TCP;
	iphdr->_chksum = 0;
	iphdr->_tos = pcb->tos;
	iphdr->_ttl = pcb->ttl;
	iphdr->src.addr = pcb->local_ip.addr;
	iphdr->dest.addr = pcb->remote_ip.addr;

	for (curp = p; curp; curp = curp->next) {
		memcpy(payload, curp->payload, curp->len);
		payload += curp->len;
	}

	/* Offload IP and TCP tx checksums */
	pkt->ol_flags = PKT_TX_IP_CKSUM;
	pkt->ol_flags |= PKT_TX_TCP_CKSUM;

	ret = ip_send_one(cur_fg, &dst_addr, pkt, sizeof(struct eth_hdr) +
			  sizeof(struct ip_hdr) + p->tot_len);
	if (unlikely(ret)) {
		mbuf_free(pkt);
		return -EIO;
	}

	return 0;
}


int tcp_api_init(void)
{
	int ret;
	ret = mempool_create_datastore(&pcb_datastore, MAX_PCBS,
				       sizeof(struct tcpapi_pcb), 0, MEMPOOL_DEFAULT_CHUNKSIZE, "pcb");
	if (ret)
		return ret;

	if (pcb_datastore.elem_len != TCPAPI_PCB_SIZE)
		panic("tcp_api_init -- wrong ELEM_LEN\n");

	ret = mempool_create_datastore(&id_datastore, MAX_PCBS,
				       sizeof(struct ip_tuple), 1, MEMPOOL_DEFAULT_CHUNKSIZE, "ip");
	if (ret)
		return ret;

	ret = mempool_pagemem_map_to_user(&id_datastore);
	return ret;
}


int tcp_api_init_cpu(void)
{
	int ret;
	ret = mempool_create(&percpu_get(pcb_mempool), &pcb_datastore, MEMPOOL_SANITY_PERCPU, percpu_get(cpu_id));
	if (ret)
		return ret;

	ret = mempool_create(&percpu_get(id_mempool), &id_datastore, MEMPOOL_SANITY_PERCPU, percpu_get(cpu_id));
	if (ret)
		return ret;

	if (CFG.num_ports == 0) {
		ret = tcp_listen_with_backlog(&percpu_get(listen_ports[0]), TCP_DEFAULT_LISTEN_BACKLOG, IP_ADDR_ANY, DEFAULT_PORT);
		if (ret)
			return ret;
	} else {
		int i;
		for (i = 0; i < CFG.num_ports; i++) {
			ret = tcp_listen_with_backlog(&percpu_get(listen_ports[i]), TCP_DEFAULT_LISTEN_BACKLOG, IP_ADDR_ANY, CFG.ports[i]);
			if (ret)
				return ret;
		}
	}

//	percpu_get(port8000).accept = on_accept;


	return 0;
}

int tcp_api_init_fg(void)
{

	return 0;
}


