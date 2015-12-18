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
 * pci.h - PCI bus support
 */

#pragma once

#include <ix/types.h>

struct pci_bar {
	uint64_t start;	/* the start address, or zero if no resource */
	uint64_t len;	/* the length of the resource */
	uint64_t flags; /* Linux resource flags */
};

/* NOTE: these are the same as the Linux PCI sysfs resource flags */
#define PCI_BAR_IO		0x00000100
#define PCI_BAR_MEM		0x00000200
#define PCI_BAR_PREFETCH	0x00002000 /* typically WC memory */
#define PCI_BAR_READONLY	0x00004000 /* typically option ROMs */

#define PCI_MAX_BARS 7

struct pci_addr {
	uint16_t domain;
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
};

extern int pci_str_to_addr(const char *str, struct pci_addr *addr);

struct pci_dev {
	struct pci_addr addr;

	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_device_id;

	struct pci_bar bars[PCI_MAX_BARS];
	int numa_node;
	int max_vfs;
};

extern struct pci_dev *pci_alloc_dev(const struct pci_addr *addr);
extern struct pci_bar *pci_find_mem_bar(struct pci_dev *dev, int count);
extern void *pci_map_mem_bar(struct pci_dev *dev, struct pci_bar *bar, bool wc);
extern void pci_unmap_mem_bar(struct pci_bar *bar, void *vaddr);
extern int pci_enable_device(struct pci_dev *dev);
extern int pci_set_master(struct pci_dev *dev);
