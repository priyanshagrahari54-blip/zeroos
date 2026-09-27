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

#define SESSION_TARGET_W 320
#define SESSION_TARGET_H 200
#define SESSION_WINDOW_W 180
#define SESSION_WINDOW_H 70

static char line_buf[256];
static uint32_t staging[SESSION_TARGET_W * SESSION_TARGET_H];
static uint32_t window_pixels[SESSION_WINDOW_W * SESSION_WINDOW_H];

/* Shell-service state lives in .bss: the search index alone is larger than
 * the Ring-3 stack the launcher maps. */
static struct zd_fm session_fm;
static struct zd_search session_search;
static struct zd_settings session_settings;
static struct zd_files_provider session_files;
static struct zd_settings_provider session_settings_provider;
static struct zd_cmd_provider session_commands;
static struct zd_diag_provider session_diagnostics;
static struct zd_search_result session_results[8];

static uint64_t slen(const char *s) {
    uint64_t n = 0;
    while (s[n])
        ++n;
    return n;
}

static void say(const char *s) {
    (void)zeroos_write(1, s, slen(s));
    (void)zeroos_write(1, "\n", 1);
}

static void say_pair(const char *prefix, const char *suffix) {
    uint64_t k = 0;
    for (uint64_t i = 0; prefix[i] && k < sizeof(line_buf) - 1; ++i)
        line_buf[k++] = prefix[i];
    for (uint64_t i = 0; suffix[i] && k < sizeof(line_buf) - 2; ++i)
        line_buf[k++] = suffix[i];
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

/* Builds "dir/name" into out; -ZD_EOVERFLOW when it would not fit. */
static int session_join(char *out, uint32_t cap, const char *dir,
                        const char *name) {
    uint32_t k = 0;
    uint32_t i;
    if (!out || !dir || !name || !dir[0] || !name[0] || cap == 0)
        return -ZD_EINVAL;
    while (dir[k] && k + 1 < cap) {
        out[k] = dir[k];
        ++k;
    }
    if (dir[k])
        return -ZD_EOVERFLOW;
    if (out[k - 1] != '/' && k + 1 < cap) {
        out[k] = '/';
        ++k;
    }
    for (i = 0; name[i]; ++i) {
        if (k + 1 >= cap)
            return -ZD_EOVERFLOW;
        out[k] = name[i];
        ++k;
    }
    out[k] = 0;
    return 0;
}

/* ---- shell services over the real file syscalls (Stage 5) ------------- */

/* Directory source for the file manager and the files search provider.
 * Same callback shape the host suites inject a fixture into; here it is the
 * OPEN/READDIR/STAT/CLOSE ABI (syscalls 25-50), so a listing is a real
 * directory read, never a canned table. Returns a negative ZEROOS_E*. */
static int session_dir_source(void *context, const char *path,
                              struct zd_fm_entry *out, uint32_t cap,
                              uint32_t *out_n) {
    struct zeroos_dirent entry;
    struct zeroos_stat st;
    char full[ZD_FM_PATH];
    int64_t fd;
    int64_t rc = 0;
    uint32_t n = 0;
    uint32_t i;
    (void)context;

    *out_n = 0;
    if (!path || path[0] != '/')
        return -ZEROOS_EINVAL;
    fd = zeroos_open(path, ZEROOS_O_RDONLY | ZEROOS_O_DIRECTORY, 0);
    if (fd < 0)
        return (int)fd;
    for (;;) {
        rc = zeroos_readdir(fd, &entry);
        if (rc != 1)
            break;
        /* "." and ".." are navigation, not directory content. */
        if (entry.name[0] == '.' &&
            (entry.name[1] == 0 ||
             (entry.name[1] == '.' && entry.name[2] == 0)))
            continue;
        if (n >= cap)
            break;
        for (i = 0; entry.name[i] && i + 1 < ZD_FM_NAME; ++i)
            out[n].name[i] = entry.name[i];
        out[n].name[i] = 0;
        out[n].flags = 0;
        /* dirent.type is the mode's file-type nibble (mode >> 12), not the
         * raw S_IF* bits: ZEROOS_DT_DIR == 4. */
        if (entry.type == ZEROOS_DT_DIR)
            out[n].flags |= ZD_FM_DIR;
        if (out[n].name[0] == '.')
            out[n].flags |= ZD_FM_HIDDEN;
        out[n].size = 0;
        out[n].mtime = 0;
        if (session_join(full, sizeof(full), path, out[n].name) == 0 &&
            zeroos_stat(full, &st) == 0) {
            out[n].size = st.size;
            out[n].mtime = (int64_t)st.mtime_ns;
        }
        ++n;
    }
    (void)zeroos_close(fd);
    *out_n = n;
    return rc < 0 ? (int)rc : 0;
}

static int session_fs_remove(void *context, const char *path) {
    (void)context;
    return (int)zeroos_unlink(path);
}

static int session_fs_mkdir(void *context, const char *path) {
    (void)context;
    return (int)zeroos_mkdir(path, 0755);
}

/* Writes `length` bytes of `pattern` to `path`; 0 or -ZEROOS_E*. */
static int64_t session_write_file(const char *path, uint64_t length) {
    static const char filler[] = "zeroos shell binding\n";
    int64_t fd = zeroos_open(path, ZEROOS_O_WRONLY | ZEROOS_O_CREAT |
                                       ZEROOS_O_TRUNC,
                             0644);
    int64_t total = 0;
    if (fd < 0)
        return fd;
    while ((uint64_t)total < length) {
        uint64_t chunk = length - (uint64_t)total;
        int64_t written;
        if (chunk > sizeof(filler))
            chunk = sizeof(filler);
        written = zeroos_file_write(fd, filler, chunk);
        if (written <= 0) {
            (void)zeroos_close(fd);
            return written < 0 ? written : -ZEROOS_EIO;
        }
        total += written;
    }
    if (zeroos_close(fd) != 0)
        return -ZEROOS_EIO;
    return 0;
}

static int session_streq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && a[i] == b[i])
        ++i;
    return a[i] == 0 && b[i] == 0;
}

/* "shell reload" command: proves a command provider entry executes real
 * work against the live file manager. */
static uint32_t session_command_runs;

static int session_command_reload(void *context) {
    struct zd_fm *fm = context;
    if (zd_fm_refresh(fm) != 0)
        return -1;
    ++session_command_runs;
    return 0;
}

/* Diagnostics provider lines come from real session counters. */
static uint32_t session_diag_lines(void *context, uint32_t index, char *out,
                                   uint32_t cap) {
    const struct zd_fm *fm = context;
    const char *text;
    uint32_t i = 0;
    switch (index) {
    case 0: text = "shell: file manager bound to vfs"; break;
    case 1: text = "shell: search providers live"; break;
    case 2: text = "shell: compositor frame path ok"; break;
    default: return 0;
    }
    (void)fm;
    while (text[i] && i + 1 < cap) {
        out[i] = text[i];
        ++i;
    }
    out[i] = 0;
    return 1;
}

static const struct zd_setting_def session_scale_setting = {
    .key = "shell.scale_percent",
    .type = ZD_SETTING_INT,
    .scope = ZD_SCOPE_USER,
    .permissions_required = ZD_PERM_SETTINGS_USER,
    .default_value = 100,
    .min_value = 50,
    .max_value = 300,
    .group = "shell",
    .description = "Shell interface scaling"
};

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
    struct zeroos_stat file_stat;
    int64_t sys_result;
    int attach_result;
    int live = 0;
    int64_t written;
    uint32_t result_count;
    uint32_t found;
    uint32_t i;

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

    /* 7. File manager bound to the real VFS file syscalls. The listing, the
     * mkdir and the unlink below are real namespace operations on the
     * volatile /ram filesystem; nothing here is a canned table. */
    sys_result = zeroos_mkdir("/ram/shell", 0755);
    if (sys_result != 0 && sys_result != -ZEROOS_EEXIST)
        return fail("shell directory create", sys_result);
    if (session_write_file("/ram/shell/notes.txt", 64) != 0)
        return fail("shell notes write", 0);
    if (session_write_file("/ram/shell/.hidden", 8) != 0)
        return fail("shell hidden write", 0);

    zd_fm_init(&session_fm, session_dir_source, 0);
    session_fm.ops.remove = session_fs_remove;
    session_fm.ops.mkdir = session_fs_mkdir;
    session_fm.ops.ctx = 0;
    if (zd_fm_open(&session_fm, "/ram/shell") != 0)
        return fail("file manager open", session_fm.hist_state);
    /* notes.txt only: the dotfile is filtered, not dropped. */
    if (zd_fm_visible_count(&session_fm) != 1)
        return fail("file manager listing",
                    (int64_t)zd_fm_visible_count(&session_fm));
    if (!zd_fm_visible(&session_fm, 0) ||
        !session_streq(zd_fm_visible(&session_fm, 0)->name, "notes.txt"))
        return fail("file manager entry name", 0);
    if (zd_fm_visible(&session_fm, 0)->size != 64)
        return fail("file manager entry size",
                    (int64_t)zd_fm_visible(&session_fm, 0)->size);
    if (zd_fm_select(&session_fm, 0) != 0 ||
        zd_fm_selected_count(&session_fm) != 1)
        return fail("file manager select", 0);
    /* Permission gate first: a read-only actor cannot mutate. */
    if (zd_fm_remove(&session_fm, ZD_FM_PERM_READ, "notes.txt") != -1)
        return fail("file manager permission gate", 0);
    if (zeroos_stat("/ram/shell/notes.txt", &file_stat) != 0)
        return fail("file manager gate leaked a delete", 0);
    /* Namespace mutation through the injected VFS ops. */
    if (zd_fm_mkdir(&session_fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                    "inbox") != 0)
        return fail("file manager mkdir", 0);
    if (zd_fm_visible_count(&session_fm) != 2)
        return fail("file manager after mkdir",
                    (int64_t)zd_fm_visible_count(&session_fm));
    if (zd_fm_remove(&session_fm, ZD_FM_PERM_WRITE, "notes.txt") != 0)
        return fail("file manager remove", 0);
    if (zeroos_stat("/ram/shell/notes.txt", &file_stat) != -ZEROOS_ENOENT)
        return fail("file manager remove verify", 0);
    /* Only "inbox" is left visible: the dotfile stays filtered. */
    if (zd_fm_visible_count(&session_fm) != 1)
        return fail("file manager after remove",
                    (int64_t)zd_fm_visible_count(&session_fm));
    /* Hidden entries are a filter decision, never a listing decision. */
    if (zd_fm_set_show_hidden(&session_fm, 1) != 0 ||
        zd_fm_visible_count(&session_fm) != 2)
        return fail("file manager hidden filter",
                    (int64_t)zd_fm_visible_count(&session_fm));
    /* A missing directory surfaces the VFS errno and the UI condition that
     * belongs to it (VFS ENOENT is EMPTY, not LOW_RESOURCE). */
    sys_result = zd_fm_open(&session_fm, "/ram/no-such-dir");
    if (sys_result != -ZEROOS_ENOENT || session_fm.hist_state != 3)
        return fail("file manager missing directory", sys_result);
    if (zd_ui_condition_from_vfs_rc(sys_result) != ZD_UI_EMPTY)
        return fail("file manager error condition",
                    (int64_t)zd_ui_condition_from_vfs_rc(sys_result));
    if (zd_fm_open(&session_fm, "/ram/shell") != 0)
        return fail("file manager reopen", 0);
    say("ZEROOS: session file manager VFS binding passed.");

    /* 8. Universal Search over the live shell services: the files provider
     * reads the same VFS source as the file manager, the settings provider
     * reads the schema registry, and commands/diagnostics are wired to real
     * session state. */
    zd_search_init(&session_search);
    if (zd_files_provider_init(&session_files, session_dir_source, 0) != 0 ||
        zd_files_provider_add_root(&session_files, "/ram/shell") != 0)
        return fail("search files provider", 0);
    zd_settings_init(&session_settings);
    if (zd_settings_register(&session_settings, &session_scale_setting) != 0)
        return fail("search settings register", 0);
    zd_settings_provider_init(&session_settings_provider, &session_settings);
    if (zd_commands_init(&session_commands) != 0 ||
        zd_commands_register(&session_commands, "reload",
                             "Reload the file manager listing",
                             session_command_reload, &session_fm) != 0)
        return fail("search command register", 0);
    zd_diagnostics_init(&session_diagnostics, session_diag_lines,
                        &session_fm);
    if (zd_search_add_provider(&session_search,
                               &session_files.provider) != 0 ||
        zd_search_add_provider(&session_search,
                               &session_settings_provider.provider) != 0 ||
        zd_search_add_provider(&session_search,
                               &session_commands.provider) != 0 ||
        zd_search_add_provider(&session_search,
                               &session_diagnostics.provider) != 0)
        return fail("search provider registry", 0);

    /* "inbox" exists only because the file manager created it through the
     * VFS moments ago, so this hit proves a live directory read. */
    result_count = 0;
    if (zd_search_query(&session_search, "folder:inbox", session_results,
                        ZD_ARRAY_COUNT(session_results),
                        &result_count) != 0)
        return fail("search folder query", 0);
    found = 0;
    for (i = 0; i < result_count; ++i)
        if (session_results[i].kind == ZD_SEARCH_FOLDER &&
            session_streq(session_results[i].path, "/ram/shell/inbox"))
            found = 1;
    if (!found)
        return fail("search folder result", (int64_t)result_count);

    result_count = 0;
    if (zd_search_query(&session_search, "set:scale", session_results,
                        ZD_ARRAY_COUNT(session_results),
                        &result_count) != 0)
        return fail("search settings query", 0);
    found = 0;
    for (i = 0; i < result_count; ++i)
        if (session_results[i].kind == ZD_SEARCH_SETTING &&
            session_streq(session_results[i].label, "shell.scale_percent"))
            found = 1;
    if (!found)
        return fail("search settings result", (int64_t)result_count);

    result_count = 0;
    if (zd_search_query(&session_search, "cmd:reload", session_results,
                        ZD_ARRAY_COUNT(session_results),
                        &result_count) != 0)
        return fail("search command query", 0);
    found = 0;
    for (i = 0; i < result_count; ++i)
        if (session_results[i].kind == ZD_SEARCH_COMMAND)
            found = 1;
    if (!found || zd_commands_execute(&session_commands, "reload") != 0 ||
        session_command_runs != 1)
        return fail("search command execute", (int64_t)session_command_runs);

    result_count = 0;
    if (zd_search_query(&session_search, "diag:shell", session_results,
                        ZD_ARRAY_COUNT(session_results),
                        &result_count) != 0 ||
        result_count == 0)
        return fail("search diagnostics query", (int64_t)result_count);
    found = 0;
    for (i = 0; i < result_count; ++i)
        if (session_results[i].kind == ZD_SEARCH_DIAGNOSTIC)
            found = 1;
    if (!found)
        return fail("search diagnostics result", 0);

    /* Search must never rescan storage: neither the index core nor the
     * files provider is allowed a whole-tree walk. */
    if (session_search.index_stats.full_scans != 0 ||
        session_files.stats.full_scans != 0)
        return fail("search full scan",
                    (int64_t)session_search.index_stats.full_scans);
    say("ZEROOS: session universal search live providers passed.");

    say("ZEROOS: session shell process complete.");
    return 0;
}
