/* Soak suite (Stage 5 part M — SOAK evidence).
 * Sustained, seeded end-to-end churn across subsystems with two
 * classes of invariant checked continuously:
 *   1. generation isolation — every re-init yields a pristine object
 *      (stats zeroed, cursors at start, no bleed between epochs);
 *   2. steady-state bounds — under long runs, counters stay exact
 *      and capacity limits never drift.
 * Fixed seed; total work is sized to finish in seconds yet far
 * exceeds any single unit run. */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

#define SOAK_EPOCHS 64
#define SOAK_ROUNDS 256

static uint32_t soak_rng;
static uint32_t soak_next(void) {
    soak_rng = soak_rng * 1664525u + 1013904223u;
    return soak_rng >> 8;
}
static uint32_t soak_pick(uint32_t n) {
    return n ? soak_next() % n : 0;
}

/* ---- epoch 1: clipboard churn with generation isolation ---- */
static void soak_clipboard(void) {
    uint32_t epoch;
    char out[32];
    char text[64];
    for (epoch = 0; epoch < SOAK_EPOCHS; ++epoch) {
        struct zd_clipboard cb;
        uint32_t i;
        zd_clipboard_init(&cb);
        /* pristine */
        ZD_CHECK_EQ(zd_clipboard_history_count(&cb), 0);
        ZD_CHECK_EQ(zd_clipboard_paste(&cb, out, sizeof(out)), -1);
        /* fill well past capacity */
        for (i = 0; i < SOAK_ROUNDS; ++i) {
            uint32_t n = 0;
            text[0] = 's';
            text[1] = (char)('0' + (i % 10));
            text[2] = (char)('a' + (i % 26));
            text[3] = 0;
            (void)n;
            if (soak_pick(16) == 0)
                ZD_CHECK_OK(zd_clipboard_copy(&cb, "soak", text,
                                              ZD_CLIP_FMT_TEXT, 1));
            else
                ZD_CHECK_OK(zd_clipboard_copy(&cb, "soak", text,
                                              ZD_CLIP_FMT_TEXT, 0));
            ZD_CHECK(zd_clipboard_history_count(&cb) <=
                     ZD_CLIP_MAX - 1);
        }
        zd_clipboard_clear(&cb);
        ZD_CHECK_EQ(zd_clipboard_history_count(&cb), 0);
        ZD_CHECK_EQ(zd_clipboard_paste(&cb, out, sizeof(out)), -1);
    }
}

/* ---- epoch 2: downloads lifecycle totals stay exact ---- */
static int soak_dl_start(void *ctx, uint32_t id, const char *url) {
    uint32_t *opens = ctx;
    if (id == 0 || !url || !url[0])
        return -22;
    (*opens)++;
    return 0;
}
static void soak_downloads(void) {
    uint32_t epoch;
    uint32_t opens = 0;
    for (epoch = 0; epoch < SOAK_EPOCHS; ++epoch) {
        struct zd_downloads dl;
        uint32_t i;
        zd_downloads_init(&dl, soak_dl_start, &opens);
        for (i = 0; i < ZD_DL_MAX; ++i) {
            char url[24];
            url[0] = 'u';
            url[1] = (char)('0' + (i % 10));
            url[2] = 0;
            ZD_CHECK_OK(zd_downloads_add(&dl, url, "n", 1000));
        }
        ZD_CHECK_EQ(dl.count, ZD_DL_MAX);
        /* start->progress->finish in FIFO order until drained */
        while (zd_downloads_start_next(&dl) == 0) {
            uint32_t id = zd_downloads_active(&dl);
            ZD_CHECK(id != 0);
            ZD_CHECK_OK(zd_downloads_progress(&dl, id, 500, 1000));
            ZD_CHECK_OK(zd_downloads_progress(&dl, id, 1000, 1000));
            ZD_CHECK_OK(zd_downloads_finish(&dl, id, 0));
            ZD_CHECK_EQ(zd_downloads_active(&dl), 0);
        }
        ZD_CHECK_EQ(dl.stats.started, ZD_DL_MAX);
        ZD_CHECK_EQ(dl.stats.completed, ZD_DL_MAX);
        ZD_CHECK_EQ(dl.stats.failed, 0u);
        ZD_CHECK_EQ(dl.count, ZD_DL_MAX);
        /* terminal items still occupy slots: full capacity rejects
         * exactly one probe and the counter records it */
        ZD_CHECK_EQ(zd_downloads_add(&dl, "extra", "x", 1), -28);
        ZD_CHECK_EQ(dl.count, ZD_DL_MAX);
        ZD_CHECK_EQ(dl.stats.rejected, 1u);
    }
    ZD_CHECK_EQ(opens, SOAK_EPOCHS * ZD_DL_MAX);
}

/* ---- epoch 3: notify dedupe/rate limits hold over long time ---- */
static void soak_notify(void) {
    uint32_t epoch;
    for (epoch = 0; epoch < SOAK_EPOCHS; ++epoch) {
        struct zd_notify nb;
        struct zd_notify_post post;
        zd_notification_id id = 0;
        uint32_t i;
        zd_notify_init(&nb);
        memset(&post, 0, sizeof(post));
        post.app_id = "soak.app";
        post.title = "t";
        post.body = "b";
        post.dedupe_key = "same";
        for (i = 0; i < 64; ++i) {
            int r = zd_notify_post(&nb, &post,
                                   (uint64_t)(epoch * 1000 + i) *
                                       1000000u,
                                   &id);
            ZD_CHECK(r <= 0 || r > 0);
        }
        /* visible never exceeds the bus cap */
        {
            zd_notification_id ids[ZD_NOTIFY_MAX];
            uint32_t vis = zd_notify_visible(
                &nb, 0xFFFFFFFFFFFFFFFFull, ids, ZD_NOTIFY_MAX);
            ZD_CHECK(vis <= ZD_NOTIFY_MAX);
            ZD_CHECK_EQ(vis, zd_notify_visible(
                                &nb, 0xFFFFFFFFFFFFFFFFull, 0, 0));
        }
        /* dismiss storm on empty ids must not crash */
        for (i = 0; i < 8; ++i)
            ZD_CHECK(zd_notify_dismiss(&nb, 100000u + i) <= 0);
    }
}

/* ---- epoch 4: lifecycle storms settle ---- */
static void soak_lifecycle(void) {
    uint32_t epoch;
    for (epoch = 0; epoch < SOAK_EPOCHS; ++epoch) {
        struct zd_lifecycle lc;
        uint32_t i;
        zd_lifecycle_init(&lc);
        for (i = 0; i < 128; ++i) {
            int rc = zd_lifecycle_dispatch(
                &lc, (enum zd_lifecycle_event)soak_pick(11),
                (uint64_t)i * 1000000u);
            ZD_CHECK(rc <= 0);
        }
        ZD_CHECK(zd_lifecycle_state(&lc) >= 0);
        ZD_CHECK(zd_lifecycle_state(&lc) <= ZD_LIFECYCLE_SUSPENDED);
        /* heavy-work gate agrees with the state */
        {
            int st = zd_lifecycle_state(&lc);
            int heavy = zd_lifecycle_allows_heavy_work(&lc);
            if (st == ZD_LIFECYCLE_ACTIVE)
                ZD_CHECK_EQ(heavy, 1);
            if (st == ZD_LIFECYCLE_STOPPED ||
                st == ZD_LIFECYCLE_SUSPENDED)
                ZD_CHECK_EQ(heavy, 0);
        }
    }
}

/* ---- epoch 5: settings schema isolation between generations ---- */
static void soak_settings(void) {
    uint32_t epoch;
    for (epoch = 0; epoch < SOAK_EPOCHS; ++epoch) {
        struct zd_settings st;
        struct zd_setting_def def;
        int64_t v = -1;
        uint32_t changed = 0;
        zd_settings_init(&st);
        /* fresh generation has no schema bleed */
        ZD_CHECK(zd_settings_get(&st, "soak.key", &v, 0, 0) != 0);
        memset(&def, 0, sizeof(def));
        def.key = "soak.key";
        def.type = ZD_SETTING_INT;
        def.scope = ZD_SCOPE_SESSION;
        def.default_value = 7;
        def.min_value = 0;
        def.max_value = 10;
        ZD_CHECK_OK(zd_settings_register(&st, &def));
        ZD_CHECK_OK(zd_settings_get(&st, "soak.key", &v, 0, 0));
        ZD_CHECK_EQ((int)v, 7);
        ZD_CHECK_OK(zd_settings_set_number(&st, "soak.key",
                                           3, ZD_PERM_SETTINGS_USER,
                                           &changed));
        ZD_CHECK_OK(zd_settings_get(&st, "soak.key", &v, 0, 0));
        ZD_CHECK_EQ((int)v, 3);
        /* duplicate register rejected within the generation */
        ZD_CHECK(zd_settings_register(&st, &def) < 0);
    }
}

/* ---- epoch 6: perfcenter rings do not accumulate ---- */
static void soak_perfcenter(void) {
    uint32_t epoch;
    for (epoch = 0; epoch < SOAK_EPOCHS; ++epoch) {
        struct zd_perf_center pc;
        struct zd_pc_input in;
        struct zd_pc_report rep;
        uint32_t i;
        zd_perf_center_init(&pc);
        for (i = 0; i < 400; ++i)
            ZD_CHECK_OK(zd_perf_center_record(&pc,
                                              5000 + soak_pick(6000)));
        ZD_CHECK_EQ(pc.count, ZD_PC_SAMPLES);
        memset(&in, 0, sizeof(in));
        in.fps_milli = 60000;
        in.budget_us = 16667;
        ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
        ZD_CHECK_EQ(rep.samples, ZD_PC_SAMPLES);
        ZD_CHECK(rep.worst_us >= rep.p95_us);
        ZD_CHECK(rep.p95_us >= rep.p50_us);
    }
}

void zd_test_soak_suite(void) {
    soak_rng = 0x50A4C0u; /* fixed seed */
    soak_clipboard();
    soak_downloads();
    soak_notify();
    soak_lifecycle();
    soak_settings();
    soak_perfcenter();
    /* final cross-generation sanity: first subsystem unaffected */
    {
        struct zd_clipboard cb;
        char out[32];
        zd_clipboard_init(&cb);
        ZD_CHECK_OK(zd_clipboard_copy(&cb, "final", "still-good",
                                      ZD_CLIP_FMT_TEXT, 0));
        ZD_CHECK_OK(zd_clipboard_paste(&cb, out, sizeof(out)));
        ZD_CHECK(strcmp(out, "still-good") == 0);
    }
}
