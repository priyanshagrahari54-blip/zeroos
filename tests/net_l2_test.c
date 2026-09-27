#include <assert.h>
#include "../kernel/net_l2.h"
int main(void){uint8_t f[18]={0};f[12]=0x81;f[13]=0;f[16]=8;f[17]=0;struct net_eth_view e;assert(net_ethernet_parse(f,sizeof(f),&e)==0&&e.ethertype==0x0800&&e.payload_length==0);uint8_t a[28]={0,1,8,0,6,4,0,1};struct net_arp_view v;assert(net_arp_parse(a,sizeof(a),&v)==0&&v.operation==1);assert(net_arp_parse(a,9,&v)==-1);return 0;}
