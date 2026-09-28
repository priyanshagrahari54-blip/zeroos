/* Host test for kernel/net_socket.c: the bounded, owner-bound IPv4 UDP
 * endpoint table with generation-tagged handles. */
#include <assert.h>
#include <string.h>
#include "../kernel/net_socket.h"

static void mapped(uint8_t out[16], uint32_t address) {
    for (uint32_t i = 0; i < 16; i++)
        out[i] = 0;
    out[10] = 0xff;
    out[11] = 0xff;
    out[12] = (uint8_t)(address >> 24);
    out[13] = (uint8_t)(address >> 16);
    out[14] = (uint8_t)(address >> 8);
    out[15] = (uint8_t)address;
}

static uint32_t slot_of(uint64_t handle) {
    return (uint32_t)handle; /* 1-based slot id in the low half */
}

static void test_table_init(void) {
    struct net_udp_socket_table t;

    net_udp_socket_table_init(0); /* must tolerate NULL */

    memset(&t, 0xff, sizeof(t));
    net_udp_socket_table_init(&t);
    assert(t.next_generation == 1);
    assert(t.delivered == 0);
    assert(t.dropped == 0);
    assert(t.unmatched == 0);
    assert(t.invalid == 0);
    for (uint32_t i = 0; i < NET_UDP_SOCKET_CAPACITY; ++i) {
        assert(t.slots[i].active == 0);
        assert(t.slots[i].count == 0);
        assert(t.slots[i].dropped == 0);
        assert(t.slots[i].head == 0);
        assert(t.slots[i].tail == 0);
    }
}

static void test_bind_arguments(void) {
    struct net_udp_socket_table t;
    uint64_t handle = 0;

    net_udp_socket_table_init(&t);

    assert(net_udp_socket_bind(0, 1, 0, 8080, &handle) == -1);
    assert(net_udp_socket_bind(&t, 1, 0, 8080, 0) == -1);
    /* Owner 0 is not a principal. */
    assert(net_udp_socket_bind(&t, 0, 0, 8080, &handle) == -1);
    /* Port 0 is not bindable. */
    assert(net_udp_socket_bind(&t, 1, 0, 0, &handle) == -1);
    /* Broadcast and multicast local addresses are refused. */
    assert(net_udp_socket_bind(&t, 1, 0xffffffffU, 8080, &handle) == -1);
    assert(net_udp_socket_bind(&t, 1, 0xe0000001U, 8080, &handle) == -1);
    assert(net_udp_socket_bind(&t, 1, 0xefffffffU, 8080, &handle) == -1);
    assert(net_udp_socket_bind(&t, 1, 0xf0000000U, 8080, &handle) == 0);
    /* Nothing was consumed by the rejected binds. */
    assert(t.next_generation == 2);
}

static void test_bind_conflicts(void) {
    struct net_udp_socket_table t;
    uint64_t wildcard = 0, specific = 0, other = 0;

    net_udp_socket_table_init(&t);

    /* A wildcard bind blocks every later bind of that port, by any owner. */
    assert(net_udp_socket_bind(&t, 1, 0, 8080, &wildcard) == 0);
    assert(net_udp_socket_bind(&t, 1, 0xc0000202U, 8080, &specific) == -3);
    assert(net_udp_socket_bind(&t, 2, 0, 8080, &other) == -3);
    assert(net_udp_socket_bind(&t, 2, 0xc0000202U, 8080, &other) == -3);

    /* A different port is unaffected. */
    assert(net_udp_socket_bind(&t, 1, 0xc0000202U, 8081, &specific) == 0);

    /* Closing the wildcard frees the port again. */
    assert(net_udp_socket_close(&t, 1, wildcard) == 0);
    assert(net_udp_socket_bind(&t, 2, 0, 8080, &other) == 0);
    assert(other != wildcard); /* generation differs */
    assert(slot_of(other) == slot_of(wildcard)); /* slot reused */

    /* A specific bind only conflicts with the same address or a wildcard. */
    net_udp_socket_table_init(&t);
    assert(net_udp_socket_bind(&t, 1, 0xc0000202U, 8080, &specific) == 0);
    assert(net_udp_socket_bind(&t, 1, 0xc0000202U, 8080, &other) == -3);
    assert(net_udp_socket_bind(&t, 2, 0xc0000202U, 8080, &other) == -3);
    /* Same port, different local address: allowed. */
    assert(net_udp_socket_bind(&t, 2, 0xc0000203U, 8080, &other) == 0);
    /* A wildcard bind is refused while any specific bind holds the port. */
    assert(net_udp_socket_bind(&t, 3, 0, 8080, &wildcard) == -3);
}

static void test_bind_capacity(void) {
    struct net_udp_socket_table t;
    uint64_t handles[NET_UDP_SOCKET_CAPACITY];
    uint64_t extra = 0;

    net_udp_socket_table_init(&t);
    for (uint32_t i = 0; i < NET_UDP_SOCKET_CAPACITY; ++i)
        assert(net_udp_socket_bind(&t, 9, 0, (uint16_t)(10000U + i),
                                   &handles[i]) == 0);
    assert(net_udp_socket_bind(&t, 9, 0, 20000, &extra) == -4);

    /* Freeing one slot admits exactly one more bind. */
    assert(net_udp_socket_close(&t, 9, handles[0]) == 0);
    assert(net_udp_socket_bind(&t, 9, 0, 20000, &extra) == 0);
    assert(net_udp_socket_bind(&t, 9, 0, 20001, &extra) == -4);
}

static void test_generation_exhaustion(void) {
    struct net_udp_socket_table t;
    uint64_t handle = 0;

    /* A table whose generation counter has wrapped refuses new binds rather
     * than handing out a generation that could alias a live handle. */
    net_udp_socket_table_init(&t);
    t.next_generation = 0;
    assert(net_udp_socket_bind(&t, 1, 0, 8080, &handle) == -2);

    /* One generation left still works and leaves the counter at 0. */
    net_udp_socket_table_init(&t);
    t.next_generation = 0xffffffffU;
    assert(net_udp_socket_bind(&t, 1, 0, 8080, &handle) == 0);
    assert(t.next_generation == 0);
    assert(net_udp_socket_bind(&t, 1, 0, 8081, &handle) == -2);
}

static void test_close(void) {
    struct net_udp_socket_table t;
    uint64_t handle = 0;
    uint32_t invalid_before;

    net_udp_socket_table_init(&t);
    assert(net_udp_socket_bind(&t, 1, 0, 8080, &handle) == 0);

    /* A foreign owner cannot close someone else's endpoint. */
    invalid_before = (uint32_t)t.invalid;
    assert(net_udp_socket_close(&t, 2, handle) == -1);
    assert(t.invalid == invalid_before + 1);

    /* A stale handle (slot id 0) and an out-of-range slot are rejected. */
    assert(net_udp_socket_close(&t, 1, 0) == -1);
    assert(net_udp_socket_close(&t, 1,
                                (uint64_t)(NET_UDP_SOCKET_CAPACITY + 1)) == -1);
    /* A zero generation is never valid. */
    assert(net_udp_socket_close(&t, 1, 1) == -1);

    /* Double close fails: the slot is inactive the second time. */
    assert(net_udp_socket_close(&t, 1, handle) == 0);
    assert(net_udp_socket_close(&t, 1, handle) == -1);

    /* Closing with a NULL table is rejected without faulting. */
    assert(net_udp_socket_close(0, 1, handle) == -1);
}

static void test_receive(void) {
    struct net_udp_socket_table t;
    uint64_t handle = 0;
    uint8_t src[16], dst[16], output[16], data[3] = {'u', 'd', 'p'};
    struct net_udp_receive_info info;

    net_udp_socket_table_init(&t);
    assert(net_udp_socket_bind(&t, 1, 0, 8080, &handle) == 0);
    mapped(src, 0xc0000201U);
    mapped(dst, 0xc0000202U);

    /* Empty queue reports "empty", not an error. */
    assert(net_udp_socket_receive(&t, 1, handle, output, sizeof(output),
                                  &info) == 1);

    /* NULL payload/info and a NULL table are rejected. */
    {
        uint32_t invalid_before = (uint32_t)t.invalid;
        assert(net_udp_socket_receive(&t, 1, handle, 0, sizeof(output),
                                      &info) == -1);
        assert(net_udp_socket_receive(&t, 1, handle, output, sizeof(output),
                                      0) == -1);
        assert(net_udp_socket_receive(0, 1, handle, output, sizeof(output),
                                      &info) == -1);
        assert(t.invalid > invalid_before);
    }

    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 40000, 8080, data, 3) ==
           0);
    assert(t.delivered == 1);

    /* A short buffer leaves the datagram queued. */
    assert(net_udp_socket_receive(&t, 1, handle, output, 2, &info) == -2);
    assert(t.slots[slot_of(handle) - 1U].count == 1);

    /* Exactly the datagram length is enough. */
    assert(net_udp_socket_receive(&t, 1, handle, output, 3, &info) == 0);
    assert(info.source_address == 0xc0000201U);
    assert(info.source_port == 40000);
    assert(info.length == 3);
    assert(output[0] == 'u' && output[1] == 'd' && output[2] == 'p');
    assert(net_udp_socket_receive(&t, 1, handle, output, sizeof(output),
                                  &info) == 1);

    /* A foreign owner cannot drain the queue. */
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 40000, 8080, data, 3) ==
           0);
    assert(net_udp_socket_receive(&t, 2, handle, output, sizeof(output),
                                  &info) == -1);
    assert(net_udp_socket_receive(&t, 1, handle, output, sizeof(output),
                                  &info) == 0);
}

static void test_queue_wraparound(void) {
    struct net_udp_socket_table t;
    uint64_t handle = 0;
    uint8_t src[16], dst[16], output[8], data[1];
    struct net_udp_receive_info info;

    net_udp_socket_table_init(&t);
    assert(net_udp_socket_bind(&t, 1, 0, 9000, &handle) == 0);
    mapped(src, 0xc0000201U);
    mapped(dst, 0xc0000202U);

    /* Two full fill/drain cycles: the ring indices must wrap cleanly and the
     * FIFO order must survive. */
    for (uint32_t cycle = 0; cycle < 2; ++cycle) {
        for (uint32_t i = 0; i < NET_UDP_SOCKET_QUEUE_DEPTH; ++i) {
            data[0] = (uint8_t)(0x40 + cycle * 8 + i);
            assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 1000, 9000,
                                           data, 1) == 0);
        }
        for (uint32_t i = 0; i < NET_UDP_SOCKET_QUEUE_DEPTH; ++i) {
            assert(net_udp_socket_receive(&t, 1, handle, output,
                                          sizeof(output), &info) == 0);
            assert(info.length == 1);
            assert(output[0] == (uint8_t)(0x40 + cycle * 8 + i));
        }
        assert(net_udp_socket_receive(&t, 1, handle, output, sizeof(output),
                                      &info) == 1);
    }
    assert(t.dropped == 0);
    assert(t.delivered == 2U * NET_UDP_SOCKET_QUEUE_DEPTH);
}

static void test_dispatch_drop_accounting(void) {
    struct net_udp_socket_table t;
    uint64_t handle = 0;
    uint8_t src[16], dst[16], data[3] = {'a', 'b', 'c'};

    net_udp_socket_table_init(&t);
    assert(net_udp_socket_bind(&t, 1, 0, 8080, &handle) == 0);
    mapped(src, 0xc0000201U);
    mapped(dst, 0xc0000202U);

    /* The queue fills; the next datagram is dropped newest-first. */
    for (uint32_t i = 0; i < NET_UDP_SOCKET_QUEUE_DEPTH; ++i)
        assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 8080, data, 3) ==
               0);
    assert(t.delivered == NET_UDP_SOCKET_QUEUE_DEPTH);
    assert(t.dropped == 0);

    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 8080, data, 3) == 0);
    assert(t.dropped == 1);
    assert(t.slots[slot_of(handle) - 1U].dropped == 1);
    /* A drop still reports success: the RX path never blocks. */
    assert(t.delivered == NET_UDP_SOCKET_QUEUE_DEPTH);

    /* An unbound destination port counts as unmatched, not dropped. */
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 9, data, 3) == 0);
    assert(t.unmatched == 1);
}

static void test_dispatch_address_matching(void) {
    struct net_udp_socket_table t;
    uint64_t specific = 0, wildcard = 0;
    uint8_t src[16], dst[16], other[16], data[3] = {'a', 'b', 'c'};
    struct net_udp_receive_info info;
    uint8_t output[8];

    net_udp_socket_table_init(&t);
    assert(net_udp_socket_bind(&t, 1, 0xc0000202U, 7000, &specific) == 0);
    assert(net_udp_socket_bind(&t, 1, 0, 7001, &wildcard) == 0);
    mapped(src, 0xc0000201U);
    mapped(dst, 0xc0000202U);
    mapped(other, 0xc0000203U);

    /* A specific endpoint accepts only its own address. */
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 7000, data, 3) == 0);
    assert(t.delivered == 1);
    assert(net_udp_socket_receive(&t, 1, specific, output, sizeof(output),
                                  &info) == 0);

    assert(net_udp_socket_dispatch(&t, 4, 1, src, other, 53, 7000, data, 3) ==
           0);
    assert(t.unmatched == 1);

    /* A wildcard endpoint accepts any local destination. */
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 7001, data, 3) == 0);
    assert(net_udp_socket_dispatch(&t, 4, 1, src, other, 53, 7001, data, 3) ==
           0);
    assert(t.delivered == 3);

    /* A datagram addressed to a different local address is not delivered to
     * the specific endpoint even when the port matches. */
    assert(net_udp_socket_receive(&t, 1, specific, output, sizeof(output),
                                  &info) == 1);
    assert(net_udp_socket_receive(&t, 1, wildcard, output, sizeof(output),
                                  &info) == 0);
    assert(info.source_address == 0xc0000201U);
}

static void test_dispatch_validation(void) {
    struct net_udp_socket_table t;
    uint64_t handle = 0;
    uint8_t src[16], dst[16], data[3] = {'a', 'b', 'c'};

    net_udp_socket_table_init(&t);
    assert(net_udp_socket_bind(&t, 1, 0, 8080, &handle) == 0);
    mapped(src, 0xc0000201U);
    mapped(dst, 0xc0000202U);

    /* Wrong family. */
    assert(net_udp_socket_dispatch(&t, 6, 1, src, dst, 53, 8080, data, 3) == -1);
    assert(net_udp_socket_dispatch(&t, 0, 1, src, dst, 53, 8080, data, 3) == -1);
    /* Interface index 0 is not a real interface. */
    assert(net_udp_socket_dispatch(&t, 4, 0, src, dst, 53, 8080, data, 3) == -1);
    /* Missing address buffers. */
    assert(net_udp_socket_dispatch(&t, 4, 1, 0, dst, 53, 8080, data, 3) == -1);
    assert(net_udp_socket_dispatch(&t, 4, 1, src, 0, 53, 8080, data, 3) == -1);
    /* Wildcard ports. */
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 0, 8080, data, 3) == -1);
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 0, data, 3) == -1);
    /* A non-IPv4-mapped address (not ::ffff:a.b.c.d). */
    src[0] = 1;
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 8080, data, 3) == -1);
    mapped(src, 0xc0000201U);
    src[10] = 0x00;
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 8080, data, 3) == -1);
    mapped(src, 0xc0000201U);
    /* A payload pointer is required whenever the length is non-zero. */
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 8080, 0, 3) == -1);
    /* Oversized datagram. */
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 8080, data,
                                   NET_UDP_SOCKET_PAYLOAD_MAX + 1U) == -1);
    /* A NULL table is rejected without faulting. */
    assert(net_udp_socket_dispatch(0, 4, 1, src, dst, 53, 8080, data, 3) == -1);

    /* A zero-length datagram with a NULL payload is legal and queueable. */
    assert(net_udp_socket_dispatch(&t, 4, 1, src, dst, 53, 8080, 0, 0) == 0);
    assert(t.delivered == 1);
}

int main(void) {
    test_table_init();
    test_bind_arguments();
    test_bind_conflicts();
    test_bind_capacity();
    test_generation_exhaustion();
    test_close();
    test_receive();
    test_queue_wraparound();
    test_dispatch_drop_accounting();
    test_dispatch_address_matching();
    test_dispatch_validation();
    return 0;
}
