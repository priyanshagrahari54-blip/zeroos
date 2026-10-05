/* ZEROOS Android Compatibility Core (Stage 8 / AOSP 14 API 34 Architecture)
 *
 * Provides a demand-loaded, isolated Android runtime architecture:
 *  - Demand-loaded: dormant when unused (0 bytes resident); never permanently
 *    resident on a constrained 2 GB G560 profile.
 *  - Isolated: sandboxed app environment with isolated storage, UID/GID mapping,
 *    and strict path boundary checks.
 *  - Permissions: explicit runtime permission matrix (Camera, Audio, Location,
 *    Media, Notifications, Internet) with prompt and revocation support.
 *  - Package lifecycle: APK manifest validation, install, start, pause, stop,
 *    suspend, discard, uninstall.
 *  - Bridges: Graphics (retained surface/damage), Audio (PCM stream), Input
 *    (MotionEvent/KeyEvent), Storage (isolated mapping), Notification (ZEROOS
 *    notification center integration).
 *  - Fault isolation: app or ART crash never takes down the ZEROOS desktop.
 */
#ifndef ZEROOS_COMPAT_ANDROID_H
#define ZEROOS_COMPAT_ANDROID_H

#include <stdint.h>

#define ZANDROID_OK 0
#define ZANDROID_ERR (-1)
#define ZANDROID_BADARG (-2)
#define ZANDROID_NOSPACE (-3)
#define ZANDROID_NOTFOUND (-4)
#define ZANDROID_BADSTATE (-5)
#define ZANDROID_UNSUPPORTED (-6)
#define ZANDROID_PERMISSION_DENIED (-7)

#define ZANDROID_MAX_APPS 16
#define ZANDROID_MAX_NOTIFICATIONS 32
#define ZANDROID_PKG_NAME_MAX 64
#define ZANDROID_PATH_MAX 128
#define ZANDROID_API_LEVEL_TARGET 34  /* Android 14 */

/* Runtime permissions bitmask */
#define ZANDROID_PERM_INTERNET              (1u << 0)
#define ZANDROID_PERM_CAMERA                (1u << 1)
#define ZANDROID_PERM_RECORD_AUDIO          (1u << 2)
#define ZANDROID_PERM_ACCESS_FINE_LOCATION  (1u << 3)
#define ZANDROID_PERM_READ_MEDIA_IMAGES     (1u << 4)
#define ZANDROID_PERM_POST_NOTIFICATIONS    (1u << 5)

enum zandroid_runtime_state {
    ZANDROID_RT_DORMANT = 0,   /* not loaded, 0 bytes resident */
    ZANDROID_RT_WARM,          /* loaded in memory, no active apps */
    ZANDROID_RT_ACTIVE,        /* active app executing */
    ZANDROID_RT_SUSPENDED      /* under memory pressure, frozen */
};

enum zandroid_app_state {
    ZANDROID_APP_NONE = 0,
    ZANDROID_APP_INSTALLED,
    ZANDROID_APP_STARTING,
    ZANDROID_APP_RUNNING,
    ZANDROID_APP_PAUSED,
    ZANDROID_APP_STOPPED
};

struct zandroid_apk_info {
    char package_name[ZANDROID_PKG_NAME_MAX];
    uint32_t min_sdk;
    uint32_t target_sdk;
    uint32_t version_code;
    uint32_t requested_perms;
    uint32_t granted_perms;
    uint32_t apk_size_bytes;
};

struct zandroid_app {
    struct zandroid_apk_info apk;
    enum zandroid_app_state state;
    uint32_t uid;
    uint32_t resident_bytes;
    uint32_t crashes;
    uint8_t used;
};

/* Bridge structures */
struct zandroid_gfx_frame {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t damage_x;
    uint32_t damage_y;
    uint32_t damage_w;
    uint32_t damage_h;
    uint64_t frame_seq;
};

enum zandroid_motion_action {
    ZANDROID_MOTION_DOWN = 0,
    ZANDROID_MOTION_UP,
    ZANDROID_MOTION_MOVE,
    ZANDROID_MOTION_CANCEL
};

struct zandroid_motion_event {
    enum zandroid_motion_action action;
    int32_t x;
    int32_t y;
    uint32_t pointer_id;
    uint32_t timestamp_ms;
};

struct zandroid_notification {
    char package_name[ZANDROID_PKG_NAME_MAX];
    uint32_t notification_id;
    char title[64];
    char text[128];
    uint32_t priority;
    uint8_t active;
};

struct zandroid_stats {
    uint32_t apps_installed;
    uint32_t apps_started;
    uint32_t apps_stopped;
    uint32_t apps_crashed;
    uint32_t runtime_wakeups;
    uint32_t frames_rendered;
    uint32_t audio_chunks_played;
    uint32_t notifications_posted;
    uint32_t permissions_rejected;
    uint32_t path_traversals_blocked;
};

struct zandroid_runtime {
    enum zandroid_runtime_state state;
    uint32_t total_resident_bytes;
    struct zandroid_app apps[ZANDROID_MAX_APPS];
    struct zandroid_notification notifications[ZANDROID_MAX_NOTIFICATIONS];
    struct zandroid_stats stats;
    char storage_root[ZANDROID_PATH_MAX];
};

void zandroid_init(struct zandroid_runtime *rt, const char *storage_root);

/* Runtime management: demand loading & memory pressure */
int zandroid_runtime_wake(struct zandroid_runtime *rt);
int zandroid_runtime_suspend(struct zandroid_runtime *rt);
int zandroid_runtime_stop(struct zandroid_runtime *rt);

/* Package / App lifecycle */
int zandroid_install_apk(struct zandroid_runtime *rt, const struct zandroid_apk_info *apk);
int zandroid_uninstall_app(struct zandroid_runtime *rt, const char *package_name);
int zandroid_start_app(struct zandroid_runtime *rt, const char *package_name, uint32_t initial_heap_bytes);
int zandroid_pause_app(struct zandroid_runtime *rt, const char *package_name);
int zandroid_stop_app(struct zandroid_runtime *rt, const char *package_name);
int zandroid_crash_app(struct zandroid_runtime *rt, const char *package_name);
int zandroid_get_app_state(const struct zandroid_runtime *rt, const char *package_name, enum zandroid_app_state *out_state);

/* Permission Management */
int zandroid_grant_permission(struct zandroid_runtime *rt, const char *package_name, uint32_t perm_bit);
int zandroid_revoke_permission(struct zandroid_runtime *rt, const char *package_name, uint32_t perm_bit);
int zandroid_check_permission(const struct zandroid_runtime *rt, const char *package_name, uint32_t perm_bit);

/* Path translation & boundary enforcement */
int zandroid_translate_path(struct zandroid_runtime *rt, const char *package_name,
                            const char *guest_path, char *out_path, uint32_t out_cap);

/* Bridges */
int zandroid_bridge_submit_frame(struct zandroid_runtime *rt, const char *package_name,
                                 const struct zandroid_gfx_frame *frame);
int zandroid_bridge_post_notification(struct zandroid_runtime *rt, const char *package_name,
                                      uint32_t notif_id, const char *title, const char *text, uint32_t priority);
int zandroid_bridge_cancel_notification(struct zandroid_runtime *rt, const char *package_name, uint32_t notif_id);
int zandroid_bridge_inject_motion(struct zandroid_runtime *rt, const char *package_name,
                                  const struct zandroid_motion_event *motion);

#endif /* ZEROOS_COMPAT_ANDROID_H */
