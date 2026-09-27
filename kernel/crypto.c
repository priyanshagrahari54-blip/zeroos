/* ChaCha20 / Poly1305 / AEAD_CHACHA20_POLY1305 — RFC 8439.
 *
 * Portable, freestanding-safe (no libc calls, no division by runtime
 * 64-bit values).  Poly1305 uses five 26-bit limbs; carries are folded
 * modulo (2^130 - 5) after every block.  All arithmetic in the hot path
 * is bounded: limb products fit uint64_t (max 5 * 2^55-ish) and the
 * fold coefficient 5*c fits uint32_t because the final limb's carry
 * only ever spans pure-r products (max 5 * 2^52).
 *
 * Verified against RFC 8439 test vectors in tests/crypto_test.c
 * (sections 2.3.2, 2.4.2, 2.5.2, 2.6.2, 2.8.2). */
#include "crypto.h"

#define P26 0x3ffffffu

static uint32_t load32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

/* ------------------------------------------------------------------ */
/* ChaCha20                                                           */
/* ------------------------------------------------------------------ */

#define ZCHACHA_QR(a, b, c, d)                       \
    do {                                             \
        (a) += (b); (d) ^= (a); (d) = rotl32((d), 16); \
        (c) += (d); (b) ^= (c); (b) = rotl32((b), 12); \
        (a) += (b); (d) ^= (a); (d) = rotl32((d), 8);  \
        (c) += (d); (b) ^= (c); (b) = rotl32((b), 7);  \
    } while (0)

void zeroos_chacha20_block(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                           uint32_t counter,
                           const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                           uint8_t out[64]) {
    uint32_t s[16], x[16];
    unsigned i;

    s[0] = 0x61707865u; /* "expa" */
    s[1] = 0x3320646eu; /* "nd 3" */
    s[2] = 0x79622d32u; /* "2-by" */
    s[3] = 0x6b206574u; /* "te k" */
    for (i = 0; i < 8; i++)
        s[4 + i] = load32_le(key + 4 * i);
    s[12] = counter;
    s[13] = load32_le(nonce);
    s[14] = load32_le(nonce + 4);
    s[15] = load32_le(nonce + 8);

    for (i = 0; i < 16; i++)
        x[i] = s[i];
    for (i = 0; i < 10; i++) {
        ZCHACHA_QR(x[0], x[4], x[8], x[12]);
        ZCHACHA_QR(x[1], x[5], x[9], x[13]);
        ZCHACHA_QR(x[2], x[6], x[10], x[14]);
        ZCHACHA_QR(x[3], x[7], x[11], x[15]);
        ZCHACHA_QR(x[0], x[5], x[10], x[15]);
        ZCHACHA_QR(x[1], x[6], x[11], x[12]);
        ZCHACHA_QR(x[2], x[7], x[8], x[13]);
        ZCHACHA_QR(x[3], x[4], x[9], x[14]);
    }
    for (i = 0; i < 16; i++)
        store32_le(out + 4 * i, x[i] + s[i]);
}

void zeroos_chacha20_xor(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                         uint32_t counter,
                         const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                         const uint8_t *input, uint8_t *output,
                         uint32_t length) {
    uint8_t stream[64];
    uint32_t offset = 0, i, block_index = 0;

    while (offset < length) {
        zeroos_chacha20_block(key, counter + block_index, nonce, stream);
        uint32_t n = length - offset;
        if (n > 64)
            n = 64;
        for (i = 0; i < n; i++)
            output[offset + i] = input ? (uint8_t)(input[offset + i] ^ stream[i])
                                       : stream[i];
        offset += n;
        block_index++;
    }
    for (i = 0; i < 64; i++)
        stream[i] = 0; /* wipe keystream copy */
}

void zeroos_poly1305_key_gen(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                             const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                             uint8_t out[ZEROOS_POLY1305_KEY_LEN]) {
    uint8_t block[64];
    unsigned i;

    zeroos_chacha20_block(key, 0u, nonce, block);
    for (i = 0; i < 32; i++)
        out[i] = block[i];
    for (i = 0; i < 64; i++)
        block[i] = 0; /* wipe: contains the one-time key */
}

/* ------------------------------------------------------------------ */
/* Poly1305 — five 26-bit limbs, RFC 8439 section 2.5                 */
/* ------------------------------------------------------------------ */

/* Ripple limbs to 26 bits; fold limb-5 overflow via 2^130 == 5 (mod p).
 * Terminates: each fold adds at most 5 * small-carry, which shrinks. */
static void poly1305_reduce(uint32_t h[5]) {
    unsigned guard;

    for (guard = 0; guard < 8; guard++) {
        uint32_t c;
        c = h[0] >> 26; h[0] &= P26; h[1] += c;
        c = h[1] >> 26; h[1] &= P26; h[2] += c;
        c = h[2] >> 26; h[2] &= P26; h[3] += c;
        c = h[3] >> 26; h[3] &= P26; h[4] += c;
        c = h[4] >> 26; h[4] &= P26;
        if (!c)
            return;
        h[0] += c * 5u;
    }
}

/* h = (h * r) mod p, r clamped, s_j = 5*r_j fold for cross terms. */
static void poly1305_mul_r(struct zeroos_poly1305_ctx *ctx) {
    const uint32_t *h = ctx->h;
    const uint32_t *r = ctx->r;
    uint32_t s1 = r[1] * 5u, s2 = r[2] * 5u, s3 = r[3] * 5u, s4 = r[4] * 5u;
    uint64_t d0, d1, d2, d3, d4, c;

    d0 = (uint64_t)h[0] * r[0] + (uint64_t)h[1] * s4 +
         (uint64_t)h[2] * s3 + (uint64_t)h[3] * s2 + (uint64_t)h[4] * s1;
    d1 = (uint64_t)h[0] * r[1] + (uint64_t)h[1] * r[0] +
         (uint64_t)h[2] * s4 + (uint64_t)h[3] * s3 + (uint64_t)h[4] * s2;
    d2 = (uint64_t)h[0] * r[2] + (uint64_t)h[1] * r[1] +
         (uint64_t)h[2] * r[0] + (uint64_t)h[3] * s4 + (uint64_t)h[4] * s3;
    d3 = (uint64_t)h[0] * r[3] + (uint64_t)h[1] * r[2] +
         (uint64_t)h[2] * r[1] + (uint64_t)h[3] * r[0] + (uint64_t)h[4] * s4;
    d4 = (uint64_t)h[0] * r[4] + (uint64_t)h[1] * r[3] +
         (uint64_t)h[2] * r[2] + (uint64_t)h[3] * r[1] + (uint64_t)h[4] * r[0];

    c = d0 >> 26; d1 += c;
    c = d1 >> 26; d2 += c;
    c = d2 >> 26; d3 += c;
    c = d3 >> 26; d4 += c;
    c = d4 >> 26;
    ctx->h[0] = (uint32_t)d0 & P26;
    ctx->h[1] = (uint32_t)d1 & P26;
    ctx->h[2] = (uint32_t)d2 & P26;
    ctx->h[3] = (uint32_t)d3 & P26;
    ctx->h[4] = (uint32_t)d4 & P26;
    ctx->h[0] += (uint32_t)c * 5u;
    poly1305_reduce(ctx->h);
}

/* Load k bytes (1..16) as little-endian integer + hibit 2^(8*k),
 * accumulate into h, reduce mod (2^130 - 5). */
static void poly1305_block_add(struct zeroos_poly1305_ctx *ctx,
                               const uint8_t *data, uint32_t k) {
    uint32_t n[5] = {0, 0, 0, 0, 0};
    uint32_t b, bit, limb, sh;

    for (b = 0; b < k; b++) {
        uint32_t v = data[b];
        bit = 8u * b;
        limb = bit / 26u;
        sh = bit % 26u;
        n[limb] |= (v << sh) & P26;
        if (sh > 18u)
            n[limb + 1] |= v >> (26u - sh);
    }
    bit = 8u * k;
    limb = bit / 26u;
    sh = bit % 26u;
    n[limb] |= 1u << sh;

    for (b = 0; b < 5; b++)
        ctx->h[b] += n[b];
    poly1305_reduce(ctx->h);
    poly1305_mul_r(ctx);
}

void zeroos_poly1305_init(struct zeroos_poly1305_ctx *ctx,
                          const uint8_t key[ZEROOS_POLY1305_KEY_LEN]) {
    uint32_t w0 = load32_le(key) & 0x0fffffffu;
    uint32_t w1 = load32_le(key + 4) & 0x0ffffffcu;
    uint32_t w2 = load32_le(key + 8) & 0x0ffffffcu;
    uint32_t w3 = load32_le(key + 12) & 0x0ffffffcu;
    uint32_t x0 = load32_le(key + 16);
    uint32_t x1 = load32_le(key + 20);
    uint32_t x2 = load32_le(key + 24);
    uint32_t x3 = load32_le(key + 28);
    unsigned i;

    ctx->r[0] = w0 & P26;
    ctx->r[1] = ((w0 >> 26) | (w1 << 6)) & P26;
    ctx->r[2] = ((w1 >> 20) | (w2 << 12)) & P26;
    ctx->r[3] = ((w2 >> 14) | (w3 << 18)) & P26;
    ctx->r[4] = w3 >> 8;

    ctx->s[0] = x0 & P26;
    ctx->s[1] = ((x0 >> 26) | (x1 << 6)) & P26;
    ctx->s[2] = ((x1 >> 20) | (x2 << 12)) & P26;
    ctx->s[3] = ((x2 >> 14) | (x3 << 18)) & P26;
    ctx->s[4] = x3 >> 8;

    for (i = 0; i < 5; i++)
        ctx->h[i] = 0;
    ctx->buffered = 0;
}

void zeroos_poly1305_update(struct zeroos_poly1305_ctx *ctx,
                            const uint8_t *message, uint32_t length) {
    uint32_t i = 0;

    if (!ctx || !message)
        return;
    if (ctx->buffered) {
        uint32_t need = 16u - ctx->buffered;
        if (need > length)
            need = length;
        for (i = 0; i < need; i++)
            ctx->buffer[ctx->buffered + i] = message[i];
        ctx->buffered += need;
        i = need;
        if (ctx->buffered < 16)
            return;
        poly1305_block_add(ctx, ctx->buffer, 16);
        ctx->buffered = 0;
    }
    while (length - i >= 16) {
        poly1305_block_add(ctx, message + i, 16);
        i += 16;
    }
    while (i < length) {
        ctx->buffer[ctx->buffered++] = message[i++];
    }
}

void zeroos_poly1305_final(struct zeroos_poly1305_ctx *ctx,
                           uint8_t tag[ZEROOS_POLY1305_TAG_LEN]) {
    uint32_t h0, h1, h2, h3, h4;
    uint32_t c, w[4];
    unsigned i;

    if (!ctx || !tag)
        return;
    if (ctx->buffered)
        poly1305_block_add(ctx, ctx->buffer, ctx->buffered);

    /* h < 2^130 (reduced): conditional subtract p = 2^130 - 5 via
     * g = h + 5; overflow of the 130-bit space means h >= p. */
    {
        uint32_t g[5];
        c = 5u;
        for (i = 0; i < 5; i++) {
            g[i] = ctx->h[i] + c;
            c = g[i] >> 26;
            g[i] &= P26;
        }
        if (c)
            for (i = 0; i < 5; i++)
                ctx->h[i] = g[i];
    }

    /* tag = (h + s) mod 2^128, serialized little-endian. */
    c = 0;
    for (i = 0; i < 5; i++) {
        uint64_t v = (uint64_t)ctx->h[i] + ctx->s[i] + c;
        ctx->h[i] = (uint32_t)v & P26;
        c = (uint32_t)(v >> 26);
    }

    h0 = ctx->h[0];
    h1 = ctx->h[1];
    h2 = ctx->h[2];
    h3 = ctx->h[3];
    h4 = ctx->h[4];
    w[0] = h0 | (h1 << 26);
    w[1] = (h1 >> 6) | (h2 << 20);
    w[2] = (h2 >> 12) | (h3 << 14);
    w[3] = (h3 >> 18) | (h4 << 8);
    for (i = 0; i < 4; i++)
        store32_le(tag + 4 * i, w[i]);

    for (i = 0; i < 5; i++) {
        ctx->h[i] = 0;
        ctx->r[i] = 0;
        ctx->s[i] = 0;
    }
    ctx->buffered = 0;
    for (i = 0; i < 16; i++)
        ctx->buffer[i] = 0;
}

void zeroos_poly1305(const uint8_t key[ZEROOS_POLY1305_KEY_LEN],
                     const uint8_t *message, uint32_t length,
                     uint8_t tag[ZEROOS_POLY1305_TAG_LEN]) {
    struct zeroos_poly1305_ctx ctx;

    zeroos_poly1305_init(&ctx, key);
    if (message && length)
        zeroos_poly1305_update(&ctx, message, length);
    zeroos_poly1305_final(&ctx, tag);
}

/* ------------------------------------------------------------------ */
/* AEAD_CHACHA20_POLY1305                                             */
/* ------------------------------------------------------------------ */

static int aead_check(const uint8_t *aad, uint32_t aad_len,
                      const uint8_t *data, uint32_t data_len,
                      const uint8_t *tag) {
    if (!aad && aad_len)
        return 0;
    if (!data && data_len)
        return 0;
    if (aad_len > ZEROOS_CRYPTO_MAX_LEN || data_len > ZEROOS_CRYPTO_MAX_LEN)
        return 0;
    if (!tag)
        return 0;
    return 1;
}

static void aead_mac(const uint8_t otk[ZEROOS_POLY1305_KEY_LEN],
                     const uint8_t *aad, uint32_t aad_len,
                     const uint8_t *cipher, uint32_t ct_len,
                     uint8_t tag[ZEROOS_AEAD_TAG_LEN]) {
    static const uint8_t zeros[16] = {0};
    struct zeroos_poly1305_ctx ctx;
    uint8_t lens[16];
    uint32_t pad;

    zeroos_poly1305_init(&ctx, otk);
    if (aad_len)
        zeroos_poly1305_update(&ctx, aad, aad_len);
    pad = (16u - (aad_len & 15u)) & 15u;
    if (pad)
        zeroos_poly1305_update(&ctx, zeros, pad);
    if (ct_len)
        zeroos_poly1305_update(&ctx, cipher, ct_len);
    pad = (16u - (ct_len & 15u)) & 15u;
    if (pad)
        zeroos_poly1305_update(&ctx, zeros, pad);

    {   /* le64(aad_len) | le64(ct_len) */
        uint32_t i;
        uint64_t a = aad_len, c = ct_len;
        for (i = 0; i < 8; i++) {
            lens[i] = (uint8_t)a;
            a >>= 8;
            lens[8 + i] = (uint8_t)c;
            c >>= 8;
        }
    }
    zeroos_poly1305_update(&ctx, lens, 16);
    zeroos_poly1305_final(&ctx, tag);
}

static int tags_equal(const uint8_t *a, const uint8_t *b) {
    uint8_t diff = 0;
    unsigned i;

    for (i = 0; i < ZEROOS_AEAD_TAG_LEN; i++)
        diff = (uint8_t)(diff | (uint8_t)(a[i] ^ b[i]));
    return diff == 0;
}

int zeroos_aead_encrypt(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                        const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                        const uint8_t *aad, uint32_t aad_len,
                        const uint8_t *plaintext, uint32_t pt_len,
                        uint8_t *ciphertext,
                        uint8_t tag[ZEROOS_AEAD_TAG_LEN]) {
    uint8_t otk[ZEROOS_POLY1305_KEY_LEN];
    unsigned i;

    if (!key || !nonce || !ciphertext ||
        !aead_check(aad, aad_len, plaintext, pt_len, tag))
        return ZCRYPTO_ERR;
    zeroos_chacha20_xor(key, 1u, nonce, plaintext, ciphertext, pt_len);
    zeroos_poly1305_key_gen(key, nonce, otk);
    aead_mac(otk, aad, aad_len, ciphertext, pt_len, tag);
    for (i = 0; i < sizeof(otk); i++)
        otk[i] = 0;
    return ZCRYPTO_OK;
}

int zeroos_aead_decrypt(const uint8_t key[ZEROOS_CHACHA20_KEY_LEN],
                        const uint8_t nonce[ZEROOS_CHACHA20_NONCE_LEN],
                        const uint8_t *aad, uint32_t aad_len,
                        const uint8_t *ciphertext, uint32_t ct_len,
                        const uint8_t tag[ZEROOS_AEAD_TAG_LEN],
                        uint8_t *plaintext) {
    uint8_t otk[ZEROOS_POLY1305_KEY_LEN];
    uint8_t want[ZEROOS_AEAD_TAG_LEN];
    unsigned i;
    int ok;

    if (!key || !nonce || !ciphertext ||
        !aead_check(aad, aad_len, ciphertext, ct_len, tag))
        return ZCRYPTO_ERR;
    if (!plaintext && ct_len)
        return ZCRYPTO_ERR;

    zeroos_poly1305_key_gen(key, nonce, otk);
    aead_mac(otk, aad, aad_len, ciphertext, ct_len, want);
    for (i = 0; i < sizeof(otk); i++)
        otk[i] = 0;

    ok = tags_equal(want, tag);
    if (!ok) {
        /* Never hand back unauthenticated plaintext: zero output. */
        for (i = 0; i < ct_len; i++)
            plaintext[i] = 0;
        for (i = 0; i < sizeof(want); i++)
            want[i] = 0;
        return ZCRYPTO_AUTHFAIL;
    }
    zeroos_chacha20_xor(key, 1u, nonce, ciphertext, plaintext, ct_len);
    for (i = 0; i < sizeof(want); i++)
        want[i] = 0;
    return ZCRYPTO_OK;
}
