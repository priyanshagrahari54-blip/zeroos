/* Firewall policy engine host tests (part B). */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/firewall.h>

static struct zd_fw_rule rule(int dir, int proto, int action,
                              uint16_t lo, uint16_t hi,
                              const char *app) {
    struct zd_fw_rule r;
    memset(&r, 0, sizeof(r));
    r.dir = (uint8_t)dir;
    r.proto = (uint8_t)proto;
    r.action = (uint8_t)action;
    r.port_lo = lo;
    r.port_hi = hi;
    if (app) {
        uint32_t i;
        for (i = 0; app[i] && i < ZD_FW_APP - 1; ++i)
            r.app[i] = app[i];
    }
    return r;
}

void zd_test_firewall_suite(void) {
    struct zd_fw fw;
    struct zd_fw_rule r;
    struct zd_fw_flow f;

    zd_fw_init(&fw);
    ZD_CHECK_EQ(fw.rule_count, 0U);

    /* empty policy = default deny everything */
    memset(&f, 0, sizeof(f));
    f.dir = ZD_FW_OUT;
    f.proto = ZD_FW_TCP;
    f.dst_port = 443;
    f.dst_ip = 0x08080808;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    ZD_CHECK_EQ(fw.stats.denied, 1U);

    /* allow web outbound, everything else still denied */
    r = rule(ZD_FW_OUT, ZD_FW_TCP, ZD_FW_ALLOW, 80, 443, 0);
    ZD_CHECK_OK(zd_fw_add(&fw, &r, 0));
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_ALLOW); /* 443 */
    f.dst_port = 8080;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);  /* out of range */
    f.dst_port = 79;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    ZD_CHECK_EQ(fw.stats.allowed, 1U);
    ZD_CHECK_EQ(fw.stats.denied, 3U);

    /* deny before allow via front insertion (first match wins) */
    r = rule(ZD_FW_OUT, ZD_FW_TCP, ZD_FW_DENY, 443, 443, 0);
    ZD_CHECK_OK(zd_fw_add(&fw, &r, 1)); /* front */
    f.dst_port = 443;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    f.dst_port = 80;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_ALLOW);

    /* direction isolation: inbound not matched by OUT rules */
    f.dir = ZD_FW_IN;
    f.src_port = 443;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    f.dir = ZD_FW_OUT;

    /* app scoping: rule with app only matches that app */
    r = rule(ZD_FW_OUT, ZD_FW_ANY, ZD_FW_ALLOW, 0, 0, "browser");
    ZD_CHECK_OK(zd_fw_add(&fw, &r, 0));
    f.app_id = "browser";
    f.dst_port = 9999;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_ALLOW);
    f.app_id = "term";
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY); /* falls to default */
    f.app_id = 0;

    /* established-only rule: replies allowed, new inbound not */
    zd_fw_init(&fw);
    r = rule(ZD_FW_IN, ZD_FW_TCP, ZD_FW_ALLOW, 1024, 65535, 0);
    r.established_only = 1;
    ZD_CHECK_OK(zd_fw_add(&fw, &r, 0));
    memset(&f, 0, sizeof(f));
    f.dir = ZD_FW_IN;
    f.proto = ZD_FW_TCP;
    f.src_port = 50000;
    f.conn_known = 0;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    f.conn_known = 1;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_ALLOW);

    /* ip range match */
    zd_fw_init(&fw);
    r = rule(ZD_FW_IN, ZD_FW_ANY, ZD_FW_DENY, 0, 0, 0);
    r.ip_lo = 0x0A000000;         /* 10.0.0.0 */
    r.ip_hi = 0x0A0000FF;         /* 10.0.0.255 */
    ZD_CHECK_OK(zd_fw_add(&fw, &r, 0));
    memset(&f, 0, sizeof(f));
    f.dir = ZD_FW_IN;
    f.src_ip = 0x0A000042;
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY);
    f.src_ip = 0xC0A80001; /* 192.168.0.1 */
    ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_DENY); /* default deny too */
    ZD_CHECK_EQ(fw.stats.denied, 2U);

    /* template validation */
    ZD_CHECK(zd_fw_add(&fw, 0, 0) == -22);
    r = rule(9, ZD_FW_TCP, ZD_FW_ALLOW, 0, 0, 0); /* bad dir */
    ZD_CHECK(zd_fw_add(&fw, &r, 0) == -22);
    r = rule(ZD_FW_OUT, ZD_FW_TCP, ZD_FW_ALLOW, 500, 100, 0); /* lo>hi */
    ZD_CHECK(zd_fw_add(&fw, &r, 0) == -22);
    ZD_CHECK(fw.stats.rules_rejected >= 3U);

    /* invalid flows */
    ZD_CHECK(zd_fw_decide(&fw, 0) == -22);
    memset(&f, 0, sizeof(f));
    f.dir = 7;
    ZD_CHECK(zd_fw_decide(&fw, &f) == -22);
    ZD_CHECK(fw.stats.invalid_flows >= 1U);

    /* remove: unknown id, then real */
    ZD_CHECK(zd_fw_remove(&fw, 9999) == -2);
    {
        uint32_t first = fw.rules[0].id;
        ZD_CHECK_OK(zd_fw_remove(&fw, first));
        ZD_CHECK(zd_fw_remove(&fw, first) == -2);
    }

    /* capacity: fill to 32, next rejected */
    zd_fw_init(&fw);
    {
        int i;
        for (i = 0; i < ZD_FW_MAX_RULES; ++i) {
            r = rule(ZD_FW_OUT, ZD_FW_UDP, ZD_FW_ALLOW,
                     (uint16_t)(1000 + i), (uint16_t)(1000 + i), 0);
            ZD_CHECK_OK(zd_fw_add(&fw, &r, 0));
        }
        ZD_CHECK_EQ(fw.rule_count, (uint32_t)ZD_FW_MAX_RULES);
        r = rule(ZD_FW_OUT, ZD_FW_UDP, ZD_FW_ALLOW, 1, 2, 0);
        ZD_CHECK(zd_fw_add(&fw, &r, 0) == -28);
        ZD_CHECK_EQ(fw.stats.rules_rejected, 1U);
        /* still decides normally at capacity */
        memset(&f, 0, sizeof(f));
        f.dir = ZD_FW_OUT;
        f.proto = ZD_FW_UDP;
        f.dst_port = 1005;
        ZD_CHECK_EQ(zd_fw_decide(&fw, &f), ZD_FW_ALLOW);
    }

    /* matching helper counts without deciding */
    ZD_CHECK(zd_fw_matching(&fw, &f) == 1U);
    ZD_CHECK(zd_fw_matching(&fw, 0) == 0U);
}
