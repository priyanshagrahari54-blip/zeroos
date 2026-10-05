#include <assert.h>
#include <string.h>
#include "../kernel/dhcp_core.h"

#define PACKET_SIZE 260U

static void packet_init(uint8_t *packet) {
    memset(packet, 0, PACKET_SIZE);
    packet[0] = 2; /* BOOTREPLY */
    packet[1] = 1; /* Ethernet */
    packet[2] = 6;
    packet[4] = 0x12;
    packet[5] = 0x34;
    packet[236] = 0x63;
    packet[237] = 0x82;
    packet[238] = 0x53;
    packet[239] = 0x63;
}

static uint32_t option_u32(uint8_t *packet, uint32_t offset, uint8_t code,
                           uint32_t value) {
    packet[offset++] = code;
    packet[offset++] = 4;
    packet[offset++] = (uint8_t)(value >> 24);
    packet[offset++] = (uint8_t)(value >> 16);
    packet[offset++] = (uint8_t)(value >> 8);
    packet[offset++] = (uint8_t)value;
    return offset;
}

static uint32_t fuzz_next(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void test_parser_mutation_sweep(void) {
    uint8_t packet[PACKET_SIZE];
    struct dhcp_view view;
    uint32_t state = 0x7a31c4e9U;

    /* Arbitrary datagrams are copied into exactly-sized VLAs so ASan can
     * catch any parser read beyond the supplied length, not just the array. */
    for (uint32_t trial = 0; trial < 512; ++trial) {
        uint32_t length = fuzz_next(&state) % (PACKET_SIZE + 1U);
        for (uint32_t i = 0; i < PACKET_SIZE; ++i)
            packet[i] = (uint8_t)fuzz_next(&state);
        {
            uint8_t exact[length ? length : 1U];
            int result;
            if (length)
                memcpy(exact, packet, length);
            result = dhcp_parse(exact, length, &view);
            assert(result == -1 || result == 0);
            if (result == 0) {
                assert((view.op == 1 || view.op == 2) &&
                       view.hardware_length > 0 &&
                       view.hardware_length <= 16 && view.message_type != 0);
            }
        }
    }

    /* Keep the BOOTP header valid while fuzzing TLV structure and every
     * truncation boundary in the option region. */
    for (uint32_t trial = 0; trial < 512; ++trial) {
        uint32_t length;
        packet_init(packet);
        for (uint32_t i = 240; i < PACKET_SIZE; ++i)
            packet[i] = (uint8_t)fuzz_next(&state);
        length = 240U + fuzz_next(&state) % 21U;
        {
            uint8_t exact[length];
            int result;
            memcpy(exact, packet, length);
            result = dhcp_parse(exact, length, &view);
            assert(result == -1 || result == 0);
            if (result == 0)
                assert(view.message_type != 0);
        }
    }
}

int main(void) {
    uint8_t packet[PACKET_SIZE];
    struct dhcp_view view;

    packet_init(packet);
    packet[240] = 53;
    packet[241] = 1;
    packet[242] = 2; /* DHCPOFFER */
    packet[243] = 255;
    assert(dhcp_parse(packet, 244, &view) == 0);
    assert(view.message_type == 2 && view.xid == 0x12340000U);
    assert(view.op == 2 && view.hardware_type == 1 &&
           view.hardware_length == 6);
    assert(view.client_hardware == packet + 28);

    /* Pad and unknown zero-length options are safe to skip. */
    packet_init(packet);
    packet[240] = 0;
    packet[241] = 200;
    packet[242] = 0;
    packet[243] = 53;
    packet[244] = 1;
    packet[245] = 2;
    packet[246] = 255;
    assert(dhcp_parse(packet, 247, &view) == 0);

    assert(dhcp_parse(NULL, 244, &view) == -1);
    assert(dhcp_parse(packet, 244, NULL) == -1);
    assert(dhcp_parse(packet, 239, &view) == -1);

    packet_init(packet);
    packet[0] = 3; /* invalid BOOTP operation */
    assert(dhcp_parse(packet, PACKET_SIZE, &view) == -1);
    packet_init(packet);
    packet[2] = 0;
    assert(dhcp_parse(packet, PACKET_SIZE, &view) == -1);
    packet_init(packet);
    packet[2] = 17;
    assert(dhcp_parse(packet, PACKET_SIZE, &view) == -1);
    packet_init(packet);
    packet[236] = 0;
    assert(dhcp_parse(packet, PACKET_SIZE, &view) == -1);

    /* Message type is mandatory, nonzero and unique. */
    packet_init(packet);
    packet[240] = 53;
    packet[241] = 1;
    packet[242] = 0;
    packet[243] = 255;
    assert(dhcp_parse(packet, 244, &view) == -1);
    packet[242] = 2;
    packet[243] = 53;
    packet[244] = 1;
    packet[245] = 2;
    packet[246] = 255;
    assert(dhcp_parse(packet, 247, &view) == -1);

    /* Known option duplicates are detected by presence, even if value zero. */
    packet_init(packet);
    packet[240] = 53;
    packet[241] = 1;
    packet[242] = 2;
    uint32_t offset = option_u32(packet, 243, 1, 0);
    offset = option_u32(packet, offset, 1, 0);
    packet[offset++] = 255;
    assert(dhcp_parse(packet, offset, &view) == -1);

    /* Missing end marker and truncated option payloads fail closed. */
    packet_init(packet);
    packet[240] = 53;
    packet[241] = 1;
    packet[242] = 2;
    assert(dhcp_parse(packet, 243, &view) == -1);
    packet[243] = 54;
    packet[244] = 4;
    packet[245] = 192;
    assert(dhcp_parse(packet, 246, &view) == -1);

    test_parser_mutation_sweep();
    return 0;
}
