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

#ifndef __LWIP_MEMP_H__
#define __LWIP_MEMP_H__

#include <ix/types.h>
#include <ix/cpu.h>
#include <ix/mempool.h>

#include "lwip/opt.h"

/* Create the list of all memory pools managed by memp. MEMP_MAX represents a NULL pool at the end */
typedef enum {
#define LWIP_MEMPOOL(name,num,size,desc)  MEMP_##name,
#include "lwip/memp_std.h"
  MEMP_MAX
} memp_t;

extern const u16_t memp_sizes[MEMP_MAX];

int  memp_init(void);
int  memp_init_cpu(void);

DECLARE_PERCPU(struct mempool, pbuf_mempool);
DECLARE_PERCPU(struct mempool, pbuf_with_payload_mempool);
DECLARE_PERCPU(struct mempool, tcp_pcb_mempool);
DECLARE_PERCPU(struct mempool, tcp_pcb_listen_mempool);
DECLARE_PERCPU(struct mempool, tcp_seg_mempool);

static inline void *memp_malloc(memp_t type)
{
	switch (type) {
	case MEMP_PBUF:
		return mempool_alloc(&percpu_get(pbuf_mempool));
	case MEMP_TCP_PCB:
		return mempool_alloc(&percpu_get(tcp_pcb_mempool));
	case MEMP_TCP_PCB_LISTEN:
		return mempool_alloc(&percpu_get(tcp_pcb_listen_mempool));
	case MEMP_TCP_SEG:
		return mempool_alloc(&percpu_get(tcp_seg_mempool));
	case MEMP_SYS_TIMEOUT:
	case MEMP_PBUF_POOL:
	case MEMP_MAX:
		break;
	}

	return NULL;
}

static inline void memp_free(memp_t type, void *mem)
{
	switch (type) {
	case MEMP_PBUF:
		mempool_free(&percpu_get(pbuf_mempool), mem);
		return;
	case MEMP_TCP_PCB:
		mempool_free(&percpu_get(tcp_pcb_mempool), mem);
		return;
	case MEMP_TCP_PCB_LISTEN:
		mempool_free(&percpu_get(tcp_pcb_listen_mempool), mem);
		return;
	case MEMP_TCP_SEG:
		mempool_free(&percpu_get(tcp_seg_mempool), mem);
		return;
	case MEMP_SYS_TIMEOUT:
	case MEMP_PBUF_POOL:
	case MEMP_MAX:
		break;
	}
}

#endif /* __LWIP_MEMP_H__ */
