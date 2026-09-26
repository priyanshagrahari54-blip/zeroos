/* Gaming core (Stage 5 part I): per-app profiles, FPS overlay,
 * controller remapping, low-latency flag and a cooperative resource
 * policy that yields under system pressure.  Measurements come from
 * the fps ring — this module only decides, it never samples. */
#ifndef ZEROOS_DESKTOP_GAMING_H
#define ZEROOS_DESKTOP_GAMING_H

#include <stdint.h>

#define ZD_GAME_PROFILES 8
#define ZD_GAME_APP 24
#define ZD_GAME_BTNS 32
#define ZD_GAME_ACTIONS 16

enum zd_game_mode {
    ZD_GAME_ECO = 0,
    ZD_GAME_BALANCED = 1,
    ZD_GAME_PERF = 2
};

/* cooperative policy outcomes */
enum zd_game_yield {
    ZD_GAME_YIELD_NONE = 0,
    ZD_GAME_YIELD_OVERLAY_OFF,     /* drop overlay under pressure */
    ZD_GAME_YIELD_TARGET_FLOOR,    /* target below floor while busy */
    ZD_GAME_YIELD_DEGRADE          /* both of the above */
};

#define ZD_GAME_FLAG_OVERLAY     (1u << 0)
#define ZD_GAME_FLAG_LOW_LATENCY (1u << 1)

struct zd_game_profile {
    char app[ZD_GAME_APP];
    uint32_t in_use;
    uint8_t mode;                 /* enum zd_game_mode */
    uint16_t target_fps;          /* 1..240 */
    uint32_t flags;
    uint8_t btn_map[ZD_GAME_BTNS];/* physical -> logical action */
    uint32_t overlay_frames;      /* frames shown (measured use) */
};

struct zd_gaming {
    struct zd_game_profile profiles[ZD_GAME_PROFILES];
    uint32_t count;
    struct {
        uint32_t sets, remaps, yields, rejected, unknown_buttons;
    } stats;
};

void zd_gaming_init(struct zd_gaming *g);
/* Create/update a profile.  Empty/overlong app -> -22 +rejected;
 * mode>PERF or target 0 or >240 or bad flags -> -22; full -> -28. */
int zd_gaming_profile_set(struct zd_gaming *g, const char *app,
                          uint32_t mode, uint32_t target_fps,
                          uint32_t flags);
int zd_gaming_profile_remove(struct zd_gaming *g, const char *app);
struct zd_game_profile *zd_gaming_profile(struct zd_gaming *g,
                                          const char *app);
/* Map physical button (0..31) to logical action (0..15).
 * -2 unknown profile, -22 range violations +rejected. */
int zd_gaming_remap(struct zd_gaming *g, const char *app,
                    uint32_t physical, uint32_t action);
/* -2 unknown profile / unmapped button (+unknown_buttons). */
int zd_gaming_lookup(struct zd_gaming *g, const char *app,
                     uint32_t physical);
/* Cooperative decision from measured inputs (fps in milli-units,
 * pressure 0..100, 0 fps = unknown), highest severity wins:
 *   DEGRADE      = pressure >= 80 AND measured fps < 30;
 *   OVERLAY_OFF  = overlay active AND (pressure >= 80 OR fps < 30);
 *   TARGET_FLOOR = pressure >= 80 (hold target at the 30 fps floor);
 *   NONE         = otherwise.
 * Returns enum zd_game_yield (>= 0) or -2 unknown profile. */
int zd_gaming_cooperative(struct zd_gaming *g, const char *app,
                          uint32_t fps_milli, uint32_t pressure);

#endif /* ZEROOS_DESKTOP_GAMING_H */
