/* Canonical UI condition contract (Stage 5 part A): every shell/app
 * surface renders one of these conditions plus overlay modes.  The
 * per-surface state machines map INTO this contract (adoption table
 * in docs/ARCHITECTURE.md); this module owns the shared vocabulary,
 * mode flags and the error-code mapping.  Labels come from the i18n
 * catalog (`state.*` keys) so English/Hindi localization is one
 * path, not two. */
#ifndef ZEROOS_DESKTOP_UI_H
#define ZEROOS_DESKTOP_UI_H

#include <stdint.h>

struct zd_i18n; /* labels resolved through the shell catalog */

enum zd_ui_condition {
    ZD_UI_NORMAL = 0,         /* data present, fully interactive */
    ZD_UI_LOADING = 1,        /* transient; not interactive */
    ZD_UI_EMPTY = 2,          /* valid but nothing to show; interactive */
    ZD_UI_ERROR = 3,          /* failed; retry actions allowed */
    ZD_UI_OFFLINE = 4,        /* network/device unavailable; interactive */
    ZD_UI_PERMISSION_DENIED = 5, /* gate refused; interactive only via
                                  * grant actions */
    ZD_UI_LOW_RESOURCE = 6,   /* backpressure/reduced mode */
    ZD_UI_CONDITION_COUNT = 7
};

/* Modes overlay any condition (they never replace it). */
#define ZD_UI_F_REDUCED_MOTION 0x1u
#define ZD_UI_F_KEYBOARD_NAV   0x2u
#define ZD_UI_F_LOCALIZED      0x4u /* active locale differs from en */
#define ZD_UI_F_ALL (ZD_UI_F_REDUCED_MOTION | ZD_UI_F_KEYBOARD_NAV | \
                     ZD_UI_F_LOCALIZED)

struct zd_ui_surface {
    enum zd_ui_condition condition;
    uint32_t modes;
    uint32_t transitions;
};

void zd_ui_init(struct zd_ui_surface *u); /* NORMAL, no modes */
/* -22 + no change when the condition is out of range. */
int zd_ui_set_condition(struct zd_ui_surface *u,
                        enum zd_ui_condition c);
/* flags must be a subset of ZD_UI_F_ALL (-22 otherwise). */
int zd_ui_set_modes(struct zd_ui_surface *u, uint32_t flags,
                    int enabled);
/* Can the surface accept user action right now?  LOADING/ERROR/
 * LOW_RESOURCE/PERMISSION_DENIED say no here (permission surfaces
 * only accept grant actions, not general input). */
int zd_ui_interactive(enum zd_ui_condition c);
/* Catalog key for the condition ("state.<name>"), never NULL for
 * valid conditions, NULL otherwise. */
const char *zd_ui_condition_key(enum zd_ui_condition c);
/* Localized label through the i18n catalog (0 on bad condition or
 * catalog miss — catalog ships complete, tests enforce it). */
const char *zd_ui_condition_name(struct zd_i18n *i18n,
                                 enum zd_ui_condition c);
/* Map a negative ZD_E* return code to a condition:
 * EPERM->PERMISSION_DENIED, ENOENT->EMPTY, ENOSPC/EOVERFLOW->
 * LOW_RESOURCE, EBUSY/ESTATE->LOADING, EAGAIN->OFFLINE,
 * ECANCELED->NORMAL (back to idle), everything else -> ERROR.
 * Non-negative input maps to NORMAL. */
enum zd_ui_condition zd_ui_condition_from_rc(int rc);

#endif /* ZEROOS_DESKTOP_UI_H */
