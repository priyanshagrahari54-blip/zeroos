#ifndef ZEROOS_FIREWALL_H
#define ZEROOS_FIREWALL_H

#include "types.h"
#include "net_transport.h"

#define ZD_FW_MAX_RULES 32
#define ZD_FW_APP 32

enum zd_fw_action { ZD_FW_DENY = 0, ZD_FW_ALLOW = 1 };
enum zd_fw_dir { ZD_FW_IN = 0, ZD_FW_OUT = 1 };
enum zd_fw_proto { ZD_FW_ANY = 0, ZD_FW_TCP, ZD_FW_UDP, ZD_FW_ICMP };

struct zd_fw_flow {
    int dir;                    /* enum zd_fw_dir */
    int proto;                  /* enum zd_fw_proto */
    uint32_t src_ip, dst_ip;    /* host byte order */
    uint16_t src_port, dst_port;
    int conn_known;             /* 1 = established/related */
    const char *app_id;         /* "" or NULL = system traffic */
};

struct zd_fw_rule {
    uint32_t id;                /* assigned by add_rule */
    uint8_t enabled;
    uint8_t dir;                /* enum zd_fw_dir */
    uint8_t proto;              /* enum zd_fw_proto */
    uint8_t action;             /* enum zd_fw_action */
    uint8_t established_only;   /* 1 = matches only conn_known flows */
    uint16_t port_lo, port_hi;  /* inclusive 0..65535; 0/0 = any */
    uint32_t ip_lo, ip_hi;      /* inclusive range; 0/0 = any */
    char app[ZD_FW_APP];        /* "" = any app */
};

struct zd_fw {
    struct zd_fw_rule rules[ZD_FW_MAX_RULES];
    uint32_t rule_count;
    uint32_t next_id;
    struct {
        uint32_t flows, allowed, denied, invalid_flows,
                 rules_added, rules_removed, rules_rejected;
    } stats;
};

void net_fw_init(struct zd_fw *fw);
int net_fw_add(struct zd_fw *fw, const struct zd_fw_rule *tmpl, int front);
int net_fw_remove(struct zd_fw *fw, uint32_t id);
int net_fw_decide(struct zd_fw *fw, const struct zd_fw_flow *flow);
/* Live-path enforcement wrappers for TCP, ICMP, and IPv6. */
int net_fw_check_tcp(const struct zd_fw *fw,
                     const struct net_tcp_conn *c,
                     uint32_t src_ip, uint32_t dst_ip,
                     uint16_t src_port, uint16_t dst_port);
int net_fw_check_icmp(const struct zd_fw *fw,
                      uint32_t src_ip, uint32_t dst_ip,
                      uint8_t icmp_type, uint8_t icmp_code);
/* Fails closed for unsupported next headers and matching address-scoped
 * rules because the current rule format stores IPv4 ranges. */
int net_fw_check_ipv6(const struct zd_fw *fw,
                      const uint8_t src[16],
                      const uint8_t dst[16],
                      uint8_t nexthdr, uint16_t src_port,
                      uint16_t dst_port);

#endif
