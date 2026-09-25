#include <assert.h>
#include "../kernel/net_stack.h"
#include "../kernel/net_l2.h"

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

int main(void) {
    struct netif interface;
    struct net_stack stack;
    struct delivery delivery={0};
    uint8_t mac[6]={0x02,0,0,0,0,1};
    uint8_t frame[45];
    struct net_firewall_rule rule={0};
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
    return 0;
}
