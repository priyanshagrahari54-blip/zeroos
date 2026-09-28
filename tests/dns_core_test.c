#include <assert.h>
#include <string.h>
#include "../kernel/dns_core.h"

static void test_dns_null_and_bounds(void) {
    struct dns_summary s;
    uint8_t valid_q[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 'a', 0x00, 0x00,
        0x01, 0x00, 0x01
    };

    assert(dns_validate_message(0, sizeof(valid_q), &s) == -1);
    assert(dns_validate_message(valid_q, sizeof(valid_q), 0) == -1);
    assert(dns_validate_message(valid_q, 11, &s) == -1);
    assert(dns_validate_message(valid_q, DNS_PACKET_MAX + 1, &s) == -1);
}

static void test_dns_pointer_loop(void) {
    struct dns_summary s;
    /* Question with a compression pointer pointing to itself (offset 12) */
    uint8_t loop[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01
    };
    assert(dns_validate_message(loop, sizeof(loop), &s) == -1);

    /* Compression pointer pointing past buffer end */
    uint8_t oob_ptr[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xc0, 0x50, 0x00, 0x01, 0x00, 0x01
    };
    assert(dns_validate_message(oob_ptr, sizeof(oob_ptr), &s) == -1);
}

static void test_dns_malformed_labels(void) {
    struct dns_summary s;

    /* Label length byte specifies 64 (> 63) */
    uint8_t long_label[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        64, 'x', 0x00, 0x00, 0x01, 0x00, 0x01
    };
    assert(dns_validate_message(long_label, sizeof(long_label), &s) == -1);

    /* Truncated label: length byte 10 but only 2 bytes follow */
    uint8_t trunc_label[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        10, 'a', 'b', 0x00, 0x00, 0x01, 0x00, 0x01
    };
    assert(dns_validate_message(trunc_label, sizeof(trunc_label), &s) == -1);
}

static void test_dns_valid_response_with_answer(void) {
    struct dns_summary s;

    /* DNS Response:
     * Header: ID=0xABCD, QR=1, RCODE=0, QDCOUNT=1, ANCOUNT=1, NSCOUNT=0, ARCOUNT=0
     * Question at offset 12: \x03foo\x00, QTYPE=1, QCLASS=1 (total 5 + 4 = 9 bytes, end at 21)
     * Answer at offset 21: \xc0\x0c (points to offset 12), TYPE=1, CLASS=1,
     *                      TTL=60 (0x0000003c), RDLENGTH=4, RDATA=192.168.1.1
     * Total length = 12 + 9 + (2 + 2 + 2 + 4 + 2 + 4) = 37 bytes
     */
    uint8_t response[] = {
        /* Header (12 bytes) */
        0xab, 0xcd, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        /* Question (offset 12): "foo", QTYPE=1, QCLASS=1 */
        0x03, 'f', 'o', 'o', 0x00, 0x00, 0x01, 0x00, 0x01,
        /* Answer (offset 21): ptr to 12, TYPE=1, CLASS=1, TTL=60, RDLEN=4, IP=192.168.1.1 */
        0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x04,
        192, 168, 1, 1
    };

    memset(&s, 0, sizeof(s));
    assert(dns_validate_message(response, sizeof(response), &s) == 0);
    assert(s.id == 0xabcd);
    assert(s.response == 1);
    assert(s.rcode == 0);
    assert(s.questions == 1);
    assert(s.answers == 1);
    assert(s.authorities == 0);
    assert(s.additional == 0);
}

int main(void) {
    test_dns_null_and_bounds();
    test_dns_pointer_loop();
    test_dns_malformed_labels();
    test_dns_valid_response_with_answer();
    return 0;
}
