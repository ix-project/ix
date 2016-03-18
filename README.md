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
   ./deps/fetch-deps.sh
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

4. Set up the environment:
   ```
   cp ix.conf.sample ix.conf
   # modify at least host_addr, gateway_addr, devices, and cpu
   sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'
   sudo modprobe -r ixgbe
   sudo insmod deps/dune/kern/dune.ko
   sudo insmod deps/pcidma/pcidma.ko
   ```
   
5.  run the IX TCP echo server and check that it works. Make sure that at least the "Device" field in ix.conf matches your network card PCI bus address (try `lspci | grep -i eth` and look for the virtual adapter):

   ```
   sudo ./dp/ix -- ./apps/echoserver 4
   ```

   Then, try from another host:
   ```
   echo 123 | nc -vv <IP> <PORT>
   ```

## Setup Instuctions on VFs

IX is compatible with Virtual Functions on SR-IOV capable multiport network cards. The following instructions explain the process of deploying IX in your environment using VFs:

1. follow steps 1-3.

2. get your device PCI address then bring up one (or more) Virtual Function(s) on it:
   ```
   modprobe ixgbe
   PCI_DEVICE="$(basename "$(readlink "/sys/class/net/$IFACE/device")")"
   echo 1 > "/sys/bus/pci/devices/$PCI_DEVICE/sriov_numvfs"
   sudo ifconfig $VIRTUAL_IFACE up
   modprobe -r ixgbevf
   ```

   where `$IFACE` is the logical name assigned to your IX-compatible network device (e.g. `eth3`), and `$VIRTUAL_IFACE` is the logical name assigned to the VF. You can also get the PCI address of your device using `lspci | grep -i eth`. For a more detailed explanation, see the corresponding [wiki page](https://github.com/ix-project/ix/wiki/running-ix#virtual-functions). 

3. Try running IX on the newly created VF device. Make sure that the "Device" field in `ix.conf` matches your VF PCI address (try `lspci | grep -i eth` and look for the virtual adapter) :

   ```
   sudo ./dp/ix -- ./apps/echoserver 4
   ```

   Then, try from another host:
   ```
   echo 123 | nc -vv <IP> <PORT>
   ```
