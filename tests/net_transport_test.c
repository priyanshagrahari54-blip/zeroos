/* Host test for kernel/net_transport.c: the UDP header parser and the TCP
 * connection state machine (including its retransmit timer). */
#include <assert.h>
#include <string.h>
#include "../kernel/net_transport.h"

#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_ACK 0x10u

/* ------------------------------------------------------------------ */
/* UDP                                                                 */
/* ------------------------------------------------------------------ */

static uint32_t build_udp(uint8_t *s, uint16_t sport, uint16_t dport,
                          uint16_t length) {
    memset(s, 0, 64);
    s[0] = (uint8_t)(sport >> 8);
    s[1] = (uint8_t)sport;
    s[2] = (uint8_t)(dport >> 8);
    s[3] = (uint8_t)dport;
    s[4] = (uint8_t)(length >> 8);
    s[5] = (uint8_t)length;
    return length;
}

static void test_udp_null_and_bounds(void) {
    uint8_t u[64];
    struct net_udp_view v;

    build_udp(u, 53, 7, 11);
    memset(&v, 0, sizeof(v));

    assert(net_udp_parse(0, 11, &v) == -1);
    assert(net_udp_parse(u, 11, 0) == -1);
    assert(net_udp_parse(u, 7, &v) == -1);
    assert(net_udp_parse(u, 0, &v) == -1);
}

static void test_udp_fields(void) {
    uint8_t u[64];
    struct net_udp_view v;

    build_udp(u, 53, 7, 11);
    memset(&v, 0, sizeof(v));
    assert(net_udp_parse(u, sizeof(u), &v) == 0);
    assert(v.source_port == 53);
    assert(v.destination_port == 7);
    assert(v.length == 11);
    assert(v.payload_length == 3);
    assert(v.payload == u + 8);

    /* A header-only datagram has no payload. */
    build_udp(u, 1024, 80, 8);
    memset(&v, 0, sizeof(v));
    assert(net_udp_parse(u, sizeof(u), &v) == 0);
    assert(v.length == 8);
    assert(v.payload_length == 0);
    assert(v.payload == u + 8);

    /* Well-known port extremes. */
    build_udp(u, 0, 0xffffU, 8);
    assert(net_udp_parse(u, sizeof(u), &v) == 0);
    assert(v.source_port == 0);
    assert(v.destination_port == 0xffffU);
}

static void test_udp_length_relations(void) {
    uint8_t u[64];
    struct net_udp_view v;

    /* A length field below the fixed header is contradictory. */
    build_udp(u, 53, 7, 7);
    assert(net_udp_parse(u, sizeof(u), &v) == -1);
    build_udp(u, 53, 7, 0);
    assert(net_udp_parse(u, sizeof(u), &v) == -1);

    /* A length field beyond the captured bytes over-reads. */
    build_udp(u, 53, 7, 12);
    assert(net_udp_parse(u, 11, &v) == -1);
    /* Exactly the available bytes is fine. */
    assert(net_udp_parse(u, 12, &v) == 0);
    assert(v.payload_length == 4);
}

/* ------------------------------------------------------------------ */
/* TCP state machine                                                   */
/* ------------------------------------------------------------------ */

static void test_tcp_null_and_reset(void) {
    struct net_tcp_conn c;

    assert(net_tcp_input(0, TCP_SYN, 1, 0, 10, 5) == -1);

    /* RST closes from any state and reports no progress. */
    {
        enum net_tcp_state states[] = {
            TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RECEIVED, TCP_ESTABLISHED,
            TCP_FIN_WAIT_1, TCP_FIN_WAIT_2, TCP_CLOSE_WAIT, TCP_CLOSING,
            TCP_LAST_ACK, TCP_TIME_WAIT
        };
        for (uint32_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
            memset(&c, 0, sizeof(c));
            c.state = states[i];
            assert(net_tcp_input(&c, TCP_RST, 0, 0, 10, 5) == 0);
            assert(c.state == TCP_CLOSED);
        }
    }

    /* RST takes precedence over every other flag combination. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_ESTABLISHED;
    assert(net_tcp_input(&c, (uint8_t)(TCP_RST | TCP_ACK | TCP_FIN | TCP_SYN),
                         0, 0, 10, 5) == 0);
    assert(c.state == TCP_CLOSED);
}

static void test_tcp_open(void) {
    struct net_tcp_conn c;

    /* Passive open: LISTEN --SYN--> SYN_RECEIVED --ACK--> ESTABLISHED. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_LISTEN;
    c.snd_nxt = 100;
    assert(net_tcp_input(&c, TCP_SYN, 40, 0, 10, 5) == 1);
    assert(c.state == TCP_SYN_RECEIVED);
    assert(c.rcv_nxt == 41);
    assert(c.snd_nxt == 101);
    assert(c.deadline == 15);
    assert(c.retries == 0);

    /* The completing ACK must carry the right sequence and acknowledgement. */
    assert(net_tcp_input(&c, TCP_ACK, 41, 100, 11, 5) == -1); /* ack too low */
    assert(c.state == TCP_SYN_RECEIVED);
    assert(net_tcp_input(&c, TCP_ACK, 42, 101, 11, 5) == -1); /* wrong seq */
    assert(c.state == TCP_SYN_RECEIVED);
    assert(net_tcp_input(&c, TCP_ACK, 41, 101, 11, 5) == 1);
    assert(c.state == TCP_ESTABLISHED);

    /* Active open: SYN_SENT --SYN|ACK--> ESTABLISHED. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_SYN_SENT;
    c.snd_nxt = 500;
    /* A bare SYN (no ACK) does not complete the handshake. */
    assert(net_tcp_input(&c, TCP_SYN, 900, 0, 10, 5) == -1);
    assert(c.state == TCP_SYN_SENT);
    /* A SYN|ACK acknowledging the wrong sequence number is ignored. */
    assert(net_tcp_input(&c, (uint8_t)(TCP_SYN | TCP_ACK), 900, 499, 10, 5) ==
           -1);
    assert(c.state == TCP_SYN_SENT);
    assert(net_tcp_input(&c, (uint8_t)(TCP_SYN | TCP_ACK), 900, 500, 10, 5) ==
           1);
    assert(c.state == TCP_ESTABLISHED);
    assert(c.rcv_nxt == 901);
    assert(c.snd_una == 500);
}

static void test_tcp_established_data(void) {
    struct net_tcp_conn c;

    memset(&c, 0, sizeof(c));
    c.state = TCP_ESTABLISHED;
    c.rcv_nxt = 1000;
    c.snd_nxt = 5000;

    /* In-order data: accepted, no state change. */
    assert(net_tcp_input(&c, TCP_ACK, 1000, 5000, 10, 5) == 0);
    assert(c.state == TCP_ESTABLISHED);

    /* Out-of-order data is reported distinctly. */
    assert(net_tcp_input(&c, TCP_ACK, 1001, 5000, 10, 5) == -2);
    assert(net_tcp_input(&c, TCP_ACK, 999, 5000, 10, 5) == -2);
    assert(c.state == TCP_ESTABLISHED);

    /* A bare segment (no flags at all) is still sequence-checked. */
    assert(net_tcp_input(&c, 0, 1000, 0, 10, 5) == 0);
    assert(net_tcp_input(&c, 0, 1002, 0, 10, 5) == -2);

    /* FIN moves to CLOSE_WAIT and consumes one sequence number. */
    assert(net_tcp_input(&c, (uint8_t)(TCP_FIN | TCP_ACK), 1000, 5000, 11, 5) ==
           1);
    assert(c.state == TCP_CLOSE_WAIT);
    assert(c.rcv_nxt == 1001);

    /* Nothing further is accepted in CLOSE_WAIT. */
    assert(net_tcp_input(&c, TCP_ACK, 1001, 5000, 12, 5) == -1);
    assert(c.state == TCP_CLOSE_WAIT);
}

static void test_tcp_close_paths(void) {
    struct net_tcp_conn c;

    /* FIN_WAIT_1 --ACK--> FIN_WAIT_2 --FIN--> TIME_WAIT. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_FIN_WAIT_1;
    c.snd_nxt = 700;
    c.rcv_nxt = 300;
    assert(net_tcp_input(&c, TCP_ACK, 300, 699, 10, 5) == 0);
    assert(c.state == TCP_FIN_WAIT_1);
    assert(net_tcp_input(&c, TCP_ACK, 300, 700, 10, 5) == 1);
    assert(c.state == TCP_FIN_WAIT_2);

    /* A non-FIN, non-matching segment in FIN_WAIT_2 changes nothing. */
    assert(net_tcp_input(&c, 0, 300, 700, 11, 5) == -1);
    assert(c.state == TCP_FIN_WAIT_2);
    assert(net_tcp_input(&c, TCP_FIN, 300, 700, 11, 5) == 1);
    assert(c.state == TCP_TIME_WAIT);
    assert(c.rcv_nxt == 301);

    /* Simultaneous close: FIN_WAIT_1 --FIN--> TIME_WAIT directly. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_FIN_WAIT_1;
    c.snd_nxt = 700;
    c.rcv_nxt = 300;
    assert(net_tcp_input(&c, TCP_FIN, 300, 700, 10, 5) == 1);
    assert(c.state == TCP_TIME_WAIT);
    assert(c.rcv_nxt == 301);

    /* CLOSING --ACK--> TIME_WAIT. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_CLOSING;
    c.snd_nxt = 900;
    assert(net_tcp_input(&c, TCP_ACK, 0, 899, 10, 5) == 0);
    assert(c.state == TCP_CLOSING);
    assert(net_tcp_input(&c, TCP_ACK, 0, 900, 10, 5) == 1);
    assert(c.state == TCP_TIME_WAIT);

    /* LAST_ACK --ACK--> CLOSED. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_LAST_ACK;
    c.snd_nxt = 400;
    assert(net_tcp_input(&c, TCP_ACK, 0, 401, 10, 5) == 0);
    assert(c.state == TCP_LAST_ACK);
    assert(net_tcp_input(&c, TCP_ACK, 0, 400, 10, 5) == 1);
    assert(c.state == TCP_CLOSED);

    /* Terminal states reject everything but RST. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_TIME_WAIT;
    assert(net_tcp_input(&c, TCP_ACK, 0, 0, 10, 5) == -1);
    assert(net_tcp_input(&c, TCP_FIN, 0, 0, 10, 5) == -1);
    assert(c.state == TCP_TIME_WAIT);

    memset(&c, 0, sizeof(c));
    c.state = TCP_CLOSED;
    assert(net_tcp_input(&c, TCP_SYN, 0, 0, 10, 5) == -1);
    assert(c.state == TCP_CLOSED);

    /* LISTEN ignores anything that is not a SYN. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_LISTEN;
    assert(net_tcp_input(&c, TCP_ACK, 0, 0, 10, 5) == -1);
    assert(net_tcp_input(&c, 0, 0, 0, 10, 5) == -1);
    assert(c.state == TCP_LISTEN);
}

static void test_tcp_timeout(void) {
    struct net_tcp_conn c;

    assert(net_tcp_timeout(0, 10, 1, 5) == -1);

    /* A zero interval cannot rearm the timer. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_SYN_SENT;
    c.deadline = 5;
    assert(net_tcp_timeout(&c, 10, 1, 0) == -1);

    /* States with no timer work report 0 without touching the deadline. */
    {
        enum net_tcp_state quiet[] = { TCP_CLOSED, TCP_ESTABLISHED,
                                       TCP_LISTEN, TCP_TIME_WAIT };
        for (uint32_t i = 0; i < sizeof(quiet) / sizeof(quiet[0]); ++i) {
            memset(&c, 0, sizeof(c));
            c.state = quiet[i];
            c.deadline = 1;
            assert(net_tcp_timeout(&c, 1000, 0, 5) == 0);
            assert(c.deadline == 1);
            assert(c.retries == 0);
            assert(c.state == quiet[i]);
        }
    }

    /* Before the deadline nothing fires. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_SYN_SENT;
    c.deadline = 20;
    assert(net_tcp_timeout(&c, 19, 3, 5) == 0);
    assert(c.retries == 0);
    assert(c.state == TCP_SYN_SENT);

    /* At the deadline a retry is armed and the deadline pushed out. */
    assert(net_tcp_timeout(&c, 20, 3, 5) == 1);
    assert(c.retries == 1);
    assert(c.deadline == 25);
    assert(net_tcp_timeout(&c, 24, 3, 5) == 0);
    assert(c.retries == 1);
    assert(net_tcp_timeout(&c, 25, 3, 5) == 1);
    assert(c.retries == 2);
    assert(c.deadline == 30);
    assert(net_tcp_timeout(&c, 30, 3, 5) == 1);
    assert(c.retries == 3);
    assert(c.deadline == 35);

    /* The limit is exhausted: the connection closes. */
    assert(net_tcp_timeout(&c, 35, 3, 5) == -1);
    assert(c.state == TCP_CLOSED);
    assert(c.retries == 3);

    /* A zero limit means the first deadline closes immediately. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_SYN_RECEIVED;
    c.deadline = 10;
    assert(net_tcp_timeout(&c, 10, 0, 5) == -1);
    assert(c.state == TCP_CLOSED);

    /* A SYN_RECEIVED connection that completes clears the timer pressure:
     * ESTABLISHED never times out. */
    memset(&c, 0, sizeof(c));
    c.state = TCP_LISTEN;
    c.snd_nxt = 100;
    assert(net_tcp_input(&c, TCP_SYN, 40, 0, 10, 5) == 1);
    assert(c.deadline == 15);
    assert(net_tcp_input(&c, TCP_ACK, 41, 101, 11, 5) == 1);
    assert(c.state == TCP_ESTABLISHED);
    assert(net_tcp_timeout(&c, 1000, 0, 5) == 0);
    assert(c.state == TCP_ESTABLISHED);
}

int main(void) {
    test_udp_null_and_bounds();
    test_udp_fields();
    test_udp_length_relations();
    test_tcp_null_and_reset();
    test_tcp_open();
    test_tcp_established_data();
    test_tcp_close_paths();
    test_tcp_timeout();
    return 0;
}
