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

void zd_test_stress_suite(void) {
    printf("  suite: stress/soak\n");
    ZD_RUN(stress_browser_cycles);
    ZD_RUN(stress_vault_cycles);
    ZD_RUN(stress_firewall_throughput);
    ZD_RUN(stress_snapshot_turnover);
    ZD_RUN(stress_fps_and_metrics);
}
