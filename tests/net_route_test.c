#include <assert.h>
#include "../kernel/net_route.h"
int main(void){struct net_routes t={0};struct net_route d={0,0,1,1,100,1},n={0x0a000000,0xff000000,0,2,10,1};assert(net_route_add(&t,&d)==0&&net_route_add(&t,&n)==0);struct net_route out;assert(net_route_lookup(&t,0x0a000001,&out)==0&&out.interface_id==2);assert(net_route_lookup(&t,0x08080808,&out)==0&&out.interface_id==1);assert(net_route_remove(&t,n.prefix,n.mask,n.interface_id)==0);assert(net_route_lookup(&t,0x0a000001,&out)==0&&out.interface_id==1);return 0;}
