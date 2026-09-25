#include <zeroos/desktop/i18n.h>

/* Shipped shell catalog: covers ZERO Bar, Universal Search, Settings,
 * Notifications, UI acceptance states and lifecycle labels in English and
 * Hindi (Devanagari, UTF-8). Adding a key requires both languages —
 * coverage tests fail on any missing translation. */
static const struct zd_i18n_entry shell_catalog[] = {
    {"shell.zero_bar", "ZERO Bar", "ज़ीरो बार"},
    {"shell.launcher", "Launcher", "लॉन्चर"},
    {"shell.search", "Search", "खोज"},
    {"shell.workspaces", "Workspaces", "कार्यक्षेत्र"},
    {"shell.overview", "Window overview", "विंडो अवलोकन"},
    {"shell.notifications", "Notifications", "सूचनाएँ"},
    {"shell.quick_controls", "Quick controls", "त्वरित नियंत्रण"},
    {"shell.status", "System status", "सिस्टम स्थिति"},
    {"shell.performance", "Performance center", "प्रदर्शन केंद्र"},
    {"shell.updates", "Updates and recovery", "अपडेट और पुनर्प्राप्ति"},
    {"shell.settings", "Settings", "सेटिंग्स"},
    {"shell.files", "Files", "फ़ाइलें"},
    {"shell.terminal", "Terminal", "टर्मिनल"},
    {"shell.clipboard", "Clipboard", "क्लिपबोर्ड"},
    {"shell.downloads", "Downloads", "डाउनलोड"},
    {"shell.wifi", "Wi-Fi", "वाई-फ़ाइ"},
    {"shell.bluetooth", "Bluetooth", "ब्लूटूथ"},
    {"shell.dnd", "Do not disturb", "डिस्टर्ब न करें"},
    {"shell.brightness", "Brightness", "चमक"},
    {"shell.menu", "Main menu", "मुख्य मेनू"},
    {"shell.title_none", "No focused window", "कोई विंडो फ़ोकस में नहीं"},
    {"launcher.no_apps", "No applications", "कोई अनुप्रयोग नहीं"},
    {"launcher.recent", "Recent", "हाल के"},
    {"launcher.busy", "Already running", "पहले से चल रहा है"},
    {"launcher.failed", "Launch failed", "लॉन्च विफल"},
    {"browser.tab_frozen", "Tab frozen", "टैब जमा हुआ"},
    {"browser.tab_discarded", "Tab discarded — reloads on focus",
     "टैब हटा दिया — फ़ोकस पर फिर से लोड होगा"},
    {"compat.not_supported", "Not supported by Windows compatibility",
     "विंडोज़ अनुकूलता द्वारा समर्थित नहीं"},
    {"state.loading", "Loading…", "लोड हो रहा है…"},
    {"state.empty", "Nothing here yet", "यहाँ अभी कुछ नहीं है"},
    {"state.error", "Something went wrong", "कुछ गड़बड़ हो गई"},
    {"state.offline", "Offline", "ऑफ़लाइन"},
    {"state.permission_denied", "Permission denied", "अनुमति अस्वीकृत"},
    {"state.low_resource", "Low resource mode", "कम संसाधन मोड"},
    {"state.reduced_motion", "Reduced motion", "कम गति"},
    {"state.keyboard_nav", "Keyboard navigation", "कीबोर्ड नेविगेशन"},
    {"lifecycle.stopped", "Stopped", "रोका गया"},
    {"lifecycle.dormant", "Dormant", "निष्क्रिय"},
    {"lifecycle.warm", "Warm", "तैयार"},
    {"lifecycle.active", "Active", "सक्रिय"},
    {"lifecycle.throttled", "Throttled", "थ्रॉटल"},
    {"lifecycle.suspended", "Suspended", "निलंबित"},
    {"ai.dormant_hint", "AI assistant ready — starts when requested",
     "एआई सहायक तैयार — अनुरोध पर शुरू होगा"},
    {"search.no_results", "No matches", "कोई परिणाम नहीं"},
    {"search.provider_unavailable", "Provider unavailable",
     "प्रदाता अनुपलब्ध"},
    {"notify.center_title", "Notification center", "सूचना केंद्र"},
    {"notify.rate_limited", "Notifications paused briefly",
     "सूचनाएँ थोड़ी देर के लिए रोकी गईं"},
    {"settings.reset", "Reset to default", "डिफ़ॉल्ट पर रीसेट करें"},
    {"settings.search_hint", "Search settings", "सेटिंग्स खोजें"},
    {"window.minimize", "Minimize", "छोटा करें"},
    {"window.maximize", "Maximize", "बड़ा करें"},
    {"window.close", "Close", "बंद करें"},
    {"window.restore", "Restore", "पुनर्स्थापित करें"},
    {"gaming.mode", "Gaming mode", "गेमिंग मोड"},
    {"media.paused", "Paused", "रोका गया"}
};

static const struct zd_i18n_entry *find_entry(const struct zd_i18n *i18n,
                                              const char *key) {
    uint32_t index;
    if (!i18n || !key || !i18n->catalog)
        return (const struct zd_i18n_entry *)0;
    for (index = 0; index < i18n->catalog_size; ++index)
        if (zd_str_equal(i18n->catalog[index].key, key))
            return &i18n->catalog[index];
    return (const struct zd_i18n_entry *)0;
}

void zd_i18n_init(struct zd_i18n *i18n, enum zd_locale locale) {
    if (!i18n)
        return;
    zd_memset(i18n, 0, sizeof(*i18n));
    i18n->locale = locale;
}

uint32_t zd_i18n_load_shell_catalog(struct zd_i18n *i18n) {
    if (!i18n)
        return 0;
    i18n->catalog = shell_catalog;
    i18n->catalog_size = (uint32_t)ZD_ARRAY_COUNT(shell_catalog);
    i18n->fallback_count = 0;
    i18n->missing_count = 0;
    return i18n->catalog_size;
}

void zd_i18n_set_locale(struct zd_i18n *i18n, enum zd_locale locale) {
    if (!i18n)
        return;
    i18n->locale = locale;
    i18n->fallback_count = 0;
    i18n->missing_count = 0;
}

enum zd_locale zd_i18n_locale(const struct zd_i18n *i18n) {
    return i18n ? i18n->locale : ZD_LOCALE_EN;
}

const char *zd_i18n_text(struct zd_i18n *i18n, const char *key) {
    const struct zd_i18n_entry *entry;
    if (!i18n || !key)
        return "";
    entry = find_entry(i18n, key);
    if (!entry) {
        ++i18n->missing_count;
        return key;
    }
    if (i18n->locale == ZD_LOCALE_HI) {
        if (entry->hi && *entry->hi)
            return entry->hi;
        ++i18n->fallback_count;
    }
    return entry->en ? entry->en : key;
}

int zd_i18n_format(struct zd_i18n *i18n, const char *key,
                   const char *const *args, uint32_t arg_count,
                   char *out, uint32_t capacity) {
    const char *template_text;
    uint32_t written = 0;
    const char *cursor;

    if (!i18n || !key || !out || capacity == 0)
        return -ZD_EINVAL;
    if (arg_count > 3)
        return -ZD_EOVERFLOW;
    template_text = zd_i18n_text(i18n, key);
    cursor = template_text;
    while (*cursor) {
        if (cursor[0] == '{' && cursor[1] >= '0' && cursor[1] <= '2' &&
            cursor[2] == '}') {
            uint32_t which = (uint32_t)(cursor[1] - '0');
            const char *sub;
            if (which >= arg_count || !args)
                sub = "";
            else
                sub = args[which] ? args[which] : "";
            while (*sub) {
                if (written + 1U >= capacity)
                    return -ZD_EOVERFLOW;
                out[written++] = *sub++;
            }
            cursor += 3;
            continue;
        }
        if (written + 1U >= capacity)
            return -ZD_EOVERFLOW;
        out[written++] = *cursor++;
    }
    out[written] = '\0';
    return 0;
}

void zd_i18n_coverage(const struct zd_i18n *i18n, uint32_t *out_total,
                      uint32_t *out_hi_present) {
    uint32_t present = 0;
    uint32_t index;
    if (out_total)
        *out_total = i18n ? i18n->catalog_size : 0;
    if (!i18n || !i18n->catalog) {
        if (out_hi_present)
            *out_hi_present = 0;
        return;
    }
    for (index = 0; index < i18n->catalog_size; ++index)
        if (i18n->catalog[index].hi && *i18n->catalog[index].hi)
            ++present;
    if (out_hi_present)
        *out_hi_present = present;
}

const char *zd_i18n_locale_name(enum zd_locale locale) {
    switch (locale) {
    case ZD_LOCALE_HI:
        return "hi";
    case ZD_LOCALE_EN:
    default:
        return "en";
    }
}

enum zd_locale zd_i18n_locale_from_name(const char *name) {
    if (name && (name[0] == 'h' || name[0] == 'H') &&
        (name[1] == 'i' || name[1] == 'I'))
        return ZD_LOCALE_HI;
    return ZD_LOCALE_EN;
}

const struct zd_i18n_entry *zd_i18n_shell_catalog(uint32_t *out_size) {
    if (out_size)
        *out_size = (uint32_t)ZD_ARRAY_COUNT(shell_catalog);
    return shell_catalog;
}
