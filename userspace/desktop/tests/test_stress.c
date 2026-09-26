/* Stress/soak host suite (part M): sustained cycles across cores at
 * and beyond their capacity limits, with exact end-state accounting.
 * No allocation drift is possible here (static storage), so the
 * assertions prove behavioural stability, not just survival. */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>

static void stress_browser_cycles(void) {
    struct zd_browser b;
    uint32_t ids[16];
    int round;
    zd_browser_init(&b, 100, 300, 900);
    for (round = 0; round < 40; ++round) {
        int i;
        /* fill to the 16-tab cap, then prove the cap still rejects */
        for (i = 0; i < 16; ++i)
            ZD_CHECK_OK(zd_browser_open(&b, 1024, &ids[i]));
        ZD_CHECK(zd_browser_open(&b, 1024, &ids[0]) == -ZD_ENOSPC);
        /* churn: freeze some, discard some, close all */
        for (i = 0; i < 16; i += 2)
            zd_browser_event(&b, ids[i], ZD_TAB_EV_TICK, 400 + round);
        for (i = 1; i < 16; i += 4)
            zd_browser_event(&b, ids[i], ZD_TAB_EV_DISCARD_NOW,
                             500 + round);
        for (i = 0; i < 16; ++i)
            ZD_CHECK_OK(zd_browser_close(&b, ids[i]));
    }
    ZD_CHECK(b.stats.discards > 0);
    /* after 40 rounds the cap rejects exactly like round one */
    {
        int i;
        for (i = 0; i < 16; ++i)
            ZD_CHECK_OK(zd_browser_open(&b, 1024, &ids[i]));
        ZD_CHECK(zd_browser_open(&b, 1024, &ids[0]) == -ZD_ENOSPC);
        for (i = 0; i < 16; ++i)
            ZD_CHECK_OK(zd_browser_close(&b, ids[i]));
    }
    ZD_CHECK_EQ(b.stats.opened, 41u * 16);
    ZD_CHECK_EQ(b.stats.closed, 41u * 16);
}

static void stress_vault_cycles(void) {
    struct zd_vault v;
    uint8_t key[ZD_VAULT_KEY_LEN];
    uint8_t out[ZD_VAULT_SECRET_MAX];
    uint8_t blob[32];
    uint32_t out_len = 0;
    int i, k;
    for (i = 0; i < ZD_VAULT_KEY_LEN; ++i)
        key[i] = (uint8_t)(i ^ 0x5A);
    memset(blob, 0x77, sizeof(blob));
    zd_vault_init(&v);
    zd_vault_unlock(&v, key);
    for (k = 0; k < 100; ++k) {
        char nm[8];
        int j = 0;
        nm[j++] = 's';
        nm[j++] = (char)('0' + (k / 10) % 10);
        nm[j++] = (char)('0' + k % 10);
        nm[j] = 0;
        ZD_CHECK_OK(zd_vault_put(&v, nm, blob, sizeof(blob)));
        ZD_CHECK_OK(zd_vault_get(&v, nm, out, sizeof(out), &out_len));
        ZD_CHECK_EQ(out_len, sizeof(blob));
        ZD_CHECK(memcmp(out, blob, sizeof(blob)) == 0);
        ZD_CHECK_OK(zd_vault_forget(&v, nm));
        ZD_CHECK_EQ(zd_vault_count(&v), 0);
    }
    ZD_CHECK_EQ(v.stats.puts, 100u);
    ZD_CHECK_EQ(v.stats.gets, 100u);
    /* lock/unlock churn keeps working */
    for (k = 0; k < 20; ++k) {
        zd_vault_lock(&v);
        zd_vault_unlock(&v, key);
    }
    ZD_CHECK_OK(zd_vault_put(&v, "keep", blob, 4));
    ZD_CHECK_OK(zd_vault_get(&v, "keep", out, sizeof(out), &out_len));
    ZD_CHECK_EQ(v.stats.wipes, 20u);
}

static void stress_firewall_throughput(void) {
    struct zd_fw fw;
    struct zd_fw_rule r;
    struct zd_fw_flow f;
    uint32_t i;
    memset(&r, 0, sizeof(r));
    r.dir = ZD_FW_OUT;
    r.proto = ZD_FW_UDP;
    r.action = ZD_FW_ALLOW;
    r.port_lo = 1000;
    r.port_hi = 2000;
    zd_fw_init(&fw);
    ZD_CHECK_OK(zd_fw_add(&fw, &r, 1));
    memset(&f, 0, sizeof(f));
    f.dir = ZD_FW_OUT;
    f.proto = ZD_FW_UDP;
    for (i = 0; i < 20000; ++i) {
        f.dst_port = (uint16_t)(1000 + (i % 1000));
        if (f.dst_port <= 2000)
            ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_ALLOW);
        else
            ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    }
    ZD_CHECK_EQ(fw.stats.flows, 20000u);
    ZD_CHECK_EQ(fw.stats.allowed + fw.stats.denied, 20000u);
}

static void stress_snapshot_turnover(void) {
    struct zd_snapshots s;
    int k;
    zd_snapshots_init(&s, 0); /* hookless: decisions still enforced */
    for (k = 0; k < 24; ++k) {
        char nm[8];
        int j = 0;
        nm[j++] = 'p';
        nm[j++] = (char)('0' + k % 10);
        nm[j] = 0;
        if (zd_snapshots_create(&s, nm) == 0)
            ZD_CHECK_OK(zd_snapshots_create_finish(&s, 0));
        if (k >= 8)
            ZD_CHECK(zd_snapshots_restore(&s, 0) == 0);
    }
    ZD_CHECK(zd_snapshots_ready_count(&s) == (uint32_t)ZD_SNAP_MAX);
    ZD_CHECK(s.stats.created >= 16u);
    ZD_CHECK(s.stats.discarded >= 8u);
}

static void stress_fps_and_metrics(void) {
    struct zd_fps f;
    struct zd_metrics m;
    uint32_t i;
    zd_fps_init(&f, 1000, ZD_PERF_BALANCED);
    zd_metrics_init(&m);
    for (i = 1; i <= 10000; ++i) {
        ZD_CHECK_OK(zd_fps_record(&f, (uint64_t)i * 16, 16));
        if ((i % 8) == 0)
            zd_metrics_record_interval(&m, 16);
    }
    ZD_CHECK_EQ(f.frames_total, 10000u);
    ZD_CHECK_EQ(f.count, (uint32_t)ZD_FPS_RING);
    ZD_CHECK_EQ(zd_fps_avg_frame_ms(&f), 16u);
    ZD_CHECK_EQ(m.interval_count, 1250u);
    ZD_CHECK_EQ(zd_metrics_percentile(&m, 50), 16u);
}


/* New shell/app cores (batch 10): volume churn with exact totals */
static int st_fm_src(void *ctx, const char *path,
                     struct zd_fm_entry *out, uint32_t cap,
                     uint32_t *out_n) {
    (void)ctx;
    *out_n = 0;
    if (strcmp(path, "/sr") == 0 || strcmp(path, "/sr/sub") == 0) {
        uint32_t i;
        if (cap < 3)
            return -28;
        for (i = 0; i < 3; ++i) {
            memset(&out[i], 0, sizeof(out[i]));
            out[i].name[0] = 'e';
            out[i].name[1] = (char)('0' + (char)i);
            out[i].size = i * 10;
            out[i].mtime = (int64_t)i;
        }
        *out_n = 3;
        return 0;
    }
    return -2;
}

static void stress_shell_cores(void) {
    /* terminal: 200 rounds of fill+scroll on a 4x16 grid */
    {
        struct zd_term t;
        uint32_t r;
        zd_term_init(&t, 4, 16);
        for (r = 0; r < 200; ++r)
            ZD_CHECK_OK(zd_term_write(
                &t, (const uint8_t *)"0123456789abcdef\r\n", 18));
        ZD_CHECK_EQ(t.stats.writes, 200u); /* one write per round */
        ZD_CHECK_EQ(t.stats.bytes, 200u * 18u);
        ZD_CHECK_EQ(t.stats.scrolls, 197u); /* first 3 fill rows */
        ZD_CHECK_EQ(t.sb_count, (uint32_t)ZD_TERM_SCROLLBACK);
    }
    /* file manager: 100 alternating opens + one back/forward pair */
    {
        struct zd_fm fm;
        uint32_t r;
        zd_fm_init(&fm, st_fm_src, 0);
        for (r = 0; r < 100; ++r)
            ZD_CHECK_OK(zd_fm_open(&fm, (r & 1) ? "/sr/sub" : "/sr"));
        ZD_CHECK_EQ(fm.stats.navigations, 100u);
        ZD_CHECK_EQ(fm.stats.refreshes, 100u);
        /* history holds 16; 84 oldest entries were dropped */
        ZD_CHECK_EQ(fm.hist_count, (uint32_t)ZD_FM_HISTORY);
        ZD_CHECK_EQ(fm.stats.history_dropped, 84u);
        ZD_CHECK_OK(zd_fm_back(&fm));
        ZD_CHECK_OK(zd_fm_forward(&fm));
        ZD_CHECK_EQ(fm.stats.backs, 1u);
        ZD_CHECK_EQ(fm.stats.forwards, 1u);
        ZD_CHECK_EQ(fm.stats.source_errors, 0u);
    }
    /* formula: 500 parse/eval cycles with a moving variable */
    {
        struct zd_formula f;
        uint32_t r;
        zd_formula_init(&f);
        ZD_CHECK_OK(zd_formula_parse(&f, "1+2*3-x"));
        for (r = 0; r < 500; ++r) {
            double v = 0;
            ZD_CHECK_OK(zd_formula_set_var(&f, "x", (double)r));
            ZD_CHECK_OK(zd_formula_eval(&f, &v));
            ZD_CHECK(v == 7.0 - (double)r);
        }
        ZD_CHECK_EQ(f.stats.parse_errors, 0u);
        ZD_CHECK_EQ(f.stats.evals, 500u);
        ZD_CHECK_EQ(f.stats.var_sets, 500u);
        ZD_CHECK_EQ(f.stats.parses, 1u);
    }
    /* overview: 100 set+remove quadruples over 16-window batches */
    {
        struct zd_overview o;
        struct zd_rect area;
        uint32_t ids[16];
        uint32_t r, i;
        memset(&o, 0, sizeof(o));
        area.x = 0;
        area.y = 0;
        area.w = 400;
        area.h = 400;
        ZD_CHECK_OK(zd_overview_open(&o, area, 4));
        for (r = 0; r < 100; ++r) {
            for (i = 0; i < 16; ++i)
                ids[i] = r * 16 + i + 1;
            ZD_CHECK_OK(zd_overview_set_windows(&o, ids, 16));
            for (i = 0; i < 4; ++i)
                ZD_CHECK_OK(zd_overview_remove(&o, ids[i]));
        }
        ZD_CHECK_EQ(o.stats.relayouts, 500u); /* 100 x (set+4 rm) */
        ZD_CHECK_EQ(o.stats.closes, 400u);
        ZD_CHECK_EQ(o.count, 12u);
        ZD_CHECK_EQ(o.cols, 4u); /* ceil(sqrt(12)) */
        ZD_CHECK_EQ(o.stats.rejected, 0u);
    }
    /* ecosystem: 100 pair/grant/queue/flush/unpair generations */
    {
        struct zd_eco e;
        uint32_t r, idx = 0;
        zd_eco_init(&e);
        zd_eco_set_conn(&e, ZD_ECO_ONLINE);
        for (r = 0; r < 100; ++r) {
            char nm[8];
            nm[0] = 'd';
            nm[1] = (char)('0' + (char)(r % 10));
            nm[2] = (char)('a' + (char)((r / 10) % 26));
            nm[3] = 0;
            ZD_CHECK_OK(zd_eco_pair(&e, nm, &idx));
            ZD_CHECK_OK(zd_eco_grant(&e, idx,
                                     ZD_ECO_PERM_SYNC_FILES));
            ZD_CHECK_OK(zd_eco_enqueue(&e, idx,
                                       ZD_ECO_PERM_SYNC_FILES, "p"));
            ZD_CHECK_EQ(zd_eco_flush(&e), 1u);
            ZD_CHECK_OK(zd_eco_unpair(&e, idx));
        }
        ZD_CHECK_EQ(e.stats.paired, 100u);
        ZD_CHECK_EQ(e.stats.granted, 100u);
        ZD_CHECK_EQ(e.stats.queued, 100u);
        ZD_CHECK_EQ(e.stats.flushed, 100u);
        ZD_CHECK_EQ(e.stats.unpairs, 100u);
        ZD_CHECK_EQ(e.device_count, 0u);
        ZD_CHECK_EQ(e.queued, 0u);
        ZD_CHECK_EQ(e.stats.refused_perm, 0u);
    }
}

void zd_test_stress_suite(void) {
    printf("  suite: stress/soak\n");
    ZD_RUN(stress_browser_cycles);
    ZD_RUN(stress_vault_cycles);
    ZD_RUN(stress_firewall_throughput);
    ZD_RUN(stress_snapshot_turnover);
    ZD_RUN(stress_fps_and_metrics);
    ZD_RUN(stress_shell_cores);
}
