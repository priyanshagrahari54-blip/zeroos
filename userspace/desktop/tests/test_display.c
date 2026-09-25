#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

/* Display-service tests: geometry adoption, degraded/suspended gating,
 * damage merging, pacing and the exact present-call contract (rect-local
 * source pointer, stride passthrough) against injected ops. */

struct fake_display {
    struct zeroos_display_info info;
    int query_result;          /* returned by query_info */
    int present_result;        /* returned by present */
    uint32_t query_calls;
    uint32_t present_calls;
    uint32_t last_x, last_y, last_w, last_h, last_stride;
    const uint8_t *last_pixels;
};

static int fake_query(void *context, struct zeroos_display_info *info) {
    struct fake_display *fake = context;
    fake->query_calls++;
    if (fake->query_result != 0)
        return fake->query_result;
    *info = fake->info;
    return 0;
}

static int fake_present(void *context, uint32_t x, uint32_t y, uint32_t w,
                        uint32_t h, uint32_t stride, const void *pixels) {
    struct fake_display *fake = context;
    fake->present_calls++;
    fake->last_x = x;
    fake->last_y = y;
    fake->last_w = w;
    fake->last_h = h;
    fake->last_stride = stride;
    fake->last_pixels = pixels;
    return fake->present_result;
}

static uint64_t fake_ticks(void *context) {
    (void)context;
    return 0;
}

static uint32_t ticks_value;

static uint64_t counting_ticks(void *context) {
    (void)context;
    return ticks_value;
}

static void fill_valid_info(struct fake_display *fake) {
    memset(fake, 0, sizeof(*fake));
    fake->info.width = 1024;
    fake->info.height = 768;
    fake->info.pitch = 4096;
    fake->info.bpp = 32;
    fake->info.byte_size = 4096ULL * 768ULL;
    fake->info.flags = ZEROOS_DISPLAY_FLAG_PRESENT;
    fake->query_result = 0;
    fake->present_result = 0;
}

static struct zd_display_ops make_ops(struct fake_display *fake) {
    struct zd_display_ops ops;
    ops.query_info = fake_query;
    ops.present = fake_present;
    ops.ticks = fake_ticks;
    ops.context = fake;
    return ops;
}

static void test_init_validation(void) {
    struct zd_display_service service;
    struct fake_display fake;
    struct zd_display_ops ops = make_ops(&fake);
    struct zd_display_ops incomplete = ops;

    memset(&fake, 0, sizeof(fake));
    ZD_CHECK_EQ(zd_display_service_init(0, &ops, 0), -ZD_EINVAL);
    ZD_CHECK_EQ(zd_display_service_init(&service, 0, 0), -ZD_EINVAL);
    incomplete.query_info = 0;
    ZD_CHECK_EQ(zd_display_service_init(&service, &incomplete, 0),
                -ZD_EINVAL);
    incomplete = ops;
    incomplete.present = 0;
    ZD_CHECK_EQ(zd_display_service_init(&service, &incomplete, 0),
                -ZD_EINVAL);
    incomplete = ops;
    incomplete.ticks = 0;
    ZD_CHECK_EQ(zd_display_service_init(&service, &incomplete, 0),
                -ZD_EINVAL);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_ATTACHING);
    ZD_CHECK_EQ(service.failure_limit, 3);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 5), 0);
    ZD_CHECK_EQ(service.failure_limit, 5);
}

static void test_attach_live_and_degraded(void) {
    struct zd_display_service service;
    struct fake_display fake;
    struct zd_display_ops ops;

    /* Live adoption. */
    fill_valid_info(&fake);
    ops = make_ops(&fake);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_LIVE);
    ZD_CHECK_EQ(service.width, 1024);
    ZD_CHECK_EQ(service.height, 768);
    ZD_CHECK_EQ(fake.query_calls, 1);

    /* No PRESENT flag: usable state, refused submits. */
    fill_valid_info(&fake);
    fake.info.flags = 0;
    ops = make_ops(&fake);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), -ZD_ENOENT);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_DEGRADED);
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 10, 10}),
                0);
    ZD_CHECK_EQ(zd_display_service_present(&service, &fake, 64, 0),
                -ZD_ENOENT);
    ZD_CHECK_EQ(fake.present_calls, 0);
    ZD_CHECK_EQ(service.stats.presents_refused_degraded, 1);

    /* Query error propagates and degrades. */
    fill_valid_info(&fake);
    fake.query_result = -9;
    ops = make_ops(&fake);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), -9);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_DEGRADED);

    /* Invalid geometry (pitch too small) degrades with EINVAL. */
    fill_valid_info(&fake);
    fake.info.pitch = 16;
    ops = make_ops(&fake);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), -ZD_EINVAL);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_DEGRADED);
}

static void test_damage_merge_and_clip(void) {
    struct zd_display_service service;
    struct fake_display fake;
    struct zd_display_ops ops;

    fill_valid_info(&fake);
    ops = make_ops(&fake);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);

    /* Before geometry: rejected. */
    zd_display_service_init(&service, &ops, 0);
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}),
                -ZD_EINVAL);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);

    /* Merge two rects into a bounding box. */
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){10, 10, 20, 20}),
                0);
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){100, 50, 30, 10}),
                0);
    ZD_CHECK_EQ(service.pending_damage.x, 10);
    ZD_CHECK_EQ(service.pending_damage.y, 10);
    ZD_CHECK_EQ(service.pending_damage.w, 120); /* 10..130 */
    ZD_CHECK_EQ(service.pending_damage.h, 50);  /* 10..60 */

    /* Clip: partially out of bounds. */
    zd_display_service_init(&service, &ops, 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){1000, 700, 100,
                                                           200}),
                0);
    ZD_CHECK_EQ(service.pending_damage.x, 1000);
    ZD_CHECK_EQ(service.pending_damage.y, 700);
    ZD_CHECK_EQ(service.pending_damage.w, 24);
    ZD_CHECK_EQ(service.pending_damage.h, 68);

    /* Fully outside: ignored. */
    zd_display_service_init(&service, &ops, 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){-50, -50, 10, 10}),
                0);
    ZD_CHECK_EQ(service.pending_valid, 0);
}

static void test_present_contract(void) {
    struct zd_display_service service;
    struct fake_display fake;
    struct zd_display_ops ops;
    uint8_t frame[1024];

    fill_valid_info(&fake);
    ops = make_ops(&fake);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);

    /* Nothing pending: refused, no syscall. */
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 64, 0),
                -ZD_EAGAIN);
    ZD_CHECK_EQ(fake.present_calls, 0);
    ZD_CHECK_EQ(service.stats.presents_refused_empty, 1);

    /* Damage at (8,4): present receives rect-local pointer. */
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){8, 4, 6, 2}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 64, 100), 0);
    ZD_CHECK_EQ(fake.present_calls, 1);
    ZD_CHECK_EQ(fake.last_x, 8);
    ZD_CHECK_EQ(fake.last_y, 4);
    ZD_CHECK_EQ(fake.last_w, 6);
    ZD_CHECK_EQ(fake.last_h, 2);
    ZD_CHECK_EQ(fake.last_stride, 64);
    ZD_CHECK((const uint8_t *)fake.last_pixels == frame + 4 * 64 + 8 * 4);
    ZD_CHECK_EQ(service.pending_valid, 0);
    ZD_CHECK_EQ(service.stats.frames_presented, 1);
    ZD_CHECK_EQ(service.stats.pixels_submitted, 12);

    /* Redundant present without new damage: never submitted. */
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 64, 200),
                -ZD_EAGAIN);
    ZD_CHECK_EQ(fake.present_calls, 1);
}

static void test_pacing(void) {
    struct zd_display_service service;
    struct fake_display fake;
    struct zd_display_ops ops;
    uint8_t frame[16];

    fill_valid_info(&fake);
    ops = make_ops(&fake);
    ops.ticks = counting_ticks;
    ticks_value = 1000;
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);
    service.min_present_interval_ticks = 3;

    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 1000), 0);

    /* Within the interval: paced out, no syscall. */
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 1001),
                -ZD_EAGAIN);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 1002),
                -ZD_EAGAIN);
    ZD_CHECK_EQ(fake.present_calls, 1);
    ZD_CHECK_EQ(service.stats.presents_refused_paced, 2);

    /* Interval elapsed: submits again. */
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 1003), 0);
    ZD_CHECK_EQ(fake.present_calls, 2);
}

static void test_failure_suspension_and_recovery(void) {
    struct zd_display_service service;
    struct fake_display fake;
    struct zd_display_ops ops;
    uint8_t frame[16];

    fill_valid_info(&fake);
    ops = make_ops(&fake);
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);

    /* Two failures stay LIVE (below the limit of 3). */
    fake.present_result = -9;
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 1), -9);
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 2), -9);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_LIVE);
    ZD_CHECK_EQ(service.stats.present_failures, 2);

    /* Third consecutive failure suspends the circuit. */
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 3), -9);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_SUSPENDED);

    /* Suspended: refuses without touching the syscall. */
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    fake.present_calls = 0;
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 4),
                -ZD_ESTATE);
    ZD_CHECK_EQ(fake.present_calls, 0);
    ZD_CHECK_EQ(service.stats.presents_refused_suspended, 1);

    /* Explicit re-attach recovers: counters reset, LIVE again, and any
     * damage recorded before the restart was discarded with the old
     * geometry (callers re-damage on attach). */
    fake.present_result = 0;
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_LIVE);
    ZD_CHECK_EQ(service.consecutive_failures, 0);
    ZD_CHECK_EQ(service.pending_valid, 0);
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 5), 0);
    ZD_CHECK_EQ(service.stats.frames_presented, 1);

    /* A success between failures resets the consecutive counter. */
    fake.present_result = -9;
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 6), -9);
    fake.present_result = 0;
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 7), 0);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_LIVE);
    fake.present_result = -9;
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){0, 0, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 16, 8), -9);
    ZD_CHECK_EQ(service.state, ZD_DISPLAY_LIVE); /* 1 < limit after reset */
}

void zd_test_display_suite(void) {
    zd_test_current = "display";
    test_init_validation();
    test_attach_live_and_degraded();
    test_damage_merge_and_clip();
    test_present_contract();
    test_pacing();
    test_failure_suspension_and_recovery();
    zd_test_current = "main";
}
