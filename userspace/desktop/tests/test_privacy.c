/* Privacy centre tests (part B) — counters fed by real engines */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

void zd_test_privacy_suite(void) {
    struct zd_sandbox sb;
    struct zd_fw fw;
    struct zd_clipboard cb;
    struct zd_media m;
    struct zd_eco e;
    struct zd_privacy_sources src;
    struct zd_privacy_report rep;
    uint32_t i, idx;

    zd_sandbox_init(&sb);
    zd_fw_init(&fw);
    zd_clipboard_init(&cb);
    zd_media_init(&m);
    zd_eco_init(&e);

    /* empty sources -> NONE */
    memset(&src, 0, sizeof(src));
    ZD_CHECK_EQ(zd_privacy_assess(&src, NULL), -22); /* NULL report */
    ZD_CHECK_EQ(zd_privacy_assess(NULL, &rep), -22);
    ZD_CHECK_OK(zd_privacy_assess(&src, &rep));
    ZD_CHECK_EQ(rep.risk, ZD_PRIV_RISK_NONE);
    ZD_CHECK_EQ(rep.denials_total, 0);

    /* one denial -> WATCH */
    ZD_CHECK_OK(zd_sandbox_define(&sb, "web", 1u << ZD_SB_FS_READ));
    for (i = 0; i < 3; ++i) {
        ZD_CHECK(zd_sandbox_check(&sb, "web", ZD_SB_FS_WRITE) < 0);
        ZD_CHECK(zd_sandbox_check(&sb, "web", ZD_SB_NET_SERVER) < 0);
    }
    src.sb = &sb;
    ZD_CHECK_OK(zd_privacy_assess(&src, &rep));
    ZD_CHECK_EQ(rep.risk, ZD_PRIV_RISK_WATCH);
    ZD_CHECK_EQ(rep.bd.filesystem, 6); /* all sandbox denials */
    ZD_CHECK_EQ(rep.denials_total, 6);

    /* sensitive clipboard counted as protected, not risk */
    ZD_CHECK_OK(zd_clipboard_copy(&cb, "app", "secret",
                                  ZD_CLIP_FMT_TEXT, 1));
    src.clip = &cb;
    ZD_CHECK_OK(zd_privacy_assess(&src, &rep));
    ZD_CHECK_EQ(rep.protected_events, 1);
    ZD_CHECK_EQ(rep.risk, ZD_PRIV_RISK_WATCH);

    /* media DRM refusals push content domain up: single domain
     * >= 25 -> REVIEW even if total < 50 */
    ZD_CHECK_OK(zd_media_register_source(&m, "https://lawful",
                                          ZD_MEDIA_RIGHT_PLAY));
    ZD_CHECK_OK(zd_media_add_item(&m, "https://lawful", "tv", 1));
    src.media = &m;
    for (i = 0; i < 25; ++i)
        ZD_CHECK(zd_media_play(&m, 1) < 0);
    ZD_CHECK_OK(zd_privacy_assess(&src, &rep));
    ZD_CHECK_EQ(rep.bd.content, 25);
    ZD_CHECK_EQ(rep.risk, ZD_PRIV_RISK_REVIEW);
    ZD_CHECK_EQ(rep.denials_total, 31);

    /* network denials via firewall default-deny */
    {
        struct zd_fw_flow f;
        src.fw = &fw;
        memset(&f, 0, sizeof(f));
        f.dir = ZD_FW_OUT;
        f.proto = ZD_FW_TCP;
        f.dst_port = 443;
        for (i = 0; i < 50; ++i)
            ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    }
    ZD_CHECK_OK(zd_privacy_assess(&src, &rep));
    ZD_CHECK_EQ(rep.bd.network, 50);
    /* total now 81 >= 200? no; >=50 yes; but domain REVIEW wins
     * precedence? act-now requires >=200; review requires domain
     * >=25 -> REVIEW has precedence over ELEVATED in our order */
    ZD_CHECK_EQ(rep.risk, ZD_PRIV_RISK_REVIEW);
    ZD_CHECK_EQ(rep.denials_total, 81);

    /* eco refusals feed the sync domain */
    ZD_CHECK_OK(zd_eco_pair(&e, "phone", &idx));
    src.eco = &e;
    ZD_CHECK(zd_eco_enqueue(&e, idx, ZD_ECO_PERM_SYNC_FILES, "x") < 0);
    ZD_CHECK(zd_eco_enqueue(&e, 7, ZD_ECO_PERM_SYNC_FILES, "x") < 0);
    ZD_CHECK_OK(zd_privacy_assess(&src, &rep));
    ZD_CHECK_EQ(rep.bd.sync, 2);
    ZD_CHECK_EQ(rep.denials_total, 83);

    /* push total >= 200 -> ACT_NOW (precedes everything) */
    for (i = 0; i < 120; ++i) {
        struct zd_fw_flow f;
        memset(&f, 0, sizeof(f));
        f.dir = ZD_FW_OUT;
        f.proto = ZD_FW_UDP;
        f.dst_port = 53;
        ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    }
    ZD_CHECK_OK(zd_privacy_assess(&src, &rep));
    ZD_CHECK_EQ(rep.denials_total, 203);
    ZD_CHECK_EQ(rep.risk, ZD_PRIV_RISK_ACT_NOW);

    /* labels */
    ZD_CHECK(strcmp(zd_privacy_risk_label(ZD_PRIV_RISK_NONE),
                    "none") == 0);
    ZD_CHECK(strcmp(zd_privacy_risk_label(ZD_PRIV_RISK_WATCH),
                    "watch") == 0);
    ZD_CHECK(strcmp(zd_privacy_risk_label(ZD_PRIV_RISK_REVIEW),
                    "review") == 0);
    ZD_CHECK(strcmp(zd_privacy_risk_label(ZD_PRIV_RISK_ELEVATED),
                    "elevated") == 0);
    ZD_CHECK(strcmp(zd_privacy_risk_label(ZD_PRIV_RISK_ACT_NOW),
                    "act-now") == 0);
    ZD_CHECK(strcmp(zd_privacy_risk_label(42), "act-now") == 0);
}
