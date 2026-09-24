#include <zeroos/desktop/search.h>

/* Parser --------------------------------------------------------------- */

static const struct {
    const char *prefix;
    enum zd_search_kind kind;
} kind_prefixes[] = {
    {"app:", ZD_SEARCH_APP},
    {"file:", ZD_SEARCH_FILE},
    {"folder:", ZD_SEARCH_FOLDER},
    {"dir:", ZD_SEARCH_FOLDER},
    {"set:", ZD_SEARCH_SETTING},
    {"settings:", ZD_SEARCH_SETTING},
    {"doc:", ZD_SEARCH_DOCUMENT},
    {"recent:", ZD_SEARCH_RECENT},
    {"cmd:", ZD_SEARCH_COMMAND},
    {"diag:", ZD_SEARCH_DIAGNOSTIC},
    {"ai:", ZD_SEARCH_AI_ACTION}
};

const char *zd_search_kind_name(enum zd_search_kind kind) {
    static const char *const names[ZD_SEARCH_KIND_COUNT] = {
        "any", "app", "file", "folder", "setting", "document", "recent",
        "command", "diagnostic", "ai_action"
    };
    if ((int)kind < 0 || (int)kind >= ZD_SEARCH_KIND_COUNT)
        return "?";
    return names[kind];
}

int zd_search_parse(const char *input, struct zd_intent *out_intent) {
    char scratch[ZD_SEARCH_QUERY_CAP];
    const char *cursor;
    size_t length = 0;
    uint32_t token_count = 0;
    size_t index;

    if (!input || !out_intent)
        return -ZD_EINVAL;
    zd_memset(out_intent, 0, sizeof(*out_intent));
    out_intent->kind = ZD_SEARCH_ANY;

    length = zd_str_length(input);
    if (length == 0)
        return -ZD_EINVAL;
    if (length >= ZD_SEARCH_QUERY_CAP)
        return -ZD_EOVERFLOW;
    zd_str_copy(scratch, sizeof(scratch), input);

    cursor = scratch;
    while (*cursor == ' ')
        ++cursor;

    /* Kind prefix at the very start (unquoted only). */
    for (index = 0; index < ZD_ARRAY_COUNT(kind_prefixes); ++index) {
        size_t prefix_length = zd_str_length(kind_prefixes[index].prefix);
        if (zd_str_length(cursor) > prefix_length) {
            uint32_t match = 1;
            size_t inner;
            for (inner = 0; inner < prefix_length; ++inner)
                if (zd_lower_ascii(cursor[inner]) !=
                    zd_lower_ascii(kind_prefixes[index].prefix[inner])) {
                    match = 0;
                    break;
                }
            if (match) {
                out_intent->kind = kind_prefixes[index].kind;
                cursor += prefix_length;
                break;
            }
        }
    }

    /* Tokenize: whitespace separated, double quotes group. Overflow is
     * rejected only when capacity is genuinely exhausted. */
    for (;;) {
        char *dest;
        size_t written = 0;
        uint32_t quoted = 0;
        while (*cursor == ' ')
            ++cursor;
        if (!*cursor)
            break;
        if (token_count >= ZD_SEARCH_MAX_TOKENS)
            return -ZD_EOVERFLOW;
        dest = out_intent->tokens[token_count];
        if (*cursor == '"') {
            quoted = 1;
            ++cursor;
            while (*cursor && *cursor != '"') {
                if (written + 1 >= ZD_SEARCH_TOKEN_CAP)
                    return -ZD_EOVERFLOW;
                dest[written++] = *cursor++;
            }
            if (*cursor != '"')
                return -ZD_EINVAL; /* unterminated quote */
            ++cursor;
        } else {
            while (*cursor && *cursor != ' ') {
                if (written + 1 >= ZD_SEARCH_TOKEN_CAP)
                    return -ZD_EOVERFLOW;
                dest[written++] = *cursor++;
            }
        }
        if (written == 0)
            return -ZD_EINVAL; /* empty quoted token */
        dest[written] = '\0';
        out_intent->quoted[token_count] = quoted;
        ++token_count;
    }
    if (token_count == 0)
        return -ZD_EINVAL;
    out_intent->token_count = token_count;
    zd_str_copy(out_intent->raw, sizeof(out_intent->raw), input);
    return 0;
}

/* Index ---------------------------------------------------------------- */

void zd_search_init(struct zd_search *search) {
    if (!search)
        return;
    zd_memset(search, 0, sizeof(*search));
    zd_search_register_index_provider(search);
}

static int doc_slot(struct zd_search *search, const char *key) {
    uint32_t index;
    uint64_t id = zd_hash64(key);
    for (index = 0; index < ZD_SEARCH_INDEX_CAPACITY; ++index)
        if (search->documents[index].in_use &&
            search->documents[index].id == id &&
            zd_str_equal(search->documents[index].key, key))
            return (int)index;
    return -1;
}

static void emit_index_event(struct zd_search *search,
                             enum zd_index_doc_kind kind, const char *key) {
    uint32_t index;
    for (index = 0; index < search->listener_count; ++index)
        search->listeners[index].callback(search->listeners[index].context,
                                          kind, key);
}

int zd_search_index_upsert(struct zd_search *search,
                           enum zd_index_doc_kind kind, const char *key,
                           const char *label) {
    int slot;
    uint32_t index;

    if (!search || !key || !*key)
        return -ZD_EINVAL;
    if ((int)kind < 0 || (int)kind > ZD_INDEX_AI_ACTION)
        return -ZD_EINVAL;
    slot = doc_slot(search, key);
    if (slot < 0) {
        for (index = 0; index < ZD_SEARCH_INDEX_CAPACITY; ++index)
            if (!search->documents[index].in_use) {
                slot = (int)index;
                break;
            }
        if (slot < 0)
            return -ZD_ENOSPC;
        search->documents[slot].in_use = 1;
        search->documents[slot].id = zd_hash64(key);
        search->documents[slot].tokenized = 0;
        ++search->document_count;
        ++search->index_stats.seeded;
    }
    {
        struct zd_index_document *doc = &search->documents[slot];
        doc->kind = kind;
        doc->deleted = 0;
        zd_str_copy(doc->key, sizeof(doc->key), key);
        zd_str_copy(doc->label, sizeof(doc->label),
                    label && *label ? label : key);
        ++search->index_stats.events_applied;
    }
    emit_index_event(search, kind, key);
    return 0;
}

int zd_search_index_remove(struct zd_search *search, const char *key) {
    int slot;
    if (!search || !key)
        return -ZD_EINVAL;
    slot = doc_slot(search, key);
    if (slot < 0)
        return -ZD_ENOENT;
    search->documents[slot].deleted = 1;
    search->documents[slot].tokenized = 0;
    ++search->index_stats.events_applied;
    emit_index_event(search, search->documents[slot].kind, key);
    return 0;
}

int zd_search_index_touch(struct zd_search *search, const char *key,
                          uint64_t recency_score) {
    int slot;
    if (!search || !key)
        return -ZD_EINVAL;
    slot = doc_slot(search, key);
    if (slot < 0)
        return -ZD_ENOENT;
    search->documents[slot].recency_score = recency_score;
    search->documents[slot].tokenized = 0;
    ++search->index_stats.events_applied;
    return 0;
}

int zd_search_index_step(struct zd_search *search, uint32_t budget_documents) {
    uint32_t processed = 0;
    if (!search)
        return -ZD_EINVAL;
    if (budget_documents == 0)
        budget_documents = 1;
    if (search->index_stats.paused)
        return 0;
    if (search->index_stats.cancel_generation != search->index_stats.query_generation) {
        /* Cancellation: reset cursor; caller re-arms via zd_search_index_cancel. */
        search->index_stats.cursor = 0;
        search->index_stats.cancel_generation = search->index_stats.query_generation;
        return 0;
    }
    while (processed < budget_documents &&
           search->index_stats.cursor < ZD_SEARCH_INDEX_CAPACITY) {
        struct zd_index_document *doc =
            &search->documents[search->index_stats.cursor];
        if (doc->in_use && !doc->tokenized) {
            if (doc->deleted) {
                /* Reap tombstones incrementally. */
                doc->in_use = 0;
                if (search->document_count)
                    --search->document_count;
            } else {
                /* Tokenization hook: label/key already normalized at
                 * upsert; mark processed so slices are idempotent and
                 * bounded. Real text extraction hooks in here when the
                 * VFS document provider lands. */
                doc->tokenized = 1;
                ++search->index_stats.docs_tokenized;
            }
            ++processed;
        }
        ++search->index_stats.cursor;
    }
    ++search->index_stats.slices_run;
    if (search->index_stats.cursor >= ZD_SEARCH_INDEX_CAPACITY) {
        search->index_stats.cursor = 0;
        return 0; /* caught up; waits for next event (no polling loop) */
    }
    return 1;
}

void zd_search_index_set_paused(struct zd_search *search, uint32_t paused) {
    if (search)
        search->index_stats.paused = paused ? 1U : 0U;
}

uint32_t zd_search_index_cancel(struct zd_search *search) {
    if (!search)
        return 0;
    ++search->index_stats.query_generation;
    search->index_stats.cursor = 0;
    return search->index_stats.query_generation;
}

/* Ranking --------------------------------------------------------------- */

static uint32_t token_match_score(const struct zd_intent *intent,
                                  const char *text) {
    uint32_t token_index;
    uint32_t best = 0;
    for (token_index = 0; token_index < intent->token_count; ++token_index) {
        const char *token = intent->tokens[token_index];
        uint32_t score = 0;
        size_t token_length = zd_str_length(token);
        size_t text_length = zd_str_length(text);
        size_t inner;
        if (!zd_str_length(token))
            continue;
        if (zd_str_equal_ci(text, token))
            score = 1000;
        else {
            for (inner = 0; text[inner]; ++inner) {
                uint32_t match = 1;
                size_t offset;
                for (offset = 0; offset < token_length; ++offset) {
                    if (zd_lower_ascii(text[inner + offset]) !=
                        zd_lower_ascii(token[offset])) {
                        match = 0;
                        break;
                    }
                    if (!text[inner + offset] && offset < token_length) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    /* Prefix of the text scores higher than a deep match. */
                    score = inner == 0 ? 700U : (inner < 8 ? 500U : 300U);
                    break;
                }
            }
            if (!score && zd_str_contains_ci(text, token))
                score = 300;
        }
        if (score && token_length >= text_length && score < 1000)
            score = 1000;
        if (score > best)
            best = score;
    }
    return best;
}

static uint32_t recency_bucket(uint64_t recency_score) {
    if (recency_score == 0)
        return 0;
    if (recency_score > 1000)
        return 150;
    if (recency_score > 100)
        return 100;
    return 50;
}

uint32_t zd_search_score(const struct zd_intent *intent,
                         const struct zd_search_result *result) {
    uint32_t score;
    if (!intent || !result)
        return 0;
    score = token_match_score(intent, result->label);
    if (!score)
        score = token_match_score(intent, result->path) / 2U;
    if (!score)
        return 0;
    if (intent->kind != ZD_SEARCH_ANY && result->kind == intent->kind)
        score += 250;
    else if (intent->kind != ZD_SEARCH_ANY && result->kind != intent->kind)
        score = score / 2U; /* cross-kind results rank below */
    score += recency_bucket(result->recency_score);
    score += zd_min_u32(result->use_count, 16) * 5U;
    score += zd_min_u32(result->provider_priority, 8) * 10U;
    return score;
}

static void sort_results(struct zd_search_result *results, uint32_t count) {
    uint32_t index;
    for (index = 1; index < count; ++index) {
        struct zd_search_result key = results[index];
        uint32_t inner = index;
        while (inner > 0 &&
               (results[inner - 1].score < key.score ||
                (results[inner - 1].score == key.score &&
                 results[inner - 1].document_id > key.document_id))) {
            results[inner] = results[inner - 1];
            --inner;
        }
        results[inner] = key;
    }
}

uint32_t zd_search_rank_merge(const struct zd_intent *intent,
                              struct zd_search_result *results,
                              uint32_t count) {
    uint32_t index;
    uint32_t write = 0;
    if (!intent || !results || count == 0)
        return 0;
    for (index = 0; index < count; ++index)
        results[index].score = zd_search_score(intent, &results[index]);
    /* Drop zero-score rows, dedupe by document id keeping best score. */
    for (index = 0; index < count; ++index) {
        uint32_t probe;
        int duplicate = 0;
        if (results[index].score == 0)
            continue;
        for (probe = 0; probe < index; ++probe)
            if (results[probe].document_id == results[index].document_id &&
                results[probe].score != 0) {
                if (results[index].score > results[probe].score)
                    results[probe].score = results[index].score;
                duplicate = 1;
                break;
            }
        if (!duplicate)
            results[write++] = results[index];
    }
    count = write;
    /* Re-sort after score updates. */
    {
        uint32_t a;
        for (a = 0; a < count; ++a) {
            uint32_t b;
            for (b = a + 1; b < count; ++b)
                if (results[b].score > results[a].score ||
                    (results[b].score == results[a].score &&
                     results[b].document_id < results[a].document_id)) {
                    struct zd_search_result tmp = results[a];
                    results[a] = results[b];
                    results[b] = tmp;
                }
        }
    }
    (void)sort_results;
    return write; /* compacted live-row count after filtering/dedupe */
}

/* Providers ------------------------------------------------------------- */

int zd_search_add_provider(struct zd_search *search,
                           const struct zd_search_provider *provider) {
    if (!search || !provider || !provider->name || !provider->query)
        return -ZD_EINVAL;
    if (search->provider_count >= ZD_SEARCH_MAX_PROVIDERS)
        return -ZD_ENOSPC;
    search->providers[search->provider_count] = *provider;
    search->providers[search->provider_count].registered = 1;
    ++search->provider_count;
    return 0;
}

int zd_search_add_index_listener(struct zd_search *search,
                                 zd_search_index_event_fn callback,
                                 void *context) {
    if (!search || !callback)
        return -ZD_EINVAL;
    if (search->listener_count >= ZD_SEARCH_MAX_LISTENERS)
        return -ZD_ENOSPC;
    search->listeners[search->listener_count].callback = callback;
    search->listeners[search->listener_count].context = context;
    ++search->listener_count;
    return 0;
}

static int kind_selected(const struct zd_search_provider *provider,
                         enum zd_search_kind kind) {
    if (provider->kind_mask == 0)
        return 1;
    if (kind == ZD_SEARCH_ANY)
        return provider->kind_mask != 0;
    return (provider->kind_mask & (1U << (uint32_t)kind)) != 0;
}

static int index_provider_query(void *context, const struct zd_intent *intent,
                                struct zd_search_result *results,
                                uint32_t capacity,
                                volatile uint32_t *cancel_token,
                                uint32_t query_generation) {
    struct zd_search *search = (struct zd_search *)context;
    uint32_t index;
    uint32_t count = 0;
    if (!search || !intent || !results || capacity == 0)
        return -ZD_EINVAL;
    for (index = 0; index < ZD_SEARCH_INDEX_CAPACITY && count < capacity;
         ++index) {
        const struct zd_index_document *doc = &search->documents[index];
        struct zd_search_result *row;
        static const enum zd_index_doc_kind kind_map[] = {
            ZD_INDEX_APP, ZD_INDEX_FILE, ZD_INDEX_FOLDER, ZD_INDEX_SETTING,
            ZD_INDEX_DOCUMENT, ZD_INDEX_COMMAND, ZD_INDEX_DIAGNOSTIC,
            ZD_INDEX_AI_ACTION
        };
        (void)kind_map;
        if (!doc->in_use || doc->deleted)
            continue;
        if (cancel_token && *cancel_token != query_generation)
            return -ZD_ECANCELED;
        row = &results[count];
        zd_memset(row, 0, sizeof(*row));
        row->document_id = doc->id;
        row->kind = (enum zd_search_kind)((int)doc->kind + 1);
        /* Map index doc kinds onto search kinds: APP->APP(1) etc. */
        switch (doc->kind) {
        case ZD_INDEX_APP: row->kind = ZD_SEARCH_APP; break;
        case ZD_INDEX_FILE: row->kind = ZD_SEARCH_FILE; break;
        case ZD_INDEX_FOLDER: row->kind = ZD_SEARCH_FOLDER; break;
        case ZD_INDEX_SETTING: row->kind = ZD_SEARCH_SETTING; break;
        case ZD_INDEX_DOCUMENT: row->kind = ZD_SEARCH_DOCUMENT; break;
        case ZD_INDEX_COMMAND: row->kind = ZD_SEARCH_COMMAND; break;
        case ZD_INDEX_DIAGNOSTIC: row->kind = ZD_SEARCH_DIAGNOSTIC; break;
        case ZD_INDEX_AI_ACTION: row->kind = ZD_SEARCH_AI_ACTION; break;
        default: row->kind = ZD_SEARCH_FILE; break;
        }
        zd_str_copy(row->label, sizeof(row->label), doc->label);
        zd_str_copy(row->path, sizeof(row->path), doc->key);
        row->recency_score = doc->recency_score;
        row->use_count = doc->use_count;
        row->available = 1;
        ++count;
    }
    return (int)count;
}

void zd_search_register_index_provider(struct zd_search *search) {
    struct zd_search_provider provider;
    if (!search)
        return;
    zd_memset(&provider, 0, sizeof(provider));
    provider.name = "index";
    provider.kind_mask = 0;
    provider.priority = 4;
    provider.query = index_provider_query;
    provider.context = search;
    (void)zd_search_add_provider(search, &provider);
}

void zd_search_set_live_apps(struct zd_search *search,
                             const struct zd_window *const *windows,
                             uint32_t count) {
    if (!search)
        return;
    search->app_windows = windows;
    search->app_window_count = count;
}

int zd_search_query(struct zd_search *search, const char *input,
                    struct zd_search_result *results_out, uint32_t capacity,
                    uint32_t *out_count) {
    struct zd_intent intent;
    struct zd_search_result scratch[ZD_SEARCH_MAX_RESULTS];
    uint32_t total = 0;
    uint32_t provider_index;
    uint32_t query_generation;
    int parse_result;

    if (!search || !results_out || capacity == 0)
        return -ZD_EINVAL;
    parse_result = zd_search_parse(input, &intent);
    if (parse_result != 0)
        return parse_result;

    ++search->stats.queries;
    ++search->index_stats.queries;
    ++search->index_stats.query_generation; /* cancels older in-flight work */
    query_generation = search->index_stats.query_generation;
    /* Arm the cancel token to this query's generation: providers treat a
     * token mismatch as a cancellation, so it must match while this query
     * runs and diverge afterwards. */
    search->index_stats.cancel_generation = query_generation;

    for (provider_index = 0; provider_index < search->provider_count;
         ++provider_index) {
        const struct zd_search_provider *provider =
            &search->providers[provider_index];
        uint32_t available = 1;
        int produced;
        uint32_t index;

        if (!kind_selected(provider, intent.kind))
            continue;
        if (provider->available) {
            available = provider->available(provider->context);
            if (!available) {
                ++search->stats.provider_unavailable;
                continue; /* explicit unavailability, never fake results */
            }
        }
        if (total >= ZD_SEARCH_MAX_RESULTS)
            break;
        produced = provider->query(provider->context, &intent,
                                   &scratch[total],
                                   ZD_SEARCH_MAX_RESULTS - total,
                                   &search->index_stats.cancel_generation,
                                   query_generation);
        if (produced == -ZD_ECANCELED) {
            ++search->stats.stale_drops;
            continue;
        }
        if (produced < 0)
            continue;
        for (index = 0; index < (uint32_t)produced && total < ZD_SEARCH_MAX_RESULTS;
             ++index) {
            scratch[total].provider_priority = provider->priority;
            if (!scratch[total].available)
                ++search->stats.provider_unavailable;
            ++total;
        }
    }

    total = zd_search_rank_merge(&intent, scratch, total);
    {
        uint32_t write_count = total < capacity ? total : capacity;
        uint32_t index;
        for (index = 0; index < write_count; ++index)
            results_out[index] = scratch[index];
        search->stats.results_returned += write_count;
        if (out_count)
            *out_count = write_count;
    }
    /* In-flight work carrying the previous generation is now stale. */
    ++search->index_stats.cancel_generation;
    return 0;
}
