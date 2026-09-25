#ifndef ZEROOS_CRYPTO_H
#define ZEROOS_CRYPTO_H
/* Kernel cryptographic primitives: ChaCha20, Poly1305, AEAD_CHACHA20_POLY1305.
 *
 * Vectors and construction follow RFC 8439 (ChaCha20 and Poly1305 for
 * IETF Protocols).  These are primitives only: policy (who may encrypt
 * what, key storage, nonces generation) belongs to callers (vault,
 * updates, privacy centre) and is deliberately NOT decided here.
 *
 * Bounds: lengths are uint32_t and capped at ZEROOS_CRYPTO_MAX_LEN
 * (64 MiB) per operation; callers with larger data stream through
 * chunked APIs at a higher layer.  Nonces MUST be unique per key per
 * invocation — generation is the caller's contract.
 *
 * Return codes: ZCRYPTO_OK (0), ZCRYPTO_ERR (-1, invalid arguments or
 * bounds), ZCRYPTO_AUTHFAIL (-2, tag mismatch; plaintext output of
 * decrypt is zeroed before returning). */
#include "types.h"

#define ZEROOS_CHACHA20_KEY_LEN 32u
#define ZEROOS_CHACHA20_NONCE_LEN 12u
#define ZEROOS_POLY1305_KEY_LEN 32u
#define ZEROOS_POLY1305_TAG_LEN 16u
#define ZEROOS_AEAD_TAG_LEN 16u
#define ZEROOS_CRYPTO_MAX_LEN (64u * 1024u * 1024u)

#define ZCRYPTO_OK 0
#define ZCRYPTO_ERR (-1)
#define ZCRYPTO_AUTHFAIL (-2)

/* Streaming Poly1305 (init/update/final) for callers that build MAC data
 * from several buffers (AEAD, audit chaining). */
struct zeroos_poly1305_ctx {
    uint32_t h[5];
    uint32_t r[5];
    uint32_t s[5];
    uint8_t buffer[16];
    uint32_t buffered;
};

void zeroos_poly1305_init(struct zeroos_poly1305_ctx *ctx,
                          const uint8_t key[ZEROOS_POLY1305_KEY_LEN]);
void zeroos_poly1305_update(struct zeroos_poly1305_ctx *ctx,
                            const uint8_t *message, uint32_t length);
void zeroos_poly1305_final(struct zeroos_poly1305_ctx *ctx,
                           uint8_t tag[ZEROOS_POLY1305_TAG_LEN]);

/* One-shot Poly1305 (RFC 8439 section 2.5). */
void zeroos_poly1305(const uint8_t key[ZEROOS_POLY1305_KEY_LEN],
                     const uint8_t *message, uint32_t length,
                     uint8_t tag[ZEROOS_POLY1305_TAG_LEN]);

/* ChaCha20 block function (RFC 8439 section 2.3). */
void zeroos_chacha20_block(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                           uint32_t counter,
                           const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                           uint8_t out[64]);

/* ChaCha20 keystream XOR: encrypt == decrypt (RFC 8439 section 2.4).
 * output may alias input. */
void zeroos_chacha20_xor(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                         uint32_t counter,
                         const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                         const uint8_t *input, uint8_t *output,
                         uint32_t length);

/* Poly1305 one-time key from ChaCha20 block 0 (RFC 8439 section 2.6). */
void zeroos_poly1305_key_gen(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                             const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                             uint8_t out[ZEROOS_POLY1305_KEY_LEN]);

/* AEAD_CHACHA20_POLY1305 (RFC 8439 section 2.8): nonce is the full
 * 96-bit value (constant | IV).  ciphertext and plaintext buffers are
 * exactly pt_len bytes; tag is 16 bytes. */
int zeroos_aead_encrypt(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                        const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                        const uint8_t *aad, uint32_t aad_len,
                        const uint8_t *plaintext, uint32_t pt_len,
                        uint8_t *ciphertext,
                        uint8_t tag[ZEROOS_AEAD_TAG_LEN]);

int zeroos_aead_decrypt(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                        const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                        const uint8_t *aad, uint32_t aad_len,
                        const uint8_t *ciphertext, uint32_t ct_len,
                        const uint8_t tag[ZEROOS_AEAD_TAG_LEN],
                        uint8_t *plaintext);
#endif
