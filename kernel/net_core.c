#include "net_core.h"
static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
int net_ipv4_parse(const uint8_t *p,uint32_t n,struct net_ipv4_view *o) {
    if(!p||!o||n<20) return -1;
    uint8_t version=p[0]>>4, ihl=(uint8_t)(p[0]&15U);
    if(version!=4 || ihl<5 || ihl>15) return -1;
    uint32_t h=(uint32_t)ihl*4U; uint16_t total=be16(p+2);
    if(h>n || total<h || total>n) return -1;
    uint32_t sum=0;
    for(uint32_t i=0;i<h;i+=2) sum+=(uint32_t)be16(p+i);
    while(sum>>16) sum=(sum&0xffffU)+(sum>>16);
    if((uint16_t)sum!=0xffffU) return -1;
    uint16_t fragment=be16(p+6);
    if (fragment&0x8000U) return -1; /* Reserved IPv4 flag. */
    o->header_length=(uint8_t)h; o->total_length=total; o->protocol=p[9];
    o->source=be32(p+12); o->destination=be32(p+16);
    o->fragment_offset=(uint16_t)(fragment&0x1fffU);
    o->more_fragments=(uint8_t)((fragment&0x2000U)!=0);
    o->is_fragment=(uint8_t)(o->fragment_offset!=0||o->more_fragments);
    return 0;
}
void net_firewall_init(struct net_firewall *f) { if(!f)return; uint8_t *p=(uint8_t *)f; for(uint32_t i=0;i<sizeof(*f);i++)p[i]=0; }
int net_firewall_add(struct net_firewall *f,const struct net_firewall_rule *r) {
    if(!f||!r||f->count>=NET_MAX_FIREWALL_RULES||r->action>NET_ACTION_ALLOW)return -1;
    f->rules[f->count++]=*r; return 0;
}
int net_firewall_remove(struct net_firewall *f,uint32_t i) {
    if(!f||i>=f->count)return -1;
    for(uint32_t j=i+1;j<f->count;j++)f->rules[j-1]=f->rules[j];
    f->count--; return 0;
}
int net_firewall_check(struct net_firewall *f,const struct net_ipv4_view *p) {
    if(!f||!p)return NET_ACTION_DENY;
    for(uint32_t i=0;i<f->count;i++) {
        const struct net_firewall_rule *r=&f->rules[i];
        if((r->protocol==NET_PROTO_ANY||r->protocol==p->protocol)&&
           ((p->source&r->source_mask)==(r->source&r->source_mask))&&
           ((p->destination&r->destination_mask)==(r->destination&r->destination_mask))) {
            if(r->action==NET_ACTION_ALLOW)f->accepted++;else f->denied++;
            return r->action;
        }
    }
    f->denied++; return NET_ACTION_DENY;
}
