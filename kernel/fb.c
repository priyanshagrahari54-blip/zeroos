#include "fb.h"
#include "memory.h"
#include "vmm.h"
#include "sync.h"

extern void serial_write_public(const char *text);

/* Multiboot2 boot-info tag types (info stream, not the header). */
#define MB2_INFO_TAG_END 0U
#define MB2_INFO_TAG_FRAMEBUFFER 8U

/* FB lives at a dedicated slice of the shared supervisor MMIO window:
 * +0x000000 Local APIC, +0x1000..+0x10000 IOAPICs, +0x100000 VMM self-test
 * (transient), so +0x20000000 stays clear of every existing mapping. */
#define FB_MMIO_WINDOW_OFFSET 0x20000000ULL
#define FB_MAX_BYTES (256ULL * 1024ULL * 1024ULL)
/* Mirror of vmm.c PHYS_MASK (52-bit physical frames); kept local so fb.c
 * does not reach into VMM-private headers. */
#define FB_PHYS_MASK 0x000ffffffffff000ULL

struct mb2_info_tag {
    uint32_t type;
    uint32_t size;
};

/* Multiboot2 info framebuffer tag (type 8), fixed 32-byte payload. */
struct mb2_info_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t fb_type; /* 0 indexed, 1 RGB, 2 EGA text */
    uint16_t reserved;
    uint8_t red_field;
    uint8_t red_position;
    uint8_t green_field;
    uint8_t green_position;
    uint8_t blue_field;
    uint8_t blue_position;
    uint8_t reserved2;
};

#define MB2_FB_TYPE_RGB 1U
#define MB2_FB_TYPE_TEXT 2U
#define MB2_INFO_MIN_TOTAL 16U
#define MB2_INFO_MAX_TOTAL (16ULL * 1024ULL * 1024ULL)
#define MB2_TAG_MIN_SIZE 8U

static struct zeroos_display_info display_state;
static uint64_t fb_kernel_virtual;
static struct spinlock present_lock;

static void fb_write_u64(uint64_t value) {
    char buffer[21];
    int pos=20;
    buffer[pos]='\0';
    if (value==0) {
        serial_write_public("0");
        return;
    }
    while (value && pos>0) {
        buffer[--pos]=(char)('0'+(value%10ULL));
        value/=10ULL;
    }
    serial_write_public(&buffer[pos]);
}

static const char *format_name(uint32_t format) {
    switch (format) {
    case ZEROOS_DISPLAY_FORMAT_RGB565: return "rgb565";
    case ZEROOS_DISPLAY_FORMAT_RGB888: return "rgb888";
    case ZEROOS_DISPLAY_FORMAT_XRGB8888: return "xrgb8888";
    case ZEROOS_DISPLAY_FORMAT_EGA_TEXT: return "ega-text";
    default: return "indexed";
    }
}

static uint32_t format_for_depth(uint8_t bpp, uint8_t fb_type) {
    if (fb_type==MB2_FB_TYPE_TEXT)
        return ZEROOS_DISPLAY_FORMAT_EGA_TEXT;
    if (bpp==32U)
        return ZEROOS_DISPLAY_FORMAT_XRGB8888;
    if (bpp==24U)
        return ZEROOS_DISPLAY_FORMAT_RGB888;
    if (bpp==16U)
        return ZEROOS_DISPLAY_FORMAT_RGB565;
    if (fb_type==0U)
        return ZEROOS_DISPLAY_FORMAT_INDEXED;
    return ZEROOS_DISPLAY_FORMAT_INDEXED;
}

static void degrade(const char *reason) {
    serial_write_public("ZEROOS: framebuffer unavailable (");
    serial_write_public(reason);
    serial_write_public("); serial console remains the display path.\n");
    serial_write_public("ZEROOS: display primitive degraded (serial-only).\n");
}

static int fb_validate_geometry(const struct mb2_info_tag_framebuffer *tag,
                                struct zeroos_display_info *out) {
    uint64_t bytes;
    uint64_t minimum_pitch;

    if (!tag || !out || tag->size<32U)
        return -1;
    if (!tag->address || !tag->width || !tag->height || !tag->pitch)
        return -1;
    if (tag->address & (ZEROOS_PAGE_SIZE-1ULL))
        return -1; /* unaligned LFB cannot be mapped page-wise yet */
    if (tag->fb_type!=MB2_FB_TYPE_RGB)
        return -1; /* indexed/text scanout has no linear RGB consumer */
    if (tag->bpp!=16U && tag->bpp!=24U && tag->bpp!=32U)
        return -1;
    minimum_pitch=((uint64_t)tag->width*(uint64_t)tag->bpp + 7ULL)/8ULL;
    if (tag->pitch<minimum_pitch)
        return -1;
    bytes=(uint64_t)tag->pitch*tag->height;
    if (!bytes || bytes>FB_MAX_BYTES)
        return -1;
    if ((tag->address & ~FB_PHYS_MASK)!=0)
        return -1;

    out->physical_address=tag->address;
    out->byte_size=bytes;
    out->width=tag->width;
    out->height=tag->height;
    out->pitch=tag->pitch;
    out->bpp=tag->bpp;
    out->format=format_for_depth(tag->bpp,tag->fb_type);
    out->flags=ZEROOS_DISPLAY_FLAG_PRESENT;
    return 0;
}

static int fb_map_and_verify(struct zeroos_display_info *info) {
    uint64_t page_count=(info->byte_size+ZEROOS_PAGE_SIZE-1ULL)/ZEROOS_PAGE_SIZE;
    uint64_t virtual_base=VMM_MMIO_BASE+FB_MMIO_WINDOW_OFFSET;
    uint64_t mapped=0;
    uint8_t probe_saved;
    volatile uint8_t *probe;

    for (; mapped<page_count; ++mapped) {
        if (vmm_map_mmio_page(virtual_base+mapped*ZEROOS_PAGE_SIZE,
                              info->physical_address+mapped*ZEROOS_PAGE_SIZE,
                              VMM_WRITABLE|VMM_CACHE_DISABLE|
                              VMM_NO_EXECUTE)!=0)
            break;
    }
    if (mapped!=page_count) {
        while (mapped) {
            --mapped;
            (void)vmm_unmap_mmio_page(virtual_base+mapped*ZEROOS_PAGE_SIZE);
        }
        return -1;
    }
    if (vmm_translate(virtual_base)!=info->physical_address) {
        for (mapped=0; mapped<page_count; ++mapped)
            (void)vmm_unmap_mmio_page(virtual_base+mapped*ZEROOS_PAGE_SIZE);
        return -1;
    }

    /* Non-destructive readback: capture byte 0, rewrite it, compare. A
     * device that cannot round-trip through the UC mapping is unfit for a
     * software scanout consumer. */
    probe=(volatile uint8_t *)virtual_base;
    probe_saved=probe[0];
    probe[0]=probe_saved;
    for (uint64_t flush=0; flush<ZEROOS_PAGE_SIZE; flush+=64ULL)
        (void)*(volatile uint8_t *)(virtual_base+flush);
    if (probe[0]!=probe_saved) {
        for (mapped=0; mapped<page_count; ++mapped)
            (void)vmm_unmap_mmio_page(virtual_base+mapped*ZEROOS_PAGE_SIZE);
        return -1;
    }

    fb_kernel_virtual=virtual_base;
    return 0;
}

int fb_init(uint64_t multiboot_info) {
    const struct mb2_info_tag *tag;
    const struct mb2_info_tag *end;
    uint32_t total_size;

    display_state.flags=0;
    fb_kernel_virtual=0;
    spinlock_init(&present_lock);

    if (!multiboot_info) {
        degrade("no boot info");
        return -1;
    }
    total_size=*(const uint32_t *)(uint64_t)multiboot_info;
    if (total_size<MB2_INFO_MIN_TOTAL || total_size>MB2_INFO_MAX_TOTAL) {
        degrade("boot info size invalid");
        return -1;
    }

    tag=(const struct mb2_info_tag *)(uint64_t)(multiboot_info+8ULL);
    end=(const struct mb2_info_tag *)(uint64_t)(multiboot_info+total_size);
    while (tag<end && tag->type!=MB2_INFO_TAG_END &&
           tag->size>=MB2_TAG_MIN_SIZE) {
        if (tag->type==MB2_INFO_TAG_FRAMEBUFFER) {
            struct zeroos_display_info candidate={0};
            if (fb_validate_geometry((const struct
                                      mb2_info_tag_framebuffer *)tag,
                                      &candidate)==0) {
                /* Keep VRAM out of the page allocator when it overlaps the
                 * managed window; a no-op for typical high PCI LFBs. */
                memory_reserve_physical(candidate.physical_address,
                                        candidate.byte_size);
                if (fb_map_and_verify(&candidate)!=0) {
                    degrade("MMIO map/readback failed");
                    return -1;
                }
                display_state=candidate;
                serial_write_public("ZEROOS: framebuffer acquired: ");
                fb_write_u64(display_state.width);
                serial_write_public("x");
                fb_write_u64(display_state.height);
                serial_write_public(" pitch=");
                fb_write_u64(display_state.pitch);
                serial_write_public(" bpp=");
                fb_write_u64(display_state.bpp);
                serial_write_public(" format=");
                serial_write_public(format_name(display_state.format));
                serial_write_public(" size=");
                fb_write_u64(display_state.byte_size);
                serial_write_public(" bytes.\n");
                serial_write_public("ZEROOS: display primitive ready.\n");
                return 0;
            }
        }
        {
            uint64_t step=(tag->size+7ULL)&~7ULL;
            if (step<8ULL)
                break;
            tag=(const struct mb2_info_tag *)((const uint8_t *)tag+step);
        }
    }
    degrade("no linear framebuffer tag");
    return -1;
}

const struct zeroos_display_info *fb_display_info(void) {
    return &display_state;
}

/* ---- Pixel-mapping primitives (DISPLAY_PRESENT scanout path) ---- */

int fb_present_active(void) {
    return (display_state.flags & ZEROOS_DISPLAY_FLAG_PRESENT) &&
           fb_kernel_virtual!=0;
}

static uint32_t fb_bytes_per_pixel(void) {
    return (display_state.bpp+7U)/8U;
}

int fb_write_pixels(uint32_t x, uint32_t y, uint32_t count,
                    const uint8_t *source) {
    uint32_t bytes_pp;
    uint64_t offset;
    volatile uint8_t *destination;
    uint64_t length;
    uint64_t flags;

    if (!source || !count || !fb_present_active())
        return -1;
    if (x>=display_state.width || y>=display_state.height ||
        count>display_state.width-x)
        return -1;
    bytes_pp=fb_bytes_per_pixel();
    offset=(uint64_t)y*display_state.pitch+(uint64_t)x*bytes_pp;
    if (offset+(uint64_t)count*bytes_pp>display_state.byte_size)
        return -1;
    destination=(volatile uint8_t *)(fb_kernel_virtual+offset);
    length=(uint64_t)count*bytes_pp;

    flags=spin_lock_irqsave(&present_lock);
    for (uint64_t i=0; i<length; ++i)
        destination[i]=source[i];
    spin_unlock_irqrestore(&present_lock,flags);
    return 0;
}

/* Native scanout byte order -> XRGB8888 for verification and feedback. */
static uint32_t fb_native_to_xrgb(const uint8_t *source) {
    uint32_t pixel;
    if (display_state.bpp==32U) {
        pixel=(uint32_t)source[0] | ((uint32_t)source[1]<<8) |
              ((uint32_t)source[2]<<16) | ((uint32_t)source[3]<<24);
        return 0xFF000000U | (pixel & 0x00FFFFFFU);
    }
    if (display_state.bpp==24U) {
        return 0xFF000000U | ((uint32_t)source[2]<<16) |
               ((uint32_t)source[1]<<8) | (uint32_t)source[0];
    }
    /* RGB565, little-endian. */
    pixel=(uint32_t)source[0] | ((uint32_t)source[1]<<8);
    {
        uint32_t r5=(pixel>>11)&0x1FU;
        uint32_t g6=(pixel>>5)&0x3FU;
        uint32_t b5=pixel&0x1FU;
        uint32_t r=(r5<<3)|(r5>>2);
        uint32_t g=(g6<<2)|(g6>>4);
        uint32_t b=(b5<<3)|(b5>>2);
        return 0xFF000000U | (r<<16) | (g<<8) | b;
    }
}

/* XRGB8888 -> native scanout byte order. */
static void fb_xrgb_to_native(uint32_t xrgb, uint8_t *destination) {
    uint32_t r=(xrgb>>16)&0xFFU;
    uint32_t g=(xrgb>>8)&0xFFU;
    uint32_t b=xrgb&0xFFU;
    if (display_state.bpp==32U) {
        destination[0]=(uint8_t)b;
        destination[1]=(uint8_t)g;
        destination[2]=(uint8_t)r;
        destination[3]=0xFFU;
        return;
    }
    if (display_state.bpp==24U) {
        destination[0]=(uint8_t)b;
        destination[1]=(uint8_t)g;
        destination[2]=(uint8_t)r;
        return;
    }
    {
        uint16_t packed=(uint16_t)(((r>>3)<<11) | ((g>>2)<<5) | (b>>3));
        destination[0]=(uint8_t)(packed & 0xFFU);
        destination[1]=(uint8_t)(packed>>8);
    }
}

int fb_read_pixel(uint32_t x, uint32_t y, uint32_t *xrgb_out) {
    uint32_t bytes_pp;
    uint64_t offset;
    const volatile uint8_t *source;
    uint8_t native[4];
    uint64_t flags;

    if (!xrgb_out || !fb_present_active())
        return -1;
    if (x>=display_state.width || y>=display_state.height)
        return -1;
    bytes_pp=fb_bytes_per_pixel();
    if (bytes_pp>4U)
        return -1;
    offset=(uint64_t)y*display_state.pitch+(uint64_t)x*bytes_pp;
    if (offset+bytes_pp>display_state.byte_size)
        return -1;
    source=(const volatile uint8_t *)(fb_kernel_virtual+offset);

    flags=spin_lock_irqsave(&present_lock);
    for (uint32_t i=0; i<bytes_pp; ++i)
        native[i]=source[i];
    spin_unlock_irqrestore(&present_lock,flags);
    *xrgb_out=fb_native_to_xrgb(native);
    return 0;
}

/* Deterministic probe pattern: opaque, non-uniform across both axes so a
 * swapped row/column or stale buffer cannot pass verification. */
uint32_t fb_probe_pixel(uint32_t x, uint32_t y) {
    return 0xFF000000U | ((x*1973U + y*9277U + 0x5A3C17U) & 0x00FFFFFFU);
}

int fb_fill_probe_native(uint8_t *destination, uint64_t capacity) {
    uint32_t bytes_pp;
    uint64_t stride;

    if (!destination || !fb_present_active())
        return -1;
    bytes_pp=fb_bytes_per_pixel();
    if (!bytes_pp || bytes_pp>4U)
        return -1;
    stride=(uint64_t)FB_PRESENT_PROBE_W*bytes_pp;
    if (capacity<stride*FB_PRESENT_PROBE_H)
        return -1;
    for (uint32_t row=0; row<FB_PRESENT_PROBE_H; ++row) {
        for (uint32_t column=0; column<FB_PRESENT_PROBE_W; ++column) {
            fb_xrgb_to_native(fb_probe_pixel(column,row),
                              destination+row*stride+column*bytes_pp);
        }
    }
    return 0;
}

int fb_probe_verify(void) {
    if (!fb_present_active())
        return -1;
    for (uint32_t row=0; row<FB_PRESENT_PROBE_H; ++row) {
        for (uint32_t column=0; column<FB_PRESENT_PROBE_W; ++column) {
            uint32_t pixel=0;
            if (fb_read_pixel(column,row,&pixel)!=0)
                return -1;
            if (pixel!=fb_probe_pixel(column,row))
                return -1;
        }
    }
    return 0;
}
