#ifndef ZEROOS_NET_CONNTRACK_H
#define ZEROOS_NET_CONNTRACK_H
#include "types.h"
#define NET_CONNTRACK_CAPACITY 64
struct net_flow { uint32_t source,destination; uint16_t source_port,destination_port; uint8_t protocol; };
struct net_flow_entry { struct net_flow flow; uint32_t expires; uint8_t active,seen_reverse; };
struct net_conntrack { struct net_flow_entry entries[NET_CONNTRACK_CAPACITY]; uint32_t inserts,evictions; };
/* Returns 0 when the flow had to be inserted (first seen), 1 when it was
 * already tracked in either direction, and -1 when capacity prevents
 * admission. net_conntrack_expire() runs first, so an expired entry is
 * reclaimed before the table is consulted and reads as first-seen again. */
int net_conntrack_observe(struct net_conntrack *table,const struct net_flow *flow,uint32_t now,uint32_t lifetime);
void net_conntrack_expire(struct net_conntrack *table,uint32_t now);
#endif
