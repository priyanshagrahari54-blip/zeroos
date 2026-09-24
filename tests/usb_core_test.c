#include <assert.h>
#include "../kernel/usb_core.h"
int main(void){struct usb_descriptor_summary s;uint8_t good[]={18,1,0,2,0,0,0,64,0,0,0,0,0,0,0,0,0,0,9,2,9,0,0,1,0,0x80,50};assert(usb_validate_descriptors(good,sizeof(good),&s)==0&&s.count==2&&s.device_seen);uint8_t malformed[]={1,2};assert(usb_validate_descriptors(malformed,sizeof(malformed),&s)==-1);return 0;}
