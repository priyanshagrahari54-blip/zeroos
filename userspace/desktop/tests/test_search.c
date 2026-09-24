#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static struct zd_search search;

static uint32_t provider_available_yes(void *context) {
    (void)context;
    return 1;
}

static uint32_t provider_available_no(void *context) {
    (void)context;
    return 0;
}

static int provider_dead(void *context, const struct zd_intent *intent,
                         struct zd_search_result *results,
                         uint32_t capacity, volatile uint32_t *cancel_token,
                         uint32_t query_generation) {
    (void)context;
    (void)intent;
    (void)results;
    (void)capacity;
    (void)cancel_token;
    (void)query_generation;
    return -ZD_ENODEV;
}

static int provider_unavailable_called(void *context,
                                       const struct zd_intent *intent,
                                       struct zd_search_result *results,
                                       uint32_t capacity,
                                       volatile uint32_t *cancel_token,
                                       uint32_t query_generation) {
    (void)context;
    (void)intent;
    (void)results;
    (void)capacity;
    (void)cancel_token;
    (void)query_generation;
    return -ZD_EPERM; /* must never be reached when availability=0 */
}

static void test_parser(void) {
    struct zd_intent intent;
    ZD_CHECK_OK(zd_search_parse("terminal", &intent));
    ZD_CHECK_EQ(intent.kind, ZD_SEARCH_ANY);
    ZD_CHECK_EQ(intent.token_count, 1U);
    ZD_CHECK(strcmp(intent.tokens[0], "terminal") == 0);

    ZD_CHECK_OK(zd_search_parse("app: files", &intent));
    ZD_CHECK_EQ(intent.kind, ZD_SEARCH_APP);
    ZD_CHECK_EQ(intent.token_count, 1U);
    ZD_CHECK(strcmp(intent.tokens[0], "files") == 0);

    ZD_CHECK_OK(zd_search_parse("set: ui scale", &intent));
    ZD_CHECK_EQ(intent.kind, ZD_SEARCH_SETTING);
    ZD_CHECK_EQ(intent.token_count, 2U);

    ZD_CHECK_OK(zd_search_parse("file: \"my report.pdf\"", &intent));
    ZD_CHECK_EQ(intent.kind, ZD_SEARCH_FILE);
    ZD_CHECK_EQ(intent.token_count, 1U);
    ZD_CHECK(strcmp(intent.tokens[0], "my report.pdf") == 0);
    ZD_CHECK_EQ(intent.quoted[0], 1U);

    ZD_CHECK_OK(zd_search_parse("diag: batt", &intent));
    ZD_CHECK_EQ(intent.kind, ZD_SEARCH_DIAGNOSTIC);

    /* Negative: empty, unterminated quote, too many tokens. */
    ZD_CHECK_ERR(zd_search_parse("", &intent), ZD_EINVAL);
    ZD_CHECK_ERR(zd_search_parse("   ", &intent), ZD_EINVAL);
    ZD_CHECK_ERR(zd_search_parse("\"unterminated", &intent), ZD_EINVAL);
    ZD_CHECK_ERR(zd_search_parse("a b c d e f g h i j k", &intent),
                 ZD_EOVERFLOW);
    ZD_CHECK_ERR(zd_search_parse(0, &intent), ZD_EINVAL);
}

static void test_index_incremental(void) {
    uint32_t steps;
    uint32_t before;
    zd_search_init(&search);
    ZD_CHECK_OK(zd_search_index_upsert(&search, ZD_INDEX_APP, "app:terminal",
                                       "Terminal"));
    ZD_CHECK_OK(zd_search_index_upsert(&search, ZD_INDEX_FILE,
                                       "file:report.pdf", "report.pdf"));
    ZD_CHECK_OK(zd_search_index_upsert(&search, ZD_INDEX_SETTING,
                                       "set:ui.scale_percent", "UI scale"));
    ZD_CHECK_EQ(search.document_count, 3U);
    ZD_CHECK_EQ(search.index_stats.seeded, 3U);
    /* Duplicate key upserts in place. */
    ZD_CHECK_OK(zd_search_index_upsert(&search, ZD_INDEX_FILE,
                                       "file:report.pdf", "report v2"));
    ZD_CHECK_EQ(search.document_count, 3U);
    /* Incremental slices: bounded work per call. */
    before = search.index_stats.docs_tokenized;
    steps = 0;
    while (zd_search_index_step(&search, 1) == 1 && steps < 100U)
        ++steps;
    ZD_CHECK_EQ(search.index_stats.docs_tokenized - before, 3U);
    /* Caught up returns 0; full scans remain zero. */
    ZD_CHECK_EQ(zd_search_index_step(&search, 8), 0);
    ZD_CHECK_EQ(search.index_stats.full_scans, 0U);
    /* Churn: 5000 events never trigger a full scan and stay bounded. */
    {
        uint32_t index;
        uint64_t expected_events = 0;
        char key[32];
        for (index = 0; index < 5000U; ++index) {
            snprintf(key, sizeof(key), "file:hot/%u", index % 200U);
            if (index % 7U == 0) {
                if (zd_search_index_remove(&search, key) == 0)
                    ++expected_events;
            } else {
                if (zd_search_index_upsert(&search, ZD_INDEX_FILE, key,
                                           key) == 0)
                    ++expected_events;
            }
        }
        ZD_CHECK_EQ(search.index_stats.full_scans, 0U);
        ZD_CHECK(search.document_count <= ZD_SEARCH_INDEX_CAPACITY);
        ZD_CHECK(expected_events >= 4000U);
        ZD_CHECK(search.index_stats.events_applied >= expected_events);
    }
    /* Paused indexer performs no work (resource-aware). */
    zd_search_index_set_paused(&search, 1);
    before = search.index_stats.docs_tokenized;
    ZD_CHECK_EQ(zd_search_index_step(&search, 16), 0);
    ZD_CHECK_EQ(search.index_stats.docs_tokenized, before);
    zd_search_index_set_paused(&search, 0);
    /* Cancellation bumps the generation. */
    {
        uint32_t generation = zd_search_index_cancel(&search);
        ZD_CHECK(generation > 0);
        ZD_CHECK_EQ(zd_search_index_cancel(&search), generation + 1U);
    }
    /* Remove unknown key -> ENOENT; upsert empty key -> EINVAL. */
    ZD_CHECK_ERR(zd_search_index_remove(&search, "file:missing"),
                 ZD_ENOENT);
    ZD_CHECK_ERR(zd_search_index_upsert(&search, ZD_INDEX_FILE, "", "x"),
                 ZD_EINVAL);
}

static void test_query_pipeline(void) {
    struct zd_search_result results[ZD_SEARCH_MAX_RESULTS];
    uint32_t count = 0;
    struct zd_search_provider dead;
    struct zd_search_provider unavailable;

    zd_search_init(&search);
    ZD_CHECK_OK(zd_search_index_upsert(&search, ZD_INDEX_APP, "terminal",
                                       "Terminal"));
    ZD_CHECK_OK(zd_search_index_upsert(&search, ZD_INDEX_APP, "files",
                                       "Files"));
    ZD_CHECK_OK(zd_search_index_upsert(&search, ZD_INDEX_SETTING,
                                       "ui.scale_percent",
                                       "ui.scale_percent"));

    memset(&dead, 0, sizeof(dead));
    dead.name = "dead";
    dead.available = provider_available_yes;
    dead.query = provider_dead;
    ZD_CHECK_OK(zd_search_add_provider(&search, &dead));

    memset(&unavailable, 0, sizeof(unavailable));
    unavailable.name = "cloud";
    unavailable.available = provider_available_no;
    unavailable.query = provider_unavailable_called;
    ZD_CHECK_OK(zd_search_add_provider(&search, &unavailable));

    /* Basic query ranks exact label matches at the top. */
    ZD_CHECK_OK(zd_search_query(&search, "terminal", results, 16, &count));
    ZD_CHECK(count >= 1U);
    ZD_CHECK(strcmp(results[0].label, "Terminal") == 0);
    /* Dead provider didn't break the pipeline (index provider still ran). */
    ZD_CHECK(search.stats.queries >= 1U);

    /* Kind filter: setting: prefix prefers the settings document. */
    ZD_CHECK_OK(zd_search_query(&search, "set: scale", results, 16, &count));
    ZD_CHECK(count >= 1U);
    ZD_CHECK_EQ(results[0].kind, ZD_SEARCH_SETTING);

    /* Unavailable provider counted, never invoked (its query returns
     * EPERM; if invoked, provider_unavailable would grow differently). */
    {
        uint64_t before = search.stats.provider_unavailable;
        ZD_CHECK_OK(zd_search_query(&search, "anything", results, 16,
                                    &count));
        ZD_CHECK(search.stats.provider_unavailable > before);
    }

    /* Dead provider failure is isolated: results still returned. */
    ZD_CHECK_OK(zd_search_query(&search, "files", results, 16, &count));
    ZD_CHECK(count >= 1U);

    /* Invalid queries rejected. */
    ZD_CHECK_ERR(zd_search_query(&search, "", results, 16, &count),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_search_query(&search, "x", 0, 16, &count), ZD_EINVAL);

    /* Determinism: identical queries produce identical ordering. {
    } */
    {
        struct zd_search_result first[8];
        struct zd_search_result second[8];
        uint32_t first_count = 0;
        uint32_t second_count = 0;
        uint32_t index;
        ZD_CHECK_OK(zd_search_query(&search, "file:", first, 8, &first_count));
        ZD_CHECK_OK(zd_search_query(&search, "file:", second, 8,
                                    &second_count));
        ZD_CHECK_EQ(first_count, second_count);
        for (index = 0; index < first_count && index < second_count; ++index) {
            ZD_CHECK_EQ(first[index].document_id, second[index].document_id);
            ZD_CHECK_EQ(first[index].score, second[index].score);
        }
    }
}

static void test_ranking_semantics(void) {
    struct zd_intent intent;
    struct zd_search_result results[4];
    uint32_t score_exact;
    uint32_t score_partial;

    ZD_CHECK_OK(zd_search_parse("note", &intent));
    memset(results, 0, sizeof(results));
    strcpy(results[0].label, "note");
    results[0].kind = ZD_SEARCH_FILE;
    results[0].document_id = 1;
    score_exact = zd_search_score(&intent, &results[0]);

    memset(results, 0, sizeof(results));
    strcpy(results[0].label, "notes-archive.zip");
    results[0].kind = ZD_SEARCH_FILE;
    results[0].document_id = 2;
    score_partial = zd_search_score(&intent, &results[0]);
    ZD_CHECK(score_exact > score_partial);

    /* Recency and use frequency break ties upward. */
    memset(results, 0, sizeof(results));
    strcpy(results[0].label, "note");
    results[0].kind = ZD_SEARCH_FILE;
    results[0].document_id = 3;
    results[0].recency_score = 500;
    results[0].use_count = 10;
    ZD_CHECK(zd_search_score(&intent, &results[0]) >= score_exact);

    /* Cross-kind matches rank below same-kind for typed queries. The token
     * must actually match the labels so scores are > 0 (zero-score results
     * are dropped by rank), and the intent must carry a concrete kind so
     * the kind bonus/penalty applies. */
    ZD_CHECK_OK(zd_search_parse("app: a", &intent));
    memset(results, 0, sizeof(results));
    strcpy(results[0].label, "scale");
    results[0].kind = ZD_SEARCH_SETTING;
    results[0].document_id = 4;
    {
        uint32_t cross = zd_search_score(&intent, &results[0]);
        results[0].kind = ZD_SEARCH_APP;
        uint32_t same = zd_search_score(&intent, &results[0]);
        ZD_CHECK(same > cross);
    }

    /* Merge dedupes by document id, keeping the best score. */
    {
        struct zd_search_result merged[4];
        memset(merged, 0, sizeof(merged));
        strcpy(merged[0].label, "alpha");
        merged[0].document_id = 10;
        strcpy(merged[1].label, "alpha");
        merged[1].document_id = 10;
        merged[1].recency_score = 999;
        strcpy(merged[2].label, "beta");
        merged[2].document_id = 11;
        zd_search_rank_merge(&intent, merged, 3);
        ZD_CHECK_EQ(merged[0].document_id, 10U);
        ZD_CHECK(strcmp(merged[1].label, "beta") == 0);
    }
}

static void test_provider_registry_limits(void) {
    struct zd_search_provider provider;
    uint32_t index;
    uint32_t registered = 0;
    zd_search_init(&search);
    memset(&provider, 0, sizeof(provider));
    provider.name = "p";
    provider.query = provider_dead;
    for (index = 0; index < ZD_SEARCH_MAX_PROVIDERS + 3U; ++index) {
        static char names[ZD_SEARCH_MAX_PROVIDERS + 3][8];
        snprintf(names[index], sizeof(names[index]), "p%u", index);
        provider.name = names[index];
        if (zd_search_add_provider(&search, &provider) == 0)
            ++registered;
        else
            break;
    }
    /* init already registered "index", so capacity-1 more fit. */
    ZD_CHECK_EQ(registered, ZD_SEARCH_MAX_PROVIDERS - 1U);
    ZD_CHECK_ERR(zd_search_add_provider(&search, &provider), ZD_ENOSPC);
    ZD_CHECK_ERR(zd_search_add_provider(&search, 0), ZD_EINVAL);
}

void zd_test_search_suite(void) {
    printf(" suite: universal search\n");
    ZD_RUN(test_parser);
    ZD_RUN(test_index_incremental);
    ZD_RUN(test_query_pipeline);
    ZD_RUN(test_ranking_semantics);
    ZD_RUN(test_provider_registry_limits);
}
