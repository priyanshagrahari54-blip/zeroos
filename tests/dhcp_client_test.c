#include <assert.h>
#include <string.h>
#include "../kernel/dhcp_core.h"

#define PACKET_SIZE 270U
#define OFFERED_ADDRESS 0xc000022cU
#define SERVER_ONE 0xc0000201U
#define SERVER_TWO 0xc0000202U

static void packet_init(uint8_t *packet, uint8_t message_type,
                        uint32_t address, uint32_t server) {
    memset(packet, 0, PACKET_SIZE);
    packet[0] = 2; /* BOOTREPLY */
    packet[1] = 1; /* Ethernet */
    packet[2] = 6;
    packet[7] = 1; /* transaction ID */
    packet[16] = (uint8_t)(address >> 24);
    packet[17] = (uint8_t)(address >> 16);
    packet[18] = (uint8_t)(address >> 8);
    packet[19] = (uint8_t)address;
    packet[236] = 0x63;
    packet[237] = 0x82;
    packet[238] = 0x53;
    packet[239] = 0x63;
    packet[240] = 53;
    packet[241] = 1;
    packet[242] = message_type;
    packet[243] = 54;
    packet[244] = 4;
    packet[245] = (uint8_t)(server >> 24);
    packet[246] = (uint8_t)(server >> 16);
    packet[247] = (uint8_t)(server >> 8);
    packet[248] = (uint8_t)server;
}

static uint32_t add_u32_option(uint8_t *packet, uint32_t offset,
                               uint8_t code, uint32_t value) {
    packet[offset++] = code;
    packet[offset++] = 4;
    packet[offset++] = (uint8_t)(value >> 24);
    packet[offset++] = (uint8_t)(value >> 16);
    packet[offset++] = (uint8_t)(value >> 8);
    packet[offset++] = (uint8_t)value;
    return offset;
}

int main(void) {
    uint8_t packet[PACKET_SIZE];
    struct dhcp_client client = {0};
    uint32_t length;

    assert(dhcp_client_start(NULL, 1, 100) == -1);
    assert(dhcp_client_start(&client, 0, 100) == -1);
    assert(dhcp_client_start(&client, 1, 100) == 0);
    assert(dhcp_client_tick(&client, 100, 5, 3) == DHCP_ACTION_DISCOVER);

    /* A valid OFFER advances SELECTING -> REQUESTING. */
    packet_init(packet, 2, OFFERED_ADDRESS, SERVER_ONE);
    packet[249] = 255;
    length = 250;
    packet[7] = 2; /* wrong transaction */
    assert(dhcp_client_receive(&client, packet, length, 101) == -1);
    assert(client.state == DHCP_SELECTING);
    packet[7] = 1;
    assert(dhcp_client_receive(&client, packet, length, 101) == 1);
    assert(client.state == DHCP_REQUESTING);
    assert(client.address == OFFERED_ADDRESS && client.server == SERVER_ONE);
    assert(dhcp_client_tick(&client, 101, 5, 3) == DHCP_ACTION_REQUEST);

    /* NAK/ACK from a different server must not steal the selected lease. */
    packet_init(packet, 6, 0, SERVER_TWO);
    packet[249] = 255;
    assert(dhcp_client_receive(&client, packet, 250, 102) == -1);
    assert(client.state == DHCP_REQUESTING && client.server == SERVER_ONE);

    packet_init(packet, 5, OFFERED_ADDRESS, SERVER_TWO);
    length = add_u32_option(packet, 249, 51, 100);
    length = add_u32_option(packet, length, 58, 50);
    length = add_u32_option(packet, length, 59, 87);
    packet[length++] = 255;
    assert(dhcp_client_receive(&client, packet, length, 110) == -1);
    assert(client.state == DHCP_REQUESTING && client.server == SERVER_ONE);

    /* Valid ACK pins the lease to the selected server and establishes timers. */
    packet_init(packet, 5, OFFERED_ADDRESS, SERVER_ONE);
    length = add_u32_option(packet, 249, 51, 100);
    length = add_u32_option(packet, length, 58, 50);
    length = add_u32_option(packet, length, 59, 87);
    packet[length++] = 255;
    assert(dhcp_client_receive(&client, packet, length, 110) == 3);
    assert(client.state == DHCP_BOUND);
    assert(client.lease_seconds == 100);
    assert(client.renewal_seconds == 50 && client.rebind_seconds == 87);
    assert(dhcp_client_tick(&client, 159, 5, 3) == DHCP_ACTION_NONE);
    assert(dhcp_client_tick(&client, 160, 5, 3) == DHCP_ACTION_RENEW);
    assert(dhcp_client_tick(&client, 197, 5, 3) == DHCP_ACTION_REBIND);
    assert(dhcp_client_tick(&client, 210, 5, 3) == DHCP_ACTION_EXPIRED);
    assert(client.state == DHCP_FAILED && client.address == 0);

    /* T1/T2/expiry comparisons remain correct across a 32-bit tick wrap. */
    {
        struct dhcp_client wrap = {0};
        uint32_t start = 0xfffffff0U;
        assert(dhcp_client_start(&wrap, 1, start) == 0);
        packet_init(packet, 2, OFFERED_ADDRESS, SERVER_ONE);
        packet[249] = 255;
        assert(dhcp_client_receive(&wrap, packet, 250, start + 1U) == 1);
        packet_init(packet, 5, OFFERED_ADDRESS, SERVER_ONE);
        length = add_u32_option(packet, 249, 51, 16);
        length = add_u32_option(packet, length, 58, 8);
        length = add_u32_option(packet, length, 59, 14);
        packet[length++] = 255;
        assert(dhcp_client_receive(&wrap, packet, length, start + 2U) == 3);
        assert(dhcp_client_tick(&wrap, start + 9U, 2, 3) ==
               DHCP_ACTION_NONE);
        assert(dhcp_client_tick(&wrap, start + 10U, 2, 3) ==
               DHCP_ACTION_RENEW);
        assert(dhcp_client_tick(&wrap, start + 16U, 2, 3) ==
               DHCP_ACTION_REBIND);
        assert(dhcp_client_tick(&wrap, start + 18U, 2, 3) ==
               DHCP_ACTION_EXPIRED);
    }

    /* Malformed/truncated ACK options cannot advance client state. */
    {
        struct dhcp_client pending = {0};
        assert(dhcp_client_start(&pending, 1, 10) == 0);
        packet_init(packet, 2, OFFERED_ADDRESS, SERVER_ONE);
        packet[249] = 255;
        assert(dhcp_client_receive(&pending, packet, 250, 11) == 1);
        packet_init(packet, 5, OFFERED_ADDRESS, SERVER_ONE);
        packet[249] = 51;
        packet[250] = 4;
        packet[251] = 0;
        packet[252] = 0;
        packet[253] = 0;
        assert(dhcp_client_receive(&pending, packet, 254, 12) == -1);
        assert(pending.state == DHCP_REQUESTING);
    }

    return 0;
}
