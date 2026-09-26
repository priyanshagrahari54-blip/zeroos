#ifndef ZEROOS_DISPLAY_H
#define ZEROOS_DISPLAY_H

#include "types.h"
#include "sync.h"

#define ZEROOS_DISPLAY_MAX_DEVICES 8U
#define ZEROOS_DISPLAY_MAX_MODES 32U
#define ZEROOS_DISPLAY_MAX_NAME 32U

enum zeroos_display_state {
    ZEROOS_DISPLAY_STOPPED = 0,
    ZEROOS_DISPLAY_DORMANT,
    ZEROOS_DISPLAY_WARM,
    ZEROOS_DISPLAY_ACTIVE,
    ZEROOS_DISPLAY_THROTTLED,
    ZEROOS_DISPLAY_SUSPENDED
};

enum zeroos_display_type {
    ZEROOS_DISPLAY_TYPE_UNKNOWN = 0,
    ZEROOS_DISPLAY_TYPE_FRAMEBUFFER,
    ZEROOS_DISPLAY_TYPE_GPU
};

struct zeroos_display_mode {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_hz;
    uint32_t bpp;
    uint64_t pitch;
    uint8_t valid;
};

struct zeroos_display_device {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_display_state state;
    enum zeroos_display_type type;
    char name[ZEROOS_DISPLAY_MAX_NAME];
    uint64_t framebuffer_phys;
    uint64_t framebuffer_virt;
    uint64_t framebuffer_size;
    uint32_t current_mode_index;
    struct zeroos_display_mode modes[ZEROOS_DISPLAY_MAX_MODES];
    uint32_t mode_count;
    uint8_t connected;
    uint8_t enabled;
    struct spinlock lock;
    uint64_t gpu_memory_budget;
    uint64_t vsync_count;
};

int display_system_init(void);
int display_device_register(enum zeroos_display_type type, const char *name,
                            uint64_t fb_phys, uint64_t fb_size,
                            uint64_t *device_id_out);
int display_device_add_mode(uint64_t device_id, uint32_t width, uint32_t height,
                            uint32_t refresh, uint32_t bpp, uint64_t pitch);
int display_device_set_mode(uint64_t device_id, uint32_t mode_index);
int display_device_set_state(uint64_t device_id, enum zeroos_display_state state);
struct zeroos_display_device *display_device_lookup(uint64_t device_id);
int display_debug_validate(void);

#endif
