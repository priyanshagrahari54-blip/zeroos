/* Vault core — AEAD wrapping via the certified kernel crypto module.
 * The crypto header explicitly assigns nonce generation and key
 * storage policy to this layer; plaintext is never retained. */
#include <string.h>
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
void zd_vault_init(struct zd_vault *v) {
    if (!v)
        return;
    zeroos_secure_zero(v,sizeof(*v));
}

int zd_vault_set_nonce_source(struct zd_vault *v,
                              zd_vault_nonce_source_fn source, void *ctx) {
    if (!v)
        return v_bad();
    if (v->unlocked)
        return -16;
    v->nonce_source=source;
    v->nonce_source_ctx=ctx;
    return 0;
}

int zd_vault_unlock(struct zd_vault *v, const uint8_t key[ZD_VAULT_KEY_LEN]) {
    if (!v || !key)
        return v_bad();
    if (v->unlocked)
        return -16;
    for (uint32_t i=0;i<ZD_VAULT_KEY_LEN;++i)
        v->key[i]=key[i];
    v->unlocked=1;
    return 0;
}

int zd_vault_lock(struct zd_vault *v) {
    if (!v)
        return v_bad();
    zeroos_secure_zero(v->key,sizeof(v->key));
    v->unlocked=0;
    v->stats.wipes++;
    return 0;
}

int zd_vault_put(struct zd_vault *v, const char *name,
                 const uint8_t *secret, uint32_t secret_len) {
    int idx, i, r;
    uint8_t nonce[V_NONCE];
    uint8_t replacement[ZD_VAULT_CT_MAX];
    uint32_t slen;
    uint32_t replacement_len;

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
    replacement_len=V_NONCE+secret_len+V_TAG;
    if (ZD_VAULT_CT_MAX < replacement_len) {
        v->stats.rejected++;
        return v_bad();
    }
    if (!v->unlocked) {
        v->stats.rejected++;
        return -1;
    }
    if (!v->nonce_source) {
        v->stats.rejected++;
        return -95; /* ENOTSUP: no secure nonce source */
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

    zeroos_secure_zero(replacement,sizeof(replacement));
    if (v->nonce_source(v->nonce_source_ctx,nonce)!=0) {
        zeroos_secure_zero(nonce,sizeof(nonce));
        zeroos_secure_zero(replacement,sizeof(replacement));
        v->stats.rejected++;
        return -5;
    }
    for (i=0;i<ZD_VAULT_MAX;++i) {
        if (v->entries[i].in_use &&
            memcmp(v->entries[i].ct,nonce,V_NONCE)==0) {
            zeroos_secure_zero(nonce,sizeof(nonce));
            zeroos_secure_zero(replacement,sizeof(replacement));
            v->stats.rejected++;
            return -5; /* reject observed nonce reuse under this vault key */
        }
    }
    r = zeroos_aead_encrypt(v->key, nonce, (const uint8_t *)name,
                            slen, secret, secret_len,
                            replacement + V_NONCE,
                            replacement + V_NONCE + secret_len);
    if (r != ZCRYPTO_OK) {
        zeroos_secure_zero(nonce,sizeof(nonce));
        zeroos_secure_zero(replacement,sizeof(replacement));
        v->stats.rejected++;
        return -5;
    }
    memcpy(replacement,nonce,V_NONCE);
    /* Commit only after nonce generation and AEAD succeed; replacing a value
     * cannot leave a truncated/partially encrypted record on failure. */
    zeroos_secure_zero(v->entries[idx].ct,sizeof(v->entries[idx].ct));
    memcpy(v->entries[idx].ct,replacement,replacement_len);
    v->entries[idx].ct_len=replacement_len;
    zeroos_secure_zero(v->entries[idx].name,sizeof(v->entries[idx].name));
    memcpy(v->entries[idx].name,name,slen);
    v->entries[idx].in_use=1;
    v->stats.puts++;
    zeroos_secure_zero(nonce,sizeof(nonce));
    zeroos_secure_zero(replacement,sizeof(replacement));
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
    int idx;
    if (!v || !name)
        return v_bad();
    idx = v_find(v, name);
    if (idx < 0)
        return -2;
    zeroos_secure_zero(v->entries[idx].ct,sizeof(v->entries[idx].ct));
    zeroos_secure_zero(v->entries[idx].name,sizeof(v->entries[idx].name));
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
