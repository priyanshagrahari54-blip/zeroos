/* Study dictionary.  See dict.h. */
#include <string.h>
#include <zeroos/desktop/dict.h>

static char dc_lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

/* case-insensitive strcmp */
static int dc_cmp(const char *a, const char *b) {
    size_t i;
    for (i = 0;; ++i) {
        char ca = dc_lower(a[i]);
        char cb = dc_lower(b[i]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (!ca)
            return 0;
    }
}

static void dc_insert_sort(char w[][ZD_DICT_WORD], uint32_t n) {
    uint32_t i;
    for (i = 1; i < n; ++i) {
        char tmp[ZD_DICT_WORD];
        uint32_t j = i;
        memcpy(tmp, w[i], ZD_DICT_WORD);
        while (j > 0 && dc_cmp(tmp, w[j - 1]) < 0) {
            memcpy(w[j], w[j - 1], ZD_DICT_WORD);
            j--;
        }
        memcpy(w[j], tmp, ZD_DICT_WORD);
    }
}

static int dc_has_prefix(const char *word, const char *prefix) {
    size_t i;
    for (i = 0; prefix[i]; ++i)
        if (dc_lower(word[i]) != dc_lower(prefix[i]))
            return 0;
    return 1;
}

int zd_dict_load(struct zd_dict *d, zd_dict_source_fn src, void *ctx) {
    uint32_t n = 0, i;
    int rc;
    if (!d || !src)
        return -22;
    d->loaded = 0; /* failed loads leave the dict unreadable (-95) */
    d->truncated = 0;
    rc = src(ctx, d->words, ZD_DICT_MAX, &n);
    if (rc < 0) {
        d->count = 0;
        d->stats.load_errors++;
        return rc;
    }
    if (n > ZD_DICT_MAX) { /* source contract violation: clamp */
        n = ZD_DICT_MAX;
        d->truncated = 1;
    }
    dc_insert_sort(d->words, n);
    { /* case-insensitive de-duplicate in place */
        uint32_t k = 0;
        for (i = 0; i < n; ++i) {
            if (k > 0 && dc_cmp(d->words[k - 1], d->words[i]) == 0)
                continue;
            if (k != i)
                memcpy(d->words[k], d->words[i], ZD_DICT_WORD);
            k++;
        }
        d->count = k;
    }
    d->loaded = 1;
    d->stats.loads++;
    return 0;
}

int zd_dict_lookup(struct zd_dict *d, const char *word) {
    uint32_t lo, hi;
    if (!d || !word || !word[0])
        return -22;
    if (!d->loaded)
        return -95;
    d->stats.lookups++;
    lo = 0;
    hi = d->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = dc_cmp(d->words[mid], word);
        if (c == 0) {
            d->stats.lookup_hits++;
            return 1;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

int zd_dict_prefix(struct zd_dict *d, const char *prefix,
                   char out[][ZD_DICT_WORD], uint32_t cap,
                   uint32_t *out_n) {
    uint32_t i, written = 0;
    int total = 0;
    if (!d || !prefix || !out_n)
        return -22;
    if (!d->loaded)
        return -95;
    d->stats.prefix_queries++;
    for (i = 0; i < d->count; ++i) {
        if (!dc_has_prefix(d->words[i], prefix))
            continue;
        if (out && written < cap) {
            memcpy(out[written], d->words[i], ZD_DICT_WORD);
            written++;
        }
        total++;
    }
    d->stats.suggestions += written;
    *out_n = written;
    return total;
}
