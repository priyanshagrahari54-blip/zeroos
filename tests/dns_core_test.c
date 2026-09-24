#include <assert.h>
#include "../kernel/dns_core.h"
int main(void){uint8_t q[]={0x12,0x34,1,0,0,1,0,0,0,0,0,0,1,'a',0,0,1,0,1};struct dns_summary s;assert(dns_validate_message(q,sizeof(q),&s)==0&&s.questions==1&&s.id==0x1234&&!s.response);uint8_t bad[]={0,1,1,0,0,1,0,0,0,0,0,0,0xc,0,0,1,0,1};assert(dns_validate_message(bad,sizeof(bad),&s)==-1);return 0;}
