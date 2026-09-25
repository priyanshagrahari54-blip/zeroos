#ifndef ZEROOS_DHCP_CORE_H
#define ZEROOS_DHCP_CORE_H
#include "types.h"
struct dhcp_view { uint32_t xid,client_ip,your_ip,server_ip,server_identifier,subnet_mask,lease_seconds,renewal_seconds,rebind_seconds; uint8_t op,hardware_type,hardware_length,message_type; const uint8_t *client_hardware; };
enum dhcp_state { DHCP_STOPPED,DHCP_SELECTING,DHCP_REQUESTING,DHCP_BOUND,DHCP_RENEWING,DHCP_REBINDING,DHCP_FAILED };
enum dhcp_action { DHCP_ACTION_NONE,DHCP_ACTION_DISCOVER,DHCP_ACTION_REQUEST,DHCP_ACTION_RENEW,DHCP_ACTION_REBIND,DHCP_ACTION_EXPIRED };
struct dhcp_client { enum dhcp_state state; uint32_t xid,address,server,lease_start,lease_seconds,renewal_seconds,rebind_seconds,deadline,retries; };
int dhcp_parse(const uint8_t *packet,uint32_t length,struct dhcp_view *out);
int dhcp_client_start(struct dhcp_client *client,uint32_t xid,uint32_t now);
int dhcp_client_receive(struct dhcp_client *client,const uint8_t *packet,uint32_t length,uint32_t now);
enum dhcp_action dhcp_client_tick(struct dhcp_client *client,uint32_t now,uint32_t retry_interval,uint32_t retry_limit);
#endif
