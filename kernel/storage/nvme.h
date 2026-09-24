#ifndef ZEROOS_NVME_H
#define ZEROOS_NVME_H
#include "block.h"

/* NVMe 1.x PCIe controller driver. See docs/STORAGE.md "NVMe". */
int nvme_probe_all(void);
void nvme_shutdown_all(void);
int nvme_is_nvme_device(struct block_device *device);
int nvme_test_mask_irq(struct block_device *device, int masked);

#endif
