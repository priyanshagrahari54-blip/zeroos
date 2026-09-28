#include <assert.h>
#include "../kernel/net_stack.h"
#include "../kernel/net_l2.h"
#include "../kernel/net_socket.h"

static uint64_t lock_noop(void *context) { (void)context; return 0; }
static void unlock_noop(void *context, uint64_t state) {
    (void)context; (void)state;
}
static int tx_noop(void *context, const uint8_t *frame, uint16_t length) {
    (void)context; (void)frame; (void)length; return 0;
}

struct delivery { uint32_t count, interface_index; uint8_t family; uint16_t source_port, destination_port, length; uint8_t source[16], destination[16], payload[16]; };
static int delivered(void *context, uint8_t family, uint32_t index,
                     const uint8_t source[16], const uint8_t destination[16],
                     uint16_t source_port, uint16_t destination_port,
                     const uint8_t *payload, uint16_t length) {
    struct delivery *d = (struct delivery *)context;
    assert(length <= sizeof(d->payload));
    d->count++;
    d->family=family; d->interface_index=index;
    d->source_port=source_port; d->destination_port=destination_port;
    d->length=length;
    for (uint32_t i=0;i<16;i++) { d->source[i]=source[i]; d->destination[i]=destination[i]; }
    for (uint32_t i=0;i<length;i++) d->payload[i]=payload[i];
    return 0;
}

static uint32_t sum_bytes(uint32_t sum, const uint8_t *p, uint32_t n) {
    uint32_t i=0;
    while (i+1<n) { sum += ((uint32_t)p[i]<<8)|p[i+1]; i+=2; }
    if (i<n) sum += (uint32_t)p[i]<<8;
    while (sum>>16) sum=(sum&0xffffU)+(sum>>16);
    return sum;
}
static uint16_t checksum(uint32_t sum) {
    while (sum>>16) sum=(sum&0xffffU)+(sum>>16);
    return (uint16_t)~sum;
}
static void make_ipv4_udp(uint8_t *frame, uint16_t fragment, int bad_udp_checksum) {
    static const uint8_t mac[6]={0x02,0,0,0,0,1};
    for (uint32_t i=0;i<6;i++) { frame[i]=mac[i]; frame[6+i]=0x10+i; }
    frame[12]=0x08; frame[13]=0x00;
    uint8_t *ip=frame+14;
    for (uint32_t i=0;i<31;i++) ip[i]=0;
    ip[0]=0x45; ip[2]=0; ip[3]=31; ip[6]=(uint8_t)(fragment>>8); ip[7]=(uint8_t)fragment;
    ip[8]=64; ip[9]=17;
    ip[12]=192; ip[13]=0; ip[14]=2; ip[15]=1;
    ip[16]=192; ip[17]=0; ip[18]=2; ip[19]=2;
    uint16_t c=checksum(sum_bytes(0,ip,20)); ip[10]=(uint8_t)(c>>8); ip[11]=(uint8_t)c;
    uint8_t *udp=ip+20;
    udp[0]=0x04; udp[1]=0xd2; udp[2]=0x27; udp[3]=0x0f;
    udp[4]=0; udp[5]=11; udp[6]=udp[7]=0;
    udp[8]='n'; udp[9]='e'; udp[10]='t';
    uint32_t sum=0;
    sum=sum_bytes(sum,ip+12,8);
    uint8_t pseudo[4]={0,17,0,11}; sum=sum_bytes(sum,pseudo,4);
    sum=sum_bytes(sum,udp,11);
    uint16_t uc=checksum(sum); if (!uc) uc=0xffff;
    if (bad_udp_checksum) uc^=1;
    udp[6]=(uint8_t)(uc>>8); udp[7]=(uint8_t)uc;
}

static int failing_delivery(void *context, uint8_t family, uint32_t index,
                            const uint8_t source[16],
                            const uint8_t destination[16],
                            uint16_t source_port, uint16_t destination_port,
                            const uint8_t *payload, uint16_t length) {
    (void)context; (void)family; (void)index; (void)source;
    (void)destination; (void)source_port; (void)destination_port;
    (void)payload; (void)length;
    return -1;
}

/* A minimal, valid IPv6 datagram with no payload: enough for the stack to
 * reach its deliberately fail-closed IPv6 branch. */
static void make_ipv6(uint8_t *frame) {
    static const uint8_t mac[6]={0x02,0,0,0,0,1};
    for (uint32_t i=0;i<6;i++) { frame[i]=mac[i]; frame[6+i]=0x10+i; }
    frame[12]=0x86; frame[13]=0xdd;
    uint8_t *ip=frame+14;
    for (uint32_t i=0;i<40;i++) ip[i]=0;
    ip[0]=0x60;
    ip[6]=17;  /* next header: UDP */
    ip[7]=64;  /* hop limit */
}

static void test_stack_init_and_address(void) {
    struct net_stack stack;
    struct delivery delivery={0};

    net_stack_init(0, delivered, &delivery);
    net_stack_init(&stack, 0, 0);
    assert(stack.udp_receive==0 && stack.context==0);
    assert(stack.ipv4_configured==0 && stack.ipv4_local_address==0);
    assert(stack.stats.frames==0);

    net_stack_init(&stack, delivered, &delivery);
    assert(stack.udp_receive==delivered && stack.context==&delivery);

    /* Re-initialising clears configuration and counters. */
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    assert(stack.ipv4_configured==1);
    net_stack_init(&stack, delivered, &delivery);
    assert(stack.ipv4_configured==0);
    assert(stack.ipv4_local_address==0);

    assert(net_stack_set_ipv4_address(0,0xc0000202U)==-1);
    /* Zero, limited broadcast, multicast and loopback are not assignable. */
    assert(net_stack_set_ipv4_address(&stack,0)==-1);
    assert(net_stack_set_ipv4_address(&stack,0xffffffffU)==-1);
    assert(net_stack_set_ipv4_address(&stack,0xe0000001U)==-1);
    assert(net_stack_set_ipv4_address(&stack,0xefffffffU)==-1);
    assert(net_stack_set_ipv4_address(&stack,0x7f000001U)==-1);
    assert(net_stack_set_ipv4_address(&stack,0x000000ffU)==-1);
    /* Ordinary unicast is assignable; the reserved class-E range is not. */
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    assert(stack.ipv4_local_address==0xc0000202U);
    assert(net_stack_set_ipv4_address(&stack,0xdf000001U)==0);
    assert(net_stack_set_ipv4_address(&stack,0xf0000001U)==-1);
    /* A zero in the host part is a valid unicast address, not "address 0". */
    assert(net_stack_set_ipv4_address(&stack,0x01000000U)==0);
}

static void test_input_argument_guards(void) {
    struct netif interface;
    struct net_stack stack;
    struct delivery delivery={0};
    uint8_t mac[6]={0x02,0,0,0,0,1};
    uint8_t frame[45];

    assert(netif_init(&interface,"test0",7,mac,1500,0,tx_noop,lock_noop,
                      unlock_noop)==0);
    assert(netif_set_link(&interface,1)==0);
    net_stack_init(&stack,delivered,&delivery);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    {
        struct net_firewall_rule rule={0};
        rule.protocol=NET_PROTO_UDP; rule.action=NET_ACTION_ALLOW;
        assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);
    }
    make_ipv4_udp(frame,0,0);

    assert(net_stack_input(0,&interface,frame,sizeof(frame))==-1);
    assert(net_stack_input(&stack,0,frame,sizeof(frame))==-1);
    assert(net_stack_input(&stack,&interface,0,sizeof(frame))==-1);
    /* None of the rejected calls reached the frame counter. */
    assert(stack.stats.frames==0);

    /* Link down: the frame is malformed for this interface, not dropped by
     * policy. */
    assert(netif_set_link(&interface,0)==0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    assert(stack.stats.malformed==1 && stack.stats.frames==1);
    assert(netif_set_link(&interface,1)==0);

    /* A zero source MAC and a group-bit source MAC are both refused. */
    make_ipv4_udp(frame,0,0);
    for (uint32_t i=0;i<6;i++) frame[6+i]=0;
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    make_ipv4_udp(frame,0,0);
    frame[6]=0x03;
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    assert(stack.stats.malformed==3);
}

static void test_input_destination_filtering(void) {
    struct netif interface;
    struct net_stack stack;
    struct delivery delivery={0};
    uint8_t mac[6]={0x02,0,0,0,0,1};
    uint8_t frame[45];
    struct net_firewall_rule rule={0};

    assert(netif_init(&interface,"test0",7,mac,1500,0,tx_noop,lock_noop,
                      unlock_noop)==0);
    assert(netif_set_link(&interface,1)==0);
    net_stack_init(&stack,delivered,&delivery);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    rule.protocol=NET_PROTO_UDP; rule.action=NET_ACTION_ALLOW;
    assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);

    /* Broadcast and multicast Ethernet destinations are allowed through to
     * IP policy. */
    make_ipv4_udp(frame,0,0);
    for (uint32_t i=0;i<6;i++) frame[i]=0xff;
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==0);
    assert(delivery.count==1);

    make_ipv4_udp(frame,0,0);
    frame[0]=0x33; frame[1]=0x33;
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==0);
    assert(delivery.count==2);

    /* A unicast destination for another host is dropped by policy. */
    make_ipv4_udp(frame,0,0);
    frame[5]=0x09;
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    assert(stack.stats.policy_drops==1);
    assert(delivery.count==2);
}

static void test_input_address_and_protocol_policy(void) {
    struct netif interface;
    struct net_stack stack;
    struct delivery delivery={0};
    uint8_t mac[6]={0x02,0,0,0,0,1};
    uint8_t frame[45];
    struct net_firewall_rule rule={0};

    assert(netif_init(&interface,"test0",7,mac,1500,0,tx_noop,lock_noop,
                      unlock_noop)==0);
    assert(netif_set_link(&interface,1)==0);
    net_stack_init(&stack,delivered,&delivery);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    rule.protocol=NET_PROTO_ANY; rule.action=NET_ACTION_ALLOW;
    assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);

    /* The limited broadcast address is accepted alongside our own address. */
    make_ipv4_udp(frame,0,0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==0);
    assert(delivery.count==1);
    {
        uint8_t *ip=frame+14;
        ip[16]=ip[17]=ip[18]=ip[19]=0xff;
        ip[10]=ip[11]=0;
        uint16_t c=checksum(sum_bytes(0,ip,20));
        ip[10]=(uint8_t)(c>>8); ip[11]=(uint8_t)c;
        /* The UDP checksum covers the pseudo-header, so rebuild it. */
        uint8_t *udp=ip+20;
        udp[6]=udp[7]=0;
        uint32_t sum=sum_bytes(0,ip+12,8);
        uint8_t pseudo[4]={0,17,0,11};
        sum=sum_bytes(sum,pseudo,4);
        sum=sum_bytes(sum,udp,11);
        uint16_t uc=checksum(sum);
        if (!uc) uc=0xffff;
        udp[6]=(uint8_t)(uc>>8); udp[7]=(uint8_t)uc;
    }
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==0);
    assert(delivery.count==2);
    assert(delivery.destination[12]==0xff && delivery.destination[15]==0xff);

    /* A unicast address that is not ours is a policy drop. */
    make_ipv4_udp(frame,0,0);
    {
        uint8_t *ip=frame+14;
        ip[19]=0x09;
        ip[10]=ip[11]=0;
        uint16_t c=checksum(sum_bytes(0,ip,20));
        ip[10]=(uint8_t)(c>>8); ip[11]=(uint8_t)c;
    }
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    assert(stack.stats.policy_drops==1);

    /* A non-UDP protocol is counted as unsupported, not malformed. */
    make_ipv4_udp(frame,0,0);
    {
        uint8_t *ip=frame+14;
        ip[9]=6; /* TCP */
        ip[10]=ip[11]=0;
        uint16_t c=checksum(sum_bytes(0,ip,20));
        ip[10]=(uint8_t)(c>>8); ip[11]=(uint8_t)c;
    }
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    assert(stack.stats.unsupported==1);
    assert(stack.stats.malformed==0);

    /* Without a UDP callback the datagram cannot be delivered. */
    net_stack_init(&stack,0,0);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    rule.protocol=NET_PROTO_UDP; rule.action=NET_ACTION_ALLOW;
    assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);
    make_ipv4_udp(frame,0,0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    assert(stack.stats.unsupported==1);
}

static void test_input_unsupported_families(void) {
    struct netif interface;
    struct net_stack stack;
    struct delivery delivery={0};
    uint8_t mac[6]={0x02,0,0,0,0,1};
    uint8_t frame[64];

    assert(netif_init(&interface,"test0",7,mac,1500,0,tx_noop,lock_noop,
                      unlock_noop)==0);
    assert(netif_set_link(&interface,1)==0);
    net_stack_init(&stack,delivered,&delivery);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);

    /* IPv6 parses but is deliberately fail-closed pending a policy. */
    make_ipv6(frame);
    assert(net_stack_input(&stack,&interface,frame,14+40)==1);
    assert(stack.stats.unsupported==1);
    assert(stack.stats.malformed==0);
    assert(delivery.count==0);

    /* A malformed IPv6 header is malformed, not unsupported. */
    make_ipv6(frame);
    frame[14+1]=0xff; /* payload length beyond the capture */
    frame[14+4]=0xff;
    frame[14+5]=0xff;
    assert(net_stack_input(&stack,&interface,frame,14+40)==-1);
    assert(stack.stats.malformed==1);

    /* Any other ethertype is unsupported. */
    make_ipv6(frame);
    frame[12]=0x08; frame[13]=0x06; /* ARP */
    assert(net_stack_input(&stack,&interface,frame,14+40)==1);
    assert(stack.stats.unsupported==2);
}

static void test_input_udp_framing_and_callback(void) {
    struct netif interface;
    struct net_stack stack;
    struct delivery delivery={0};
    uint8_t mac[6]={0x02,0,0,0,0,1};
    uint8_t frame[45];
    struct net_firewall_rule rule={0};

    assert(netif_init(&interface,"test0",7,mac,1500,0,tx_noop,lock_noop,
                      unlock_noop)==0);
    assert(netif_set_link(&interface,1)==0);
    net_stack_init(&stack,delivered,&delivery);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    rule.protocol=NET_PROTO_UDP; rule.action=NET_ACTION_ALLOW;
    assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);

    /* The UDP length field must agree with the IPv4 payload length. */
    make_ipv4_udp(frame,0,0);
    frame[14+20+5]=10;
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    assert(stack.stats.malformed==1);
    assert(delivery.count==0);

    /* A UDP header that does not fit in the IPv4 payload is malformed. */
    make_ipv4_udp(frame,0,0);
    frame[14+2]=0; frame[14+3]=24; /* total_length 24 < 20 + 8 */
    {
        uint8_t *ip=frame+14;
        ip[10]=ip[11]=0;
        uint16_t c=checksum(sum_bytes(0,ip,20));
        ip[10]=(uint8_t)(c>>8); ip[11]=(uint8_t)c;
    }
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    assert(stack.stats.malformed==2);

    /* A failing callback is counted and reported as an error. */
    net_stack_init(&stack,failing_delivery,0);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);
    make_ipv4_udp(frame,0,0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    assert(stack.stats.callback_errors==1);
    assert(stack.stats.udp_delivered==0);
}

static void test_poll_budget(void) {
    struct netif interface;
    struct net_stack stack;
    struct delivery delivery={0};
    uint8_t mac[6]={0x02,0,0,0,0,1};
    uint8_t frame[45];
    struct net_firewall_rule rule={0};

    assert(netif_init(&interface,"test0",7,mac,1500,0,tx_noop,lock_noop,
                      unlock_noop)==0);
    assert(netif_set_link(&interface,1)==0);
    net_stack_init(&stack,delivered,&delivery);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    rule.protocol=NET_PROTO_UDP; rule.action=NET_ACTION_ALLOW;
    assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);
    make_ipv4_udp(frame,0,0);

    /* Argument guards. */
    assert(net_stack_poll(0,&interface,1)==-1);
    assert(net_stack_poll(&stack,0,1)==-1);
    assert(net_stack_poll(&stack,&interface,0)==-1);

    /* An empty queue is not an error: poll returns 0 processed. */
    assert(net_stack_poll(&stack,&interface,4)==0);

    /* A budget larger than the queue drains it without spinning. */
    assert(netif_receive(&interface,frame,sizeof(frame))==0);
    assert(netif_receive(&interface,frame,sizeof(frame))==0);
    assert(net_stack_poll(&stack,&interface,100)==2);
    assert(delivery.count==2);
    assert(net_stack_poll(&stack,&interface,100)==0);

    /* A budget smaller than the queue leaves the remainder for later. */
    for (uint32_t i=0;i<NETIF_RX_QUEUE;i++)
        assert(netif_receive(&interface,frame,sizeof(frame))==0);
    assert(net_stack_poll(&stack,&interface,3)==3);
    assert(net_stack_poll(&stack,&interface,3)==3);
    assert(net_stack_poll(&stack,&interface,3)==2);
    assert(net_stack_poll(&stack,&interface,3)==0);
    assert(delivery.count==2+NETIF_RX_QUEUE);
}

int main(void) {
    struct netif interface;
    struct net_stack stack;
    struct delivery delivery={0};
    uint8_t mac[6]={0x02,0,0,0,0,1};
    uint8_t frame[45];
    struct net_firewall_rule rule={0};

    test_stack_init_and_address();
    test_input_argument_guards();
    test_input_destination_filtering();
    test_input_address_and_protocol_policy();
    test_input_unsupported_families();
    test_input_udp_framing_and_callback();
    test_poll_budget();
    assert(netif_init(&interface,"test0",7,mac,1500,0,tx_noop,lock_noop,unlock_noop)==0);
    assert(netif_set_link(&interface,1)==0);
    net_stack_init(&stack,delivered,&delivery);
    assert(net_stack_set_ipv4_address(&stack,0)==-1);
    assert(net_stack_set_ipv4_address(&stack,0xe0000001U)==-1);

    make_ipv4_udp(frame,0,0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    assert(stack.stats.policy_drops==1 && delivery.count==0); /* no address */
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    assert(stack.stats.policy_drops==2 && delivery.count==0); /* firewall default deny */

    rule.protocol=NET_PROTO_UDP; rule.action=NET_ACTION_ALLOW;
    assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==0);
    assert(delivery.count==1 && delivery.family==NET_STACK_FAMILY_IPV4);
    assert(delivery.interface_index==7 && delivery.source_port==1234 &&
           delivery.destination_port==9999 && delivery.length==3);
    assert(delivery.payload[0]=='n' && delivery.payload[2]=='t');
    assert(delivery.source[10]==0xff && delivery.source[11]==0xff &&
           delivery.source[12]==192 && delivery.source[15]==1);

    make_ipv4_udp(frame,0,1);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    assert(stack.stats.udp_checksum_errors==1 && delivery.count==1);
    make_ipv4_udp(frame,0x2000,0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    assert(stack.stats.fragments==1 && delivery.count==1);

    make_ipv4_udp(frame,0,0);
    frame[14+10]^=1; /* Invalid IPv4 header checksum. */
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    assert(stack.stats.malformed==1);
    assert(net_stack_input(&stack,&interface,frame,13)==-1);
    make_ipv4_udp(frame,0,0);
    frame[6]=0x01; /* Source MACs must be unicast. */
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==-1);
    uint8_t oversized[1600];
    for (uint32_t i=0;i<sizeof(oversized);i++) oversized[i]=0;
    assert(net_stack_input(&stack,&interface,oversized,sizeof(oversized))==-1);
    assert(stack.stats.malformed==4);

    make_ipv4_udp(frame,0,0);
    frame[14+16+3]=99; /* Validly re-checksummed unicast for another host. */
    frame[14+10]=frame[14+11]=0;
    uint16_t ip_checksum=checksum(sum_bytes(0,frame+14,20));
    frame[14+10]=(uint8_t)(ip_checksum>>8); frame[14+11]=(uint8_t)ip_checksum;
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    make_ipv4_udp(frame,0,0);
    frame[0]=0x04; /* Not our unicast MAC. */
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==1);
    assert(stack.stats.policy_drops==4);

    /* A bounded poll drains only the caller's budget, leaving work queued. */
    make_ipv4_udp(frame,0,0);
    assert(netif_receive(&interface,frame,sizeof(frame))==0);
    assert(netif_receive(&interface,frame,sizeof(frame))==0);
    assert(net_stack_poll(&stack,&interface,1)==1);
    assert(delivery.count==2);
    assert(net_stack_poll(&stack,&interface,1)==1);
    assert(delivery.count==3);
    assert(net_stack_poll(&stack,&interface,4)==0);
    assert(stack.stats.udp_delivered==3);

    /* End-to-end receive path: validated IPv4/UDP is demultiplexed into an
     * owner-bound, generation-tagged bounded UDP endpoint. */
    struct net_udp_socket_table sockets;
    struct net_udp_receive_info socket_info;
    uint64_t socket_handle;
    uint8_t socket_payload[16];
    net_udp_socket_table_init(&sockets);
    assert(net_udp_socket_bind(&sockets,42,0xc0000202U,9999,
                               &socket_handle)==0);
    net_stack_init(&stack,net_udp_socket_dispatch,&sockets);
    assert(net_stack_set_ipv4_address(&stack,0xc0000202U)==0);
    assert(net_firewall_add(&stack.ipv4_firewall,&rule)==0);
    make_ipv4_udp(frame,0,0);
    assert(net_stack_input(&stack,&interface,frame,sizeof(frame))==0);
    assert(net_udp_socket_receive(&sockets,42,socket_handle,socket_payload,
                                  sizeof(socket_payload),&socket_info)==0);
    assert(socket_info.source_address==0xc0000201U &&
           socket_info.source_port==1234 && socket_info.length==3);
    assert(socket_payload[0]=='n' && socket_payload[2]=='t');
    assert(net_udp_socket_receive(&sockets,7,socket_handle,socket_payload,
                                  sizeof(socket_payload),&socket_info)==-1);
    return 0;
}
