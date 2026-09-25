/*
 * ZEROOS Stage 5 session/shell process (Ring-3). The first real shell
 * process: wires the desktop display service to the DISPLAY_INFO and
 * DISPLAY_PRESENT syscalls, runs the compositor window path into a
 * staging framebuffer, pumps the INPUT_POLL/INPUT_WAIT syscalls through
 * the desktop input router, and reports each contract as a serial
 * milestone. Degraded (serial-only) displays are a first-class outcome:
 * the session attaches, refuses scanout submits through the service
 * gate, and still completes. Exits nonzero on any contract violation.
 */
#include <zeroos/desktop/desktop.h>

#define SESSION_TARGET_W 320
#define SESSION_TARGET_H 200
#define SESSION_WINDOW_W 180
#define SESSION_WINDOW_H 70

static char line_buf[256];
static uint32_t staging[SESSION_TARGET_W * SESSION_TARGET_H];
static uint32_t window_pixels[SESSION_WINDOW_W * SESSION_WINDOW_H];

static uint64_t slen(const char *s) {
    uint64_t n = 0;
    while (s[n])
        ++n;
    return n;
}

static void say(const char *s) {
    (void)zeroos_write(1, s, slen(s));
}

static void say_pair(const char *prefix, const char *suffix) {
    uint64_t k = 0;
    for (uint64_t i = 0; prefix[i] && k < sizeof(line_buf) - 1; ++i)
        line_buf[k++] = prefix[i];
    for (uint64_t i = 0; suffix[i] && k < sizeof(line_buf) - 2; ++i)
        line_buf[k++] = suffix[i];
    line_buf[k++] = '\n';
    line_buf[k] = 0;
    say(line_buf);
}

static int fail(const char *what, int64_t value) {
    char tmp[24];
    char out[200];
    int n = 0;
    int neg = value < 0;
    uint64_t u = neg ? (uint64_t)(-value) : (uint64_t)value;
    uint64_t k = 0;

    do {
        tmp[n++] = (char)('0' + u % 10U);
        u /= 10U;
    } while (u && n < 22);
    const char *prefix = "ZEROOS: session FAILED: ";
    for (uint64_t i = 0; prefix[i] && k < 190; ++i)
        out[k++] = prefix[i];
    for (uint64_t i = 0; what[i] && k < 190; ++i)
        out[k++] = what[i];
    out[k++] = ' ';
    out[k++] = '(';
    if (neg)
        out[k++] = '-';
    while (n > 0 && k < 195)
        out[k++] = tmp[--n];
    out[k++] = ')';
    out[k++] = '\n';
    out[k] = 0;
    say(out);
    return 1;
}

/* Display-service ops bound to the real kernel syscalls. */
static int session_query_info(void *context,
                              struct zeroos_display_info *info) {
    (void)context;
    return (int)zeroos_display_info(info);
}

static int session_present(void *context, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height,
                           uint32_t stride_bytes, const void *pixels) {
    (void)context;
    return (int)zeroos_display_present(x, y, width, height, stride_bytes,
                                       pixels);
}

/* No wall-clock syscall exists in ABI v1; pacing is disabled on-target
 * (min_present_interval_ticks stays 0) and the tick source is only a
 * required hook — host tests own the pacing behavior. */
static uint64_t session_ticks(void *context) {
    (void)context;
    return 0;
}

static const uint32_t *session_pixel_source(void *context,
                                            zd_window_id id, int32_t *width,
                                            int32_t *height,
                                            int32_t *stride_px) {
    (void)context;
    (void)id;
    *width = SESSION_WINDOW_W;
    *height = SESSION_WINDOW_H;
    *stride_px = SESSION_WINDOW_W;
    return window_pixels;
}

int session_main(void) {
    struct zeroos_display_info raw_info;
    struct zd_display_ops display_ops;
    struct zd_display_service display;
    struct zd_wm wm;
    struct zd_monitor monitor;
    struct zd_window_create_info window_info;
    struct zd_compositor compositor;
    struct zd_frame_policy policy;
    struct zd_input_router router;
    struct zd_input_delivery delivery;
    struct zeroos_input_event event;
    zd_client_id client;
    zd_window_id window = ZD_INVALID_WINDOW;
    struct zd_bitmap target;
    int64_t sys_result;
    int attach_result;
    int live = 0;
    int64_t written;

    /* 1. Display geometry from the kernel. */
    sys_result = zeroos_display_info(&raw_info);
    if (sys_result < 0)
        return fail("display info", sys_result);
    live = (raw_info.flags & ZEROOS_DISPLAY_FLAG_PRESENT) ? 1 : 0;
    say_pair("ZEROOS: session display service attached (",
             live ? "live)." : "degraded).");

    /* 2. Adopt geometry through the display service (real syscall ops). */
    display_ops.query_info = session_query_info;
    display_ops.present = session_present;
    display_ops.ticks = session_ticks;
    display_ops.context = 0;
    if (zd_display_service_init(&display, &display_ops, 0) != 0)
        return fail("display service init", 0);
    attach_result = zd_display_service_attach(&display);
    if (attach_result != 0 && attach_result != -ZD_ENOENT)
        return fail("display service attach", attach_result);
    if (live && display.state != ZD_DISPLAY_LIVE)
        return fail("live display not adopted", display.state);
    if (!live && display.state != ZD_DISPLAY_DEGRADED)
        return fail("degraded display not gated", display.state);

    /* 3. Window + compositor over the staging framebuffer. */
    zd_wm_init(&wm);
    monitor.id = 1;
    monitor.bounds.x = 0;
    monitor.bounds.y = 0;
    monitor.bounds.w = (int32_t)raw_info.width;
    monitor.bounds.h = (int32_t)raw_info.height;
    monitor.scale_percent = 100;
    monitor.primary = 1;
    monitor.enabled = 1;
    monitor.name[0] = 'e';
    monitor.name[1] = 'D';
    monitor.name[2] = 'P';
    monitor.name[3] = '-';
    monitor.name[4] = '1';
    monitor.name[5] = 0;
    if (zd_wm_add_monitor(&wm, &monitor) != 0)
        return fail("monitor add", 0);
    client = zd_wm_register_client(&wm);
    if (!client)
        return fail("client register", 0);

    window_info.client = client;
    window_info.title = "session-window";
    window_info.logical_rect.x = 10;
    window_info.logical_rect.y = 10;
    window_info.logical_rect.w = SESSION_WINDOW_W;
    window_info.logical_rect.h = SESSION_WINDOW_H;
    window_info.min_width = 0;
    window_info.min_height = 0;
    window_info.max_width = 0;
    window_info.max_height = 0;
    window_info.monitor_id = 1;
    window_info.a11y_label = "session test window";
    window_info.role = ZD_ROLE_WINDOW;
    window_info.resizable = 1;
    if (zd_wm_create_window(&wm, &window_info, &window) != 0)
        return fail("window create", 0);

    /* Solid window source (first row constant, then a simple gradient —
     * deterministic content the compositor can prove it copied). */
    for (int32_t y = 0; y < SESSION_WINDOW_H; ++y) {
        for (int32_t x = 0; x < SESSION_WINDOW_W; ++x)
            window_pixels[y * SESSION_WINDOW_W + x] =
                0xFF000000U | ((uint32_t)x << 8) | (uint32_t)(y & 0xFF);
    }

    policy.refresh_mhz = 60000;
    policy.low_power = 0;
    policy.reduced_motion = 0;
    policy.max_fps_low_power = 30;
    zd_compositor_init(&compositor, &wm, &policy);
    if (zd_compositor_set_pixel_source(&compositor, session_pixel_source,
                                       0) != 0)
        return fail("pixel source", 0);
    zd_compositor_sync_scene(&compositor);
    if (zd_compositor_damage_full(&compositor, window) != 0)
        return fail("damage", 0);
    if (zd_compositor_begin_frame(&compositor, 0) != 0)
        return fail("begin frame", 0);

    target.pixels = staging;
    target.width = SESSION_TARGET_W;
    target.height = SESSION_TARGET_H;
    target.stride_px = SESSION_TARGET_W;
    written = zd_compositor_present(&compositor, target, 0);
    if (written <= 0)
        return fail("compositor present", written);
    /* Source pixel (2,2) must land at screen (12,12): proof the
     * compositor really rasterized, not just counted. */
    if (staging[12 * SESSION_TARGET_W + 12] !=
        (0xFF000000U | (2U << 8) | 2U))
        return fail("compositor pixels",
                    staging[12 * SESSION_TARGET_W + 12]);
    say("ZEROOS: session compositor frame path passed.");

    /* 4. Scanout submit of the composited damage through the service. */
    if (zd_display_service_damage(
            &display,
            (struct zd_rect){10, 10, SESSION_WINDOW_W, SESSION_WINDOW_H}) !=
        0)
        return fail("service damage", 0);
    sys_result = zd_display_service_present(&display, staging,
                                            SESSION_TARGET_W * 4, 0);
    if (live) {
        if (sys_result != 0)
            return fail("live present", sys_result);
        say("ZEROOS: session scanout present contract passed.");
    } else {
        if (sys_result != -ZD_ENOENT)
            return fail("degraded present gate", sys_result);
        say("ZEROOS: session scanout degraded path passed.");
    }

    /* 5. Input syscalls: nonblocking drain, nonblocking wait, timed wait. */
    for (int attempt = 0; attempt < 16; ++attempt) {
        sys_result = zeroos_input_poll(&event);
        if (sys_result != 0 && sys_result != -ZEROOS_EAGAIN)
            return fail("input poll", sys_result);
        if (sys_result == 0) {
            if (event.kind != ZEROOS_INPUT_KIND_KEY &&
                event.kind != ZEROOS_INPUT_KIND_POINTER)
                return fail("input event kind", event.kind);
        }
    }
    sys_result = zeroos_input_wait(&event, ZEROOS_WAIT_FLAG_NONBLOCK, 0);
    if (sys_result != 0 && sys_result != -ZEROOS_EAGAIN)
        return fail("input wait nonblock", sys_result);
    sys_result = zeroos_input_wait(&event, 0, 5);
    if (sys_result != 0 && sys_result != -ZEROOS_ETIMEDOUT)
        return fail("input wait timed", sys_result);
    say("ZEROOS: session input pump passed.");

    /* 6. Desktop input routing over the live window manager. */
    zd_input_router_init(&router, &wm);
    delivery = zd_input_pointer_move(&router, 50, 50);
    if (delivery.result != ZD_INPUT_TO_WINDOW || delivery.window != window)
        return fail("pointer focus", delivery.result);
    delivery = zd_input_pointer_button(&router, 1, 1);
    if (delivery.result != ZD_INPUT_TO_WINDOW ||
        zd_input_pointer_focus(&router) != window)
        return fail("click to focus", delivery.result);
    delivery = zd_input_pointer_button(&router, 1, 0);
    if (delivery.result != ZD_INPUT_TO_WINDOW)
        return fail("click release", delivery.result);
    say("ZEROOS: session input routing passed.");

    say("ZEROOS: session shell process complete.");
    return 0;
}
