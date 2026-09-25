/* Network firewall policy core (Stage 5 part B).
 * Pure decision engine over explicit rules: first match wins, default
 * DENY.  Runs in userspace policy service; the kernel packet path
 * feeds it flows (device integration binds later).  Host-testable. */
#ifndef ZEROOS_DESKTOP_FIREWALL_H
#define ZEROOS_DESKTOP_FIREWALL_H

#include <stdint.h>

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
    uint16_t port_lo, port_hi;  /* inclusive, 0..65535; 0/0 = any */
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

void zd_fw_init(struct zd_fw *fw);
/* Insert at the front (highest precedence) or back.  Full -> -28. */
int zd_fw_add(struct zd_fw *fw, const struct zd_fw_rule *tmpl, int front);
int zd_fw_remove(struct zd_fw *fw, uint32_t id);   /* -2 if unknown */
/* First-match decision; invalid flow -> -22 and stats.invalid_flows. */
int zd_fw_decide(struct zd_fw *fw, const struct zd_fw_flow *flow);
/* Count enabled rules matching a flow without deciding (planning). */
uint32_t zd_fw_matching(const struct zd_fw *fw, const struct zd_fw_flow *flow);

#endif /* ZEROOS_DESKTOP_FIREWALL_H */
