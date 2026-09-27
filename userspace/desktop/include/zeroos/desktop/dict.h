/* Study dictionary (Stage 5 part G): bounded word list loaded from an
 * injected source with case-insensitive exact lookup and prefix
 * suggestions.  Data arrives from the caller (bundled word list binds
 * later); this module owns search logic, never a faked corpus. */
#ifndef ZEROOS_DESKTOP_DICT_H
#define ZEROOS_DESKTOP_DICT_H

#include <stdint.h>

#define ZD_DICT_MAX 2048
#define ZD_DICT_WORD 32

/* Source contract: fill up to `cap` NUL-terminated words (length
 * < ZD_DICT_WORD) and set *out_n; return 0, or a negative errno.
 * Returning more than cap entries is a source contract violation. */
typedef int (*zd_dict_source_fn)(void *ctx, char (*words)[ZD_DICT_WORD],
                                 uint32_t cap, uint32_t *out_n);

struct zd_dict {
    char words[ZD_DICT_MAX][ZD_DICT_WORD];
    uint32_t count;
    uint8_t loaded;
    uint8_t truncated; /* source returned more than we kept */
    struct {
        uint32_t loads, load_errors, lookups, lookup_hits;
        uint32_t prefix_queries, suggestions, rejected;
    } stats;
};

/* Load (replace) from source: duplicates are dropped, entries sorted
 * byte-order case-insensitively for stable binary search.  Source
 * error -> -errno + load_errors (list left unloaded).  Full+more ->
 * truncated=1 (kept ZD_DICT_MAX). */
int zd_dict_load(struct zd_dict *d, zd_dict_source_fn src, void *ctx);
/* Exact lookup, ASCII case-insensitive: 1 hit, 0 miss, -22 args,
 * -95 before a successful load. */
int zd_dict_lookup(struct zd_dict *d, const char *word);
/* Words with the given prefix (case-insensitive), byte-order, up to
 * cap written; *out_n = number written, return = total matches
 * (>= *out_n when truncated by cap).  -22 args, -95 not loaded. */
int zd_dict_prefix(struct zd_dict *d, const char *prefix,
                   char out[][ZD_DICT_WORD], uint32_t cap,
                   uint32_t *out_n);

#endif /* ZEROOS_DESKTOP_DICT_H */
