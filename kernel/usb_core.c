#include "usb_core.h"
int usb_validate_descriptors(const uint8_t *p,uint32_t n,struct usb_descriptor_summary *o) {
    if(!p||!o||!n||n>USB_DESC_LIMIT)return -1;
    o->count=0;o->device_seen=0;o->configuration_seen=0;uint32_t config_end=0;
    for(uint32_t off=0;off<n;) {
        if(config_end&&off>=config_end){if(off!=config_end)return -1;config_end=0;}
        if(n-off<2)return -1;
        uint8_t len=p[off],type=p[off+1];
        if(len<2||len>n-off||(config_end&&off+len>config_end))return -1;
        if(type==1) { if(len<18||o->device_seen)return -1; o->device_seen=1; }
        if(type==2) { if(len<9||config_end)return -1; uint32_t total=(uint32_t)p[off+2]|((uint32_t)p[off+3]<<8); if(total<len||total>n-off)return -1; config_end=off+total; o->configuration_seen=1; }
        if(type==4&&len<9)return -1;
        if(type==5&&len<7)return -1;
        if(type==0x21&&len<6)return -1;
        if(type==0x22&&len<2)return -1;
        if(o->count==0xffffU)return -1;
        o->count++; off+=len;
        if(config_end&&off==config_end)config_end=0;
    }
    return config_end ? -1 : 0;
}
