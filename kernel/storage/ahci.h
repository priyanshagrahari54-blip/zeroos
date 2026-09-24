#ifndef ZEROOS_AHCI_H
#define ZEROOS_AHCI_H
#include "block.h"

/* AHCI 1.3 SATA driver. See docs/STORAGE.md "AHCI". */
int ahci_probe_all(void);
/* Test hooks: mask/unmask a port's interrupt enables (lost-IRQ recovery
 * test) and trigger a port reset through the block reset path. */
int ahci_test_mask_irq(struct block_device *device, int masked);
int ahci_is_ahci_device(struct block_device *device);

#endif
