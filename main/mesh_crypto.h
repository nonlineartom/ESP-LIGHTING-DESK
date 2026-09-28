/*
 * Bluetooth Mesh cryptographic toolbox (Mesh Profile 1.0.1, section 3.8).
 *
 * The AES primitive comes from mbedTLS on the target and from OpenSSL when the
 * file is compiled for the host unit tests (-DMESHDECK_HOST_TEST).  Everything
 * above the block cipher (CMAC, CCM, s1/k1..k4) is implemented here so the exact
 * same code is exercised by the host tests and by the firmware.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AES-128 single block, ECB. */
void mesh_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

/* AES-CMAC (RFC 4493). */
void mesh_aes_cmac(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t mac[16]);

/*
 * AES-CCM with a 13-byte nonce (L = 2) as used by the mesh network/transport
 * layers.  mic_len is 4 or 8.  aad may be NULL/0.  ct and pt may alias.
 * Returns 0 on success, -1 on authentication failure (decrypt only).
 */
int mesh_aes_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13],
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *pt, size_t len,
                         uint8_t *ct, uint8_t *mic, size_t mic_len);
int mesh_aes_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13],
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *ct, size_t len,
                         uint8_t *pt, const uint8_t *mic, size_t mic_len);

/* Key derivation functions. */
void    mesh_s1(const uint8_t *m, size_t len, uint8_t out[16]);
void    mesh_k1(const uint8_t *n, size_t n_len, const uint8_t salt[16],
                const uint8_t *p, size_t p_len, uint8_t out[16]);
void    mesh_k2(const uint8_t netkey[16], const uint8_t *p, size_t p_len,
                uint8_t *nid, uint8_t enc_key[16], uint8_t priv_key[16]);
void    mesh_k3(const uint8_t netkey[16], uint8_t network_id[8]);
uint8_t mesh_k4(const uint8_t appkey[16]);

/* Derived keys used by beacons and proxy advertising. */
void mesh_identity_key(const uint8_t netkey[16], uint8_t out[16]);
void mesh_beacon_key(const uint8_t netkey[16], uint8_t out[16]);

/* Random bytes (esp_fill_random on target, /dev/urandom on host). */
void mesh_random(uint8_t *buf, size_t len);

/*
 * ECDH P-256 for provisioning.  Public keys are raw X||Y (64 bytes), the shared
 * secret is the 32-byte X coordinate.  Not available in the host test build.
 */
typedef struct mesh_ecdh mesh_ecdh_t;
mesh_ecdh_t *mesh_ecdh_new(void);
int  mesh_ecdh_public_key(mesh_ecdh_t *e, uint8_t pub[64]);
int  mesh_ecdh_shared_secret(mesh_ecdh_t *e, const uint8_t peer_pub[64], uint8_t secret[32]);
void mesh_ecdh_free(mesh_ecdh_t *e);

#ifdef __cplusplus
}
#endif
