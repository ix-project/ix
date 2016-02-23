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
 * ethqueue.c - ethernet queue support
 */

#include <ix/stddef.h>
#include <ix/kstats.h>
#include <ix/ethdev.h>
#include <ix/log.h>
#include <ix/control_plane.h>

/* Accumulate metrics period (in us) */
#define METRICS_PERIOD_US 10000

/* Power measurement period (in us) */
#define POWER_PERIOD_US 500000

#define EMA_SMOOTH_FACTOR_0 0.5
#define EMA_SMOOTH_FACTOR_1 0.25
#define EMA_SMOOTH_FACTOR_2 0.125
#define EMA_SMOOTH_FACTOR EMA_SMOOTH_FACTOR_0

DEFINE_PERCPU(int, eth_num_queues);
DEFINE_PERCPU(struct eth_rx_queue *, eth_rxqs[NETHDEV]);
DEFINE_PERCPU(struct eth_tx_queue *, eth_txqs[NETHDEV]);

struct metrics_accumulator {
	long timestamp;
	long queuing_delay;
	int batch_size;
	int count;
	long queue_size;
	long loop_duration;
	long prv_timestamp;
};

static DEFINE_PERCPU(struct metrics_accumulator, metrics_acc);

struct power_accumulator {
	int prv_energy;
	long prv_timestamp;
};

static struct power_accumulator power_acc;

/* FIXME: convert to per-flowgroup */
//DEFINE_PERQUEUE(struct eth_tx_queue *, eth_txq);

unsigned int eth_rx_max_batch = 64;

/**
 * eth_process_poll - polls HW for new packets
 *
 * Returns the number of new packets received.
 */
int eth_process_poll(void)
{
	int i, count = 0;
	struct eth_rx_queue *rxq;

	for (i = 0; i < percpu_get(eth_num_queues); i++) {
		rxq = percpu_get(eth_rxqs[i]);
		count += eth_rx_poll(rxq);
	}

	return count;
}

static int eth_process_recv_queue(struct eth_rx_queue *rxq)
{
	struct mbuf *pos = rxq->head;
#ifdef ENABLE_KSTATS
	kstats_accumulate tmp;
#endif

	if (!pos)
		return -EAGAIN;

	/* NOTE: pos could get freed after eth_input(), so check next here */
	rxq->head = pos->next;
	rxq->len--;

	KSTATS_PUSH(eth_input, &tmp);
	eth_input(rxq, pos);
	KSTATS_POP(&tmp);

	return 0;
}

/**
 * eth_process_recv - processes pending received packets
 *
 * Returns true if there are no remaining packets.
 */
int eth_process_recv(void)
{
	int i, count = 0;
	bool empty;
	unsigned long min_timestamp = -1;
	unsigned long timestamp;
	int value;
	struct metrics_accumulator *this_metrics_acc = &percpu_get(metrics_acc);
	int backlog;
	double idle;
	unsigned int energy;
	int energy_diff;

	/*
	 * We round robin through each queue one packet at
	 * a time for fairness, and stop when all queues are
	 * empty or the batch limit is hit. We're okay with
	 * going a little over the batch limit if it means
	 * we're not favoring one queue over another.
	 */
	do {
		empty = true;
		for (i = 0; i < percpu_get(eth_num_queues); i++) {
			struct eth_rx_queue *rxq = percpu_get(eth_rxqs[i]);
			struct mbuf *pos = rxq->head;
			if (pos)
				min_timestamp = min(min_timestamp, pos->timestamp);
			if (!eth_process_recv_queue(rxq)) {
				count++;
				empty = false;
			}
		}
	} while (!empty && count < eth_rx_max_batch);

	backlog = 0;
	for (i = 0; i < percpu_get(eth_num_queues); i++)
		backlog += percpu_get(eth_rxqs[i])->len;

	timestamp = rdtsc();
	this_metrics_acc->count++;
	value = count ? (timestamp - min_timestamp) / cycles_per_us : 0;
	this_metrics_acc->queuing_delay += value;
	this_metrics_acc->batch_size += count;
	this_metrics_acc->queue_size += count + backlog;
	this_metrics_acc->loop_duration += timestamp - this_metrics_acc->prv_timestamp;
	this_metrics_acc->prv_timestamp = timestamp;
	if (timestamp - this_metrics_acc->timestamp > (long) cycles_per_us * METRICS_PERIOD_US) {
		idle = (double) percpu_get(idle_cycles) / (timestamp - this_metrics_acc->timestamp);
		EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].idle[0], idle, EMA_SMOOTH_FACTOR_0);
		EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].idle[1], idle, EMA_SMOOTH_FACTOR_1);
		EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].idle[2], idle, EMA_SMOOTH_FACTOR_2);
		if (this_metrics_acc->count) {
			this_metrics_acc->loop_duration -= percpu_get(idle_cycles);
			this_metrics_acc->loop_duration /= cycles_per_us;
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].queuing_delay, (double) this_metrics_acc->queuing_delay / this_metrics_acc->count, EMA_SMOOTH_FACTOR);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].batch_size, (double) this_metrics_acc->batch_size / this_metrics_acc->count, EMA_SMOOTH_FACTOR);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].queue_size[0], (double) this_metrics_acc->queue_size / this_metrics_acc->count, EMA_SMOOTH_FACTOR_0);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].queue_size[1], (double) this_metrics_acc->queue_size / this_metrics_acc->count, EMA_SMOOTH_FACTOR_1);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].queue_size[2], (double) this_metrics_acc->queue_size / this_metrics_acc->count, EMA_SMOOTH_FACTOR_2);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].loop_duration, (double) this_metrics_acc->loop_duration / this_metrics_acc->count, EMA_SMOOTH_FACTOR_0);
		} else {
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].queuing_delay, 0, EMA_SMOOTH_FACTOR);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].batch_size, 0, EMA_SMOOTH_FACTOR);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].queue_size[0], 0, EMA_SMOOTH_FACTOR_0);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].queue_size[1], 0, EMA_SMOOTH_FACTOR_1);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].queue_size[2], 0, EMA_SMOOTH_FACTOR_2);
			EMA_UPDATE(cp_shmem->cpu_metrics[percpu_get(cpu_nr)].loop_duration, 0, EMA_SMOOTH_FACTOR_0);
		}
		this_metrics_acc->timestamp = timestamp;
		percpu_get(idle_cycles) = 0;
		this_metrics_acc->count = 0;
		this_metrics_acc->queuing_delay = 0;
		this_metrics_acc->batch_size = 0;
		this_metrics_acc->queue_size = 0;
		this_metrics_acc->loop_duration = 0;
	}
	/* NOTE: assuming that the first CPU never idles */
	if (percpu_get(cpu_nr) == 0 && timestamp - power_acc.prv_timestamp > (long) cycles_per_us * POWER_PERIOD_US) {
		energy = rdmsr(MSR_PKG_ENERGY_STATUS);
		if (power_acc.prv_timestamp) {
			energy_diff = energy - power_acc.prv_energy;
			if (energy_diff < 0)
				energy_diff += 1 << 31;
			cp_shmem->pkg_power = (double) energy_diff * energy_unit / (timestamp - power_acc.prv_timestamp) * cycles_per_us * 1000000;
		} else {
			cp_shmem->pkg_power = 0;
		}
		power_acc.prv_timestamp = timestamp;
		power_acc.prv_energy = energy;
	}

	KSTATS_PACKETS_INC(count);
	KSTATS_BATCH_INC(count);
#ifdef ENABLE_KSTATS
	backlog = div_up(backlog, eth_rx_max_batch);
	KSTATS_BACKLOG_INC(backlog);
#endif

	return empty;
}

/**
 * eth_process_send - processes packets pending to be sent
 */
void eth_process_send(void)
{
	int i, nr;
	struct eth_tx_queue *txq;

	for (i = 0; i < percpu_get(eth_num_queues); i++) {
		txq = percpu_get(eth_txqs[i]);

		nr = eth_tx_xmit(txq, txq->len, txq->bufs);
		if (unlikely(nr != txq->len))
			panic("transmit buffer size mismatch\n");

		txq->len = 0;
	}
}

/**
 * eth_process_reclaim - processs packets that have completed sending
 */
void eth_process_reclaim(void)
{
	int i;
	struct eth_tx_queue *txq;

	for (i = 0; i < percpu_get(eth_num_queues); i++) {
		txq = percpu_get(eth_txqs[i]);
		txq->cap = eth_tx_reclaim(txq);
	}
}

