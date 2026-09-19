#ifndef ZEROOS_PIC_H
#define ZEROOS_PIC_H

#include "types.h"

void pic_init(void);
void pic_send_eoi(uint8_t irq);

#endif
