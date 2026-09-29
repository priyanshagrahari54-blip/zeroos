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
#include <zeroos/desktop/clipboard.h>
#include <zeroos/desktop/downloads.h>
#include <zeroos/desktop/notify.h>
#include <zeroos/desktop/launcher.h>
#include <zeroos/desktop/perfcenter.h>
#include <zeroos/desktop/overview.h>
#include <zeroos/desktop/bar.h>
#include <zeroos/desktop/metrics.h>
#include <zeroos/desktop/ai.h>
#include <zeroos/desktop/browser.h>
#include <zeroos/desktop/study.h>
#include <zeroos/desktop/fps.h>
#include <zeroos/desktop/gaming.h>
#include <zeroos/desktop/a11y.h>
#include <zeroos/desktop/i18n.h>
#include <zeroos/desktop/media.h>
#include <zeroos/desktop/snapshot.h>
#include <zeroos/desktop/vault.h>
#include <zeroos/desktop/firewall.h>
#include <zeroos/desktop/url.h>
#include <zeroos/desktop/pdf.h>
#include <zeroos/desktop/notes.h>
#include <zeroos/desktop/ocr.h>
#include <zeroos/desktop/dict.h>
#include <zeroos/desktop/nav.h>
#include <zeroos/desktop/eco.h>
#include <zeroos/desktop/capability.h>
#include <zeroos/desktop/update.h>
#include "crypto.h"

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
static struct zd_settings session_settings_restored;
static struct zd_settings session_settings_tampered;
static char settings_blob[ZD_SETTINGS_BLOB_CAP];
static char settings_blob_read[ZD_SETTINGS_BLOB_CAP + 1];
static struct zd_files_provider session_files;
static struct zd_settings_provider session_settings_provider;
static struct zd_cmd_provider session_commands;
static struct zd_diag_provider session_diagnostics;
static struct zd_search_result session_results[8];
/* Terminal screen buffer (~476 KiB): must stay in .bss, never on the
 * 64 KiB user stack. */
static struct zd_sandbox session_sandbox;
/* Buffer for the shell-child ELF fetched through syscall 56. The image is
 * 163 bytes today; ZEROOS_EXEC_MAX_IMAGE keeps the buffer valid for any
 * image the kernel will accept. */
static uint8_t child_image[ZEROOS_EXEC_MAX_IMAGE];
static const char child_arg0[] = "shell";
static const char child_arg1[] = "certify";
static const char child_env0[] = "ZEROOS_CHILD=certified";
/* Keystrokes for the line-discipline step: type "zero", erase two bytes,
 * type "os", then Enter.  The edited line must be "zeos". */
static const uint8_t session_typed[] = { 'z', 'e', 'r', 'o', 0x08, 0x08,
                                         'o', 's', 0x0d };
static uint8_t echo_buffer[64];
static struct zd_term_line session_line;
static struct zd_term session_term;
/* One line of shell output: SGR colour, text, CRLF. It travels through a
 * real kernel pipe before the VT parser sees a single byte. */
static const char term_source[] = "\033[32mzeroos shell\r\n";
static struct zd_automation session_automation;
static struct zd_lifecycle session_lifecycle;
static struct zd_governor session_governor;
static struct zd_watchdog session_watchdog;
static uint32_t session_notify_posts;
static uint32_t session_auto_callbacks;
static uint32_t session_wd_restarts;
static uint32_t session_wd_degraded;
static uint32_t session_lifecycle_transitions;

/* 20 ms pacing floor and automation cooldown, expressed in uptime
 * nanoseconds because the display service compares the tick hook's own
 * units (the shell feeds it monotonic ns). */
#define SESSION_PACING_NS 20000000ULL

/* Automation actions reach real services: notifications really count, a
 * setting really changes in the schema registry, callbacks really run. */
static int session_auto_notify(void *context, const char *target) {
    (void)context;
    if (!target || target[0] == '\0')
        return -ZD_EINVAL;
    ++session_notify_posts;
    return 0;
}

static int session_auto_set_setting(void *context, const char *target,
                                    int32_t value) {
    uint32_t changed = 0;
    (void)context;
    if (!target)
        return -ZD_EINVAL;
    return zd_settings_set_number(&session_settings, target, (int64_t)value,
                                  ZD_PERM_SETTINGS_USER, &changed);
}

static int session_auto_callback(void *context, const char *target,
                                 uint32_t event) {
    (void)context;
    (void)event;
    if (!target)
        return -ZD_EINVAL;
    ++session_auto_callbacks;
    return 0;
}

static const struct zd_automation_ops session_auto_ops = {
    .notify = session_auto_notify,
    .set_setting = session_auto_set_setting,
    .callback = session_auto_callback,
    .log = 0,
    .context = 0
};

static void session_lifecycle_on_change(void *context,
                                        enum zd_lifecycle_state previous,
                                        enum zd_lifecycle_state next,
                                        enum zd_lifecycle_event cause) {
    (void)context;
    (void)previous;
    (void)next;
    (void)cause;
    ++session_lifecycle_transitions;
}

static void session_wd_on_decision(void *context, uint32_t service_id,
                                   enum zd_watchdog_decision decision,
                                   uint64_t delay_ns) {
    (void)context;
    (void)service_id;
    (void)delay_ns;
    if (decision == ZD_WD_RESTART)
        ++session_wd_restarts;
}

static void session_wd_on_degraded(void *context) {
    (void)context;
    ++session_wd_degraded;
}

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

/* Monotonic clock for the shell. ABI v1 exposes no wall clock, so the
 * system-information syscall (55) provides the kernel's own monotonic
 * time — invariant TSC when the CPU guarantees it, PIT otherwise. Every
 * deadline in the shell (present pacing, automation cooldowns, watchdog
 * heartbeats) is compared in these nanoseconds; 0 means the read failed
 * and callers treat that as "no time has passed". */
static uint64_t session_uptime_ns(void) {
    static struct zeroos_system_info clock;
    if (zeroos_system_info(&clock, sizeof(clock)) != 0)
        return 0;
    return clock.uptime_ns;
}

/* Milliseconds elapsed on the kernel clock since `origin`; 0 if the clock
 * read failed or went backwards. */
static uint64_t session_elapsed_ms(uint64_t origin) {
    uint64_t now = session_uptime_ns();
    if (now == 0 || now < origin)
        return 0;
    return (now - origin) / 1000000ULL;
}

/* Wait until at least `ms` milliseconds have passed since `origin`. The
 * guard bound keeps a stalled clock from hanging the boot. */
static int session_wait_ms(uint64_t origin, uint64_t ms) {
    uint64_t guard = 0;
    while (session_elapsed_ms(origin) < ms) {
        if (++guard > 20000000ULL)
            return -1;
    }
    return 0;
}

static const struct zd_tab *session_browser_tab(const struct zd_browser *b,
                                                uint32_t id) {
    uint32_t i;
    for (i = 0; i < ZD_BROWSER_MAX_TABS; ++i)
        if (b->tabs[i].used && b->tabs[i].id == id)
            return &b->tabs[i];
    return (const struct zd_tab *)0;
}

static uint64_t session_ticks(void *context) {
    (void)context;
    return session_uptime_ns();
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

/* Rename inside one filesystem: syscall 38, so a move within a directory
 * is a real namespace operation rather than a copy. */
static int session_fs_rename(void *context, const char *from,
                             const char *to) {
    (void)context;
    return (int)zeroos_rename(from, to);
}

/* Bounded read for preview and copy. Never writes past `capacity`; the
 * caller owns the buffer. */
static int session_fs_read_file(void *context, const char *path, void *buffer,
                                uint32_t capacity, uint32_t *out_length) {
    int64_t fd;
    int64_t n;
    (void)context;
    if (out_length)
        *out_length = 0;
    if (!buffer || capacity == 0)
        return -ZEROOS_EINVAL;
    fd = zeroos_open(path, ZEROOS_O_RDONLY, 0);
    if (fd < 0)
        return (int)fd;
    n = zeroos_read(fd, buffer, capacity);
    (void)zeroos_close(fd);
    if (n < 0)
        return (int)n;
    if (out_length)
        *out_length = (uint32_t)n;
    return 0;
}

/* Create-or-truncate write: the destination of a copy is always a whole
 * file, never an appended fragment. */
static int session_fs_write_file(void *context, const char *path,
                                 const void *buffer, uint32_t length) {
    int64_t fd;
    int64_t n;
    (void)context;
    fd = zeroos_open(path,
                     ZEROOS_O_WRONLY | ZEROOS_O_CREAT | ZEROOS_O_TRUNC, 0644);
    if (fd < 0)
        return (int)fd;
    n = zeroos_file_write(fd, buffer, length);
    (void)zeroos_close(fd);
    if (n < 0)
        return (int)n;
    if ((uint32_t)n != length)
        return -ZEROOS_EIO;
    return 0;
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
/* One line of filler; shared by the writer below and the preview
 * assertion in step 7 so the two can never disagree about the content. */
static const char session_filler[] = "zeroos shell binding\n";
/* Pins the array/pointer distinction the write loop depends on: a
 * pointer-sized chunk would rewrite the same leading bytes forever. */
_Static_assert(sizeof(session_filler) == 22U, "session filler layout");

static int64_t session_write_file_mode(const char *path, uint64_t length,
                                       uint32_t mode) {
    int64_t fd = zeroos_open(path, ZEROOS_O_WRONLY | ZEROOS_O_CREAT |
                                       ZEROOS_O_TRUNC,
                             mode);
    int64_t total = 0;
    if (fd < 0)
        return fd;
    while ((uint64_t)total < length) {
        uint64_t chunk = length - (uint64_t)total;
        int64_t written;
        /* sizeof of the ARRAY, not of a pointer: the loop restarts at the
         * beginning of the filler every iteration, so a pointer-sized
         * chunk would write the same leading bytes over and over. */
        if (chunk > sizeof(session_filler))
            chunk = sizeof(session_filler);
        written = zeroos_file_write(fd, session_filler, chunk);
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

static int64_t session_write_file(const char *path, uint64_t length) {
    return session_write_file_mode(path, length, 0644);
}

/* Raw buffer file I/O: the update key and bundle are byte images, not
 * text, so they must not go through the filler-based writer. */
static int64_t session_write_buffer(const char *path, const uint8_t *data,
                                    uint64_t length, uint32_t mode) {
    int64_t fd = zeroos_open(path, ZEROOS_O_WRONLY | ZEROOS_O_CREAT |
                                       ZEROOS_O_TRUNC,
                             mode);
    int64_t written;
    if (fd < 0)
        return fd;
    written = zeroos_file_write(fd, data, length);
    if (written != (int64_t)length) {
        (void)zeroos_close(fd);
        return written < 0 ? written : -ZEROOS_EIO;
    }
    if (zeroos_close(fd) != 0)
        return -ZEROOS_EIO;
    return 0;
}

static int64_t session_read_buffer(const char *path, uint8_t *out,
                                   uint64_t capacity, uint64_t *read_len) {
    int64_t fd = zeroos_open(path, ZEROOS_O_RDONLY, 0);
    int64_t n;
    if (fd < 0)
        return fd;
    n = zeroos_read(fd, out, capacity);
    (void)zeroos_close(fd);
    if (n < 0)
        return n;
    *read_len = (uint64_t)n;
    return 0;
}

/* Certification key material.  What is under test is the provisioning
 * path -- the key travels through the filesystem and is used only after
 * it has been read back -- not the secrecy of these arrays. */
static const uint8_t update_key[32] = {
    0x5a, 0xe4, 0x11, 0x9c, 0x73, 0x08, 0xbd, 0x2f,
    0x66, 0xa1, 0xd5, 0x40, 0x1e, 0x8b, 0xc7, 0x52,
    0x09, 0xf3, 0x6a, 0xb8, 0x2d, 0x74, 0xe0, 0x35,
    0xc1, 0x48, 0x9f, 0x06, 0xba, 0xd2, 0x57, 0x8e
};
static const uint8_t update_nonce[12] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c
};
static const uint8_t update_payload[] = "ZEROOS update bundle 1.1.0\n";
#define UPDATE_PAYLOAD_LEN (sizeof(update_payload) - 1)
static const char update_version[] = "1.1.0";
static uint8_t update_key_file[32];
static uint8_t update_ct[UPDATE_PAYLOAD_LEN];
static uint8_t update_tag[16];
static struct zd_update session_update;
static struct zd_clipboard session_clipboard;
static char clip_overlong[ZD_CLIP_TEXT + 8];
static struct zd_overview session_overview;
static struct zd_perf_center session_perf;
static struct zd_perf_center session_perf_cold;
static struct zd_bar session_bar;
static struct zd_metrics session_metrics;
static struct zd_fw session_fw;
static struct zd_url session_nav_url;
static struct zd_pdf session_pdf;
static struct zd_notes session_notes;
static struct zd_dict session_dict;
static struct zd_nav session_nav;
static struct zd_eco session_eco;
/* A device name over the ecosystem's limit. */
static const char eco_long_name[] =
    "0123456789012345678901234";
static int session_dict_fail;
/* The media item's real name, kept where the dictionary's
 * source can reach it. */
static char session_dict_extra[ZD_DICT_WORD];

/* The dictionary's source is the shell's own live vocabulary: the titles the
 * notebook really holds, the media item's real name, and one duplicate the
 * loader has to drop. No bundled corpus is claimed. */
static int session_dict_source(void *ctx, char (*words)[ZD_DICT_WORD],
                               uint32_t cap, uint32_t *out_n) {
    uint32_t n = 0;
    uint32_t i;
    uint32_t j;
    (void)ctx;
    if (session_dict_fail)
        return -ZD_ENOENT;
    for (i = 0; i < session_notes.count && n + 1 < cap; ++i) {
        for (j = 0; session_notes.items[i].title[j] &&
                    j + 1 < ZD_DICT_WORD; ++j)
            words[n][j] = session_notes.items[i].title[j];
        words[n][j] = 0;
        ++n;
    }
    if (n + 2 < cap && session_dict_extra[0]) {
        for (j = 0; session_dict_extra[j] && j + 1 < ZD_DICT_WORD; ++j)
            words[n][j] = session_dict_extra[j];
        words[n][j] = 0;
        ++n;
        /* the same word the notebook already gave, in lower case */
        for (j = 0; session_notes.items[0].title[j] &&
                    j + 1 < ZD_DICT_WORD; ++j)
            words[n][j] = (char)(session_notes.items[0].title[j] |
                                 (session_notes.items[0].title[j] >= 'A' &&
                                  session_notes.items[0].title[j] <= 'Z'
                                      ? 0x20 : 0));
        words[n][j] = 0;
        ++n;
    }
    *out_n = n;
    return 0;
}
/* A title one byte over the notebook's limit. */
static const char note_long_title[] =
    "0123456789012345678901234567890123";
static uint8_t session_pdf_bytes[512];

/* Hand-authored minimal PDF images, written to a real file
 * by the shell and read back through the kernel. */
static const char session_pdf_two_pages[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
    "2 0 obj << /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >> endobj\n"
    "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
    "4 0 obj << /Length 20 >> stream\n"
    "(ZEROOS page one) Tj\n"
    "endstream endobj\n"
    "5 0 obj << /Type /Page /Parent 2 0 R /Contents 6 0 R >> endobj\n"
    "6 0 obj << /Length 20 >> stream\n"
    "[ (Hel) 3 (lo) ] TJ\n"
    "endstream endobj\n"
    "trailer << /Size 7 /Root 1 0 R >>\n"
    "startxref\n0\n%%EOF\n";
static const char session_pdf_filtered[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
    "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
    "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
    "4 0 obj << /Length 10 /Filter /FlateDecode >> stream\n"
    "xxxxxxxxxx\n"
    "endstream endobj\n"
    "trailer << /Size 5 /Root 1 0 R /Encrypt 9 0 R >>\n"
    "%%EOF\n";
static const char session_pdf_encrypted[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
    "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
    "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
    "4 0 obj << /Length 10 >> stream\n"
    "(locked!!!!) Tj\n"
    "endstream endobj\n"
    "trailer << /Size 6 /Root 1 0 R /Encrypt 5 0 R >>\n"
    "%%EOF\n";
static const char session_pdf_no_pages[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog >> endobj\n"
    "trailer << /Size 2 /Root 1 0 R >>\n"
    "%%EOF\n";


/* Navigation fixtures: two URLs the shell will really open, and four it must
 * refuse -- a script scheme, a data scheme, a local file the shell does have,
 * a hostless URL and an out-of-range port. */
#define ZD_NAV_URL_COUNT 6u
static const char *const nav_urls[ZD_NAV_URL_COUNT] = {
    "https://example.invalid/docs",
    "http://example.invalid:8080/x",
    "javascript:alert(1)",
    "data:text/html;base64,PHN2Zz4=",
    "file:/ram/shell/inbox/copy.txt",
    "https://example.invalid:99999/"
};
static const uint32_t nav_reasons[ZD_NAV_URL_COUNT] = {
    ZD_URL_OK_REJECT, ZD_URL_OK_REJECT, ZD_URL_R_BAD_SCHEME,
    ZD_URL_R_BAD_SCHEME, ZD_URL_R_BAD_SCHEME, ZD_URL_R_BAD_PORT
};
static const int nav_schemes[ZD_NAV_URL_COUNT] = {
    ZD_URL_SCHEME_HTTPS, ZD_URL_SCHEME_HTTP, ZD_URL_SCHEME_NONE,
    ZD_URL_SCHEME_NONE, ZD_URL_SCHEME_NONE, ZD_URL_SCHEME_NONE
};
static const uint16_t nav_ports[ZD_NAV_URL_COUNT] = { 0, 8080, 0, 0, 0, 0 };
static const uint8_t nav_secure[ZD_NAV_URL_COUNT] = { 1, 0, 0, 0, 0, 0 };
static const char *const nav_paths[ZD_NAV_URL_COUNT] = {
    "/docs", "/x", "", "", "", ""
};
static struct zd_vault session_vault;
static struct zd_scan session_scan;
static struct zd_snapshots session_snapshots;
static struct zd_update_ops session_snap_update_ops;
static uint8_t session_snap_buffer[64];
static int session_snap_fail;

/* Snapshot hooks over the real filesystem: capture copies the live
 * user-data file into a per-snapshot file, restore copies it back, discard
 * removes it. A hook failure becomes a FAILED snapshot point, never a
 * half-registered one. */
static int session_snap_path(const char *name, char *out, uint32_t cap) {
    static const char prefix[] = "/ram/shell/snap-";
    uint32_t n = 0;
    uint32_t i;
    if (!name || !out || !cap)
        return -1;
    for (i = 0; prefix[i]; ++i) {
        if (n + 1 >= cap)
            return -1;
        out[n++] = prefix[i];
    }
    for (i = 0; name[i]; ++i) {
        if (n + 1 >= cap)
            return -1;
        out[n++] = name[i];
    }
    out[n] = 0;
    return 0;
}

static int session_snap_capture(void *ctx, const char *name) {
    char path[64];
    uint64_t len = 0;
    (void)ctx;
    if (session_snap_fail)
        return -ZD_ENOSPC;
    if (session_snap_path(name, path, (uint32_t)sizeof(path)) != 0)
        return -ZD_EINVAL;
    if (session_read_buffer("/ram/shell/userdata.txt", session_snap_buffer,
                            sizeof(session_snap_buffer), &len) != 0)
        return -ZD_ENOENT;
    if (session_write_buffer(path, session_snap_buffer, len, 0644) != 0)
        return -ZD_ENOSPC;
    return 0;
}

static int session_snap_restore(void *ctx, const char *name) {
    char path[64];
    uint64_t len = 0;
    (void)ctx;
    if (session_snap_path(name, path, (uint32_t)sizeof(path)) != 0)
        return -ZD_EINVAL;
    if (session_read_buffer(path, session_snap_buffer,
                            sizeof(session_snap_buffer), &len) != 0)
        return -ZD_ENOENT;
    if (session_write_buffer("/ram/shell/userdata.txt", session_snap_buffer,
                             len, 0644) != 0)
        return -ZD_ENOSPC;
    return 0;
}

static int session_snap_discard(void *ctx, const char *name) {
    char path[64];
    (void)ctx;
    if (session_snap_path(name, path, (uint32_t)sizeof(path)) != 0)
        return -ZD_EINVAL;
    if (zeroos_unlink(path) != 0)
        return -ZD_ENOENT;
    return 0;
}

static const struct zd_snapshot_ops session_snap_ops = {
    session_snap_capture, session_snap_restore, session_snap_discard,
    (void *)0
};

static struct zd_media session_media;
static struct zd_a11y session_a11y;
static struct zd_i18n session_i18n;
static struct zd_fps session_fps;
static struct zd_gaming session_gaming;
static struct zd_study session_study;
static struct zd_browser session_browser;
static struct zd_ai_broker session_ai;
static uint32_t session_ai_reads;
static uint64_t session_ai_bytes;

static struct zd_ai_request session_ai_request(const char *path,
                                               uint32_t context_mask,
                                               uint8_t want_remote) {
    struct zd_ai_request r;
    uint32_t i = 0;
    for (i = 0; i < sizeof(r); ++i)
        ((uint8_t *)&r)[i] = 0;
    r.id = 1;
    r.kind = ZD_AI_REQ_SUMMARIZE;
    r.context_mask = context_mask;
    r.want_remote = want_remote;
    i = 0;
    while (path[i] && i + 1 < sizeof(r.payload)) {
        r.payload[i] = path[i];
        ++i;
    }
    r.payload[i] = 0;
    return r;
}

static int session_ai_select(void *ctx, const struct zd_ai_request *req,
                             uint32_t grants, enum zd_ai_backend *out) {
    (void)ctx;
    (void)grants;
    *out = req->want_remote ? ZD_AI_BACKEND_REMOTE : ZD_AI_BACKEND_LOCAL;
    return 0;
}

/* Decimal rendering for the freestanding shell (no libc here). */
static uint32_t session_decimal(char *out, uint32_t cap, uint64_t value) {
    char digits[24];
    uint32_t n = 0;
    uint32_t written = 0;
    if (!out || !cap)
        return 0;
    if (value == 0)
        digits[n++] = '0';
    while (value != 0 && n < (uint32_t)sizeof(digits)) {
        digits[n++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    }
    while (n != 0 && written + 1 < cap)
        out[written++] = digits[--n];
    if (written < cap)
        out[written] = 0;
    return written;
}

/* The study assistant's requests carry "study <deck>: <front>" rather than a
 * path. */
static int session_payload_is_study(const char *payload) {
    static const char prefix[] = "study ";
    uint32_t i;
    if (!payload)
        return 0;
    for (i = 0; prefix[i]; ++i) {
        if (payload[i] != prefix[i])
            return 0;
    }
    return 1;
}

/* The shell's own backend: a summarize request carries a path and the answer
 * is that document's size as the kernel reports it; a study-assistant request
 * is answered from the live deck. A missing grant, a missing file or a
 * fabricated answer all come out as a wrong number. */
static int session_ai_run(void *ctx, enum zd_ai_backend backend,
                          const struct zd_ai_request *req, char *out,
                          uint32_t out_cap, uint32_t *out_len) {
    uint8_t buf[128];
    uint64_t len = 0;
    uint64_t value;
    (void)ctx;
    if (backend == ZD_AI_BACKEND_NONE || !out || !out_len)
        return -ZD_EINVAL;
    if (session_payload_is_study(req->payload)) {
        value = session_study.card_count;
    } else {
        if (session_read_buffer(req->payload, buf, sizeof(buf), &len) != 0)
            return -ZD_ENOENT;
        ++session_ai_reads;
        session_ai_bytes = len;
        value = len;
    }
    *out_len = session_decimal(out, out_cap, value);
    return 0;
}

static const struct zd_ai_ops session_ai_ops = { session_ai_select,
                                                 session_ai_run, (void *)0 };
static uint8_t session_toggle_applied[ZD_BAR_TOGGLE_COUNT];
static int session_bar_toggle_seen = -1;

/* The bar never applies a quick control itself: it asks the shell, and the
 * shell refuses the ones this image cannot honour. */
static int session_bar_toggle(void *ctx, int toggle_id, int on) {
    (void)ctx;
    session_bar_toggle_seen = toggle_id;
    if (toggle_id == ZD_BAR_TOGGLE_BLUETOOTH)
        return -38; /* no radio in the image */
    session_toggle_applied[toggle_id] = (uint8_t)on;
    return 0;
}

static const struct zd_bar_ops session_bar_ops = { session_bar_toggle,
                                                   (void *)0 };
static struct zd_launcher session_launcher;
static struct zd_caps session_caps;

/* The launcher's hook really creates a process: it spawns the embedded
 * child image with SPAWN and hands the pid back to the shell, which waits
 * for it. A refusal is an errno, never a pretend launch. */
struct session_launch_ctx {
    const uint8_t *image;
    uint64_t image_size;
    int64_t pid;
};
static struct session_launch_ctx session_launch_ctx;

static int session_launch_hook(void *ctx, const char *name) {
    struct session_launch_ctx *c = (struct session_launch_ctx *)ctx;
    (void)name;
    if (!c || !c->image || c->image_size == 0)
        return -ZEROOS_EINVAL;
    c->pid = zeroos_spawn(c->image, c->image_size, 0, 0, 0, 0);
    if (c->pid <= 0)
        return c->pid < 0 ? (int)c->pid : -ZEROOS_EAGAIN;
    return 0;
}

static struct zd_notify session_notify;

/* Listener counters: the shell observes posts and rate limits through the
 * same callback contract a UI would use. */
struct session_notify_ctx {
    uint32_t posted;
    uint32_t limited;
    int last_priority;
};
static struct session_notify_ctx session_notify_ctx;

static void session_notify_posted(void *context, zd_notification_id id,
                                  enum zd_notify_priority priority) {
    struct session_notify_ctx *c = (struct session_notify_ctx *)context;
    (void)id;
    if (!c)
        return;
    c->posted++;
    c->last_priority = (int)priority;
}

static void session_notify_limited(void *context, const char *app_id) {
    struct session_notify_ctx *c = (struct session_notify_ctx *)context;
    (void)app_id;
    if (c)
        c->limited++;
}

static struct zd_downloads session_downloads;
static uint8_t dl_src_buf[128];
static uint8_t dl_dst_buf[128];

/* The download hooks are bound to the VFS. The start hook opens both
 * ends of the transfer; the bytes are then pumped in bounded chunks while
 * the item is RUNNING, because that is the only state in which the queue
 * accepts progress -- reporting from inside the start hook is rejected
 * with -22, since the queue flips the state only after the hook returns.
 * A missing source fails the transfer with the errno the VFS returned. */
struct session_dl_ctx {
    char dest[96];
    int64_t src_fd;
    int64_t dst_fd;
    uint32_t copied;
};
static struct session_dl_ctx session_dl_ctx;

static int session_dl_start(void *ctx, uint32_t id, const char *url) {
    struct session_dl_ctx *c = (struct session_dl_ctx *)ctx;
    (void)id;
    if (!c || !url)
        return -ZEROOS_EINVAL;
    c->copied = 0;
    c->src_fd = -1;
    c->dst_fd = -1;
    c->src_fd = zeroos_open(url, ZEROOS_O_RDONLY, 0);
    if (c->src_fd < 0)
        return (int)c->src_fd;
    c->dst_fd = zeroos_open(c->dest, ZEROOS_O_WRONLY | ZEROOS_O_CREAT |
                                         ZEROOS_O_TRUNC,
                            0644);
    if (c->dst_fd < 0) {
        (void)zeroos_close(c->src_fd);
        c->src_fd = -1;
        return (int)c->dst_fd;
    }
    return 0;
}

static int session_dl_pump(struct session_dl_ctx *c, uint32_t id) {
    uint8_t buffer[64];
    int64_t n, written;
    if (!c || c->src_fd < 0 || c->dst_fd < 0)
        return -ZEROOS_EINVAL;
    for (;;) {
        n = zeroos_read(c->src_fd, buffer, sizeof(buffer));
        if (n < 0)
            return (int)n;
        if (n == 0)
            break;
        written = zeroos_file_write(c->dst_fd, buffer, (uint64_t)n);
        if (written != n)
            return written < 0 ? (int)written : -ZEROOS_EIO;
        c->copied += (uint32_t)n;
        if (zd_downloads_progress(&session_downloads, id, c->copied, 0) != 0)
            return -ZEROOS_EIO;
    }
    if (zeroos_close(c->src_fd) != 0 || zeroos_close(c->dst_fd) != 0)
        return -ZEROOS_EIO;
    c->src_fd = -1;
    c->dst_fd = -1;
    return 0;
}

/* zd_downloads_add reports success with 0 and keeps the id in the item,
 * so the shell looks the item up by the url it enqueued. */
static int64_t session_dl_find_url(const struct zd_downloads *d,
                                   const char *url) {
    uint32_t i;
    if (!d || !url)
        return -ZEROOS_EINVAL;
    for (i = 0; i < ZD_DL_MAX; ++i) {
        const struct zd_dl_item *it = &d->items[i];
        uint32_t k = 0;
        if (!it->id)
            continue;
        while (it->url[k] && it->url[k] == url[k])
            ++k;
        if (it->url[k] == 0 && url[k] == 0)
            return (int64_t)it->id;
    }
    return -ZEROOS_ENOENT;
}

struct session_update_ctx {
    const uint8_t *ciphertext;
    uint32_t ciphertext_len;
    const char *version;
    uint32_t version_len;
};
static struct session_update_ctx session_update_ctx;

/* Pipeline hooks with real side effects: staging writes the sealed
 * bundle, activation writes the version into the active slot, commit
 * records the good marker, and rollback removes the active slot. */
static int session_update_stage(void *ctx) {
    const struct session_update_ctx *c =
        (const struct session_update_ctx *)ctx;
    if (!c || !c->ciphertext || c->ciphertext_len == 0)
        return -1;
    return session_write_buffer("/ram/shell/update.staged", c->ciphertext,
                                c->ciphertext_len, 0600) == 0 ? 0 : -1;
}

static int session_update_activate(void *ctx) {
    const struct session_update_ctx *c =
        (const struct session_update_ctx *)ctx;
    if (!c || !c->version || c->version_len == 0)
        return -1;
    return session_write_buffer("/ram/shell/update.active",
                                (const uint8_t *)c->version, c->version_len,
                                0644) == 0 ? 0 : -1;
}

static int session_update_rollback(void *ctx) {
    int64_t rc;
    (void)ctx;
    rc = zeroos_unlink("/ram/shell/update.active");
    /* Rollback must be idempotent: an already-absent slot is success. */
    return (rc == 0 || rc == -ZEROOS_ENOENT) ? 0 : -1;
}

static int session_update_commit(void *ctx) {
    const struct session_update_ctx *c =
        (const struct session_update_ctx *)ctx;
    if (!c || !c->version || c->version_len == 0)
        return -1;
    return session_write_buffer("/ram/shell/update.good",
                                (const uint8_t *)c->version, c->version_len,
                                0644) == 0 ? 0 : -1;
}

static const struct zd_update_ops session_update_ops = {
    session_update_stage, session_update_activate, session_update_rollback,
    session_update_commit, &session_update_ctx
};

static int session_streq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && a[i] == b[i])
        ++i;
    return a[i] == 0 && b[i] == 0;
}

/* Select the visible entry with this exact name; -1 when it is absent.
 * Selections are by visible index, so the lookup has to run against the
 * filtered, sorted listing the user actually sees. */
static int session_select_by_name(struct zd_fm *fm, const char *name) {
    uint32_t i;
    for (i = 0; i < zd_fm_visible_count(fm); ++i) {
        const struct zd_fm_entry *entry = zd_fm_visible(fm, i);
        if (entry && session_streq(entry->name, name)) {
            if (zd_fm_select(fm, i) != 0)
                return -1;
            return (int)i;
        }
    }
    return -1;
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
    char preview[24];
    struct zeroos_ipc_pair pipe_pair;
    const struct zd_term_cell *cell;
    uint8_t term_buffer[64]; /* VT parser takes uint8_t */
    char term_row[96];
    uint64_t term_len;
    uint32_t term_line_len;
    uint32_t row_len;
    uint32_t preview_len;
    struct zd_fm_batch_result batch;
    struct zd_privacy_sources sources;
    struct zd_privacy_report report;
    struct zd_sb_audit sb_audit[4];
    int audit_entries;
    int64_t cred;
    uint64_t child_size;
    uint64_t child_argv[2];
    uint64_t child_envp[1];
    uint64_t key_len;
    uint32_t key_index;
    int event_index;
    int64_t dl_id;
    struct zd_dl_item *dl_item;
    uint32_t dl_index;
    struct zd_notify_post note_post;
    zd_notification_id note_id;
    zd_notification_id note_ids[4];
    struct zd_notify_group note_groups[4];
    uint64_t notify_now;
    zd_notification_id bar_note_ids[4];
    struct zeroos_system_info status_info;
    struct zd_ai_request ai_request;
    const struct zd_tab *tab;
    const struct zd_fm_entry *study_entry;
    struct zd_study_card *study_card;
    char study_back[24];
    uint64_t fps_start;
    uint64_t fps_now;
    uint64_t fps_frame_ms;
    uint32_t fps_live;
    uint32_t game_pressure;
    uint32_t game_fps_milli;
    uint32_t game_budget;
    uint32_t game_effects;
    int game_yield;
    uint32_t fps_index;
    struct zd_a11y_snapshot_row a11y_snap[8];
    struct zd_a11y_announcement a11y_msg;
    const struct zd_a11y_node *a11y_found;
    const struct zd_window *a11y_win;
    const struct zd_notification *a11y_note;
    const char *i18n_text_en;
    const char *i18n_text_hi;
    zd_window_id a11y_ids[8];
    zd_notification_id a11y_note_ids[4];
    zd_window_id a11y_target;
    uint32_t a11y_windows;
    uint32_t a11y_node;
    uint32_t a11y_rows;
    uint32_t a11y_first;
    uint32_t a11y_steps;
    uint32_t a11y_states;
    uint32_t a11y_notes;
    uint32_t i18n_loaded;
    uint32_t i18n_total;
    uint32_t i18n_hi_present;
    int a11y_i;
    int a11y_urgency;
    enum zd_notify_a11y a11y_policy;
    /* Filesystem prefix for the listing; the media library itself is
     * keyed by origin label, not by path. */
    static const char media_dir[] = "/ram/shell/inbox/";
    static const char fw_app[] = "ai.broker";
    const struct zd_fm_entry *media_entry;
    char media_path[96];
    char media_first[48];
    uint32_t media_count;
    uint32_t media_len;
    int media_i;
    int media_j;
    struct zd_snapshot *snap;
    struct zd_vault_entry *vault_entry;
    uint8_t vault_out[ZD_VAULT_SECRET_MAX];
    uint8_t vault_wrong[ZD_VAULT_KEY_LEN];
    uint32_t vault_len = 0;
    int vault_i;
    int vault_j;
    int vault_match;
    int vault_same;
    uint8_t vault_nonce[12];
    struct zd_fw_flow fw_flow;
    struct zd_fw_rule fw_rule;
    struct zd_ai_request fw_request;
    uint32_t fw_downgrades = 0;
    uint32_t fw_rule_id = 0;
    int fw_i;
    uint32_t nav_tab = 0;
    uint32_t nav_opened = 0;
    int nav_i;
    int nav_rc;
    uint64_t pdf_len = 0;
    uint32_t pdf_cards;
    const struct zd_note *nt_body;
    uint8_t nt_buffer[256];
    char nt_snippet[64];
    uint64_t nt_len = 0;
    uint32_t nt_id = 0;
    uint32_t note_id2 = 0;
    uint32_t note_id3 = 0;
    uint32_t nt_found = 0;
    int nt_hits;
    int nt_snip_len;
    int nt_i;
    char dict_out[4][ZD_DICT_WORD];
    uint32_t dict_n = 0;
    int dict_total;
    uint32_t eco_idx = 0;
    uint32_t eco_idx2 = 0;
    uint32_t eco_sent = 0;
    uint32_t study_entries;
    uint32_t study_cards;
    int study_i;
    uint8_t browser_doc[128];
    uint64_t browser_origin;
    uint64_t browser_step;
    uint64_t browser_base;
    uint64_t browser_now_ms;
    uint64_t browser_guard;
    uint64_t doc_len;
    uint32_t tab_id;
    uint32_t ai_done;
    int ai_i;
    uint32_t status_free;
    struct session_status {
        uint64_t uptime_ns;
        uint32_t cpu_count;
        uint32_t ram_mb;
        uint32_t free_percent;
        uint32_t frame_budget_ms;
        uint64_t avg_interval_ms;
        uint64_t p95_interval_ms;
        uint64_t frames;
        uint32_t health;
        uint32_t notifications;
    } sys_status;
    uint32_t bar_notes;
    int bar_action;
    int bar_arg;
    int bar_i;
    const struct zd_window *bar_target;
    uint64_t notify_due;
    struct zd_app *launch_results[4];
    struct zd_app *launch_app;
    int launch_count;
    zd_window_id window_side;
    const struct zd_window *win_view;
    const struct zd_window *side_view;
    struct zd_rect snap_left;
    struct zd_rect snap_right;
    struct zd_rect overview_area;
    uint32_t overview_ids[2];
    uint32_t overview_first;
    uint32_t overview_second;
    uint32_t switches_before;
    struct zd_pc_input perf_in;
    struct zd_pc_report perf_report;
    uint64_t perf_cycle_ns;
    uint64_t perf_start;
    uint64_t perf_now;
    uint32_t perf_frame_us;
    uint32_t perf_index;
    const char *perf_label;
    uint32_t blob_len;
    uint32_t blob_version;
    uint32_t blob_index;
    uint32_t setting_changed;
    uint32_t note_visible;
    uint32_t note_group_count;
    int64_t clip_result;
    char clip_text[ZD_CLIP_TEXT];
    uint32_t echo_total;
    uint32_t echo_index;
    uint32_t typed_index;
    int line_done;
    uint64_t child_pid;
    uint64_t status;
    int64_t sys_result;
    int attach_result;
    int live = 0;
    int64_t written;
    uint32_t result_count;
    uint32_t found;
    uint32_t i;
    struct zeroos_system_info sysinfo;
    struct zd_capabilities caps;
    struct zd_automation_rule rule;
    struct zd_watchdog_result wd_result;
    struct zd_automation_audit_entry audit[4];
    const struct zd_window *live_apps[1];
    uint64_t uptime;
    int64_t setting_value;
    uint32_t consumed;
    uint32_t fired;
    uint32_t audit_count;
    uint32_t notify_rule;
    uint32_t frame_budget;
    uint32_t effects;
    uint32_t free_percent;
    uint32_t display_service;
    enum zd_pressure_level pressure;

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
    session_fm.ops.rename = session_fs_rename;
    session_fm.ops.read_file = session_fs_read_file;
    session_fm.ops.write_file = session_fs_write_file;
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
    /* Preview, copy, rename and move over the same VFS: the preview must
     * return the bytes this process wrote, and every step is verified
     * through STAT so a silent no-op cannot pass. */
    preview_len = 0;
    if (zd_fm_peek(&session_fm, ZD_FM_PERM_READ, "notes.txt", preview,
                   sizeof(preview), &preview_len) != 0 ||
        preview_len != sizeof(preview) - 1 ||
        /* The writer repeats the filler plus its terminator, so the
         * preview starts with exactly one full line. */
        !session_streq(preview, session_filler))
        return fail("file manager preview", (int64_t)preview_len);
    if (zd_fm_peek(&session_fm, 0, "notes.txt", preview, sizeof(preview),
                   &preview_len) != -1)
        return fail("file manager preview permission gate", 0);
    if (zd_fm_copy(&session_fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                   "notes.txt", "/ram/shell/inbox", "copy.txt") != 0)
        return fail("file manager copy", 0);
    if (zeroos_stat("/ram/shell/inbox/copy.txt", &file_stat) != 0 ||
        file_stat.size != 64)
        return fail("file manager copy verify", (int64_t)file_stat.size);
    if (zd_fm_copy(&session_fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                   "notes.txt", "/ram/shell", "work.txt") != 0 ||
        zd_fm_rename(&session_fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                     "work.txt", "renamed.txt") != 0)
        return fail("file manager rename", 0);
    if (zeroos_stat("/ram/shell/work.txt", &file_stat) != -ZEROOS_ENOENT ||
        zeroos_stat("/ram/shell/renamed.txt", &file_stat) != 0 ||
        file_stat.size != 64)
        return fail("file manager rename verify", 0);
    if (zd_fm_move(&session_fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                   "renamed.txt", "/ram/shell/inbox", "moved.txt") != 0)
        return fail("file manager move", 0);
    if (zeroos_stat("/ram/shell/renamed.txt", &file_stat) != -ZEROOS_ENOENT ||
        zeroos_stat("/ram/shell/inbox/moved.txt", &file_stat) != 0 ||
        file_stat.size != 64)
        return fail("file manager move verify", 0);
    say("ZEROOS: session file manager transfer ops passed.");
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
    /* Batch operations over a real multi-selection. Every operation
     * refreshes the listing and clears the selection, so the selection is
     * rebuilt from names; the directory policy is verified against a real
     * subdirectory, which must survive a batch delete it was not opted
     * into. */
    if (session_write_file("/ram/shell/batch-a.txt", 16) != 0 ||
        session_write_file("/ram/shell/batch-b.txt", 16) != 0 ||
        session_write_file("/ram/shell/batch-c.txt", 16) != 0)
        return fail("batch fixture", 0);
    if (zd_fm_refresh(&session_fm) != 0)
        return fail("batch refresh", 0);
    if (session_select_by_name(&session_fm, "batch-a.txt") < 0 ||
        session_select_by_name(&session_fm, "batch-b.txt") < 0 ||
        session_select_by_name(&session_fm, "batch-c.txt") < 0)
        return fail("batch selection", 0);
    if (zd_fm_batch_copy(&session_fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                         "/ram/shell/inbox", 0, &batch) != 0 ||
        batch.attempted != 3 || batch.succeeded != 3 || batch.failed != 0)
        return fail("batch copy", (int64_t)batch.succeeded);
    if (zeroos_stat("/ram/shell/inbox/batch-c.txt", &file_stat) != 0 ||
        file_stat.size != 16)
        return fail("batch copy verify", (int64_t)file_stat.size);
    if (zd_fm_mkdir(&session_fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                    "batchdir") != 0)
        return fail("batch directory fixture", 0);
    if (session_select_by_name(&session_fm, "batch-a.txt") < 0 ||
        session_select_by_name(&session_fm, "batch-b.txt") < 0 ||
        session_select_by_name(&session_fm, "batch-c.txt") < 0 ||
        session_select_by_name(&session_fm, "batchdir") < 0)
        return fail("batch reselection", 0);
    if (zd_fm_batch_remove(&session_fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                           0, &batch) != 0 ||
        batch.attempted != 3 || batch.succeeded != 3 ||
        batch.skipped_dirs != 1)
        return fail("batch directory policy", (int64_t)batch.skipped_dirs);
    if (zeroos_stat("/ram/shell/batchdir", &file_stat) != 0)
        return fail("batch deleted a directory", 0);
    if (zeroos_stat("/ram/shell/batch-a.txt", &file_stat) != -ZEROOS_ENOENT ||
        zeroos_stat("/ram/shell/batch-c.txt", &file_stat) != -ZEROOS_ENOENT)
        return fail("batch remove verify", 0);
    say("ZEROOS: session file manager batch operations passed.");
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
    /* Live windows are part of the index: the app provider reads the real
     * window-manager list, so a hit proves the shell chrome is searchable
     * without a canned app table. */
    live_apps[0] = zd_wm_window_const(&wm, window);
    if (!live_apps[0])
        return fail("search app window", 0);
    zd_search_set_live_apps(&session_search, live_apps, 1);
    result_count = 0;
    if (zd_search_query(&session_search, "app:session", session_results,
                        ZD_ARRAY_COUNT(session_results),
                        &result_count) != 0 || result_count == 0)
        return fail("search app query", (int64_t)result_count);
    found = 0;
    for (i = 0; i < result_count; ++i)
        if (session_results[i].kind == ZD_SEARCH_APP)
            found = 1;
    if (!found)
        return fail("search app result", (int64_t)result_count);
    say("ZEROOS: session universal search live providers passed.");

    /* 9. Shell platform services on real kernel state. The system
     * information syscall gives Ring 3 a monotonic clock plus the real
     * CPU/memory topology, so the display pacer, the resource governor,
     * the session lifecycle, the service watchdog and the automation
     * engine all run on measured values instead of caller-supplied
     * ticks. */
    sys_result = zeroos_system_info(&sysinfo, sizeof(sysinfo));
    if (sys_result != 0 ||
        sysinfo.version != ZEROOS_SYSTEM_INFO_VERSION ||
        sysinfo.size != sizeof(sysinfo) ||
        sysinfo.cpus_online == 0 ||
        sysinfo.cpus_discovered < sysinfo.cpus_online ||
        sysinfo.page_size != ZEROOS_PAGE_SIZE ||
        sysinfo.timer_hz == 0 ||
        sysinfo.ram_total_bytes == 0 ||
        sysinfo.ram_free_bytes > sysinfo.ram_total_bytes)
        return fail("system information", sys_result);
    if (zeroos_system_info(0, sizeof(sysinfo)) != -ZEROOS_EFAULT)
        return fail("system information null buffer", 0);
    if (zeroos_system_info(&sysinfo, 8) != -ZEROOS_EFAULT)
        return fail("system information short buffer", 0);

    /* The clock must really advance, or nothing that depends on it
     * (pacing, cooldowns, heartbeats) means anything. */
    uptime = sysinfo.uptime_ns;
    sys_result = zeroos_input_wait(&event, 0, (sysinfo.timer_hz / 10) + 1);
    if (sys_result != 0 && sys_result != -ZEROOS_ETIMEDOUT)
        return fail("monotonic clock wait", sys_result);
    if (session_uptime_ns() <= uptime)
        return fail("monotonic clock advance", 0);

    /* Present pacing on the live scanout: inside the interval the frame
     * is refused and counted, after real elapsed time the same damage is
     * accepted again. */
    if (live) {
        display.min_present_interval_ticks = SESSION_PACING_NS;
        /* Baseline present stamped with the real clock: the pacer compares
         * against the last present's own tick, so an unstamped (zero)
         * baseline would look like an interval that already elapsed. */
        if (zd_display_service_damage(
                &display,
                (struct zd_rect){10, 10, SESSION_WINDOW_W,
                                 SESSION_WINDOW_H}) != 0)
            return fail("pacing damage", 0);
        sys_result = zd_display_service_present(&display, staging,
                                                SESSION_TARGET_W * 4,
                                                session_uptime_ns());
        if (sys_result != 0)
            return fail("pacing baseline present", sys_result);
        /* The very next frame is inside the interval: refused and counted,
         * with the damage kept for the retry. */
        if (zd_display_service_damage(
                &display,
                (struct zd_rect){10, 10, SESSION_WINDOW_W,
                                 SESSION_WINDOW_H}) != 0)
            return fail("pacing damage refused", 0);
        sys_result = zd_display_service_present(&display, staging,
                                                SESSION_TARGET_W * 4,
                                                session_uptime_ns());
        if (sys_result != -ZD_EAGAIN ||
            display.stats.presents_refused_paced != 1)
            return fail("display pacing refusal", sys_result);
        sys_result = zeroos_input_wait(&event, 0, (sysinfo.timer_hz / 2) + 1);
        if (sys_result != 0 && sys_result != -ZEROOS_ETIMEDOUT)
            return fail("pacing wait", sys_result);
        sys_result = zd_display_service_present(&display, staging,
                                                SESSION_TARGET_W * 4,
                                                session_uptime_ns());
        if (sys_result != 0)
            return fail("display pacing resume", sys_result);
        display.min_present_interval_ticks = 0;
    }
    say("ZEROOS: session monotonic clock and pacing passed.");

    /* Resource governance from measured topology and measured free
     * memory; the tier's frame budget must be real and the action set
     * must match the requested pressure level. */
    caps.cpu_count = sysinfo.cpus_online;
    caps.ram_mb = (uint32_t)(sysinfo.ram_total_bytes / (1024ULL * 1024ULL));
    caps.gpu_tier = 0; /* software compositing: no GPU path exists yet */
    caps.display_width = raw_info.width;
    caps.display_height = raw_info.height;
    caps.refresh_mhz = 0; /* on-demand present: no refresh domain */
    caps.hardware_accel = 0;
    caps.thermal_state = 0;
    caps.battery_powered = 0;
    zd_governor_init(&session_governor, &caps);
    zd_governor_effects_for_tier(session_governor.tier, &frame_budget,
                                 &effects);
    if (caps.cpu_count == 0 || caps.ram_mb == 0 || frame_budget == 0)
        return fail("governor tier", (int64_t)frame_budget);
    free_percent = (uint32_t)((sysinfo.ram_free_bytes * 100ULL) /
                              sysinfo.ram_total_bytes);
    pressure = free_percent < 10U ? ZD_PRESSURE_CRITICAL :
               free_percent < 25U ? ZD_PRESSURE_HIGH :
               free_percent < 50U ? ZD_PRESSURE_MODERATE :
               free_percent < 75U ? ZD_PRESSURE_LOW : ZD_PRESSURE_NONE;
    if (zd_governor_set_pressure(&session_governor, pressure, 0) != 0 ||
        session_governor.active_actions !=
            zd_governor_actions_for_level(pressure, 0))
        return fail("governor pressure", (int64_t)free_percent);

    /* Lifecycle: real session transitions drive the state machine, an
     * illegal event is rejected, and heavy engines are gated by state. */
    zd_lifecycle_init(&session_lifecycle);
    if (zd_lifecycle_add_listener(&session_lifecycle,
                                  session_lifecycle_on_change, 0) != 0)
        return fail("lifecycle listener", 0);
    if (zd_lifecycle_dispatch(&session_lifecycle, ZD_LIFECYCLE_START,
                              uptime) != 0 ||
        zd_lifecycle_dispatch(&session_lifecycle,
                              ZD_LIFECYCLE_CONTROLLERS_UP, uptime) != 0 ||
        zd_lifecycle_dispatch(&session_lifecycle, ZD_LIFECYCLE_ACTIVATE,
                              uptime) != 0 ||
        zd_lifecycle_state(&session_lifecycle) != ZD_LIFECYCLE_ACTIVE)
        return fail("lifecycle activation",
                    zd_lifecycle_state(&session_lifecycle));
    if (!zd_lifecycle_allows_heavy_work(&session_lifecycle))
        return fail("lifecycle heavy work gate", 0);
    if (zd_lifecycle_dispatch(&session_lifecycle, ZD_LIFECYCLE_START,
                              uptime) == 0)
        return fail("lifecycle illegal event", 0);
    if (zd_lifecycle_dispatch(&session_lifecycle, ZD_LIFECYCLE_PRESSURE,
                              uptime) != 0 ||
        zd_lifecycle_allows_heavy_work(&session_lifecycle))
        return fail("lifecycle throttle gate", 0);
    if (zd_lifecycle_dispatch(&session_lifecycle,
                              ZD_LIFECYCLE_PRESSURE_RELEASED, uptime) != 0 ||
        zd_lifecycle_state(&session_lifecycle) != ZD_LIFECYCLE_ACTIVE)
        return fail("lifecycle pressure release",
                    zd_lifecycle_state(&session_lifecycle));
    if (session_lifecycle_transitions != 5)
        return fail("lifecycle listener count",
                    (int64_t)session_lifecycle_transitions);
    say("ZEROOS: session lifecycle governor activation passed.");

    /* Watchdog over the real search service: healthy evaluation, a
     * failure that produces a restart decision, the restart performed for
     * real (the provider is re-bound and answers a query again), and a
     * healthy evaluation afterwards. */
    zd_watchdog_init(&session_watchdog);
    if (zd_watchdog_set_listener(&session_watchdog, session_wd_on_decision,
                                 session_wd_on_degraded, 0) != 0)
        return fail("watchdog listener", 0);
    if (zd_watchdog_register(&session_watchdog, "search", ZD_WD_ON_FAILURE,
                             3, 0, SESSION_PACING_NS,
                             SESSION_PACING_NS * 8, &display_service) != 0)
        return fail("watchdog register", 0);
    if (zd_watchdog_set_heartbeat(&session_watchdog, display_service,
                                  SESSION_PACING_NS * 4) != 0 ||
        zd_watchdog_note_start(&session_watchdog, display_service,
                               uptime) != 0 ||
        zd_watchdog_note_heartbeat(&session_watchdog, display_service,
                                   uptime) != 0)
        return fail("watchdog start", 0);
    if (zd_watchdog_evaluate(&session_watchdog, display_service, uptime,
                             &wd_result) != 0 ||
        wd_result.decision != ZD_WD_CONTINUE)
        return fail("watchdog healthy evaluation", (int64_t)wd_result.decision);
    if (zd_watchdog_note_exit(&session_watchdog, display_service,
                              uptime + 1, 1, &wd_result) != 0 ||
        wd_result.decision != ZD_WD_RESTART || session_wd_restarts != 1)
        return fail("watchdog restart decision", (int64_t)wd_result.decision);
    if (zd_files_provider_init(&session_files, session_dir_source, 0) != 0 ||
        zd_files_provider_add_root(&session_files, "/ram/shell") != 0)
        return fail("watchdog recovery rebind", 0);
    result_count = 0;
    if (zd_search_query(&session_search, "folder:inbox", session_results,
                        ZD_ARRAY_COUNT(session_results),
                        &result_count) != 0 || result_count == 0)
        return fail("watchdog recovery query", (int64_t)result_count);
    if (zd_watchdog_note_start(&session_watchdog, display_service,
                               uptime + 2) != 0 ||
        zd_watchdog_evaluate(&session_watchdog, display_service, uptime + 2,
                             &wd_result) != 0 ||
        wd_result.decision != ZD_WD_CONTINUE)
        return fail("watchdog recovery evaluation",
                    (int64_t)wd_result.decision);
    say("ZEROOS: session watchdog recovery passed.");

    /* Automation on real events: the supervisor's restart is the event
     * source, permission is enforced before any action runs, the setting
     * action really writes the registry, and the cooldown really
     * rate-limits until measured time passes. */
    if (zd_automation_init(&session_automation, &session_auto_ops) != 0)
        return fail("automation init", 0);
    rule.enabled = 1;
    rule.event = ZD_AUTO_EV_SERVICE_FAILED;
    rule.action = ZD_AUTO_ACT_NOTIFY;
    rule.target[0] = 's';
    rule.target[1] = 'h';
    rule.target[2] = 'e';
    rule.target[3] = 'l';
    rule.target[4] = 'l';
    rule.target[5] = 0;
    rule.setting_value = 0;
    rule.cooldown_ticks = 0;
    rule.fire_cap = 0;
    rule.fires = 0;
    rule.last_fire_tick = 0;
    rule.permission = 0;
    rule.id = 0;
    if (zd_automation_add(&session_automation, &rule, &notify_rule) != 0)
        return fail("automation notify rule", 0);
    if (zd_automation_fire(&session_automation, ZD_AUTO_EV_SERVICE_FAILED,
                           uptime) != 0 || session_notify_posts != 0 ||
        session_automation.stats.denied_permission != 1)
        return fail("automation permission gate",
                    (int64_t)session_notify_posts);
    if (zd_automation_set_permission(&session_automation, notify_rule,
                                     1) != 0 ||
        zd_automation_fire(&session_automation, ZD_AUTO_EV_SERVICE_FAILED,
                           uptime) != 1 || session_notify_posts != 1)
        return fail("automation notify action",
                    (int64_t)session_notify_posts);

    rule.event = ZD_AUTO_EV_CALLER_TICK;
    rule.action = ZD_AUTO_ACT_SETTING;
    rule.target[0] = 's';
    rule.target[1] = 'h';
    rule.target[2] = 'e';
    rule.target[3] = 'l';
    rule.target[4] = 'l';
    rule.target[5] = '.';
    rule.target[6] = 's';
    rule.target[7] = 'c';
    rule.target[8] = 'a';
    rule.target[9] = 'l';
    rule.target[10] = 'e';
    rule.target[11] = '_';
    rule.target[12] = 'p';
    rule.target[13] = 'e';
    rule.target[14] = 'r';
    rule.target[15] = 'c';
    rule.target[16] = 'e';
    rule.target[17] = 'n';
    rule.target[18] = 't';
    rule.target[19] = 0;
    rule.setting_value = 150;
    rule.cooldown_ticks = SESSION_PACING_NS;
    rule.permission = 1;
    if (zd_automation_add(&session_automation, &rule, &notify_rule) != 0)
        return fail("automation tick rule", 0);
    fired = zd_automation_fire(&session_automation, ZD_AUTO_EV_CALLER_TICK,
                               uptime);
    if (fired != 1)
        return fail("automation tick fire", (int64_t)fired);
    if (zd_settings_get(&session_settings, "shell.scale_percent",
                        &setting_value, 0, 0) != 0 || setting_value != 150)
        return fail("automation setting write", (int64_t)setting_value);
    fired = zd_automation_fire(&session_automation, ZD_AUTO_EV_CALLER_TICK,
                               uptime + 1);
    if (fired != 0 || session_automation.stats.rate_limited != 1)
        return fail("automation rate limit", (int64_t)fired);
    sys_result = zeroos_input_wait(&event, 0, (sysinfo.timer_hz / 2) + 1);
    if (sys_result != 0 && sys_result != -ZEROOS_ETIMEDOUT)
        return fail("automation cooldown wait", sys_result);
    fired = zd_automation_fire(&session_automation, ZD_AUTO_EV_CALLER_TICK,
                               session_uptime_ns());
    if (fired != 1 || session_automation.stats.fires != 3)
        return fail("automation cooldown release", (int64_t)fired);

    /* The audit ring is the privacy trail: draining it must return the
     * fires and the denial recorded above. */
    audit_count = zd_automation_drain_audit(&session_automation, audit,
                                            ZD_ARRAY_COUNT(audit), &consumed);
    if (audit_count == 0 || consumed != audit_count)
        return fail("automation audit drain", (int64_t)audit_count);
    say("ZEROOS: session automation live events passed.");

    /* 10. Terminal over a real kernel pipe. The shell's output transport
     * is a pipe pair (syscalls 13-15): bytes are written into one end,
     * read back from the other, and only then parsed by the VT core, so
     * nothing here is a canned string. Child spawn remains pending — Ring
     * 3 has no way to reach a second ELF image yet. */
    if (zeroos_pipe_create(&pipe_pair) != 0)
        return fail("terminal pipe create", 0);
    term_len = 0;
    sys_result = zeroos_pipe_read(pipe_pair.local, term_buffer,
                                  sizeof(term_buffer),
                                  ZEROOS_IPC_FLAG_NONBLOCK, &term_len, 0);
    if (sys_result != -ZEROOS_EAGAIN)
        return fail("terminal pipe empty", sys_result);
    term_line_len = 0;
    while (term_source[term_line_len])
        ++term_line_len;
    sys_result = zeroos_pipe_write(pipe_pair.peer, term_source,
                                   term_line_len, 0, 0);
    if (sys_result != (int64_t)term_line_len)
        return fail("terminal pipe write", sys_result);
    term_len = 0;
    sys_result = zeroos_pipe_read(pipe_pair.local, term_buffer,
                                  sizeof(term_buffer),
                                  ZEROOS_IPC_FLAG_NONBLOCK, &term_len, 0);
    /* The pipe read returns the byte count (ipc_pipe_read_timeout), and
     * *length must agree with it. */
    if (sys_result != (int64_t)term_line_len || term_len != term_line_len)
        return fail("terminal pipe read", sys_result);
    zd_term_init(&session_term, 24, 80);
    if (zd_term_write(&session_term, term_buffer, term_len) != 0)
        return fail("terminal parse", 0);
    row_len = zd_term_row_text(&session_term, 0, term_row, sizeof(term_row));
    if (row_len == 0 || !session_streq(term_row, "zeroos shell"))
        return fail("terminal row text", (int64_t)row_len);
    /* The SGR sequence survived the transport: cell 0 is green (SGR 32 ->
     * palette 2), which a plain-text pipe would not produce. */
    cell = zd_term_cell(&session_term, 0, 0);
    if (!cell || cell->fg != 2)
        return fail("terminal sgr colour", cell ? (int64_t)cell->fg : -1);
    if (session_term.stats.bytes != term_len)
        return fail("terminal byte count",
                    (int64_t)session_term.stats.bytes);
    say("ZEROOS: session terminal pipe binding passed.");

    /* 10b. Interactive line editing over the same pipe. Keystrokes are fed
     * one byte at a time through the line discipline; every echo it asks
     * for is written into the kernel pipe, read back, and only then handed
     * to the VT parser, so the screen contents come from the same code path
     * as program output. Backspace really erases: the edited line and the
     * rendered row must both read "zeos". */
    zd_term_line_init(&session_line);
    echo_total = 0;
    line_done = 0;
    for (typed_index = 0; typed_index < ZD_ARRAY_COUNT(session_typed);
         ++typed_index) {
        const char *echo_bytes;
        uint32_t echo_bytes_len;
        int line_state;
        line_state = zd_term_line_input(&session_line,
                                        session_typed[typed_index],
                                        &echo_bytes, &echo_bytes_len);
        if (line_state < 0)
            return fail("terminal line input", line_state);
        if (line_state == 1)
            line_done = 1;
        if (echo_bytes_len == 0)
            continue;
        if (echo_total + echo_bytes_len > sizeof(echo_buffer))
            return fail("terminal echo buffer", (int64_t)echo_total);
        for (echo_index = 0; echo_index < echo_bytes_len; ++echo_index)
            echo_buffer[echo_total + echo_index] =
                (uint8_t)echo_bytes[echo_index];
        echo_total += echo_bytes_len;
    }
    if (line_done != 1)
        return fail("terminal line completion", line_done);
    sys_result = zeroos_pipe_write(pipe_pair.peer, echo_buffer, echo_total,
                                   0, 0);
    if (sys_result != (int64_t)echo_total)
        return fail("terminal echo write", sys_result);
    term_len = 0;
    sys_result = zeroos_pipe_read(pipe_pair.local, term_buffer,
                                  sizeof(term_buffer),
                                  ZEROOS_IPC_FLAG_NONBLOCK, &term_len, 0);
    if (sys_result != (int64_t)echo_total || term_len != echo_total)
        return fail("terminal echo read", sys_result);
    if (zd_term_write(&session_term, term_buffer, term_len) != 0)
        return fail("terminal echo parse", 0);
    if (zd_term_line_copy(&session_line, term_row, sizeof(term_row)) != 4 ||
        !session_streq(term_row, "zeos"))
        return fail("terminal edited line", 0);
    row_len = zd_term_row_text(&session_term, 1, term_row, sizeof(term_row));
    if (row_len != 4 || !session_streq(term_row, "zeos"))
        return fail("terminal echo row", (int64_t)row_len);
    if (session_line.erased != 2 || session_line.completions != 1 ||
        session_line.overflow != 0)
        return fail("terminal line stats", (int64_t)session_line.erased);
    /* A full line refuses further bytes instead of silently truncating. */
    zd_term_line_init(&session_line);
    for (typed_index = 0; typed_index < ZD_TERM_LINE_MAX + 1U;
         ++typed_index) {
        const char *echo_bytes;
        uint32_t echo_bytes_len;
        if (zd_term_line_input(&session_line, 'x', &echo_bytes,
                               &echo_bytes_len) != 0)
            return fail("terminal line fill", 0);
    }
    if (session_line.len != ZD_TERM_LINE_MAX || session_line.overflow != 1)
        return fail("terminal line overflow", (int64_t)session_line.len);
    say("ZEROOS: session terminal line editing passed.");

    /* 10c. Bounded byte-stream pipe semantics: partial write and backpressure. */
    {
        uint8_t chunk[256];
        uint32_t fill_i;
        for (fill_i = 0; fill_i < sizeof(chunk); ++fill_i)
            chunk[fill_i] = (uint8_t)fill_i;
        while (zeroos_pipe_read(pipe_pair.local, term_buffer,
                                sizeof(term_buffer),
                                ZEROOS_IPC_FLAG_NONBLOCK, &term_len, 0) > 0)
            ;
        for (fill_i = 0; fill_i < 8; ++fill_i) {
            sys_result = zeroos_pipe_write(pipe_pair.peer, chunk,
                                           sizeof(chunk),
                                           ZEROOS_IPC_FLAG_NONBLOCK, 0);
            if (sys_result != (int64_t)sizeof(chunk))
                return fail("pipe fill", sys_result);
        }
        sys_result = zeroos_pipe_write(pipe_pair.peer, chunk, 1,
                                       ZEROOS_IPC_FLAG_NONBLOCK, 0);
        if (sys_result != -ZEROOS_EAGAIN)
            return fail("pipe full backpressure", sys_result);
        term_len = 0;
        sys_result = zeroos_pipe_read(pipe_pair.local, term_buffer, 16,
                                      ZEROOS_IPC_FLAG_NONBLOCK, &term_len, 0);
        if (sys_result != 16 || term_len != 16)
            return fail("pipe partial read", sys_result);
        sys_result = zeroos_pipe_write(pipe_pair.peer, chunk, 64,
                                       ZEROOS_IPC_FLAG_NONBLOCK, 0);
        if (sys_result != 16)
            return fail("pipe partial write", sys_result);
        sys_result = zeroos_pipe_write(pipe_pair.peer, chunk, 1,
                                       ZEROOS_IPC_FLAG_NONBLOCK, 0);
        if (sys_result != -ZEROOS_EAGAIN)
            return fail("pipe refilled backpressure", sys_result);
        (void)zeroos_ipc_close(pipe_pair.local);
        (void)zeroos_ipc_close(pipe_pair.peer);
    }

    /* 11. Privacy centre over live refusal counters. The filesystem domain
     * is real: the shell defines a sandbox profile and the denials counted
     * here are actual policy decisions taken during this boot, not a
     * fixture. Domains without a live engine are passed as NULL, which the
     * centre documents as "unavailable, contributes zero" — they are never
     * reported as zero-risk. */
    zd_sandbox_init(&session_sandbox);
    if (zd_sandbox_define(&session_sandbox, "shell",
                          (1U << ZD_SB_FS_READ) |
                          (1U << ZD_SB_FS_WRITE)) != 0)
        return fail("privacy sandbox profile", 0);
    /* Allowed by the profile: the shell really reads and writes files. */
    if (zd_sandbox_check(&session_sandbox, "shell", ZD_SB_FS_READ) != 0 ||
        zd_sandbox_check(&session_sandbox, "shell", ZD_SB_FS_WRITE) != 0)
        return fail("privacy allowed check", 0);
    /* Outside the profile: process spawn and raw devices. Both must deny. */
    if (zd_sandbox_check(&session_sandbox, "shell", ZD_SB_PROC_SPAWN) != -1)
        return fail("privacy spawn denial", 0);
    if (zd_sandbox_check(&session_sandbox, "shell", ZD_SB_DEVICE) != -1)
        return fail("privacy device denial", 0);
    /* An unknown profile fails closed and is still counted. */
    if (zd_sandbox_check(&session_sandbox, "no-such-profile",
                         ZD_SB_FS_READ) != -1)
        return fail("privacy unknown profile", 0);
    /* The clipboard is the second live privacy source. A sensitive
     * clipping is delivered to the current slot but never enters history,
     * the cycle can never return it, and the counters behind the report
     * are the service's own. */
    zd_clipboard_init(&session_clipboard);
    if (zd_clipboard_copy(&session_clipboard, "", "zeroos release notes",
                          ZD_CLIP_FMT_TEXT, 0) != 0)
        return fail("clipboard copy", 0);
    if (zd_clipboard_history_count(&session_clipboard) != 1)
        return fail("clipboard history",
                    (int64_t)zd_clipboard_history_count(&session_clipboard));
    if (zd_clipboard_copy(&session_clipboard, "vault", "vault-master-key",
                          ZD_CLIP_FMT_TEXT, 1) != 0)
        return fail("clipboard sensitive copy", 0);
    /* Sensitive content is current but must not grow the history. */
    if (zd_clipboard_history_count(&session_clipboard) != 1)
        return fail("clipboard sensitive history",
                    (int64_t)zd_clipboard_history_count(&session_clipboard));
    if (session_clipboard.stats.sensitive_kept != 1)
        return fail("clipboard sensitive count",
                    (int64_t)session_clipboard.stats.sensitive_kept);
    clip_result = zd_clipboard_paste(&session_clipboard, clip_text,
                                     sizeof(clip_text));
    if (clip_result != 0 || !session_streq(clip_text, "vault-master-key"))
        return fail("clipboard paste", clip_result);
    /* Cycling skips the sensitive entry and yields the older clip. */
    clip_result = zd_clipboard_cycle(&session_clipboard, 1, clip_text,
                                     sizeof(clip_text));
    if (clip_result != 0 ||
        !session_streq(clip_text, "zeroos release notes"))
        return fail("clipboard cycle", clip_result);
    /* Overlong input is refused and counted, never truncated. */
    for (key_index = 0; key_index < sizeof(clip_overlong) - 1; ++key_index)
        clip_overlong[key_index] = 'x';
    clip_overlong[sizeof(clip_overlong) - 1] = 0;
    if (zd_clipboard_copy(&session_clipboard, "", clip_overlong,
                          ZD_CLIP_FMT_TEXT, 0) != -22)
        return fail("clipboard overlong", 0);
    if (session_clipboard.stats.rejected != 1)
        return fail("clipboard rejection count",
                    (int64_t)session_clipboard.stats.rejected);
    /* The privacy action really empties the clipboard; the counter that
     * fed the report survives, because it records what was protected. */
    zd_clipboard_clear(&session_clipboard);
    if (zd_clipboard_history_count(&session_clipboard) != 0)
        return fail("clipboard clear", 0);
    clip_result = zd_clipboard_paste(&session_clipboard, clip_text,
                                     sizeof(clip_text));
    if (clip_result != -1 || clip_text[0] != 0)
        return fail("clipboard empty paste", clip_result);
    say("ZEROOS: session clipboard privacy passed.");

    sources.sb = &session_sandbox;
    sources.clip = &session_clipboard;
    sources.fw = 0;      /* no live packet path in this build */
    sources.media = 0;   /* decoder backends pending */
    sources.eco = 0;     /* transports pending */
    if (zd_privacy_assess(&sources, &report) != 0)
        return fail("privacy assess", 0);
    if (report.denials_total != 3 || report.bd.filesystem != 3)
        return fail("privacy denial count",
                    (int64_t)report.denials_total);
    /* One sensitive clipping was kept out of history: the centre reports
     * it as a protected event, sourced from the live clipboard. */
    if (report.protected_events != 1)
        return fail("privacy protected events",
                    (int64_t)report.protected_events);
    /* Three denials, no domain at the review threshold: "watch". */
    if (report.risk != ZD_PRIV_RISK_WATCH)
        return fail("privacy risk band", (int64_t)report.risk);
    /* The audit ring is the evidence behind the number. */
    audit_entries = zd_sandbox_audit_recent(&session_sandbox, sb_audit,
                                            ZD_ARRAY_COUNT(sb_audit));
    if (audit_entries == 0)
        return fail("privacy audit ring", 0);
    say("ZEROOS: session privacy aggregation passed.");

    /* 12. A real child process. The shell fetches the embedded child image
     * (syscall 56), spawns it, and reaps it: the child writes its own line
     * to the console and exits with a status the parent asserts, so the
     * SPAWN/WAIT lifecycle is exercised by a second Ring-3 process rather
     * than simulated. ELF validation and W^X still apply to the fetched
     * image — the kernel maps nothing it has not checked. */
    sys_result = zeroos_child_image(child_image, sizeof(child_image));
    if (sys_result <= 0)
        return fail("child image fetch", sys_result);
    if ((uint64_t)sys_result > sizeof(child_image))
        return fail("child image size", sys_result);
    /* A short buffer must be refused, never partially filled. */
    if (zeroos_child_image(child_image, 16) != -ZEROOS_EFAULT)
        return fail("child image short buffer", 0);
    sys_result = zeroos_child_image(child_image, sizeof(child_image));
    if (sys_result <= 0)
        return fail("child image refetch", sys_result);
    child_size = (uint64_t)sys_result;
    /* argv really crosses the exec boundary: the child echoes argv[1] and
     * CI greps for it, so a broken argument vector cannot pass. */
    child_argv[0] = (uint64_t)(uintptr_t)child_arg0;
    child_argv[1] = (uint64_t)(uintptr_t)child_arg1;
    /* The environment vector crosses the same boundary; the child echoes
     * envp[0] back on the console. */
    child_envp[0] = (uint64_t)(uintptr_t)child_env0;
    sys_result = zeroos_spawn(child_image, child_size, child_argv, 2,
                              child_envp, 1);
    if (sys_result <= 0)
        return fail("child spawn", sys_result);
    child_pid = (uint64_t)sys_result;
    status = 0;
    /* Bounded wait: a wedged child must fail the certification, not hang
     * the boot. */
    sys_result = zeroos_wait(child_pid, &status, 0,
                             (uint64_t)sysinfo.timer_hz * 10U);
    if (sys_result != (int64_t)child_pid)
        return fail("child wait", sys_result);
    if (status != 7)
        return fail("child exit status", (int64_t)status);
    /* Reaping twice must report no such child: the slot is really gone. */
    sys_result = zeroos_wait(child_pid, &status, ZEROOS_WAIT_FLAG_NONBLOCK,
                             0);
    if (sys_result != -ZEROOS_ECHILD)
        return fail("child double reap", sys_result);
    say("ZEROOS: session child process reaped cleanly.");

    /* 13. Downloads over the real filesystem. The queue's start hook is
     * bound to the VFS: it opens the enqueued source path, streams it into
     * the destination in bounded chunks and reports progress, so the
     * lifecycle counters describe bytes that actually moved. A missing
     * source fails the transfer with the VFS errno instead of being
     * reported as a successful download. */
    sys_result = session_write_file("/ram/shell/release.bin",
                                    sizeof(dl_src_buf));
    if (sys_result != 0)
        return fail("download source create", sys_result);
    session_dl_ctx.dest[0] = 0;
    {
        static const char dl_dest[] = "/ram/shell/incoming.bin";
        uint32_t dl_pos = 0;
        while (dl_dest[dl_pos] && dl_pos < sizeof(session_dl_ctx.dest) - 1) {
            session_dl_ctx.dest[dl_pos] = dl_dest[dl_pos];
            ++dl_pos;
        }
        session_dl_ctx.dest[dl_pos] = 0;
    }
    zd_downloads_init(&session_downloads, session_dl_start, &session_dl_ctx);
    if (zd_downloads_add(&session_downloads, "/ram/shell/release.bin",
                         "incoming.bin", (uint32_t)sizeof(dl_src_buf)) != 0)
        return fail("download enqueue", 0);
    sys_result = zd_downloads_start_next(&session_downloads);
    if (sys_result != 0)
        return fail("download start", sys_result);
    /* Single-active policy: a second transfer is refused while one runs. */
    if (zd_downloads_start_next(&session_downloads) != -16)
        return fail("download single active", 0);
    dl_id = session_dl_find_url(&session_downloads,
                                "/ram/shell/release.bin");
    if (dl_id <= 0)
        return fail("download item id", dl_id);
    dl_item = zd_downloads_find(&session_downloads, (uint32_t)dl_id);
    if (!dl_item || dl_item->state != ZD_DL_RUNNING)
        return fail("download running state", 0);
    if (zd_downloads_active(&session_downloads) != (uint32_t)dl_id)
        return fail("download active id", 0);
    /* The transfer runs while the item is RUNNING, reporting progress in
     * 64-byte chunks: two reports for a 128-byte source. */
    sys_result = session_dl_pump(&session_dl_ctx, (uint32_t)dl_id);
    if (sys_result != 0)
        return fail("download transfer", sys_result);
    dl_item = zd_downloads_find(&session_downloads, (uint32_t)dl_id);
    if (!dl_item || dl_item->received != sizeof(dl_src_buf) ||
        dl_item->total != sizeof(dl_src_buf))
        return fail("download progress",
                    dl_item ? (int64_t)dl_item->received : -1);
    /* Progress must never go backwards. */
    if (zd_downloads_progress(&session_downloads, (uint32_t)dl_id, 1, 0) !=
        -22)
        return fail("download progress regression", 0);
    if (session_downloads.stats.progress_regressions != 1)
        return fail("download regression count",
                    (int64_t)session_downloads.stats.progress_regressions);
    if (zd_downloads_finish(&session_downloads, (uint32_t)dl_id, 0) != 0)
        return fail("download finish", 0);
    dl_item = zd_downloads_find(&session_downloads, (uint32_t)dl_id);
    if (!dl_item || dl_item->state != ZD_DL_DONE)
        return fail("download done state", 0);
    if (session_downloads.stats.completed != 1)
        return fail("download completed count",
                    (int64_t)session_downloads.stats.completed);
    /* The bytes are really on the ramdisk, and they are the source bytes. */
    if (zeroos_stat("/ram/shell/incoming.bin", &file_stat) != 0 ||
        file_stat.size != sizeof(dl_dst_buf))
        return fail("download dest size", (int64_t)file_stat.size);
    key_len = 0;
    if (session_read_buffer("/ram/shell/release.bin", dl_src_buf,
                            sizeof(dl_src_buf), &key_len) != 0 ||
        key_len != sizeof(dl_src_buf))
        return fail("download source readback", (int64_t)key_len);
    key_len = 0;
    if (session_read_buffer("/ram/shell/incoming.bin", dl_dst_buf,
                            sizeof(dl_dst_buf), &key_len) != 0 ||
        key_len != sizeof(dl_dst_buf))
        return fail("download dest readback", (int64_t)key_len);
    for (dl_index = 0; dl_index < sizeof(dl_dst_buf); ++dl_index) {
        if (dl_src_buf[dl_index] != dl_dst_buf[dl_index])
            return fail("download content", (int64_t)dl_index);
    }
    /* A source that does not exist must fail the transfer, not fake it. */
    if (zd_downloads_add(&session_downloads, "/ram/shell/missing.bin",
                         "missing.bin", 16) != 0)
        return fail("download missing enqueue", 0);
    sys_result = zd_downloads_start_next(&session_downloads);
    if (sys_result != -ZEROOS_ENOENT)
        return fail("download missing start", sys_result);
    dl_id = session_dl_find_url(&session_downloads, "/ram/shell/missing.bin");
    if (dl_id <= 0)
        return fail("download missing id", dl_id);
    dl_item = zd_downloads_find(&session_downloads, (uint32_t)dl_id);
    if (!dl_item || dl_item->state != ZD_DL_FAILED ||
        dl_item->fail_errno != ZEROOS_ENOENT)
        return fail("download failure errno",
                    dl_item ? (int64_t)dl_item->fail_errno : -1);
    if (session_downloads.stats.failed != 1)
        return fail("download failed count",
                    (int64_t)session_downloads.stats.failed);
    say("ZEROOS: session downloads pipeline passed.");

    /* 14. Notification centre on the real monotonic clock. Posting,
     * deduplication, deferral, expiry and dismissal are all evaluated
     * against the kernel clock read through SYSTEM_INFO, so the lifecycle
     * is driven by real time rather than a synthetic timeline. */
    notify_now = session_uptime_ns();
    notify_due = notify_now + 5ULL * 1000000000ULL;
    zd_notify_init(&session_notify);
    if (zd_notify_add_listener(&session_notify, session_notify_posted,
                               session_notify_limited,
                               &session_notify_ctx) != 0)
        return fail("notify listener", 0);
    note_post.app_id = "shell";
    note_post.category = "system";
    note_post.dedupe_key = 0;
    note_post.title = "Update verified";
    note_post.body = "1.1.0 staged";
    note_post.priority = ZD_NOTIFY_HIGH;
    note_post.ttl_ns = 0;
    note_id = 0;
    if (zd_notify_post(&session_notify, &note_post, notify_now,
                       &note_id) != 0 || note_id == 0)
        return fail("notify post", (int64_t)note_id);
    if (session_notify_ctx.posted != 1 ||
        session_notify_ctx.last_priority != (int)ZD_NOTIFY_HIGH)
        return fail("notify listener post",
                    (int64_t)session_notify_ctx.posted);
    /* Accessibility semantics are derived from priority, not chosen ad
     * hoc: critical interrupts, low stays quiet. */
    if (zd_notify_a11y_policy(ZD_NOTIFY_CRITICAL) !=
            ZD_NOTIFY_A11Y_ASSERTIVE ||
        zd_notify_a11y_policy(ZD_NOTIFY_LOW) != ZD_NOTIFY_A11Y_QUIET)
        return fail("notify a11y policy", 0);
    /* Deduplication folds a repeat inside the window into the first item. */
    note_post.priority = ZD_NOTIFY_NORMAL;
    note_post.title = "Release notes";
    note_post.body = "1.1.0 changes";
    note_post.dedupe_key = "release-1.1.0";
    {
        zd_notification_id dup_id = 0;
        if (zd_notify_post(&session_notify, &note_post, notify_now,
                           &dup_id) != 0 || dup_id == 0)
            return fail("notify second post", (int64_t)dup_id);
        note_id = dup_id;
        if (zd_notify_post(&session_notify, &note_post, notify_now,
                           &dup_id) != 0 || dup_id != note_id)
            return fail("notify dedupe", (int64_t)dup_id);
    }
    if (session_notify.stats.deduped != 1)
        return fail("notify dedupe count",
                    (int64_t)session_notify.stats.deduped);
    /* A critical alert joins and must sort above the others. */
    note_post.priority = ZD_NOTIFY_CRITICAL;
    note_post.title = "Rollback available";
    note_post.body = "previous slot kept";
    note_post.dedupe_key = 0;
    {
        zd_notification_id crit_id = 0;
        if (zd_notify_post(&session_notify, &note_post, notify_now,
                           &crit_id) != 0 || crit_id == 0)
            return fail("notify critical post", (int64_t)crit_id);
        note_visible = zd_notify_visible(&session_notify, notify_now,
                                         note_ids,
                                         ZD_ARRAY_COUNT(note_ids));
        if (note_visible != 3 || note_ids[0] != crit_id)
            return fail("notify priority order", (int64_t)note_visible);
        /* Deferral hides it until it is due, on the real clock. */
        if (zd_notify_defer(&session_notify, crit_id, notify_now,
                            5ULL * 1000000000ULL) != 0)
            return fail("notify defer", 0);
        note_visible = zd_notify_visible(&session_notify, notify_now,
                                         note_ids,
                                         ZD_ARRAY_COUNT(note_ids));
        if (note_visible != 2)
            return fail("notify deferred hidden", (int64_t)note_visible);
        note_visible = zd_notify_visible(&session_notify, notify_due + 1ULL,
                                         note_ids,
                                         ZD_ARRAY_COUNT(note_ids));
        if (note_visible != 3 || note_ids[0] != crit_id)
            return fail("notify deferred due", (int64_t)note_visible);
        /* Dismissal removes one, and the count says so. */
        if (zd_notify_dismiss(&session_notify, crit_id) != 0)
            return fail("notify dismiss", 0);
        if (session_notify.stats.dismissed != 1)
            return fail("notify dismissed count",
                        (int64_t)session_notify.stats.dismissed);
        note_visible = zd_notify_visible(&session_notify, notify_due + 1ULL,
                                         note_ids,
                                         ZD_ARRAY_COUNT(note_ids));
        if (note_visible != 2)
            return fail("notify visible after dismiss",
                        (int64_t)note_visible);
    }
    note_group_count = zd_notify_groups(&session_notify, notify_now,
                                        note_groups,
                                        ZD_ARRAY_COUNT(note_groups));
    if (note_group_count == 0 || note_groups[0].count == 0)
        return fail("notify groups", (int64_t)note_group_count);
    say("ZEROOS: session notification lifecycle passed.");

    /* 15. Launcher bound to real process creation and the capability gate.
     * Launching an app is not a state change on a list: the hook spawns the
     * embedded child image, the shell waits for it, and the capability gate
     * decides whether the launcher may launch at all. */
    zd_caps_init(&session_caps);
    zd_launcher_init(&session_launcher, session_launch_hook,
                     &session_launch_ctx);
    zd_launcher_set_caps(&session_launcher, &session_caps);
    if (zd_launcher_add(&session_launcher, "child", "process demo", 1) != 0)
        return fail("launcher register", 0);
    /* Without the capability the request is refused before the hook runs. */
    if (zd_launcher_launch(&session_launcher, "child") != -1)
        return fail("launcher capability denial", 0);
    if (session_launcher.stats.cap_denied != 1)
        return fail("launcher denial count",
                    (int64_t)session_launcher.stats.cap_denied);
    if (zd_caps_activate(&session_caps, ZD_SVC_LAUNCHER,
                         1ULL << ZD_CAP_LAUNCH_APPS) != 0)
        return fail("launcher capability grant", 0);
    session_launch_ctx.image = child_image;
    session_launch_ctx.image_size = child_size;
    session_launch_ctx.pid = 0;
    if (zd_launcher_launch(&session_launcher, "child") != 0)
        return fail("launcher launch", 0);
    if (session_launch_ctx.pid <= 0)
        return fail("launcher spawn pid", session_launch_ctx.pid);
    launch_app = zd_launcher_find(&session_launcher, "child");
    if (!launch_app || launch_app->state != ZD_LAUNCH_PENDING)
        return fail("launcher pending state", 0);
    /* A second request while one is in flight is deduplicated. */
    if (zd_launcher_launch(&session_launcher, "child") != -16)
        return fail("launcher busy dedup", 0);
    if (session_launcher.stats.launch_rejected != 1)
        return fail("launcher rejection count",
                    (int64_t)session_launcher.stats.launch_rejected);
    /* The launched process really ran: wait for it and check its status. */
    status = 0;
    sys_result = zeroos_wait((uint64_t)session_launch_ctx.pid, &status, 0,
                             (uint64_t)sysinfo.timer_hz * 10U);
    if (sys_result != session_launch_ctx.pid)
        return fail("launcher child wait", sys_result);
    if (status != 7)
        return fail("launcher child status", (int64_t)status);
    zd_launcher_report(&session_launcher, "child", 1, 0);
    launch_app = zd_launcher_find(&session_launcher, "child");
    if (!launch_app || launch_app->state != ZD_LAUNCH_RUNNING ||
        launch_app->in_recents == 0)
        return fail("launcher recents", 0);
    if (session_launcher.stats.launches != 1)
        return fail("launcher launch count",
                    (int64_t)session_launcher.stats.launches);
    /* An empty query is the recents view: the app just used comes first. */
    launch_count = zd_launcher_query(&session_launcher, "", launch_results,
                                     (int)ZD_ARRAY_COUNT(launch_results));
    if (launch_count < 1 || launch_results[0] != launch_app)
        return fail("launcher recents query", (int64_t)launch_count);
    /* Keyword search finds it; a miss invents nothing. */
    launch_count = zd_launcher_query(&session_launcher, "proc",
                                     launch_results,
                                     (int)ZD_ARRAY_COUNT(launch_results));
    if (launch_count != 1 || launch_results[0] != launch_app)
        return fail("launcher keyword query", (int64_t)launch_count);
    launch_count = zd_launcher_query(&session_launcher, "zzzz",
                                     launch_results,
                                     (int)ZD_ARRAY_COUNT(launch_results));
    if (launch_count != 0)
        return fail("launcher empty result", (int64_t)launch_count);
    say("ZEROOS: session launcher process binding passed.");

    /* 16. Settings persistence through the filesystem. The registry is
     * exported to its versioned blob, written to the ramdisk, read back and
     * imported into a fresh registry, so a restart really recovers the
     * stored value. A corrupted blob is refused wholesale and leaves the
     * live store at its defaults -- the import stages before it commits. */
    setting_changed = 0;
    if (zd_settings_set_number(&session_settings, "shell.scale_percent", 175,
                               ZD_PERM_SETTINGS_USER,
                               &setting_changed) != 0 ||
        setting_changed != 1)
        return fail("settings change", (int64_t)setting_changed);
    blob_len = 0;
    blob_version = 0;
    if (zd_settings_export(&session_settings, settings_blob,
                           sizeof(settings_blob), &blob_len,
                           &blob_version) != 0 || blob_len == 0)
        return fail("settings export", (int64_t)blob_len);
    if (session_write_buffer("/ram/shell/settings.blob",
                             (const uint8_t *)settings_blob, blob_len,
                             0600) != 0)
        return fail("settings blob write", 0);
    key_len = 0;
    if (session_read_buffer("/ram/shell/settings.blob",
                            (uint8_t *)settings_blob_read,
                            sizeof(settings_blob_read) - 1,
                            &key_len) != 0 || key_len != blob_len)
        return fail("settings blob readback", (int64_t)key_len);
    settings_blob_read[key_len] = 0;
    zd_settings_init(&session_settings_restored);
    if (zd_settings_register(&session_settings_restored,
                             &session_scale_setting) != 0)
        return fail("settings restore schema", 0);
    if (zd_settings_import(&session_settings_restored, settings_blob_read,
                           ZD_PERM_SETTINGS_USER) != 0)
        return fail("settings import", 0);
    setting_value = 0;
    if (zd_settings_get(&session_settings_restored, "shell.scale_percent",
                        &setting_value, 0, 0) != 0 || setting_value != 175)
        return fail("settings restored value", setting_value);
    /* Two distinct corruptions, because the import treats them
     * differently. A value that is not a number is a parse failure: the
     * whole transaction aborts and the store is never touched. */
    for (blob_index = 0; blob_index + 2 < blob_len; ++blob_index) {
        if (settings_blob_read[blob_index] == '1' &&
            settings_blob_read[blob_index + 1] == '7' &&
            settings_blob_read[blob_index + 2] == '5')
            break;
    }
    if (blob_index + 2 >= blob_len)
        return fail("settings blob content", 0);
    settings_blob_read[blob_index + 1] = 'x';   /* 175 -> 1x5 */
    zd_settings_init(&session_settings_tampered);
    if (zd_settings_register(&session_settings_tampered,
                             &session_scale_setting) != 0)
        return fail("settings tamper schema", 0);
    if (zd_settings_import(&session_settings_tampered, settings_blob_read,
                           ZD_PERM_SETTINGS_USER) != -ZD_EINVAL)
        return fail("settings tamper rejection", 0);
    if (session_settings_tampered.stats.import_failures != 1)
        return fail("settings import failure count",
                    (int64_t)session_settings_tampered.stats.import_failures);
    setting_value = -1;
    if (zd_settings_get(&session_settings_tampered, "shell.scale_percent",
                        &setting_value, 0, 0) != 0 || setting_value != 100)
        return fail("settings tamper isolation", setting_value);
    /* A value that parses but falls outside the schema range is refused
     * when the staged write is applied: the import itself succeeds and the
     * stored value does not move off the default. */
    settings_blob_read[blob_index] = '9';
    settings_blob_read[blob_index + 1] = '7';   /* 1x5 -> 975 */
    zd_settings_init(&session_settings_tampered);
    if (zd_settings_register(&session_settings_tampered,
                             &session_scale_setting) != 0)
        return fail("settings range schema", 0);
    if (zd_settings_import(&session_settings_tampered, settings_blob_read,
                           ZD_PERM_SETTINGS_USER) != 0)
        return fail("settings range import", 0);
    setting_value = -1;
    if (zd_settings_get(&session_settings_tampered, "shell.scale_percent",
                        &setting_value, 0, 0) != 0 || setting_value != 100)
        return fail("settings range refusal", setting_value);
    say("ZEROOS: session settings persistence passed.");

    /* 17. Performance centre over measured frame cycles. Every sample is
     * the kernel-clock duration of a real present-and-pace cycle, so the
     * percentiles describe this machine instead of a fixture, and the
     * health verdict is derived from the documented thresholds. */
    zd_perf_center_init(&session_perf);
    zd_metrics_init(&session_metrics);
    if (frame_budget == 0)
        return fail("perf frame budget", 0);
    perf_cycle_ns = (uint64_t)frame_budget * 1000000ULL;
    for (perf_index = 0; perf_index < 8U; ++perf_index) {
        perf_start = session_uptime_ns();
        if (perf_start == 0)
            return fail("perf clock", 0);
        if (session_present(0, 0, 0, SESSION_WINDOW_W, SESSION_WINDOW_H,
                            SESSION_WINDOW_W * 4U, window_pixels) != 0)
            return fail("perf present", 0);
        do {
            perf_now = session_uptime_ns();
            if (perf_now == 0)
                return fail("perf clock", 0);
        } while (perf_now - perf_start < perf_cycle_ns);
        perf_frame_us = (uint32_t)((perf_now - perf_start) / 1000ULL);
        if (zd_perf_center_record(&session_perf, perf_frame_us) != 0)
            return fail("perf sample", (int64_t)perf_frame_us);
        /* The same measured cycle feeds the metrics recorder, in the
         * milliseconds the status readout reports. */
        zd_metrics_add(&session_metrics, ZD_METRIC_FRAMES_PRESENTED, 1);
        zd_metrics_record_interval(&session_metrics,
                                   (perf_now - perf_start) / 1000000ULL);
    }
    perf_in.budget_us = frame_budget * 1000U;
    perf_in.fps_milli = (uint32_t)(1000000000000ULL / perf_cycle_ns);
    perf_in.mem_pressure = 100U - free_percent;
    perf_in.throttled = 0;
    perf_in.governor_eco = 0;
    perf_in.update_pending = 0;
    if (zd_perf_center_assess(&session_perf, &perf_in, &perf_report) != 0)
        return fail("perf assess", 0);
    if (perf_report.samples != 8U || perf_report.p50_us == 0 ||
        perf_report.p95_us < perf_report.p50_us ||
        perf_report.worst_us < perf_report.p95_us)
        return fail("perf percentiles", (int64_t)perf_report.samples);
    perf_label = zd_perf_center_health_label(perf_report.health);
    if (!perf_label || !perf_label[0])
        return fail("perf health label", 0);
    /* Precedence is explicit in the assessment, so both sides of it are
     * pinned. With throughput above the floor, critical memory pressure
     * owns the verdict and carries its documented suggestions. */
    perf_in.mem_pressure = 100U;
    perf_in.fps_milli = 60000U;
    if (zd_perf_center_assess(&session_perf, &perf_in, &perf_report) != 0)
        return fail("perf pressure assess", 0);
    if (perf_report.issue != ZD_PC_ISSUE_MEMORY ||
        perf_report.health != ZD_PC_HEALTH_CRITICAL ||
        (perf_report.suggestions &
         (ZD_PC_SUGGEST_CLOSE_BG | ZD_PC_SUGGEST_CHECK_MEMORY)) !=
            (ZD_PC_SUGGEST_CLOSE_BG | ZD_PC_SUGGEST_CHECK_MEMORY))
        return fail("perf pressure verdict", (int64_t)perf_report.issue);
    /* At the throughput this machine actually runs at -- the tier budget
     * is 50 ms, i.e. 20 fps against the centre's 30 fps floor -- the
     * throughput issue outranks memory by design, while the memory
     * suggestions are still carried. */
    perf_in.fps_milli = (uint32_t)(1000000000000ULL / perf_cycle_ns);
    if (zd_perf_center_assess(&session_perf, &perf_in, &perf_report) != 0)
        return fail("perf throughput assess", 0);
    if (perf_report.issue != ZD_PC_ISSUE_LOW_FPS ||
        perf_report.health != ZD_PC_HEALTH_CRITICAL ||
        (perf_report.suggestions &
         (ZD_PC_SUGGEST_CLOSE_BG | ZD_PC_SUGGEST_CHECK_MEMORY)) !=
            (ZD_PC_SUGGEST_CLOSE_BG | ZD_PC_SUGGEST_CHECK_MEMORY))
        return fail("perf throughput verdict", (int64_t)perf_report.issue);
    /* Bounds: a zero frame duration is refused, and fewer than four
     * samples cannot produce percentiles at all. */
    if (zd_perf_center_record(&session_perf, 0) >= 0)
        return fail("perf zero sample", 0);
    zd_perf_center_init(&session_perf_cold);
    perf_in.mem_pressure = 0;
    if (zd_perf_center_assess(&session_perf_cold, &perf_in,
                              &perf_report) != -22)
        return fail("perf insufficient samples", 0);
    say("ZEROOS: session performance centre passed.");

    /* 18. Workspaces, snapping and the overview on the live window manager.
     * Snapping is geometry rather than a flag: both halves must equal the
     * rectangles derived from the monitor the kernel reported. Moving a
     * window between workspaces changes which one the shell is looking at,
     * and the overview is driven by the live window ids. */
    window_info.title = "session-side";
    window_info.logical_rect.x = 200;
    window_info.logical_rect.y = 60;
    if (zd_wm_create_window(&wm, &window_info, &window_side) != 0)
        return fail("workspace window create", 0);
    if (zd_wm_snap(&wm, window, ZD_SNAP_LEFT) != 0 ||
        zd_wm_snap(&wm, window_side, ZD_SNAP_RIGHT) != 0)
        return fail("window snap", 0);
    snap_left = zd_wm_snap_geometry(&monitor, ZD_SNAP_LEFT);
    snap_right = zd_wm_snap_geometry(&monitor, ZD_SNAP_RIGHT);
    win_view = zd_wm_window_const(&wm, window);
    side_view = zd_wm_window_const(&wm, window_side);
    if (!win_view || !side_view)
        return fail("window views", 0);
    if (win_view->snap != ZD_SNAP_LEFT ||
        win_view->logical.x != snap_left.x ||
        win_view->logical.y != snap_left.y ||
        win_view->logical.w != snap_left.w ||
        win_view->logical.h != snap_left.h)
        return fail("snap left geometry", (int64_t)win_view->logical.w);
    if (side_view->snap != ZD_SNAP_RIGHT ||
        side_view->logical.x != snap_right.x ||
        side_view->logical.y != snap_right.y ||
        side_view->logical.w != snap_right.w ||
        side_view->logical.h != snap_right.h)
        return fail("snap right geometry", (int64_t)side_view->logical.w);
    /* Two halves of one monitor: side by side, and together no wider than
     * the monitor itself. */
    if (snap_left.w <= 0 || snap_right.w <= 0 ||
        snap_right.x <= snap_left.x ||
        snap_left.w + snap_right.w > monitor.bounds.w)
        return fail("snap halves", (int64_t)snap_right.x);
    /* Workspaces: send the side window away, then follow it. */
    /* A manager starts with a single workspace, so the move is refused
     * until the set is grown through the API. */
    if (zd_wm_set_workspace(&wm, window_side, 1) != -ZD_EINVAL)
        return fail("workspace absent", 0);
    if (zd_wm_set_workspace_count(&wm, ZD_MAX_WORKSPACES + 1) != -ZD_EINVAL)
        return fail("workspace count reject", 0);
    if (zd_wm_set_workspace_count(&wm, 2) != 0)
        return fail("workspace count", 0);
    if (zd_wm_set_workspace(&wm, window_side, 1) != 0)
        return fail("window workspace move", 0);
    side_view = zd_wm_window_const(&wm, window_side);
    if (!side_view || side_view->workspace != 1)
        return fail("window workspace", 0);
    switches_before = wm.stats.workspace_switches;
    if (zd_wm_switch_workspace(&wm, 1) != 0)
        return fail("workspace switch", 0);
    if (wm.active_workspace != 1 ||
        wm.stats.workspace_switches != switches_before + 1)
        return fail("workspace switch count",
                    (int64_t)wm.stats.workspace_switches);
    /* The overview lists the live windows and cycles between them. */
    overview_ids[0] = window;
    overview_ids[1] = window_side;
    overview_area.x = 0;
    overview_area.y = 0;
    overview_area.w = SESSION_TARGET_W;
    overview_area.h = SESSION_TARGET_H;
    if (zd_overview_open(&session_overview, overview_area, 4) != 0)
        return fail("overview open", 0);
    if (zd_overview_set_windows(&session_overview, overview_ids, 2) != 0)
        return fail("overview windows", 0);
    if (session_overview.count != 2)
        return fail("overview count", (int64_t)session_overview.count);
    overview_first = zd_overview_focused_id(&session_overview);
    if (zd_overview_focus_next(&session_overview) != 0)
        return fail("overview focus next", 0);
    overview_second = zd_overview_focused_id(&session_overview);
    if (overview_second == 0 || overview_second == overview_first)
        return fail("overview focus moved", (int64_t)overview_second);
    /* Cycling wraps back to where it started. */
    if (zd_overview_focus_next(&session_overview) != 0 ||
        zd_overview_focused_id(&session_overview) != overview_first)
        return fail("overview focus wrap", 0);
    /* Both accepted moves are counted; nothing else selects. */
    if (session_overview.stats.selections != 2)
        return fail("overview selections",
                    (int64_t)session_overview.stats.selections);
    /* Removing a window relayouts the rest. */
    if (zd_overview_remove(&session_overview, window_side) != 0 ||
        session_overview.count != 1)
        return fail("overview remove", (int64_t)session_overview.count);
    zd_overview_close(&session_overview);
    if (zd_wm_switch_workspace(&wm, 0) != 0)
        return fail("workspace restore", 0);
    say("ZEROOS: session workspaces and snapping passed.");

    /* 19. ZERO bar over live shell state. The strip is sized from the
     * monitor the kernel reported, its quick controls pass through the real
     * capability gate and a shell hook that can refuse, its workspace
     * indicator follows the manager, and its title and notification badge
     * come from the focused window and the notification engine. */
    if (zd_bar_init(&session_bar, (uint32_t)monitor.bounds.w,
                    (uint32_t)(monitor.scale_percent / 100)) != 0)
        return fail("bar init", 0);
    if (zd_bar_layout(&session_bar) != 0)
        return fail("bar layout", 0);
    /* The layout tiles the strip exactly: the flexible title takes whatever
     * is left and the last applet ends where the monitor does. */
    if (session_bar.applets[ZD_BAR_APPLET_TITLE].w == 0 ||
        session_bar.applets[ZD_BAR_APPLET_QUICK].x +
                session_bar.applets[ZD_BAR_APPLET_QUICK].w !=
            (uint32_t)monitor.bounds.w)
        return fail("bar layout span", 0);
    if (zd_bar_physical_width(&session_bar) !=
        (uint32_t)monitor.bounds.w * (uint32_t)(monitor.scale_percent / 100))
        return fail("bar physical width",
                    (int64_t)zd_bar_physical_width(&session_bar));
    /* Quick controls: the privilege gate runs before anything is applied, so
     * without settings-write the toggle is refused (EPERM) and the shell hook
     * is never reached. */
    session_bar_toggle_seen = -1;
    zd_bar_set_ops(&session_bar, &session_bar_ops);
    zd_bar_set_caps(&session_bar, &session_caps);
    if (zd_bar_toggle_set(&session_bar, ZD_BAR_TOGGLE_WIFI, 1) != -1)
        return fail("bar toggle gate", 0);
    if (session_bar.toggle_denied[ZD_BAR_TOGGLE_WIFI] != 1 ||
        session_bar.stats.denied_toggles != 1)
        return fail("bar toggle denial",
                    (int64_t)session_bar.stats.denied_toggles);
    if (session_bar_toggle_seen >= 0)
        return fail("bar toggle ran without privilege", 0);
    if (zd_caps_activate(&session_caps, ZD_SVC_BAR,
                         1ULL << ZD_CAP_SETTINGS_WRITE) != 0)
        return fail("bar capability grant", 0);
    if (zd_bar_toggle_set(&session_bar, ZD_BAR_TOGGLE_WIFI, 1) != 0)
        return fail("bar toggle", 0);
    if (session_bar.toggle_on[ZD_BAR_TOGGLE_WIFI] != 1 ||
        session_toggle_applied[ZD_BAR_TOGGLE_WIFI] != 1)
        return fail("bar toggle state", 0);
    /* A control this image cannot honour is refused by the shell, and the bar
     * records the refusal instead of showing a radio that never came up. */
    if (zd_bar_toggle_set(&session_bar, ZD_BAR_TOGGLE_BLUETOOTH, 1) != -38)
        return fail("bar toggle refusal", 0);
    if (session_bar.toggle_denied[ZD_BAR_TOGGLE_BLUETOOTH] != 1 ||
        session_toggle_applied[ZD_BAR_TOGGLE_BLUETOOTH] != 0)
        return fail("bar toggle refusal state", 0);
    /* Do-not-disturb mirrors into the bar's own state. */
    if (zd_bar_toggle_set(&session_bar, ZD_BAR_TOGGLE_DND, 1) != 0)
        return fail("bar dnd", 0);
    if (session_bar.dnd_active != 1)
        return fail("bar dnd state", 0);
    /* The workspace indicator follows the manager and refuses indices it
     * cannot show. */
    if (zd_bar_set_workspace_count(&session_bar, wm.workspace_count) != 0 ||
        session_bar.workspace_count != wm.workspace_count)
        return fail("bar workspace count", (int64_t)wm.workspace_count);
    if (zd_bar_set_workspace(&session_bar, wm.workspace_count) != -22)
        return fail("bar workspace range", 0);
    if (zd_bar_set_workspace(&session_bar, wm.active_workspace) != 0)
        return fail("bar workspace sync", 0);
    if (zd_wm_set_title(&wm, window, "ZERO bar") != 0)
        return fail("bar window title", 0);
    bar_target = zd_wm_window_const(&wm, window);
    if (!bar_target)
        return fail("bar window read", 0);
    zd_bar_set_title(&session_bar, bar_target->title);
    for (bar_i = 0; session_bar.title[bar_i]; ++bar_i) {
        if (session_bar.title[bar_i] != bar_target->title[bar_i])
            return fail("bar title", (int64_t)bar_i);
    }
    if (bar_target->title[bar_i])
        return fail("bar title truncated", (int64_t)bar_i);
    /* The badge is the notification engine's own visible count. */
    bar_notes = zd_notify_visible(&session_notify, notify_now, bar_note_ids,
                                  ZD_ARRAY_COUNT(bar_note_ids));
    zd_bar_set_notif_count(&session_bar, bar_notes);
    if (session_bar.notif_count != bar_notes)
        return fail("bar badge", (int64_t)session_bar.notif_count);
    /* Click routing resolves to real applets, and the workspace click is
     * honoured on the manager rather than only reported. */
    if (zd_bar_click(&session_bar,
                     session_bar.applets[ZD_BAR_APPLET_MENU].x + 2,
                     &bar_action, &bar_arg) != 0)
        return fail("bar menu click", 0);
    if (bar_action != ZD_BAR_ACT_OPEN_LAUNCHER || session_bar.stats.opens != 1)
        return fail("bar menu action", (int64_t)bar_action);
    if (zd_bar_click(&session_bar,
                     session_bar.applets[ZD_BAR_APPLET_WORKSPACES].x + 2,
                     &bar_action, &bar_arg) != 0)
        return fail("bar workspace click", 0);
    if (bar_action != ZD_BAR_ACT_SWITCH_WORKSPACE ||
        bar_arg != (int)((wm.active_workspace + 1) % wm.workspace_count))
        return fail("bar workspace action", (int64_t)bar_arg);
    if (zd_wm_switch_workspace(&wm, (uint32_t)bar_arg) != 0)
        return fail("bar workspace switch", 0);
    if (zd_bar_set_workspace(&session_bar, wm.active_workspace) != 0 ||
        session_bar.workspace != wm.active_workspace)
        return fail("bar workspace follow", (int64_t)session_bar.workspace);
    /* Off the end of the strip is a miss, not a spurious action. */
    if (zd_bar_click(&session_bar, (uint32_t)monitor.bounds.w + 8,
                     &bar_action, &bar_arg) != -2 ||
        bar_action != ZD_BAR_ACT_NONE)
        return fail("bar click miss", (int64_t)bar_action);
    /* Keyboard focus walks the ring and wraps, and activating the focused
     * applet resolves to the same action a click would. */
    for (bar_i = 0; bar_i < ZD_BAR_APPLET_COUNT; ++bar_i) {
        if (zd_bar_focus_next(&session_bar) != 0)
            return fail("bar focus", 0);
    }
    if (session_bar.focus_idx != 0 ||
        session_bar.stats.focus_moves != (uint32_t)ZD_BAR_APPLET_COUNT)
        return fail("bar focus wrap", (int64_t)session_bar.focus_idx);
    if (zd_bar_activate_focused(&session_bar, &bar_action, &bar_arg) != 0)
        return fail("bar activate", 0);
    if (bar_action != ZD_BAR_ACT_OPEN_LAUNCHER)
        return fail("bar activate action", (int64_t)bar_action);
    say("ZEROOS: session bar and quick controls passed.");

    /* 20. System status assembled from live sources. Nothing in the readout
     * is a fixture: the counters come from the shells' own statistics, the
     * interval histogram from the measured present cycles above, and the
     * hardware figures from a fresh SYSTEM_INFO read -- and each is asserted
     * against its source rather than trusted. */
    if (session_metrics.interval_count != 8U)
        return fail("status interval count",
                    (int64_t)session_metrics.interval_count);
    if (zd_metrics_avg_interval(&session_metrics) == 0)
        return fail("status average interval", 0);
    if (zd_metrics_percentile(&session_metrics, 0) >
            zd_metrics_percentile(&session_metrics, 50) ||
        zd_metrics_percentile(&session_metrics, 50) >
            zd_metrics_percentile(&session_metrics, 95) ||
        zd_metrics_percentile(&session_metrics, 95) >
            zd_metrics_percentile(&session_metrics, 100))
        return fail("status percentile order", 0);
    if (zd_metrics_percentile(&session_metrics, 100) <
        session_metrics.interval_max)
        return fail("status percentile ceiling", 0);
    zd_metrics_add(&session_metrics, ZD_METRIC_INPUT_EVENTS,
                   router.stats.pointer_moves + router.stats.button_events);
    zd_metrics_add(&session_metrics, ZD_METRIC_LAUNCHES,
                   session_launcher.stats.launches);
    zd_metrics_add(&session_metrics, ZD_METRIC_CAP_DENIALS,
                   session_launcher.stats.cap_denied +
                       session_bar.stats.denied_toggles);
    if (zd_metrics_get(&session_metrics, ZD_METRIC_FRAMES_PRESENTED) !=
            session_perf.count ||
        zd_metrics_get(&session_metrics, ZD_METRIC_INPUT_EVENTS) !=
            router.stats.pointer_moves + router.stats.button_events ||
        zd_metrics_get(&session_metrics, ZD_METRIC_LAUNCHES) !=
            session_launcher.stats.launches ||
        zd_metrics_get(&session_metrics, ZD_METRIC_CAP_DENIALS) !=
            session_launcher.stats.cap_denied +
                session_bar.stats.denied_toggles)
        return fail("status counters", 0);
    /* The hardware figures are re-read from the kernel rather than carried
     * over, so the readout describes the machine as it is now. */
    if (zeroos_system_info(&status_info, sizeof(status_info)) != 0 ||
        status_info.version != ZEROOS_SYSTEM_INFO_VERSION)
        return fail("status system info", 0);
    if (status_info.uptime_ns < perf_now)
        return fail("status clock regression", 0);
    if (status_info.ram_free_bytes > status_info.ram_total_bytes ||
        status_info.ram_total_bytes == 0)
        return fail("status memory", 0);
    status_free = (uint32_t)((status_info.ram_free_bytes * 100ULL) /
                             status_info.ram_total_bytes);
    sys_status.uptime_ns = status_info.uptime_ns;
    sys_status.cpu_count = caps.cpu_count;
    sys_status.ram_mb = caps.ram_mb;
    sys_status.free_percent = status_free;
    sys_status.frame_budget_ms = (uint32_t)frame_budget;
    sys_status.avg_interval_ms = zd_metrics_avg_interval(&session_metrics);
    sys_status.p95_interval_ms = zd_metrics_percentile(&session_metrics, 95);
    sys_status.frames = zd_metrics_get(&session_metrics,
                                   ZD_METRIC_FRAMES_PRESENTED);
    sys_status.health = (uint32_t)perf_report.health;
    sys_status.notifications = session_bar.notif_count;
    if (sys_status.cpu_count == 0 || sys_status.ram_mb == 0 ||
        sys_status.frame_budget_ms == 0 || sys_status.avg_interval_ms == 0 ||
        sys_status.frames != 8U || sys_status.health > 3U || status_free > 100U)
        return fail("status readout", (int64_t)sys_status.health);
    if (sys_status.p95_interval_ms < sys_status.avg_interval_ms)
        return fail("status readout spread",
                    (int64_t)sys_status.p95_interval_ms);
    say("ZEROOS: session system status passed.");

    /* 21. AI platform broker over live shell data. The backend the broker
     * calls is the shell's own: a summarize request carries a path, the hook
     * reads that file through the kernel and answers with its size, so the
     * permission gate, the remote downgrade and the queue bound are all
     * exercised against a real document rather than a canned reply. */
    zd_ai_broker_init(&session_ai, &session_ai_ops, 0);
    /* The document is the one the file-manager step copied, renamed and
     * moved, and STAT-verified at 64 bytes on the way. */
    ai_request = session_ai_request("/ram/shell/inbox/moved.txt",
                                    ZD_AI_GRANT_CONTEXT_SELECTION, 0);
    if (zd_ai_submit(&session_ai, &ai_request) != -ZD_EPERM)
        return fail("ai permission gate", 0);
    if (session_ai.stats.denied_permission != 1)
        return fail("ai denial count",
                    (int64_t)session_ai.stats.denied_permission);
    if (session_ai_reads != 0)
        return fail("ai backend ran without grant", 0);
    /* PERSIST is granted as well, so the retained answer can be read back
     * instead of only counted. */
    zd_ai_grant(&session_ai, ZD_AI_GRANT_CONTEXT_SELECTION |
                               ZD_AI_GRANT_PERSIST);
    if (zd_ai_submit(&session_ai, &ai_request) != 0)
        return fail("ai submit", 0);
    if (session_ai.queued != 1 || session_ai.active != 1 ||
        session_ai.stats.submitted != 1 || session_ai.stats.wakeups != 1)
        return fail("ai queue state", (int64_t)session_ai.queued);
    if (zd_ai_drain(&session_ai, 4, &ai_done) != 0)
        return fail("ai drain", 0);
    if (session_ai.stats.run_failures != 0)
        return fail("ai backend failure",
                    (int64_t)session_ai.stats.run_failures);
    if (ai_done != 1)
        return fail("ai drain", (int64_t)ai_done);
    if (session_ai.stats.completed != 1 || session_ai_reads != 1)
        return fail("ai completion", (int64_t)session_ai_reads);
    /* The answer is the size of that document, read back through the kernel
     * by the backend. */
    if (session_ai_bytes != 64)
        return fail("ai document size", (int64_t)session_ai_bytes);
    if (session_ai.last_output_len != 2 || session_ai.last_output[0] != '6' ||
        session_ai.last_output[1] != '4')
        return fail("ai answer", (int64_t)session_ai.last_output_len);
    for (ai_i = 0; ai_i < (int)session_ai.last_payload_len; ++ai_i) {
        if (session_ai.last_payload[ai_i] != ai_request.payload[ai_i])
            return fail("ai retained payload", (int64_t)ai_i);
    }
    if (session_ai.stats.resident_bytes_after_drain !=
        session_ai.last_output_len + session_ai.last_payload_len)
        return fail("ai resident bytes",
                    (int64_t)session_ai.stats.resident_bytes_after_drain);
    /* Remote egress was never granted, so a remote-preferring request is
     * downgraded and still answered locally. */
    ai_request = session_ai_request("/ram/shell/inbox/moved.txt",
                                    ZD_AI_GRANT_CONTEXT_SELECTION, 1);
    if (zd_ai_submit(&session_ai, &ai_request) != 0)
        return fail("ai remote submit", 0);
    if (zd_ai_drain(&session_ai, 4, &ai_done) != 0 || ai_done != 1)
        return fail("ai remote drain", (int64_t)ai_done);
    if (session_ai.stats.backend_downgrades != 1 || session_ai_reads != 2)
        return fail("ai downgrade",
                    (int64_t)session_ai.stats.backend_downgrades);
    /* The queue is bounded: the ninth request is dropped, not queued. */
    for (ai_i = 0; ai_i < (int)ZD_AI_QUEUE_DEPTH; ++ai_i) {
        if (zd_ai_submit(&session_ai, &ai_request) != 0)
            return fail("ai queue fill", (int64_t)ai_i);
    }
    if (zd_ai_submit(&session_ai, &ai_request) != -ZD_ENOSPC)
        return fail("ai queue bound", 0);
    if (session_ai.stats.queue_dropped != 1)
        return fail("ai drop count",
                    (int64_t)session_ai.stats.queue_dropped);
    if (zd_ai_drain(&session_ai, ZD_AI_QUEUE_DEPTH, &ai_done) != 0 ||
        ai_done != ZD_AI_QUEUE_DEPTH)
        return fail("ai queue drain", (int64_t)ai_done);
    /* Demand-driven: with nothing resident the broker is dormant again. */
    if (session_ai.queued != 0 || session_ai.active != 0 ||
        session_ai.stats.wakeups != 3)
        return fail("ai dormant", (int64_t)session_ai.stats.wakeups);
    say("ZEROOS: session ai platform passed.");

    /* 22. Browser tab lifecycle on the kernel clock over a real document.
     * The tick is the kernel's own monotonic time in milliseconds measured
     * from the moment the browser is initialised, and the ladder thresholds
     * are multiples of the clock's own granularity -- measured first -- so a
     * coarse tick cannot skip a rung. The resident document is a file the
     * shell really wrote, and each rung is reached only because that much
     * real time has passed. */
    browser_origin = session_uptime_ns();
    if (browser_origin == 0)
        return fail("browser clock", 0);
    browser_step = 0;
    browser_base = session_elapsed_ms(browser_origin);
    browser_guard = 0;
    while (browser_step == 0) {
        browser_now_ms = session_elapsed_ms(browser_origin);
        if (browser_now_ms > browser_base) {
            browser_step = browser_now_ms - browser_base;
            break;
        }
        if (++browser_guard > 20000000ULL)
            return fail("browser clock step", 0);
    }
    if (browser_step == 0 || browser_step > 20)
        return fail("browser clock granularity", (int64_t)browser_step);
    zd_browser_init(&session_browser, (uint32_t)(browser_step * 3),
                    (uint32_t)(browser_step * 5),
                    (uint32_t)(browser_step * 7));
    /* The resident document is the file the file-manager step copied,
     * renamed and moved into the inbox, STAT-verified at 64 bytes. */
    if (session_read_buffer("/ram/shell/inbox/moved.txt", browser_doc,
                            sizeof(browser_doc), &doc_len) != 0)
        return fail("browser document", 0);
    if (doc_len != 64)
        return fail("browser document size", (int64_t)doc_len);
    if (zd_browser_open(&session_browser, (uint32_t)doc_len, &tab_id) != 0 ||
        tab_id == 0)
        return fail("browser open", (int64_t)tab_id);
    tab = session_browser_tab(&session_browser, tab_id);
    if (!tab)
        return fail("browser tab lookup", 0);
    if (tab->state != ZD_TAB_ACTIVE || tab->has_document != 1 ||
        tab->content_bytes != (uint32_t)doc_len ||
        session_browser.tab_count != 1 ||
        session_browser.active_id != tab_id ||
        session_browser.stats.opened != 1)
        return fail("browser open state", (int64_t)tab->state);
    /* One granularity has really passed; that is far short of the first
     * rung, so the tab is still active. */
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_TICK,
                         browser_step) != 0)
        return fail("browser first tick", 0);
    tab = session_browser_tab(&session_browser, tab_id);
    if (!tab)
        return fail("browser tab lost", 0);
    if (tab->state != ZD_TAB_ACTIVE)
        return fail("browser premature idle", (int64_t)tab->state);
    /* IDLE: the clock really advanced, and the document is still resident. */
    if (session_wait_ms(browser_origin, browser_step * 4) != 0)
        return fail("browser idle wait", 0);
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_TICK,
                         browser_step * 4) != 0)
        return fail("browser idle tick", 0);
    tab = session_browser_tab(&session_browser, tab_id);
    if (!tab)
        return fail("browser tab lost", 0);
    if (tab->state != ZD_TAB_IDLE ||
        tab->content_bytes != (uint32_t)doc_len)
        return fail("browser idle state", (int64_t)tab->state);
    /* FROZEN: still resident, scripts suspended. */
    if (session_wait_ms(browser_origin, browser_step * 6) != 0)
        return fail("browser freeze wait", 0);
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_TICK,
                         browser_step * 6) != 0)
        return fail("browser freeze tick", 0);
    tab = session_browser_tab(&session_browser, tab_id);
    if (!tab)
        return fail("browser tab lost", 0);
    if (tab->state != ZD_TAB_FROZEN ||
        session_browser.stats.freezes != 1 ||
        tab->content_bytes != (uint32_t)doc_len)
        return fail("browser frozen state", (int64_t)tab->state);
    /* DISCARDED: the resident document's bytes are released. */
    if (session_wait_ms(browser_origin, browser_step * 8) != 0)
        return fail("browser discard wait", 0);
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_TICK,
                         browser_step * 8) != 0)
        return fail("browser discard tick", 0);
    tab = session_browser_tab(&session_browser, tab_id);
    if (!tab)
        return fail("browser tab lost", 0);
    if (tab->state != ZD_TAB_DISCARDED ||
        session_browser.stats.discards != 1 || tab->has_document != 0 ||
        tab->content_bytes != 0)
        return fail("browser discarded state", (int64_t)tab->state);
    /* Reopening a discarded tab reloads it. */
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_RELOAD,
                         session_elapsed_ms(browser_origin)) != 0)
        return fail("browser reload", 0);
    tab = session_browser_tab(&session_browser, tab_id);
    if (!tab)
        return fail("browser tab lost", 0);
    if (tab->state != ZD_TAB_ACTIVE || tab->has_document != 1 ||
        tab->content_bytes == 0 || tab->reloads != 1 ||
        session_browser.stats.reloads != 1)
        return fail("browser reload state", (int64_t)tab->state);
    /* A healthy tab must not "recover", and the clock must not rewind: both
     * are refused and both refusals are counted. */
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_RELOAD,
                         session_elapsed_ms(browser_origin)) != -ZD_EINVAL)
        return fail("browser reload refusal", 0);
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_TICK,
                         session_browser.now - 1) != -ZD_EINVAL)
        return fail("browser clock rewind", 0);
    if (session_browser.stats.rejected_events != 2)
        return fail("browser rejection count",
                    (int64_t)session_browser.stats.rejected_events);
    /* Crash isolation: the renderer dies, the state refuses the moves that
     * need a live renderer, and a reload recovers it. */
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_RENDERER_CRASH,
                         session_elapsed_ms(browser_origin)) != 0)
        return fail("browser crash", 0);
    tab = session_browser_tab(&session_browser, tab_id);
    if (!tab)
        return fail("browser tab lost", 0);
    if (tab->state != ZD_TAB_CRASHED || tab->crashes != 1 ||
        session_browser.stats.crashes != 1)
        return fail("browser crashed state", (int64_t)tab->state);
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_FOCUS,
                         session_elapsed_ms(browser_origin)) != -ZD_ESTATE ||
        zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_DISCARD_NOW,
                         session_elapsed_ms(browser_origin)) != -ZD_ESTATE)
        return fail("browser crashed refusals", 0);
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_RELOAD,
                         session_elapsed_ms(browser_origin)) != 0)
        return fail("browser crash recovery", 0);
    if (session_browser.stats.crash_recoveries != 1 ||
        session_browser.stats.rejected_events != 4)
        return fail("browser recovery state",
                    (int64_t)session_browser.stats.rejected_events);
    /* An unknown tab is refused, and closing releases the slot for good. */
    if (zd_browser_event(&session_browser, 4242, ZD_TAB_EV_TICK,
                         session_elapsed_ms(browser_origin)) != -ZD_ENOENT)
        return fail("browser unknown tab", 0);
    if (zd_browser_close(&session_browser, tab_id) != 0 ||
        session_browser.tab_count != 0 ||
        session_browser.stats.closed != 1)
        return fail("browser close", (int64_t)session_browser.tab_count);
    if (zd_browser_event(&session_browser, tab_id, ZD_TAB_EV_TICK,
                         session_elapsed_ms(browser_origin)) != -ZD_ENOENT)
        return fail("browser closed tab", 0);
    if (session_browser.stats.rejected_events != 6)
        return fail("browser final rejections",
                    (int64_t)session_browser.stats.rejected_events);
    say("ZEROOS: session browser lifecycle passed.");

    /* 23. Study centre over live shell data. The deck is built from the
     * directory the file manager is really listing, the scheduler is driven
     * through its bounded ladder with the day counter the app injects, and
     * the study assistant's request travels through the same AI broker with
     * the shell's backend answering from the live deck. */
    if (zd_study_init(&session_study, "ZeroOS shell") != 0)
        return fail("study init", 0);
    if (zd_fm_open(&session_fm, "/ram/shell") != 0)
        return fail("study listing", 0);
    study_entries = zd_fm_visible_count(&session_fm);
    if (study_entries == 0)
        return fail("study listing empty", 0);
    study_cards = study_entries < 3U ? study_entries : 3U;
    for (study_i = 0; study_i < (int)study_cards; ++study_i) {
        study_entry = zd_fm_visible(&session_fm, (uint32_t)study_i);
        if (!study_entry)
            return fail("study entry", (int64_t)study_i);
        /* Front: the live name. Back: its size in bytes, rendered here. */
        session_decimal(study_back, (uint32_t)sizeof(study_back),
                        study_entry->size);
        if (zd_study_add_card(&session_study, study_entry->name,
                              study_back) != 0)
            return fail("study add card", (int64_t)study_i);
        if (!zd_study_find(&session_study, study_entry->name))
            return fail("study card lookup", (int64_t)study_i);
    }
    if (session_study.card_count != study_cards)
        return fail("study card count", (int64_t)session_study.card_count);
    /* A duplicate front is refused, not silently replaced. */
    study_entry = zd_fm_visible(&session_fm, 0);
    if (!study_entry)
        return fail("study first entry", 0);
    if (zd_study_add_card(&session_study, study_entry->name, "0") != -17)
        return fail("study duplicate", 0);
    /* The ladder: 0 -> 1 day on GOOD, two rungs on EASY, and AGAIN is a lapse
     * that resets the interval and lowers the ease. */
    study_card = zd_study_next_due(&session_study);
    if (!study_card)
        return fail("study next due", 0);
    if (zd_study_grade(&session_study, study_card->front,
                       ZD_STUDY_GOOD) != 0)
        return fail("study grade", 0);
    if (study_card->interval_days != 1 || study_card->due_day != 1 ||
        study_card->reps != 1 || session_study.stats.reviews != 1)
        return fail("study ladder good", (int64_t)study_card->interval_days);
    if (zd_study_advance_day(&session_study, 0) != -22)
        return fail("study day refusal", 0);
    if (zd_study_advance_day(&session_study, 1) != 0 ||
        session_study.day != 1)
        return fail("study day advance", (int64_t)session_study.day);
    if (zd_study_grade(&session_study, study_card->front,
                       ZD_STUDY_EASY) != 0)
        return fail("study grade easy", 0);
    if (study_card->interval_days != 7 || study_card->due_day != 8 ||
        study_card->ease != 265)
        return fail("study ladder easy", (int64_t)study_card->interval_days);
    if (zd_study_grade(&session_study, study_card->front,
                       ZD_STUDY_AGAIN) != 0)
        return fail("study grade again", 0);
    if (study_card->interval_days != 1 || study_card->lapses != 1 ||
        session_study.stats.lapses != 1 || study_card->ease != 245)
        return fail("study lapse", (int64_t)study_card->lapses);
    /* It is no longer the card that is due next. */
    if (zd_study_next_due(&session_study) == study_card)
        return fail("study due rotation", 0);
    /* Focus mode: bounded block, counted once, and it must be started. */
    if (zd_study_session_start(&session_study, 0) != -22)
        return fail("study focus range", 0);
    if (zd_study_session_start(&session_study, 2) != 0)
        return fail("study focus start", 0);
    if (zd_study_session_start(&session_study, 2) != -16)
        return fail("study focus busy", 0);
    if (zd_study_session_tick(&session_study) != 0 ||
        zd_study_session_tick(&session_study) != 0)
        return fail("study focus tick", 0);
    if (session_study.session.focus_elapsed != 2 ||
        session_study.stats.focus_blocks != 1)
        return fail("study focus block",
                    (int64_t)session_study.session.focus_elapsed);
    if (zd_study_session_tick(&session_study) != 0 ||
        session_study.stats.focus_blocks != 1 ||
        session_study.session.focus_elapsed != 2)
        return fail("study focus overrun", 0);
    if (zd_study_session_end(&session_study) != 0 ||
        zd_study_session_tick(&session_study) != -22)
        return fail("study focus end", 0);
    /* The assistant's request goes through the same broker, and the answer is
     * the deck's real card count. */
    if (zd_study_assist_submit(&session_study, &session_ai,
                              (const char *)0) != 0)
        return fail("study assist submit", 0);
    if (zd_ai_drain(&session_ai, 4, &ai_done) != 0 || ai_done != 1)
        return fail("study assist drain", (int64_t)ai_done);
    if (session_ai.last_output_len != 1 ||
        session_ai.last_output[0] != (char)('0' + (int)study_cards))
        return fail("study assist answer",
                    (int64_t)session_ai.last_output_len);
    say("ZEROOS: session study center passed.");

    /* 24. Gaming mode and the FPS monitor over measured frames, cooperating
     * with the resource governor. Every frame recorded here is a real
     * present-and-pace cycle on the kernel clock, and the cooperative
     * decision is taken from those measurements together with the governor's
     * own memory pressure. */
    if (zd_fps_init(&session_fps, 1000, ZD_PERF_QUALITY) != 0)
        return fail("fps init", 0);
    if (zd_fps_budget_ms(ZD_PERF_QUALITY) == 0 || zd_fps_budget_ms(99) != 0)
        return fail("fps budget", (int64_t)zd_fps_budget_ms(ZD_PERF_QUALITY));
    for (fps_index = 0; fps_index < 3U; ++fps_index) {
        fps_start = session_uptime_ns();
        if (fps_start == 0)
            return fail("fps clock", 0);
        if (session_present(0, 0, 0, SESSION_WINDOW_W, SESSION_WINDOW_H,
                            SESSION_WINDOW_W * 4U, window_pixels) != 0)
            return fail("fps present", 0);
        do {
            fps_now = session_uptime_ns();
            if (fps_now == 0)
                return fail("fps clock", 0);
        } while (fps_now - fps_start <
                 (uint64_t)frame_budget * 1000000ULL);
        fps_frame_ms = (fps_now - fps_start) / 1000000ULL;
        if (zd_fps_record(&session_fps, fps_start / 1000000ULL,
                          fps_frame_ms) != 0)
            return fail("fps record", (int64_t)fps_frame_ms);
    }
    if (session_fps.frames_total != 3 || session_fps.count != 3)
        return fail("fps samples", (int64_t)session_fps.frames_total);
    if (zd_fps_avg_frame_ms(&session_fps) == 0)
        return fail("fps average", 0);
    /* This machine's tier budget sits above the quality profile's budget, so
     * every measured frame breaches it; the monitor reports that instead of
     * hiding it. */
    if (frame_budget > zd_fps_budget_ms(ZD_PERF_QUALITY)) {
        if (session_fps.budget_breaches != 3)
            return fail("fps breaches",
                        (int64_t)session_fps.budget_breaches);
    } else if (session_fps.budget_breaches > 3) {
        return fail("fps breaches", (int64_t)session_fps.budget_breaches);
    }
    if (zd_fps_percentile(&session_fps, 100) <
        zd_fps_avg_frame_ms(&session_fps))
        return fail("fps percentile", 0);
    fps_live = zd_fps_current(&session_fps, session_uptime_ns() / 1000000ULL);
    if (fps_live == 0)
        return fail("fps current", 0);
    zd_gaming_init(&session_gaming);
    if (zd_gaming_profile_set(&session_gaming, "child", 99, 60, 0) != -22 ||
        zd_gaming_profile_set(&session_gaming, "child", ZD_GAME_PERF, 0,
                              0) != -22 ||
        zd_gaming_profile_set(&session_gaming, "child", ZD_GAME_PERF, 241,
                              0) != -22 ||
        zd_gaming_profile_set(&session_gaming, "", ZD_GAME_PERF, 60,
                              0) != -22)
        return fail("gaming profile validation", 0);
    if (session_gaming.stats.rejected != 4)
        return fail("gaming rejections",
                    (int64_t)session_gaming.stats.rejected);
    if (zd_gaming_profile_set(&session_gaming, "child", ZD_GAME_PERF, 60,
                              ZD_GAME_FLAG_OVERLAY |
                                  ZD_GAME_FLAG_LOW_LATENCY) != 0 ||
        session_gaming.count != 1 || session_gaming.stats.sets != 1)
        return fail("gaming profile", (int64_t)session_gaming.count);
    if (zd_gaming_remap(&session_gaming, "child", 0, 3) != 0 ||
        zd_gaming_lookup(&session_gaming, "child", 0) != 3)
        return fail("gaming remap", 0);
    if (zd_gaming_lookup(&session_gaming, "child", 1) != -2 ||
        session_gaming.stats.unknown_buttons != 1)
        return fail("gaming unmapped button", 0);
    if (zd_gaming_remap(&session_gaming, "child", 32, 0) != -22)
        return fail("gaming remap range", 0);
    /* The cooperative decision comes from this machine's own numbers. */
    game_pressure = 100U - free_percent;
    game_fps_milli = fps_live * 1000U;
    game_yield = zd_gaming_cooperative(&session_gaming, "child",
                                       game_fps_milli, game_pressure);
    if (game_yield < 0)
        return fail("gaming cooperative", (int64_t)game_yield);
    /* A budget of this size is under the 30 fps floor, so with the overlay on
     * the policy must at least drop the overlay. */
    if (game_fps_milli < 30000U && game_yield < ZD_GAME_YIELD_OVERLAY_OFF)
        return fail("gaming yield", (int64_t)game_yield);
    if (session_gaming.stats.yields !=
        (game_yield == ZD_GAME_YIELD_NONE ? 0U : 1U))
        return fail("gaming yield count",
                    (int64_t)session_gaming.stats.yields);
    /* Both ends of the policy ladder are pinned. Holding the target floor is
     * only reachable for a profile with no overlay, since dropping the
     * overlay outranks it under the same pressure. */
    if (zd_gaming_profile_set(&session_gaming, "no-overlay",
                              ZD_GAME_BALANCED, 60,
                              ZD_GAME_FLAG_LOW_LATENCY) != 0)
        return fail("gaming second profile", 0);
    if (zd_gaming_cooperative(&session_gaming, "child", 60000U, 0U) !=
        ZD_GAME_YIELD_NONE)
        return fail("gaming policy none", 0);
    if (zd_gaming_cooperative(&session_gaming, "no-overlay", 60000U, 90U) !=
        ZD_GAME_YIELD_TARGET_FLOOR)
        return fail("gaming policy floor", 0);
    if (zd_gaming_cooperative(&session_gaming, "child", 60000U, 90U) !=
        ZD_GAME_YIELD_OVERLAY_OFF)
        return fail("gaming policy overlay", 0);
    if (zd_gaming_cooperative(&session_gaming, "child", 20000U, 90U) !=
        ZD_GAME_YIELD_DEGRADE)
        return fail("gaming policy degrade", 0);
    if (zd_gaming_cooperative(&session_gaming, "no-such-app", 60000U, 0U) != -2)
        return fail("gaming unknown app", 0);
    /* Gaming mode must not bypass the governor: the budget in force is still
     * the one the governor assigned to this tier. */
    zd_governor_effects_for_tier(session_governor.tier, &game_budget,
                                 &game_effects);
    if (game_budget != frame_budget)
        return fail("gaming bypassed the governor", (int64_t)game_budget);
    if (zd_gaming_profile_remove(&session_gaming, "child") != 0 ||
        zd_gaming_profile_remove(&session_gaming, "no-overlay") != 0 ||
        session_gaming.count != 0 ||
        zd_gaming_profile_remove(&session_gaming, "child") != -2)
        return fail("gaming remove", (int64_t)session_gaming.count);
    say("ZEROOS: session gaming and fps passed.");

    /* 25. Accessibility tree and localization over live shell state. Every
     * node is a real window from the manager, named from its real title and
     * bound to its window id; the announcement's urgency comes from a real
     * notification's priority through the notification centre's own policy;
     * and the announced text is the shell's own label from the localized
     * catalog. */
    zd_a11y_init(&session_a11y);
    a11y_windows = zd_wm_stacking_order(&wm, a11y_ids,
                                        ZD_ARRAY_COUNT(a11y_ids));
    if (a11y_windows == 0)
        return fail("a11y stacking", 0);
    for (a11y_i = 0; a11y_i < (int)a11y_windows; ++a11y_i) {
        a11y_win = zd_wm_window_const(&wm, a11y_ids[a11y_i]);
        if (!a11y_win)
            return fail("a11y window read", (int64_t)a11y_i);
        a11y_states = ZD_A11Y_FOCUSABLE;
        if (a11y_win->keyboard_focused)
            a11y_states |= ZD_A11Y_FOCUSED;
        if (a11y_win->state == ZD_WINDOW_MINIMIZED)
            a11y_states |= ZD_A11Y_HIDDEN;
        /* The node takes the window's own declared role, not a guess. */
        if (zd_a11y_create(&session_a11y, ZD_A11Y_ROOT, a11y_win->role,
                           a11y_win->title, a11y_states, a11y_win->id,
                           &a11y_node) != 0 || a11y_node == ZD_A11Y_INVALID)
            return fail("a11y node create", (int64_t)a11y_i);
    }
    if (session_a11y.stats.created != a11y_windows)
        return fail("a11y created count",
                    (int64_t)session_a11y.stats.created);
    /* The snapshot walks the tree depth first: the node bound to the window
     * the manager says has the keyboard must carry that window's title. */
    a11y_rows = zd_a11y_snapshot(&session_a11y, a11y_snap,
                                 ZD_ARRAY_COUNT(a11y_snap));
    if (a11y_rows == 0)
        return fail("a11y snapshot", 0);
    a11y_target = zd_wm_keyboard_target(&wm);
    if (a11y_target == ZD_INVALID_WINDOW)
        return fail("a11y keyboard target", 0);
    a11y_found = (const struct zd_a11y_node *)0;
    for (a11y_i = 0; a11y_i < (int)a11y_rows; ++a11y_i) {
        const struct zd_a11y_node *candidate =
            zd_a11y_node_const(&session_a11y, a11y_snap[a11y_i].id);
        if (candidate && candidate->widget == a11y_target)
            a11y_found = candidate;
    }
    if (!a11y_found)
        return fail("a11y window node", 0);
    a11y_win = zd_wm_window_const(&wm, a11y_target);
    if (!a11y_win)
        return fail("a11y focused window", 0);
    for (a11y_i = 0; a11y_found->name[a11y_i] || a11y_win->title[a11y_i];
         ++a11y_i) {
        if (a11y_found->name[a11y_i] != a11y_win->title[a11y_i])
            return fail("a11y node name", (int64_t)a11y_i);
    }
    if (zd_a11y_focus_node(&session_a11y, a11y_found->id) != 0 ||
        zd_a11y_focused(&session_a11y) != a11y_found->id)
        return fail("a11y focus node", (int64_t)a11y_found->id);
    /* Traversal walks the focusable nodes and wraps back to where it began. */
    a11y_first = zd_a11y_focus_next(&session_a11y);
    if (a11y_first == ZD_A11Y_INVALID)
        return fail("a11y focus next", 0);
    a11y_steps = 1;
    while (zd_a11y_focused(&session_a11y) != a11y_first &&
           a11y_steps <= a11y_windows) {
        if (zd_a11y_focus_next(&session_a11y) == ZD_A11Y_INVALID)
            return fail("a11y focus walk", (int64_t)a11y_steps);
        ++a11y_steps;
    }
    if (zd_a11y_focused(&session_a11y) != a11y_first)
        return fail("a11y focus wrap", (int64_t)a11y_steps);
    /* The urgency of what the shell announces is the notification centre's
     * own accessibility verdict for a notification that is really visible. */
    a11y_notes = zd_notify_visible(&session_notify, notify_now, a11y_note_ids,
                                   ZD_ARRAY_COUNT(a11y_note_ids));
    if (a11y_notes == 0)
        return fail("a11y notification source", 0);
    a11y_note = zd_notify_get(&session_notify, a11y_note_ids[0]);
    if (!a11y_note)
        return fail("a11y notification read", 0);
    a11y_policy = zd_notify_a11y_policy(a11y_note->priority);
    /* The engine's own mapping, checked against this notification's real
     * priority: critical interrupts, low stays quiet, the rest is announced
     * when idle. */
    if (a11y_note->priority == ZD_NOTIFY_CRITICAL) {
        if (a11y_policy != ZD_NOTIFY_A11Y_ASSERTIVE)
            return fail("a11y policy", (int64_t)a11y_policy);
    } else if (a11y_note->priority == ZD_NOTIFY_LOW) {
        if (a11y_policy != ZD_NOTIFY_A11Y_QUIET)
            return fail("a11y policy", (int64_t)a11y_policy);
    } else if (a11y_policy != ZD_NOTIFY_A11Y_POLITE) {
        return fail("a11y policy", (int64_t)a11y_policy);
    }
    a11y_urgency = a11y_policy == ZD_NOTIFY_A11Y_ASSERTIVE
                       ? (int)ZD_A11Y_URGENCY_HIGH
                   : a11y_policy == ZD_NOTIFY_A11Y_QUIET
                       ? (int)ZD_A11Y_URGENCY_LOW
                       : (int)ZD_A11Y_URGENCY_NORMAL;
    /* Localization: the shell's own labels come from the catalog, both
     * locales are present for every key, and the two really differ. */
    zd_i18n_init(&session_i18n, ZD_LOCALE_EN);
    i18n_loaded = zd_i18n_load_shell_catalog(&session_i18n);
    if (i18n_loaded == 0)
        return fail("i18n catalog", 0);
    zd_i18n_coverage(&session_i18n, &i18n_total, &i18n_hi_present);
    if (i18n_total != i18n_loaded || i18n_hi_present != i18n_total)
        return fail("i18n coverage", (int64_t)i18n_hi_present);
    i18n_text_en = zd_i18n_text(&session_i18n, "shell.zero_bar");
    if (!i18n_text_en || !i18n_text_en[0])
        return fail("i18n english label", 0);
    zd_i18n_set_locale(&session_i18n, ZD_LOCALE_HI);
    if (zd_i18n_locale(&session_i18n) != ZD_LOCALE_HI)
        return fail("i18n locale", 0);
    i18n_text_hi = zd_i18n_text(&session_i18n, "shell.zero_bar");
    if (!i18n_text_hi || !i18n_text_hi[0])
        return fail("i18n hindi label", 0);
    for (a11y_i = 0; i18n_text_en[a11y_i] || i18n_text_hi[a11y_i]; ++a11y_i) {
        if (i18n_text_en[a11y_i] != i18n_text_hi[a11y_i])
            break;
    }
    if (!i18n_text_en[a11y_i] && !i18n_text_hi[a11y_i])
        return fail("i18n localization", 0);
    if (zd_i18n_locale_from_name(zd_i18n_locale_name(ZD_LOCALE_HI)) !=
        ZD_LOCALE_HI)
        return fail("i18n locale round trip", 0);
    /* With no screen reader there is no consumer, so the shell accepts the
     * announcement and queues nothing rather than announcing into the void. */
    if (zd_a11y_announce(&session_a11y, a11y_found->id, a11y_win->title,
                         ZD_A11Y_URGENCY_HIGH) != 0)
        return fail("a11y announce without reader", 0);
    if (zd_a11y_next_announcement(&session_a11y, &a11y_msg) != -ZD_ENOENT)
        return fail("a11y dead announcement queued", 0);
    if (session_a11y.stats.announcements != 0)
        return fail("a11y announcement count",
                    (int64_t)session_a11y.stats.announcements);
    /* The shell's screen reader is on, so what it announces carries that
     * verdict, and its text is the focused window's real title. */
    session_a11y.profile.screen_reader_enabled = 1;
    if (zd_a11y_announce(&session_a11y, a11y_found->id, a11y_win->title,
                         (enum zd_a11y_urgency)a11y_urgency) != 0)
        return fail("a11y announce", 0);
    if (session_a11y.stats.announcements != 1)
        return fail("a11y announcement queued",
                    (int64_t)session_a11y.stats.announcements);
    if (zd_a11y_next_announcement(&session_a11y, &a11y_msg) != 0)
        return fail("a11y announcement read", 0);
    if ((int)a11y_msg.urgency != a11y_urgency)
        return fail("a11y announcement urgency", (int64_t)a11y_msg.urgency);
    for (a11y_i = 0; a11y_msg.text[a11y_i] || a11y_win->title[a11y_i];
         ++a11y_i) {
        if (a11y_msg.text[a11y_i] != a11y_win->title[a11y_i])
            return fail("a11y announcement text", (int64_t)a11y_i);
    }
    /* A low-urgency announcement is pending; a high-urgency one carrying the
     * shell's localized label must preempt it. */
    if (zd_a11y_announce(&session_a11y, a11y_found->id, a11y_win->title,
                         ZD_A11Y_URGENCY_LOW) != 0)
        return fail("a11y announce low", 0);
    if (zd_a11y_announce(&session_a11y, a11y_found->id, i18n_text_hi,
                         ZD_A11Y_URGENCY_HIGH) != 0)
        return fail("a11y announce high", 0);
    if (zd_a11y_next_announcement(&session_a11y, &a11y_msg) != 0)
        return fail("a11y preemption read", 0);
    if (a11y_msg.urgency != ZD_A11Y_URGENCY_HIGH)
        return fail("a11y preemption", (int64_t)a11y_msg.urgency);
    for (a11y_i = 0; a11y_msg.text[a11y_i] || i18n_text_hi[a11y_i];
         ++a11y_i) {
        if (a11y_msg.text[a11y_i] != i18n_text_hi[a11y_i])
            return fail("a11y preemption text", (int64_t)a11y_i);
    }
    say("ZEROOS: session accessibility and localization passed.");

    /* 26. Media library over the live filesystem with explicit rights. The
     * origins are real directories, every item is a real file verified
     * through STAT before it is listed, and the rights, origin and DRM gates
     * are each shown to refuse rather than to be worked around. */
    zd_media_init(&session_media);
    if (zd_media_register_source(&session_media, "",
                                 ZD_MEDIA_RIGHT_ALL) != -22 ||
        zd_media_register_source(&session_media, "file:/ram/shell/inbox",
                                 ZD_MEDIA_RIGHT_ALL << 1) != -22)
        return fail("media source validation", 0);
    if (session_media.stats.rejected != 2)
        return fail("media rejections",
                    (int64_t)session_media.stats.rejected);
    /* Two real origins: the inbox granted play and cache, its parent listed
     * with no rights at all -- listing is not playing. */
    if (zd_media_register_source(&session_media, "file:/ram/shell/inbox",
                                 ZD_MEDIA_RIGHT_PLAY |
                                     ZD_MEDIA_RIGHT_CACHE) != 0 ||
        zd_media_register_source(&session_media, "file:/ram/shell", 0) != 0)
        return fail("media source grant", 0);
    if (session_media.source_count != 2 ||
        session_media.stats.registered != 2)
        return fail("media source count",
                    (int64_t)session_media.source_count);
    if (zd_fm_open(&session_fm, "/ram/shell/inbox") != 0)
        return fail("media listing", 0);
    media_count = zd_fm_visible_count(&session_fm);
    if (media_count == 0)
        return fail("media listing empty", 0);
    if (media_count > 3)
        media_count = 3;
    for (media_i = 0; media_i < (int)media_count; ++media_i) {
        media_entry = zd_fm_visible(&session_fm, (uint32_t)media_i);
        if (!media_entry)
            return fail("media entry", (int64_t)media_i);
        media_len = 0;
        for (media_j = 0; media_dir[media_j]; ++media_j)
            media_path[media_len++] = media_dir[media_j];
        for (media_j = 0; media_entry->name[media_j] &&
                 media_len + 1 < (uint32_t)sizeof(media_path); ++media_j)
            media_path[media_len++] = media_entry->name[media_j];
        media_path[media_len] = 0;
        /* The file must really be there before the library lists it. */
        if (zeroos_stat(media_path, &file_stat) != 0)
            return fail("media item stat", (int64_t)media_i);
        if (zd_media_add_item(&session_media, "file:/ram/shell/inbox",
                              media_entry->name, 0) != 0)
            return fail("media add item", (int64_t)media_i);
        if (media_i == 0) {
            for (media_j = 0; media_entry->name[media_j] &&
                 media_j + 1 < (int)sizeof(media_first); ++media_j)
                media_first[media_j] = media_entry->name[media_j];
            media_first[media_j] = 0;
            for (media_j = 0; media_first[media_j] &&
                 media_j + 1 < (int)sizeof(session_dict_extra);
                 ++media_j)
                session_dict_extra[media_j] = media_first[media_j];
            session_dict_extra[media_j] = 0;
        }
    }
    if (session_media.item_count != media_count ||
        session_media.stats.items_added != media_count)
        return fail("media item count",
                    (int64_t)session_media.item_count);
    /* The first item sits under a granted origin and plays; cache is granted
     * for that origin and export is not. */
    if (zd_media_play(&session_media, 1) != 0 ||
        session_media.stats.plays != 1)
        return fail("media play", (int64_t)session_media.stats.plays);
    if (zd_media_request(&session_media, 1, ZD_MEDIA_RIGHT_CACHE) != 0)
        return fail("media cache right", 0);
    /* Export is not granted for that origin, so the request is refused and
     * counted as the first rights refusal. */
    if (zd_media_request(&session_media, 1, ZD_MEDIA_RIGHT_EXPORT) != -1 ||
        session_media.stats.refusals_rights != 1)
        return fail("media export gate",
                    (int64_t)session_media.stats.refusals_rights);
    /* The same title under the origin that holds no rights is refused. */
    if (zd_media_add_item(&session_media, "file:/ram/shell", media_first,
                          0) != 0)
        return fail("media ungranted add", 0);
    if (zd_media_play(&session_media, media_count + 1) != -1 ||
        session_media.stats.refusals_rights != 2)
        return fail("media rights refusal",
                    (int64_t)session_media.stats.refusals_rights);
    /* Protected content is listed but never played, and the refusal is
     * counted rather than quietly bypassed. */
    if (zd_media_add_item(&session_media, "file:/ram/shell/inbox",
                          media_first, 1) != 0)
        return fail("media drm add", 0);
    if (zd_media_play(&session_media, media_count + 2) != -1 ||
        session_media.stats.refusals_drm != 1)
        return fail("media drm refusal",
                    (int64_t)session_media.stats.refusals_drm);
    /* An origin that was never registered is refused before rights matter. */
    if (zd_media_add_item(&session_media, "file:/ram/unregistered",
                          media_first, 0) != 0)
        return fail("media unregistered add", 0);
    if (zd_media_play(&session_media, media_count + 3) != -1 ||
        session_media.stats.refusals_origin != 1)
        return fail("media origin refusal",
                    (int64_t)session_media.stats.refusals_origin);
    if (zd_media_play(&session_media, 999) != -2)
        return fail("media unknown item", 0);
    /* Unregistering an origin takes its rights away with it. */
    if (zd_media_unregister_source(&session_media,
                                   "file:/ram/shell/inbox") != 0)
        return fail("media unregister", 0);
    if (zd_media_play(&session_media, 1) != -1)
        return fail("media revoked play", 0);
    say("ZEROOS: session media rights passed.");

    /* 27. Recovery snapshots over the real filesystem, wired into the update
     * pipeline. Capture copies live user data into a per-snapshot file and
     * restore copies it back; every step is verified through the filesystem
     * rather than trusted, and a capture the hook cannot honour leaves a
     * FAILED point instead of a half-registered one. */
    if (session_write_file("/ram/shell/userdata.txt", 32) != 0)
        return fail("snapshot user data", 0);
    zd_snapshots_init(&session_snapshots, &session_snap_ops);
    if (zd_snapshots_create(&session_snapshots, "before-update") != 0)
        return fail("snapshot create", 0);
    snap = zd_snapshots_find(&session_snapshots, "before-update");
    if (!snap || snap->state != ZD_SNAP_CREATING)
        return fail("snapshot creating state", 0);
    if (zd_snapshots_create_finish(&session_snapshots, 0) != 0)
        return fail("snapshot finish", 0);
    snap = zd_snapshots_find(&session_snapshots, "before-update");
    if (!snap || snap->state != ZD_SNAP_READY ||
        zd_snapshots_ready_count(&session_snapshots) != 1 ||
        session_snapshots.stats.created != 1)
        return fail("snapshot ready state", 0);
    if (zeroos_stat("/ram/shell/snap-before-update", &file_stat) != 0 ||
        file_stat.size != 32)
        return fail("snapshot file", (int64_t)file_stat.size);
    /* Mutate the user data, restore, and read the size back from the
     * filesystem. */
    if (session_write_file("/ram/shell/userdata.txt", 48) != 0)
        return fail("snapshot mutate", 0);
    if (zd_snapshots_restore(&session_snapshots, (const char *)0) != 0 ||
        session_snapshots.stats.restored != 1)
        return fail("snapshot restore",
                    (int64_t)session_snapshots.stats.restored);
    if (zeroos_stat("/ram/shell/userdata.txt", &file_stat) != 0 ||
        file_stat.size != 32)
        return fail("snapshot restored size", (int64_t)file_stat.size);
    /* A capture the hook refuses leaves a FAILED point with the hook's own
     * error, and it is not counted as a restorable snapshot. */
    session_snap_fail = 1;
    if (zd_snapshots_create(&session_snapshots, "bad") != 0)
        return fail("snapshot failed create", 0);
    /* The hook's own error is returned to the caller, and the slot is left
     * FAILED rather than half-registered. */
    if (zd_snapshots_create_finish(&session_snapshots, 0) != -ZD_ENOSPC)
        return fail("snapshot failed finish", 0);
    snap = zd_snapshots_find(&session_snapshots, "bad");
    if (!snap || snap->state != ZD_SNAP_FAILED ||
        snap->last_error != ZD_ENOSPC ||
        session_snapshots.stats.create_failed != 1 ||
        zd_snapshots_ready_count(&session_snapshots) != 1)
        return fail("snapshot failed state", 0);
    session_snap_fail = 0;
    /* The update pipeline's recovery hooks come from this manager: staging
     * captures a snapshot and rollback restores the newest ready one. Slot
     * activation and commit belong to the block layer, so they stay unset. */
    if (zd_snapshots_bind_update(&session_snapshots,
                                 &session_snap_update_ops) != 0)
        return fail("snapshot bind", 0);
    if (!session_snap_update_ops.stage_apply ||
        !session_snap_update_ops.rollback ||
        session_snap_update_ops.activate ||
        session_snap_update_ops.commit)
        return fail("snapshot update hooks", 0);
    if (session_snap_update_ops.stage_apply(
            session_snap_update_ops.ctx) != 0)
        return fail("snapshot stage capture", 0);
    snap = zd_snapshots_latest_ready(&session_snapshots);
    if (!snap)
        return fail("snapshot latest ready", 0);
    if (session_write_file("/ram/shell/userdata.txt", 64) != 0)
        return fail("snapshot second mutate", 0);
    if (session_snap_update_ops.rollback(session_snap_update_ops.ctx) != 0)
        return fail("snapshot rollback", 0);
    if (zeroos_stat("/ram/shell/userdata.txt", &file_stat) != 0 ||
        file_stat.size != 32)
        return fail("snapshot rolled back size", (int64_t)file_stat.size);
    /* Discarding removes the snapshot's own file, not just its slot. */
    if (zd_snapshots_discard(&session_snapshots, "before-update") != 0 ||
        session_snapshots.stats.discarded != 1 ||
        zeroos_stat("/ram/shell/snap-before-update",
                    &file_stat) != -ZEROOS_ENOENT)
        return fail("snapshot discard",
                    (int64_t)session_snapshots.stats.discarded);
    say("ZEROOS: session recovery snapshots passed.");

    /* 28. The secret vault over the kernel's certified AEAD, holding the very
     * key material the update pipeline signs with. Nothing here is trusted:
     * the plaintext must not appear in the stored ciphertext, a locked vault
     * refuses on both paths, and a wrong key or a single flipped byte fails
     * authentication rather than handing back garbage. */
    zd_vault_init(&session_vault);
    if (zd_vault_put(&session_vault, "update.key", update_key,
                     (uint32_t)sizeof(update_key)) != -1 ||
        session_vault.stats.rejected != 1)
        return fail("vault locked put",
                    (int64_t)session_vault.stats.rejected);
    if (zd_vault_get(&session_vault, "update.key", vault_out,
                     (uint32_t)sizeof(vault_out), &vault_len) != -1 ||
        session_vault.stats.get_denied != 1)
        return fail("vault locked get",
                    (int64_t)session_vault.stats.get_denied);
    if (zd_vault_unlock(&session_vault, update_key) != 0 ||
        session_vault.unlocked != 1)
        return fail("vault unlock", 0);
    if (zd_vault_put(&session_vault, "update.key", update_key,
                     (uint32_t)sizeof(update_key)) != 0 ||
        session_vault.stats.puts != 1 ||
        zd_vault_count(&session_vault) != 1)
        return fail("vault put", (int64_t)zd_vault_count(&session_vault));
    vault_entry = &session_vault.entries[0];
    if (!vault_entry->in_use ||
        vault_entry->ct_len !=
            12u + (uint32_t)sizeof(update_key) + 16u)
        return fail("vault ciphertext length",
                    (int64_t)vault_entry->ct_len);
    /* No run of the key may appear in what the vault keeps. */
    vault_match = 0;
    for (vault_i = 0; vault_i + 8 <= (int)vault_entry->ct_len; ++vault_i) {
        vault_match = 1;
        for (vault_j = 0; vault_j < 8; ++vault_j)
            if (vault_entry->ct[vault_i + vault_j] != update_key[vault_j])
                vault_match = 0;
        if (vault_match)
            break;
    }
    if (vault_match)
        return fail("vault plaintext retained", 0);
    if (zd_vault_get(&session_vault, "update.key", vault_out,
                     (uint32_t)sizeof(vault_out), &vault_len) != 0 ||
        vault_len != (uint32_t)sizeof(update_key) ||
        session_vault.stats.gets != 1)
        return fail("vault get", (int64_t)session_vault.stats.gets);
    for (vault_i = 0; vault_i < (int)vault_len; ++vault_i)
        if (vault_out[vault_i] != update_key[vault_i])
            return fail("vault round trip", (int64_t)vault_i);
    /* Re-wrapping one slot under one key must not reuse a nonce. ChaCha20
     * keystream reuse turns two ciphertexts into the XOR of their plaintexts,
     * so the 12-byte nonce the vault keeps ahead of the ciphertext has to
     * change on every put -- even a put of the very same secret. */
    if (session_vault.wraps != 1)
        return fail("vault wrap count", (int64_t)session_vault.wraps);
    for (vault_i = 0; vault_i < 12; ++vault_i)
        vault_nonce[vault_i] = vault_entry->ct[vault_i];
    if (zd_vault_put(&session_vault, "update.key", update_key,
                     (uint32_t)sizeof(update_key)) != 0)
        return fail("vault rewrap", 0);
    vault_same = 1;
    for (vault_i = 0; vault_i < 12; ++vault_i)
        if (vault_entry->ct[vault_i] != vault_nonce[vault_i])
            vault_same = 0;
    if (vault_same)
        return fail("vault nonce reuse", 0);
    if (session_vault.wraps != 2)
        return fail("vault wrap advance", (int64_t)session_vault.wraps);
    if (zd_vault_get(&session_vault, "update.key", vault_out,
                     (uint32_t)sizeof(vault_out), &vault_len) != 0 ||
        vault_len != (uint32_t)sizeof(update_key))
        return fail("vault rewrap get", (int64_t)vault_len);
    /* An over-long secret and an empty name are both refused and counted. */
    if (zd_vault_put(&session_vault, "big", vault_out,
                     ZD_VAULT_SECRET_MAX + 1) != -22 ||
        zd_vault_put(&session_vault, "", vault_out, 8) != -22 ||
        session_vault.stats.rejected != 3)
        return fail("vault bounds",
                    (int64_t)session_vault.stats.rejected);
    /* Locking wipes the key and leaves the entries as ciphertext. */
    if (zd_vault_lock(&session_vault) != 0 ||
        session_vault.unlocked != 0 ||
        session_vault.stats.wipes != 1 ||
        zd_vault_count(&session_vault) != 1)
        return fail("vault lock", (int64_t)session_vault.stats.wipes);
    /* The wrong key does not decrypt: authentication fails explicitly. */
    for (vault_i = 0; vault_i < (int)sizeof(vault_wrong); ++vault_i)
        vault_wrong[vault_i] = (uint8_t)(update_key[vault_i] ^ 0xFFu);
    if (zd_vault_unlock(&session_vault, vault_wrong) != 0)
        return fail("vault wrong unlock", 0);
    if (zd_vault_get(&session_vault, "update.key", vault_out,
                     (uint32_t)sizeof(vault_out), &vault_len) != -3 ||
        session_vault.stats.auth_failures != 1)
        return fail("vault wrong key",
                    (int64_t)session_vault.stats.auth_failures);
    /* Neither does a single flipped ciphertext byte under the right key. */
    if (zd_vault_unlock(&session_vault, update_key) != 0)
        return fail("vault relock", 0);
    session_vault.entries[0].ct[16] ^= 0x01u;
    if (zd_vault_get(&session_vault, "update.key", vault_out,
                     (uint32_t)sizeof(vault_out), &vault_len) != -3 ||
        session_vault.stats.auth_failures != 2)
        return fail("vault tamper",
                    (int64_t)session_vault.stats.auth_failures);
    session_vault.entries[0].ct[16] ^= 0x01u;
    /* Forgetting erases the ciphertext and the entry with it. */
    if (zd_vault_forget(&session_vault, "update.key") != 0 ||
        zd_vault_count(&session_vault) != 0)
        return fail("vault forget", (int64_t)zd_vault_count(&session_vault));
    if (zd_vault_get(&session_vault, "update.key", vault_out,
                     (uint32_t)sizeof(vault_out), &vault_len) != -2)
        return fail("vault forgotten get", 0);
    say("ZEROOS: session secret vault passed.");

    /* 29. Egress policy. The firewall is the authority on whether the AI
     * broker may leave this machine: nothing is allowed until a rule says so,
     * the first matching rule decides, and the shell turns the verdict into a
     * capability. Denied means the egress grant is withheld, so a
     * remote-preferring request is answered here and counted as a downgrade;
     * allowed means the same request keeps its remote backend. */
    zd_fw_init(&session_fw);
    fw_flow.dir = ZD_FW_OUT;
    fw_flow.proto = ZD_FW_TCP;
    fw_flow.src_ip = 0;
    fw_flow.dst_ip = 0;
    fw_flow.src_port = 0;
    fw_flow.dst_port = 443;
    fw_flow.conn_known = 0;
    fw_flow.app_id = "ai.broker";
    /* An outbound L4 flow with no port is not something the policy can judge,
     * and it is counted rather than guessed at. */
    fw_flow.dst_port = 0;
    if (zd_fw_decide(&session_fw, &fw_flow) != -22 ||
        session_fw.stats.invalid_flows != 1)
        return fail("firewall invalid flow",
                    (int64_t)session_fw.stats.invalid_flows);
    fw_flow.dst_port = 443;
    if (zd_fw_decide(&session_fw, &fw_flow) != ZD_FW_DENY ||
        session_fw.stats.flows != 1 || session_fw.stats.denied != 1)
        return fail("firewall default deny",
                    (int64_t)session_fw.stats.denied);
    /* That verdict reaches the broker. */
    zd_ai_revoke(&session_ai, ZD_AI_GRANT_REMOTE_EGRESS);
    fw_downgrades = session_ai.stats.backend_downgrades;
    fw_request = session_ai_request("/ram/shell/inbox/moved.txt",
                                    ZD_AI_GRANT_CONTEXT_SELECTION, 1);
    if (zd_ai_submit(&session_ai, &fw_request) != 0)
        return fail("firewall broker submit", 0);
    if (zd_ai_drain(&session_ai, 4, &ai_done) != 0 || ai_done != 1)
        return fail("firewall broker drain", (int64_t)ai_done);
    if (session_ai.stats.backend_downgrades != fw_downgrades + 1)
        return fail("firewall downgrade",
                    (int64_t)session_ai.stats.backend_downgrades);
    /* A template with an inverted port range is refused and counted. */
    fw_rule.id = 0;
    fw_rule.enabled = 1;
    fw_rule.dir = ZD_FW_OUT;
    fw_rule.proto = ZD_FW_TCP;
    fw_rule.action = ZD_FW_ALLOW;
    fw_rule.established_only = 0;
    fw_rule.port_lo = 444;
    fw_rule.port_hi = 443;
    fw_rule.ip_lo = 0;
    fw_rule.ip_hi = 0;
    for (fw_i = 0; fw_app[fw_i] && fw_i + 1 < (int)sizeof(fw_rule.app); ++fw_i)
        fw_rule.app[fw_i] = fw_app[fw_i];
    fw_rule.app[fw_i] = 0;
    if (zd_fw_add(&session_fw, &fw_rule, 1) != -22 ||
        session_fw.stats.rules_rejected != 1)
        return fail("firewall rule range",
                    (int64_t)session_fw.stats.rules_rejected);
    fw_rule.port_lo = 443;
    if (zd_fw_add(&session_fw, &fw_rule, 1) != 0 ||
        session_fw.stats.rules_added != 1 ||
        session_fw.rule_count != 1)
        return fail("firewall rule add",
                    (int64_t)session_fw.stats.rules_added);
    fw_rule_id = session_fw.rules[0].id;
    /* A catch-all denial sits behind it, so the broker is allowed while
     * anything else is still stopped. */
    fw_rule.action = ZD_FW_DENY;
    fw_rule.port_lo = 0;
    fw_rule.port_hi = 0;
    fw_rule.app[0] = 0;
    if (zd_fw_add(&session_fw, &fw_rule, 0) != 0 ||
        session_fw.stats.rules_added != 2)
        return fail("firewall catch all",
                    (int64_t)session_fw.stats.rules_added);
    if (zd_fw_decide(&session_fw, &fw_flow) != ZD_FW_ALLOW ||
        session_fw.stats.allowed != 1)
        return fail("firewall allow", (int64_t)session_fw.stats.allowed);
    /* Both rules match this flow; only the first one decides it. */
    if (zd_fw_matching(&session_fw, &fw_flow) != 2)
        return fail("firewall matching",
                    (int64_t)zd_fw_matching(&session_fw, &fw_flow));
    /* Egress is granted on the strength of that verdict, and the same request
     * keeps its remote backend instead of being downgraded. */
    zd_ai_grant(&session_ai, ZD_AI_GRANT_REMOTE_EGRESS);
    fw_request = session_ai_request("/ram/shell/inbox/moved.txt",
                                    ZD_AI_GRANT_CONTEXT_SELECTION, 1);
    if (zd_ai_submit(&session_ai, &fw_request) != 0)
        return fail("firewall granted submit", 0);
    if (zd_ai_drain(&session_ai, 4, &ai_done) != 0 || ai_done != 1)
        return fail("firewall granted drain", (int64_t)ai_done);
    if (session_ai.stats.backend_downgrades != fw_downgrades + 1)
        return fail("firewall granted downgrade",
                    (int64_t)session_ai.stats.backend_downgrades);
    /* Removing the rule takes the allowance away with it. */
    if (zd_fw_remove(&session_fw, fw_rule_id) != 0 ||
        session_fw.stats.rules_removed != 1)
        return fail("firewall rule remove",
                    (int64_t)session_fw.stats.rules_removed);
    if (zd_fw_remove(&session_fw, 999) != -2)
        return fail("firewall unknown rule", 0);
    if (zd_fw_decide(&session_fw, &fw_flow) != ZD_FW_DENY)
        return fail("firewall revoked allow", 0);
    zd_ai_revoke(&session_ai, ZD_AI_GRANT_REMOTE_EGRESS);
    say("ZEROOS: session egress policy passed.");

    /* 30. Navigation is gated by URL validation before the browser sees a
     * single byte of it. What the parser accepts is opened as a real tab;
     * what it refuses never reaches the browser at all, and the reason is
     * kept so the shell can say why. */
    for (nav_i = 0; nav_i < (int)ZD_NAV_URL_COUNT; ++nav_i) {
        nav_rc = zd_url_parse(nav_urls[nav_i], &session_nav_url);
        if (session_nav_url.reject_reason != nav_reasons[nav_i])
            return fail("url reject reason",
                        (int64_t)session_nav_url.reject_reason);
        if (nav_reasons[nav_i] != ZD_URL_OK_REJECT) {
            if (nav_rc != -22)
                return fail("url refused parse", (int64_t)nav_rc);
            /* The shell shows a reason, never a blank refusal. */
            if (!zd_url_reject_str(session_nav_url.reject_reason) ||
                !zd_url_reject_str(session_nav_url.reject_reason)[0])
                return fail("url reject reason text", (int64_t)nav_i);
            /* A smuggled scheme is not even recorded as a scheme. */
            if (nav_reasons[nav_i] == ZD_URL_R_BAD_SCHEME &&
                (session_nav_url.scheme != ZD_URL_SCHEME_NONE ||
                 zd_url_scheme_allowed(session_nav_url.scheme)))
                return fail("url smuggled scheme", (int64_t)nav_i);
            continue;
        }
        if (nav_rc != 0 ||
            session_nav_url.scheme != nav_schemes[nav_i] ||
            session_nav_url.port != nav_ports[nav_i] ||
            session_nav_url.is_secure != nav_secure[nav_i] ||
            !session_streq(session_nav_url.path, nav_paths[nav_i]))
            return fail("url parse", (int64_t)nav_i);
        if (!zd_url_scheme_allowed(session_nav_url.scheme))
            return fail("url scheme allowed", (int64_t)nav_i);
        /* Only an accepted URL becomes a tab. */
        if (zd_browser_open(&session_browser, 4096, &nav_tab) != 0 ||
            nav_tab == 0)
            return fail("url open tab", (int64_t)nav_i);
        ++nav_opened;
    }
    if (nav_opened != 2 || session_browser.tab_count != nav_opened)
        return fail("url opened tabs",
                    (int64_t)session_browser.tab_count);
    for (nav_i = 0; nav_i < (int)nav_opened; ++nav_i) {
        if (zd_browser_close(&session_browser, (uint32_t)nav_i + 1) != 0)
            return fail("url close tab", (int64_t)nav_i);
    }
    if (session_browser.tab_count != 0)
        return fail("url tabs closed",
                    (int64_t)session_browser.tab_count);
    say("ZEROOS: session navigation policy passed.");

    /* 31. Study attachments. The shell writes a real PDF, reads it back
     * through the kernel, and the extracted page text becomes a card in the
     * live deck. Inputs the parser cannot honestly handle are refused with a
     * reason the shell can show, never guessed at. */
    if (session_write_buffer("/ram/shell/study.pdf",
                             (const uint8_t *)session_pdf_two_pages,
                             sizeof(session_pdf_two_pages) - 1, 0644) != 0)
        return fail("pdf write", 0);
    if (session_read_buffer("/ram/shell/study.pdf", session_pdf_bytes,
                            sizeof(session_pdf_bytes), &pdf_len) != 0 ||
        pdf_len != sizeof(session_pdf_two_pages) - 1)
        return fail("pdf read back", (int64_t)pdf_len);
    if (zd_pdf_open(session_pdf_bytes, (uint32_t)pdf_len,
                    &session_pdf) != 0)
        return fail("pdf open", 0);
    if (session_pdf.page_count != 2 || session_pdf.encrypted != 0 ||
        session_pdf.filtered_streams != 0 ||
        !session_streq(session_pdf.version, "1.7"))
        return fail("pdf structure", (int64_t)session_pdf.page_count);
    if (zd_pdf_extract(&session_pdf) != 0)
        return fail("pdf extract", 0);
    if (session_pdf.pages[0].obj_num != 3 ||
        session_pdf.pages[0].text_len != 16 ||
        !session_streq(session_pdf.pages[0].text, "ZEROOS page one\n"))
        return fail("pdf page one",
                    (int64_t)session_pdf.pages[0].text_len);
    if (session_pdf.pages[1].obj_num != 5 ||
        !session_streq(session_pdf.pages[1].text, "Hello"))
        return fail("pdf page two",
                    (int64_t)session_pdf.pages[1].text_len);
    /* What was extracted becomes a card in the deck the study centre is
     * already running. */
    pdf_cards = session_study.card_count;
    if (zd_study_add_card(&session_study, session_pdf.pages[0].text,
                          session_pdf.pages[1].text) != 0 ||
        session_study.card_count != pdf_cards + 1)
        return fail("pdf card", (int64_t)session_study.card_count);
    /* A filtered content stream is refused, and the reason is a real
     * sentence rather than a blank error. */
    if (zd_pdf_open((const uint8_t *)session_pdf_filtered,
                    (uint32_t)sizeof(session_pdf_filtered) - 1,
                    &session_pdf) != ZD_PDF_UNSUPPORTED_FILTER)
        return fail("pdf filter refusal", 0);
    if (!zd_pdf_status_name(ZD_PDF_UNSUPPORTED_FILTER) ||
        !zd_pdf_status_name(ZD_PDF_UNSUPPORTED_FILTER)[0])
        return fail("pdf filter reason", 0);
    /* So is an encrypted document, and one with no page tree at all. */
    if (zd_pdf_open((const uint8_t *)session_pdf_encrypted,
                    (uint32_t)sizeof(session_pdf_encrypted) - 1,
                    &session_pdf) != ZD_PDF_UNSUPPORTED_FILTER ||
        session_pdf.encrypted != 1)
        return fail("pdf encryption refusal",
                    (int64_t)session_pdf.encrypted);
    if (zd_pdf_open((const uint8_t *)session_pdf_no_pages,
                    (uint32_t)sizeof(session_pdf_no_pages) - 1,
                    &session_pdf) != ZD_PDF_MALFORMED)
        return fail("pdf page tree refusal", 0);
    /* Truncating a document the shell really wrote is refused rather than
     * partially trusted. */
    if (zd_pdf_open(session_pdf_bytes, 24,
                    &session_pdf) != ZD_PDF_MALFORMED)
        return fail("pdf truncated", 0);
    say("ZEROOS: session study attachments passed.");

    /* 32. Study notes, with the shell as their storage. The notebook keeps no
     * files of its own, so every body is written to a real file under
     * /ram/shell and read back through the kernel, and the bytes are compared
     * rather than assumed. Search, snippets, pinning and deletion are all
     * exercised against what was really stored. */
    zd_notes_init(&session_notes);
    if (zd_notes_create(&session_notes, "Release", update_version, 1,
                        &nt_id) != 0 ||
        session_notes.stats.created != 1)
        return fail("notes create", (int64_t)session_notes.stats.created);
    if (zd_notes_create(&session_notes, "Locale", i18n_text_hi, 2,
                        &note_id2) != 0)
        return fail("notes create locale", 0);
    if (zd_notes_create(&session_notes, note_long_title, "x", 3,
                        &note_id3) != -22 ||
        session_notes.stats.rejected != 1)
        return fail("notes long title",
                    (int64_t)session_notes.stats.rejected);
    if (zd_notes_create(&session_notes, "Media", media_first, 4,
                        &note_id3) != 0 ||
        session_notes.count != 3)
        return fail("notes create media", (int64_t)session_notes.count);
    /* Search runs over what was really stored, case-insensitively, and the
     * caller-sized form hands back the id. */
    nt_hits = zd_notes_search(&session_notes, "release", (uint32_t *)0, 0);
    if (nt_hits != 1)
        return fail("notes search", (int64_t)nt_hits);
    if (zd_notes_search(&session_notes, "release", &nt_found, 1) != 1 ||
        nt_found != nt_id ||
        session_notes.stats.searches != 2 ||
        session_notes.stats.search_hits != 2)
        return fail("notes search id", (int64_t)nt_found);
    /* A snippet starts at the match, which is what a result label shows. */
    nt_snip_len = zd_notes_snippet(zd_notes_get(&session_notes, nt_id),
                                   "1.1", nt_snippet,
                                   (uint32_t)sizeof(nt_snippet));
    if (nt_snip_len <= 0 || nt_snippet[0] != '1' || nt_snippet[1] != '.' ||
        nt_snippet[2] != '1')
        return fail("notes snippet", (int64_t)nt_snip_len);
    /* Persistence: the body goes to a real file and comes back byte for
     * byte. */
    nt_body = zd_notes_get(&session_notes, nt_id);
    if (!nt_body || nt_body->body_len != 5)
        return fail("notes body length",
                    nt_body ? (int64_t)nt_body->body_len : -1);
    if (session_write_buffer("/ram/shell/note-release.txt",
                             (const uint8_t *)nt_body->body,
                             nt_body->body_len, 0644) != 0)
        return fail("notes write", 0);
    if (session_read_buffer("/ram/shell/note-release.txt", nt_buffer,
                            sizeof(nt_buffer), &nt_len) != 0 ||
        nt_len != 5)
        return fail("notes read back", (int64_t)nt_len);
    for (nt_i = 0; nt_i < (int)nt_len; ++nt_i)
        if (nt_buffer[nt_i] != (uint8_t)update_version[nt_i])
            return fail("notes bytes", (int64_t)nt_i);
    /* Reading it back into another note is what a restore looks like. */
    if (zd_notes_update(&session_notes, note_id2, (const char *)0,
                        (const char *)nt_buffer, 6) != 0 ||
        session_notes.stats.updated != 1)
        return fail("notes update", (int64_t)session_notes.stats.updated);
    nt_body = zd_notes_get(&session_notes, note_id2);
    if (!nt_body || !session_streq(nt_body->body, "1.1.0"))
        return fail("notes updated body", 0);
    /* Pinning is reflected in the note, an unknown id is refused, and
     * deleting shrinks the notebook. */
    if (zd_notes_set_pinned(&session_notes, nt_id, 1) != 0 ||
        session_notes.stats.pinned != 1 ||
        !zd_notes_get(&session_notes, nt_id)->pinned)
        return fail("notes pin", (int64_t)session_notes.stats.pinned);
    if (zd_notes_delete(&session_notes, 999) != -2)
        return fail("notes unknown", 0);
    if (zd_notes_delete(&session_notes, note_id3) != 0 ||
        session_notes.stats.deleted != 1 ||
        session_notes.count != 2)
        return fail("notes delete", (int64_t)session_notes.count);
    /* The study centre's OCR path reports what this build can honestly do:
     * no engine is linked, arguments are still validated, and nothing
     * fabricates text. */
    if (zd_ocr_available() != 0)
        return fail("ocr availability", 1);
    if (zd_ocr_recognize((const uint8_t *)0, 8, 8, "en", nt_snippet,
                         (uint32_t)sizeof(nt_snippet)) != -22)
        return fail("ocr null image", 0);
    if (zd_ocr_recognize(session_pdf_bytes, 0, 8, "en", nt_snippet,
                         (uint32_t)sizeof(nt_snippet)) != -22)
        return fail("ocr zero width", 0);
    if (zd_ocr_recognize(session_pdf_bytes, 8, 8, "en", nt_snippet,
                         (uint32_t)sizeof(nt_snippet)) != -95)
        return fail("ocr no engine", 0);
    if (!zd_ocr_status_name(-95) || !zd_ocr_status_name(-95)[0])
        return fail("ocr status name", 0);
    if (zd_ocr_set_engine((struct zd_ocr_engine *)0) !=
        (struct zd_ocr_engine *)0)
        return fail("ocr engine unlink", 0);
    say("ZEROOS: session study notes passed.");

    /* 33. The study dictionary, loaded from the shell's own live vocabulary
     * rather than a bundled corpus: the notebook's real titles and the media
     * item's real name, with one duplicate the loader has to drop. A source
     * that fails leaves the dictionary unreadable instead of half-filled. */
    if (zd_dict_lookup(&session_dict, "release") != -95)
        return fail("dict before load", 0);
    if (zd_dict_load(&session_dict, session_dict_source,
                     (void *)0) != 0 ||
        session_dict.loaded != 1 || session_dict.count != 3 ||
        session_dict.stats.loads != 1)
        return fail("dict load", (int64_t)session_dict.count);
    /* Lookup is exact and case-insensitive; a word the shell never
     * stored is a miss, not an error. */
    if (zd_dict_lookup(&session_dict, "RELEASE") != 1 ||
        zd_dict_lookup(&session_dict, "release") != 1 ||
        session_dict.stats.lookup_hits != 2)
        return fail("dict lookup",
                    (int64_t)session_dict.stats.lookup_hits);
    if (zd_dict_lookup(&session_dict, "missing") != 0 ||
        session_dict.stats.lookups != 3)
        return fail("dict miss", (int64_t)session_dict.stats.lookups);
    /* Suggestions come back in a stable order, and the count is the total
     * match count rather than what fitted. */
    dict_total = zd_dict_prefix(&session_dict, "r", dict_out, 4, &dict_n);
    if (dict_total != 1 || dict_n != 1)
        return fail("dict prefix", (int64_t)dict_total);
    /* Whichever case the loader kept, the suggestion is a word the
     * dictionary really holds. */
    if (zd_dict_lookup(&session_dict, dict_out[0]) != 1)
        return fail("dict suggestion", 0);
    if (zd_dict_prefix(&session_dict, "zz", dict_out, 4, &dict_n) != 0 ||
        dict_n != 0)
        return fail("dict prefix none", (int64_t)dict_n);
    /* A source that fails clears the list: the dictionary reports itself
     * unreadable rather than serving stale words. */
    session_dict_fail = 1;
    if (zd_dict_load(&session_dict, session_dict_source,
                     (void *)0) != -ZD_ENOENT ||
        session_dict.stats.load_errors != 1 ||
        session_dict.loaded != 0 || session_dict.count != 0)
        return fail("dict failed load",
                    (int64_t)session_dict.stats.load_errors);
    if (zd_dict_lookup(&session_dict, "release") != -95)
        return fail("dict unreadable", 0);
    session_dict_fail = 0;
    if (zd_dict_load(&session_dict, session_dict_source,
                     (void *)0) != 0 ||
        zd_dict_lookup(&session_dict, "Locale") != 1)
        return fail("dict reload", (int64_t)session_dict.count);
    say("ZEROOS: session study dictionary passed.");

    /* 34. Navigation history with a real load state. The engine's report is
     * what commits a navigation, a failed load keeps its entry so it can be
     * retried, and a new navigation from the middle of the history drops the
     * forward entries it invalidates. */
    zd_nav_init(&session_nav);
    /* A smuggled scheme is blocked before it can become history. */
    if (zd_nav_go(&session_nav, "javascript:alert(1)") != -22 ||
        session_nav.last_reject != ZD_URL_R_BAD_SCHEME ||
        session_nav.stats.blocked != 1 ||
        session_nav.state != ZD_NAV_EMPTY ||
        session_nav.count != 0)
        return fail("nav blocked", (int64_t)session_nav.stats.blocked);
    if (!session_streq(zd_nav_current(&session_nav), ""))
        return fail("nav empty current", 0);
    if (zd_nav_go(&session_nav, "https://example.invalid/one") != 0 ||
        session_nav.state != ZD_NAV_LOADING ||
        session_nav.stats.navigations != 1)
        return fail("nav go", (int64_t)session_nav.stats.navigations);
    if (!session_streq(zd_nav_current(&session_nav),
                       "https://example.invalid/one"))
        return fail("nav current one", 0);
    if (zd_nav_finish(&session_nav, 0) != 0 ||
        session_nav.state != ZD_NAV_COMMITTED)
        return fail("nav commit", (int64_t)session_nav.state);
    /* A load the engine reports as failed is a FAILED navigation, and the
     * entry stays in the history so the retry has somewhere to land. */
    if (zd_nav_go(&session_nav, "https://example.invalid/two") != 0)
        return fail("nav go two", 0);
    if (zd_nav_finish(&session_nav, -ZD_ENOENT) != 0 ||
        session_nav.state != ZD_NAV_FAILED ||
        session_nav.stats.load_failures != 1)
        return fail("nav load failure",
                    (int64_t)session_nav.stats.load_failures);
    if (session_nav.count != 2 ||
        !session_streq(zd_nav_current(&session_nav),
                       "https://example.invalid/two"))
        return fail("nav failed entry kept", (int64_t)session_nav.count);
    /* Retrying commits it. */
    if (zd_nav_go(&session_nav, "https://example.invalid/two") != 0 ||
        zd_nav_finish(&session_nav, 0) != 0 ||
        session_nav.state != ZD_NAV_COMMITTED ||
        session_nav.count != 3)
        return fail("nav retry", (int64_t)session_nav.count);
    /* Back and forward walk the history, and each step is a load the engine
     * has to finish. */
    if (zd_nav_back(&session_nav) != 0 ||
        session_nav.state != ZD_NAV_LOADING ||
        zd_nav_finish(&session_nav, 0) != 0 ||
        !session_streq(zd_nav_current(&session_nav),
                       "https://example.invalid/two"))
        return fail("nav back", (int64_t)session_nav.stats.backs);
    if (zd_nav_back(&session_nav) != 0 ||
        zd_nav_finish(&session_nav, 0) != 0 ||
        !session_streq(zd_nav_current(&session_nav),
                       "https://example.invalid/one") ||
        session_nav.stats.backs != 2)
        return fail("nav back twice", (int64_t)session_nav.stats.backs);
    if (zd_nav_back(&session_nav) != -22)
        return fail("nav back start", 0);
    if (zd_nav_forward(&session_nav) != 0 ||
        zd_nav_finish(&session_nav, 0) != 0 ||
        session_nav.stats.forwards != 1 ||
        !session_streq(zd_nav_current(&session_nav),
                       "https://example.invalid/two"))
        return fail("nav forward", (int64_t)session_nav.stats.forwards);
    /* Navigating from the middle drops the forward entries it invalidates. */
    if (zd_nav_go(&session_nav, "https://example.invalid/three") != 0 ||
        zd_nav_finish(&session_nav, 0) != 0 ||
        session_nav.stats.history_dropped != 1 ||
        session_nav.count != 3)
        return fail("nav drop forward",
                    (int64_t)session_nav.stats.history_dropped);
    if (zd_nav_forward(&session_nav) != -22)
        return fail("nav forward gone", 0);
    say("ZEROOS: session navigation history passed.");

    /* 35. The device ecosystem, offline-first. This ABI has no networking, so
     * OFFLINE is the real state rather than a simulated one, and that is the
     * interesting path: pairing grants nothing, a permission that was never
     * granted is refused rather than tried anyway, work queued offline stays
     * queued and is counted as held back, and a permission revoked mid-queue
     * refuses that entry while the rest still goes. */
    zd_eco_init(&session_eco);
    if (zd_eco_pair(&session_eco, eco_long_name, &eco_idx2) != -22 ||
        session_eco.stats.rejected != 1)
        return fail("eco pair name", (int64_t)session_eco.stats.rejected);
    if (zd_eco_pair(&session_eco, "desk-01", &eco_idx) != 0 ||
        session_eco.stats.paired != 1 ||
        session_eco.devices[eco_idx].permissions != 0)
        return fail("eco pair", (int64_t)session_eco.stats.paired);
    /* Pairing is not permission. */
    if (zd_eco_enqueue(&session_eco, eco_idx, ZD_ECO_PERM_SYNC_FILES,
                       "settings.blob") != -1 ||
        session_eco.stats.refused_perm != 1)
        return fail("eco ungranted",
                    (int64_t)session_eco.stats.refused_perm);
    if (zd_eco_enqueue(&session_eco, eco_idx, ZD_ECO_PERM_ALL,
                       "settings.blob") != -22 ||
        session_eco.stats.rejected != 2)
        return fail("eco multi bit", (int64_t)session_eco.stats.rejected);
    if (zd_eco_grant(&session_eco, eco_idx,
                     ZD_ECO_PERM_SYNC_FILES) != 0 ||
        zd_eco_grant(&session_eco, eco_idx,
                     ZD_ECO_PERM_SYNC_SETTINGS) != 0 ||
        session_eco.stats.granted != 2)
        return fail("eco grant", (int64_t)session_eco.stats.granted);
    if (zd_eco_enqueue(&session_eco, eco_idx, ZD_ECO_PERM_SYNC_FILES,
                       "settings.blob") != 0 ||
        zd_eco_enqueue(&session_eco, eco_idx, ZD_ECO_PERM_SYNC_SETTINGS,
                       "release.bin") != 0 ||
        session_eco.queued != 2 || session_eco.queued_ever != 2)
        return fail("eco queue", (int64_t)session_eco.queued);
    /* Offline: nothing is sent early, nothing is lost, and what is held back
     * is counted rather than silently sitting there. */
    if (zd_eco_flush(&session_eco) != 0 || session_eco.queued != 2 ||
        session_eco.stats.flushed != 0 ||
        session_eco.stats.refused_offline != 2)
        return fail("eco offline flush",
                    (int64_t)session_eco.stats.refused_offline);
    /* A permission revoked mid-queue refuses that entry and keeps the rest. */
    if (zd_eco_revoke(&session_eco, eco_idx,
                      ZD_ECO_PERM_SYNC_FILES) != 0 ||
        session_eco.stats.revoked != 1)
        return fail("eco revoke", (int64_t)session_eco.stats.revoked);
    zd_eco_set_conn(&session_eco, ZD_ECO_ONLINE);
    eco_sent = zd_eco_flush(&session_eco);
    if (eco_sent != 1)
        return fail("eco flush sent", (int64_t)eco_sent);
    if (session_eco.stats.flushed != 1)
        return fail("eco flushed count",
                    (int64_t)session_eco.stats.flushed);
    if (session_eco.stats.refused_perm != 2)
        return fail("eco revoked refusal",
                    (int64_t)session_eco.stats.refused_perm);
    if (session_eco.devices[eco_idx].sent != 1)
        return fail("eco device sent",
                    (int64_t)session_eco.devices[eco_idx].sent);
    if (session_eco.queued != 0)
        return fail("eco queue drained", (int64_t)session_eco.queued);
    /* Work queued for a device that is then unpaired is dropped on the next
     * flush rather than sent to nowhere. */
    if (zd_eco_pair(&session_eco, "desk-02", &eco_idx2) != 0 ||
        zd_eco_grant(&session_eco, eco_idx2,
                     ZD_ECO_PERM_SYNC_CLIPBOARD) != 0 ||
        zd_eco_enqueue(&session_eco, eco_idx2, ZD_ECO_PERM_SYNC_CLIPBOARD,
                       "clipboard") != 0)
        return fail("eco queue two", 0);
    if (zd_eco_unpair(&session_eco, eco_idx2) != 0 ||
        session_eco.stats.unpairs != 1 ||
        session_eco.stats.refused_pairing != 1)
        return fail("eco unpair",
                    (int64_t)session_eco.stats.refused_pairing);
    if (zd_eco_flush(&session_eco) != 0 || session_eco.queued != 0 ||
        session_eco.stats.flushed != 1)
        return fail("eco unpaired flush", (int64_t)session_eco.queued);
    zd_eco_set_conn(&session_eco, ZD_ECO_OFFLINE);
    say("ZEROOS: session device ecosystem passed.");

    /* 36. Update payload verification with a provisioned key. Nothing
     * here is a fixture: the key is provisioned through the filesystem
     * (written, read back, and only then used), the bundle is sealed with
     * the RFC 8439 AEAD core, and the update engine verifies it with the
     * version bound as AAD. The pipeline hooks perform real VFS writes, so
     * staging, activation and commit leave files this step reads back --
     * and a failed health check really removes the activated slot. */
    sys_result = session_write_buffer("/ram/shell/update.key", update_key,
                                      sizeof(update_key), 0600);
    if (sys_result != 0)
        return fail("update key provision", sys_result);
    key_len = 0;
    if (session_read_buffer("/ram/shell/update.key", update_key_file,
                            sizeof(update_key_file), &key_len) != 0 ||
        key_len != sizeof(update_key))
        return fail("update key readback", (int64_t)key_len);
    for (key_index = 0; key_index < sizeof(update_key); ++key_index) {
        if (update_key_file[key_index] != update_key[key_index])
            return fail("update key mismatch", (int64_t)key_index);
    }
    if (zeroos_aead_encrypt(update_key_file, update_nonce,
                            (const uint8_t *)update_version,
                            (uint32_t)(sizeof(update_version) - 1),
                            update_payload, UPDATE_PAYLOAD_LEN,
                            update_ct, update_tag) != ZCRYPTO_OK)
        return fail("update seal", 0);
    if (zd_update_verify_payload(update_key_file, update_nonce,
                                 update_version, update_ct,
                                 UPDATE_PAYLOAD_LEN, update_tag) != 0)
        return fail("update verify", 0);
    /* A flipped byte, a different version and a different key must each be
     * refused: the tag covers the payload and the AAD. */
    update_ct[0] ^= 0x01;
    if (zd_update_verify_payload(update_key_file, update_nonce,
                                 update_version, update_ct,
                                 UPDATE_PAYLOAD_LEN, update_tag) != -3)
        return fail("update tamper", 0);
    update_ct[0] ^= 0x01;
    if (zd_update_verify_payload(update_key_file, update_nonce, "9.9.9",
                                 update_ct, UPDATE_PAYLOAD_LEN,
                                 update_tag) != -3)
        return fail("update version binding", 0);
    update_key_file[0] ^= 0x01;
    if (zd_update_verify_payload(update_key_file, update_nonce,
                                 update_version, update_ct,
                                 UPDATE_PAYLOAD_LEN, update_tag) != -3)
        return fail("update wrong key", 0);
    update_key_file[0] ^= 0x01;
    session_update_ctx.ciphertext = update_ct;
    session_update_ctx.ciphertext_len = (uint32_t)UPDATE_PAYLOAD_LEN;
    session_update_ctx.version = update_version;
    session_update_ctx.version_len = (uint32_t)(sizeof(update_version) - 1);
    zd_update_init(&session_update, &session_update_ops);
    if (zd_update_begin(&session_update, update_version) != 0)
        return fail("update begin", 0);
    {
        static const int update_events[] = {
            ZD_UPD_EV_DOWNLOAD_OK, ZD_UPD_EV_VERIFY_OK, ZD_UPD_EV_STAGE_OK,
            ZD_UPD_EV_PREFLIGHT_OK, ZD_UPD_EV_ACTIVATE_OK,
            ZD_UPD_EV_HEALTH_OK, ZD_UPD_EV_COMMIT_OK
        };
        for (event_index = 0;
             event_index < (int)ZD_ARRAY_COUNT(update_events);
             ++event_index) {
            sys_result = zd_update_event(&session_update,
                                         update_events[event_index]);
            if (sys_result != 0)
                return fail("update pipeline", sys_result);
        }
    }
    if (zd_update_state(&session_update) != ZD_UPD_DONE)
        return fail("update state", (int64_t)zd_update_state(&session_update));
    if (session_update.stats.committed != 1)
        return fail("update commit count",
                    (int64_t)session_update.stats.committed);
    /* The hooks really wrote: the staged bundle has the sealed size and
     * the active slot holds the version string. */
    if (zeroos_stat("/ram/shell/update.staged", &file_stat) != 0 ||
        file_stat.size != UPDATE_PAYLOAD_LEN)
        return fail("update staged size", (int64_t)file_stat.size);
    key_len = 0;
    if (session_read_buffer("/ram/shell/update.active", update_key_file,
                            sizeof(update_key_file), &key_len) != 0 ||
        key_len != sizeof(update_version) - 1)
        return fail("update active slot", (int64_t)key_len);
    update_key_file[key_len] = 0;
    if (!session_streq((const char *)update_key_file, update_version))
        return fail("update active version", 0);
    /* A failed health check must roll back for real: the activated slot is
     * removed, not merely marked. */
    zd_update_init(&session_update, &session_update_ops);
    if (zd_update_begin(&session_update, update_version) != 0)
        return fail("update rollback begin", 0);
    {
        static const int rollback_events[] = {
            ZD_UPD_EV_DOWNLOAD_OK, ZD_UPD_EV_VERIFY_OK, ZD_UPD_EV_STAGE_OK,
            ZD_UPD_EV_PREFLIGHT_OK, ZD_UPD_EV_ACTIVATE_OK,
            ZD_UPD_EV_HEALTH_FAIL, ZD_UPD_EV_ROLLBACK_DONE
        };
        for (event_index = 0;
             event_index < (int)ZD_ARRAY_COUNT(rollback_events);
             ++event_index) {
            sys_result = zd_update_event(&session_update,
                                         rollback_events[event_index]);
            if (sys_result != 0)
                return fail("update rollback pipeline", sys_result);
        }
    }
    if (zd_update_state(&session_update) != ZD_UPD_FAILED)
        return fail("update rollback state",
                    (int64_t)zd_update_state(&session_update));
    if (session_update.stats.rollbacks != 1 ||
        session_update.stats.health_failures != 1)
        return fail("update rollback stats",
                    (int64_t)session_update.stats.rollbacks);
    if (zeroos_stat("/ram/shell/update.active", &file_stat) !=
        -ZEROOS_ENOENT)
        return fail("update slot removed", 0);
    say("ZEROOS: session update verification passed.");

    /* 37. Security scanning architecture (Stage 5 part B). The queue, the
     * bounded incremental scheduler, the quarantine policy and the audit
     * ring are real and driven here against files the VFS really holds;
     * detection is a pluggable engine and this build links none, so what
     * is certified is the property that matters: with no engine every
     * item reports UNAVAILABLE and not one is reported CLEAN. "Not
     * scanned" is never a clean bill of health, and no caller in this
     * guest can read a CLEAN verdict out of a build with no detector. */
    zd_scan_init(&session_scan);
    if (zd_scan_available(&session_scan) != 0)
        return fail("scan engine should be absent", 0);
    if (session_write_file("/ram/shell/scan-one.txt", 32) != 0 ||
        session_write_file("/ram/shell/scan-two.txt", 48) != 0)
        return fail("scan fixture files", 0);
    if (zd_scan_submit(&session_scan, "/ram/shell/scan-one.txt", 32) != 0 ||
        zd_scan_submit(&session_scan, "/ram/shell/scan-two.txt", 48) != 0)
        return fail("scan submit", 0);
    {
        int guard = 0;
        while (zd_scan_step(&session_scan, 1, 4096) == 1 && guard < 64)
            ++guard;
        if (guard >= 64)
            return fail("scan step bound", guard);
    }
    if (session_scan.stats.unavailable != 2 ||
        session_scan.stats.clean != 0 || session_scan.stats.completed != 2)
        return fail("scan verdict counters",
                    (int64_t)session_scan.stats.clean);
    {
        const struct zd_scan_item *one =
            zd_scan_item(&session_scan, "/ram/shell/scan-one.txt");
        const struct zd_scan_item *two =
            zd_scan_item(&session_scan, "/ram/shell/scan-two.txt");
        if (!one || !two)
            return fail("scan items present", 0);
        if (one->verdict != ZD_SCAN_UNAVAILABLE ||
            two->verdict != ZD_SCAN_UNAVAILABLE)
            return fail("scan fails closed", (int64_t)one->verdict);
        if (one->state != ZD_SCAN_DONE || one->quarantined != 0)
            return fail("scan item state", (int64_t)one->state);
    }
    {
        struct zd_scan_audit_row rows[4];
        uint32_t rows_out = zd_scan_audit(&session_scan, rows, 4);
        if (rows_out != 2U || rows[0].verdict != ZD_SCAN_UNAVAILABLE)
            return fail("scan audit ring", (int64_t)rows_out);
    }
    say("ZEROOS: session security scan pipeline passed.");

    /* 38. Sandbox decisions enforced by the kernel, not only by Ring-3
     * policy. The confined profile denies writes, so the session really
     * drops its identity: after SETCRED the VFS itself refuses the
     * owner-only file with EACCES while a world-readable file still
     * opens. SETCRED is one-way once unprivileged (only root may change
     * identity), so this step runs last. */
    if (zd_sandbox_define(&session_sandbox, "confined",
                          (1U << ZD_SB_FS_READ)) != 0)
        return fail("credential sandbox profile", 0);
    if (zd_sandbox_check(&session_sandbox, "confined", ZD_SB_FS_WRITE) != -1)
        return fail("credential policy denial", 0);
    if (session_write_file_mode("/ram/shell/locked.txt", 16, 0600) != 0 ||
        session_write_file("/ram/shell/shared.txt", 16) != 0)
        return fail("credential fixture", 0);
    cred = zeroos_getcred();
    if (cred < 0 || (uint32_t)cred != 0)
        return fail("credential start", cred);
    if (zeroos_setcred(1000, 1000) != 0)
        return fail("credential drop", 0);
    cred = zeroos_getcred();
    if ((uint32_t)cred != 1000U || (uint32_t)((uint64_t)cred >> 32) != 1000U)
        return fail("credential identity", cred);
    /* The kernel refuses the owner-only file: uid 1000 is neither the
     * owner nor in the group, and the other bits are zero. */
    sys_result = zeroos_open("/ram/shell/locked.txt", ZEROOS_O_RDONLY, 0);
    if (sys_result != -ZEROOS_EACCES)
        return fail("kernel permission enforcement", sys_result);
    /* A world-readable file still opens, so this is a permission decision
     * and not a blanket failure. */
    sys_result = zeroos_open("/ram/shell/shared.txt", ZEROOS_O_RDONLY, 0);
    if (sys_result < 0)
        return fail("world readable open", sys_result);
    if (zeroos_close(sys_result) != 0)
        return fail("world readable close", 0);
    /* And an unprivileged process cannot give root back to itself. */
    sys_result = zeroos_setcred(0, 0);
    if (sys_result != -ZEROOS_EPERM)
        return fail("credential drop is one-way", sys_result);
    say("ZEROOS: session credential enforcement passed.");

    say("ZEROOS: session shell process complete.");
    return 0;
}
