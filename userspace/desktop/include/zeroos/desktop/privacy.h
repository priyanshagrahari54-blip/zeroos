/* Privacy centre (Stage 5 part B): an aggregation surface that reads
 * denial/refusal counters from the policy engines and derives an
 * explicit risk band with domain breakdown.  Pure read-only
 * derivation — no hidden state, thresholds are documented constants,
 * and sources are optional (NULL = domain unavailable, contributes
 * zero rather than failing the report). */
#ifndef ZEROOS_DESKTOP_PRIVACY_H
#define ZEROOS_DESKTOP_PRIVACY_H

#include <stdint.h>
#include <zeroos/desktop/sandbox.h>
#include <zeroos/desktop/firewall.h>
#include <zeroos/desktop/clipboard.h>
#include <zeroos/desktop/media.h>
#include <zeroos/desktop/eco.h>

/* risk thresholds (cumulative domain denials) */
#define ZD_PRIV_RISK_LOW 1      /* >= 1  -> watch */
#define ZD_PRIV_RISK_MODERATE 25 /* >= 25 in one domain -> review */
#define ZD_PRIV_RISK_HIGH 50     /* >= 50 total -> elevated */
#define ZD_PRIV_RISK_CRITICAL 200 /* >= 200 total -> act now */

enum zd_privacy_risk {
    ZD_PRIV_RISK_NONE = 0,
    ZD_PRIV_RISK_WATCH,
    ZD_PRIV_RISK_REVIEW,
    ZD_PRIV_RISK_ELEVATED,
    ZD_PRIV_RISK_ACT_NOW
};

/* domains of refusal */
struct zd_privacy_breakdown {
    uint32_t filesystem;   /* sandbox fs-class denials (approx: all
                            * sandbox denials attributed to policy) */
    uint32_t network;      /* firewall denials + sandbox net-class */
    uint32_t content;      /* media refusals (drm/rights/origin) */
    uint32_t sync;         /* eco refusals (perm/offline/pairing) */
};

struct zd_privacy_report {
    int risk;                       /* enum zd_privacy_risk */
    uint32_t denials_total;         /* sum of all domains */
    uint32_t protected_events;      /* e.g. sensitive clips kept */
    struct zd_privacy_breakdown bd;
};

struct zd_privacy_sources {
    const struct zd_sandbox *sb;
    const struct zd_fw *fw;
    const struct zd_clipboard *clip;
    const struct zd_media *media;
    const struct zd_eco *eco;
};

/* Assess current counters.  risk levels:
 *   NONE      all domains 0;
 *   WATCH     total >= 1 but every domain < MODERATE and total < HIGH;
 *   REVIEW    any single domain >= 25;
 *   ELEVATED  total >= 50 (and no single domain >= 200 case below);
 *   ACT_NOW   total >= 200.
 * EVALUATED IN ORDER: ACT_NOW (total>=200) > REVIEW (domain>=25) >
 * ELEVATED (total>=50) > WATCH (total>=1) > NONE. */
int zd_privacy_assess(const struct zd_privacy_sources *src,
                      struct zd_privacy_report *out);
const char *zd_privacy_risk_label(int risk);

#endif /* ZEROOS_DESKTOP_PRIVACY_H */
