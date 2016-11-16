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
 * ethfg.c - Support for flow groups, the basic unit of load balancing
 */

#include <ix/stddef.h>
#include <ix/ethfg.h>
#include <ix/mem.h>
#include <ix/errno.h>
#include <ix/ethdev.h>
#include <ix/control_plane.h>
#include <ix/cfg.h>

#define TRANSITION_TIMEOUT (1 * ONE_MS)

extern const char __perfg_start[];
extern const char __perfg_end[];

DEFINE_PERCPU(void *, fg_offset);

int nr_flow_groups;

struct eth_fg *fgs[ETH_MAX_TOTAL_FG + NCPU];


struct queue {
	struct mbuf *head;
	struct mbuf *tail;
};

struct migration_info {
	struct timer transition_timeout;
	unsigned int prev_cpu;
	unsigned int target_cpu;
	DEFINE_BITMAP(fg_bitmap, ETH_MAX_TOTAL_FG);
};

DEFINE_PERCPU(struct queue, local_mbuf_queue);
DEFINE_PERCPU(struct queue, remote_mbuf_queue);
DEFINE_PERCPU(struct hlist_head, remote_timers_list);
DEFINE_PERCPU(uint64_t, remote_timer_pos);
DEFINE_PERCPU(struct migration_info, migration_info);

static void transition_handler_prev(struct timer *t, struct eth_fg *);
static void transition_handler_target(void *fg_);
static void migrate_pkts_to_remote(struct eth_fg *fg);
static void migrate_timers_to_remote(int fg_id);
static void migrate_timers_from_remote(void);
static void enqueue(struct queue *q, struct mbuf *pkt);

int init_migration_cpu(void)
{
	timer_init_entry(&percpu_get(migration_info).transition_timeout, transition_handler_prev);
	percpu_get(migration_info).prev_cpu = -1;

	return 0;
}

/**
 * eth_fg_init - initializes a flow group globally
 * @fg: the flow group
 * @idx: the index number of the flow group
 */
void eth_fg_init(struct eth_fg *fg, unsigned int idx)
{
	fg->perfg = NULL;
	fg->idx = idx;
	fg->cur_cpu = -1;
	fg->in_transition = false;
	spin_lock_init(&fg->lock);
}

/**
 * eth_fg_init_cpu - intialize a flow group for a specific CPU
 * @fg: the flow group
 *
 * Returns 0 if successful, otherwise fail.
 */
int eth_fg_init_cpu(struct eth_fg *fg)
{
	size_t len = __perfg_end - __perfg_start;
	char *addr;

	addr = mem_alloc_pages_onnode(div_up(len, PGSIZE_2MB),
				      PGSIZE_2MB, percpu_get(cpu_numa_node),
				      MPOL_BIND);
	if (!addr)
		return -ENOMEM;

	memset(addr, 0, len);
	fg->perfg = addr;

	return 0;
}

/**
 * eth_fg_free - frees all memory used by a flow group
 * @fg: the flow group
 */
void eth_fg_free(struct eth_fg *fg)
{
	size_t len = __perfg_end - __perfg_start;

	if (fg->perfg)
		mem_free_pages(fg->perfg, div_up(len, PGSIZE_2MB), PGSIZE_2MB);
}

static int eth_fg_assign_single_to_cpu(int fg_id, int cpu, struct rte_eth_rss_reta *rss_reta, struct ix_rte_eth_dev **eth)
{
	struct eth_fg *fg = fgs[fg_id];
	int ret;

	assert(!fg->in_transition);

	if (fg->cur_cpu == CFG.cpu[cpu]) {
		return 0;
	} else if (fg->cur_cpu == -1) {
		fg->cur_cpu = CFG.cpu[cpu];
		ret = 0;
	} else {
		assert(fg->cur_cpu == percpu_get(cpu_id));
		fg->in_transition = true;
		fg->prev_cpu = fg->cur_cpu;
		fg->cur_cpu = -1;
		fg->target_cpu = CFG.cpu[cpu];
		migrate_pkts_to_remote(fg);
		migrate_timers_to_remote(fg_id);
		ret = 1;
	}

	bitmap_set(rss_reta->mask, fg->idx);
	rss_reta->reta[fg->idx] = cpu;
	cp_shmem->flow_group[fg_id].cpu = cpu;
	*eth = fg->eth;

	return ret;
}

#include <lwip/tcp.h>
#include <lwip/tcp_impl.h>

static void migrate_fdir(struct ix_rte_eth_dev *dev, struct eth_fg *cur_fg,
			 int cpu)
{
	int ret;
	int idx;
	struct tcp_hash_entry *he;
	struct hlist_node *cur, *n, *tmp;
	struct tcp_pcb *pcb;
	struct rte_fdir_filter fdir_ftr;

	assert(cur_fg->cur_cpu == percpu_get(cpu_id));
	cur_fg->target_cpu = CFG.cpu[cpu];
	/* FIXME: implement */
	/* migrate_pkts_to_remote(cur_fg); */
	/* migrate_timers_to_remote(outbound_fg_idx()); */

	fdir_ftr.iptype = RTE_FDIR_IPTYPE_IPV4;
	fdir_ftr.l4type = RTE_FDIR_L4TYPE_TCP;

	hlist_for_each(&cur_fg->active_buckets, cur) {
		he = hlist_entry(cur, struct tcp_hash_entry, hash_link);
		hlist_for_each_safe(&he->pcbs, n, tmp) {
			pcb = hlist_entry(n, struct tcp_pcb, link);

			fdir_ftr.ip_src.ipv4_addr = ntoh32(pcb->remote_ip.addr);
			fdir_ftr.ip_dst.ipv4_addr = ntoh32(pcb->local_ip.addr);
			fdir_ftr.port_src = pcb->remote_port;
			fdir_ftr.port_dst = pcb->local_port;

			ret = dev->dev_ops->fdir_remove_perfect_filter(dev, &fdir_ftr, 0);
			assert(ret >= 0);

			ret = dev->dev_ops->fdir_add_perfect_filter(dev, &fdir_ftr, 0, cpu, 0);
			assert(ret >= 0);

			idx = tcp_to_idx(&pcb->local_ip, &pcb->remote_ip, pcb->local_port, pcb->remote_port);
			TCP_RMV_ACTIVE(pcb);
			TCP_REG_ACTIVE(pcb, idx, outbound_fg_remote(cur_fg->target_cpu));
		}
	}

	cur_fg->cur_cpu = CFG.cpu[cpu];
}

/**
 * eth_fg_assign_to_cpu - assigns the flow group to the given cpu
 * @fg_id: the flow group (global name; across devices)
 * @cpu: the cpu sequence number
 */
void eth_fg_assign_to_cpu(bitmap_ptr fg_bitmap, int cpu)
{
	int i, j;
	struct rte_eth_rss_reta rss_reta[NETHDEV];
	struct ix_rte_eth_dev *eth[NETHDEV], *first_eth;
	int ret;
	int real = 0;
	int count;
	int migrate;

	count = 0;
	for (i = 0; i < percpu_get(eth_num_queues); i++)
		count += percpu_get(eth_rxqs[i])->len;
	SCRATCHPAD->backlog_before = count;

	SCRATCHPAD->ts_migration_start = rdtsc();

	assert(percpu_get(migration_info).prev_cpu == -1);
	assert(percpu_get_remote(migration_info, CFG.cpu[cpu]).prev_cpu == -1);

	bitmap_init(percpu_get(migration_info).fg_bitmap, ETH_MAX_TOTAL_FG, 0);

	for (i = 0; i < NETHDEV; i++) {
		first_eth = NULL;
		bitmap_init(rss_reta[i].mask, ETH_MAX_NUM_FG, 0);
		eth[i] = NULL;

		for (j = 0; j < ETH_MAX_NUM_FG; j++) {
			if (!bitmap_test(fg_bitmap, i * ETH_MAX_NUM_FG + j))
				continue;
			ret = eth_fg_assign_single_to_cpu(i * ETH_MAX_NUM_FG + j, cpu, &rss_reta[i], &eth[i]);
			if (ret) {
				bitmap_set(percpu_get(migration_info).fg_bitmap, i * ETH_MAX_NUM_FG + j);
				real = 1;
			}
			if (!first_eth)
				first_eth = eth[i];
			else
				assert(first_eth == eth[i]);

		}
	}

	count = 0;
	struct mbuf *pkt = percpu_get_remote(remote_mbuf_queue, CFG.cpu[cpu]).head;
	while (pkt) {
		pkt = pkt->next;
		count++;
	}
	SCRATCHPAD->remote_queue_pkts_begin = count;

	if (!real) {
		percpu_get(cp_cmd)->status = CP_STATUS_READY;
	} else {
		percpu_get(migration_info).prev_cpu = percpu_get(cpu_id);
		percpu_get(migration_info).target_cpu = CFG.cpu[cpu];
		timer_add(&percpu_get(migration_info).transition_timeout, NULL, TRANSITION_TIMEOUT);
		percpu_get_remote(migration_info, CFG.cpu[cpu]) = percpu_get(migration_info);
	}

	for (i = 0; i < NETHDEV; i++) {
		if (eth[i])
			eth[i]->dev_ops->reta_update(eth[i], &rss_reta[i]);
	}

	SCRATCHPAD->ts_data_structures_done = rdtsc();

	for (i = 0; i < ETH_MAX_TOTAL_FG; i++)
		if (fgs[i] && fgs[i]->cur_cpu == percpu_get(cpu_id))
			break;

	/* If the current core has no FG assigned, then migrate flow director
	 * FGs to the destination core. */
	if (i == ETH_MAX_TOTAL_FG) {
		for (i = 0; i < NCPU; i++) {
			if (!outbound_fg_remote(i))
				continue;
			if (outbound_fg_remote(i)->cur_cpu !=
			    percpu_get(cpu_id))
				continue;
			migrate_fdir(eth[0], outbound_fg_remote(i), cpu);
		}
	}

	/* Try to migrate flow director FGs to the destination core. */
	migrate = 0;
	for (i = 0; i < NCPU; i++) {
		if (i == percpu_get(cpu_id) || !outbound_fg_remote(i))
			continue;
		if (outbound_fg_remote(i)->cur_cpu != percpu_get(cpu_id))
			continue;
		if (!migrate)
			continue;
		migrate_fdir(eth[0], outbound_fg_remote(i), cpu);
		migrate = !migrate;
	}

}

static void transition_handler_prev(struct timer *t, struct eth_fg *cur_fg)
{
	struct migration_info *info = container_of(t, struct migration_info, transition_timeout);
	assert(cur_fg == 0);
	if (!SCRATCHPAD->ts_first_pkt_at_target)
		SCRATCHPAD->ts_first_pkt_at_target = rdtsc();
	SCRATCHPAD->timer_fired = 1;
	cpu_run_on_one(transition_handler_target, info, info->target_cpu);
}

static void early_transition_handler_prev(void *unused)
{
	struct migration_info *info = &percpu_get(migration_info);

	if (!timer_pending(&info->transition_timeout))
		return;

	timer_del(&info->transition_timeout);

	/* A packet of a migrated flow group has arrived to the target cpu. This
	 * means that the migration has been completed in hardware. Read all the
	 * remaining packets from the previous cpu queue. */
	eth_process_poll();

	cpu_run_on_one(transition_handler_target, info, info->target_cpu);
}

static struct eth_rx_queue *queue_from_fg(struct eth_fg *fg)
{
	int i;
	struct eth_rx_queue *rxq;

	for (i = 0; i < percpu_get(eth_num_queues); i++) {
		rxq = percpu_get(eth_rxqs[i]);
		if (fg->eth == rxq->dev)
			return rxq;
	}

	assert(false);
}

static void transition_handler_target(void *info_)
{
	struct mbuf *pkt, *next;
	struct queue *q;
	struct migration_info *info = (struct migration_info *) info_;
	struct eth_fg *fg;
	int prev_cpu = info->prev_cpu;
	int i;
	int count;

	SCRATCHPAD->ts_before_backlog = rdtsc();

	for (i = 0; i < ETH_MAX_TOTAL_FG; i++) {
		if (!bitmap_test(info->fg_bitmap, i))
			continue;
		fg = fgs[i];
		fg->in_transition = false;
		fg->cur_cpu = fg->target_cpu;
		fg->target_cpu = -1;
		fg->prev_cpu = -1;
	}

	count = 0;
	q = &percpu_get(remote_mbuf_queue);
	pkt = q->head;
	while (pkt) {
		next = pkt->next;
		/* FIXME: Hard to get queue at this point. Nevertheless, it is
		 * not used in eth_input */
		eth_input(NULL, pkt);
		pkt = next;
		count++;
	}
	q->head = NULL;
	q->tail = NULL;

	SCRATCHPAD->remote_queue_pkts_end = count;

	count = 0;
	q = &percpu_get(local_mbuf_queue);
	pkt = q->head;
	while (pkt) {
		next = pkt->next;
		/* FIXME: see previous */
		eth_input(NULL, pkt);
		pkt = next;
		count++;
	}
	q->head = NULL;
	q->tail = NULL;

	SCRATCHPAD->local_queue_pkts = count;

	SCRATCHPAD->ts_after_backlog = rdtsc();

	migrate_timers_from_remote();

	SCRATCHPAD->ts_migration_end = rdtsc();

	count = 0;
	for (i = 0; i < percpu_get(eth_num_queues); i++)
		count += percpu_get(eth_rxqs[i])->len;
	SCRATCHPAD->backlog_after = count;
	SCRATCHPAD_NEXT;

	percpu_get(migration_info).prev_cpu = -1;
	info->prev_cpu = -1;
	percpu_get_remote(cp_cmd, prev_cpu)->status = CP_STATUS_READY;
}

static void migrate_pkts_to_remote(struct eth_fg *fg)
{
	struct eth_rx_queue *rxq = queue_from_fg(fg);
	struct mbuf *pkt = rxq->head;
	struct mbuf **prv = &rxq->head;
	struct queue *q = &percpu_get_remote(remote_mbuf_queue, fg->target_cpu);

	while (pkt) {
		if (fg->fg_id == pkt->fg_id) {
			*prv = pkt->next;
			enqueue(q, pkt);
			pkt = *prv;
			rxq->len--;
		} else {
			prv = &pkt->next;
			pkt = pkt->next;
		}
	}
	rxq->tail = container_of(prv, struct mbuf, next);
}

static void enqueue(struct queue *q, struct mbuf *pkt)
{
	pkt->next = NULL;
	if (!q->head) {
		q->head = pkt;
		q->tail = pkt;
	} else {
		q->tail->next = pkt;
		q->tail = pkt;
	}
}

void eth_recv_at_prev(struct eth_rx_queue *rx_queue, struct mbuf *pkt)
{
	if (SCRATCHPAD->ts_last_pkt_at_prev == 0) {
		SCRATCHPAD->ts_last_pkt_at_prev = rdtsc();
		SCRATCHPAD->ts_first_pkt_at_prev = SCRATCHPAD->ts_last_pkt_at_prev;
	} else {
		SCRATCHPAD->ts_last_pkt_at_prev = rdtsc();
	}

	struct eth_fg *fg = fgs[pkt->fg_id];
	struct queue *q = &percpu_get_remote(remote_mbuf_queue, fg->target_cpu);
	enqueue(q, pkt);
}

void eth_recv_at_target(struct eth_rx_queue *rx_queue, struct mbuf *pkt)
{
	if (SCRATCHPAD->ts_last_pkt_at_target == 0) {
		SCRATCHPAD->ts_last_pkt_at_target = rdtsc();
		SCRATCHPAD->ts_first_pkt_at_target = SCRATCHPAD->ts_last_pkt_at_target;
	} else {
		SCRATCHPAD->ts_last_pkt_at_target = rdtsc();
	}

	struct queue *q = &percpu_get(local_mbuf_queue);
	if (!q->head) {
		/*
		 * When we receive the first packet on the target CPU, we cause
		 * an early transition and we don't wait for the timeout to
		 * expire.
		 */
		struct migration_info *info = &percpu_get(migration_info);
		cpu_run_on_one(early_transition_handler_prev, NULL, info->prev_cpu);
	}
	enqueue(q, pkt);
}

int eth_recv_handle_fg_transition(struct eth_rx_queue *rx_queue, struct mbuf *pkt)
{
	if (pkt->fg_id == MBUF_INVALID_FG_ID)
		pkt->fg_id = outbound_fg_idx();
	struct eth_fg *fg = fgs[pkt->fg_id];

	if (!fg->in_transition && fg->cur_cpu == percpu_get(cpu_id)) {
		/* continue processing */
		return 0;
	} else if (fg->in_transition && fg->prev_cpu == percpu_get(cpu_id)) {
		eth_recv_at_prev(rx_queue, pkt);
		return 1;
	} else if (fg->in_transition && fg->target_cpu == percpu_get(cpu_id)) {
		eth_recv_at_target(rx_queue, pkt);
		return 1;
	} else {
		/* FIXME: somebody must mbuf_free(pkt) but we cannot do it here
		   because we don't own the memory pool */
		log_warn("dropping packet: flow group %d of device %d should be handled by cpu %d\n", fg->idx, fg->dev_idx, fg->cur_cpu);
		return 1;
	}
}

static void migrate_timers_to_remote(int fg_id)
{
	struct eth_fg *fg = fgs[fg_id];
	struct hlist_head *timers_list = &percpu_get_remote(remote_timers_list, fg->target_cpu);
	uint64_t *timer_pos = &percpu_get_remote(remote_timer_pos, fg->target_cpu);
	uint8_t fg_vector[ETH_MAX_TOTAL_FG] = {0};

	fg_vector[fg_id] = 1;
	hlist_init_head(timers_list);
	SCRATCHPAD->timers = timer_collect_fgs(fg_vector, timers_list, timer_pos);
}

static void migrate_timers_from_remote(void)
{
	timer_reinject_fgs(&percpu_get(remote_timers_list), percpu_get(remote_timer_pos));
}
