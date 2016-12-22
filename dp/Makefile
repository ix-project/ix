# Copyright 2013-16 Board of Trustees of Stanford University
# Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# A Makefile for IX.
#
# path for this Makefile to work properly.

DPDK	= ../deps/dpdk
DUNE	= ../deps/dune
PCIDMA	= ../deps/pcidma
DPDK_INC = -I$(DPDK)/build/include -I$(DPDK)/lib/librte_eal/common -I$(DPDK)/drivers/net/ixgbe -I$(DPDK)/drivers/net/i40e
INC	= -I../inc -I$(DUNE)/libdune -I../inc/lwip -I../inc/lwip/ipv4 -I../inc/lwip/ipv6 $(DPDK_INC) -include$(DUNE)/kern/dune.h
INC	+= -I$(PCIDMA)
CC	= gcc
CFLAGS	= -g -Wall -fno-pie -fno-dwarf2-cfi-asm -fno-asynchronous-unwind-tables -O3 -mno-red-zone $(INC) -D__KERNEL__ $(EXTRA_CFLAGS)
LD	= gcc
LDFLAGS	= -T ix.ld -no-pie
LDLIBS	= -lrt -lpthread -lm -lnuma -ldl -lconfig

ifneq ($(DEBUG),)
CFLAGS += -DDEBUG
endif

ifneq ($(ENABLE_KSTATS),)
CFLAGS += -DENABLE_KSTATS
endif

SRCS =
DIRS = core drivers lwip net sandbox

define register_dir
SRCS += $(patsubst %, $(1)/%, $(2))
endef

include $(patsubst %, %/dir.mk, $(DIRS))

%.generated.S: %.c
	$(CC) $(CFLAGS) -o $@ -S $<

all: ix

OBJS=$(subst .c,.o,$(SRCS))

DUNE_LIB=$(DUNE)/libdune/libdune.a

depend: .depend

.depend: $(SRCS)
	bash -c "cat $(foreach SRC,$(SRCS),<($(CC) $(CFLAGS) -MM -MT $(SRC:.c=.o) $(SRC))) > ./.depend"

ifneq ($(MAKECMDGOALS),clean)
-include .depend
endif

DPDK_LIBS=
DPDK_LIBS+=-Wl,-whole-archive $(DPDK)/build/lib/librte_pmd_ixgbe.a -Wl,-no-whole-archive
DPDK_LIBS+=-Wl,-whole-archive $(DPDK)/build/lib/librte_pmd_i40e.a -Wl,-no-whole-archive
DPDK_LIBS+=$(DPDK)/build/lib/libethdev.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_mbuf.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_eal.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_mempool.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_ring.a

ix: $(DEPENDENCY) $(OBJS) $(DUNE_LIB) ix.ld
	$(LD) $(LDFLAGS) -o ix $(OBJS) $(DUNE_LIB) $(DPDK_LIBS) $(LDLIBS)

clean:
	rm -f $(OBJS) ix .depend

dist-clean: clean
	rm *~

