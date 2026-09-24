#include <assert.h>
#include "../kernel/dhcp_core.h"
int main(void){uint8_t p[244]={0};p[0]=2;p[1]=1;p[2]=6;p[4]=0x12;p[5]=0x34;p[236]=0x63;p[237]=0x82;p[238]=0x53;p[239]=0x63;p[240]=53;p[241]=1;p[242]=2;p[243]=255;struct dhcp_view v;assert(dhcp_parse(p,sizeof(p),&v)==0&&v.message_type==2&&v.xid==0x12340000);p[241]=9;assert(dhcp_parse(p,sizeof(p),&v)==-1);return 0;}
