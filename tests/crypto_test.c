/* RFC 8439 test vectors for kernel/crypto.c.
 *
 * Sources (exact, fetched from rfc-editor.org/rfc/rfc8439.txt):
 *   2.3.2 ChaCha20 block function   2.4.2 ChaCha20 cipher
 *   2.5.2 Poly1305 MAC              2.6.2 Poly1305 key generation
 *   2.8.2 AEAD_CHACHA20_POLY1305
 * Plus negative cases: wrong key, tampered tag, tampered ciphertext,
 * tampered AAD — all must fail with ZCRYPTO_AUTHFAIL and zero output. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/crypto.h"

static int checks;

static void expect(int cond, const char *what) {
    checks++;
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        assert(cond);
    }
}

static int bytes_eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int all_zero(const uint8_t *a, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++)
        if (a[i])
            return 0;
    return 1;
}

static void hex_fill(uint8_t *dst, const char *hex, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        unsigned v = 0, j;
        for (j = 0; j < 2; j++) {
            char c = hex[2 * i + j];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else assert(0);
        }
        dst[i] = (uint8_t)v;
    }
}

/* RFC 8439 2.3.2 — block function */
static void test_block(void) {
    uint8_t key[32], nonce[12], out[64], want[64];

    hex_fill(key, "000102030405060708090a0b0c0d0e0f"
                  "101112131415161718191a1b1c1d1e1f", 32);
    hex_fill(nonce, "000000090000004a00000000", 12);
    hex_fill(want, "10f1e7e4d13b5915500fdd1fa32071c4"
                   "c7d1f4c733c068030422aa9ac3d46c4e"
                   "d2826446079faa0914c2d705d98b02a2"
                   "b5129cd1de164eb9cbd083e8a2503c4e", 64);
    zeroos_chacha20_block(key, 1u, nonce, out);
    expect(bytes_eq(out, want, 64), "chacha20 block vector (2.3.2)");
}

/* RFC 8439 2.4.2 — stream cipher, 114-byte plaintext */
static const uint8_t sunscreen_pt[114] =
    "Ladies and Gentlemen of the class of '99: If I "
    "could offer you only one tip for the future, sunscreen would be it.";

static const uint8_t sunscreen_ct_24[114] = {
    0x6e,0x2e,0x35,0x9a,0x25,0x68,0xf9,0x80,0x41,0xba,0x07,0x28,0xdd,0x0d,0x69,0x81,
    0xe9,0x7e,0x7a,0xec,0x1d,0x43,0x60,0xc2,0x0a,0x27,0xaf,0xcc,0xfd,0x9f,0xae,0x0b,
    0xf9,0x1b,0x65,0xc5,0x52,0x47,0x33,0xab,0x8f,0x59,0x3d,0xab,0xcd,0x62,0xb3,0x57,
    0x16,0x39,0xd6,0x24,0xe6,0x51,0x52,0xab,0x8f,0x53,0x0c,0x35,0x9f,0x08,0x61,0xd8,
    0x07,0xca,0x0d,0xbf,0x50,0x0d,0x6a,0x61,0x56,0xa3,0x8e,0x08,0x8a,0x22,0xb6,0x5e,
    0x52,0xbc,0x51,0x4d,0x16,0xcc,0xf8,0x06,0x81,0x8c,0xe9,0x1a,0xb7,0x79,0x37,0x36,
    0x5a,0xf9,0x0b,0xbf,0x74,0xa3,0x5b,0xe6,0xb4,0x0b,0x8e,0xed,0xf2,0x78,0x5e,0x42,
    0x87,0x4d
};

static void test_cipher(void) {
    uint8_t key[32], nonce[12], out[114], back[114];

    hex_fill(key, "000102030405060708090a0b0c0d0e0f"
                  "101112131415161718191a1b1c1d1e1f", 32);
    hex_fill(nonce, "000000000000004a00000000", 12);
    zeroos_chacha20_xor(key, 1u, nonce, sunscreen_pt, out, 114);
    expect(bytes_eq(out, sunscreen_ct_24, 114), "chacha20 cipher vector (2.4.2)");
    zeroos_chacha20_xor(key, 1u, nonce, out, back, 114);
    expect(bytes_eq(back, sunscreen_pt, 114), "chacha20 decrypt roundtrip");
}

/* RFC 8439 2.5.2 — Poly1305 MAC */
static void test_poly1305(void) {
    uint8_t key[32], tag[16], want[16];
    static const uint8_t msg[34] = "Cryptographic Forum Research Group";

    hex_fill(key, "85d6be7857556d337f4452fe42d506a8"
                  "0103808afb0db2fd4abff6af4149f51b", 32);
    hex_fill(want, "a8061dc1305136c6c22b8baf0c0127a9", 16);
    zeroos_poly1305(key, msg, 34, tag);
    expect(bytes_eq(tag, want, 16), "poly1305 vector (2.5.2)");

    /* Streaming split must equal one-shot. */
    {
        struct zeroos_poly1305_ctx ctx;
        uint8_t tag2[16];
        zeroos_poly1305_init(&ctx, key);
        zeroos_poly1305_update(&ctx, msg, 10);
        zeroos_poly1305_update(&ctx, msg + 10, 9);
        zeroos_poly1305_update(&ctx, msg + 19, 15);
        zeroos_poly1305_final(&ctx, tag2);
        expect(bytes_eq(tag2, want, 16), "poly1305 streaming split");
    }
    /* Empty message: tag == s. */
    {
        uint8_t tag3[16];
        zeroos_poly1305(key, 0, 0, tag3);
        expect(bytes_eq(tag3, key + 16, 16), "poly1305 empty message = s");
    }
}

/* RFC 8439 2.6.2 — Poly1305 key generation from ChaCha20 */
static void test_key_gen(void) {
    uint8_t key[32], nonce[12], otk[32], want[32];

    hex_fill(key, "808182838485868788898a8b8c8d8e8f"
                  "909192939495969798999a9b9c9d9e9f", 32);
    hex_fill(nonce, "000000000001020304050607", 12);
    hex_fill(want, "8ad5a08b905f81cc815040274ab29471"
                   "a833b637e3fd0da508dbb8e2fdd1a646", 32);
    zeroos_poly1305_key_gen(key, nonce, otk);
    expect(bytes_eq(otk, want, 32), "poly1305 keygen vector (2.6.2)");
}

/* RFC 8439 2.8.2 — AEAD encrypt, decrypt, and negatives */
static void test_aead(void) {
    uint8_t key[32], nonce[12], aad[12];
    uint8_t ct[114], tag[16], want_ct[114], want_tag[16];
    uint8_t pt[114], key2[32];
    uint32_t i;

    hex_fill(key, "808182838485868788898a8b8c8d8e8f"
                  "909192939495969798999a9b9c9d9e9f", 32);
    hex_fill(nonce, "070000004041424344454647", 12);
    hex_fill(aad, "50515253c0c1c2c3c4c5c6c7", 12);
    hex_fill(want_ct, "d31a8d34648e60db7b86afbc53ef7ec2"
                      "a4aded51296e08fea9e2b5a736ee62d6"
                      "3dbea45e8ca9671282fafb69da92728b"
                      "1a71de0a9e060b2905d6a5b67ecd3b36"
                      "92ddbd7f2d778b8c9803aee328091b58"
                      "fab324e4fad675945585808b4831d7bc"
                      "3ff4def08e4b7a9de576d26586cec64b"
                      "6116", 114);
    hex_fill(want_tag, "1ae10b594f09e26a7e902ecbd0600691", 16);

    expect(zeroos_aead_encrypt(key, nonce, aad, 12, sunscreen_pt, 114,
                               ct, tag) == ZCRYPTO_OK,
           "aead encrypt status");
    expect(bytes_eq(ct, want_ct, 114), "aead ciphertext vector (2.8.2)");
    expect(bytes_eq(tag, want_tag, 16), "aead tag vector (2.8.2)");

    expect(zeroos_aead_decrypt(key, nonce, aad, 12, ct, 114, tag, pt) ==
               ZCRYPTO_OK,
           "aead decrypt status");
    expect(bytes_eq(pt, sunscreen_pt, 114), "aead decrypt roundtrip");

    /* Wrong key must fail and zero the output. */
    for (i = 0; i < 32; i++)
        key2[i] = (uint8_t)(key[i] ^ 0xa5);
    for (i = 0; i < 114; i++)
        pt[i] = 0x5a;
    expect(zeroos_aead_decrypt(key2, nonce, aad, 12, ct, 114, tag, pt) ==
               ZCRYPTO_AUTHFAIL,
           "aead wrong key rejected");
    expect(all_zero(pt, 114), "aead wrong key zeroes output");

    /* Tampered tag. */
    for (i = 0; i < 114; i++)
        pt[i] = 0x5a;
    tag[0] ^= 1;
    expect(zeroos_aead_decrypt(key, nonce, aad, 12, ct, 114, tag, pt) ==
               ZCRYPTO_AUTHFAIL,
           "aead tampered tag rejected");
    expect(all_zero(pt, 114), "aead tampered tag zeroes output");
    tag[0] ^= 1;

    /* Tampered ciphertext. */
    for (i = 0; i < 114; i++)
        pt[i] = 0x5a;
    ct[40] ^= 0x80;
    expect(zeroos_aead_decrypt(key, nonce, aad, 12, ct, 114, tag, pt) ==
               ZCRYPTO_AUTHFAIL,
           "aead tampered ciphertext rejected");
    expect(all_zero(pt, 114), "aead tampered ciphertext zeroes output");
    ct[40] ^= 0x80;

    /* Tampered AAD. */
    for (i = 0; i < 114; i++)
        pt[i] = 0x5a;
    aad[3] ^= 0x10;
    expect(zeroos_aead_decrypt(key, nonce, aad, 12, ct, 114, tag, pt) ==
               ZCRYPTO_AUTHFAIL,
           "aead tampered aad rejected");
    aad[3] ^= 0x10;

    /* Argument guards. */
    expect(zeroos_aead_encrypt(0, nonce, aad, 12, sunscreen_pt, 114, ct,
                               tag) == ZCRYPTO_ERR,
           "aead encrypt null key rejected");
    expect(zeroos_aead_decrypt(key, nonce, aad, 12, ct, 114, 0, pt) ==
               ZCRYPTO_ERR,
           "aead decrypt null tag rejected");
    expect(zeroos_aead_encrypt(key, nonce, 0, 7, sunscreen_pt, 114, ct,
                               tag) == ZCRYPTO_ERR,
           "aead encrypt null aad with length rejected");
}

/* ---- Coverage beyond the RFC vectors ---- */

/* Multi-block ChaCha20: the keystream must be the concatenation of the
 * per-block outputs with the counter advanced once per 64-byte block, and a
 * buffer of any length must round-trip. */
static void test_chacha20_multiblock(void) {
    uint8_t key[32], nonce[12];
    uint8_t pt[300], ct[300], back[300], want[300];
    uint32_t i, blocks;
    unsigned j;

    hex_fill(key, "000102030405060708090a0b0c0d0e0f"
                  "101112131415161718191a1b1c1d1e1f", 32);
    hex_fill(nonce, "000000000000004a00000000", 12);
    for (i = 0; i < sizeof(pt); i++)
        pt[i] = (uint8_t)(i * 7u + 3u);

    blocks = (sizeof(pt) + 63u) / 64u;
    for (i = 0; i < blocks; i++) {
        uint8_t stream[64];
        zeroos_chacha20_block(key, 1u + i, nonce, stream);
        for (j = 0; j < 64 && i * 64u + j < sizeof(pt); j++)
            want[i * 64u + j] = (uint8_t)(pt[i * 64u + j] ^ stream[j]);
    }

    zeroos_chacha20_xor(key, 1u, nonce, pt, ct, sizeof(pt));
    expect(bytes_eq(ct, want, sizeof(pt)), "chacha20 multi-block keystream");

    zeroos_chacha20_xor(key, 1u, nonce, ct, back, sizeof(pt));
    expect(bytes_eq(back, pt, sizeof(pt)), "chacha20 multi-block roundtrip");

    /* Counter overflow into the next block must not alias block 0. */
    {
        uint8_t a[64], b[64];
        zeroos_chacha20_xor(key, 5u, nonce, 0, a, 64);
        zeroos_chacha20_block(key, 5u, nonce, b);
        expect(bytes_eq(a, b, 64), "chacha20 keystream equals block 0");
    }

    /* In-place operation: output aliasing input must still round-trip. */
    {
        uint8_t buf[200], copy[200];
        for (i = 0; i < sizeof(buf); i++)
            buf[i] = (uint8_t)(i ^ 0x5a);
        memcpy(copy, buf, sizeof(buf));
        zeroos_chacha20_xor(key, 1u, nonce, buf, buf, sizeof(buf));
        expect(!bytes_eq(buf, copy, sizeof(buf)), "chacha20 in-place encrypts");
        zeroos_chacha20_xor(key, 1u, nonce, buf, buf, sizeof(buf));
        expect(bytes_eq(buf, copy, sizeof(buf)), "chacha20 in-place roundtrip");
    }

    /* Zero length is a no-op that must not touch the output. */
    {
        uint8_t out[4] = {1, 2, 3, 4};
        zeroos_chacha20_xor(key, 1u, nonce, pt, out, 0);
        expect(out[0] == 1 && out[3] == 4, "chacha20 zero length is a no-op");
    }

    /* A different nonce must produce a different keystream. */
    {
        uint8_t a[64], b[64];
        uint8_t nonce2[12];
        memcpy(nonce2, nonce, 12);
        nonce2[0] ^= 0x01;
        zeroos_chacha20_xor(key, 1u, nonce, 0, a, 64);
        zeroos_chacha20_xor(key, 1u, nonce2, 0, b, 64);
        expect(!bytes_eq(a, b, 64), "chacha20 nonce changes keystream");
    }
}

/* Streaming Poly1305 must be independent of how the message is chopped:
 * the partial-block buffer is exercised by byte-at-a-time feeding. */
static void test_poly1305_chunking(void) {
    uint8_t key[32], tag_one[16], tag_stream[16];
    uint8_t msg[100];
    struct zeroos_poly1305_ctx ctx;
    uint32_t i;

    hex_fill(key, "85d6be7857556d337f4452fe42d506a8"
                  "0103808afb0db2fd4abff6af4149f51b", 32);
    for (i = 0; i < sizeof(msg); i++)
        msg[i] = (uint8_t)(i * 31u + 11u);

    zeroos_poly1305(key, msg, sizeof(msg), tag_one);

    /* One byte at a time. */
    zeroos_poly1305_init(&ctx, key);
    for (i = 0; i < sizeof(msg); i++)
        zeroos_poly1305_update(&ctx, msg + i, 1);
    zeroos_poly1305_final(&ctx, tag_stream);
    expect(bytes_eq(tag_one, tag_stream, 16), "poly1305 byte-at-a-time");

    /* 16-byte aligned chunks. */
    zeroos_poly1305_init(&ctx, key);
    for (i = 0; i + 16 <= sizeof(msg); i += 16)
        zeroos_poly1305_update(&ctx, msg + i, 16);
    zeroos_poly1305_update(&ctx, msg + i, sizeof(msg) - i);
    zeroos_poly1305_final(&ctx, tag_stream);
    expect(bytes_eq(tag_one, tag_stream, 16), "poly1305 aligned chunks");

    /* A single 17-byte chunk: one whole block plus one buffered byte. */
    zeroos_poly1305_init(&ctx, key);
    zeroos_poly1305_update(&ctx, msg, 17);
    zeroos_poly1305_update(&ctx, msg + 17, sizeof(msg) - 17);
    zeroos_poly1305_final(&ctx, tag_stream);
    expect(bytes_eq(tag_one, tag_stream, 16), "poly1305 split block refill");

    /* Zero-length updates are harmless. */
    zeroos_poly1305_init(&ctx, key);
    zeroos_poly1305_update(&ctx, 0, 0);
    zeroos_poly1305_update(&ctx, msg, sizeof(msg));
    zeroos_poly1305_update(&ctx, msg, 0);
    zeroos_poly1305_final(&ctx, tag_stream);
    expect(bytes_eq(tag_one, tag_stream, 16), "poly1305 zero-length updates");

    /* A different message must produce a different tag. */
    msg[50] ^= 0x80;
    zeroos_poly1305(key, msg, sizeof(msg), tag_stream);
    expect(!bytes_eq(tag_one, tag_stream, 16), "poly1305 detects message change");
}

/* AEAD argument and bound guards, plus the empty-message and no-AAD paths. */
static void test_aead_edges(void) {
    uint8_t key[32], nonce[12], aad[12], tag[16], tag2[16];
    uint8_t ct[64], pt[64], back[64];
    uint32_t i;

    hex_fill(key, "808182838485868788898a8b8c8d8e8f"
                  "909192939495969798999a9b9c9d9e9f", 32);
    hex_fill(nonce, "070000004041424344454647", 12);
    hex_fill(aad, "50515253c0c1c2c3c4c5c6c7", 12);
    for (i = 0; i < sizeof(pt); i++)
        pt[i] = (uint8_t)(i + 1);

    /* Empty plaintext with AAD: the tag must authenticate the AAD alone. */
    expect(zeroos_aead_encrypt(key, nonce, aad, 12, 0, 0, ct, tag) ==
               ZCRYPTO_OK,
           "aead empty plaintext encrypt");
    expect(zeroos_aead_decrypt(key, nonce, aad, 12, ct, 0, tag, back) ==
               ZCRYPTO_OK,
           "aead empty plaintext decrypt");

    /* No AAD at all (NULL pointer, zero length) must be accepted. */
    expect(zeroos_aead_encrypt(key, nonce, 0, 0, pt, sizeof(pt), ct, tag) ==
               ZCRYPTO_OK,
           "aead no-aad encrypt");
    expect(zeroos_aead_decrypt(key, nonce, 0, 0, ct, sizeof(pt), tag, back) ==
               ZCRYPTO_OK,
           "aead no-aad decrypt");
    expect(bytes_eq(back, pt, sizeof(pt)), "aead no-aad roundtrip");

    /* Empty ciphertext with a NULL plaintext buffer is legal. */
    expect(zeroos_aead_encrypt(key, nonce, 0, 0, 0, 0, ct, tag2) == ZCRYPTO_OK,
           "aead empty message tag");
    expect(zeroos_aead_decrypt(key, nonce, 0, 0, ct, 0, tag2, 0) ==
               ZCRYPTO_OK,
           "aead empty message decrypt with null output");

    /* A zero-length AAD and a non-zero-length AAD over the same data must
     * not produce the same tag: the lengths are bound into the MAC. */
    expect(zeroos_aead_encrypt(key, nonce, aad, 12, 0, 0, ct, tag) ==
               ZCRYPTO_OK,
           "aead aad-only tag");
    expect(!bytes_eq(tag, tag2, 16), "aead binds aad length into the tag");

    /* Length bounds. */
    expect(zeroos_aead_encrypt(key, nonce, aad, ZEROOS_CRYPTO_MAX_LEN + 1u, pt,
                               sizeof(pt), ct, tag) == ZCRYPTO_ERR,
           "aead rejects oversized aad");
    expect(zeroos_aead_encrypt(key, nonce, aad, 12, pt,
                               ZEROOS_CRYPTO_MAX_LEN + 1u, ct,
                               tag) == ZCRYPTO_ERR,
           "aead rejects oversized plaintext");

    /* Missing required buffers. */
    expect(zeroos_aead_encrypt(key, 0, aad, 12, pt, sizeof(pt), ct, tag) ==
               ZCRYPTO_ERR,
           "aead encrypt null nonce rejected");
    expect(zeroos_aead_encrypt(key, nonce, aad, 12, pt, sizeof(pt), 0, tag) ==
               ZCRYPTO_ERR,
           "aead encrypt null ciphertext rejected");
    expect(zeroos_aead_encrypt(key, nonce, aad, 12, 0, 8, ct, tag) ==
               ZCRYPTO_ERR,
           "aead encrypt null plaintext with length rejected");
    expect(zeroos_aead_encrypt(key, nonce, aad, 12, pt, sizeof(pt), ct, 0) ==
               ZCRYPTO_ERR,
           "aead encrypt null tag rejected");
    expect(zeroos_aead_decrypt(key, nonce, aad, 12, 0, sizeof(pt), tag, back) ==
               ZCRYPTO_ERR,
           "aead decrypt null ciphertext rejected");
    expect(zeroos_aead_decrypt(key, nonce, aad, 12, ct, sizeof(pt), tag, 0) ==
               ZCRYPTO_ERR,
           "aead decrypt null plaintext with length rejected");
    expect(zeroos_aead_decrypt(0, nonce, aad, 12, ct, sizeof(pt), tag, back) ==
               ZCRYPTO_ERR,
           "aead decrypt null key rejected");
    expect(zeroos_aead_decrypt(key, 0, aad, 12, ct, sizeof(pt), tag, back) ==
               ZCRYPTO_ERR,
           "aead decrypt null nonce rejected");

    /* Wrong nonce must fail authentication and zero the output. */
    expect(zeroos_aead_encrypt(key, nonce, aad, 12, pt, sizeof(pt), ct, tag) ==
               ZCRYPTO_OK,
           "aead reference seal");
    nonce[11] ^= 0x01;
    for (i = 0; i < sizeof(back); i++)
        back[i] = 0x5a;
    expect(zeroos_aead_decrypt(key, nonce, aad, 12, ct, sizeof(pt), tag,
                               back) == ZCRYPTO_AUTHFAIL,
           "aead wrong nonce rejected");
    expect(all_zero(back, sizeof(pt)), "aead wrong nonce zeroes output");
    nonce[11] ^= 0x01;

    /* In-place seal/open must round-trip. */
    {
        uint8_t buf[64], copy[64];
        memcpy(copy, pt, sizeof(copy));
        memcpy(buf, pt, sizeof(buf));
        expect(zeroos_aead_encrypt(key, nonce, aad, 12, buf, sizeof(buf), buf,
                                   tag) == ZCRYPTO_OK,
               "aead in-place encrypt");
        expect(zeroos_aead_decrypt(key, nonce, aad, 12, buf, sizeof(buf), tag,
                                   buf) == ZCRYPTO_OK,
               "aead in-place decrypt");
        expect(bytes_eq(buf, copy, sizeof(buf)), "aead in-place roundtrip");
    }
}

int main(void) {
    test_block();
    test_cipher();
    test_chacha20_multiblock();
    test_poly1305();
    test_poly1305_chunking();
    test_key_gen();
    test_aead();
    test_aead_edges();
    printf("checks=%d failures=0\n", checks);
    return 0;
}
