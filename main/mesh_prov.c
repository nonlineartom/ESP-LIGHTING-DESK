#include "mesh_prov.h"

#include <string.h>
#include <stdio.h>

#ifdef MESHDECK_HOST_TEST
#define LOGI(fmt, ...) printf("[prov] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) printf("[prov] WARN " fmt "\n", ##__VA_ARGS__)
#else
#include "esp_log.h"
static const char *TAG = "mesh_prov";
#define LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#endif

#define PDU_INVITE          0x00
#define PDU_CAPABILITIES    0x01
#define PDU_START           0x02
#define PDU_PUBLIC_KEY      0x03
#define PDU_INPUT_COMPLETE  0x04
#define PDU_CONFIRMATION    0x05
#define PDU_RANDOM          0x06
#define PDU_DATA            0x07
#define PDU_COMPLETE        0x08
#define PDU_FAILED          0x09

#define PROV_TIMEOUT_TICKS  150   /* 60 s at 400 ms */

static const char *err_text(uint8_t code)
{
    switch (code) {
    case 0x01: return "invalid PDU";
    case 0x02: return "invalid format";
    case 0x03: return "unexpected PDU";
    case 0x04: return "confirmation failed";
    case 0x05: return "out of resources";
    case 0x06: return "decryption failed";
    case 0x07: return "unexpected error";
    case 0x08: return "cannot assign addresses";
    default:   return "failed";
    }
}

static void fail(mesh_prov_t *p, uint8_t code, const char *text)
{
    p->state = MESH_PROV_FAILED;
    p->error = code;
    p->error_text = text ? text : err_text(code);
    if (p->ecdh) {
        mesh_ecdh_free(p->ecdh);
        p->ecdh = NULL;
    }
    LOGW("provisioning failed: %s", p->error_text);
}

static void send_pdu(mesh_prov_t *p, uint8_t type, const uint8_t *params, size_t plen)
{
    uint8_t buf[1 + 64];
    buf[0] = type;
    if (plen) memcpy(buf + 1, params, plen);
    if (p->send) p->send(p->ctx, buf, 1 + plen);
}

int mesh_prov_start(mesh_prov_t *p, mesh_prov_send_fn send, void *ctx,
                    const uint8_t netkey[16], uint16_t net_idx, uint32_t iv_index,
                    uint16_t unicast)
{
    memset(p, 0, sizeof(*p));
    p->send = send;
    p->ctx = ctx;
    memcpy(p->netkey, netkey, 16);
    p->net_idx = net_idx;
    p->iv_index = iv_index;
    p->unicast = unicast;
    p->flags = 0;

    p->ecdh = mesh_ecdh_new();
    if (!p->ecdh || mesh_ecdh_public_key(p->ecdh, p->pub_local) != 0) {
        fail(p, 0x07, "ECDH key generation failed");
        return -1;
    }
    mesh_random(p->rand_local, 16);

    uint8_t attention = 0x00;
    p->conf_inputs[0] = attention;
    p->state = MESH_PROV_WAIT_CAPABILITIES;
    send_pdu(p, PDU_INVITE, &attention, 1);
    LOGI("invite sent (unicast 0x%04x)", unicast);
    return 0;
}

void mesh_prov_abort(mesh_prov_t *p)
{
    if (p->ecdh) {
        mesh_ecdh_free(p->ecdh);
        p->ecdh = NULL;
    }
    p->state = MESH_PROV_IDLE;
}

void mesh_prov_tick(mesh_prov_t *p)
{
    if (p->state == MESH_PROV_IDLE || p->state == MESH_PROV_COMPLETE || p->state == MESH_PROV_FAILED) {
        return;
    }
    if (++p->age_ticks > PROV_TIMEOUT_TICKS) fail(p, 0x07, "timeout");
}

void mesh_prov_derive_session(mesh_prov_t *p, uint8_t session_key[16], uint8_t session_nonce[13])
{
    uint8_t salt_in[48], prov_salt[16], tmp[16];
    memcpy(salt_in, p->conf_salt, 16);
    memcpy(salt_in + 16, p->rand_local, 16);
    memcpy(salt_in + 32, p->rand_device, 16);
    mesh_s1(salt_in, 48, prov_salt);
    mesh_k1(p->secret, 32, prov_salt, (const uint8_t *)"prsk", 4, session_key);
    mesh_k1(p->secret, 32, prov_salt, (const uint8_t *)"prsn", 4, tmp);
    memcpy(session_nonce, tmp + 3, 13);
    mesh_k1(p->secret, 32, prov_salt, (const uint8_t *)"prdk", 4, p->dev_key);
}

static void on_capabilities(mesh_prov_t *p, const uint8_t *d, size_t len)
{
    if (len != 11) {
        fail(p, 0x02, NULL);
        return;
    }
    p->num_elements    = d[0];
    p->algorithms      = (uint16_t)((d[1] << 8) | d[2]);
    p->pubkey_type     = d[3];
    p->static_oob      = d[4];
    p->out_oob_size    = d[5];
    p->out_oob_actions = (uint16_t)((d[6] << 8) | d[7]);
    p->in_oob_size     = d[8];
    p->in_oob_actions  = (uint16_t)((d[9] << 8) | d[10]);
    LOGI("capabilities: elements=%u algorithms=0x%04x pubkey=%u static_oob=%u",
         p->num_elements, p->algorithms, p->pubkey_type, p->static_oob);

    if (p->num_elements == 0) {
        fail(p, 0x02, "device reports zero elements");
        return;
    }
    if (!(p->algorithms & 0x0001)) {
        fail(p, 0x02, "device lacks FIPS P-256");
        return;
    }
    memcpy(p->conf_inputs + 1, d, 11);

    /* Start: algorithm 0 (P-256), no OOB public key, no OOB auth */
    uint8_t start[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
    memcpy(p->conf_inputs + 12, start, 5);
    send_pdu(p, PDU_START, start, 5);
    memcpy(p->conf_inputs + 17, p->pub_local, 64);
    send_pdu(p, PDU_PUBLIC_KEY, p->pub_local, 64);
    p->state = MESH_PROV_WAIT_PUBKEY;
}

static void on_public_key(mesh_prov_t *p, const uint8_t *d, size_t len)
{
    if (len != 64) {
        fail(p, 0x02, NULL);
        return;
    }
    memcpy(p->pub_device, d, 64);
    memcpy(p->conf_inputs + 81, d, 64);
    if (mesh_ecdh_shared_secret(p->ecdh, p->pub_device, p->secret) != 0) {
        fail(p, 0x02, "invalid device public key");
        return;
    }
    mesh_ecdh_free(p->ecdh);
    p->ecdh = NULL;

    mesh_s1(p->conf_inputs, sizeof(p->conf_inputs), p->conf_salt);
    mesh_k1(p->secret, 32, p->conf_salt, (const uint8_t *)"prck", 4, p->conf_key);

    uint8_t msg[32] = { 0 };     /* Random || AuthValue (16 zero bytes for no-OOB) */
    memcpy(msg, p->rand_local, 16);
    uint8_t conf[16];
    mesh_aes_cmac(p->conf_key, msg, 32, conf);
    send_pdu(p, PDU_CONFIRMATION, conf, 16);
    p->state = MESH_PROV_WAIT_CONFIRMATION;
}

static void on_confirmation(mesh_prov_t *p, const uint8_t *d, size_t len)
{
    if (len != 16) {
        fail(p, 0x02, NULL);
        return;
    }
    memcpy(p->conf_device, d, 16);
    send_pdu(p, PDU_RANDOM, p->rand_local, 16);
    p->state = MESH_PROV_WAIT_RANDOM;
}

static void on_random(mesh_prov_t *p, const uint8_t *d, size_t len)
{
    if (len != 16) {
        fail(p, 0x02, NULL);
        return;
    }
    memcpy(p->rand_device, d, 16);

    uint8_t msg[32] = { 0 }, expect[16];
    memcpy(msg, p->rand_device, 16);
    mesh_aes_cmac(p->conf_key, msg, 32, expect);
    if (memcmp(expect, p->conf_device, 16) != 0) {
        fail(p, 0x04, NULL);
        return;
    }

    uint8_t session_key[16], nonce[13];
    mesh_prov_derive_session(p, session_key, nonce);

    uint8_t data[25];
    memcpy(data, p->netkey, 16);
    data[16] = (uint8_t)(p->net_idx >> 8);
    data[17] = (uint8_t)p->net_idx;
    data[18] = p->flags;
    data[19] = (uint8_t)(p->iv_index >> 24);
    data[20] = (uint8_t)(p->iv_index >> 16);
    data[21] = (uint8_t)(p->iv_index >> 8);
    data[22] = (uint8_t)p->iv_index;
    data[23] = (uint8_t)(p->unicast >> 8);
    data[24] = (uint8_t)p->unicast;

    uint8_t enc[25 + 8];
    mesh_aes_ccm_encrypt(session_key, nonce, NULL, 0, data, 25, enc, enc + 25, 8);
    send_pdu(p, PDU_DATA, enc, sizeof(enc));
    p->state = MESH_PROV_WAIT_COMPLETE;
}

int mesh_prov_rx(mesh_prov_t *p, const uint8_t *pdu, size_t len)
{
    if (len < 1) return 0;
    uint8_t type = pdu[0] & 0x3f;
    const uint8_t *d = pdu + 1;
    size_t dlen = len - 1;

    if (type == PDU_FAILED) {
        fail(p, dlen ? d[0] : 0x07, NULL);
        return -1;
    }

    switch (p->state) {
    case MESH_PROV_WAIT_CAPABILITIES:
        if (type != PDU_CAPABILITIES) { fail(p, 0x03, NULL); return -1; }
        on_capabilities(p, d, dlen);
        break;
    case MESH_PROV_WAIT_PUBKEY:
        if (type != PDU_PUBLIC_KEY) { fail(p, 0x03, NULL); return -1; }
        on_public_key(p, d, dlen);
        break;
    case MESH_PROV_WAIT_CONFIRMATION:
        if (type != PDU_CONFIRMATION) { fail(p, 0x03, NULL); return -1; }
        on_confirmation(p, d, dlen);
        break;
    case MESH_PROV_WAIT_RANDOM:
        if (type != PDU_RANDOM) { fail(p, 0x03, NULL); return -1; }
        on_random(p, d, dlen);
        break;
    case MESH_PROV_WAIT_COMPLETE:
        if (type != PDU_COMPLETE) { fail(p, 0x03, NULL); return -1; }
        p->state = MESH_PROV_COMPLETE;
        LOGI("provisioning complete: unicast 0x%04x, %u element(s)", p->unicast, p->num_elements);
        return 1;
    default:
        return 0;
    }
    return (p->state == MESH_PROV_FAILED) ? -1 : 0;
}
