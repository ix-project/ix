Overview
--------
IX a protected dataplane operating system project from Stanford and EPFL. It provides event-driven applications with:
* low-latency (including at the tail)
* high-throughput
* efficient use of resources (to support workload consolidation and energy proportionality)

IX is licensed under an MIT-style license.  See LICENSE.

Requirements
------------
IX requires Intel DPDK and a compatible Intel NIC (e.g. 82599, X520,
X540, etc).

Setup Instructions
------------------

To fetch the dependencies:

    ./deps/fetch-deps.sh

To build the dependecies:

    make -sj64 -C deps/dune kern libdune
    make -sj64 -C deps/dpdk config T=x86_64-native-linuxapp-gcc
    make -sj64 -C deps/dpdk

To build IX:

    sudo apt-get install libconfig-dev libnuma-dev
    make -sj64

To run the IX TCP echo server:

    cp ix.conf.sample ix.conf
    # modify at least host_addr, gateway_addr, devices, and cpu
    sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'
    sudo modprobe -r ixgbe
    sudo insmod deps/dune/kern/dune.ko
    sudo ./dp/ix -- ./apps/echoserver 4

Then, try from another computer:

    echo 123 | nc -vv 192.168.21.1 1234
