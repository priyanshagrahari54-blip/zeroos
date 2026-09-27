/* Firewall decision engine.  See firewall.h. */
#include <zeroos/desktop/firewall.h>

static int fw_bad(void) { return -22; }

static int fw_str_app(const char *rule_app, const char *flow_app) {
    /* "" rule matches any; otherwise exact match required */
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

void zd_fw_init(struct zd_fw *fw) {
    uint32_t i;
    if (!fw)
        return;
    for (i = 0; i < ZD_FW_MAX_RULES; ++i)
        fw->rules[i].enabled = 0;
    fw->rule_count = 0;
    fw->next_id = 1;
    fw->stats.flows = fw->stats.allowed = fw->stats.denied = 0;
    fw->stats.invalid_flows = fw->stats.rules_added = 0;
    fw->stats.rules_removed = fw->stats.rules_rejected = 0;
}

static int fw_valid_tmpl(const struct zd_fw_rule *t) {
    uint32_t i;
    if (!t)
        return 0;
    if (t->dir != ZD_FW_IN && t->dir != ZD_FW_OUT)
        return 0;
    if (t->proto != ZD_FW_ANY && t->proto != ZD_FW_TCP &&
        t->proto != ZD_FW_UDP && t->proto != ZD_FW_ICMP)
        return 0;
    if (t->action != ZD_FW_DENY && t->action != ZD_FW_ALLOW)
        return 0;
    if (t->port_lo > t->port_hi)
        return 0;
    if (t->ip_lo > t->ip_hi)
        return 0;
    for (i = 0; i < ZD_FW_APP && t->app[i]; ++i) {
        if ((unsigned char)t->app[i] < 0x21)
            return 0; /* no control/space in app ids */
    }
    return 1;
}

int zd_fw_add(struct zd_fw *fw, const struct zd_fw_rule *tmpl, int front) {
    struct zd_fw_rule r;
    uint32_t i;
    if (!fw || !fw_valid_tmpl(tmpl)) {
        if (fw)
            fw->stats.rules_rejected++;
        return fw_bad();
    }
    if (fw->rule_count >= ZD_FW_MAX_RULES) {
        fw->stats.rules_rejected++;
        return -28;
    }
    r = *tmpl;
    r.id = fw->next_id++;
    r.enabled = 1;
    if (front) {
        for (i = fw->rule_count; i > 0; --i)
            fw->rules[i] = fw->rules[i - 1];
        fw->rules[0] = r;
    } else {
        fw->rules[fw->rule_count] = r;
    }
    fw->rule_count++;
    fw->stats.rules_added++;
    return 0;
}

int zd_fw_remove(struct zd_fw *fw, uint32_t id) {
    uint32_t i;
    if (!fw)
        return fw_bad();
    for (i = 0; i < fw->rule_count; ++i) {
        if (fw->rules[i].id == id) {
            uint32_t j;
            for (j = i + 1; j < fw->rule_count; ++j)
                fw->rules[j - 1] = fw->rules[j];
            fw->rule_count--;
            fw->stats.rules_removed++;
            return 0;
        }
    }
    return -2;
}

static int fw_match(const struct zd_fw_rule *r,
                    const struct zd_fw_flow *f) {
    uint16_t port;
    if (!r->enabled)
        return 0;
    if ((int)r->dir != f->dir)
        return 0;
    if (r->proto != ZD_FW_ANY && (int)r->proto != f->proto)
        return 0;
    if (r->established_only && !f->conn_known)
        return 0;
    if (r->port_lo || r->port_hi) {
        port = (f->dir == ZD_FW_OUT) ? f->dst_port : f->src_port;
        if (port < r->port_lo || port > r->port_hi)
            return 0;
    }
    if (r->ip_lo || r->ip_hi) {
        uint32_t ip = (f->dir == ZD_FW_OUT) ? f->dst_ip : f->src_ip;
        if (ip < r->ip_lo || ip > r->ip_hi)
            return 0;
    }
    if (!fw_str_app(r->app, f->app_id))
        return 0;
    return 1;
}

uint32_t zd_fw_matching(const struct zd_fw *fw,
                        const struct zd_fw_flow *flow) {
    uint32_t i, n = 0;
    if (!fw || !flow)
        return 0;
    for (i = 0; i < fw->rule_count; ++i)
        if (fw_match(&fw->rules[i], flow))
            ++n;
    return n;
}

int zd_fw_decide(struct zd_fw *fw, const struct zd_fw_flow *flow) {
    uint32_t i;
    if (!fw || !flow)
        return fw_bad();
    if (flow->dir != ZD_FW_IN && flow->dir != ZD_FW_OUT) {
        fw->stats.invalid_flows++;
        return fw_bad();
    }
    if (flow->src_port == 0 && flow->proto != ZD_FW_ICMP &&
        flow->dst_port == 0 && flow->dir == ZD_FW_OUT &&
        flow->proto != ZD_FW_ANY) {
        fw->stats.invalid_flows++;
        return fw_bad(); /* outbound L4 flow without a port */
    }
    fw->stats.flows++;
    for (i = 0; i < fw->rule_count; ++i) {
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
    return ZD_FW_DENY; /* default deny */
}
