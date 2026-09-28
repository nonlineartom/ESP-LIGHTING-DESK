#include "mesh_crypto.h"

#include <string.h>

#ifdef MESHDECK_HOST_TEST
#include <openssl/evp.h>
#include <stdio.h>
#else
#include "mbedtls/aes.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/bignum.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#endif

/* ------------------------------------------------------------------------- */
/* AES block                                                                  */
/* ------------------------------------------------------------------------- */

void mesh_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
#ifdef MESHDECK_HOST_TEST
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outl = 0;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL);
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    EVP_EncryptUpdate(ctx, out, &outl, in, 16);
    EVP_CIPHER_CTX_free(ctx);
#else
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&aes);
#endif
}

/* ------------------------------------------------------------------------- */
/* AES-CMAC                                                                   */
/* ------------------------------------------------------------------------- */

static void cmac_shift_left(const uint8_t in[16], uint8_t out[16])
{
    uint8_t carry = 0;
    for (int i = 15; i >= 0; i--) {
        out[i] = (uint8_t)((in[i] << 1) | carry);
        carry = (in[i] & 0x80) ? 1 : 0;
    }
}

void mesh_aes_cmac(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t mac[16])
{
    uint8_t zero[16] = { 0 }, L[16], K1[16], K2[16];
    mesh_aes128_encrypt(key, zero, L);
    cmac_shift_left(L, K1);
    if (L[0] & 0x80) K1[15] ^= 0x87;
    cmac_shift_left(K1, K2);
    if (K1[0] & 0x80) K2[15] ^= 0x87;

    size_t n = (len + 15) / 16;
    bool last_complete = (len > 0) && (len % 16 == 0);
    if (n == 0) n = 1;

    uint8_t x[16] = { 0 }, y[16], last[16] = { 0 };

    for (size_t i = 0; i + 1 < n; i++) {
        for (int j = 0; j < 16; j++) y[j] = x[j] ^ msg[i * 16 + j];
        mesh_aes128_encrypt(key, y, x);
    }

    if (last_complete) {
        for (int j = 0; j < 16; j++) last[j] = msg[(n - 1) * 16 + j] ^ K1[j];
    } else {
        size_t rem = len % 16;
        if (rem) memcpy(last, msg + (n - 1) * 16, rem);
        last[rem] = 0x80;
        for (int j = 0; j < 16; j++) last[j] ^= K2[j];
    }
    for (int j = 0; j < 16; j++) y[j] = x[j] ^ last[j];
    mesh_aes128_encrypt(key, y, mac);
}

/* ------------------------------------------------------------------------- */
/* AES-CCM (RFC 3610), nonce length 13 => L = 2                               */
/* ------------------------------------------------------------------------- */

static void ccm_cbc_mac(const uint8_t key[16], const uint8_t nonce[13],
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *pt, size_t len, uint8_t *T, size_t mic_len)
{
    uint8_t x[16], b[16];

    /* B0 */
    b[0] = (uint8_t)((aad_len ? 0x40 : 0) | (((mic_len - 2) / 2) << 3) | 1 /* L-1 */);
    memcpy(b + 1, nonce, 13);
    b[14] = (uint8_t)(len >> 8);
    b[15] = (uint8_t)(len & 0xff);
    mesh_aes128_encrypt(key, b, x);

    /* AAD blocks: 2-byte length prefix then data, zero padded */
    if (aad_len) {
        size_t pos = 0;
        memset(b, 0, 16);
        b[0] = (uint8_t)(aad_len >> 8);
        b[1] = (uint8_t)(aad_len & 0xff);
        size_t take = aad_len < 14 ? aad_len : 14;
        memcpy(b + 2, aad, take);
        pos = take;
        for (int j = 0; j < 16; j++) b[j] ^= x[j];
        mesh_aes128_encrypt(key, b, x);
        while (pos < aad_len) {
            memset(b, 0, 16);
            take = (aad_len - pos) < 16 ? (aad_len - pos) : 16;
            memcpy(b, aad + pos, take);
            pos += take;
            for (int j = 0; j < 16; j++) b[j] ^= x[j];
            mesh_aes128_encrypt(key, b, x);
        }
    }

    /* payload blocks */
    for (size_t pos = 0; pos < len; pos += 16) {
        memset(b, 0, 16);
        size_t take = (len - pos) < 16 ? (len - pos) : 16;
        memcpy(b, pt + pos, take);
        for (int j = 0; j < 16; j++) b[j] ^= x[j];
        mesh_aes128_encrypt(key, b, x);
    }
    memcpy(T, x, mic_len);
}

static void ccm_ctr(const uint8_t key[16], const uint8_t nonce[13],
                    const uint8_t *in, size_t len, uint8_t *out, uint8_t *S0)
{
    uint8_t a[16], s[16];
    a[0] = 0x01; /* L - 1 */
    memcpy(a + 1, nonce, 13);
    a[14] = 0; a[15] = 0;
    mesh_aes128_encrypt(key, a, S0);

    uint16_t ctr = 1;
    for (size_t pos = 0; pos < len; pos += 16, ctr++) {
        a[14] = (uint8_t)(ctr >> 8);
        a[15] = (uint8_t)(ctr & 0xff);
        mesh_aes128_encrypt(key, a, s);
        size_t take = (len - pos) < 16 ? (len - pos) : 16;
        for (size_t j = 0; j < take; j++) out[pos + j] = in[pos + j] ^ s[j];
    }
}

int mesh_aes_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13],
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *pt, size_t len,
                         uint8_t *ct, uint8_t *mic, size_t mic_len)
{
    uint8_t T[16], S0[16];
    ccm_cbc_mac(key, nonce, aad, aad_len, pt, len, T, mic_len);
    ccm_ctr(key, nonce, pt, len, ct, S0);
    for (size_t i = 0; i < mic_len; i++) mic[i] = T[i] ^ S0[i];
    return 0;
}

int mesh_aes_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13],
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *ct, size_t len,
                         uint8_t *pt, const uint8_t *mic, size_t mic_len)
{
    uint8_t T[16], S0[16];
    ccm_ctr(key, nonce, ct, len, pt, S0);
    ccm_cbc_mac(key, nonce, aad, aad_len, pt, len, T, mic_len);
    uint8_t diff = 0;
    for (size_t i = 0; i < mic_len; i++) diff |= (uint8_t)(T[i] ^ S0[i] ^ mic[i]);
    if (diff) {
        memset(pt, 0, len);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Key derivation                                                             */
/* ------------------------------------------------------------------------- */

void mesh_s1(const uint8_t *m, size_t len, uint8_t out[16])
{
    static const uint8_t zero[16] = { 0 };
    mesh_aes_cmac(zero, m, len, out);
}

void mesh_k1(const uint8_t *n, size_t n_len, const uint8_t salt[16],
             const uint8_t *p, size_t p_len, uint8_t out[16])
{
    uint8_t t[16];
    mesh_aes_cmac(salt, n, n_len, t);
    mesh_aes_cmac(t, p, p_len, out);
}

void mesh_k2(const uint8_t netkey[16], const uint8_t *p, size_t p_len,
             uint8_t *nid, uint8_t enc_key[16], uint8_t priv_key[16])
{
    uint8_t salt[16], t[16], t1[16], buf[16 + 16 + 1];
    mesh_s1((const uint8_t *)"smk2", 4, salt);
    mesh_aes_cmac(salt, netkey, 16, t);

    /* T1 = CMAC(T, P || 0x01) */
    memcpy(buf, p, p_len);
    buf[p_len] = 0x01;
    mesh_aes_cmac(t, buf, p_len + 1, t1);
    /* T2 = CMAC(T, T1 || P || 0x02) */
    memcpy(buf, t1, 16);
    memcpy(buf + 16, p, p_len);
    buf[16 + p_len] = 0x02;
    mesh_aes_cmac(t, buf, 16 + p_len + 1, enc_key);
    /* T3 = CMAC(T, T2 || P || 0x03) */
    memcpy(buf, enc_key, 16);
    memcpy(buf + 16, p, p_len);
    buf[16 + p_len] = 0x03;
    mesh_aes_cmac(t, buf, 16 + p_len + 1, priv_key);

    *nid = t1[15] & 0x7f;
}

void mesh_k3(const uint8_t netkey[16], uint8_t network_id[8])
{
    uint8_t salt[16], t[16], r[16];
    mesh_s1((const uint8_t *)"smk3", 4, salt);
    mesh_aes_cmac(salt, netkey, 16, t);
    mesh_aes_cmac(t, (const uint8_t *)"id64\x01", 5, r);
    memcpy(network_id, r + 8, 8);
}

uint8_t mesh_k4(const uint8_t appkey[16])
{
    uint8_t salt[16], t[16], r[16];
    mesh_s1((const uint8_t *)"smk4", 4, salt);
    mesh_aes_cmac(salt, appkey, 16, t);
    mesh_aes_cmac(t, (const uint8_t *)"id6\x01", 4, r);
    return r[15] & 0x3f;
}

void mesh_identity_key(const uint8_t netkey[16], uint8_t out[16])
{
    uint8_t salt[16];
    mesh_s1((const uint8_t *)"nkik", 4, salt);
    mesh_k1(netkey, 16, salt, (const uint8_t *)"id128\x01", 6, out);
}

void mesh_beacon_key(const uint8_t netkey[16], uint8_t out[16])
{
    uint8_t salt[16];
    mesh_s1((const uint8_t *)"nkbk", 4, salt);
    mesh_k1(netkey, 16, salt, (const uint8_t *)"id128\x01", 6, out);
}

/* ------------------------------------------------------------------------- */
/* Random + ECDH                                                              */
/* ------------------------------------------------------------------------- */

#ifdef MESHDECK_HOST_TEST

void mesh_random(uint8_t *buf, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t n = fread(buf, 1, len, f);
        (void)n;
        fclose(f);
    }
}

mesh_ecdh_t *mesh_ecdh_new(void) { return NULL; }
int  mesh_ecdh_public_key(mesh_ecdh_t *e, uint8_t pub[64]) { (void)e; (void)pub; return -1; }
int  mesh_ecdh_shared_secret(mesh_ecdh_t *e, const uint8_t peer_pub[64], uint8_t secret[32])
{ (void)e; (void)peer_pub; (void)secret; return -1; }
void mesh_ecdh_free(mesh_ecdh_t *e) { (void)e; }

#else

void mesh_random(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
}

struct mesh_ecdh {
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point Q;
};

static int rng_cb(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    esp_fill_random(out, len);
    return 0;
}

mesh_ecdh_t *mesh_ecdh_new(void)
{
    mesh_ecdh_t *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    mbedtls_ecp_group_init(&e->grp);
    mbedtls_mpi_init(&e->d);
    mbedtls_ecp_point_init(&e->Q);
    if (mbedtls_ecp_group_load(&e->grp, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
        mbedtls_ecp_gen_keypair(&e->grp, &e->d, &e->Q, rng_cb, NULL) != 0) {
        mesh_ecdh_free(e);
        return NULL;
    }
    return e;
}

int mesh_ecdh_public_key(mesh_ecdh_t *e, uint8_t pub[64])
{
    if (!e) return -1;
    if (mbedtls_mpi_write_binary(&e->Q.MBEDTLS_PRIVATE(X), pub, 32) != 0) return -1;
    if (mbedtls_mpi_write_binary(&e->Q.MBEDTLS_PRIVATE(Y), pub + 32, 32) != 0) return -1;
    return 0;
}

int mesh_ecdh_shared_secret(mesh_ecdh_t *e, const uint8_t peer_pub[64], uint8_t secret[32])
{
    if (!e) return -1;
    int rc = -1;
    mbedtls_ecp_point Qp;
    mbedtls_mpi z;
    mbedtls_ecp_point_init(&Qp);
    mbedtls_mpi_init(&z);

    uint8_t uncompressed[65];
    uncompressed[0] = 0x04;
    memcpy(uncompressed + 1, peer_pub, 64);
    if (mbedtls_ecp_point_read_binary(&e->grp, &Qp, uncompressed, 65) != 0) goto out;
    if (mbedtls_ecp_check_pubkey(&e->grp, &Qp) != 0) goto out;
    if (mbedtls_ecdh_compute_shared(&e->grp, &z, &Qp, &e->d, rng_cb, NULL) != 0) goto out;
    if (mbedtls_mpi_write_binary(&z, secret, 32) != 0) goto out;
    rc = 0;
out:
    mbedtls_mpi_free(&z);
    mbedtls_ecp_point_free(&Qp);
    return rc;
}

void mesh_ecdh_free(mesh_ecdh_t *e)
{
    if (!e) return;
    mbedtls_ecp_point_free(&e->Q);
    mbedtls_mpi_free(&e->d);
    mbedtls_ecp_group_free(&e->grp);
    free(e);
}

#endif /* MESHDECK_HOST_TEST */
