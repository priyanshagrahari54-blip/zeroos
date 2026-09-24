#include "fb.h"
#include "memory.h"
#include "vmm.h"

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
