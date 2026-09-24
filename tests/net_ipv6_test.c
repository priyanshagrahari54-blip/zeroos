#include <assert.h>
#include "../kernel/net_ipv6.h"
int main(void){uint8_t p[40]={0x60};p[6]=17;p[7]=64;struct net_ipv6_view v;assert(net_ipv6_parse(p,sizeof(p),&v)==0&&v.next_header==17&&v.hop_limit==64);p[0]=0x40;assert(net_ipv6_parse(p,sizeof(p),&v)==-1);return 0;}
