## Overview

IX a protected dataplane operating system project from Stanford and EPFL. It provides event-driven applications with:
* low-latency (including at the tail)
* high-throughput
* efficient use of resources (to support workload consolidation and energy proportionality)

IX is licensed under an MIT-style license.  See LICENSE.

## Requirements

IX requires Intel DPDK and a supported Intel NIC: 
- Intel 82599
- Intel X520
- Intel X540

For more details, check the [requirements page](https://github.com/ix-project/ix/wiki/Requirements) 

## Setup Instructions

There is currently no binary distribution of IX. You will therefore have to compile it from source. Additionally, you will need to fetch and compile the source dependencies:

1. fetch the dependencies:
   ```
   git submodule update --init
   ```

2. build the dependecies:
   ```
   sudo chmod +r /boot/System.map-`uname -r`
   make -sj64 -C deps/dune
   make -sj64 -C deps/pcidma
   make -sj64 -C deps/dpdk config T=x86_64-native-linuxapp-gcc
   make -sj64 -C deps/dpdk

   ```

3. build IX:
   ```
   sudo apt-get install libconfig-dev libnuma-dev
   make -sj64
   ```
The resulting executable files are `cp/ixcp.py` for the IX control plane and `dp/ix` for the IX dataplane kernel. 

4. Set up the environment:
   ```
   cp ix.conf.sample ix.conf
   # modify at least host_addr, gateway_addr, devices, and cpu
   sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'
   sudo insmod deps/dune/kern/dune.ko
   sudo insmod deps/pcidma/pcidma.ko
   sudo modprobe uio
   sudo insmod deps/dpdk/build/kmod/igb_uio.ko
   sudo ifdown INTERFACE
   sudo deps/dpdk/tools/dpdk_nic_bind.py -b igb_uio PCI_ADDRESS
   ```
   
5.  run the IX TCP echo server and check that it works. Make sure that at least the "Device" field in ix.conf matches your network card PCI bus address (try `lspci | grep -i eth` and look for the virtual adapter):

   ```
   sudo ./dp/ix -- ./apps/echoserver 4
   ```

   Then, try from another Linux host:
   ```
   echo 123 | nc -vv <IP> <PORT>
   ```
   You should see the following output: 
   ```
   Connection to <IP> <PORT> port [tcp/*] succeeded!
   123
   ```
