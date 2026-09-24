#include <assert.h>
#include "../kernel/net_core.h"
int main(void) {
    struct net_firewall fw; net_firewall_init(&fw);
    struct net_ipv4_view p={0}; p.source=0x0a000001U; p.destination=0x08080808U; p.protocol=NET_PROTO_UDP;
    assert(net_firewall_check(&fw,&p)==NET_ACTION_DENY);
    struct net_firewall_rule r={0}; r.source=0x0a000000U; r.source_mask=0xffffff00U; r.protocol=NET_PROTO_UDP; r.action=NET_ACTION_ALLOW;
    assert(net_firewall_add(&fw,&r)==0); assert(net_firewall_check(&fw,&p)==NET_ACTION_ALLOW);
    assert(fw.accepted==1 && fw.denied==1); assert(net_firewall_remove(&fw,0)==0);
    assert(net_firewall_check(&fw,&p)==NET_ACTION_DENY);
    uint8_t bad[20]={0x45}; struct net_ipv4_view out;
    assert(net_ipv4_parse(bad,sizeof(bad),&out)==-1);
    return 0;
}
