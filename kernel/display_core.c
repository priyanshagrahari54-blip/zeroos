#include "display_core.h"
#include "syscall.h"

/* Largest accepted source row pitch: bounds every (height-1)*stride term
 * well below 2^32 even for the tallest validated modes. */
#define DISPLAY_PRESENT_MAX_STRIDE (1024U*1024U)
int display_mode_valid(const struct display_mode *m,uint64_t limit){if(!m||!m->width||!m->height||!m->refresh_millihz||m->width>16384||m->height>16384)return 0;uint32_t bpp=m->pixel_format==1?4U:(m->pixel_format==2?2U:0U);if(!bpp)return 0;uint64_t pixels=(uint64_t)m->width*m->height;if(pixels>limit/bpp)return 0;return 1;}
int display_caps_add(struct display_caps *c,const struct display_mode *m,uint64_t limit){if(!c||!display_mode_valid(m,limit)||c->count>=DISPLAY_MAX_MODES)return -1;for(uint32_t i=0;i<c->count;i++)if(c->modes[i].width==m->width&&c->modes[i].height==m->height&&c->modes[i].refresh_millihz==m->refresh_millihz&&c->modes[i].pixel_format==m->pixel_format)return -1;c->modes[c->count++]=*m;return 0;}

int display_present_request_valid(uint32_t fb_width,uint32_t fb_height,
                                  uint32_t bpp,uint32_t format,
                                  uint32_t x,uint32_t y,
                                  uint32_t width,uint32_t height,
                                  uint32_t stride_bytes) {
    uint32_t expected_format;
    uint32_t bytes_pp;
    uint64_t last_row;

    if (!fb_width || !fb_height)
        return 0;
    if (bpp==32U)
        expected_format=ZEROOS_DISPLAY_FORMAT_XRGB8888;
    else if (bpp==24U)
        expected_format=ZEROOS_DISPLAY_FORMAT_RGB888;
    else if (bpp==16U)
        expected_format=ZEROOS_DISPLAY_FORMAT_RGB565;
    else
        return 0;
    if (format!=expected_format)
        return 0;
    if (!width || !height)
        return 0;
    if (x>=fb_width || width>fb_width-x)
        return 0;
    if (y>=fb_height || height>fb_height-y)
        return 0;
    bytes_pp=bpp/8U;
    if (stride_bytes>DISPLAY_PRESENT_MAX_STRIDE)
        return 0;
    if (stride_bytes<(uint64_t)width*bytes_pp)
        return 0;
    /* Overflow guard: height>=1 here, so the sum below is the exact byte
     * length of the last source row and must stay inside u64 trivially;
     * the stride cap keeps it bounded for any caller-supplied height. */
    last_row=(uint64_t)(height-1U)*stride_bytes+(uint64_t)width*bytes_pp;
    if (last_row>(1ULL<<32))
        return 0;
    return 1;
}
