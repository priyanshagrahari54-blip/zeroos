#ifndef ZEROOS_DESKTOP_I18N_H
#define ZEROOS_DESKTOP_I18N_H

/* English/Hindi localization for shell and UI-state strings. Catalogs are
 * static tables; lookup falls back to English and then to the key itself,
 * and coverage is measurable so missing translations fail tests rather
 * than surfacing at runtime. UTF-8 is preserved byte-exactly. */

#include <zeroos/desktop/common.h>

#define ZD_I18N_KEY_CAP 40
#define ZD_I18N_TEXT_CAP 96
#define ZD_I18N_MAX_KEYS 128

enum zd_locale {
    ZD_LOCALE_EN = 0,
    ZD_LOCALE_HI = 1
};

struct zd_i18n_entry {
    const char *key;
    const char *en;
    const char *hi;
};

struct zd_i18n {
    enum zd_locale locale;
    const struct zd_i18n_entry *catalog;
    uint32_t catalog_size;
    uint32_t fallback_count;   /* missing hi translations served as en */
    uint32_t missing_count;    /* missing keys served as key text */
};

void zd_i18n_init(struct zd_i18n *i18n, enum zd_locale locale);
/* Installs the shipped shell catalog (returns catalog size). */
uint32_t zd_i18n_load_shell_catalog(struct zd_i18n *i18n);
void zd_i18n_set_locale(struct zd_i18n *i18n, enum zd_locale locale);
enum zd_locale zd_i18n_locale(const struct zd_i18n *i18n);
const char *zd_i18n_text(struct zd_i18n *i18n, const char *key);
/* Bounded formatting with {0}, {1}, {2} placeholders (at most 3). Returns
 * 0, or -ZD_EOVERFLOW when the result would not fit. */
int zd_i18n_format(struct zd_i18n *i18n, const char *key,
                   const char *const *args, uint32_t arg_count,
                   char *out, uint32_t capacity);
/* Coverage: total keys in the active catalog and count of keys whose hi
 * translation is present. Equal counts mean full Hindi coverage. */
void zd_i18n_coverage(const struct zd_i18n *i18n, uint32_t *out_total,
                      uint32_t *out_hi_present);
const char *zd_i18n_locale_name(enum zd_locale locale);
enum zd_locale zd_i18n_locale_from_name(const char *name);
const struct zd_i18n_entry *zd_i18n_shell_catalog(uint32_t *out_size);

#endif
