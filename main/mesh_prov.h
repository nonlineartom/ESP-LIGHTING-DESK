/*
 * PB-GATT provisioner (Mesh Profile 1.0.1 section 5): No-OOB authentication,
 * FIPS P-256 ECDH.  Drives the provisioning protocol over a bearer that
 * transports Provisioning PDUs (proxy PDU type 0x03).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MESH_PROV_IDLE = 0,
    MESH_PROV_WAIT_CAPABILITIES,
    MESH_PROV_WAIT_PUBKEY,
    MESH_PROV_WAIT_CONFIRMATION,
    MESH_PROV_WAIT_RANDOM,
    MESH_PROV_WAIT_COMPLETE,
    MESH_PROV_COMPLETE,
    MESH_PROV_FAILED,
} mesh_prov_state_t;

typedef void (*mesh_prov_send_fn)(void *ctx, const uint8_t *pdu, size_t len);

typedef struct {
    mesh_prov_state_t state;
    uint8_t  error;                 /* provisioning error code or local reason */
    const char *error_text;
    mesh_prov_send_fn send;
    void *ctx;

    /* network data handed to the device */
    uint8_t  netkey[16];
    uint16_t net_idx;
    uint8_t  flags;
    uint32_t iv_index;
    uint16_t unicast;

    /* device capabilities */
    uint8_t  num_elements;
    uint16_t algorithms;
    uint8_t  pubkey_type;
    uint8_t  static_oob;
    uint8_t  out_oob_size;
    uint16_t out_oob_actions;
    uint8_t  in_oob_size;
    uint16_t in_oob_actions;

    /* crypto state */
    mesh_ecdh_t *ecdh;
    uint8_t  conf_inputs[1 + 11 + 5 + 64 + 64];
    uint8_t  pub_local[64];
    uint8_t  pub_device[64];
    uint8_t  secret[32];
    uint8_t  conf_salt[16];
    uint8_t  conf_key[16];
    uint8_t  rand_local[16];
    uint8_t  rand_device[16];
    uint8_t  conf_device[16];
    uint8_t  dev_key[16];

    uint32_t age_ticks;
} mesh_prov_t;

/* Begin provisioning: sends the Provisioning Invite. */
int  mesh_prov_start(mesh_prov_t *p, mesh_prov_send_fn send, void *ctx,
                     const uint8_t netkey[16], uint16_t net_idx, uint32_t iv_index,
                     uint16_t unicast);

/* Feed a received Provisioning PDU.  Returns 0 while in progress, 1 when
 * complete (dev_key / num_elements are valid), -1 on failure. */
int  mesh_prov_rx(mesh_prov_t *p, const uint8_t *pdu, size_t len);

/* Call every ~400 ms; fails the session after the 60 s provisioning timeout. */
void mesh_prov_tick(mesh_prov_t *p);

void mesh_prov_abort(mesh_prov_t *p);

/* Exposed for unit tests: derive the session material once secret/randoms are set. */
void mesh_prov_derive_session(mesh_prov_t *p, uint8_t session_key[16], uint8_t session_nonce[13]);

#ifdef __cplusplus
}
#endif
