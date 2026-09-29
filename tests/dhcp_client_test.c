#include <assert.h>
#include <string.h>
#include "../kernel/dhcp_core.h"

/* Builds a well-formed DHCP reply with the given message type plus the
 * options the state machine needs to advance. */
static void base(uint8_t *p, uint8_t type) {
    for (unsigned i = 0; i < 260; i++)
        p[i] = 0;
    p[0] = 2;   /* boot reply */
    p[1] = 1;   /* ethernet */
    p[2] = 6;   /* 6-byte hardware address */
    p[7] = 1;   /* flags: broadcast */
    p[16] = 192; p[17] = 0; p[18] = 2; p[19] = 44;  /* your IP */
    p[236] = 0x63; p[237] = 0x82; p[238] = 0x53; p[239] = 0x63; /* magic */
    p[240] = 53; p[241] = 1; p[242] = type;
}

static void add_server_id(uint8_t *p, uint32_t at) {
    p[at] = 54; p[at + 1] = 4;
    p[at + 2] = 192; p[at + 3] = 0; p[at + 4] = 2; p[at + 5] = 1;
}

static void add_lease_times(uint8_t *p, uint32_t at, uint32_t lease,
                            uint32_t t1, uint32_t t2) {
    p[at] = 51; p[at + 1] = 4;
    p[at + 2] = (uint8_t)(lease >> 24); p[at + 3] = (uint8_t)(lease >> 16);
    p[at + 4] = (uint8_t)(lease >> 8);  p[at + 5] = (uint8_t)lease;
    p[at + 6] = 58; p[at + 7] = 4;
    p[at + 8] = (uint8_t)(t1 >> 24); p[at + 9] = (uint8_t)(t1 >> 16);
    p[at + 10] = (uint8_t)(t1 >> 8); p[at + 11] = (uint8_t)t1;
    p[at + 12] = 59; p[at + 13] = 4;
    p[at + 14] = (uint8_t)(t2 >> 24); p[at + 15] = (uint8_t)(t2 >> 16);
    p[at + 16] = (uint8_t)(t2 >> 8); p[at + 17] = (uint8_t)t2;
}

static void test_client_start(void) {
    struct dhcp_client c;

    memset(&c, 0, sizeof(c));
    assert(dhcp_client_start(0, 1, 100) == -1);
    /* A zero transaction id is not a usable identity. */
    assert(dhcp_client_start(&c, 0, 100) == -1);

    assert(dhcp_client_start(&c, 1, 100) == 0);
    assert(c.state == DHCP_SELECTING);
    assert(c.xid == 1);
    assert(c.address == 0 && c.server == 0);
    assert(c.lease_seconds == 0);
    assert(c.deadline == 100);
    assert(c.retries == 0);

    /* Restarting resets a bound client back to the discovery phase. */
    c.state = DHCP_BOUND;
    c.address = 0xc0a80101U;
    assert(dhcp_client_start(&c, 2, 200) == 0);
    assert(c.state == DHCP_SELECTING);
    assert(c.xid == 2);
    assert(c.address == 0);
    assert(c.deadline == 200);
}

static void test_client_nak_returns_to_selecting(void) {
    uint8_t p[270];
    struct dhcp_client c = {0};

    base(p, 2);
    add_server_id(p, 243);
    p[249] = 255;

    assert(dhcp_client_start(&c, 1, 100) == 0);
    assert(dhcp_client_tick(&c, 100, 5, 3) == DHCP_ACTION_DISCOVER);
    assert(dhcp_client_receive(&c, p, 250, 101) == 1);
    assert(c.state == DHCP_REQUESTING);
    assert(c.address == 0xc000022cU);

    /* A NAK from any requesting state drops the offer and restarts. */
    base(p, 6);
    add_server_id(p, 243);
    p[249] = 255;
    assert(dhcp_client_receive(&c, p, 250, 102) == 2);
    assert(c.state == DHCP_SELECTING);
    assert(c.address == 0 && c.server == 0);
    assert(c.retries == 0);

    /* A NAK while idle in SELECTING is not consumed as a transition. */
    assert(dhcp_client_receive(&c, p, 250, 103) == -1);
    assert(c.state == DHCP_SELECTING);
}

static void test_client_rejects_mismatched_packets(void) {
    uint8_t p[270];
    struct dhcp_client c = {0};

    base(p, 2);
    add_server_id(p, 243);
    p[249] = 255;

    assert(dhcp_client_start(&c, 0x1234, 100) == 0);

    /* Wrong transaction id */
    assert(dhcp_client_receive(&c, p, 250, 101) == -1);

    /* Right transaction id: xid is the big-endian word at p[4..7], so it is
     * written through all four bytes rather than overlapping the flag at p[7]. */
    p[4] = 0; p[5] = 0; p[6] = 0x12; p[7] = 0x34;
    assert(dhcp_client_receive(&c, p, 250, 101) == 1);
    assert(c.state == DHCP_REQUESTING);

    /* NULL arguments */
    assert(dhcp_client_receive(0, p, 250, 101) == -1);
    assert(dhcp_client_receive(&c, 0, 250, 101) == -1);

    /* A malformed packet is rejected without changing state. */
    p[4] = 0x12; p[5] = 0x34; p[236] = 0;
    assert(dhcp_client_receive(&c, p, 250, 102) == -1);
    assert(c.state == DHCP_REQUESTING);
    p[236] = 0x63;

    /* A request (op 1) is not a reply and is ignored. */
    p[0] = 1;
    assert(dhcp_client_receive(&c, p, 250, 103) == -1);
    p[0] = 2;

    /* An ACK without a lease time cannot advance to BOUND. */
    base(p, 5);
    p[4] = 0x12; p[5] = 0x34;
    add_server_id(p, 243);
    p[249] = 255;
    assert(dhcp_client_receive(&c, p, 250, 104) == -1);
    assert(c.state == DHCP_REQUESTING);

    /* An ACK without your_ip is equally unusable. */
    base(p, 5);
    p[4] = 0x12; p[5] = 0x34;
    p[16] = 0; p[17] = 0; p[18] = 0; p[19] = 0;
    add_server_id(p, 243);
    add_lease_times(p, 249, 100, 50, 87);
    p[267] = 255;
    assert(dhcp_client_receive(&c, p, 268, 105) == -1);
    assert(c.state == DHCP_REQUESTING);
}

static void test_client_rejects_degenerate_lease_timers(void) {
    uint8_t p[270];
    struct dhcp_client c = {0};

    assert(dhcp_client_start(&c, 0x99, 100) == 0);
    assert(dhcp_client_tick(&c, 100, 5, 3) == DHCP_ACTION_DISCOVER);

    /* Get to REQUESTING with a valid offer. */
    base(p, 2);
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x99;
    add_server_id(p, 243);
    p[249] = 255;
    assert(dhcp_client_receive(&c, p, 250, 101) == 1);
    assert(c.state == DHCP_REQUESTING);

    /* A lease whose top bit is set would make the expiry comparison wrap. */
    base(p, 5);
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x99;
    add_server_id(p, 243);
    add_lease_times(p, 249, 0x80000000U, 50, 87);
    p[267] = 255;
    assert(dhcp_client_receive(&c, p, 268, 102) == -1);
    assert(c.state == DHCP_REQUESTING);

    /* T1 must be strictly before T2 and T2 strictly before the lease. */
    base(p, 5);
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x99;
    add_server_id(p, 243);
    add_lease_times(p, 249, 100, 100, 100);
    p[267] = 255;
    assert(dhcp_client_receive(&c, p, 268, 103) == -1);

    /* T2 >= lease is rejected. */
    base(p, 5);
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x99;
    add_server_id(p, 243);
    add_lease_times(p, 249, 100, 50, 100);
    p[267] = 255;
    assert(dhcp_client_receive(&c, p, 268, 104) == -1);
    assert(c.state == DHCP_REQUESTING);

    /* A sane set of timers is accepted. */
    base(p, 5);
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x99;
    add_server_id(p, 243);
    add_lease_times(p, 249, 100, 50, 87);
    p[267] = 255;
    assert(dhcp_client_receive(&c, p, 268, 105) == 3);
    assert(c.state == DHCP_BOUND);
    assert(c.lease_start == 105);
    assert(c.lease_seconds == 100);
    assert(c.renewal_seconds == 50);
    assert(c.rebind_seconds == 87);
    assert(c.deadline == 155);
}

static void test_client_default_timers(void) {
    uint8_t p[270];
    struct dhcp_client c = {0};

    assert(dhcp_client_start(&c, 7, 100) == 0);

    /* Offer then ACK with no T1/T2 options: the client derives T1 = lease/2
     * and T2 = lease - lease/8. */
    base(p, 2);
    p[7] = 7;
    add_server_id(p, 243);
    p[249] = 255;
    assert(dhcp_client_receive(&c, p, 250, 100) == 1);

    base(p, 5);
    p[7] = 7;
    add_server_id(p, 243);
    p[243] = 51; p[244] = 4;
    p[245] = 0; p[246] = 0; p[247] = 3; p[248] = 0xE8; /* lease 1000 */
    p[249] = 255;
    assert(dhcp_client_receive(&c, p, 250, 110) == 3);
    assert(c.state == DHCP_BOUND);
    assert(c.lease_seconds == 1000);
    assert(c.renewal_seconds == 500);   /* 1000/2 */
    assert(c.rebind_seconds == 875);    /* 1000 - 1000/8 */
    assert(c.deadline == 610);
}

static void test_client_retry_ladder(void) {
    struct dhcp_client c = {0};

    assert(dhcp_client_start(&c, 1, 100) == 0);

    /* Before the deadline nothing is sent. */
    assert(dhcp_client_tick(&c, 99, 5, 3) == DHCP_ACTION_NONE);

    /* Each tick at or past the deadline sends one retry and re-arms. */
    assert(dhcp_client_tick(&c, 100, 5, 3) == DHCP_ACTION_DISCOVER);
    assert(c.retries == 1 && c.deadline == 105);
    assert(dhcp_client_tick(&c, 104, 5, 3) == DHCP_ACTION_NONE);
    assert(dhcp_client_tick(&c, 105, 5, 3) == DHCP_ACTION_DISCOVER);
    assert(c.retries == 2 && c.deadline == 110);
    assert(dhcp_client_tick(&c, 110, 5, 3) == DHCP_ACTION_DISCOVER);
    assert(c.retries == 3 && c.deadline == 115);

    /* The limit is exhausted: no more transmissions and the client fails. */
    assert(dhcp_client_tick(&c, 115, 5, 3) == DHCP_ACTION_NONE);
    assert(c.state == DHCP_FAILED);
    assert(c.retries == 3);

    /* A failed client stays quiet. */
    assert(dhcp_client_tick(&c, 200, 5, 3) == DHCP_ACTION_NONE);
    assert(c.state == DHCP_FAILED);

    /* An idle (never-started) client never transmits. */
    memset(&c, 0, sizeof(c));
    assert(dhcp_client_tick(&c, 100, 5, 3) == DHCP_ACTION_NONE);
    assert(dhcp_client_tick(0, 100, 5, 3) == DHCP_ACTION_NONE);

    /* A zero or out-of-range interval disables the ladder entirely. */
    assert(dhcp_client_start(&c, 1, 100) == 0);
    assert(dhcp_client_tick(&c, 100, 0, 3) == DHCP_ACTION_NONE);
    assert(c.retries == 0);
    assert(dhcp_client_tick(&c, 100, 0x80000000U, 3) == DHCP_ACTION_NONE);
    assert(c.retries == 0);
}

static void test_client_full_lifecycle(void) {
    uint8_t p[270];
    struct dhcp_client c = {0};

    base(p, 2);
    p[243] = 54; p[244] = 4; p[245] = 192; p[246] = 0; p[247] = 2; p[248] = 1;
    p[249] = 255;
    assert(dhcp_client_start(&c, 1, 100) == 0);
    assert(dhcp_client_tick(&c, 100, 5, 3) == DHCP_ACTION_DISCOVER);
    assert(dhcp_client_receive(&c, p, 250, 101) == 1);
    assert(c.state == DHCP_REQUESTING);
    assert(c.address == 0xc000022cU);
    assert(dhcp_client_tick(&c, 101, 5, 3) == DHCP_ACTION_REQUEST);

    base(p, 5);
    p[243] = 54; p[244] = 4; p[245] = 192; p[246] = 0; p[247] = 2; p[248] = 1;
    p[249] = 51; p[250] = 4; p[251] = 0; p[252] = 0; p[253] = 0; p[254] = 100;
    p[255] = 58; p[256] = 4; p[257] = 0; p[258] = 0; p[259] = 0;
    /* truncated options must be rejected */
    assert(dhcp_client_receive(&c, p, sizeof(p), 110) == -1);

    uint8_t a[270] = {0};
    base(a, 5);
    a[243] = 54; a[244] = 4; a[245] = 192; a[246] = 0; a[247] = 2; a[248] = 1;
    a[249] = 51; a[250] = 4; a[251] = 0; a[252] = 0; a[253] = 0; a[254] = 100;
    a[255] = 58; a[256] = 4; a[257] = 0; a[258] = 0; a[259] = 0;
    a[260] = 50; a[261] = 59; a[262] = 4; a[263] = 0; a[264] = 0; a[265] = 0;
    a[266] = 87; a[267] = 255;
    assert(dhcp_client_receive(&c, a, sizeof(a), 110) == 3);
    assert(c.state == DHCP_BOUND);

    /* Quiet through the renewal interval. */
    assert(dhcp_client_tick(&c, 159, 5, 3) == DHCP_ACTION_NONE);
    assert(dhcp_client_tick(&c, 160, 5, 3) == DHCP_ACTION_RENEW);
    assert(c.state == DHCP_RENEWING);

    /* Renewal retransmits on its own ladder. */
    assert(dhcp_client_tick(&c, 165, 5, 3) == DHCP_ACTION_RENEW);
    assert(dhcp_client_tick(&c, 197, 5, 3) == DHCP_ACTION_REBIND);
    assert(c.state == DHCP_REBINDING);
    assert(dhcp_client_tick(&c, 202, 5, 3) == DHCP_ACTION_REBIND);

    /* Lease expiry (110 + 100 = 210) wins over further retransmission. */
    assert(dhcp_client_tick(&c, 210, 5, 3) == DHCP_ACTION_EXPIRED);
    assert(c.state == DHCP_FAILED);
    assert(c.address == 0);
}

static void test_client_renew_rebind_rebinds(void) {
    uint8_t p[280];
    struct dhcp_client c = {0};

    /* Bind the client with lease 100 from t=110. */
    base(p, 2);
    p[7] = 3;
    add_server_id(p, 243);
    p[249] = 255;
    assert(dhcp_client_start(&c, 3, 100) == 0);
    assert(dhcp_client_receive(&c, p, 250, 110) == 1);

    base(p, 5);
    p[7] = 3;
    add_server_id(p, 243);
    add_lease_times(p, 249, 100, 50, 87);
    p[267] = 255;
    assert(dhcp_client_receive(&c, p, 268, 110) == 3);
    assert(c.state == DHCP_BOUND);
    assert(c.server == 0xc0000201U);

    /* Enter RENEWING at T1. */
    assert(dhcp_client_tick(&c, 160, 5, 3) == DHCP_ACTION_RENEW);
    assert(c.state == DHCP_RENEWING);

    /* A renewed ACK extends the lease from the new start time. */
    base(p, 5);
    p[7] = 3;
    add_server_id(p, 243);
    add_lease_times(p, 249, 100, 50, 87);
    p[267] = 255;
    assert(dhcp_client_receive(&c, p, 268, 170) == 3);
    assert(c.state == DHCP_BOUND);
    assert(c.lease_start == 170);
    assert(c.deadline == 220);
    assert(c.retries == 0);

    /* Enter REBINDING at T2 and accept an ACK from a different server. */
    assert(dhcp_client_tick(&c, 220, 5, 3) == DHCP_ACTION_RENEW);
    assert(c.state == DHCP_RENEWING);
    assert(dhcp_client_tick(&c, 257, 5, 3) == DHCP_ACTION_REBIND);
    assert(c.state == DHCP_REBINDING);

    base(p, 5);
    p[7] = 3;
    add_server_id(p, 243);
    p[248] = 9; /* different server id: 192.0.2.9 */
    add_lease_times(p, 249, 100, 50, 87);
    p[267] = 255;
    assert(dhcp_client_receive(&c, p, 268, 260) == 3);
    assert(c.state == DHCP_BOUND);
    assert(c.server == 0xc0000209U);
    assert(c.lease_start == 260);
}

int main(void) {
    test_client_start();
    test_client_nak_returns_to_selecting();
    test_client_rejects_mismatched_packets();
    test_client_rejects_degenerate_lease_timers();
    test_client_default_timers();
    test_client_retry_ladder();
    test_client_renew_rebind_rebinds();
    test_client_full_lifecycle();
    return 0;
}
