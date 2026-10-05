#include "dhcp_core.h"

#define DHCP_COOKIE 0x63825363U
#define DHCP_SEEN_MESSAGE_TYPE (1U << 0)
#define DHCP_SEEN_SERVER_ID    (1U << 1)
#define DHCP_SEEN_SUBNET_MASK  (1U << 2)
#define DHCP_SEEN_LEASE        (1U << 3)
#define DHCP_SEEN_RENEWAL      (1U << 4)
#define DHCP_SEEN_REBIND       (1U << 5)

static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

int dhcp_parse(const uint8_t *packet, uint32_t length,
               struct dhcp_view *out) {
    uint32_t seen = 0;
    uint8_t ended = 0;

    if (!packet || !out || length < 240 ||
        (packet[0] != 1 && packet[0] != 2) ||
        packet[2] == 0 || packet[2] > 16 ||
        get32(packet + 236) != DHCP_COOKIE)
        return -1;

    out->op = packet[0];
    out->hardware_type = packet[1];
    out->hardware_length = packet[2];
    out->xid = get32(packet + 4);
    out->client_ip = get32(packet + 12);
    out->your_ip = get32(packet + 16);
    out->server_ip = get32(packet + 20);
    out->client_hardware = packet + 28;
    out->message_type = 0;
    out->server_identifier = 0;
    out->subnet_mask = 0;
    out->lease_seconds = 0;
    out->renewal_seconds = 0;
    out->rebind_seconds = 0;

    for (uint32_t i = 240; i < length;) {
        uint8_t code = packet[i++];
        uint8_t option_length;
        uint32_t value;
        uint32_t seen_bit = 0;

        if (code == 0) /* pad */
            continue;
        if (code == 255) {
            ended = 1;
            break;
        }
        if (i >= length)
            return -1;
        option_length = packet[i++];
        if ((uint32_t)option_length > length - i)
            return -1;
        value = option_length == 4 ? get32(packet + i) : 0;

        switch (code) {
        case 53: /* DHCP Message Type */
            seen_bit = DHCP_SEEN_MESSAGE_TYPE;
            if (option_length != 1 || packet[i] == 0)
                return -1;
            out->message_type = packet[i];
            break;
        case 54: /* Server Identifier */
            seen_bit = DHCP_SEEN_SERVER_ID;
            if (option_length != 4)
                return -1;
            out->server_identifier = value;
            break;
        case 1: /* Subnet Mask */
            seen_bit = DHCP_SEEN_SUBNET_MASK;
            if (option_length != 4)
                return -1;
            out->subnet_mask = value;
            break;
        case 51: /* IP Address Lease Time */
            seen_bit = DHCP_SEEN_LEASE;
            if (option_length != 4)
                return -1;
            out->lease_seconds = value;
            break;
        case 58: /* Renewal (T1) Time */
            seen_bit = DHCP_SEEN_RENEWAL;
            if (option_length != 4)
                return -1;
            out->renewal_seconds = value;
            break;
        case 59: /* Rebinding (T2) Time */
            seen_bit = DHCP_SEEN_REBIND;
            if (option_length != 4)
                return -1;
            out->rebind_seconds = value;
            break;
        default:
            break; /* unknown options are length-checked and skipped */
        }

        /* Presence is distinct from value: zero-valued known options are
         * still duplicates and cannot bypass the duplicate check. */
        if (seen_bit) {
            if (seen & seen_bit)
                return -1;
            seen |= seen_bit;
        }
        i += option_length;
    }

    if (!ended || !(seen & DHCP_SEEN_MESSAGE_TYPE))
        return -1;
    return 0;
}

int dhcp_client_start(struct dhcp_client *client, uint32_t xid, uint32_t now) {
    if (!client || !xid)
        return -1;
    client->state = DHCP_SELECTING;
    client->xid = xid;
    client->address = 0;
    client->server = 0;
    client->lease_start = 0;
    client->lease_seconds = 0;
    client->renewal_seconds = 0;
    client->rebind_seconds = 0;
    client->deadline = now;
    client->retries = 0;
    return 0;
}

int dhcp_client_receive(struct dhcp_client *client, const uint8_t *packet,
                        uint32_t length, uint32_t now) {
    struct dhcp_view view;
    int has_server;

    if (!client || dhcp_parse(packet, length, &view) != 0 ||
        view.xid != client->xid || view.op != 2)
        return -1;

    has_server = view.server_identifier != 0;
    if (client->state == DHCP_SELECTING && view.message_type == 2 &&
        view.your_ip && has_server) {
        client->address = view.your_ip;
        client->server = view.server_identifier;
        client->state = DHCP_REQUESTING;
        client->deadline = now;
        client->retries = 0;
        return 1;
    }

    if (client->state != DHCP_REQUESTING && client->state != DHCP_RENEWING &&
        client->state != DHCP_REBINDING)
        return -1;

    if (view.message_type == 6) { /* NAK */
        if (!has_server ||
            (client->state != DHCP_REBINDING &&
             view.server_identifier != client->server))
            return -1;
        client->state = DHCP_SELECTING;
        client->address = 0;
        client->server = 0;
        client->deadline = now;
        client->retries = 0;
        return 2;
    }

    if (view.message_type == 5 && view.your_ip && view.lease_seconds) {
        uint32_t lease = view.lease_seconds;
        uint32_t t1;
        uint32_t t2;

        /* REQUESTING/RENEWING replies must come from the selected lease
         * server. REBINDING deliberately accepts a different server. */
        if (!has_server ||
            (client->state != DHCP_REBINDING &&
             view.server_identifier != client->server) ||
            lease >= 0x80000000U)
            return -1;

        t1 = view.renewal_seconds ? view.renewal_seconds : lease / 2U;
        t2 = view.rebind_seconds ? view.rebind_seconds :
             (uint32_t)(((uint64_t)lease * 7ULL) / 8ULL);
        if (t1 >= t2 || t2 >= lease)
            return -1;

        client->address = view.your_ip;
        client->server = view.server_identifier;
        client->lease_start = now;
        client->lease_seconds = lease;
        client->renewal_seconds = t1;
        client->rebind_seconds = t2;
        client->deadline = now + t1;
        client->retries = 0;
        client->state = DHCP_BOUND;
        return 3;
    }

    return -1;
}

enum dhcp_action dhcp_client_tick(struct dhcp_client *client, uint32_t now,
                                  uint32_t interval, uint32_t limit) {
    if (!client || !interval || interval >= 0x80000000U)
        return DHCP_ACTION_NONE;

    if (client->state == DHCP_BOUND || client->state == DHCP_RENEWING ||
        client->state == DHCP_REBINDING) {
        uint32_t expiry = client->lease_start + client->lease_seconds;
        if ((int32_t)(now - expiry) >= 0) {
            client->state = DHCP_FAILED;
            client->address = 0;
            return DHCP_ACTION_EXPIRED;
        }
        if (client->state == DHCP_BOUND &&
            (int32_t)(now - (client->lease_start +
                             client->renewal_seconds)) >= 0) {
            client->state = DHCP_RENEWING;
            client->deadline = now;
            client->retries = 0;
            return DHCP_ACTION_RENEW;
        }
        if (client->state == DHCP_RENEWING &&
            (int32_t)(now - (client->lease_start +
                             client->rebind_seconds)) >= 0) {
            client->state = DHCP_REBINDING;
            client->deadline = now;
            client->retries = 0;
            return DHCP_ACTION_REBIND;
        }
    }

    if ((client->state == DHCP_SELECTING ||
         client->state == DHCP_REQUESTING ||
         client->state == DHCP_RENEWING ||
         client->state == DHCP_REBINDING) &&
        (int32_t)(now - client->deadline) >= 0) {
        if (client->retries >= limit) {
            if (client->state == DHCP_SELECTING ||
                client->state == DHCP_REQUESTING)
                client->state = DHCP_FAILED;
            return DHCP_ACTION_NONE;
        }
        client->retries++;
        client->deadline = now + interval;
        if (client->state == DHCP_SELECTING)
            return DHCP_ACTION_DISCOVER;
        if (client->state == DHCP_REQUESTING)
            return DHCP_ACTION_REQUEST;
        if (client->state == DHCP_RENEWING)
            return DHCP_ACTION_RENEW;
        return DHCP_ACTION_REBIND;
    }
    return DHCP_ACTION_NONE;
}
