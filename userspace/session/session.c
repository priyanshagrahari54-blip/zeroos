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
#include <zeroos/storage.h>

#include <zeroos/font8x8.h>

#define SESSION_TARGET_W 320
#define SESSION_TARGET_H 200
#define SESSION_WINDOW_W 180
#define SESSION_WINDOW_H 70

/* The compositor contract test above runs against a small fixed frame. What
 * the panel actually shows is painted separately, a bounded strip at a time:
 * session.ld caps this image at 2 MB, so a framebuffer-sized buffer (3 MB at
 * 1024x768x32) cannot live here. Painting strips is what lets the desktop
 * cover the whole panel instead of a patch in its corner. */
#define SESSION_PANEL_MAX_W 1366
#define SESSION_STRIP_H 64
#define SESSION_BAR_H 28

/* Flat, legible desktop colours. Deliberately not a gradient: a ramp is what
 * the panel showed when only the contract-test window was composited, so a
 * gradient wallpaper would make a working desktop indistinguishable from the
 * bug it replaced. */
#define SESSION_COLOUR_WALL 0xFF1B2733U
#define SESSION_COLOUR_BAR 0xFF101418U
#define SESSION_COLOUR_BAR_EDGE 0xFF3A5068U
#define SESSION_COLOUR_TEXT 0xFFE6EDF3U
#define SESSION_COLOUR_TEXT_DIM 0xFF8B98A5U

#ifndef SESSION_BUILD_REVISION
/* Set by the Makefile from `git rev-parse --short HEAD`. The fallback keeps
 * this file compilable outside the build (host tools include the font only). */
#define SESSION_BUILD_REVISION "unknown"
#endif

static char line_buf[256];
static uint32_t staging[SESSION_TARGET_W * SESSION_TARGET_H];
static uint32_t window_pixels[SESSION_WINDOW_W * SESSION_WINDOW_H];
static uint32_t strip_pixels[SESSION_PANEL_MAX_W * SESSION_STRIP_H];

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

/* ---- Small string builders (no libc in Ring 3) ---- */

static uint64_t append_str(char *buf, uint64_t cap, uint64_t pos,
                           const char *s) {
    for (uint64_t i = 0; s[i] && pos + 1 < cap; ++i)
        buf[pos++] = s[i];
    return pos;
}

static uint64_t append_uint(char *buf, uint64_t cap, uint64_t pos,
                            uint32_t value) {
    char tmp[12];
    int n = 0;
    uint32_t u = value;
    do {
        tmp[n++] = (char)('0' + (u % 10U));
        u /= 10U;
    } while (u && n < 12);
    while (n > 0 && pos + 1 < cap)
        buf[pos++] = tmp[--n];
    return pos;
}

static uint64_t append_hex(char *buf, uint64_t cap, uint64_t pos,
                           uint32_t value, uint32_t digits) {
    static const char hex_digits[] = "0123456789abcdef";
    uint32_t i;

    for (i = 0; i < digits; ++i) {
        uint32_t shift = (digits - 1U - i) * 4U;

        if (pos + 1 < cap)
            buf[pos++] = hex_digits[(value >> shift) & 0xFU];
    }
    return pos;
}

/* Rasterise text into dst (dst_w x dst_h, one uint32 per pixel) at (ox,oy)
 * using the 8x8 font. Pixels that fall outside the destination are skipped, so
 * a strip shorter than the glyph is safe rather than a buffer overrun. */
static void draw_text(uint32_t *dst, int32_t dst_w, int32_t dst_h, int32_t ox,
                      int32_t oy, const char *text, uint32_t colour) {
    int32_t cx = ox;
    for (uint64_t i = 0; text[i] && cx >= 0 && cx + 8 <= dst_w; ++i) {
        const unsigned char *glyph = font8x8_glyph((unsigned char)text[i]);
        for (int32_t row = 0; row < FONT8X8_ROWS; ++row) {
            int32_t y = oy + row;
            if (y < 0 || y >= dst_h)
                continue;
            for (int32_t col = 0; col < 8; ++col) {
                if (glyph[row] & (0x80U >> col))
                    dst[y * dst_w + cx + col] = colour;
            }
        }
        cx += 8;
    }
}

/* ---- Interactive shell: real VFS commands over the file syscall ABI ----
 *
 * Output goes through say()/fd 1, which the kernel mirrors onto the
 * framebuffer console, so the shell needs no renderer of its own. Input is the
 * existing PS/2 keycode stream. Commands operate on the real /ram filesystem. */

static char shell_line[128];
static uint64_t shell_len;

static void shell_puts(const char *s) {
    say(s);
    say("\n");
}

static void shell_prompt(void) {
    say("zeroos:/ram$ ");
}

static int starts_with(const char *s, const char *prefix) {
    uint64_t i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i])
            return 0;
        ++i;
    }
    return 1;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ')
        ++s;
    return s;
}

static void shell_ls(const char *path) {
    struct zeroos_dirent entry;
    int64_t fd = zeroos_open(path, ZEROOS_O_RDONLY | ZEROOS_O_DIRECTORY, 0);
    uint64_t i, k;

    if (fd < 0) {
        shell_puts("ls: cannot open directory");
        return;
    }
    while (zeroos_readdir(fd, &entry) == 0) {
        char out[280];
        k = 0;
        for (i = 0; i < sizeof(entry.name) && entry.name[i] && k < 278; ++i)
            out[k++] = entry.name[i];
        out[k] = 0;
        shell_puts(out);
    }
    zeroos_close(fd);
}

static void shell_cat(const char *path) {
    uint8_t buf[256];
    int64_t fd = zeroos_open(path, ZEROOS_O_RDONLY, 0);
    int64_t n, i;

    if (fd < 0) {
        shell_puts("cat: no such file");
        return;
    }
    for (;;) {
        char tmp[257];
        n = zeroos_read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        for (i = 0; i < n; ++i)
            tmp[i] = (char)buf[i];
        tmp[i] = 0;
        say(tmp);
    }
    say("\n");
    zeroos_close(fd);
}

static void shell_write(const char *args) {
    char path[64];
    uint64_t k = 0;
    const char *p = skip_ws(args);
    int64_t fd;

    while (*p && *p != ' ' && k < sizeof(path) - 1)
        path[k++] = *p++;
    path[k] = 0;
    fd = zeroos_open(path, ZEROOS_O_WRONLY | ZEROOS_O_CREAT | ZEROOS_O_TRUNC,
                     0644);
    if (fd < 0) {
        shell_puts("write: cannot create");
        return;
    }
    zeroos_file_write(fd, skip_ws(p), slen(skip_ws(p)));
    zeroos_close(fd);
    shell_puts("write: ok");
}

/* List the PCI devices the kernel enumerated at boot, read through the
 * ZEROOS_SYS_PCI_LIST ABI. */
static void shell_pci(void) {
    struct zeroos_pci_device_info devices[16];
    int64_t n = zeroos_pci_list(devices, 16);
    int64_t i;

    if (n < 0) {
        shell_puts("pci: query failed");
        return;
    }
    if (n == 0) {
        shell_puts("pci: no devices enumerated yet");
        return;
    }
    for (i = 0; i < n; ++i) {
        char out[112];
        uint64_t k = 0;

        k = append_uint(out, sizeof(out), k, devices[i].bus);
        k = append_str(out, sizeof(out), k, ":");
        k = append_uint(out, sizeof(out), k, devices[i].slot);
        k = append_str(out, sizeof(out), k, ".");
        k = append_uint(out, sizeof(out), k, devices[i].function);
        k = append_str(out, sizeof(out), k, "  ");
        k = append_hex(out, sizeof(out), k, devices[i].vendor, 4);
        k = append_str(out, sizeof(out), k, ":");
        k = append_hex(out, sizeof(out), k, devices[i].device, 4);
        k = append_str(out, sizeof(out), k, "  class=");
        k = append_hex(out, sizeof(out), k, devices[i].class_code, 2);
        k = append_hex(out, sizeof(out), k, devices[i].subclass, 2);
        k = append_hex(out, sizeof(out), k, devices[i].prog_if, 2);
        out[k < sizeof(out) ? k : sizeof(out) - 1] = 0;
        shell_puts(out);
    }
    if (n == 16)
        shell_puts("pci: (list capped at 16 devices)");
}

static void shell_execute(const char *cmd) {
    const char *c = skip_ws(cmd);

    if (!*c)
        return;
    if (starts_with(c, "help")) {
        shell_puts("commands: help | ls [dir] | cat <file> | "
                   "write <file> <text> | rm <file> | pci");
    } else if (starts_with(c, "ls")) {
        const char *arg = skip_ws(c + 2);
        shell_ls(*arg ? arg : "/ram");
    } else if (starts_with(c, "cat ")) {
        shell_cat(skip_ws(c + 3));
    } else if (starts_with(c, "write ")) {
        shell_write(c + 5);
    } else if (starts_with(c, "pci")) {
        shell_pci();
    } else if (starts_with(c, "rm ")) {
        shell_puts(zeroos_unlink(skip_ws(c + 2)) == 0 ? "rm: ok"
                                                      : "rm: failed");
    } else {
        shell_puts("unknown command (try 'help')");
    }
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

/* Paint the real desktop over the whole panel. Called as soon as the
 * display service is adopted, BEFORE the compositor/input contracts, so a
 * person in front of the machine sees the desktop even if a later contract
 * stalls or fails on real hardware. Contracts still run afterwards and are
 * fatal in certification builds, keeping CI strict.
 */
static int session_paint_desktop(struct zd_display_service *display,
                                  const struct zeroos_display_info *info,
                                  int live) {
    /* 7. Paint the real desktop over the whole panel. Everything above proves
     * the frame path; this is what a person in front of the machine sees. */
    if (live) {
        int32_t panel_w = (int32_t)info->width;
        int32_t panel_h = (int32_t)info->height;
        char label[96];
        char geometry[32];
        char report[192];
        uint64_t label_len;
        uint64_t geo_len;
        uint64_t report_len;
        int64_t sys_result;

        if (panel_w <= 0 || panel_h <= SESSION_BAR_H + 1)
            return fail("panel geometry cannot host a desktop", 0);
        if (panel_w > SESSION_PANEL_MAX_W) {
            say("ZEROOS: session desktop unsupported - panel is wider than the "
                "strip buffer; refusing to present a clipped desktop.\n");
            return fail("panel wider than the supported cap", 0);
        }

        /* Build identity, assembled once. The revision is compiled in by the
         * Makefile from `git rev-parse --short HEAD`, so the panel itself names
         * the build the person in front of it is looking at - no serial cable
         * and no download log needed to tell two ISOs apart. */
        label_len = 0;
        label_len = append_str(label, sizeof(label), label_len,
                               "ZEROOS  build ");
        label_len = append_str(label, sizeof(label), label_len,
                               SESSION_BUILD_REVISION);
        label[label_len] = 0;

        geo_len = 0;
        geo_len = append_uint(geometry, sizeof(geometry), geo_len,
                              info->width);
        geo_len = append_str(geometry, sizeof(geometry), geo_len, "x");
        geo_len = append_uint(geometry, sizeof(geometry), geo_len,
                              info->height);
        geometry[geo_len] = 0;

        for (int32_t y0 = 0; y0 < panel_h; y0 += SESSION_STRIP_H) {
            struct zd_rect band;
            int32_t rows = panel_h - y0;
            int32_t r;
            int32_t x;

            if (rows > SESSION_STRIP_H)
                rows = SESSION_STRIP_H;
            for (r = 0; r < rows; ++r) {
                int32_t y = y0 + r;
                uint32_t colour = SESSION_COLOUR_WALL;
                if (y < SESSION_BAR_H - 1)
                    colour = SESSION_COLOUR_BAR;
                else if (y == SESSION_BAR_H - 1)
                    colour = SESSION_COLOUR_BAR_EDGE;
                for (x = 0; x < panel_w; ++x)
                    strip_pixels[r * panel_w + x] = colour;
            }
            if (y0 == 0) {
                /* Identity in the bar: build on the left, panel size on the
                 * right, skipped if the panel is too narrow to fit both. */
                int32_t right = panel_w - (int32_t)geo_len * 8 - 12;
                draw_text(strip_pixels, panel_w, rows, 12, 10, label,
                          SESSION_COLOUR_TEXT);
                if (right >= 12 + (int32_t)label_len * 8)
                    draw_text(strip_pixels, panel_w, rows, right, 10, geometry,
                              SESSION_COLOUR_TEXT_DIM);
            }
            band.x = 0;
            band.y = y0;
            band.w = panel_w;
            band.h = rows;
            if (zd_display_service_damage(display, band) != 0)
                return fail("desktop strip damage", 0);
            sys_result = zd_display_service_present_rect(
                display, band, (uint32_t)panel_w * 4U, strip_pixels,
                session_ticks(0));
            if (sys_result != 0)
                return fail("desktop strip present", sys_result);
        }

        /* Geometry and revision on one line. The boot gate asserts this exact
         * shape, so a green run means the live branch really painted. */
        report_len = 0;
        report_len = append_str(report, sizeof(report), report_len,
                                "ZEROOS: session desktop painted across the "
                                "full panel at ");
        report_len = append_str(report, sizeof(report), report_len, geometry);
        report_len = append_str(report, sizeof(report), report_len,
                                " (build ");
        report_len = append_str(report, sizeof(report), report_len,
                                SESSION_BUILD_REVISION);
        report_len = append_str(report, sizeof(report), report_len, ").\n");
        report[report_len] = 0;
        say(report);
    } else {
        /* No live scanout: say so instead of implying a desktop is up. */
        say("ZEROOS: session desktop paint degraded (no live scanout).\n");
    }
    return 0;
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

    /* Paint first: the desktop must appear before any contract can stall.
     * Contracts below still run and certify in certification builds. */
    session_paint_desktop(&display, &raw_info, live);

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


    /* 8. Shell self-test: exercise the real VFS from this Ring-3 process so
     * the file ABI is certified end-to-end on every boot. */
    {
        const char *path = "/ram/shell_selftest.txt";
        uint8_t rb[32];
        int64_t fd, n;
        int ok;

        /* /ram is mounted by the asynchronous storage task, so wait for it
         * (cooperatively) before touching the filesystem. */
        {
            int mounted = 0, i;
            for (i = 0; i < 200000; ++i) {
                int64_t d = zeroos_open("/ram", ZEROOS_O_RDONLY |
                                                  ZEROOS_O_DIRECTORY, 0);
                if (d >= 0) {
                    zeroos_close(d);
                    mounted = 1;
                    break;
                }
                zeroos_yield();
            }
            if (!mounted)
                return fail("shell /ram mount wait", 0);
        }

        fd = zeroos_open(path, ZEROOS_O_WRONLY | ZEROOS_O_CREAT |
                                 ZEROOS_O_TRUNC, 0644);
        ok = fd >= 0;
        if (ok) {
            ok = zeroos_file_write(fd, "zeroos", 6) == 6;
            zeroos_close(fd);
        }
        fd = zeroos_open(path, ZEROOS_O_RDONLY, 0);
        ok = ok && fd >= 0;
        if (fd >= 0) {
            n = zeroos_read(fd, rb, sizeof(rb));
            ok = ok && n == 6 && rb[0] == 'z' && rb[5] == 's';
            zeroos_close(fd);
        }
        ok = ok && zeroos_unlink(path) == 0;
        if (!ok)
            return fail("shell file self-test", 0);
        /* Also drive the interactive command engine so it is exercised (and
         * referenced) in both the certification and production builds. */
        shell_len = 0;
        shell_line[0] = 0;
        shell_prompt();
        shell_execute("help");
        shell_execute("write /ram/shell_probe.txt hi");
        shell_execute("cat /ram/shell_probe.txt");
        shell_execute("ls /ram");
        shell_execute("rm /ram/shell_probe.txt");
        say("ZEROOS: session shell file self-test passed.\n");
    }

    /* 9. PCI enumeration is reachable from Ring-3 through the syscall ABI.
     * pci_init() runs before storage_mount_ram() in the storage task, so the
     * /ram wait above already guarantees enumeration has completed. */
    {
        struct zeroos_pci_device_info devices[16];
        int64_t n = zeroos_pci_list(devices, 16);
        uint64_t pos = 0;

        if (n <= 0)
            return fail("pci enumeration from Ring-3", n);
        if (devices[0].vendor == 0xFFFFU || devices[0].vendor == 0U ||
            devices[0].bus != 0U)
            return fail("pci device record", devices[0].vendor);
        pos = append_str(line_buf, sizeof(line_buf), pos,
                         "ZEROOS: session pci enumeration visible to Ring-3 "
                         "(devices=");
        pos = append_uint(line_buf, sizeof(line_buf), pos, (uint32_t)n);
        pos = append_str(line_buf, sizeof(line_buf), pos,
                         ", first=");
        pos = append_hex(line_buf, sizeof(line_buf), pos, devices[0].vendor, 4);
        pos = append_str(line_buf, sizeof(line_buf), pos, ":");
        pos = append_hex(line_buf, sizeof(line_buf), pos, devices[0].device, 4);
        pos = append_str(line_buf, sizeof(line_buf), pos, ").\n");
        line_buf[pos < sizeof(line_buf) ? pos : sizeof(line_buf) - 1] = 0;
        say(line_buf);
    }

#ifdef ZEROOS_BOOT_CERTIFICATION
    say("ZEROOS: session shell process complete.");
    return 0;
#else
    /* Production boot: keep the desktop as a real long-lived Ring-3
     * process. The kernel input wait is the lifetime boundary, so idle CPU
     * usage stays event-driven instead of polling. */
    say("ZEROOS: interactive desktop session ready.");
    shell_puts("ZEROOS shell ready - type 'help' for commands.");
    shell_prompt();
    for (;;) {
        sys_result = zeroos_input_wait(&event, 0, 0);
        if (sys_result == -ZEROOS_EINTR)
            continue;
        if (sys_result != 0)
            return fail("persistent input wait", sys_result);
        if (event.kind != ZEROOS_INPUT_KIND_KEY ||
            !(event.flags & ZEROOS_INPUT_FLAG_DOWN))
            continue;
        if (event.code == ZEROOS_KEY_ENTER) {
            say("\n");
            shell_execute(shell_line);
            shell_len = 0;
            shell_line[0] = 0;
            shell_prompt();
        } else if (event.code == ZEROOS_KEY_BACKSPACE) {
            if (shell_len) {
                shell_len--;
                shell_line[shell_len] = 0;
            }
        } else if (event.code >= 32 && event.code < 127) {
            if (shell_len + 1 < sizeof(shell_line)) {
                char echo[2];
                echo[0] = (char)event.code;
                echo[1] = 0;
                shell_line[shell_len++] = (char)event.code;
                shell_line[shell_len] = 0;
                say(echo);
            }
        }
    }
#endif
}
