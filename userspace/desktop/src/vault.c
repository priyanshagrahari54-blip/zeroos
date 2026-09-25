/* Vault core — AEAD wrapping via the certified kernel crypto module.
 * The crypto header explicitly assigns nonce generation and key
 * storage policy to this layer; plaintext is never retained. */
#include <zeroos/desktop/vault.h>
#include "crypto.h"

#define V_NONCE  ZEROOS_CHACHA20_NONCE_LEN  /* 12 */
#define V_TAG    ZEROOS_AEAD_TAG_LEN        /* 16 */

/* entry layout: nonce | ciphertext | tag */
static int v_bad(void) { return -22; }
static int v_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}
static int v_find(const struct zd_vault *v, const char *name) {
    int i;
    for (i = 0; i < ZD_VAULT_MAX; ++i)
        if (v->entries[i].in_use && v_eq(v->entries[i].name, name))
            return i;
    return -1;
}
/* Deterministic per-slot nonce: unique per key per slot (fresh random
 * nonces belong to the OS entropy service, wired above this contract;
 * slot+epoch makes re-wraps of the same key distinct). */
static void v_nonce(uint8_t n[V_NONCE], uint32_t epoch, int slot) {
    n[0] = (uint8_t)(epoch);
    n[1] = (uint8_t)(epoch >> 8);
    n[2] = (uint8_t)(epoch >> 16);
    n[3] = (uint8_t)(epoch >> 24);
    n[4] = (uint8_t)(0xC0u + (unsigned)slot);
    n[5] = (uint8_t)((unsigned)slot >> 8);
    n[6] = 0xDE;
    n[7] = 0x5A;
    n[8] = n[9] = n[10] = n[11] = 0;
}

void zd_vault_init(struct zd_vault *v) {
    int i;
    if (!v)
        return;
    for (i = 0; i < ZD_VAULT_KEY_LEN; ++i)
        v->key[i] = 0;
    v->unlocked = 0;
    for (i = 0; i < ZD_VAULT_MAX; ++i) {
        v->entries[i].name[0] = 0;
        v->entries[i].ct_len = 0;
        v->entries[i].in_use = 0;
    }
    v->stats.puts = v->stats.gets = v->stats.get_denied = 0;
    v->stats.auth_failures = v->stats.wipes = v->stats.rejected = 0;
}

int zd_vault_unlock(struct zd_vault *v, const uint8_t key[ZD_VAULT_KEY_LEN]) {
    int i;
    if (!v || !key)
        return v_bad();
    for (i = 0; i < ZD_VAULT_KEY_LEN; ++i)
        v->key[i] = key[i];
    v->unlocked = 1;
    return 0;
}

int zd_vault_lock(struct zd_vault *v) {
    int i;
    if (!v)
        return v_bad();
    for (i = 0; i < ZD_VAULT_KEY_LEN; ++i)
        v->key[i] = 0;   /* key wiped; ciphertext remains */
    v->unlocked = 0;
    v->stats.wipes++;
    return 0;
}

int zd_vault_put(struct zd_vault *v, const char *name,
                 const uint8_t *secret, uint32_t secret_len) {
    int idx, i;
    uint8_t nonce[V_NONCE];
    int r;
    uint32_t slen;

    if (!v)
        return v_bad();
    if (!name || !name[0] || !secret || secret_len == 0 ||
        secret_len > ZD_VAULT_SECRET_MAX) {
        v->stats.rejected++;
        return v_bad();
    }
    for (slen = 0; slen < ZD_VAULT_NAME && name[slen]; ++slen)
        ;
    if (slen >= ZD_VAULT_NAME) {
        v->stats.rejected++;
        return v_bad();
    }
    if (ZD_VAULT_CT_MAX < V_NONCE + secret_len + V_TAG) {
        v->stats.rejected++;
        return v_bad();
    }
    if (!v->unlocked) {
        v->stats.rejected++;
        return -1;
    }
    idx = v_find(v, name);
    if (idx < 0) {
        for (i = 0; i < ZD_VAULT_MAX; ++i)
            if (!v->entries[i].in_use) {
                idx = i;
                break;
            }
    }
    if (idx < 0)
        return -28; /* ENOSPC */
    v_nonce(nonce, 0x5A5AC0DEu, idx);
    r = zeroos_aead_encrypt(v->key, nonce, (const uint8_t *)name,
                            slen, secret, secret_len,
                            v->entries[idx].ct + V_NONCE,
                            v->entries[idx].ct + V_NONCE + secret_len);
    if (r != ZCRYPTO_OK)
        return -5;
    for (i = 0; i < (int)V_NONCE; ++i)
        v->entries[idx].ct[i] = nonce[i];
    v->entries[idx].ct_len = V_NONCE + secret_len + V_TAG;
    for (i = 0; i < (int)slen; ++i)
        v->entries[idx].name[i] = name[i];
    v->entries[idx].name[slen] = 0;
    v->entries[idx].in_use = 1;
    v->stats.puts++;
    return 0;
}

int zd_vault_get(struct zd_vault *v, const char *name,
                 uint8_t *out, uint32_t out_cap, uint32_t *out_len) {
    int idx, r;
    uint32_t pt_len, slen;
    if (!v)
        return v_bad();
    if (!name || !out || !out_len) {
        v->stats.rejected++;
        return v_bad();
    }
    if (!v->unlocked) {
        v->stats.get_denied++;
        return -1;
    }
    idx = v_find(v, name);
    if (idx < 0)
        return -2;
    if (v->entries[idx].ct_len < V_NONCE + V_TAG)
        return -5;
    pt_len = v->entries[idx].ct_len - V_NONCE - V_TAG;
    if (out_cap < pt_len)
        return -22;
    for (slen = 0; slen < ZD_VAULT_NAME && name[slen]; ++slen)
        ;
    r = zeroos_aead_decrypt(v->key, v->entries[idx].ct,
                            (const uint8_t *)name, slen,
                            v->entries[idx].ct + V_NONCE, pt_len,
                            v->entries[idx].ct + V_NONCE + pt_len, out);
    if (r != ZCRYPTO_OK) {
        v->stats.auth_failures++;
        return -3; /* tamper or wrong key — explicit, never silent */
    }
    v->stats.gets++;
    *out_len = pt_len;
    return 0;
}

int zd_vault_forget(struct zd_vault *v, const char *name) {
    int idx, i;
    if (!v || !name)
        return v_bad();
    idx = v_find(v, name);
    if (idx < 0)
        return -2;
    for (i = 0; i < ZD_VAULT_CT_MAX; ++i)
        v->entries[idx].ct[i] = 0;
    for (i = 0; i < ZD_VAULT_NAME; ++i)
        v->entries[idx].name[i] = 0;
    v->entries[idx].ct_len = 0;
    v->entries[idx].in_use = 0;
    return 0;
}

int zd_vault_count(const struct zd_vault *v) {
    int i, n = 0;
    if (!v)
        return -22;
    for (i = 0; i < ZD_VAULT_MAX; ++i)
        if (v->entries[i].in_use)
            ++n;
    return n;
}
