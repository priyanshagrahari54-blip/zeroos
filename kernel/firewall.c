#include "firewall.h"
#include "kstring.h"

static int fw_str_app(const char *rule_app, const char *flow_app) {
    if (!rule_app[0])
        return 1;
    if (!flow_app || !flow_app[0])
        return 0;
    while (*rule_app && *flow_app && *rule_app == *flow_app) {
        ++rule_app;
        ++flow_app;
    }
    return *rule_app == 0 && *flow_app == 0;
}

void net_fw_init(struct zd_fw *fw) {
    if (!fw) return;
    memset(fw->rules, 0, sizeof(fw->rules));
    fw->rule_count = 0;
    fw->next_id = 1;
    memset(&fw->stats, 0, sizeof(fw->stats));
}

static int fw_valid_tmpl(const struct zd_fw_rule *t) {
    if (!t) return 0;
    if (t->dir != ZD_FW_IN && t->dir != ZD_FW_OUT) return 0;
    if (t->proto != ZD_FW_ANY && t->proto != ZD_FW_TCP &&
        t->proto != ZD_FW_UDP && t->proto != ZD_FW_ICMP) return 0;
    if (t->action != ZD_FW_DENY && t->action != ZD_FW_ALLOW) return 0;
    if (t->port_lo > t->port_hi) return 0;
    if (t->ip_lo > t->ip_hi) return 0;
    int terminated = 0;
    for (int i = 0; i < ZD_FW_APP; ++i) {
        if (!t->app[i]) {
            terminated = 1;
            break;
        }
        if ((unsigned char)t->app[i] < 0x21) return 0;
    }
    return terminated;
}

int net_fw_add(struct zd_fw *fw, const struct zd_fw_rule *tmpl, int front) {
    if (!fw || !fw_valid_tmpl(tmpl)) return -22;
    if (fw->rule_count >= ZD_FW_MAX_RULES) return -28;

    struct zd_fw_rule r = *tmpl;
    r.id = fw->next_id++;
    r.enabled = 1;

    if (front) {
        for (uint32_t i = fw->rule_count; i > 0; --i)
            fw->rules[i] = fw->rules[i - 1];
        fw->rules[0] = r;
    } else {
        fw->rules[fw->rule_count] = r;
    }
    fw->rule_count++;
    fw->stats.rules_added++;
    return 0;
}

int net_fw_remove(struct zd_fw *fw, uint32_t id) {
    if (!fw) return -22;
    for (uint32_t i = 0; i < fw->rule_count; ++i) {
        if (fw->rules[i].id == id) {
            for (uint32_t j = i + 1; j < fw->rule_count; ++j)
                fw->rules[j - 1] = fw->rules[j];
            fw->rule_count--;
            fw->stats.rules_removed++;
            return 0;
        }
    }
    return -2;
}

static int fw_match(const struct zd_fw_rule *r, const struct zd_fw_flow *f) {
    if (!r->enabled) return 0;
    if ((int)r->dir != f->dir) return 0;
    if (r->proto != ZD_FW_ANY && (int)r->proto != f->proto) return 0;
    if (r->established_only && !f->conn_known) return 0;
    if (r->port_lo || r->port_hi) {
        uint16_t port = f->dst_port;
        if (port < r->port_lo || port > r->port_hi) return 0;
    }
    if (r->ip_lo || r->ip_hi) {
        uint32_t ip = (f->dir == ZD_FW_OUT) ? f->dst_ip : f->src_ip;
        if (ip < r->ip_lo || ip > r->ip_hi) return 0;
    }
    if (!fw_str_app(r->app, f->app_id)) return 0;
    return 1;
}

int net_fw_decide(struct zd_fw *fw, const struct zd_fw_flow *flow) {
    if (!fw || !flow) return -22;
    if (flow->dir != ZD_FW_IN && flow->dir != ZD_FW_OUT) {
        fw->stats.invalid_flows++;
        return -22;
    }
    fw->stats.flows++;
    for (uint32_t i = 0; i < fw->rule_count; ++i) {
        if (fw_match(&fw->rules[i], flow)) {
            if (fw->rules[i].action == ZD_FW_ALLOW) {
                fw->stats.allowed++;
                return ZD_FW_ALLOW;
            }
            fw->stats.denied++;
            return ZD_FW_DENY;
        }
    }
    fw->stats.denied++;
    return ZD_FW_DENY;
}

/* Live-path enforcement wrappers for TCP, ICMP, and IPv6.
 * Each validates the protocol header and consults the firewall;
 * returns ZD_FW_ALLOW/ZD_FW_DENY/-errno. */
int net_fw_check_tcp(const struct zd_fw *fw,
                     const struct net_tcp_conn *c,
                     uint32_t src_ip, uint32_t dst_ip,
                     uint16_t src_port, uint16_t dst_port) {
    if (!fw || !c) return -22;
    struct zd_fw_flow flow = {0};
    flow.dir = ZD_FW_OUT;
    flow.proto = ZD_FW_TCP;
    flow.src_ip = src_ip;
    flow.dst_ip = dst_ip;
    flow.src_port = src_port;
    flow.dst_port = dst_port;
    flow.conn_known = (c->state >= TCP_ESTABLISHED);
    return net_fw_decide((struct zd_fw *)fw, &flow);
}

int net_fw_check_icmp(const struct zd_fw *fw,
                      uint32_t src_ip, uint32_t dst_ip,
                      uint8_t icmp_type, uint8_t icmp_code) {
    if (!fw) return -22;
    struct zd_fw_flow flow = {0};
    flow.dir = ZD_FW_IN;
    flow.proto = ZD_FW_ICMP;
    flow.src_ip = src_ip;
    flow.dst_ip = dst_ip;
    flow.conn_known = 0;
    (void)icmp_type;
    (void)icmp_code;
    return net_fw_decide((struct zd_fw *)fw, &flow);
}

int net_fw_check_ipv6(const struct zd_fw *fw,
                      const uint8_t src[16],
                      const uint8_t dst[16],
                      uint8_t nexthdr, uint16_t src_port,
                      uint16_t dst_port) {
    if (!fw || !src || !dst) return -22;
    struct zd_fw_flow flow = {0};
    flow.dir = ZD_FW_OUT;
    flow.proto = (nexthdr == 6) ? ZD_FW_TCP :
                  (nexthdr == 17) ? ZD_FW_UDP : ZD_FW_ANY;
    /* Convert first 4 bytes of each address to uint32_t for
     * the existing IPv4-oriented firewall matcher. */
    flow.src_ip = ((uint32_t)src[0] << 24) |
                  ((uint32_t)src[1] << 16) |
                  ((uint32_t)src[2] << 8)  | src[3];
    flow.dst_ip = ((uint32_t)dst[0] << 24) |
                  ((uint32_t)dst[1] << 16) |
                  ((uint32_t)dst[2] << 8)  | dst[3];
    flow.src_port = src_port;
    flow.dst_port = dst_port;
    flow.conn_known = 0;
    return net_fw_decide((struct zd_fw *)fw, &flow);
}
