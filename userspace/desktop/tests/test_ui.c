/* Canonical UI condition contract tests (part A) */
#include "test_harness.h"
#include <string.h>
#include <zeroos/desktop/desktop.h>

void zd_test_ui_suite(void) {
    struct zd_ui_surface u;
    struct zd_i18n en, hi;
    int c;

    /* init */
    zd_ui_init(&u);
    ZD_CHECK_EQ((int)u.condition, (int)ZD_UI_NORMAL);
    ZD_CHECK_EQ(u.modes, 0u);
    ZD_CHECK_EQ(u.transitions, 0u);
    zd_ui_init(0); /* null safe */

    /* condition transitions + invalid rejected without change */
    ZD_CHECK_EQ(zd_ui_set_condition(&u, ZD_UI_LOADING), 0);
    ZD_CHECK_EQ(u.transitions, 1u);
    ZD_CHECK_EQ(zd_ui_set_condition(&u, ZD_UI_LOADING), 0); /* same */
    ZD_CHECK_EQ(u.transitions, 1u);
    ZD_CHECK_EQ(zd_ui_set_condition(&u, (enum zd_ui_condition)99), -22);
    ZD_CHECK_EQ(zd_ui_set_condition(&u, (enum zd_ui_condition)-1), -22);
    ZD_CHECK_EQ(zd_ui_set_condition(0, ZD_UI_NORMAL), -22);
    ZD_CHECK_EQ((int)u.condition, (int)ZD_UI_LOADING);

    /* modes */
    ZD_CHECK_OK(zd_ui_set_modes(&u, ZD_UI_F_REDUCED_MOTION, 1));
    ZD_CHECK_OK(zd_ui_set_modes(&u, ZD_UI_F_KEYBOARD_NAV |
                                 ZD_UI_F_LOCALIZED, 1));
    ZD_CHECK_EQ(u.modes, (uint32_t)ZD_UI_F_ALL);
    ZD_CHECK_EQ(zd_ui_set_modes(&u, 0x8u, 1), -22); /* unknown flag */
    ZD_CHECK_EQ(zd_ui_set_modes(&u, ZD_UI_F_ALL, 0), 0);
    ZD_CHECK_EQ(u.modes, 0u);
    ZD_CHECK_EQ(zd_ui_set_modes(0, 0, 0), -22);

    /* interactivity matrix */
    ZD_CHECK_EQ(zd_ui_interactive(ZD_UI_NORMAL), 1);
    ZD_CHECK_EQ(zd_ui_interactive(ZD_UI_EMPTY), 1);
    ZD_CHECK_EQ(zd_ui_interactive(ZD_UI_OFFLINE), 1);
    ZD_CHECK_EQ(zd_ui_interactive(ZD_UI_LOADING), 0);
    ZD_CHECK_EQ(zd_ui_interactive(ZD_UI_ERROR), 0);
    ZD_CHECK_EQ(zd_ui_interactive(ZD_UI_PERMISSION_DENIED), 0);
    ZD_CHECK_EQ(zd_ui_interactive(ZD_UI_LOW_RESOURCE), 0);

    /* error-code mapping */
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(0), (int)ZD_UI_NORMAL);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(5), (int)ZD_UI_NORMAL);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-4),
                (int)ZD_UI_PERMISSION_DENIED);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-3), (int)ZD_UI_EMPTY);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-2),
                (int)ZD_UI_LOW_RESOURCE);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-10),
                (int)ZD_UI_LOW_RESOURCE);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-5),
                (int)ZD_UI_LOADING);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-9),
                (int)ZD_UI_LOADING);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-6),
                (int)ZD_UI_OFFLINE);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-7),
                (int)ZD_UI_NORMAL);
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-1),
                (int)ZD_UI_ERROR); /* EINVAL */
    ZD_CHECK_EQ((int)zd_ui_condition_from_rc(-99),
                (int)ZD_UI_ERROR);

    /* keys + localized labels through the shell catalog */
    zd_i18n_init(&en, ZD_LOCALE_EN);
    zd_i18n_init(&hi, ZD_LOCALE_HI);
    ZD_CHECK(zd_i18n_load_shell_catalog(&en) > 0);
    ZD_CHECK(zd_i18n_load_shell_catalog(&hi) > 0);
    for (c = 0; c < ZD_UI_CONDITION_COUNT; ++c) {
        const char *ke = zd_ui_condition_key((enum zd_ui_condition)c);
        const char *le, *lh;
        ZD_CHECK(ke != 0);
        le = zd_ui_condition_name(&en, (enum zd_ui_condition)c);
        lh = zd_ui_condition_name(&hi, (enum zd_ui_condition)c);
        ZD_CHECK(le != 0 && le[0]);
        ZD_CHECK(lh != 0 && lh[0]);
        ZD_CHECK(strcmp(le, lh) != 0); /* hi differs from en */
    }
    ZD_CHECK(zd_ui_condition_key((enum zd_ui_condition)99) == 0);
    ZD_CHECK(zd_ui_condition_name(&en,
                                  (enum zd_ui_condition)99) == 0);
    ZD_CHECK(zd_ui_condition_name(0, ZD_UI_NORMAL) == 0);
}
