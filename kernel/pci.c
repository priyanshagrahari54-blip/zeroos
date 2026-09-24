#include "pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
static inline void outl(uint16_t port,uint32_t value) { __asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t value; __asm__ volatile("inl %1,%0":"=a"(value):"Nd"(port)); return value; }
static uint32_t read_config(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t reg) {
    uint32_t address=0x80000000U|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)fn<<8)|(reg&0xfcU);
    outl(PCI_CONFIG_ADDRESS,address); return inl(PCI_CONFIG_DATA);
}
static uint8_t byte(uint8_t b,uint8_t s,uint8_t f,uint8_t r) { return (uint8_t)(read_config(b,s,f,r)>>(8*(r&3U))); }
static void parse_bars(struct pci_device *d) {
    uint8_t n=(d->header_type&0x7fU)==0?6:((d->header_type&0x7fU)==1?2:0);
    for(uint8_t i=0;i<n;i++) {
        uint32_t v=read_config(d->bus,d->slot,d->function,(uint8_t)(0x10+i*4));
        if(v==0 || v==0xffffffffU) continue;
        if(v&1U) { d->bars[i].kind=PCI_BAR_IO; d->bars[i].address=(uint64_t)(v&~3U); }
        else {
            uint8_t type=(uint8_t)((v>>1)&3U); d->bars[i].kind=PCI_BAR_MEMORY;
            d->bars[i].address=(uint64_t)(v&~15U); d->bars[i].prefetchable=(uint8_t)((v>>3)&1U);
            if(type==2 && i+1<n) { d->bars[i].is_64bit=1; d->bars[i].address|=(uint64_t)read_config(d->bus,d->slot,d->function,(uint8_t)(0x14+i*4))<<32; i++; }
            else if(type==3) d->bars[i].kind=PCI_BAR_NONE;
        }
    }
}
static void parse_caps(struct pci_device *d) {
    if(!(d->status&PCI_STATUS_CAP_LIST)) return;
    uint8_t pos=(uint8_t)(byte(d->bus,d->slot,d->function,0x34)&~3U);
    uint8_t seen[64]={0};
    while(pos>=0x40 && pos<=0xfc && d->capability_count<PCI_MAX_CAPABILITIES) {
        uint8_t index=(uint8_t)((pos-0x40)/4);
        if(seen[index]) { d->capability_count=0; return; }
        seen[index]=1;
        struct pci_capability *c=&d->capabilities[d->capability_count++];
        c->id=byte(d->bus,d->slot,d->function,pos); c->offset=pos;
        uint8_t next=(uint8_t)(byte(d->bus,d->slot,d->function,(uint8_t)(pos+1))&~3U);
        if(next==pos) { d->capability_count=0; return; }
        pos=next;
    }
    if(pos && (pos<0x40 || pos>0xfc)) d->capability_count=0;
}
static int scan_function(struct pci_inventory *o,uint8_t b,uint8_t s,uint8_t f) {
    uint32_t id=read_config(b,s,f,0); uint16_t vendor=(uint16_t)id;
    if(vendor==0xffffU || vendor==0) return 0;
    if(o->count==PCI_MAX_DEVICES) { o->truncated=1; return -1; }
    struct pci_device *d=&o->devices[o->count++];
    d->bus=b; d->slot=s; d->function=f; d->vendor_id=vendor; d->device_id=(uint16_t)(id>>16);
    uint32_t classrev=read_config(b,s,f,8); d->revision=(uint8_t)classrev;
    d->programming_interface=(uint8_t)(classrev>>8); d->subclass=(uint8_t)(classrev>>16); d->class_code=(uint8_t)(classrev>>24);
    uint32_t cmdstat=read_config(b,s,f,4); d->command=(uint16_t)cmdstat; d->status=(uint16_t)(cmdstat>>16);
    d->header_type=byte(b,s,f,0x0e); parse_bars(d); parse_caps(d);
    return 1;
}
int pci_enumerate(struct pci_inventory *out) {
    if(!out) return -1;
    uint8_t *p=(uint8_t *)out; for(uint32_t i=0;i<sizeof(*out);i++) p[i]=0;
    /* Mechanism #1 has no portable segment discovery; explicitly scan segment 0 buses. */
    for(uint16_t b=0;b<256;b++) for(uint8_t s=0;s<32;s++) {
        if((uint16_t)read_config((uint8_t)b,s,0,0)==0xffffU) continue;
        uint8_t functions=(byte((uint8_t)b,s,0,0x0e)&0x80U)?8:1;
        for(uint8_t f=0;f<functions;f++) if(scan_function(out,(uint8_t)b,s,f)<0) return 1;
    }
    return 0;
}
