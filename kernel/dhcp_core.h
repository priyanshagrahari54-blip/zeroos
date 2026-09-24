#ifndef ZEROOS_DHCP_CORE_H
#define ZEROOS_DHCP_CORE_H
#include "types.h"
struct dhcp_view { uint32_t xid,client_ip,your_ip,server_ip; uint8_t op,hardware_type,hardware_length,message_type; const uint8_t *client_hardware; };
int dhcp_parse(const uint8_t *packet,uint32_t length,struct dhcp_view *out);
#endif
