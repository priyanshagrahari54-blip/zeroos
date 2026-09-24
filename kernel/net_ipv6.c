#include "net_ipv6.h"
static uint16_t be16(const uint8_t*p){return(uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
int net_ipv6_parse(const uint8_t*p,uint32_t n,struct net_ipv6_view*out){if(!p||!out||n<40||(p[0]>>4)!=6)return -1;uint32_t plen=be16(p+4);if(plen>n-40)return -1;out->traffic_class=(uint8_t)(((p[0]&15U)<<4)|(p[1]>>4));out->flow_label=((uint32_t)(p[1]&15U)<<16)|((uint32_t)p[2]<<8)|p[3];out->payload_length=plen;out->next_header=p[6];out->hop_limit=p[7];for(uint32_t i=0;i<16;i++){out->source[i]=p[8+i];out->destination[i]=p[24+i];}out->payload=p+40;return 0;}
