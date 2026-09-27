#ifndef ZEROOS_PIC_H
#define ZEROOS_PIC_H

#include "types.h"

void pic_init(void);
void pic_mask_all(void);
/* Unmask a single legacy IRQ line (1..7, 14..15; the IRQ2 cascade stays
 * under pic_init's management). No-op outside the PIC-controller path is
 * the caller's decision — this helper only touches the mask register. */
void pic_unmask_irq(uint8_t irq);
void pic_send_eoi(uint8_t irq);

#endif
