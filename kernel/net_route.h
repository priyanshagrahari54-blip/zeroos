#ifndef ZEROOS_NET_ROUTE_H
#define ZEROOS_NET_ROUTE_H
#include "types.h"
#define NET_MAX_ROUTES 32
struct net_route { uint32_t prefix,mask,gateway,interface_id,metric; uint8_t active; };
struct net_routes { struct net_route entries[NET_MAX_ROUTES]; uint32_t count; };
int net_route_add(struct net_routes *table,const struct net_route *route);
int net_route_remove(struct net_routes *table,uint32_t prefix,uint32_t mask,uint32_t interface_id);
int net_route_lookup(const struct net_routes *table,uint32_t destination,struct net_route *result);
#endif
