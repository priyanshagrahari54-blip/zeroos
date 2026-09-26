/* Study notes + dictionary tests (part G) */
#include "test_harness.h"
#include <string.h>
#include <zeroos/desktop/desktop.h>

/* --- dictionary fixture source ------------------------------------ */
static int dict_src_ok(void *ctx, char (*words)[ZD_DICT_WORD],
                       uint32_t cap, uint32_t *out_n) {
    static const char *list[] = {"Zebra", "apple", "Banana",
                                 "apple", /* duplicate (case diff) */
                                 "apply", "band", "zebra"};
    uint32_t i, n = 0;
    (void)ctx;
    for (i = 0; i < sizeof(list) / sizeof(list[0]) && n < cap; ++i) {
        memcpy(words[n], list[i], strlen(list[i]) + 1);
        n++;
    }
    *out_n = n;
    return 0;
}

static int dict_src_err(void *ctx, char (*words)[ZD_DICT_WORD],
                        uint32_t cap, uint32_t *out_n) {
    (void)ctx;
    (void)words;
    (void)cap;
    *out_n = 0;
    return -5;
}

void zd_test_notes_dict_suite(void) {
    struct zd_notes notes;
    struct zd_dict dict;
    uint32_t id1 = 0, id2 = 0, id3 = 0;
    uint32_t ids[8];
    uint32_t out_n = 0;
    static char sugg[8][ZD_DICT_WORD];

    /* ---------------- notes ---------------- */
    zd_notes_init(&notes);
    ZD_CHECK_EQ(zd_notes_create(0, "t", "b", 0, &id1), -22);
    ZD_CHECK_EQ(zd_notes_create(&notes, "", "b", 0, &id1), -22);
    ZD_CHECK_EQ(notes.stats.rejected, 1);
    ZD_CHECK_OK(zd_notes_create(&notes, "Lecture 3", "Quantum basics",
                                100, &id1));
    ZD_CHECK_OK(zd_notes_create(&notes, "Shopping", "milk, bread",
                                200, &id2));
    ZD_CHECK_OK(zd_notes_create(&notes, "Ideas", "quantum LEAP idea",
                                300, &id3));
    ZD_CHECK_EQ(notes.stats.created, 3u);
    ZD_CHECK_EQ(id3, id2 + 1);

    /* update */
    ZD_CHECK_EQ(zd_notes_update(&notes, id1, 0, "Quantum advanced", 400),
                0);
    ZD_CHECK_EQ(zd_notes_get(&notes, id1)->body_len,
                (uint32_t)strlen("Quantum advanced"));
    ZD_CHECK_EQ(zd_notes_update(&notes, 999, 0, "x", 0), -2);
    /* overlong body rejected */
    {
        char big[ZD_NOTES_BODY + 8];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        ZD_CHECK_EQ(zd_notes_update(&notes, id1, 0, big, 0), -22);
        ZD_CHECK_EQ(notes.stats.rejected, 2);
    }

    /* search: case-insensitive, title+body */
    ZD_CHECK_EQ(zd_notes_search(&notes, "QUANTUM", ids, 8), 2);
    ZD_CHECK_EQ(ids[0], id1);
    ZD_CHECK_EQ(ids[1], id3);
    ZD_CHECK_EQ(zd_notes_search(&notes, "milk", 0, 0), 1);
    ZD_CHECK_EQ(zd_notes_search(&notes, "nothing", 0, 0), 0);
    ZD_CHECK_EQ(zd_notes_search(&notes, "", ids, 8), 3); /* all */
    /* cap bound: cap=1 returns 1 though 2 match */
    ZD_CHECK_EQ(zd_notes_search(&notes, "quantum", ids, 1), 1);
    ZD_CHECK_EQ(ids[0], id1);
    ZD_CHECK_EQ(zd_notes_search(&notes, 0, 0, 0), -22);

    /* pin */
    ZD_CHECK_OK(zd_notes_set_pinned(&notes, id2, 1));
    ZD_CHECK_EQ(zd_notes_get(&notes, id2)->pinned, 1);
    ZD_CHECK_OK(zd_notes_set_pinned(&notes, id2, 1)); /* idempotent */
    ZD_CHECK_EQ(notes.stats.pinned, 1u);
    ZD_CHECK_OK(zd_notes_set_pinned(&notes, id2, 0));
    ZD_CHECK_EQ(notes.stats.unpinned, 1u);
    ZD_CHECK_EQ(zd_notes_set_pinned(&notes, 999, 1), -2);

    /* delete + capacity */
    ZD_CHECK_OK(zd_notes_delete(&notes, id3));
    ZD_CHECK_EQ(zd_notes_delete(&notes, id3), -2);
    ZD_CHECK_EQ(notes.count, 2u);
    {
        uint32_t tmp, i;
        for (i = notes.count; i < ZD_NOTES_MAX; ++i) {
            char t[8] = "n000";
            t[1] = (char)('0' + (i / 100) % 10);
            t[2] = (char)('0' + (i / 10) % 10);
            t[3] = (char)('0' + i % 10);
            ZD_CHECK_OK(zd_notes_create(&notes, t, "b", 0, &tmp));
        }
        ZD_CHECK_EQ(zd_notes_create(&notes, "full", "b", 0, &tmp), -28);
        ZD_CHECK_EQ(notes.count, (uint32_t)ZD_NOTES_MAX);
    }

    /* ---------------- dictionary ---------------- */
    memset(&dict, 0, sizeof(dict));
    /* gate before load */
    ZD_CHECK_EQ(zd_dict_lookup(&dict, "apple"), -95);
    ZD_CHECK_EQ(zd_dict_prefix(&dict, "ap", sugg, 8, &out_n), -95);
    /* bad args */
    ZD_CHECK_EQ(zd_dict_load(&dict, 0, 0), -22);
    ZD_CHECK_EQ(zd_dict_lookup(&dict, ""), -22);
    /* source error */
    ZD_CHECK_EQ(zd_dict_load(&dict, dict_src_err, 0), -5);
    ZD_CHECK_EQ(dict.stats.load_errors, 1u);
    ZD_CHECK_EQ(zd_dict_lookup(&dict, "apple"), -95);
    /* real load: 7 raw -> dedup "apple"/"Apple" case-differently...
     * list has apple+apple (dup), Zebra+zebra (dup) -> 5 unique */
    ZD_CHECK_OK(zd_dict_load(&dict, dict_src_ok, 0));
    ZD_CHECK_EQ(dict.count, 5u); /* apple, apply, band, banana, zebra */
    ZD_CHECK_EQ(dict.stats.loads, 1u);
    /* case-insensitive exact lookup */
    ZD_CHECK_EQ(zd_dict_lookup(&dict, "APPLE"), 1);
    ZD_CHECK_EQ(zd_dict_lookup(&dict, "Zebra"), 1);
    ZD_CHECK_EQ(zd_dict_lookup(&dict, "app"), 0);
    ZD_CHECK_EQ(dict.stats.lookup_hits, 2u);
    /* sorted case-insensitive byte-order with first-seen spelling */
    ZD_CHECK(strcmp(dict.words[0], "apple") == 0);
    ZD_CHECK(strcmp(dict.words[1], "apply") == 0);
    ZD_CHECK(strcmp(dict.words[2], "Banana") == 0);
    ZD_CHECK(strcmp(dict.words[3], "band") == 0);
    ZD_CHECK(strcmp(dict.words[4], "Zebra") == 0);
    /* prefix suggestions */
    ZD_CHECK_EQ(zd_dict_prefix(&dict, "AP", sugg, 8, &out_n), 2);
    ZD_CHECK_EQ(out_n, 2u);
    ZD_CHECK(strcmp(sugg[0], "apple") == 0);
    ZD_CHECK(strcmp(sugg[1], "apply") == 0);
    ZD_CHECK_EQ(zd_dict_prefix(&dict, "ba", sugg, 8, &out_n), 2);
    /* case-insensitive order: banana < band ('a' < 'd') */
    ZD_CHECK(strcmp(sugg[0], "Banana") == 0);
    ZD_CHECK(strcmp(sugg[1], "band") == 0);
    /* cap truncation: total reported, only cap written */
    ZD_CHECK_EQ(zd_dict_prefix(&dict, "b", sugg, 1, &out_n), 2);
    ZD_CHECK_EQ(out_n, 1u);
    /* no match */
    ZD_CHECK_EQ(zd_dict_prefix(&dict, "xyz", sugg, 8, &out_n), 0);
    ZD_CHECK_EQ(out_n, 0u);
    /* empty prefix matches everything (documented) */
    ZD_CHECK_EQ(zd_dict_prefix(&dict, "", sugg, 8, &out_n), 5);
    ZD_CHECK_EQ(out_n, 8u > 5 ? 5 : 8u);
    ZD_CHECK_EQ(dict.stats.prefix_queries, 5u);
}
