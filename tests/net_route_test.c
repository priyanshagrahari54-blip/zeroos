/* Host test for kernel/net_route.c: contiguous-mask validation, exact
 * duplicate rejection, longest-prefix (then lowest-metric) selection. */
#include <assert.h>
#include <string.h>
#include "../kernel/net_route.h"

static struct net_route make_route(uint32_t prefix, uint32_t mask,
                                   uint32_t gateway, uint32_t interface_id,
                                   uint32_t metric) {
    struct net_route r;

    r.prefix = prefix;
    r.mask = mask;
    r.gateway = gateway;
    r.interface_id = interface_id;
    r.metric = metric;
    r.active = 1;
    return r;
}

static void test_argument_guards(void) {
    struct net_routes t;
    struct net_route r = make_route(0x0a000000U, 0xff000000U, 0, 1, 10);
    struct net_route out;

    memset(&t, 0, sizeof(t));

    assert(net_route_add(0, &r) == -1);
    assert(net_route_add(&t, 0) == -1);
    assert(net_route_remove(0, 0, 0, 0) == -1);
    assert(net_route_lookup(0, 0x0a000001U, &out) == -1);
    assert(net_route_lookup(&t, 0x0a000001U, 0) == -1);

    /* An inactive route is never installed. */
    r.active = 0;
    assert(net_route_add(&t, &r) == -1);
    r.active = 1;
    assert(t.count == 0);
}

static void test_mask_validation(void) {
    struct net_routes t;
    struct net_route r;

    /* A default route (mask 0) is legal and contiguous. */
    memset(&t, 0, sizeof(t));
    r = make_route(0, 0, 0x0a000001U, 1, 100);
    assert(net_route_add(&t, &r) == 0);

    /* Host route (all-ones mask) is legal. */
    memset(&t, 0, sizeof(t));
    r = make_route(0x0a000001U, 0xffffffffU, 0, 2, 10);
    assert(net_route_add(&t, &r) == 0);

    /* Every contiguous mask must be accepted. */
    for (uint32_t bits = 0; bits <= 32; ++bits) {
        uint32_t mask;
        memset(&t, 0, sizeof(t));
        mask = (bits == 0) ? 0U : (0xffffffffU << (32U - bits));
        r = make_route(0x0a000000U & mask, mask, 0, (uint32_t)(bits + 1), 10);
        assert(net_route_add(&t, &r) == 0);
    }

    /* Masks whose set bits are not one leading run are rejected.
     * (0xfe000000 is /7 and 0xfffffffe is /31 -- both are legal runs.) */
    memset(&t, 0, sizeof(t));
    r = make_route(0x0a000000U, 0xff00ff00U, 0, 1, 10);
    assert(net_route_add(&t, &r) == -1);
    r = make_route(0x0a000000U, 0x00ff0000U, 0, 1, 10);
    assert(net_route_add(&t, &r) == -1);
    r = make_route(0x0a000000U, 0x80000001U, 0, 1, 10);
    assert(net_route_add(&t, &r) == -1);
    r = make_route(0x0a000000U, 0xffff00ffU, 0, 1, 10);
    assert(net_route_add(&t, &r) == -1);
    assert(t.count == 0);

    /* Host bits set outside the mask are rejected. */
    r = make_route(0x0a0000ffU, 0xff000000U, 0, 1, 10);
    assert(net_route_add(&t, &r) == -1);
    assert(t.count == 0);
}

static void test_duplicates_and_capacity(void) {
    struct net_routes t;
    struct net_route r;

    memset(&t, 0, sizeof(t));
    r = make_route(0x0a000000U, 0xff000000U, 0, 1, 10);
    assert(net_route_add(&t, &r) == 0);

    /* Same prefix/mask/interface is a duplicate even with a different
     * gateway or metric. */
    r.gateway = 0x0a0000feU;
    assert(net_route_add(&t, &r) == -1);
    r.metric = 5;
    assert(net_route_add(&t, &r) == -1);
    assert(t.count == 1);

    /* A different interface is a distinct route (equal-cost multipath). */
    r = make_route(0x0a000000U, 0xff000000U, 0, 2, 10);
    assert(net_route_add(&t, &r) == 0);
    assert(t.count == 2);

    /* Capacity is NET_MAX_ROUTES. */
    memset(&t, 0, sizeof(t));
    for (uint32_t i = 0; i < NET_MAX_ROUTES; ++i) {
        r = make_route(0x0a000000U + (i << 8), 0xffffff00U, 0, i + 1, 10);
        assert(net_route_add(&t, &r) == 0);
    }
    assert(t.count == NET_MAX_ROUTES);
    r = make_route(0x0b000000U, 0xff000000U, 0, 200, 10);
    assert(net_route_add(&t, &r) == -1);
    assert(t.count == NET_MAX_ROUTES);
}

static void test_remove(void) {
    struct net_routes t;
    struct net_route r;
    struct net_route out;

    memset(&t, 0, sizeof(t));
    r = make_route(0x0a000000U, 0xff000000U, 0, 1, 10);
    assert(net_route_add(&t, &r) == 0);
    r = make_route(0x0b000000U, 0xff000000U, 0, 2, 10);
    assert(net_route_add(&t, &r) == 0);
    r = make_route(0x0c000000U, 0xff000000U, 0, 3, 10);
    assert(net_route_add(&t, &r) == 0);
    assert(t.count == 3);

    /* Removal is matched on prefix, mask and interface together. */
    assert(net_route_remove(&t, 0x0b000000U, 0xff000000U, 1) == -1);
    assert(t.count == 3);
    assert(net_route_remove(&t, 0x0b000000U, 0xff000000U, 2) == 0);
    assert(t.count == 2);
    assert(t.entries[0].interface_id == 1);
    assert(t.entries[1].interface_id == 3);

    /* Removing a route that was never there fails. */
    assert(net_route_remove(&t, 0x0d000000U, 0xff000000U, 4) == -1);

    /* Removing everything leaves an empty table. */
    assert(net_route_remove(&t, 0x0a000000U, 0xff000000U, 1) == 0);
    assert(net_route_remove(&t, 0x0c000000U, 0xff000000U, 3) == 0);
    assert(t.count == 0);
    assert(net_route_lookup(&t, 0x0a000001U, &out) == -1);
}

static void test_longest_prefix_match(void) {
    struct net_routes t;
    struct net_route d = make_route(0, 0, 1, 1, 100);
    struct net_route n = make_route(0x0a000000U, 0xff000000U, 0, 2, 10);
    struct net_route h = make_route(0x0a010203U, 0xffffffffU, 0, 3, 50);
    struct net_route out;

    memset(&t, 0, sizeof(t));
    assert(net_route_add(&t, &d) == 0);
    assert(net_route_add(&t, &n) == 0);

    assert(net_route_lookup(&t, 0x0a000001U, &out) == 0);
    assert(out.interface_id == 2);
    assert(net_route_lookup(&t, 0x08080808U, &out) == 0);
    assert(out.interface_id == 1);

    /* A host route beats both the /8 and the default for its own address. */
    assert(net_route_add(&t, &h) == 0);
    assert(net_route_lookup(&t, 0x0a010203U, &out) == 0);
    assert(out.interface_id == 3);
    /* Any other address in the /8 still uses the /8. */
    assert(net_route_lookup(&t, 0x0a010204U, &out) == 0);
    assert(out.interface_id == 2);

    /* Removing the /8 falls back to the default route. */
    assert(net_route_remove(&t, n.prefix, n.mask, n.interface_id) == 0);
    assert(net_route_lookup(&t, 0x0a000001U, &out) == 0);
    assert(out.interface_id == 1);
}

static void test_metric_tiebreak(void) {
    struct net_routes t;
    struct net_route out;
    struct net_route expensive = make_route(0x0a000000U, 0xff000000U, 0, 7, 90);
    struct net_route cheap = make_route(0x0a000000U, 0xff000000U, 0, 8, 5);

    memset(&t, 0, sizeof(t));
    assert(net_route_add(&t, &expensive) == 0);
    assert(net_route_add(&t, &cheap) == 0);

    assert(net_route_lookup(&t, 0x0a000001U, &out) == 0);
    assert(out.interface_id == 8);
    assert(out.metric == 5);

    /* Adding the cheap one first must not change the answer. */
    memset(&t, 0, sizeof(t));
    assert(net_route_add(&t, &cheap) == 0);
    assert(net_route_add(&t, &expensive) == 0);
    assert(net_route_lookup(&t, 0x0a000001U, &out) == 0);
    assert(out.interface_id == 8);

    /* Removing the cheap route exposes the expensive one. */
    assert(net_route_remove(&t, cheap.prefix, cheap.mask,
                            cheap.interface_id) == 0);
    assert(net_route_lookup(&t, 0x0a000001U, &out) == 0);
    assert(out.interface_id == 7);
}

static void test_inactive_routes_are_skipped(void) {
    struct net_routes t;
    struct net_route r = make_route(0x0a000000U, 0xff000000U, 0, 1, 10);
    struct net_route out;

    memset(&t, 0, sizeof(t));
    assert(net_route_add(&t, &r) == 0);
    /* Marking an installed route inactive hides it from lookup without
     * removing it from the table. */
    t.entries[0].active = 0;
    assert(net_route_lookup(&t, 0x0a000001U, &out) == -1);
    assert(t.count == 1);
    t.entries[0].active = 1;
    assert(net_route_lookup(&t, 0x0a000001U, &out) == 0);
}

int main(void) {
    test_argument_guards();
    test_mask_validation();
    test_duplicates_and_capacity();
    test_remove();
    test_metric_tiebreak();
    test_longest_prefix_match();
    test_inactive_routes_are_skipped();
    return 0;
}
