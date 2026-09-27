#ifndef ZEROOS_NETIF_H
#define ZEROOS_NETIF_H
#include "types.h"
#define NETIF_MAX_NAME 16
#define NETIF_RX_QUEUE 8
#define NETIF_FRAME_MAX 2048
#define NETIF_MTU_MIN 576
#define NETIF_MTU_MAX 9000
#define NETIF_FLAG_UP 1U
#define NETIF_FLAG_LINK 2U
struct netif_packet { uint16_t length; uint8_t bytes[NETIF_FRAME_MAX]; };
struct netif_stats { uint64_t rx_packets,tx_packets,rx_dropped,tx_dropped,rx_errors,tx_errors; };
typedef int (*netif_tx_fn)(void *context,const uint8_t *frame,uint16_t length);
typedef uint64_t (*netif_lock_fn)(void *context);
typedef void (*netif_unlock_fn)(void *context,uint64_t state);
struct netif { char name[NETIF_MAX_NAME]; uint8_t address[6]; uint16_t mtu; uint32_t index,flags; void *context; netif_tx_fn transmit; netif_lock_fn lock; netif_unlock_fn unlock; uint8_t head,tail,count; struct netif_packet rx[NETIF_RX_QUEUE]; struct netif_stats stats; };
int netif_init(struct netif *interface,const char *name,uint32_t index,const uint8_t address[6],uint16_t mtu,void *context,netif_tx_fn transmit,netif_lock_fn lock,netif_unlock_fn unlock);
int netif_set_link(struct netif *interface,int up);
/* IRQ/DMA completion producers submit a complete Ethernet frame; no polling. */
int netif_receive(struct netif *interface,const uint8_t *frame,uint32_t length);
int netif_dequeue(struct netif *interface,uint8_t *buffer,uint32_t capacity,uint16_t *length);
int netif_send(struct netif *interface,const uint8_t *frame,uint32_t length);
#endif
