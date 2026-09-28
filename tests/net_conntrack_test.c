/* Host test for kernel/net_conntrack.c: flow tracking, expiry, reverse
 * matching and capacity-driven eviction. */
#include <assert.h>
#include <string.h>
#include "../kernel/net_conntrack.h"

static struct net_flow make_flow(uint32_t src, uint32_t dst, uint16_t sport,
                                 uint16_t dport, uint8_t proto) {
    struct net_flow f;

    f.source = src;
    f.destination = dst;
    f.source_port = sport;
    f.destination_port = dport;
    f.protocol = proto;
    return f;
}

static uint32_t active_count(const struct net_conntrack *t) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < NET_CONNTRACK_CAPACITY; ++i)
        if (t->entries[i].active)
            ++n;
    return n;
}

static void test_argument_guards(void) {
    struct net_conntrack t;
    struct net_flow f = make_flow(1, 2, 100, 80, 6);

    memset(&t, 0, sizeof(t));

    /* NULL table or flow */
    assert(net_conntrack_observe(0, &f, 10, 20) == -1);
    assert(net_conntrack_observe(&t, 0, 10, 20) == -1);

    /* A zero lifetime could never expire anything. */
    assert(net_conntrack_observe(&t, &f, 10, 0) == -1);
    /* Nor can one whose top bit would wrap the expiry comparison. */
    assert(net_conntrack_observe(&t, &f, 10, 0x80000000U) == -1);
    assert(active_count(&t) == 0);

    /* expire tolerates NULL */
    net_conntrack_expire(0, 10);
    net_conntrack_expire(&t, 10);
}

static void test_insert_and_track(void) {
    struct net_conntrack t;
    struct net_flow a = make_flow(1, 2, 100, 80, 6);
    struct net_flow b = make_flow(2, 1, 80, 100, 6);

    memset(&t, 0, sizeof(t));

    /* First sighting inserts. */
    assert(net_conntrack_observe(&t, &a, 10, 20) == 0);
    assert(t.inserts == 1);
    assert(active_count(&t) == 1);
    assert(t.entries[0].expires == 30);
    assert(t.entries[0].seen_reverse == 0);

    /* The same direction again is already tracked. */
    assert(net_conntrack_observe(&t, &a, 12, 20) == 1);
    assert(t.inserts == 1);
    assert(active_count(&t) == 1);
    assert(t.entries[0].expires == 32);

    /* The reverse direction is the same connection. */
    assert(net_conntrack_observe(&t, &b, 13, 20) == 1);
    assert(t.inserts == 1);
    assert(active_count(&t) == 1);
    assert(t.entries[0].seen_reverse == 1);

    /* A genuinely different flow needs its own entry. */
    {
        struct net_flow c = make_flow(1, 2, 100, 81, 6);
        assert(net_conntrack_observe(&t, &c, 14, 20) == 0);
        assert(active_count(&t) == 2);
        assert(t.inserts == 2);
    }

    /* Same ports, different protocol: a different flow. */
    {
        struct net_flow d = make_flow(1, 2, 100, 80, 17);
        assert(net_conntrack_observe(&t, &d, 15, 20) == 0);
        assert(active_count(&t) == 3);
    }

    /* Swapped ports without swapped addresses is NOT the reverse tuple:
     * reverse() requires both address and port pairs to be exchanged. */
    {
        struct net_flow e = make_flow(1, 2, 80, 100, 6);
        assert(net_conntrack_observe(&t, &e, 16, 20) == 0);
        assert(active_count(&t) == 4);
    }
}

static void test_expiry(void) {
    struct net_conntrack t;
    struct net_flow a = make_flow(1, 2, 100, 80, 6);
    struct net_flow b = make_flow(3, 4, 200, 90, 17);

    memset(&t, 0, sizeof(t));
    assert(net_conntrack_observe(&t, &a, 10, 20) == 0);
    assert(net_conntrack_observe(&t, &b, 15, 20) == 0);
    assert(active_count(&t) == 2);

    /* Before either deadline nothing is reclaimed. */
    net_conntrack_expire(&t, 29);
    assert(active_count(&t) == 2);

    /* The first entry reaches its deadline at 30. */
    net_conntrack_expire(&t, 30);
    assert(active_count(&t) == 1);
    assert(!t.entries[0].active);
    assert(t.entries[1].active);

    /* The second at 35. */
    net_conntrack_expire(&t, 35);
    assert(active_count(&t) == 0);

    /* An expired flow is first-seen again, in a recycled slot. */
    assert(net_conntrack_observe(&t, &a, 40, 20) == 0);
    assert(active_count(&t) == 1);

    /* A live observation refreshes the deadline, so expiry does not fire. */
    assert(net_conntrack_observe(&t, &a, 45, 20) == 1);
    net_conntrack_expire(&t, 59);
    assert(active_count(&t) == 1);
    net_conntrack_expire(&t, 65);
    assert(active_count(&t) == 0);
}

static void test_capacity_and_eviction(void) {
    struct net_conntrack t;
    struct net_flow f;

    memset(&t, 0, sizeof(t));

    /* Fill the table with distinct flows. */
    for (uint32_t i = 0; i < NET_CONNTRACK_CAPACITY; ++i) {
        f = make_flow(0x0a000000U + i, 0x08080808U, (uint16_t)(1000 + i), 80, 6);
        assert(net_conntrack_observe(&t, &f, 100, 20) == 0);
    }
    assert(active_count(&t) == NET_CONNTRACK_CAPACITY);
    assert(t.evictions == 0);
    assert(t.inserts == NET_CONNTRACK_CAPACITY);

    /* An already-known flow is still found with no eviction. */
    f = make_flow(0x0a000000U, 0x08080808U, 1000, 80, 6);
    assert(net_conntrack_observe(&t, &f, 100, 20) == 1);
    assert(t.evictions == 0);
    assert(active_count(&t) == NET_CONNTRACK_CAPACITY);

    /* A new flow evicts the entry with the earliest expiry (oldest). */
    {
        struct net_flow fresh =
            make_flow(0xc0a80001U, 0x08080808U, 4444, 443, 6);
        assert(net_conntrack_observe(&t, &fresh, 100, 20) == 0);
        assert(t.evictions == 1);
        assert(active_count(&t) == NET_CONNTRACK_CAPACITY);
        /* The evicted slot now holds the fresh flow. */
        assert(t.entries[0].flow.source == 0xc0a80001U);
    }

    /* Expired entries free slots before eviction is considered. */
    net_conntrack_expire(&t, 200);
    assert(active_count(&t) == 0);
    {
        struct net_flow again =
            make_flow(0x0a000000U, 0x08080808U, 1000, 80, 6);
        assert(net_conntrack_observe(&t, &again, 200, 20) == 0);
        assert(t.evictions == 1); /* unchanged: a free slot was available */
    }
}

int main(void) {
    test_argument_guards();
    test_insert_and_track();
    test_expiry();
    test_capacity_and_eviction();
    return 0;
}
