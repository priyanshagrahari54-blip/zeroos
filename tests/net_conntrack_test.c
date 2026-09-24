#include <assert.h>
#include "../kernel/net_conntrack.h"
int main(void){struct net_conntrack t={0};struct net_flow a={1,2,100,80,6},b={2,1,80,100,6};assert(net_conntrack_observe(&t,&a,10,20)==0);assert(net_conntrack_observe(&t,&b,11,20)==1);assert(net_conntrack_observe(&t,&a,12,20)==1);net_conntrack_expire(&t,32);assert(!t.entries[0].active);assert(net_conntrack_observe(&t,&a,40,0)==-1);return 0;}
