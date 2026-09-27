/* Built-in search providers: commands + diagnostics.  providers.h */
#include <zeroos/desktop/providers.h>

static uint32_t p_contains(const char *hay, const char *needle) {
    uint32_t h = 0, n = 0, i, j;
    if (!hay || !needle || !needle[0])
        return 0;
    while (hay[h])
        ++h;
    while (needle[n])
        ++n;
    if (n > h)
        return 0;
    for (i = 0; i + n <= h; ++i) {
        for (j = 0; j < n && hay[i + j] == needle[j]; ++j)
            ;
        if (j == n)
            return 1;
    }
    return 0;
}
static uint32_t p_startswith(const char *s, const char *pfx) {
    uint32_t i = 0;
    if (!s || !pfx)
        return 0;
    while (pfx[i]) {
        if (s[i] != pfx[i])
            return 0;
        ++i;
    }
    return 1;
}

/* ---- commands provider ---- */

static uint32_t p_commands_available(void *context) {
    const struct zd_cmd_provider *cp = context;
    return cp && cp->count > 0;
}

static int p_commands_query(void *context,
                            const struct zd_intent *intent,
                            struct zd_search_result *results,
                            uint32_t capacity,
                            volatile uint32_t *cancel_token,
                            uint32_t query_generation) {
    struct zd_cmd_provider *cp = context;
    const char *needle;
    uint32_t i, n = 0, score;
    if (!cp || !intent || !results)
        return -22;
    if (cancel_token && *cancel_token != query_generation)
        return 0; /* canceled: stop immediately */
    /* match against every token's raw text (usually one) */
    needle = intent->token_count ? intent->tokens[0] : intent->raw;
    for (i = 0; i < ZD_CMD_MAX && n < capacity; ++i) {
        struct zd_command *c = &cp->commands[i];
        if (!c->in_use || !p_contains(c->name, needle))
            continue;
        if (p_startswith(c->name, needle))
            score = 100;
        else
            score = 60;
        results[n].document_id = (uint64_t)(i + 1);
        results[n].kind = ZD_SEARCH_COMMAND;
        {
            uint32_t k = 0;
            while (c->name[k] && k + 1 < ZD_SEARCH_LABEL_CAP) {
                results[n].label[k] = c->name[k];
                ++k;
            }
            results[n].label[k] = 0;
        }
        results[n].path[0] = 'c';
        results[n].path[1] = 'm';
        results[n].path[2] = 'd';
        results[n].path[3] = ':';
        {
            uint32_t k = 0;
            while (c->name[k] && k + 5 < ZD_SEARCH_PATH_CAP) {
                results[n].path[4 + k] = c->name[k];
                ++k;
            }
            results[n].path[4 + k] = 0;
        }
        results[n].score = score;
        results[n].provider_priority = 0;
        results[n].recency_score = 0;
        results[n].use_count = 0;
        results[n].available = 1;
        ++n;
    }
    return (int)n;
}

int zd_commands_init(struct zd_cmd_provider *cp) {
    uint32_t i;
    if (!cp)
        return -22;
    for (i = 0; i < ZD_CMD_MAX; ++i)
        cp->commands[i].in_use = 0;
    cp->count = 0;
    cp->provider.name = "commands";
    cp->provider.kind_mask = 1u << ZD_SEARCH_COMMAND;
    cp->provider.priority = 50;
    cp->provider.available = p_commands_available;
    cp->provider.query = p_commands_query;
    cp->provider.context = cp;
    cp->provider.registered = 0;
    return 0;
}

int zd_commands_register(struct zd_cmd_provider *cp, const char *name,
                         const char *desc,
                         int (*run)(void *), void *ctx) {
    uint32_t i;
    if (!cp || !name || !name[0] || !run)
        return -22;
    for (i = 0; i < ZD_CMD_MAX; ++i) {
        if (cp->commands[i].in_use) {
            uint32_t k = 0, same = 1;
            while (cp->commands[i].name[k] && name[k]) {
                if (cp->commands[i].name[k] != name[k]) {
                    same = 0;
                    break;
                }
                ++k;
            }
            if (same && cp->commands[i].name[k] == 0 &&
                name[k] == 0)
                return -17; /* EEXIST duplicate name */
        }
    }
    if (cp->count >= ZD_CMD_MAX)
        return -28;
    for (i = 0; i < ZD_CMD_MAX; ++i) {
        struct zd_command *c;
        if (cp->commands[i].in_use)
            continue;
        c = &cp->commands[i];
        {
            uint32_t k = 0;
            while (name[k] && k + 1 < ZD_CMD_NAME) {
                c->name[k] = name[k];
                ++k;
            }
            c->name[k] = 0;
        }
        c->description[0] = 0;
        if (desc) {
            uint32_t k = 0;
            while (desc[k] && k + 1 < ZD_CMD_DESC) {
                c->description[k] = desc[k];
                ++k;
            }
            c->description[k] = 0;
        }
        c->run = run;
        c->ctx = ctx;
        c->in_use = 1;
        cp->count++;
        return 0;
    }
    return -28;
}

int zd_commands_execute(struct zd_cmd_provider *cp, const char *name) {
    uint32_t i;
    if (!cp || !name || !name[0])
        return -22;
    for (i = 0; i < ZD_CMD_MAX; ++i) {
        struct zd_command *c = &cp->commands[i];
        uint32_t k = 0, same = 1;
        if (!c->in_use)
            continue;
        while (c->name[k] && name[k]) {
            if (c->name[k] != name[k]) {
                same = 0;
                break;
            }
            ++k;
        }
        if (same && c->name[k] == 0 && name[k] == 0)
            return c->run(c->ctx);
    }
    return -2;
}

/* ---- diagnostics provider ---- */

static uint32_t p_diag_available(void *context) {
    const struct zd_diag_provider *dp = context;
    return dp && dp->line;
}

#define P_DIAG_MAX_LINES 8

static int p_diag_query(void *context, const struct zd_intent *intent,
                        struct zd_search_result *results,
                        uint32_t capacity,
                        volatile uint32_t *cancel_token,
                        uint32_t query_generation) {
    struct zd_diag_provider *dp = context;
    uint32_t idx = 0, n = 0;
    char line[96];
    if (!dp || !intent || !results || !dp->line)
        return -22;
    if (cancel_token && *cancel_token != query_generation)
        return 0;
    while (idx < P_DIAG_MAX_LINES && n < capacity) {
        if (!dp->line(dp->ctx, idx, line, sizeof(line)))
            break;
        {
            uint32_t k = 0;
            while (line[k] && k + 1 < ZD_SEARCH_LABEL_CAP) {
                results[n].label[k] = line[k];
                ++k;
            }
            results[n].label[k] = 0;
        }
        results[n].document_id = 0x60000000ull + idx;
        results[n].kind = ZD_SEARCH_DIAGNOSTIC;
        results[n].path[0] = 0;
        results[n].score = 80 - idx * 10;
        results[n].provider_priority = 0;
        results[n].recency_score = 0;
        results[n].use_count = 0;
        results[n].available = 1;
        ++n;
        ++idx;
    }
    return (int)n;
}

void zd_diagnostics_init(struct zd_diag_provider *dp,
                         zd_diag_line_fn line, void *ctx) {
    if (!dp)
        return;
    dp->line = line;
    dp->ctx = ctx;
    dp->provider.name = "diagnostics";
    dp->provider.kind_mask = 1u << ZD_SEARCH_DIAGNOSTIC;
    dp->provider.priority = 40;
    dp->provider.available = p_diag_available;
    dp->provider.query = p_diag_query;
    dp->provider.context = dp;
    dp->provider.registered = 0;
}
