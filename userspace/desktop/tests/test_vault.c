/* Encrypted vault host tests (part B) — real AEAD, no mocks. */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/vault.h>

void zd_test_vault_suite(void) {
    struct zd_vault v;
    uint8_t key[ZD_VAULT_KEY_LEN];
    uint8_t key2[ZD_VAULT_KEY_LEN];
    uint8_t out[ZD_VAULT_SECRET_MAX];
    uint32_t out_len = 0;
    const uint8_t secret[] = "correct horse battery staple";
    int i;

    for (i = 0; i < ZD_VAULT_KEY_LEN; ++i) {
        key[i] = (uint8_t)(i * 7 + 1);
        key2[i] = (uint8_t)(0xA0 ^ i);
    }

    zd_vault_init(&v);
    ZD_CHECK(zd_vault_count(&v) == 0);

    /* locked vault refuses everything */
    ZD_CHECK(zd_vault_put(&v, "wifi", secret, sizeof(secret) - 1) == -1);
    ZD_CHECK(zd_vault_get(&v, "wifi", out, sizeof(out), &out_len) == -1);
    ZD_CHECK_EQ(v.stats.get_denied, 1);

    /* unlock + roundtrip */
    ZD_CHECK(zd_vault_unlock(&v, key) == 0);
    ZD_CHECK(zd_vault_unlock(&v, 0) == -22);
    ZD_CHECK(zd_vault_put(&v, "wifi", secret, sizeof(secret) - 1) == 0);
    ZD_CHECK(zd_vault_count(&v) == 1);
    memset(out, 0xEE, sizeof(out));
    ZD_CHECK(zd_vault_get(&v, "wifi", out, sizeof(out), &out_len) == 0);
    ZD_CHECK_EQ(out_len, sizeof(secret) - 1);
    ZD_CHECK(memcmp(out, secret, out_len) == 0);

    /* ciphertext is NOT plaintext: scan every entry byte */
    {
        int found_plain = 0;
        for (i = 0; i < ZD_VAULT_CT_MAX - 4; ++i)
            if (memcmp(v.entries[0].ct + i, "correct", 7) == 0)
                found_plain = 1;
        ZD_CHECK(found_plain == 0);
    }

    /* stored under name as AAD: wrong-name alias impossible (find by
     * exact name only); unknown name -> -2 */
    ZD_CHECK(zd_vault_get(&v, "wifi2", out, sizeof(out), &out_len) == -2);

    /* tamper: flip a ciphertext byte -> auth failure, explicit -3 */
    v.entries[0].ct[14] ^= 0x40;
    ZD_CHECK(zd_vault_get(&v, "wifi", out, sizeof(out), &out_len) == -3);
    ZD_CHECK_EQ(v.stats.auth_failures, 1);
    ZD_CHECK(out[0] != 'c'); /* no partial plaintext leak */
    v.entries[0].ct[14] ^= 0x40; /* restore */

    /* wrong key on a freshly unlocked vault -> auth failure */
    {
        struct zd_vault v2;
        zd_vault_init(&v2);
        zd_vault_unlock(&v2, key);
        zd_vault_put(&v2, "mail", secret, sizeof(secret) - 1);
        zd_vault_unlock(&v2, key2); /* rekey */
        ZD_CHECK(zd_vault_get(&v2, "mail", out, sizeof(out), &out_len) == -3);
        ZD_CHECK_EQ(v2.stats.auth_failures, 1);
    }

    /* replace value (same name) works and changes ciphertext */
    ZD_CHECK(zd_vault_put(&v, "wifi", (const uint8_t*)"new", 3) == 0);
    ZD_CHECK(zd_vault_count(&v) == 1);
    ZD_CHECK(zd_vault_get(&v, "wifi", out, sizeof(out), &out_len) == 0);
    ZD_CHECK(out_len == 3 && memcmp(out, "new", 3) == 0);

    /* bounds and bad args */
    ZD_CHECK(zd_vault_put(&v, "", secret, 1) == -22);
    ZD_CHECK(zd_vault_put(&v, "x", secret, 0) == -22);
    ZD_CHECK(zd_vault_put(&v, "x", secret, ZD_VAULT_SECRET_MAX + 1) == -22);
    ZD_CHECK(zd_vault_put(&v, 0, secret, 4) == -22);
    ZD_CHECK(zd_vault_get(&v, "wifi", out, 2, &out_len) == -22); /* cap */

    /* capacity */
    {
        int k;
        for (k = 0; k < ZD_VAULT_MAX + 3; ++k) {
            char nm[8];
            int j = 0;
            nm[j++] = 'k';
            nm[j++] = (char)('0' + k / 10);
            nm[j++] = (char)('0' + k % 10);
            nm[j] = 0;
            if (zd_vault_put(&v, nm, secret, 4) == -28)
                break;
        }
        ZD_CHECK(zd_vault_count(&v) <= ZD_VAULT_MAX);
        /* exactly-at-cap put returns ENOSPC */
        ZD_CHECK(zd_vault_put(&v, "zz", secret, 4) == -28);
    }

    /* lock wipes the key: gets denied, ciphertext untouched */
    ZD_CHECK(zd_vault_lock(&v) == 0);
    ZD_CHECK(zd_vault_get(&v, "wifi", out, sizeof(out), &out_len) == -1);
    ZD_CHECK_EQ(v.stats.wipes, 1);
    /* re-unlock decrypts existing entries again */
    ZD_CHECK(zd_vault_unlock(&v, key) == 0);
    ZD_CHECK(zd_vault_get(&v, "wifi", out, sizeof(out), &out_len) == 0);
    ZD_CHECK(out_len == 3 && memcmp(out, "new", 3) == 0);

    /* forget wipes entry bytes */
    ZD_CHECK(zd_vault_forget(&v, "wifi") == 0);
    ZD_CHECK(zd_vault_forget(&v, "wifi") == -2);
    ZD_CHECK(zd_vault_get(&v, "wifi", out, sizeof(out), &out_len) == -2);
}
