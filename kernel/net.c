#include "net.h"
#include "timer.h"

static struct spinlock net_lock;
static struct zeroos_net_interface ifaces[ZEROOS_NET_MAX_INTERFACES];
static struct zeroos_net_socket sockets[ZEROOS_NET_MAX_SOCKETS];
static uint64_t next_if_id;
static uint64_t next_sock_id;

int net_system_init(void) {
    spinlock_init(&net_lock);
    next_if_id=1;
    next_sock_id=1;
    for (uint32_t i=0;i<ZEROOS_NET_MAX_INTERFACES;++i) {
        ifaces[i].used=0;
        ifaces[i].generation=0;
        ifaces[i].state=ZEROOS_NET_IF_STOPPED;
        spinlock_init(&ifaces[i].lock);
        wait_queue_init(&ifaces[i].packet_waiters);
    }
    for (uint32_t i=0;i<ZEROOS_NET_MAX_SOCKETS;++i) {
        sockets[i].used=0;
        sockets[i].generation=0;
        spinlock_init(&sockets[i].lock);
        wait_queue_init(&sockets[i].recv_waiters);
        wait_queue_init(&sockets[i].send_waiters);
    }
    return 0;
}

static struct zeroos_net_interface *iface_lookup_locked(uint64_t id) {
    uint32_t slot=(uint32_t)(id & 0xffffULL);
    uint32_t gen=(uint32_t)(id>>16);
    if (slot==0 || slot>ZEROOS_NET_MAX_INTERFACES || gen==0) return 0;
    struct zeroos_net_interface *iface=&ifaces[slot-1];
    if (!iface->used || iface->generation!=gen) return 0;
    return iface;
}

static struct zeroos_net_socket *sock_lookup_locked(uint64_t id) {
    uint32_t slot=(uint32_t)(id & 0xffffULL);
    uint32_t gen=(uint32_t)(id>>16);
    if (slot==0 || slot>ZEROOS_NET_MAX_SOCKETS || gen==0) return 0;
    struct zeroos_net_socket *s=&sockets[slot-1];
    if (!s->used || s->generation!=gen) return 0;
    return s;
}

int net_interface_register(const char *name, const uint8_t mac[6], uint64_t mtu, uint64_t *if_id_out) {
    if (!name || !mac || !if_id_out || mtu==0 || mtu>9000) return -1;
    uint64_t flags=spin_lock_irqsave(&net_lock);
    for (uint32_t i=0;i<ZEROOS_NET_MAX_INTERFACES;++i) {
        if (ifaces[i].used) continue;
        if (ifaces[i].generation==0xffffffffU) continue;
        ifaces[i].generation++;
        if (ifaces[i].generation==0) continue;
        ifaces[i].used=1;
        ifaces[i].state=ZEROOS_NET_IF_DORMANT;
        ifaces[i].mtu=mtu;
        ifaces[i].tx_packets=ifaces[i].rx_packets=0;
        ifaces[i].tx_errors=ifaces[i].rx_errors=0;
        uint32_t n=0;
        while (n<ZEROOS_NET_MAX_NAME-1 && name[n]) { ifaces[i].name[n]=name[n]; n++; }
        ifaces[i].name[n]=0;
        for (uint32_t j=0;j<6;++j) ifaces[i].mac[j]=mac[j];
        ifaces[i].id = ((uint64_t)ifaces[i].generation<<16) | (uint64_t)(i+1);
        *if_id_out=ifaces[i].id;
        spin_unlock_irqrestore(&net_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&net_lock,flags);
    return -1;
}

int net_interface_set_ipv4(uint64_t if_id, uint32_t addr, uint32_t netmask, uint32_t gateway) {
    uint64_t flags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_interface *iface=iface_lookup_locked(if_id);
    if (!iface) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    uint64_t iflags=spin_lock_irqsave(&iface->lock);
    iface->ipv4_addr=addr;
    iface->ipv4_netmask=netmask;
    iface->ipv4_gateway=gateway;
    spin_unlock_irqrestore(&iface->lock,iflags);
    spin_unlock_irqrestore(&net_lock,flags);
    return 0;
}

int net_interface_set_state(uint64_t if_id, enum zeroos_net_interface_state state) {
    uint64_t flags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_interface *iface=iface_lookup_locked(if_id);
    if (!iface) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    uint64_t iflags=spin_lock_irqsave(&iface->lock);
    iface->state=state;
    spin_unlock_irqrestore(&iface->lock,iflags);
    spin_unlock_irqrestore(&net_lock,flags);
    return 0;
}

struct zeroos_net_interface *net_interface_lookup(uint64_t if_id) {
    uint64_t flags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_interface *iface=iface_lookup_locked(if_id);
    spin_unlock_irqrestore(&net_lock,flags);
    return iface;
}

int net_socket_create(enum zeroos_net_socket_type type, uint64_t *sock_id_out) {
    if (!sock_id_out || type==ZEROOS_NET_SOCK_UNKNOWN) return -1;
    uint64_t flags=spin_lock_irqsave(&net_lock);
    for (uint32_t i=0;i<ZEROOS_NET_MAX_SOCKETS;++i) {
        if (sockets[i].used) continue;
        if (sockets[i].generation==0xffffffffU) continue;
        sockets[i].generation++;
        if (sockets[i].generation==0) continue;
        sockets[i].used=1;
        sockets[i].type=type;
        sockets[i].state=ZEROOS_NET_SOCK_CLOSED;
        sockets[i].interface_id=0;
        sockets[i].local_ipv4=0;
        sockets[i].local_port=0;
        sockets[i].remote_ipv4=0;
        sockets[i].remote_port=0;
        sockets[i].timeout_ticks=100;
        sockets[i].error=0;
        sockets[i].recv_head=sockets[i].recv_tail=sockets[i].recv_count=0;
        sockets[i].id = ((uint64_t)sockets[i].generation<<16) | (uint64_t)(i+1);
        *sock_id_out=sockets[i].id;
        spin_unlock_irqrestore(&net_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&net_lock,flags);
    return -1;
}

int net_socket_bind(uint64_t sock_id, uint32_t addr, uint16_t port) {
    uint64_t flags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_socket *s=sock_lookup_locked(sock_id);
    if (!s) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    if (s->state!=ZEROOS_NET_SOCK_CLOSED) { spin_unlock_irqrestore(&s->lock,sflags); spin_unlock_irqrestore(&net_lock,flags); return -1; }
    s->local_ipv4=addr;
    s->local_port=port;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&net_lock,flags);
    return 0;
}

int net_socket_connect(uint64_t sock_id, uint32_t addr, uint16_t port, uint64_t timeout_ticks) {
    (void)timeout_ticks;
    uint64_t flags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_socket *s=sock_lookup_locked(sock_id);
    if (!s) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    if (s->type!=ZEROOS_NET_SOCK_STREAM) { spin_unlock_irqrestore(&s->lock,sflags); spin_unlock_irqrestore(&net_lock,flags); return -1; }
    s->remote_ipv4=addr;
    s->remote_port=port;
    s->state=ZEROOS_NET_SOCK_SYN_SENT;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&net_lock,flags);
    /* In real implementation, would send SYN and wait for SYN-ACK with retransmission */
    return 0;
}

int net_socket_listen(uint64_t sock_id, uint32_t backlog) {
    (void)backlog;
    uint64_t flags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_socket *s=sock_lookup_locked(sock_id);
    if (!s) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    if (s->state!=ZEROOS_NET_SOCK_CLOSED) { spin_unlock_irqrestore(&s->lock,sflags); spin_unlock_irqrestore(&net_lock,flags); return -1; }
    s->state=ZEROOS_NET_SOCK_LISTEN;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&net_lock,flags);
    return 0;
}

int net_socket_accept(uint64_t sock_id, uint64_t *new_sock_id_out, uint64_t timeout_ticks) {
    if (!new_sock_id_out) return -1;
    (void)timeout_ticks;
    uint64_t flags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_socket *s=sock_lookup_locked(sock_id);
    if (!s) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    if (s->state!=ZEROOS_NET_SOCK_LISTEN) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    spin_unlock_irqrestore(&net_lock,flags);
    /* For now, no incoming connections */
    return -1;
}

int net_socket_send(uint64_t sock_id, const void *data, uint64_t len, uint64_t flags, uint64_t timeout_ticks) {
    if (!data || len==0 || len>ZEROOS_NET_MAX_PACKET_SIZE) return -1;
    (void)flags; (void)timeout_ticks;
    uint64_t nflags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_socket *s=sock_lookup_locked(sock_id);
    if (!s) { spin_unlock_irqrestore(&net_lock,nflags); return -1; }
    /* Simulate send */
    spin_unlock_irqrestore(&net_lock,nflags);
    return (int)len;
}

int net_socket_recv(uint64_t sock_id, void *data, uint64_t cap, uint64_t flags, uint64_t *len_out, uint64_t timeout_ticks) {
    if (!data || !len_out || cap==0) return -1;
    (void)flags; (void)timeout_ticks;
    uint64_t nflags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_socket *s=sock_lookup_locked(sock_id);
    if (!s) { spin_unlock_irqrestore(&net_lock,nflags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    if (s->recv_count==0) {
        spin_unlock_irqrestore(&s->lock,sflags);
        spin_unlock_irqrestore(&net_lock,nflags);
        return -1; /* would block */
    }
    uint64_t to_copy = s->recv_count < cap ? s->recv_count : cap;
    for (uint64_t i=0;i<to_copy;++i) {
        ((uint8_t*)data)[i]=s->recv_buffer[(s->recv_head+i)%(ZEROOS_NET_MAX_PACKET_SIZE*4)];
    }
    s->recv_head = (s->recv_head+to_copy)%(ZEROOS_NET_MAX_PACKET_SIZE*4);
    s->recv_count -= (uint16_t)to_copy;
    *len_out=to_copy;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&net_lock,nflags);
    return (int)to_copy;
}

int net_socket_close(uint64_t sock_id) {
    uint64_t flags=spin_lock_irqsave(&net_lock);
    struct zeroos_net_socket *s=sock_lookup_locked(sock_id);
    if (!s) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->state=ZEROOS_NET_SOCK_CLOSED;
    s->used=0;
    (void)wait_queue_wake_all(&s->recv_waiters);
    (void)wait_queue_wake_all(&s->send_waiters);
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&net_lock,flags);
    return 0;
}

int net_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&net_lock);
    for (uint32_t i=0;i<ZEROOS_NET_MAX_INTERFACES;++i) if (ifaces[i].used && ifaces[i].mtu==0) { spin_unlock_irqrestore(&net_lock,flags); return -1; }
    spin_unlock_irqrestore(&net_lock,flags);
    return 0;
}
