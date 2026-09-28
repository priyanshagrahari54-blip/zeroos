/* Host test for kernel/netif.c: interface lifecycle, the bounded RX ring and
 * the transmit path (including the lock-discipline rule that the driver
 * callback is never invoked while holding the queue lock). */
#include <assert.h>
#include <string.h>
#include "../kernel/netif.h"

static int sent;
static int fail_tx;
static int lock_depth;
static int max_lock_depth;
static int tx_lock_depth;

static int tx(void *c, const uint8_t *f, uint16_t n) {
    (void)c;
    (void)f;
    tx_lock_depth = lock_depth;
    sent++;
    return (n && !fail_tx) ? 0 : -1;
}

static uint64_t lock(void *c) {
    (void)c;
    lock_depth++;
    if (lock_depth > max_lock_depth)
        max_lock_depth = lock_depth;
    return 0;
}

static void unlock(void *c, uint64_t s) {
    (void)c;
    (void)s;
    lock_depth--;
}

/* A minimal, parseable Ethernet II frame: dst(6) src(6) ethertype(2). */
static uint32_t build_frame(uint8_t *f, uint32_t payload) {
    memset(f, 0, NETIF_FRAME_MAX);
    for (uint32_t i = 0; i < 6; ++i) {
        f[i] = 0x02;
        f[6 + i] = 0x01;
    }
    f[12] = 0x08;
    f[13] = 0x00;
    return 14 + payload;
}

static void ready_interface(struct netif *n) {
    uint8_t mac[6] = {2, 0, 0, 0, 0, 1};

    sent = 0;
    fail_tx = 0;
    lock_depth = 0;
    max_lock_depth = 0;
    tx_lock_depth = -1;
    assert(netif_init(n, "qemu0", 1, mac, 1500, 0, tx, lock, unlock) == 0);
    assert(netif_set_link(n, 1) == 0);
}

static void test_init_arguments(void) {
    struct netif n;
    uint8_t mac[6] = {2, 0, 0, 0, 0, 1};

    assert(netif_init(0, "qemu0", 1, mac, 1500, 0, tx, lock, unlock) == -1);
    assert(netif_init(&n, 0, 1, mac, 1500, 0, tx, lock, unlock) == -1);
    assert(netif_init(&n, "qemu0", 1, 0, 1500, 0, tx, lock, unlock) == -1);
    assert(netif_init(&n, "qemu0", 0, mac, 1500, 0, tx, lock, unlock) == -1);
    assert(netif_init(&n, "qemu0", 1, mac, 1500, 0, 0, lock, unlock) == -1);
    assert(netif_init(&n, "qemu0", 1, mac, 1500, 0, tx, 0, unlock) == -1);
    assert(netif_init(&n, "qemu0", 1, mac, 1500, 0, tx, lock, 0) == -1);

    /* An empty name and a name that fills the buffer with no NUL room are
     * both refused. */
    assert(netif_init(&n, "", 1, mac, 1500, 0, tx, lock, unlock) == -1);
    assert(netif_init(&n, "0123456789abcdef", 1, mac, 1500, 0, tx, lock,
                      unlock) == -1);
    /* A 15-character name fits with its terminator. */
    assert(netif_init(&n, "0123456789abcde", 1, mac, 1500, 0, tx, lock,
                      unlock) == 0);
    assert(strcmp(n.name, "0123456789abcde") == 0);

    /* MTU bounds. */
    assert(netif_init(&n, "qemu0", 1, mac, NETIF_MTU_MIN - 1, 0, tx, lock,
                      unlock) == -1);
    assert(netif_init(&n, "qemu0", 1, mac, NETIF_MTU_MIN, 0, tx, lock,
                      unlock) == 0);
    assert(netif_init(&n, "qemu0", 1, mac, NETIF_MTU_MAX, 0, tx, lock,
                      unlock) == 0);
    assert(netif_init(&n, "qemu0", 1, mac, NETIF_MTU_MAX + 1, 0, tx, lock,
                      unlock) == -1);
}

static void test_init_clears_state(void) {
    struct netif n;
    uint8_t mac[6] = {2, 0, 0, 0, 0, 1};

    memset(&n, 0xff, sizeof(n));
    assert(netif_init(&n, "qemu0", 1, mac, 1500, 0, tx, lock, unlock) == 0);
    assert(n.count == 0);
    assert(n.head == 0);
    assert(n.tail == 0);
    assert(n.flags == 0);
    assert(n.stats.rx_packets == 0);
    assert(n.stats.tx_packets == 0);
    assert(n.stats.rx_dropped == 0);
    assert(n.stats.tx_dropped == 0);
    assert(n.stats.rx_errors == 0);
    assert(n.stats.tx_errors == 0);
    assert(n.mtu == 1500);
    assert(n.index == 1);
    assert(n.address[5] == 1);

    /* Re-initialising resets the queue and statistics. */
    assert(netif_set_link(&n, 1) == 0);
    {
        uint8_t f[NETIF_FRAME_MAX];
        uint32_t length = build_frame(f, 0);
        assert(netif_receive(&n, f, length) == 0);
    }
    assert(n.count == 1);
    assert(netif_init(&n, "eth1", 2, mac, 1500, 0, tx, lock, unlock) == 0);
    assert(n.count == 0);
    assert(n.flags == 0);
    assert(n.stats.rx_packets == 0);
    assert(strcmp(n.name, "eth1") == 0);
}

static void test_link_state(void) {
    struct netif n;
    uint8_t f[NETIF_FRAME_MAX];
    uint32_t length;

    ready_interface(&n);
    length = build_frame(f, 0);

    assert(netif_set_link(0, 1) == -1);

    /* Link down: receive and transmit are both refused and counted. */
    assert(netif_set_link(&n, 0) == 0);
    assert((n.flags & NETIF_FLAG_LINK) == 0);
    assert((n.flags & NETIF_FLAG_UP) == 0);
    assert(netif_receive(&n, f, length) == -1);
    assert(netif_send(&n, f, length) == -1);
    assert(n.stats.rx_errors == 1);
    assert(n.stats.tx_errors == 1);
    assert(n.stats.rx_packets == 0);
    assert(n.stats.tx_packets == 0);
    assert(sent == 0);

    /* Link up again. */
    assert(netif_set_link(&n, 1) == 0);
    assert((n.flags & NETIF_FLAG_LINK) != 0);
    assert((n.flags & NETIF_FLAG_UP) != 0);
    assert(netif_receive(&n, f, length) == 0);
    assert(netif_send(&n, f, length) == 0);
    assert(sent == 1);

    /* Setting the link up twice is idempotent. */
    assert(netif_set_link(&n, 1) == 0);
    assert((n.flags & NETIF_FLAG_LINK) != 0);
}

static void test_receive_validation(void) {
    struct netif n;
    uint8_t f[NETIF_FRAME_MAX];
    uint32_t length;

    ready_interface(&n);
    length = build_frame(f, 0);

    assert(netif_receive(0, f, length) == -1);
    assert(netif_receive(&n, 0, length) == -1);

    /* Shorter than an Ethernet header. */
    assert(netif_receive(&n, f, 13) == -1);
    assert(netif_receive(&n, f, 0) == -1);
    assert(n.stats.rx_errors == 3);

    /* Larger than the frame cap. */
    assert(netif_receive(&n, f, NETIF_FRAME_MAX + 1) == -1);
    /* Larger than MTU + 18 (Ethernet header + FCS allowance). */
    assert(netif_receive(&n, f, 1500U + 19U) == -1);
    /* Exactly MTU + 18 is accepted. */
    assert(netif_receive(&n, f, 1500U + 18U) == 0);
    assert(n.stats.rx_packets == 1);

    /* A frame whose ethertype field is really an 802.3 length is refused by
     * the Ethernet II parser. */
    f[12] = 0x05;
    f[13] = 0x00;
    assert(netif_receive(&n, f, length) == -1);
    f[12] = 0x08;
    f[13] = 0x00;
    assert(netif_receive(&n, f, length) == 0);
}

static void test_rx_queue_ring(void) {
    struct netif n;
    uint8_t f[NETIF_FRAME_MAX];
    uint8_t out[NETIF_FRAME_MAX];
    uint16_t len;
    uint32_t length;

    ready_interface(&n);
    length = build_frame(f, 4);
    /* Distinguish queued frames by a payload byte. */
    for (uint32_t i = 0; i < 4; ++i)
        f[14 + i] = (uint8_t)(0xA0 + i);

    /* Dequeue from an empty ring reports "empty", not an error. */
    assert(netif_dequeue(&n, out, sizeof(out), &len) == 1);

    /* Fill the ring and drain it in order. */
    for (uint32_t i = 0; i < NETIF_RX_QUEUE; ++i) {
        f[14] = (uint8_t)(0xB0 + i);
        assert(netif_receive(&n, f, length) == 0);
    }
    assert(n.count == NETIF_RX_QUEUE);
    for (uint32_t i = 0; i < NETIF_RX_QUEUE; ++i) {
        assert(netif_dequeue(&n, out, sizeof(out), &len) == 0);
        assert(len == length);
        assert(out[14] == (uint8_t)(0xB0 + i));
    }
    assert(n.count == 0);
    assert(netif_dequeue(&n, out, sizeof(out), &len) == 1);

    /* The ring wraps: a second fill/drain cycle must still be in order. */
    for (uint32_t i = 0; i < NETIF_RX_QUEUE; ++i) {
        f[14] = (uint8_t)(0xC0 + i);
        assert(netif_receive(&n, f, length) == 0);
    }
    for (uint32_t i = 0; i < NETIF_RX_QUEUE; ++i) {
        assert(netif_dequeue(&n, out, sizeof(out), &len) == 0);
        assert(out[14] == (uint8_t)(0xC0 + i));
    }
    assert(n.count == 0);
}

static void test_rx_queue_overflow(void) {
    struct netif n;
    uint8_t f[NETIF_FRAME_MAX];
    uint8_t out[NETIF_FRAME_MAX];
    uint16_t len;
    uint32_t length;

    ready_interface(&n);
    length = build_frame(f, 0);

    for (uint32_t i = 0; i < NETIF_RX_QUEUE; ++i)
        assert(netif_receive(&n, f, length) == 0);

    /* A full ring drops rather than overwriting in flight. */
    assert(netif_receive(&n, f, length) == -2);
    assert(n.stats.rx_dropped == 1);
    assert(n.stats.rx_packets == NETIF_RX_QUEUE);
    assert(n.count == NETIF_RX_QUEUE);

    /* Freeing one slot admits exactly one more frame. */
    assert(netif_dequeue(&n, out, sizeof(out), &len) == 0);
    assert(netif_receive(&n, f, length) == 0);
    assert(netif_receive(&n, f, length) == -2);
    assert(n.stats.rx_dropped == 2);

    /* Draining fully makes the ring usable again. */
    for (uint32_t i = 0; i < NETIF_RX_QUEUE; ++i)
        assert(netif_dequeue(&n, out, sizeof(out), &len) == 0);
    assert(n.count == 0);
    assert(netif_receive(&n, f, length) == 0);
}

static void test_dequeue_capacity(void) {
    struct netif n;
    uint8_t f[NETIF_FRAME_MAX];
    uint8_t out[NETIF_FRAME_MAX];
    uint16_t len;
    uint32_t length;

    ready_interface(&n);
    length = build_frame(f, 0);

    assert(netif_dequeue(0, out, sizeof(out), &len) == -1);
    assert(netif_dequeue(&n, 0, sizeof(out), &len) == -1);
    assert(netif_dequeue(&n, out, sizeof(out), 0) == -1);

    assert(netif_receive(&n, f, length) == 0);

    /* A buffer one byte short is refused and the frame stays queued. */
    assert(netif_dequeue(&n, out, length - 1, &len) == -2);
    assert(n.count == 1);
    /* Exactly the frame length is enough. */
    assert(netif_dequeue(&n, out, length, &len) == 0);
    assert(len == length);
    assert(n.count == 0);
}

static void test_send_paths(void) {
    struct netif n;
    uint8_t f[NETIF_FRAME_MAX];
    uint32_t length;

    ready_interface(&n);
    length = build_frame(f, 0);

    /* A NULL interface is rejected without touching statistics; a NULL frame
     * is charged to this interface as a transmit error. */
    assert(netif_send(0, f, length) == -1);
    assert(netif_send(&n, 0, length) == -1);
    assert(n.stats.tx_errors == 1);

    /* Length validation mirrors the receive path. */
    assert(netif_send(&n, f, 13) == -1);
    assert(netif_send(&n, f, NETIF_FRAME_MAX + 1) == -1);
    assert(netif_send(&n, f, 1500U + 19U) == -1);
    assert(n.stats.tx_errors == 4);
    assert(sent == 0);

    /* Unparseable framing is refused before the driver is called. */
    f[12] = 0x05;
    assert(netif_send(&n, f, length) == -1);
    assert(sent == 0);
    f[12] = 0x08;

    /* Success path. */
    assert(netif_send(&n, f, length) == 0);
    assert(sent == 1);
    assert(n.stats.tx_packets == 1);
    assert(n.stats.tx_dropped == 0);

    /* A driver that reports failure is counted as a drop, not a success. */
    fail_tx = 1;
    assert(netif_send(&n, f, length) == -2);
    assert(sent == 2);
    assert(n.stats.tx_packets == 1);
    assert(n.stats.tx_dropped == 1);
    fail_tx = 0;

    /* The driver callback must run without the queue lock held. */
    assert(netif_send(&n, f, length) == 0);
    assert(tx_lock_depth == 0);
    assert(max_lock_depth == 1);
    /* And every lock acquisition is released. */
    assert(lock_depth == 0);
}

int main(void) {
    test_init_arguments();
    test_init_clears_state();
    test_link_state();
    test_receive_validation();
    test_rx_queue_ring();
    test_rx_queue_overflow();
    test_dequeue_capacity();
    test_send_paths();
    return 0;
}
