/* Encrypted secret vault (Stage 5 part B).
 * Secrets are stored ONLY as AEAD (ChaCha20-Poly1305) ciphertext under
 * an OS-injected 32-byte master key — the vault never persists
 * plaintext.  KDF/key service stays outside this contract: the caller
 * supplies the wrapped master key.  lock() wipes key + entries. */
#ifndef ZEROOS_DESKTOP_VAULT_H
#define ZEROOS_DESKTOP_VAULT_H

#include <stdint.h>

#define ZD_VAULT_MAX 16
#define ZD_VAULT_NAME 32
#define ZD_VAULT_SECRET_MAX 128
#define ZD_VAULT_CT_MAX 160    /* secret + 16-byte tag */
#define ZD_VAULT_KEY_LEN 32

struct zd_vault_entry {
    char name[ZD_VAULT_NAME];
    uint8_t ct[ZD_VAULT_CT_MAX]; /* nonce || ciphertext||tag */
    uint32_t ct_len;
    uint8_t in_use;
};

struct zd_vault {
    uint8_t key[ZD_VAULT_KEY_LEN];
    uint8_t unlocked;             /* 1 while the key is resident */
    struct zd_vault_entry entries[ZD_VAULT_MAX];
    struct {
        uint32_t puts, gets, get_denied, auth_failures, wipes,
                 rejected;
    } stats;
};

/* Lock state; key supplied later via unlock(). */
void zd_vault_init(struct zd_vault *v);
int zd_vault_unlock(struct zd_vault *v, const uint8_t key[ZD_VAULT_KEY_LEN]);
/* Wipe key (entries stay as ciphertext; stats.wipes++). */
int zd_vault_lock(struct zd_vault *v);
/* Store/replace a secret.  Locked -> -1.  Bounds -> -22.  Full -> -28.
 * Returns 0; plaintext is never retained. */
int zd_vault_put(struct zd_vault *v, const char *name,
                 const uint8_t *secret, uint32_t secret_len);
/* Decrypt into out (cap >= secret length).  Unknown name -> -2;
 * tamper/wrong key -> -3 (auth failure counted); locked -> -1. */
int zd_vault_get(struct zd_vault *v, const char *name,
                 uint8_t *out, uint32_t out_cap, uint32_t *out_len);
int zd_vault_forget(struct zd_vault *v, const char *name);
int zd_vault_count(const struct zd_vault *v);

#endif /* ZEROOS_DESKTOP_VAULT_H */
