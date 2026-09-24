#ifndef ZEROOS_DISPLAY_CORE_H
#define ZEROOS_DISPLAY_CORE_H
#include "types.h"
#define DISPLAY_MAX_MODES 32
struct display_mode { uint32_t width,height,refresh_millihz,pixel_format; };
struct display_caps { struct display_mode modes[DISPLAY_MAX_MODES]; uint32_t count; uint8_t connected; };
int display_mode_valid(const struct display_mode *m,uint64_t max_frame_bytes);
int display_caps_add(struct display_caps *c,const struct display_mode *m,uint64_t max_frame_bytes);
#endif
