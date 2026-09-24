#ifndef ZEROOS_DISPLAY_CORE_H
#define ZEROOS_DISPLAY_CORE_H
#include "types.h"
#define DISPLAY_MAX_MODES 32
struct display_mode { uint32_t width,height,refresh_millihz,pixel_format; };
struct display_caps { struct display_mode modes[DISPLAY_MAX_MODES]; uint32_t count; uint8_t connected; };
int display_mode_valid(const struct display_mode *m,uint64_t max_frame_bytes);
int display_caps_add(struct display_caps *c,const struct display_mode *m,uint64_t max_frame_bytes);

/* Pixel-mapping present-request validation (Stage 5A scanout path).
 *
 * Single overflow-safe implementation of the DISPLAY_PRESENT bounds rules,
 * shared by the syscall handler and the host test so the arithmetic under
 * the scanout write can never drift between kernel and test. format must be
 * the ZEROOS_DISPLAY_FORMAT_* matching bpp; stride is the caller's source
 * buffer row pitch in bytes. Returns 1 when the request is safe to execute,
 * 0 otherwise (caller maps 0 to -ZEROOS_EINVAL). */
int display_present_request_valid(uint32_t fb_width,uint32_t fb_height,
                                  uint32_t bpp,uint32_t format,
                                  uint32_t x,uint32_t y,
                                  uint32_t width,uint32_t height,
                                  uint32_t stride_bytes);
#endif
