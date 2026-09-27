/* Media policy (part H) + gaming core (part I) tests */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

static void test_media(void) {
    struct zd_media m;
    uint32_t id_a = 0, id_drm = 0, i;

    zd_media_init(&m);
    /* defaults: everything denied */
    ZD_CHECK_OK(zd_media_add_item(&m, "https://unregistered", "raw", 0));
    id_a = 1;
    ZD_CHECK_EQ(zd_media_play(&m, id_a), -1);
    ZD_CHECK_EQ(m.stats.refusals_origin, 1);
    ZD_CHECK_EQ(m.stats.plays, 0);

    /* register with PLAY+CACHE */
    ZD_CHECK_OK(zd_media_register_source(&m, "https://lawful",
                                          ZD_MEDIA_RIGHT_PLAY |
                                          ZD_MEDIA_RIGHT_CACHE));
    ZD_CHECK_EQ(m.stats.registered, 1);
    /* replace rights is idempotent */
    ZD_CHECK_OK(zd_media_register_source(&m, "https://lawful",
                                          ZD_MEDIA_RIGHT_PLAY));
    ZD_CHECK_EQ(m.stats.registered, 1);

    /* validation */
    ZD_CHECK_EQ(zd_media_register_source(&m, "", 1), -22);
    ZD_CHECK_EQ(zd_media_register_source(&m, "https://x",
                                          1u << 9), -22);
    ZD_CHECK_EQ(m.stats.rejected, 2);

    /* items */
    ZD_CHECK_OK(zd_media_add_item(&m, "https://lawful", "track", 0));
    ZD_CHECK_OK(zd_media_add_item(&m, "https://lawful", "movie", 1));
    id_a = 2;
    id_drm = 3;
    ZD_CHECK_EQ(zd_media_add_item(&m, "https://lawful", "", 0), -22);
    ZD_CHECK_EQ(zd_media_add_item(NULL, "o", "t", 0), -22);

    /* play: lawful non-drm OK */
    ZD_CHECK_EQ(zd_media_play(&m, id_a), 0);
    ZD_CHECK_EQ(m.stats.plays, 1);
    /* DRM: refused, NEVER bypassed — even with full rights */
    ZD_CHECK_EQ(zd_media_register_source(&m, "https://lawful",
                                          ZD_MEDIA_RIGHT_ALL), 0);
    ZD_CHECK_EQ(zd_media_play(&m, id_drm), -1);
    ZD_CHECK_EQ(m.stats.refusals_drm, 1);
    ZD_CHECK_EQ(m.stats.plays, 1);
    /* and request(CACHE) on drm item: rights pass, no play involved */
    ZD_CHECK_EQ(zd_media_request(&m, id_drm, ZD_MEDIA_RIGHT_CACHE), 0);
    /* lawful has ALL rights now: export passes */
    ZD_CHECK_EQ(zd_media_request(&m, id_a, ZD_MEDIA_RIGHT_EXPORT), 0);
    /* a PLAY-only source still refuses CACHE */
    ZD_CHECK_OK(zd_media_register_source(&m, "https://limited",
                                          ZD_MEDIA_RIGHT_PLAY));
    ZD_CHECK_OK(zd_media_add_item(&m, "https://limited", "lim", 0));
    ZD_CHECK_EQ(zd_media_request(&m, 4, ZD_MEDIA_RIGHT_CACHE), -1);
    ZD_CHECK_EQ(m.stats.refusals_rights, 1);
    /* exactly one right bit per request */
    ZD_CHECK_EQ(zd_media_request(&m, id_a, 0), -22);
    ZD_CHECK_EQ(zd_media_request(&m, id_a,
                                 ZD_MEDIA_RIGHT_PLAY |
                                 ZD_MEDIA_RIGHT_CACHE), -22);
    /* unknown item / id */
    ZD_CHECK_EQ(zd_media_play(&m, 99), -2);
    ZD_CHECK_EQ(zd_media_play(&m, 0), -2);

    /* unregister: plays go back to origin-denied */
    ZD_CHECK_OK(zd_media_unregister_source(&m, "https://lawful"));
    ZD_CHECK_EQ(zd_media_play(&m, id_a), -1);
    ZD_CHECK_EQ(m.stats.refusals_origin, 2);
    ZD_CHECK_EQ(zd_media_unregister_source(&m, "https://lawful"), -2);

    /* capacity */
    for (i = 0; i < ZD_MEDIA_SOURCES + 2; ++i) {
        char o[40];
        uint32_t k = 0, n = i;
        o[0] = 's'; o[1] = '0' + (char)(n % 10); o[2] = 0;
        (void)k;
        if (i == 0)
            continue; /* counts as first free slot */
        if (zd_media_register_source(&m, o, 1) == -28)
            break;
    }
    /* force to full (unique names: updates would loop forever) */
    {
        uint32_t n = 0;
        int r = 0;
        while (r == 0 && n < 32) {
            char o[16];
            o[0] = 'o'; o[1] = (char)('0' + (n % 10)); o[2] = 0;
            r = zd_media_register_source(&m, o, 1);
            ++n;
        }
        ZD_CHECK_EQ(r, -28);
    }
    ZD_CHECK_EQ(zd_media_register_source(&m, "https://overflow", 1),
                -28);
    ZD_CHECK_EQ(m.source_count, ZD_MEDIA_SOURCES);
}

static void test_gaming(void) {
    struct zd_gaming g;
    struct zd_game_profile *p;

    zd_gaming_init(&g);
    /* profile validation */
    ZD_CHECK_EQ(zd_gaming_profile_set(&g, "", 1, 60, 0), -22);
    ZD_CHECK_EQ(zd_gaming_profile_set(&g, "game", 3, 60, 0), -22);
    ZD_CHECK_EQ(zd_gaming_profile_set(&g, "game", 1, 0, 0), -22);
    ZD_CHECK_EQ(zd_gaming_profile_set(&g, "game", 1, 241, 0), -22);
    ZD_CHECK_EQ(zd_gaming_profile_set(&g, "game", 1, 60, 1u << 7), -22);
    ZD_CHECK_EQ(g.stats.rejected, 5);

    ZD_CHECK_OK(zd_gaming_profile_set(&g, "quake", ZD_GAME_PERF, 144,
                                      ZD_GAME_FLAG_OVERLAY |
                                      ZD_GAME_FLAG_LOW_LATENCY));
    ZD_CHECK_OK(zd_gaming_profile_set(&g, "chess", ZD_GAME_ECO, 30, 0));
    ZD_CHECK_EQ(g.count, 2);
    ZD_CHECK_EQ(g.stats.sets, 2);
    /* update in place */
    ZD_CHECK_OK(zd_gaming_profile_set(&g, "quake", ZD_GAME_BALANCED,
                                      120, ZD_GAME_FLAG_OVERLAY));
    ZD_CHECK_EQ(g.count, 2);
    ZD_CHECK_EQ(g.stats.sets, 3);

    p = zd_gaming_profile(&g, "quake");
    ZD_CHECK(p != NULL);
    ZD_CHECK_EQ(p->mode, ZD_GAME_BALANCED);
    ZD_CHECK_EQ(p->target_fps, 120);
    ZD_CHECK(zd_gaming_profile(&g, "nope") == NULL);

    /* remapping */
    ZD_CHECK_EQ(zd_gaming_remap(&g, "quake", 0, 5), 0);
    ZD_CHECK_EQ(zd_gaming_remap(&g, "quake", 32, 0), -22);
    ZD_CHECK_EQ(zd_gaming_remap(&g, "quake", 1, 16), -22);
    ZD_CHECK_EQ(zd_gaming_remap(&g, "nope", 1, 1), -2);
    ZD_CHECK_EQ(g.stats.rejected, 7);
    ZD_CHECK_EQ(zd_gaming_lookup(&g, "quake", 0), 5);
    ZD_CHECK_EQ(zd_gaming_lookup(&g, "quake", 1), -2);
    ZD_CHECK_EQ(g.stats.unknown_buttons, 1);

    /* cooperative matrix */
    /* healthy: nothing */
    ZD_CHECK_EQ(zd_gaming_cooperative(&g, "quake", 144000, 30),
                ZD_GAME_YIELD_NONE);
    /* high pressure alone with overlay -> OVERLAY_OFF */
    ZD_CHECK_EQ(zd_gaming_cooperative(&g, "quake", 144000, 85),
                ZD_GAME_YIELD_OVERLAY_OFF);
    /* high pressure + slow measured fps -> DEGRADE */
    ZD_CHECK_EQ(zd_gaming_cooperative(&g, "quake", 24000, 85),
                ZD_GAME_YIELD_DEGRADE);
    /* slow alone with overlay -> OVERLAY_OFF */
    ZD_CHECK_EQ(zd_gaming_cooperative(&g, "quake", 24000, 30),
                ZD_GAME_YIELD_OVERLAY_OFF);
    /* chess: no overlay; pressure alone -> TARGET_FLOOR */
    ZD_CHECK_EQ(zd_gaming_cooperative(&g, "chess", 30000, 90),
                ZD_GAME_YIELD_TARGET_FLOOR);
    /* chess slow but no pressure -> NONE (fps unknown = 0 too) */
    ZD_CHECK_EQ(zd_gaming_cooperative(&g, "chess", 0, 10),
                ZD_GAME_YIELD_NONE);
    ZD_CHECK_EQ(zd_gaming_cooperative(&g, "nope", 60000, 50), -2);
    ZD_CHECK(g.stats.yields >= 4);

    /* remove frees the slot */
    ZD_CHECK_OK(zd_gaming_profile_remove(&g, "chess"));
    ZD_CHECK_EQ(g.count, 1);
    ZD_CHECK_EQ(zd_gaming_profile_remove(&g, "chess"), -2);
    ZD_CHECK_OK(zd_gaming_profile_set(&g, "civ", ZD_GAME_ECO, 60, 0));
    ZD_CHECK_EQ(g.count, 2);

    /* capacity (unique names: an existing name would just update) */
    {
        uint32_t n = 0;
        int r = 0;
        while (r == 0 && n < 32) {
            char name[16];
            name[0] = 'x'; name[1] = (char)('0' + (n % 10));
            name[2] = 0;
            r = zd_gaming_profile_set(&g, name, ZD_GAME_ECO, 60, 0);
            ++n;
        }
        ZD_CHECK_EQ(r, -28);
    }
    ZD_CHECK_EQ(zd_gaming_profile_set(&g, "overflow", ZD_GAME_ECO, 60,
                                      0), -28);
    ZD_CHECK_EQ(g.count, ZD_GAME_PROFILES);

    /* null safety */
    ZD_CHECK_EQ(zd_gaming_profile_set(NULL, "x", 1, 60, 0), -22);
    ZD_CHECK_EQ(zd_gaming_cooperative(NULL, "x", 1, 1), -22);
    zd_gaming_init(NULL);
}

void zd_test_media_gaming_suite(void) {
    test_media();
    test_gaming();
}
