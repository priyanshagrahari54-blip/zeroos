#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

#define FB_WIDTH 640
#define FB_HEIGHT 480

static struct zd_wm wm;
static struct zd_monitor monitor0;
static struct zd_compositor compositor;
static uint32_t framebuffer[FB_WIDTH * FB_HEIGHT];
static uint32_t app_pixels[256 * 256];
static zd_window_id window_id;
static zd_client_id client_id;
static int present_listener_calls;
static uint64_t present_listener_frame;

/* Simple solid-color pixel source keyed off the window. */
static const uint32_t *source_pixels(void *context, zd_window_id id,
                                     int32_t *width, int32_t *height,
                                     int32_t *stride_px) {
    (void)context;
    if (id != window_id)
        return (const uint32_t *)0;
    *width = 64;
    *height = 64;
    *stride_px = 64;
    return app_pixels;
}

static void on_present(void *context, uint64_t frame,
                       struct zd_rect damaged_area) {
    (void)context;
    (void)damaged_area;
    ++present_listener_calls;
    present_listener_frame = frame;
}

static void setup(void) {
    struct zd_window_create_info info;
    struct zd_frame_policy policy;
    uint32_t index;

    memset(framebuffer, 0xAA, sizeof(framebuffer));
    for (index = 0; index < 64U * 64U; ++index)
        app_pixels[index] = 0xFF3366CCU;

    zd_wm_init(&wm);
    memset(&monitor0, 0, sizeof(monitor0));
    monitor0.id = 1;
    monitor0.bounds.w = FB_WIDTH;
    monitor0.bounds.h = FB_HEIGHT;
    monitor0.scale_percent = 100;
    monitor0.primary = 1;
    monitor0.enabled = 1;
    strcpy(monitor0.name, "VGA-1");
    ZD_CHECK_OK(zd_wm_add_monitor(&wm, &monitor0));
    client_id = zd_wm_register_client(&wm);

    memset(&info, 0, sizeof(info));
    info.client = client_id;
    info.title = "compositor test";
    info.logical_rect.x = 40;
    info.logical_rect.y = 40;
    info.logical_rect.w = 64;
    info.logical_rect.h = 64;
    info.monitor_id = 1;
    info.resizable = 1;
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &window_id));

    memset(&policy, 0, sizeof(policy));
    policy.refresh_mhz = 60000;
    zd_compositor_init(&compositor, &wm, &policy);
    ZD_CHECK_OK(zd_compositor_set_pixel_source(&compositor, source_pixels,
                                               0));
    present_listener_calls = 0;
    ZD_CHECK_OK(zd_compositor_add_listener(&compositor, on_present, 0));
}

static uint64_t area_touched_as(const uint32_t *pixels, uint32_t sentinel,
                                uint64_t *out_area) {
    uint64_t touched = 0;
    uint32_t index;
    uint64_t area = 0;
    for (index = 0; index < FB_WIDTH * FB_HEIGHT; ++index) {
        ++area;
        if (pixels[index] != sentinel)
            ++touched;
    }
    if (out_area)
        *out_area = area;
    return touched;
}

static void test_scene_sync_and_damage(void) {
    struct zd_rect visible[16];
    setup();
    ZD_CHECK_EQ(compositor.node_count, 0U);
    zd_compositor_sync_scene(&compositor);
    ZD_CHECK_EQ(compositor.node_count, 1U);
    ZD_CHECK_EQ(compositor.nodes[0].id, window_id);
    ZD_CHECK_EQ(compositor.nodes[0].physical.x, 40);
    ZD_CHECK_EQ(compositor.nodes[0].physical.w, 64);

    ZD_CHECK_EQ(compositor.damage_count, 0U);
    ZD_CHECK_ERR(zd_compositor_begin_frame(&compositor, 0), ZD_EAGAIN);
    /* Damage outside the node is clipped away. */
    ZD_CHECK_OK(zd_compositor_damage(&compositor, window_id,
                                     (struct zd_rect){500, 500, 10, 10}));
    ZD_CHECK_EQ(compositor.damage_count, 0U);
    /* Damage inside is recorded. */
    ZD_CHECK_OK(zd_compositor_damage(&compositor, window_id,
                                     (struct zd_rect){50, 50, 20, 20}));
    ZD_CHECK_EQ(compositor.damage_count, 1U);
    /* Full damage covers the node. */
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    ZD_CHECK(compositor.damage_count >= 1U);
    /* Unknown window -> ENOENT. */
    ZD_CHECK_ERR(zd_compositor_damage(&compositor, 4242,
                                      (struct zd_rect){0, 0, 5, 5}),
                 ZD_ENOENT);
    /* Visible damage for this node (nothing above it) equals the rect. */
    {
        uint32_t count = zd_compositor_visible_damage(&compositor, visible, 16);
        ZD_CHECK(count >= 1U);
    }
}

static void test_present_writes_only_damage(void) {
    struct zd_bitmap target;
    int written;
    uint32_t index;
    uint32_t sentinel_mismatches = 0;
    setup();
    target.pixels = framebuffer;
    target.width = FB_WIDTH;
    target.height = FB_HEIGHT;
    target.stride_px = FB_WIDTH;

    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    ZD_CHECK_OK(zd_compositor_begin_frame(&compositor, 1000));
    written = zd_compositor_present(&compositor, target, 1000);
    ZD_CHECK(written > 0);
    ZD_CHECK_EQ(present_listener_calls, 1);
    ZD_CHECK(compositor.stats.frames_presented == 1);

    /* Pixels far from the damage keep their sentinel value: unchanged
     * content was not redrawn. */
    for (index = 0; index < 40U * FB_WIDTH; ++index)
        if (framebuffer[index] != 0xAAAAAAAAU)
            ++sentinel_mismatches;
    ZD_CHECK_EQ(sentinel_mismatches, 0U);
    /* Window pixels landed inside the node rect. */
    ZD_CHECK_EQ(framebuffer[45 * FB_WIDTH + 45], 0xFF3366CCU);

    /* Second present with no new damage writes zero pixels. */
    ZD_CHECK_EQ(zd_compositor_present(&compositor, target, 2000), 0);
    ZD_CHECK_EQ(present_listener_calls, 1);
    ZD_CHECK(compositor.stats.frames_presented == 1);
    (void)area_touched_as;
}

static void test_frame_pacing(void) {
    struct zd_bitmap target;
    struct zd_frame_policy policy;
    setup();
    target.pixels = framebuffer;
    target.width = FB_WIDTH;
    target.height = FB_HEIGHT;
    target.stride_px = FB_WIDTH;

    ZD_CHECK_EQ(zd_compositor_frame_interval_ns(&compositor), 16666666ULL);

    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    /* Too early: paced skip. */
    ZD_CHECK_OK(zd_compositor_begin_frame(&compositor, 1000));
    ZD_CHECK(zd_compositor_present(&compositor, target, 1000) > 0);
    /* Not dirty: skipped. */
    ZD_CHECK_ERR(zd_compositor_begin_frame(&compositor, 1001), ZD_EAGAIN);
    ZD_CHECK_EQ(zd_compositor_present(&compositor, target, 1001), 0);
    /* Dirty but inside the pacing window: skipped. */
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    ZD_CHECK_ERR(zd_compositor_begin_frame(&compositor, 1001), ZD_EAGAIN);
    ZD_CHECK_ERR(zd_compositor_present(&compositor, target, 1001),
                 ZD_EAGAIN);
    ZD_CHECK(zd_compositor_time_to_present(&compositor, 1001) > 0);
    ZD_CHECK_EQ(zd_compositor_time_to_present(&compositor,
                                              1000 + 16666666ULL), 0ULL);
    /* Next frame after the interval succeeds. */
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    ZD_CHECK_OK(zd_compositor_begin_frame(&compositor, 1000 + 16666666ULL));
    ZD_CHECK(zd_compositor_present(&compositor, target,
                                   1000 + 16666666ULL) > 0);

    /* Low-power mode halves the target rate (30 FPS cap default). */
    memset(&policy, 0, sizeof(policy));
    policy.refresh_mhz = 144000; /* 144 Hz */
    policy.low_power = 1;
    policy.max_fps_low_power = 30;
    zd_compositor_set_policy(&compositor, &policy);
    ZD_CHECK_EQ(zd_compositor_frame_interval_ns(&compositor), 33333333ULL);
    policy.low_power = 0;
    zd_compositor_set_policy(&compositor, &policy);
    ZD_CHECK_EQ(zd_compositor_frame_interval_ns(&compositor), 6944444ULL);
}

static void test_occlusion_culling(void) {
    struct zd_window_create_info info;
    zd_window_id upper;
    struct zd_rect visible[32];
    uint32_t count;
    setup();

    /* Second window fully covers the first. */
    memset(&info, 0, sizeof(info));
    info.client = client_id;
    info.title = "cover";
    info.logical_rect.x = 0;
    info.logical_rect.y = 0;
    info.logical_rect.w = 400;
    info.logical_rect.h = 400;
    info.monitor_id = 1;
    info.resizable = 1;
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &upper));
    zd_compositor_sync_scene(&compositor);

    /* Damage the covered (lower) window: everything above occludes it. */
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    count = zd_compositor_visible_damage(&compositor, visible, 32);
    ZD_CHECK_EQ(count, 0U); /* fully occluded */

    /* Partial overlap: upper sits at 200..600; lower damage at 40..104
     * remains visible in its uncovered portion. */
    ZD_CHECK_OK(zd_wm_move_resize(&wm, upper, (struct zd_rect){200, 0,
                                                                400, 400}));
    zd_compositor_sync_scene(&compositor);
    /* Clear pending damage from previous submissions. */
    compositor.damage_count = 0;
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    count = zd_compositor_visible_damage(&compositor, visible, 32);
    ZD_CHECK(count >= 1U);
    /* Upper window damaged under lower (lower is below upper in stacking):
     * the region of upper under lower is occluded by... lower is BELOW so
     * upper damage is not culled by lower. */
    compositor.damage_count = 0;
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, upper));
    count = zd_compositor_visible_damage(&compositor, visible, 32);
    ZD_CHECK(count >= 1U);
}

static void test_crash_drops_buffers_and_damage(void) {
    struct zd_bitmap target;
    setup();
    target.pixels = framebuffer;
    target.width = FB_WIDTH;
    target.height = FB_HEIGHT;
    target.stride_px = FB_WIDTH;

    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    zd_compositor_drop_client(&compositor, client_id);
    /* Pending damage from the dead client is dropped; scene resyncs. */
    ZD_CHECK_EQ(zd_wm_client_crashed(&wm, client_id), 1U);
    ZD_CHECK_EQ(wm.window_count, 0U);
    zd_compositor_sync_scene(&compositor);
    ZD_CHECK_EQ(compositor.node_count, 0U);
    ZD_CHECK_EQ(zd_compositor_present(&compositor, target, 0), 0);
    ZD_CHECK_EQ(compositor.stats.frames_presented, 0U);
}

static void test_cache_budget_and_eviction(void) {
    struct zd_bitmap target;
    setup();
    target.pixels = framebuffer;
    target.width = FB_WIDTH;
    target.height = FB_HEIGHT;
    target.stride_px = FB_WIDTH;

    /* Budget smaller than one frame of the 64x64 source (16KB). */
    zd_compositor_set_cache_budget(&compositor, 4096);
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    ZD_CHECK(zd_compositor_present(&compositor, target, 0) > 0);
    /* Entry could not be retained: cache bytes never exceed budget. */
    ZD_CHECK(compositor.cache_bytes <= compositor.cache_budget_bytes);
    ZD_CHECK(compositor.stats.cache_misses >= 1U);

    zd_compositor_set_cache_budget(&compositor, 1024ULL * 1024ULL);
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    ZD_CHECK(zd_compositor_present(&compositor, target, 16666666ULL) > 0);
    ZD_CHECK(compositor.cache_bytes <= compositor.cache_budget_bytes);
    /* Same owner+key re-presented under budget: LRU hit, no re-insert. */
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    ZD_CHECK(zd_compositor_present(&compositor, target, 33333332ULL) > 0);
    ZD_CHECK(compositor.stats.cache_hits >= 1U);
}

static void test_damage_storm_stress(void) {
    struct zd_bitmap target;
    uint32_t round;
    uint64_t presents_before;
    setup();
    target.pixels = framebuffer;
    target.width = FB_WIDTH;
    target.height = FB_HEIGHT;
    target.stride_px = FB_WIDTH;

    presents_before = compositor.stats.frames_presented;
    for (round = 0; round < 500U; ++round) {
        uint64_t now = 1000000ULL * round + 16666666ULL;
        /* Alternate small damages; merge must keep the table bounded. */
        ZD_CHECK_OK(zd_compositor_damage(
            &compositor, window_id,
            (struct zd_rect){40 + (int32_t)(round % 50U), 40, 8, 8}));
        if (zd_compositor_begin_frame(&compositor, now) == 0) {
            int written = zd_compositor_present(&compositor, target, now);
            ZD_CHECK(written >= 0);
        }
        ZD_CHECK(compositor.damage_count <= ZD_MAX_DAMAGE_RECTS);
    }
    /* Force final present. */
    ZD_CHECK_OK(zd_compositor_damage_full(&compositor, window_id));
    ZD_CHECK_OK(zd_compositor_begin_frame(&compositor, 1000000000ULL));
    ZD_CHECK(zd_compositor_present(&compositor, target, 1000000000ULL) > 0);
    ZD_CHECK(compositor.stats.frames_presented > presents_before);
    ZD_CHECK(compositor.stats.pixels_written > 0);
    ZD_CHECK(compositor.stats.damage_rects_submitted > 100U);
}

void zd_test_compositor_suite(void) {
    printf(" suite: compositor\n");
    ZD_RUN(test_scene_sync_and_damage);
    ZD_RUN(test_present_writes_only_damage);
    ZD_RUN(test_frame_pacing);
    ZD_RUN(test_occlusion_culling);
    ZD_RUN(test_crash_drops_buffers_and_damage);
    ZD_RUN(test_cache_budget_and_eviction);
    ZD_RUN(test_damage_storm_stress);
}
