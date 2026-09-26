/* Cloud/device ecosystem tests (part J) */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

void zd_test_eco_suite(void) {
    struct zd_eco e;
    uint32_t d0 = 0, d1 = 0;

    zd_eco_init(&e);
    /* offline-first: starts disconnected */
    ZD_CHECK_EQ(e.conn, ZD_ECO_OFFLINE);

    /* pairing: empty names rejected */
    ZD_CHECK_EQ(zd_eco_pair(&e, "", &d0), -22);
    ZD_CHECK_EQ(zd_eco_pair(&e, NULL, &d0), -22);
    ZD_CHECK_EQ(e.stats.rejected, 2);
    ZD_CHECK_OK(zd_eco_pair(&e, "laptop", &d0));
    ZD_CHECK_OK(zd_eco_pair(&e, "phone", &d1));
    ZD_CHECK_EQ(e.stats.paired, 2);
    /* pairing grants NOTHING */
    ZD_CHECK_EQ(e.devices[d0].permissions, 0);

    /* enqueue without permission -> refused, counted */
    ZD_CHECK_EQ(zd_eco_enqueue(&e, d0, ZD_ECO_PERM_SYNC_FILES, "doc"),
                -1);
    ZD_CHECK_EQ(e.stats.refused_perm, 1);
    /* unpaired device index -> pairing refusal */
    ZD_CHECK_EQ(zd_eco_enqueue(&e, 9, ZD_ECO_PERM_SYNC_FILES, "doc"),
                -1);
    ZD_CHECK_EQ(e.stats.refused_pairing, 1);
    /* bad payloads / perm bits */
    ZD_CHECK_EQ(zd_eco_enqueue(&e, d0, 0, "doc"), -22);
    ZD_CHECK_EQ(zd_eco_enqueue(&e, d0, 1u << 8, "doc"), -22);
    ZD_CHECK_EQ(zd_eco_enqueue(&e, d0, ZD_ECO_PERM_SYNC_FILES, ""),
                -22);
    ZD_CHECK_EQ(zd_eco_grant(&e, d0, 1u << 9), -22);
    ZD_CHECK_EQ(zd_eco_grant(&e, 9, ZD_ECO_PERM_SYNC_FILES), -2);

    /* grant -> enqueue succeeds (still offline: nothing sent) */
    ZD_CHECK_OK(zd_eco_grant(&e, d0, ZD_ECO_PERM_SYNC_FILES));
    ZD_CHECK_EQ(e.stats.granted, 1);
    ZD_CHECK_OK(zd_eco_grant(&e, d0, ZD_ECO_PERM_SYNC_FILES)); /* idem */
    ZD_CHECK_EQ(e.stats.granted, 1);
    ZD_CHECK_OK(zd_eco_enqueue(&e, d0, ZD_ECO_PERM_SYNC_FILES, "doc1"));
    ZD_CHECK_OK(zd_eco_enqueue(&e, d0, ZD_ECO_PERM_SYNC_FILES, "doc2"));
    ZD_CHECK_EQ(e.queued, 2);
    ZD_CHECK_EQ(zd_eco_flush(&e), 0); /* offline: 0 sent */
    ZD_CHECK_EQ(e.queued, 2);
    ZD_CHECK_EQ(e.stats.flushed, 0);

    /* online: flush transmits both */
    zd_eco_set_conn(&e, ZD_ECO_ONLINE);
    ZD_CHECK_EQ(zd_eco_flush(&e), 2);
    ZD_CHECK_EQ(e.queued, 0);
    ZD_CHECK_EQ(e.devices[d0].sent, 2);
    ZD_CHECK_EQ(e.stats.flushed, 2);

    /* revoke mid-queue: entry dropped on flush with refusal count */
    ZD_CHECK_OK(zd_eco_enqueue(&e, d0, ZD_ECO_PERM_SYNC_FILES, "doc3"));
    ZD_CHECK_EQ(zd_eco_revoke(&e, d0, ZD_ECO_PERM_SYNC_FILES), 0);
    ZD_CHECK_EQ(e.stats.revoked, 1);
    ZD_CHECK_EQ(e.queued, 0); /* revoke dropped the pending entry */
    ZD_CHECK_EQ(zd_eco_revoke(&e, d0, ZD_ECO_PERM_SYNC_FILES), -1);
    /* revoke of a perm never granted */
    ZD_CHECK_EQ(zd_eco_revoke(&e, d0, ZD_ECO_PERM_DISCOVER), -1);

    /* revoke-at-flush path: grant, enqueue, revoke AFTER queue but
     * wait — revoke already drops pending.  Simulate the mid-flush
     * recheck by queueing with perm, revoking another perm, flush
     * passes; then queue with perm and drop device via unpair. */
    ZD_CHECK_OK(zd_eco_grant(&e, d1, ZD_ECO_PERM_SYNC_SETTINGS));
    ZD_CHECK_OK(zd_eco_enqueue(&e, d1, ZD_ECO_PERM_SYNC_SETTINGS,
                               "prefs"));
    ZD_CHECK_OK(zd_eco_unpair(&e, d1));
    ZD_CHECK_EQ(e.queued, 0); /* unpair dropped pending entries */
    ZD_CHECK_EQ(zd_eco_flush(&e), 0); /* nothing left */
    ZD_CHECK_EQ(zd_eco_unpair(&e, d1), -2);

    /* re-pair: fresh zero permissions */
    ZD_CHECK_OK(zd_eco_pair(&e, "phone2", &d1));
    ZD_CHECK_EQ(e.devices[d1].permissions, 0);

    /* queue capacity: fill to ZD_ECO_QUEUE (d0's FILES right was
     * revoked above — re-grant first, proving grants are reversible) */
    {
        uint32_t n = 0;
        int r = 0;
        ZD_CHECK_OK(zd_eco_grant(&e, d0, ZD_ECO_PERM_SYNC_FILES));
        while (r == 0 && n < 32) {
            char payload[8];
            payload[0] = 'p';
            payload[1] = (char)('0' + (n % 10));
            payload[2] = 0;
            r = zd_eco_enqueue(&e, d0, ZD_ECO_PERM_SYNC_FILES, payload);
            ++n;
        }
        ZD_CHECK_EQ(r, -28);
        ZD_CHECK_EQ(e.queued, ZD_ECO_QUEUE);
        /* offline flush still sends nothing */
        zd_eco_set_conn(&e, ZD_ECO_OFFLINE);
        ZD_CHECK_EQ(zd_eco_flush(&e), 0);
        ZD_CHECK_EQ(e.queued, ZD_ECO_QUEUE);
        zd_eco_set_conn(&e, ZD_ECO_ONLINE);
        ZD_CHECK_EQ(zd_eco_flush(&e), ZD_ECO_QUEUE);
        ZD_CHECK_EQ(e.queued, 0);
    }

    /* device capacity */
    ZD_CHECK_OK(zd_eco_pair(&e, "tab1", 0));
    ZD_CHECK_OK(zd_eco_pair(&e, "tab2", 0));
    ZD_CHECK_EQ(zd_eco_pair(&e, "tab3", 0), -28);
    ZD_CHECK_EQ(e.device_count, ZD_ECO_DEVICES);

    /* null safety */
    zd_eco_init(NULL);
    ZD_CHECK_EQ(zd_eco_pair(NULL, "x", 0), -22);
    ZD_CHECK_EQ(zd_eco_flush(NULL), 0);
    zd_eco_set_conn(NULL, 1);
}
