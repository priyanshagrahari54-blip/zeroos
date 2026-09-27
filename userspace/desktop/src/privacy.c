/* Privacy centre aggregation.  See privacy.h. */
#include <zeroos/desktop/privacy.h>

static uint32_t p_add(uint32_t total, uint32_t v) {
    return total + v;
}

int zd_privacy_assess(const struct zd_privacy_sources *src,
                      struct zd_privacy_report *out) {
    struct zd_privacy_breakdown bd;
    uint32_t total = 0, prot = 0;
    int risk;
    uint32_t i;
    if (!src || !out)
        return -22;
    bd.filesystem = bd.network = bd.content = bd.sync = 0;
    if (src->sb)
        bd.filesystem = p_add(bd.filesystem, src->sb->stats.denied);
    if (src->fw)
        bd.network = p_add(bd.network, src->fw->stats.denied);
    if (src->clip)
        prot = p_add(prot, src->clip->stats.sensitive_kept);
    if (src->media)
        bd.content = p_add(
            bd.content, src->media->stats.refusals_drm +
                            src->media->stats.refusals_rights +
                            src->media->stats.refusals_origin);
    if (src->eco)
        bd.sync = p_add(bd.sync, src->eco->stats.refused_perm +
                                     src->eco->stats.refused_offline +
                                     src->eco->stats.refused_pairing);
    total = bd.filesystem + bd.network + bd.content + bd.sync;

    if (total >= ZD_PRIV_RISK_CRITICAL)
        risk = ZD_PRIV_RISK_ACT_NOW;
    else {
        uint32_t doms[4];
        int any_review = 0;
        doms[0] = bd.filesystem;
        doms[1] = bd.network;
        doms[2] = bd.content;
        doms[3] = bd.sync;
        for (i = 0; i < 4; ++i)
            if (doms[i] >= ZD_PRIV_RISK_MODERATE)
                any_review = 1;
        if (any_review)
            risk = ZD_PRIV_RISK_REVIEW;
        else if (total >= ZD_PRIV_RISK_HIGH)
            risk = ZD_PRIV_RISK_ELEVATED;
        else if (total >= ZD_PRIV_RISK_LOW)
            risk = ZD_PRIV_RISK_WATCH;
        else
            risk = ZD_PRIV_RISK_NONE;
    }
    out->risk = risk;
    out->denials_total = total;
    out->protected_events = prot;
    out->bd = bd;
    return 0;
}

const char *zd_privacy_risk_label(int risk) {
    switch (risk) {
    case ZD_PRIV_RISK_NONE:
        return "none";
    case ZD_PRIV_RISK_WATCH:
        return "watch";
    case ZD_PRIV_RISK_REVIEW:
        return "review";
    case ZD_PRIV_RISK_ELEVATED:
        return "elevated";
    default:
        return "act-now";
    }
}
