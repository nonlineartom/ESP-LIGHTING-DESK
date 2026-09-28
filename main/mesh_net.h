/*
 * Minimal Bluetooth Mesh network + lower/upper transport layers for a
 * GATT proxy client.  Enough of the Mesh Profile 1.0.1 to act as the
 * provisioner/controller of a small network of Telink-based lights:
 *
 *   - network PDU encrypt/obfuscate and decrypt (own NetKey only)
 *   - unsegmented + segmented access messages, TX with SegAck handling,
 *     RX reassembly with SegAck
 *   - AppKey and DevKey upper-transport encryption
 *   - proxy configuration PDUs, Secure Network Beacon parsing
 *   - proxy advertising identification (Network ID, Node Identity)
 *
 * Single-threaded: all calls must come from the same task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MESH_ADDR_UNASSIGNED   0x0000
#define MESH_ADDR_ALL_NODES    0xFFFF
#define MESH_MAX_ACCESS_LEN    380
#define MESH_SEG_MAX           32      /* max segments per message (SegN is 5 bits) */
#define MESH_SEG_RX_SLOTS      2
#define MESH_RPL_SIZE          16

/* Proxy PDU message types (Mesh Profile 6.3.1) */
#define MESH_PROXY_TYPE_NETWORK       0x00
#define MESH_PROXY_TYPE_BEACON        0x01
#define MESH_PROXY_TYPE_PROXY_CFG     0x02
#define MESH_PROXY_TYPE_PROVISIONING  0x03

/* Proxy configuration opcodes (6.5) */
#define MESH_PROXY_CFG_SET_FILTER_TYPE 0x00
#define MESH_PROXY_CFG_ADD_ADDRESSES   0x01
#define MESH_PROXY_CFG_REMOVE_ADDRESSES 0x02
#define MESH_PROXY_CFG_FILTER_STATUS   0x03
#define MESH_PROXY_FILTER_WHITELIST    0x00
#define MESH_PROXY_FILTER_BLACKLIST    0x01

typedef struct mesh_net mesh_net_t;

/* Send one network PDU (without proxy header) over the bearer. */
typedef void (*mesh_net_tx_fn)(void *ctx, const uint8_t *net_pdu, size_t len);
/* A decrypted access PDU arrived. */
typedef void (*mesh_net_access_rx_fn)(void *ctx, uint16_t src, uint16_t dst,
                                      bool devkey, const uint8_t *access, size_t len);
/* Return the DevKey of the node whose element address is `src`, or NULL. */
typedef const uint8_t *(*mesh_net_devkey_lookup_fn)(void *ctx, uint16_t src);
/* A segmented transmission finished (acknowledged) or gave up. */
typedef void (*mesh_net_seg_done_fn)(void *ctx, bool success);

typedef struct {
    mesh_net_tx_fn            tx;
    mesh_net_access_rx_fn     access_rx;
    mesh_net_devkey_lookup_fn devkey_lookup;
    mesh_net_seg_done_fn      seg_done;
    void                     *ctx;
} mesh_net_callbacks_t;

struct mesh_net {
    uint8_t  netkey[16];
    uint8_t  appkey[16];
    uint8_t  nid;
    uint8_t  enc_key[16];
    uint8_t  priv_key[16];
    uint8_t  aid;
    uint8_t  network_id[8];
    uint8_t  identity_key[16];
    uint8_t  beacon_key[16];
    uint32_t iv_index;
    uint32_t seq;           /* next sequence number to use */
    uint16_t own_addr;
    uint8_t  default_ttl;
    mesh_net_callbacks_t cb;

    struct {
        bool     active;
        uint16_t dst;
        uint32_t seq_zero;
        uint8_t  hdr;              /* first octet of every segment (SEG|AKF|AID) */
        uint8_t  szmic;
        uint8_t  seg_n;
        uint8_t  ttl;
        uint8_t  data[MESH_SEG_MAX][12];
        uint8_t  last_len;
        uint32_t ack_mask;
        uint8_t  attempts;
    } seg_tx;

    struct {
        bool     active;
        bool     complete;
        uint16_t src, dst;
        uint32_t seq_auth;
        uint32_t iv_index;
        uint8_t  hdr;
        uint8_t  szmic;
        uint8_t  seg_n;
        uint8_t  ttl;
        uint32_t mask;
        uint8_t  data[MESH_SEG_MAX * 12];
        uint8_t  last_len;
        uint32_t age_ticks;
    } seg_rx[MESH_SEG_RX_SLOTS];

    struct {
        uint16_t src;
        uint32_t seq;
    } rpl[MESH_RPL_SIZE];
    int rpl_count;
};

void mesh_net_init(mesh_net_t *n, const uint8_t netkey[16], const uint8_t appkey[16],
                   uint32_t iv_index, uint32_t seq, uint16_t own_addr,
                   const mesh_net_callbacks_t *cb);

/* Allocate the next outgoing sequence number. */
uint32_t mesh_net_next_seq(mesh_net_t *n);

/*
 * Send an access-layer message.  devkey == NULL selects the AppKey.
 * Returns 0 when sent unsegmented, 1 when a segmented transfer is now in
 * flight (completion reported through seg_done), -1 on error / busy.
 */
int mesh_net_send_access(mesh_net_t *n, uint16_t dst, uint8_t ttl, const uint8_t *devkey,
                         const uint8_t *access, size_t len);

bool mesh_net_seg_tx_busy(const mesh_net_t *n);
void mesh_net_seg_tx_abort(mesh_net_t *n);

/* Call periodically (~every 400 ms): drives segment retransmission and
 * reassembly slot expiry. */
void mesh_net_tick(mesh_net_t *n);

/* Feed a received proxy PDU payload of the given type. */
void mesh_net_recv(mesh_net_t *n, const uint8_t *net_pdu, size_t len);
void mesh_net_recv_beacon(mesh_net_t *n, const uint8_t *pdu, size_t len);

/* Proxy configuration messages (proxy PDU type 0x02). */
int  mesh_net_build_proxy_cfg(mesh_net_t *n, uint8_t opcode, const uint8_t *params, size_t plen,
                              uint8_t *out, size_t out_cap);
int  mesh_net_decode_proxy_cfg(mesh_net_t *n, const uint8_t *pdu, size_t len,
                               uint8_t *opcode, uint8_t *params, size_t *plen);

/* Proxy advertising identification. */
bool mesh_net_match_network_id(const mesh_net_t *n, const uint8_t id[8]);
bool mesh_net_match_node_identity(const mesh_net_t *n, const uint8_t hash[8],
                                  const uint8_t random[8], uint16_t addr);

/* Low-level helpers exposed for unit tests. */
int  mesh_net_encode(mesh_net_t *n, uint8_t ctl, uint8_t ttl, uint32_t seq, uint16_t src,
                     uint16_t dst, const uint8_t *tpdu, size_t tlen, bool proxy_nonce,
                     uint8_t *out, size_t out_cap);
int  mesh_net_decode(mesh_net_t *n, const uint8_t *pdu, size_t len, bool proxy_nonce,
                     uint8_t *ctl, uint8_t *ttl, uint32_t *seq, uint16_t *src, uint16_t *dst,
                     uint8_t *tpdu, size_t *tlen, uint32_t *iv_used);

#ifdef __cplusplus
}
#endif
