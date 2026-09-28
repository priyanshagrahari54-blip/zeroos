#include <assert.h>
#include <string.h>
#include "../kernel/dhcp_core.h"

static void test_dhcp_null_and_bounds(void) {
    struct dhcp_view v;
    uint8_t p[244] = {0};

    p[0] = 2;
    p[1] = 1;
    p[2] = 6;
    p[236] = 0x63; p[237] = 0x82; p[238] = 0x53; p[239] = 0x63;
    p[240] = 53; p[241] = 1; p[242] = 2;
    p[243] = 255;

    assert(dhcp_parse(0, sizeof(p), &v) == -1);
    assert(dhcp_parse(p, sizeof(p), 0) == -1);
    assert(dhcp_parse(p, 239, &v) == -1);

    /* Invalid hardware length */
    p[2] = 0;
    assert(dhcp_parse(p, sizeof(p), &v) == -1);
    p[2] = 17;
    assert(dhcp_parse(p, sizeof(p), &v) == -1);
    p[2] = 6;

    /* Invalid magic cookie */
    p[236] = 0x00;
    assert(dhcp_parse(p, sizeof(p), &v) == -1);
    p[236] = 0x63;

    /* Valid parse */
    assert(dhcp_parse(p, sizeof(p), &v) == 0);
}

static void test_dhcp_options_parsing(void) {
    struct dhcp_view v;
    uint8_t p[300] = {0};

    p[0] = 2; /* Boot reply */
    p[1] = 1; /* Ethernet */
    p[2] = 6; /* 6-byte MAC */
    p[4] = 0x12; p[5] = 0x34; p[6] = 0x56; p[7] = 0x78; /* XID */
    p[16] = 192; p[17] = 168; p[18] = 1; p[19] = 50;   /* Your IP */
    p[236] = 0x63; p[237] = 0x82; p[238] = 0x53; p[239] = 0x63; /* Magic */

    uint32_t idx = 240;
    /* Option 53: Message type 5 (ACK) */
    p[idx++] = 53; p[idx++] = 1; p[idx++] = 5;
    /* Option 54: Server ID (192.168.1.1) */
    p[idx++] = 54; p[idx++] = 4;
    p[idx++] = 192; p[idx++] = 168; p[idx++] = 1; p[idx++] = 1;
    /* Option 1: Subnet mask (255.255.255.0) */
    p[idx++] = 1; p[idx++] = 4;
    p[idx++] = 255; p[idx++] = 255; p[idx++] = 255; p[idx++] = 0;
    /* Option 51: Lease time (86400 = 0x00015180) */
    p[idx++] = 51; p[idx++] = 4;
    p[idx++] = 0x00; p[idx++] = 0x01; p[idx++] = 0x51; p[idx++] = 0x80;
    /* Option 58: Renewal time (43200 = 0x0000A8C0) */
    p[idx++] = 58; p[idx++] = 4;
    p[idx++] = 0x00; p[idx++] = 0x00; p[idx++] = 0xa8; p[idx++] = 0xc0;
    /* Option 59: Rebind time (75600 = 0x00012750) */
    p[idx++] = 59; p[idx++] = 4;
    p[idx++] = 0x00; p[idx++] = 0x01; p[idx++] = 0x27; p[idx++] = 0x50;
    /* Option 255: End */
    p[idx++] = 255;

    memset(&v, 0, sizeof(v));
    assert(dhcp_parse(p, idx, &v) == 0);
    assert(v.op == 2);
    assert(v.xid == 0x12345678U);
    assert(v.your_ip == 0xc0a80132U);
    assert(v.message_type == 5);
    assert(v.server_identifier == 0xc0a80101U);
    assert(v.subnet_mask == 0xffffff00U);
    assert(v.lease_seconds == 86400U);
    assert(v.renewal_seconds == 43200U);
    assert(v.rebind_seconds == 75600U);
}

static void test_dhcp_malformed_options(void) {
    struct dhcp_view v;
    uint8_t p[260] = {0};

    p[0] = 2; p[1] = 1; p[2] = 6;
    p[236] = 0x63; p[237] = 0x82; p[238] = 0x53; p[239] = 0x63;

    /* Duplicate option 53 */
    p[240] = 53; p[241] = 1; p[242] = 2;
    p[243] = 53; p[244] = 1; p[245] = 2;
    p[246] = 255;
    assert(dhcp_parse(p, 247, &v) == -1);

    /* Truncated option: length says 4 but only 2 bytes present before end */
    p[243] = 54; p[244] = 4; p[245] = 1; p[246] = 2;
    assert(dhcp_parse(p, 247, &v) == -1);

    /* No end option (missing 255) */
    p[240] = 53; p[241] = 1; p[242] = 2;
    assert(dhcp_parse(p, 243, &v) == -1);
}

int main(void) {
    test_dhcp_null_and_bounds();
    test_dhcp_options_parsing();
    test_dhcp_malformed_options();
    return 0;
}
