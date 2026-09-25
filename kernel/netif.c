#include "netif.h"
#include "net_l2.h"

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return NETIF_MAX_NAME;
    while (s[n] && n < NETIF_MAX_NAME)
        ++n;
    return n;
}

int netif_init(struct netif *n, const char *name, uint32_t index,
               const uint8_t address[6], uint16_t mtu, void *context,
               netif_tx_fn transmit, netif_lock_fn lock,
               netif_unlock_fn unlock) {
    if (!n || !name || !address || !index || mtu < NETIF_MTU_MIN ||
        mtu > NETIF_MTU_MAX || !transmit || !lock || !unlock)
        return -1;
    uint32_t name_length = str_len(name);
    if (name_length == 0 || name_length >= NETIF_MAX_NAME)
        return -1;

    uint8_t *bytes = (uint8_t *)n;
    for (uint32_t i = 0; i < sizeof(*n); ++i)
        bytes[i] = 0;
    for (uint32_t i = 0; i < name_length; ++i)
        n->name[i] = name[i];
    for (uint32_t i = 0; i < 6; ++i)
        n->address[i] = address[i];
    n->mtu = mtu;
    n->index = index;
    n->context = context;
    n->transmit = transmit;
    n->lock = lock;
    n->unlock = unlock;
    return 0;
}

int netif_set_link(struct netif *n, int up) {
    if (!n || !n->lock || !n->unlock)
        return -1;
    uint64_t state = n->lock(n->context);
    if (up)
        n->flags |= NETIF_FLAG_UP | NETIF_FLAG_LINK;
    else
        n->flags &= ~(NETIF_FLAG_UP | NETIF_FLAG_LINK);
    n->unlock(n->context, state);
    return 0;
}

int netif_receive(struct netif *n, const uint8_t *frame, uint32_t length) {
    if (!n || !n->lock || !n->unlock)
        return -1;
    if (!frame || length < 14 || length > NETIF_FRAME_MAX ||
        length > (uint32_t)n->mtu + 18U) {
        uint64_t state = n->lock(n->context);
        n->stats.rx_errors++;
        n->unlock(n->context, state);
        return -1;
    }
    struct net_eth_view view;
    if (net_ethernet_parse(frame, length, &view) != 0) {
        uint64_t state = n->lock(n->context);
        n->stats.rx_errors++;
        n->unlock(n->context, state);
        return -1;
    }

    uint64_t state = n->lock(n->context);
    if (!(n->flags & NETIF_FLAG_LINK)) {
        n->stats.rx_errors++;
        n->unlock(n->context, state);
        return -1;
    }
    if (n->count == NETIF_RX_QUEUE) {
        n->stats.rx_dropped++;
        n->unlock(n->context, state);
        return -2;
    }
    struct netif_packet *packet = &n->rx[n->head];
    packet->length = (uint16_t)length;
    for (uint32_t i = 0; i < length; ++i)
        packet->bytes[i] = frame[i];
    n->head = (uint8_t)((n->head + 1U) % NETIF_RX_QUEUE);
    n->count++;
    n->stats.rx_packets++;
    n->unlock(n->context, state);
    return 0;
}

int netif_dequeue(struct netif *n, uint8_t *buffer, uint32_t capacity,
                  uint16_t *length) {
    if (!n || !buffer || !length || !n->lock || !n->unlock)
        return -1;
    uint64_t state = n->lock(n->context);
    if (!n->count) {
        n->unlock(n->context, state);
        return 1;
    }
    struct netif_packet *packet = &n->rx[n->tail];
    if (capacity < packet->length) {
        n->unlock(n->context, state);
        return -2;
    }
    *length = packet->length;
    for (uint32_t i = 0; i < packet->length; ++i)
        buffer[i] = packet->bytes[i];
    n->tail = (uint8_t)((n->tail + 1U) % NETIF_RX_QUEUE);
    n->count--;
    n->unlock(n->context, state);
    return 0;
}

int netif_send(struct netif *n, const uint8_t *frame, uint32_t length) {
    if (!n || !n->lock || !n->unlock)
        return -1;
    if (!frame || length < 14 || length > NETIF_FRAME_MAX ||
        length > (uint32_t)n->mtu + 18U) {
        uint64_t state = n->lock(n->context);
        n->stats.tx_errors++;
        n->unlock(n->context, state);
        return -1;
    }
    struct net_eth_view view;
    if (net_ethernet_parse(frame, length, &view) != 0) {
        uint64_t state = n->lock(n->context);
        n->stats.tx_errors++;
        n->unlock(n->context, state);
        return -1;
    }
    uint64_t state = n->lock(n->context);
    if (!(n->flags & NETIF_FLAG_LINK)) {
        n->stats.tx_errors++;
        n->unlock(n->context, state);
        return -1;
    }
    netif_tx_fn transmit = n->transmit;
    void *context = n->context;
    n->unlock(n->context, state);

    /* Never invoke the device/driver callback under the queue lock. */
    if (transmit(context, frame, (uint16_t)length) != 0) {
        state = n->lock(n->context);
        n->stats.tx_dropped++;
        n->unlock(n->context, state);
        return -2;
    }
    state = n->lock(n->context);
    n->stats.tx_packets++;
    n->unlock(n->context, state);
    return 0;
}
