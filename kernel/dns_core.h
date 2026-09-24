#ifndef ZEROOS_DNS_CORE_H
#define ZEROOS_DNS_CORE_H
#include "types.h"
#define DNS_PACKET_MAX 4096
#define DNS_RECORD_MAX 256
struct dns_summary { uint16_t id,questions,answers,authorities,additional; uint8_t response,rcode; };
int dns_validate_message(const uint8_t *message,uint32_t length,struct dns_summary *out);
#endif
