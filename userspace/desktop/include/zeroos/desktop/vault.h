/* Encrypted secret vault (Stage 5 part B / Stage 6 key-lifecycle work).
 * Secrets are stored only as AEAD (ChaCha20-Poly1305) ciphertext under an
 * injected master key. Writes require an OS CSPRNG nonce source; there is no
 * deterministic or insecure fallback. This core has no KDF/key store, and
 * host-test nonce sources are not production entropy providers. */
#ifndef ZEROOS_DESKTOP_VAULT_H
#define ZEROOS_DESKTOP_VAULT_H

#include <stdint.h>

#define ZD_VAULT_MAX 16
#define ZD_VAULT_NAME 32
#define ZD_VAULT_SECRET_MAX 128
#define ZD_VAULT_CT_MAX 160    /* 12-byte nonce + secret + 16-byte tag */
#define ZD_VAULT_KEY_LEN 32

struct zd_vault_entry {
    char name[ZD_VAULT_NAME];
    uint8_t ct[ZD_VAULT_CT_MAX]; /* nonce || ciphertext||tag */
    uint32_t ct_len;
    uint8_t in_use;
};

typedef int (*zd_vault_nonce_source_fn)(void *ctx,
                                        uint8_t nonce[12]);

struct zd_vault {
    uint8_t key[ZD_VAULT_KEY_LEN];
    uint8_t unlocked;             /* 1 while the key is resident */
    zd_vault_nonce_source_fn nonce_source;
    void *nonce_source_ctx;
    struct zd_vault_entry entries[ZD_VAULT_MAX];
    struct {
        uint32_t puts, gets, get_denied, auth_failures, wipes,
                 rejected;
    } stats;
};

/* Lock state; key and nonce source are supplied before writing. */
void zd_vault_init(struct zd_vault *v);
/* Must be configured while locked. Provider must be a cryptographically
 * secure OS random source; a missing or failing provider makes put fail. */
int zd_vault_set_nonce_source(struct zd_vault *v,
                              zd_vault_nonce_source_fn source, void *ctx);
/* Locked-only key load; already-unlocked -> -16. */
int zd_vault_unlock(struct zd_vault *v, const uint8_t key[ZD_VAULT_KEY_LEN]);
/* Wipe resident key (entries stay as ciphertext; stats.wipes++). */
int zd_vault_lock(struct zd_vault *v);
/* Store/replace a secret. Locked -> -1; missing entropy -> -95; bounds ->
 * -22; full -> -28; nonce/encryption failure or observed nonce reuse -> -5.
 * Existing ciphertext is unchanged on failure; plaintext is never retained. */
int zd_vault_put(struct zd_vault *v, const char *name,
                 const uint8_t *secret, uint32_t secret_len);
/* Decrypt into out (cap >= secret length). Names must be NUL-terminated
 * within ZD_VAULT_NAME and nonempty; invalid args/name -> -22. Unknown name
 * -> -2; malformed ciphertext metadata -> -5; tamper/wrong key -> -3
 * (auth failure counted); locked -> -1. */
int zd_vault_get(struct zd_vault *v, const char *name,
                 uint8_t *out, uint32_t out_cap, uint32_t *out_len);
int zd_vault_forget(struct zd_vault *v, const char *name);
int zd_vault_count(const struct zd_vault *v);

#endif /* ZEROOS_DESKTOP_VAULT_H */
