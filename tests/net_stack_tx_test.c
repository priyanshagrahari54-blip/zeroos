#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/net_stack.h"
#include "../kernel/net_l2.h"

struct capture {
    uint8_t frame[NETIF_FRAME_MAX];
    uint16_t length;
    int fail;
};

static uint64_t lock_noop(void *context) { (void)context; return 0; }
static void unlock_noop(void *context, uint64_t state) {
    (void)context; (void)state;
}
static int transmit(void *context, const uint8_t *frame, uint16_t length) {
    struct capture *capture = (struct capture *)context;
    if (capture->fail)
        return -1;
    assert(length <= sizeof(capture->frame));
    memcpy(capture->frame, frame, length);
    capture->length = length;
    return 0;
}

static uint32_t add_words(uint32_t sum, const uint8_t *bytes, uint32_t length) {
    uint32_t i = 0;
    while (i + 1U < length) {
        sum += ((uint32_t)bytes[i] << 8) | bytes[i + 1U];
        i += 2U;
    }
    if (i < length)
        sum += (uint32_t)bytes[i] << 8;
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    return sum;
}

static int checksum_valid(const uint8_t *bytes, uint32_t length) {
    return add_words(0, bytes, length) == 0xffffU;
}

static int udp_checksum_valid(const uint8_t *ip, const uint8_t *udp,
                              uint16_t length) {
    uint32_t sum = add_words(0, ip + 12U, 8U);
    uint8_t pseudo[4] = { 0, 17, (uint8_t)(length >> 8), (uint8_t)length };
    sum = add_words(sum, pseudo, sizeof(pseudo));
    sum = add_words(sum, udp, length);
    return sum == 0xffffU;
}

int main(void) {
    struct capture capture = {0};
    struct netif interface;
    struct net_stack stack;
    const uint8_t local_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    const uint8_t remote_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
    const uint8_t payload[] = { 'z', 'e', 'r', 'o', 'o', 's', '!' };
    const uint32_t local_ip = 0xc0000202U;
    const uint32_t remote_ip = 0xc6336409U;

    assert(netif_init(&interface, "tx0", 1, local_mac, 1500, &capture,
                      transmit, lock_noop, unlock_noop) == 0);
    assert(netif_set_link(&interface, 1) == 0);
    net_stack_init(&stack, 0, 0);
    assert(net_stack_set_ipv4_address(&stack, local_ip) == 0);

    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   49152, 53, payload, sizeof(payload)) == 0);
    assert(stack.stats.udp_transmitted == 1 && stack.stats.transmit_errors == 0);
    assert(interface.stats.tx_packets == 1 && interface.stats.tx_dropped == 0);
    assert(capture.length == 14U + 20U + 8U + sizeof(payload));
    assert(memcmp(capture.frame, remote_mac, 6) == 0);
    assert(memcmp(capture.frame + 6, local_mac, 6) == 0);
    assert(capture.frame[12] == 0x08 && capture.frame[13] == 0x00);

    const uint8_t *ip = capture.frame + 14U;
    const uint8_t *udp = ip + 20U;
    assert(ip[0] == 0x45 && ip[8] == 64 && ip[9] == 17);
    assert(ip[6] == 0x40 && ip[7] == 0x00); /* Don't fragment. */
    assert(ip[2] == 0 && ip[3] == 35);
    assert(ip[12] == 192 && ip[15] == 2);
    assert(ip[16] == 198 && ip[17] == 51 && ip[18] == 100 && ip[19] == 9);
    assert(checksum_valid(ip, 20));
    assert(udp[0] == 0xc0 && udp[1] == 0x00);
    assert(udp[2] == 0 && udp[3] == 53);
    assert(udp[4] == 0 && udp[5] == 15);
    assert((udp[6] || udp[7]) && udp_checksum_valid(ip, udp, 15));
    assert(memcmp(udp + 8, payload, sizeof(payload)) == 0);

    /* Zero-length datagrams are legal, and still get a nonzero checksum. */
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   49152, 7, 0, 0) == 0);
    ip = capture.frame + 14U;
    udp = ip + 20U;
    assert(capture.length == 42 && udp[4] == 0 && udp[5] == 8);
    assert((udp[6] || udp[7]) && udp_checksum_valid(ip, udp, 8));

    uint8_t too_large[1473] = {0};
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   1, 2, too_large, sizeof(too_large)) == -2);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   0, 2, payload, sizeof(payload)) == -1);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, 0xe0000001U,
                                   1, 2, payload, sizeof(payload)) == -1);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, 0x7f000001U,
                                   1, 2, payload, sizeof(payload)) == -1);
    assert(stack.stats.udp_transmitted == 2 && stack.stats.transmit_errors == 4);

    capture.fail = 1;
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   1, 2, payload, sizeof(payload)) == -2);
    assert(interface.stats.tx_dropped == 1);
    assert(stack.stats.udp_transmitted == 2 && stack.stats.transmit_errors == 5);
    capture.fail = 0;
    assert(netif_set_link(&interface, 0) == 0);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   1, 2, payload, sizeof(payload)) == -1);
    assert(stack.stats.transmit_errors == 6);
    return 0;
}
