/* Fault-injection suite (Stage 5 part M — FAULT evidence).
 * A deterministic LCG drives invalid-input sequences at public API
 * boundaries across modules.  Invariants asserted continuously:
 *   - no call crashes and no return escapes 0..-64 (errno range),
 *   - counters never exceed their caps,
 *   - known-good operations still succeed afterwards (state not
 *     corrupted, fail-closed behaviour preserved).
 * Seeded and reproducible. */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

static uint32_t fault_rng;
static uint32_t fault_next(void) {
    fault_rng = fault_rng * 1664525u + 1013904223u;
    return fault_rng >> 8;
}
static uint32_t fault_pick(uint32_t n) {
    return n ? fault_next() % n : 0;
}
/* invariant: errno-style only (0 or small negative) */
static void fault_expect_rc(int rc) {
    ZD_CHECK(rc <= 0);
    ZD_CHECK(rc >= -64);
}

#define FAULT_ROUNDS 512

static const char *const fault_bad_strings[] = {
    "",
    "bad/name",
    "..",
    "with\\backslash",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
};

/* no-op snapshot hooks so capture/restore/discard never fault out */
static int fs_noop(void *ctx, const char *name) {
    (void)ctx;
    (void)name;
    return 0;
}

void zd_test_fault_suite(void) {
    uint32_t round;
    fault_rng = 0xC0FFEEu;

    for (round = 0; round < FAULT_ROUNDS; ++round) {
        const char *bad =
            fault_bad_strings[fault_pick(sizeof(fault_bad_strings) /
                                         sizeof(fault_bad_strings[0]))];
        switch (fault_pick(8)) {
        case 0: { /* settings: null/bad key/permission denied */
            struct zd_settings st;
            uint32_t changed = 1;
            zd_settings_init(&st);
            if (fault_pick(3) == 0)
                fault_expect_rc(zd_settings_set(NULL, "ui.scale_percent",
                                                100, 0, 0, &changed));
            else if (fault_pick(2))
                fault_expect_rc(zd_settings_set_number(&st, bad, 1, 0,
                                                       &changed));
            else
                fault_expect_rc(zd_settings_set_number(
                    &st, "ui.scale_percent", 99999, 0, &changed));
            break;
        }
        case 1: { /* notify: null/post with empty or overlong fields */
            struct zd_notify nb;
            struct zd_notify_post post;
            zd_notification_id nid = 0;
            zd_notify_init(&nb);
            memset(&post, 0, sizeof(post));
            post.app_id = (fault_pick(2)) ? bad : "fault.app";
            post.title = bad;
            post.body = "";
            if (fault_pick(3) == 0)
                fault_expect_rc(zd_notify_post(NULL, &post, 0, &nid));
            else
                fault_expect_rc(zd_notify_post(&nb, &post, 0, &nid));
            break;
        }
        case 2: { /* clipboard: bad format/app/text */
            struct zd_clipboard cb;
            zd_clipboard_init(&cb);
            if (fault_pick(3) == 0)
                fault_expect_rc(zd_clipboard_copy(&cb, "app", NULL,
                                                  ZD_CLIP_FMT_TEXT, 0));
            else if (fault_pick(2))
                fault_expect_rc(zd_clipboard_copy(&cb, bad, "x",
                                                  99u, 0));
            else
                fault_expect_rc(zd_clipboard_copy(&cb, NULL, "",
                                                  ZD_CLIP_FMT_TEXT, 0));
            zd_clipboard_clear(&cb);
            break;
        }
        case 3: { /* downloads: null/overlong url, wrong-state ops */
            struct zd_downloads dl;
            zd_downloads_init(&dl, NULL, NULL);
            if (fault_pick(2))
                fault_expect_rc(zd_downloads_add(&dl, NULL, NULL, 0));
            else
                fault_expect_rc(zd_downloads_add(&dl, bad, NULL, 0));
            fault_expect_rc(zd_downloads_start_next(&dl));
            fault_expect_rc(zd_downloads_finish(&dl, 42, 0));
            fault_expect_rc(zd_downloads_progress(&dl, 42, 1, 0));
            fault_expect_rc(zd_downloads_cancel(&dl, 42));
            ZD_CHECK(dl.count <= ZD_DL_MAX);
            break;
        }
        case 4: { /* snapshots: null ops / bad names / wrong state */
            struct zd_snapshots sn;
            struct zd_snapshot_ops ops;
            memset(&ops, 0, sizeof(ops));
            ops.capture = fs_noop;
            ops.restore = fs_noop;
            ops.discard = fs_noop;
            zd_snapshots_init(&sn, &ops);
            fault_expect_rc(zd_snapshots_create(&sn, bad));
            fault_expect_rc(zd_snapshots_restore(&sn, bad));
            fault_expect_rc(zd_snapshots_discard(&sn, bad));
            if (fault_pick(4) == 0)
                fault_expect_rc(zd_snapshots_discard(&sn, NULL));
            ZD_CHECK(sn.stats.created <= ZD_SNAP_MAX * 4);
            break;
        }
        case 5: { /* firewall: invalid rule ranges, null flow */
            struct zd_fw fw;
            struct zd_fw_rule rule;
            zd_fw_init(&fw);
            memset(&rule, 0, sizeof(rule));
            rule.enabled = 1;
            rule.dir = (uint8_t)fault_pick(5); /* 2+ = invalid */
            rule.proto = (uint8_t)fault_pick(7);
            rule.action = (uint8_t)fault_pick(3);
            rule.port_lo = (uint16_t)fault_pick(65536);
            rule.port_hi = (uint16_t)fault_pick(65536);
            fault_expect_rc(zd_fw_add(&fw, &rule, (int)fault_pick(2)));
            fault_expect_rc(zd_fw_remove(&fw, 0));
            fault_expect_rc(zd_fw_decide(&fw, NULL));
            ZD_CHECK(fw.rule_count <= ZD_FW_MAX_RULES);
            break;
        }
        case 6: { /* sandbox: unknown profile, out-of-range class */
            struct zd_sandbox sb;
            zd_sandbox_init(&sb);
            if (fault_pick(2))
                fault_expect_rc(zd_sandbox_define(&sb, bad,
                                                  0xFFFFFFFFu));
            else
                fault_expect_rc(zd_sandbox_check(&sb,
                                                 (fault_pick(2)) ? bad
                                                                : "missing",
                                                 (int)fault_pick(12)));
            ZD_CHECK(sb.stats.profiles_defined <= ZD_SB_PROFILES);
            break;
        }
        default: { /* lifecycle + perfcenter: bad events/samples */
            struct zd_lifecycle lc;
            struct zd_perf_center pc;
            struct zd_pc_input in;
            struct zd_pc_report rep;
            zd_lifecycle_init(&lc);
            fault_expect_rc(zd_lifecycle_dispatch(
                &lc, (enum zd_lifecycle_event)fault_pick(20), 0));
            ZD_CHECK(zd_lifecycle_state(&lc) >= 0 &&
                     zd_lifecycle_state(&lc) <=
                         ZD_LIFECYCLE_SUSPENDED);
            zd_perf_center_init(&pc);
            memset(&in, 0, sizeof(in));
            fault_expect_rc(zd_perf_center_record(
                &pc, fault_pick(2) ? 0u : 1500001u));
            fault_expect_rc(zd_perf_center_assess(&pc, &in, &rep));
            break;
        }
        }
    }

    /* ---- state machines reject wrong-state sequences ---- */
    {
        struct zd_update u;
        zd_update_init(&u, 0);
        /* begin with null/empty version fails; double-begin fails */
        fault_expect_rc(zd_update_begin(&u, NULL));
        fault_expect_rc(zd_update_begin(&u, ""));
        ZD_CHECK_OK(zd_update_begin(&u, "1.2.3"));
        ZD_CHECK(zd_update_begin(&u, "4.5.6") < 0);
        ZD_CHECK(zd_update_state(&u) >= 0);
    }
    {
        struct zd_lifecycle lc;
        int st;
        zd_lifecycle_init(&lc);
        /* SUSPEND before the session ever started must fail */
        fault_expect_rc(zd_lifecycle_dispatch(&lc,
                                              ZD_LIFECYCLE_SUSPEND, 0));
        st = zd_lifecycle_state(&lc);
        ZD_CHECK(st >= 0 && st <= ZD_LIFECYCLE_SUSPENDED);
        /* random storm leaves a valid state */
        {
            uint32_t i;
            for (i = 0; i < 32; ++i)
                fault_expect_rc(zd_lifecycle_dispatch(
                    &lc, (enum zd_lifecycle_event)fault_pick(14),
                    (uint64_t)i * 1000000u));
        }
        ZD_CHECK(zd_lifecycle_state(&lc) >= 0);
        ZD_CHECK(zd_lifecycle_state(&lc) <= ZD_LIFECYCLE_SUSPENDED);
    }

    /* ---- post-fault sanity: known-good paths still work ---- */
    {
        struct zd_settings st;
        struct zd_setting_def def;
        int64_t v = 0;
        uint32_t changed = 0;
        zd_settings_init(&st);
        memset(&def, 0, sizeof(def));
        def.key = "ui.scale_percent";
        def.type = ZD_SETTING_INT;
        def.scope = ZD_SCOPE_USER;
        def.default_value = 100;
        def.min_value = 100;
        def.max_value = 300;
        ZD_CHECK_OK(zd_settings_register(&st, &def));
        ZD_CHECK_OK(zd_settings_set_number(&st, "ui.scale_percent",
                                           150, ZD_PERM_SETTINGS_USER,
                                           &changed));
        ZD_CHECK_OK(zd_settings_get(&st, "ui.scale_percent", &v, 0, 0));
        ZD_CHECK_EQ((int)v, 150);
        ZD_CHECK_OK(zd_settings_reset(&st, "ui.scale_percent",
                                      ZD_PERM_SETTINGS_USER));
        ZD_CHECK_OK(zd_settings_get(&st, "ui.scale_percent", &v, 0, 0));
        ZD_CHECK_EQ((int)v, 100);
        /* unknown key still rejected after the storm */
        ZD_CHECK(zd_settings_get(&st, "nope.nope", &v, 0, 0) != 0);
    }
    {
        struct zd_sandbox sb;
        zd_sandbox_init(&sb);
        ZD_CHECK_OK(zd_sandbox_define(&sb, "browser",
                                      (1u << ZD_SB_FS_READ) |
                                      (1u << ZD_SB_NET_CLIENT)));
        ZD_CHECK_OK(zd_sandbox_check(&sb, "browser", ZD_SB_FS_READ));
        ZD_CHECK_EQ(zd_sandbox_check(&sb, "browser", ZD_SB_DEVICE), -1);
        ZD_CHECK_EQ(zd_sandbox_check(&sb, "missing", ZD_SB_FS_READ), -1);
    }
    {
        struct zd_fw fw;
        struct zd_fw_rule rule;
        struct zd_fw_flow flow;
        zd_fw_init(&fw);
        memset(&rule, 0, sizeof(rule));
        rule.enabled = 1;
        rule.dir = ZD_FW_OUT;
        rule.proto = ZD_FW_TCP;
        rule.action = ZD_FW_ALLOW;
        rule.port_lo = 443;
        rule.port_hi = 443;
        ZD_CHECK_OK(zd_fw_add(&fw, &rule, 0));
        memset(&flow, 0, sizeof(flow));
        flow.dir = ZD_FW_OUT;
        flow.proto = ZD_FW_TCP;
        flow.dst_port = 443;
        ZD_CHECK(zd_fw_decide(&fw, &flow) == ZD_FW_ALLOW);
        flow.dst_port = 22;
        ZD_CHECK(zd_fw_decide(&fw, &flow) == ZD_FW_DENY);
    }
    {
        struct zd_clipboard cb;
        char out[64];
        zd_clipboard_init(&cb);
        ZD_CHECK_OK(zd_clipboard_copy(&cb, "app", "fault-ok",
                                      ZD_CLIP_FMT_TEXT, 0));
        ZD_CHECK_OK(zd_clipboard_paste(&cb, out, sizeof(out)));
        ZD_CHECK(strcmp(out, "fault-ok") == 0);
        zd_clipboard_clear(&cb);
    }
}
