/* Cloud/device ecosystem core (Stage 5 part J): offline-first
 * behaviour with explicit per-capability permissions.
 *
 * Every sync action is gated on (a) device pairing, (b) an explicit
 * granted permission, and (c) connectivity state — offline queues
 * are first-class: nothing is lost, nothing is sent early, and a
 * denied permission never falls back to "try anyway".
 */
#ifndef ZEROOS_DESKTOP_ECO_H
#define ZEROOS_DESKTOP_ECO_H

#include <stdint.h>

#define ZD_ECO_DEVICES 4
#define ZD_ECO_QUEUE 16
#define ZD_ECO_NAME 24
#define ZD_ECO_PAYLOAD 64

/* permission bits */
#define ZD_ECO_PERM_SYNC_FILES  (1u << 0)
#define ZD_ECO_PERM_SYNC_SETTINGS (1u << 1)
#define ZD_ECO_PERM_SYNC_CLIPBOARD (1u << 2)
#define ZD_ECO_PERM_DISCOVER    (1u << 3)
#define ZD_ECO_PERM_ALL 0xFu

enum zd_eco_conn {
    ZD_ECO_OFFLINE = 0,
    ZD_ECO_ONLINE = 1
};

struct zd_eco_device {
    char name[ZD_ECO_NAME];
    uint32_t permissions;          /* granted mask (pairing grants 0) */
    uint8_t paired;
    uint8_t in_use;
    uint32_t sent;
    uint32_t refused;
};

struct zd_eco_entry {
    uint32_t device_idx;
    uint32_t permission;           /* required permission bit */
    char payload[ZD_ECO_PAYLOAD];
    uint8_t in_use;
    uint8_t flushed;               /* 1 once transmitted */
};

struct zd_eco {
    struct zd_eco_device devices[ZD_ECO_DEVICES];
    struct zd_eco_entry queue[ZD_ECO_QUEUE];
    uint32_t device_count;
    uint32_t queued;
    uint32_t queued_ever;
    uint32_t conn;                 /* enum zd_eco_conn */
    struct {
        uint32_t paired, unpairs, granted, revoked, queued,
                 flushed, refused_perm, refused_offline,
                 refused_pairing, rejected;
    } stats;
};

void zd_eco_init(struct zd_eco *e);
void zd_eco_set_conn(struct zd_eco *e, uint32_t conn);
/* Pair with zero permissions (explicit grant required afterwards).
 * empty/overlong name -> -22; full -> -28. */
int zd_eco_pair(struct zd_eco *e, const char *name, uint32_t *out_idx);
int zd_eco_unpair(struct zd_eco *e, uint32_t idx);
/* Grant/revoke exactly one permission bit on a paired device. */
int zd_eco_grant(struct zd_eco *e, uint32_t idx, uint32_t perm);
int zd_eco_revoke(struct zd_eco *e, uint32_t idx, uint32_t perm);
/* Queue work for a device: requires paired + that permission
 * (else -1 with the matching refusal counter).  Full -> -28. */
int zd_eco_enqueue(struct zd_eco *e, uint32_t idx, uint32_t perm,
                   const char *payload);
/* Flush while ONLINE: each entry still checks pairing+perm (a
 * revoked permission mid-queue refuses that entry, counted, keeps
 * the rest).  Offline -> 0 flushed, entries stay queued. */
uint32_t zd_eco_flush(struct zd_eco *e);

#endif /* ZEROOS_DESKTOP_ECO_H */
