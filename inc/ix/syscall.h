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
 * syscall.h - system call support (regular and batched)
 */

#pragma once

#include <ix/compiler.h>
#include <ix/types.h>
#include <ix/cpu.h>

#define SYSCALL_START	0x100000


/*
 * Data structures used as arguments.
 */

struct ip_tuple {
	uint32_t src_ip;
	uint32_t dst_ip;
	uint16_t src_port;
	uint16_t dst_port;
} __packed;

struct sg_entry {
	void *base;
	size_t len;
} __packed;

#define MAX_SG_ENTRIES	30

typedef long hid_t;

enum {
	RET_OK		= 0, /* Successful                 */
	RET_NOMEM	= 1, /* Out of memory              */
	RET_NOBUFS	= 2, /* Out of buffer space        */
	RET_INVAL	= 3, /* Invalid parameter          */
	RET_AGAIN	= 4, /* Try again later            */
	RET_FAULT	= 5, /* Bad memory address         */
	RET_NOSYS	= 6, /* System call does not exist */
	RET_NOTSUP	= 7, /* Operation is not supported */
	RET_BADH	= 8, /* An invalid handle was used */
	RET_CLOSED	= 9, /* The connection is closed   */
	RET_CONNREFUSED = 10, /* Connection refused        */
};


/*
 * System calls
 */
enum {
	SYS_BPOLL = 0,
	SYS_BCALL,
	SYS_BADDR,
	SYS_MMAP,
	SYS_MUNMAP,
	SYS_SPAWNMODE,
	SYS_NRCPUS,
	SYS_TIMER_INIT,
	SYS_TIMER_CTL,
	SYS_NR,
};


/*
 * Batched system calls
 */

typedef long(*bsysfn_t)(uint64_t, uint64_t, uint64_t, uint64_t);

/*
 * batched system call descriptor format:
 * sysnr: - the system call number
 * arga-argd: parameters one through four
 * argd: overwritten with the return code
 */
struct bsys_desc {
	uint64_t sysnr;
	uint64_t arga, argb, argc, argd;
} __packed;

struct bsys_ret {
	uint64_t sysnr;
	uint64_t cookie;
	long ret;
	uint64_t pad[2];
} __packed;

#define BSYS_DESC_NOARG(desc, vsysnr) \
	(desc)->sysnr = (uint64_t) (vsysnr)
#define BSYS_DESC_1ARG(desc, vsysnr, varga) \
	BSYS_DESC_NOARG(desc, vsysnr),      \
	(desc)->arga = (uint64_t) (varga)
#define BSYS_DESC_2ARG(desc, vsysnr, varga, vargb) \
	BSYS_DESC_1ARG(desc, vsysnr, varga),       \
	(desc)->argb = (uint64_t) (vargb)
#define BSYS_DESC_3ARG(desc, vsysnr, varga, vargb, vargc) \
	BSYS_DESC_2ARG(desc, vsysnr, varga, vargb),       \
	(desc)->argc = (uint64_t) (vargc)
#define BSYS_DESC_4ARG(desc, vsysnr, varga, vargb, vargc, vargd) \
	BSYS_DESC_3ARG(desc, vsysnr, varga, vargb, vargc),       \
	(desc)->argd = (uint64_t) (vargd)

struct bsys_arr {
	unsigned long len;
	unsigned long max_len;
	struct bsys_desc descs[];
};

/**
 * __bsys_arr_next - get the next free descriptor
 * @a: the syscall array
 *
 * Returns a descriptor, or NULL if none are available.
 */
static inline struct bsys_desc *__bsys_arr_next(struct bsys_arr *a)
{
	return &a->descs[a->len++];
}

/**
 * bsys_arr_next - get the next free descriptor, checking for overflow
 * @a: the syscall array
 *
 * Returns a descriptor, or NULL if none are available.
 */
static inline struct bsys_desc *bsys_arr_next(struct bsys_arr *a)
{
	if (a->len >= a->max_len)
		return NULL;

	return __bsys_arr_next(a);
}


/*
 * Commands that can be sent from the user-level application to the kernel.
 */

enum {
	KSYS_UDP_SEND = 0,
	KSYS_UDP_SENDV,
	KSYS_UDP_RECV_DONE,
	KSYS_TCP_CONNECT,
	KSYS_TCP_ACCEPT,
	KSYS_TCP_REJECT,
	KSYS_TCP_SEND,
	KSYS_TCP_SENDV,
	KSYS_TCP_RECV_DONE,
	KSYS_TCP_CLOSE,
	KSYS_NR,
};

/**
 * ksys_udp_send - transmits a UDP packet
 * @d: the syscall descriptor to program
 * @addr: the address of the packet data
 * @len: the length of the packet data
 * @id: the UDP 4-tuple
 * @cookie: a user-level tag for the request
 */
static inline void ksys_udp_send(struct bsys_desc *d, void *addr,
				 size_t len, struct ip_tuple *id,
				 unsigned long cookie)
{
	BSYS_DESC_4ARG(d, KSYS_UDP_SEND, addr, len, id, cookie);
}

/**
 * ksys_udp_sendv - transmits a UDP packet using scatter-gather data
 * @d: the syscall descriptor to program
 * @ents: the scatter-gather vector array
 * @nrents: the number of scatter-gather vectors
 * @cookie: a user-level tag for the request
 * @id: the UDP 4-tuple
 */
static inline void ksys_udp_sendv(struct bsys_desc *d,
				  struct sg_entry *ents,
				  unsigned int nrents,
				  struct ip_tuple *id,
				  unsigned long cookie)
{
	BSYS_DESC_4ARG(d, KSYS_UDP_SENDV, ents, nrents, id, cookie);
}

/**
 * ksys_udp_recv_done - inform the kernel done using a UDP packet buffer
 * @d: the syscall descriptor to program
 * @iomap: an address anywhere inside the mbuf
 *
 * NOTE: Calling this function allows the kernel to free mbuf's when
 * the application has finished using them.
 */
static inline void ksys_udp_recv_done(struct bsys_desc *d, void *iomap)
{
	BSYS_DESC_1ARG(d, KSYS_UDP_RECV_DONE, iomap);
}

/**
 * ksys_tcp_connect - create a TCP connection
 * @d: the syscall descriptor to program
 * @id: the TCP 4-tuple
 * @cookie: a user-level tag for the flow
 */
static inline void
ksys_tcp_connect(struct bsys_desc *d, struct ip_tuple *id,
		 unsigned long cookie)
{
	BSYS_DESC_2ARG(d, KSYS_TCP_CONNECT, id, cookie);
}

/**
 * ksys_tcp_accept - accept a TCP connection request
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 * @cookie: a user-level tag for the flow
 */
static inline void
ksys_tcp_accept(struct bsys_desc *d, hid_t handle, unsigned long cookie)
{
	BSYS_DESC_2ARG(d, KSYS_TCP_ACCEPT, handle, cookie);
}

/**
 * ksys_tcp_reject - reject a TCP connection request
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 */
static inline void
ksys_tcp_reject(struct bsys_desc *d, hid_t handle)
{
	BSYS_DESC_1ARG(d, KSYS_TCP_REJECT, handle);
}

/**
 * ksys_tcp_send - send data on a TCP flow
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 * @addr: the address of the data
 * @len: the length of the data
 */
static inline void
ksys_tcp_send(struct bsys_desc *d, hid_t handle,
	      void *addr, size_t len)
{
	BSYS_DESC_3ARG(d, KSYS_TCP_SEND, handle, addr, len);
}

/**
 * ksys_tcp_sendv - send scatter-gather data on a TCP flow
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 * @ents: an array of scatter-gather vectors
 * @nrents: the number of scatter-gather vectors
 */
static inline void
ksys_tcp_sendv(struct bsys_desc *d, hid_t handle,
	       struct sg_entry *ents, unsigned int nrents)
{
	BSYS_DESC_3ARG(d, KSYS_TCP_SENDV, handle, ents, nrents);
}

/**
 * ksys_tcp_recv_done - acknowledge the receipt of TCP data buffers
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 * @len: the number of bytes to acknowledge
 *
 * NOTE: This function is used to free memory and to adjust
 * the receive window.
 */
static inline void
ksys_tcp_recv_done(struct bsys_desc *d, hid_t handle, size_t len)
{
	BSYS_DESC_2ARG(d, KSYS_TCP_RECV_DONE, handle, len);
}

/**
 * ksys_tcp_close - closes a TCP connection
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 */
static inline void
ksys_tcp_close(struct bsys_desc *d, hid_t handle)
{
	BSYS_DESC_1ARG(d, KSYS_TCP_CLOSE, handle);
}


/*
 * Commands that can be sent from the kernel to the user-level application.
 */

enum {
	USYS_UDP_RECV = 0,
	USYS_UDP_SENT,
	USYS_TCP_CONNECTED,
	USYS_TCP_KNOCK,
	USYS_TCP_RECV,
	USYS_TCP_SENT,
	USYS_TCP_DEAD,
	USYS_TIMER,
	USYS_NR,
};

#ifdef __KERNEL__

DECLARE_PERCPU(struct bsys_arr *, usys_arr);
DECLARE_PERCPU(unsigned long, syscall_cookie);

/**
 * usys_reset - reset the batched call array
 *
 * Call this before batching system calls.
 */
static inline void usys_reset(void)
{
	percpu_get(usys_arr)->len = 0;
}

/**
 * usys_next - get the next batched syscall descriptor
 *
 * Returns a syscall descriptor.
 */
static inline struct bsys_desc *usys_next(void)
{
	return __bsys_arr_next(percpu_get(usys_arr));
}

/**
 * usys_udp_recv - receive a UDP packet
 * @addr: the address of the packet data
 * @len: the length of the packet data
 * @id: the UDP 4-tuple
 */
static inline void usys_udp_recv(void *addr, size_t len, struct ip_tuple *id)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_UDP_RECV, addr, len, id);
}

/**
 * usys_udp_sent - Notifies the user that a UDP packet send completed
 * @cookie: a user-level token for the request
 *
 * NOTE: Calling this function allows the user application to unpin memory
 * that was locked for zero copy transfer. Acknowledgements are always in
 * FIFO order.
 */
static inline void usys_udp_sent(unsigned long cookie)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_1ARG(d, USYS_UDP_SENT, cookie);
}

/**
 * usys_tcp_connected - Notifies the user that an outgoing connection attempt
 *			has completed. (not necessarily successfully)
 * @handle: the TCP flow handle
 * @cookie: a user-level token
 * @ret: the result (return code)
 */
static inline void
usys_tcp_connected(hid_t handle, unsigned long cookie, long ret)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_TCP_CONNECTED, handle, cookie, ret);
}

/**
 * usys_tcp_knock - Notifies the user that a remote host is
 *                  trying to open a connection
 * @handle: the TCP flow handle
 * @id: the TCP 4-tuple
 */
static inline void
usys_tcp_knock(hid_t handle, struct ip_tuple *id)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_2ARG(d, USYS_TCP_KNOCK, handle, id);
}

/**
 * usys_tcp_recv - receive TCP data
 * @handle: the TCP flow handle
 * @cookie: a user-level tag for the flow
 * @addr: the address of the received data
 * @len: the length of the received data
 */
static inline void
usys_tcp_recv(hid_t handle, unsigned long cookie, void *addr, size_t len)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_4ARG(d, USYS_TCP_RECV, handle, cookie, addr, len);
}

/**
 * usys_tcp_sent - indicates transmission is finished
 * @handle: the TCP flow handle
 * @cookie: a user-level tag for the flow
 * @len: the length in bytes sent
 *
 * Typically, an application will use this notifier to unreference buffers
 * and to send more pending data.
 */
static inline void
usys_tcp_sent(hid_t handle, unsigned long cookie, size_t len)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_TCP_SENT, handle, cookie, len);
}

/**
 * usys_tcp_dead - indicates that the remote host has closed the connection
 * @handle: the TCP flow handle
 * @cookie: a user-level tag for the flow
 *
 * NOTE: the user must still close the connection on the local end
 * using ksys_tcp_close().
 */
static inline void
usys_tcp_dead(hid_t handle, unsigned long cookie)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_2ARG(d, USYS_TCP_DEAD, handle, cookie);
}

/**
 * usys_timer - indicates that there is a timer event
 */
static inline void
usys_timer(unsigned long cookie)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_1ARG(d, USYS_TIMER, cookie);
}

/*
 * Kernel system call definitions
 */

/* FIXME: could use Sparse to statically check */
#define __user

extern long bsys_udp_send(void __user *addr, size_t len,
			  struct ip_tuple __user *id,
			  unsigned long cookie);
extern long bsys_udp_sendv(struct sg_entry __user *ents,
			   unsigned int nrents,
			   struct ip_tuple __user *id,
			   unsigned long cookie);
extern long bsys_udp_recv_done(void *iomap);

extern long bsys_tcp_connect(struct ip_tuple __user *id,
			     unsigned long cookie);
extern long bsys_tcp_accept(hid_t handle, unsigned long cookie);
extern long bsys_tcp_reject(hid_t handle);
extern ssize_t bsys_tcp_send(hid_t handle, void *addr, size_t len);
extern ssize_t bsys_tcp_sendv(hid_t handle, struct sg_entry __user *ents,
			      unsigned int nrents);
extern long bsys_tcp_recv_done(hid_t handle, size_t len);
extern long bsys_tcp_close(hid_t handle);

struct dune_tf;
extern void do_syscall(struct dune_tf *tf, uint64_t sysnr);

extern int syscall_init_cpu(void);
extern void syscall_exit_cpu(void);

#endif /* __KERNEL__ */

