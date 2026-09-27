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

int main(void) {
    test_block();
    test_cipher();
    test_poly1305();
    test_key_gen();
    test_aead();
    printf("checks=%d failures=0\n", checks);
    return 0;
}
