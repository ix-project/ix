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
 * actually not a pragma once file
 */


DEF_KSTATS(none);
DEF_KSTATS(idle);
DEF_KSTATS(user);
DEF_KSTATS(timer);
DEF_KSTATS(timer_collapse);
DEF_KSTATS(print_kstats);
DEF_KSTATS(percpu_bookkeeping);
DEF_KSTATS(tx_reclaim);
DEF_KSTATS(tx_send);
DEF_KSTATS(rx_poll);
DEF_KSTATS(rx_recv);
DEF_KSTATS(bsys);
DEF_KSTATS(timer_tcp_fasttmr);
DEF_KSTATS(timer_tcp_slowtmr);
DEF_KSTATS(eth_input);
DEF_KSTATS(tcp_input_fast_path);
DEF_KSTATS(tcp_input_listen);
DEF_KSTATS(tcp_output_syn);
DEF_KSTATS(tcp_unified_handler);
DEF_KSTATS(timer_tcp_send_delayed_ack);
DEF_KSTATS(timer_handler);
DEF_KSTATS(timer_tcp_retransmit);
DEF_KSTATS(timer_tcp_persist);
DEF_KSTATS(bsys_dispatch_one);
DEF_KSTATS(bsys_tcp_accept);
DEF_KSTATS(bsys_tcp_close);
DEF_KSTATS(bsys_tcp_connect);
DEF_KSTATS(bsys_tcp_recv_done);
DEF_KSTATS(bsys_tcp_reject);
DEF_KSTATS(bsys_tcp_send);
DEF_KSTATS(bsys_tcp_sendv);
DEF_KSTATS(bsys_udp_recv_done);
DEF_KSTATS(bsys_udp_send);
DEF_KSTATS(bsys_udp_sendv);

DEF_KSTATS(posix_syscall);
