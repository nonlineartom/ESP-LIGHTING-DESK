#include "mesh_net.h"
#include "mesh_crypto.h"

#include <string.h>
#include <stdio.h>

#ifdef MESHDECK_HOST_TEST
#define LOGI(fmt, ...) printf("[net] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) printf("[net] WARN " fmt "\n", ##__VA_ARGS__)
#else
#include "esp_log.h"
static const char *TAG = "mesh_net";
#define LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#endif

#define SEG_TX_MAX_ATTEMPTS  5
#define SEG_RX_EXPIRE_TICKS  25   /* ~10 s at a 400 ms tick */

/* ------------------------------------------------------------------------- */
/* init                                                                       */
/* ------------------------------------------------------------------------- */

void mesh_net_init(mesh_net_t *n, const uint8_t netkey[16], const uint8_t appkey[16],
                   uint32_t iv_index, uint32_t seq, uint16_t own_addr,
                   const mesh_net_callbacks_t *cb)
{
    memset(n, 0, sizeof(*n));
    memcpy(n->netkey, netkey, 16);
    memcpy(n->appkey, appkey, 16);
    static const uint8_t p_master = 0x00;
    mesh_k2(netkey, &p_master, 1, &n->nid, n->enc_key, n->priv_key);
    n->aid = mesh_k4(appkey);
    mesh_k3(netkey, n->network_id);
    mesh_identity_key(netkey, n->identity_key);
    mesh_beacon_key(netkey, n->beacon_key);
    n->iv_index = iv_index;
    n->seq = seq & 0xffffff;
    n->own_addr = own_addr;
    n->default_ttl = 7;
    if (cb) n->cb = *cb;
}

uint32_t mesh_net_next_seq(mesh_net_t *n)
{
    uint32_t s = n->seq;
    n->seq = (n->seq + 1) & 0xffffff;
    return s;
}

/* ------------------------------------------------------------------------- */
/* nonces                                                                     */
/* ------------------------------------------------------------------------- */

static void make_nonce(uint8_t type, uint8_t b1, uint32_t seq, uint16_t src, uint16_t dst,
                       uint32_t iv, uint8_t out[13])
{
    out[0] = type;
    out[1] = b1;
    out[2] = (uint8_t)(seq >> 16);
    out[3] = (uint8_t)(seq >> 8);
    out[4] = (uint8_t)seq;
    out[5] = (uint8_t)(src >> 8);
    out[6] = (uint8_t)src;
    out[7] = (uint8_t)(dst >> 8);
    out[8] = (uint8_t)dst;
    out[9]  = (uint8_t)(iv >> 24);
    out[10] = (uint8_t)(iv >> 16);
    out[11] = (uint8_t)(iv >> 8);
    out[12] = (uint8_t)iv;
}

static void pecb(const mesh_net_t *n, uint32_t iv, const uint8_t privacy_random[7], uint8_t out[16])
{
    uint8_t in[16] = { 0 };
    in[5] = (uint8_t)(iv >> 24);
    in[6] = (uint8_t)(iv >> 16);
    in[7] = (uint8_t)(iv >> 8);
    in[8] = (uint8_t)iv;
    memcpy(in + 9, privacy_random, 7);
    mesh_aes128_encrypt(n->priv_key, in, out);
}

/* ------------------------------------------------------------------------- */
/* network PDU encode / decode                                                */
/* ------------------------------------------------------------------------- */

int mesh_net_encode(mesh_net_t *n, uint8_t ctl, uint8_t ttl, uint32_t seq, uint16_t src,
                    uint16_t dst, const uint8_t *tpdu, size_t tlen, bool proxy_nonce,
                    uint8_t *out, size_t out_cap)
{
    size_t mic_len = ctl ? 8 : 4;
    size_t total = 1 + 6 + 2 + tlen + mic_len;
    if (tlen > 16 || out_cap < total) return -1;

    uint8_t nonce[13];
    if (proxy_nonce) {
        make_nonce(0x03, 0x00, seq, src, 0x0000, n->iv_index, nonce);
    } else {
        make_nonce(0x00, (uint8_t)((ctl << 7) | (ttl & 0x7f)), seq, src, 0x0000, n->iv_index, nonce);
    }

    uint8_t plain[2 + 16];
    plain[0] = (uint8_t)(dst >> 8);
    plain[1] = (uint8_t)dst;
    memcpy(plain + 2, tpdu, tlen);

    uint8_t *enc = out + 7;
    mesh_aes_ccm_encrypt(n->enc_key, nonce, NULL, 0, plain, 2 + tlen, enc, enc + 2 + tlen, mic_len);

    uint8_t hdr[6] = {
        (uint8_t)((ctl << 7) | (ttl & 0x7f)),
        (uint8_t)(seq >> 16), (uint8_t)(seq >> 8), (uint8_t)seq,
        (uint8_t)(src >> 8), (uint8_t)src,
    };
    uint8_t p[16];
    pecb(n, n->iv_index, enc, p);
    out[0] = (uint8_t)(((n->iv_index & 1) << 7) | (n->nid & 0x7f));
    for (int i = 0; i < 6; i++) out[1 + i] = hdr[i] ^ p[i];
    return (int)total;
}

int mesh_net_decode(mesh_net_t *n, const uint8_t *pdu, size_t len, bool proxy_nonce,
                    uint8_t *ctl, uint8_t *ttl, uint32_t *seq, uint16_t *src, uint16_t *dst,
                    uint8_t *tpdu, size_t *tlen, uint32_t *iv_used)
{
    if (len < 1 + 6 + 2 + 1 + 4) return -1;
    if ((pdu[0] & 0x7f) != n->nid) return -1;

    uint8_t ivi = pdu[0] >> 7;
    uint32_t iv = n->iv_index;
    if ((iv & 1) != ivi) {
        if (iv == 0) return -1;
        iv--;
    }

    uint8_t p[16];
    pecb(n, iv, pdu + 7, p);
    uint8_t hdr[6];
    for (int i = 0; i < 6; i++) hdr[i] = pdu[1 + i] ^ p[i];

    uint8_t  c = hdr[0] >> 7;
    uint8_t  t = hdr[0] & 0x7f;
    uint32_t s = ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
    uint16_t sr = (uint16_t)((hdr[4] << 8) | hdr[5]);
    size_t mic_len = c ? 8 : 4;
    if (len < 7 + 2 + mic_len) return -1;
    size_t enc_len = len - 7 - mic_len;
    if (enc_len > 2 + 16) return -1;

    uint8_t nonce[13];
    if (proxy_nonce) make_nonce(0x03, 0x00, s, sr, 0, iv, nonce);
    else             make_nonce(0x00, hdr[0], s, sr, 0, iv, nonce);

    uint8_t plain[2 + 16];
    if (mesh_aes_ccm_decrypt(n->enc_key, nonce, NULL, 0, pdu + 7, enc_len, plain,
                             pdu + 7 + enc_len, mic_len) != 0) {
        return -1;
    }
    *ctl = c;
    *ttl = t;
    *seq = s;
    *src = sr;
    *dst = (uint16_t)((plain[0] << 8) | plain[1]);
    *tlen = enc_len - 2;
    memcpy(tpdu, plain + 2, *tlen);
    if (iv_used) *iv_used = iv;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* replay protection                                                          */
/* ------------------------------------------------------------------------- */

static bool rpl_check_and_update(mesh_net_t *n, uint16_t src, uint32_t seq)
{
    for (int i = 0; i < n->rpl_count; i++) {
        if (n->rpl[i].src == src) {
            if (seq <= n->rpl[i].seq) return false;
            n->rpl[i].seq = seq;
            return true;
        }
    }
    if (n->rpl_count < MESH_RPL_SIZE) {
        n->rpl[n->rpl_count].src = src;
        n->rpl[n->rpl_count].seq = seq;
        n->rpl_count++;
    } else {
        /* evict slot 0 (oldest) */
        memmove(&n->rpl[0], &n->rpl[1], sizeof(n->rpl[0]) * (MESH_RPL_SIZE - 1));
        n->rpl[MESH_RPL_SIZE - 1].src = src;
        n->rpl[MESH_RPL_SIZE - 1].seq = seq;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* transmit                                                                   */
/* ------------------------------------------------------------------------- */

static void tx_net(mesh_net_t *n, uint8_t ctl, uint8_t ttl, uint32_t seq, uint16_t dst,
                   const uint8_t *tpdu, size_t tlen)
{
    uint8_t pdu[32];
    int len = mesh_net_encode(n, ctl, ttl, seq, n->own_addr, dst, tpdu, tlen, false, pdu, sizeof(pdu));
    if (len > 0 && n->cb.tx) n->cb.tx(n->cb.ctx, pdu, (size_t)len);
}

static void seg_tx_send(mesh_net_t *n, uint8_t seg_o)
{
    uint8_t lower[1 + 3 + 12];
    size_t dlen = (seg_o == n->seg_tx.seg_n) ? n->seg_tx.last_len : 12;
    lower[0] = n->seg_tx.hdr;
    lower[1] = (uint8_t)((n->seg_tx.szmic << 7) | ((n->seg_tx.seq_zero >> 6) & 0x7f));
    lower[2] = (uint8_t)(((n->seg_tx.seq_zero & 0x3f) << 2) | ((seg_o >> 3) & 0x03));
    lower[3] = (uint8_t)(((seg_o & 0x07) << 5) | (n->seg_tx.seg_n & 0x1f));
    memcpy(lower + 4, n->seg_tx.data[seg_o], dlen);
    tx_net(n, 0, n->seg_tx.ttl, mesh_net_next_seq(n), n->seg_tx.dst, lower, 4 + dlen);
}

int mesh_net_send_access(mesh_net_t *n, uint16_t dst, uint8_t ttl, const uint8_t *devkey,
                         const uint8_t *access, size_t len)
{
    if (len == 0 || len > MESH_MAX_ACCESS_LEN) return -1;
    if (ttl == 0xff) ttl = n->default_ttl;

    uint8_t upper[MESH_MAX_ACCESS_LEN + 4];
    uint32_t seq_auth = mesh_net_next_seq(n);

    uint8_t nonce[13];
    const uint8_t *key;
    uint8_t akf, aid;
    if (devkey) {
        key = devkey; akf = 0; aid = 0;
        make_nonce(0x02, 0x00, seq_auth, n->own_addr, dst, n->iv_index, nonce);
    } else {
        key = n->appkey; akf = 1; aid = n->aid;
        make_nonce(0x01, 0x00, seq_auth, n->own_addr, dst, n->iv_index, nonce);
    }
    mesh_aes_ccm_encrypt(key, nonce, NULL, 0, access, len, upper, upper + len, 4);
    size_t ulen = len + 4;

    if (ulen <= 15) {
        uint8_t lower[16];
        lower[0] = (uint8_t)((akf << 6) | (aid & 0x3f));
        memcpy(lower + 1, upper, ulen);
        tx_net(n, 0, ttl, seq_auth, dst, lower, 1 + ulen);
        return 0;
    }

    if (n->seg_tx.active) return -1;
    size_t nseg = (ulen + 11) / 12;
    if (nseg > MESH_SEG_MAX) return -1;

    memset(&n->seg_tx, 0, sizeof(n->seg_tx));
    n->seg_tx.active = true;
    n->seg_tx.dst = dst;
    n->seg_tx.ttl = ttl;
    n->seg_tx.seq_zero = seq_auth & 0x1fff;
    n->seg_tx.hdr = (uint8_t)(0x80 | (akf << 6) | (aid & 0x3f));
    n->seg_tx.szmic = 0;
    n->seg_tx.seg_n = (uint8_t)(nseg - 1);
    for (size_t i = 0; i < nseg; i++) {
        size_t off = i * 12;
        size_t take = (ulen - off) < 12 ? (ulen - off) : 12;
        memcpy(n->seg_tx.data[i], upper + off, take);
        if (i == nseg - 1) n->seg_tx.last_len = (uint8_t)take;
    }
    n->seg_tx.attempts = 1;
    /* Segment 0 is sent with seq_auth (already allocated); the others take fresh
     * sequence numbers. */
    {
        uint8_t lower[1 + 3 + 12];
        lower[0] = n->seg_tx.hdr;
        lower[1] = (uint8_t)((n->seg_tx.seq_zero >> 6) & 0x7f);
        lower[2] = (uint8_t)((n->seg_tx.seq_zero & 0x3f) << 2);
        lower[3] = (uint8_t)(n->seg_tx.seg_n & 0x1f);
        size_t dlen = (n->seg_tx.seg_n == 0) ? n->seg_tx.last_len : 12;
        memcpy(lower + 4, n->seg_tx.data[0], dlen);
        tx_net(n, 0, ttl, seq_auth, dst, lower, 4 + dlen);
    }
    for (uint8_t i = 1; i <= n->seg_tx.seg_n; i++) seg_tx_send(n, i);
    return 1;
}

bool mesh_net_seg_tx_busy(const mesh_net_t *n) { return n->seg_tx.active; }

void mesh_net_seg_tx_abort(mesh_net_t *n) { n->seg_tx.active = false; }

static void seg_tx_finish(mesh_net_t *n, bool ok)
{
    n->seg_tx.active = false;
    if (n->cb.seg_done) n->cb.seg_done(n->cb.ctx, ok);
}

static void handle_seg_ack(mesh_net_t *n, uint16_t src, const uint8_t *params, size_t plen)
{
    if (plen < 6 || !n->seg_tx.active || src != n->seg_tx.dst) return;
    uint32_t seq_zero = (uint32_t)(((params[0] & 0x7f) << 6) | (params[1] >> 2));
    if (seq_zero != n->seg_tx.seq_zero) return;
    uint32_t block = ((uint32_t)params[2] << 24) | ((uint32_t)params[3] << 16) |
                     ((uint32_t)params[4] << 8) | params[5];
    if (block == 0) {
        LOGW("seg tx cancelled by peer");
        seg_tx_finish(n, false);
        return;
    }
    n->seg_tx.ack_mask |= block;
    uint32_t all = (n->seg_tx.seg_n >= 31) ? 0xffffffffu : ((1u << (n->seg_tx.seg_n + 1)) - 1);
    if ((n->seg_tx.ack_mask & all) == all) {
        seg_tx_finish(n, true);
    } else {
        /* resend immediately what the peer says is missing */
        for (uint8_t i = 0; i <= n->seg_tx.seg_n; i++) {
            if (!(n->seg_tx.ack_mask & (1u << i))) seg_tx_send(n, i);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* receive                                                                    */
/* ------------------------------------------------------------------------- */

static void deliver_upper(mesh_net_t *n, uint16_t src, uint16_t dst, uint8_t hdr, uint8_t szmic,
                          uint32_t seq_auth, uint32_t iv, const uint8_t *upper, size_t ulen)
{
    size_t mic_len = szmic ? 8 : 4;
    if (ulen <= mic_len || ulen - mic_len > MESH_MAX_ACCESS_LEN) return;
    size_t alen = ulen - mic_len;
    bool akf = (hdr >> 6) & 1;
    uint8_t aid = hdr & 0x3f;

    const uint8_t *key;
    uint8_t nonce[13];
    if (akf) {
        if (aid != n->aid) return;
        key = n->appkey;
        make_nonce(0x01, (uint8_t)(szmic << 7), seq_auth, src, dst, iv, nonce);
    } else {
        key = n->cb.devkey_lookup ? n->cb.devkey_lookup(n->cb.ctx, src) : NULL;
        if (!key) return;
        make_nonce(0x02, (uint8_t)(szmic << 7), seq_auth, src, dst, iv, nonce);
    }
    uint8_t access[MESH_MAX_ACCESS_LEN];
    if (mesh_aes_ccm_decrypt(key, nonce, NULL, 0, upper, alen, access, upper + alen, mic_len) != 0) {
        LOGW("upper transport MIC failed src=0x%04x akf=%d", src, akf);
        return;
    }
    if (n->cb.access_rx) n->cb.access_rx(n->cb.ctx, src, dst, !akf, access, alen);
}

static void send_seg_ack(mesh_net_t *n, uint16_t dst, uint32_t seq_zero, uint32_t mask, uint8_t ttl)
{
    uint8_t ctl_pdu[1 + 6];
    ctl_pdu[0] = 0x00;                                   /* SEG=0, opcode 0x00 = SegAck */
    ctl_pdu[1] = (uint8_t)((seq_zero >> 6) & 0x7f);      /* OBO=0 */
    ctl_pdu[2] = (uint8_t)((seq_zero & 0x3f) << 2);
    ctl_pdu[3] = (uint8_t)(mask >> 24);
    ctl_pdu[4] = (uint8_t)(mask >> 16);
    ctl_pdu[5] = (uint8_t)(mask >> 8);
    ctl_pdu[6] = (uint8_t)mask;
    tx_net(n, 1, ttl ? n->default_ttl : 0, mesh_net_next_seq(n), dst, ctl_pdu, sizeof(ctl_pdu));
}

static void handle_segment(mesh_net_t *n, uint16_t src, uint16_t dst, uint8_t ttl, uint32_t seq,
                           uint32_t iv, const uint8_t *lower, size_t llen)
{
    if (llen < 5) return;
    uint8_t hdr = lower[0] & 0x7f;
    uint8_t szmic = lower[1] >> 7;
    uint32_t seq_zero = (uint32_t)(((lower[1] & 0x7f) << 6) | (lower[2] >> 2));
    uint8_t seg_o = (uint8_t)(((lower[2] & 0x03) << 3) | (lower[3] >> 5));
    uint8_t seg_n = lower[3] & 0x1f;
    size_t dlen = llen - 4;
    if (seg_o > seg_n || dlen > 12) return;

    uint32_t seq_auth = (seq & 0xffe000u) | seq_zero;
    if (seq_auth > seq) seq_auth -= 0x2000;

    /* find or allocate a slot */
    int slot = -1, free_slot = -1;
    for (int i = 0; i < MESH_SEG_RX_SLOTS; i++) {
        if (n->seg_rx[i].active && n->seg_rx[i].src == src && n->seg_rx[i].seq_auth == seq_auth) {
            slot = i;
            break;
        }
        if (!n->seg_rx[i].active && free_slot < 0) free_slot = i;
    }
    if (slot < 0) {
        if (free_slot < 0) {
            /* evict the oldest */
            free_slot = 0;
            for (int i = 1; i < MESH_SEG_RX_SLOTS; i++) {
                if (n->seg_rx[i].age_ticks > n->seg_rx[free_slot].age_ticks) free_slot = i;
            }
        }
        slot = free_slot;
        memset(&n->seg_rx[slot], 0, sizeof(n->seg_rx[slot]));
        n->seg_rx[slot].active = true;
        n->seg_rx[slot].src = src;
        n->seg_rx[slot].dst = dst;
        n->seg_rx[slot].seq_auth = seq_auth;
        n->seg_rx[slot].iv_index = iv;
        n->seg_rx[slot].hdr = hdr;
        n->seg_rx[slot].szmic = szmic;
        n->seg_rx[slot].seg_n = seg_n;
        n->seg_rx[slot].ttl = ttl;
    }
    typeof(n->seg_rx[0]) *rx = &n->seg_rx[slot];
    rx->age_ticks = 0;

    if (rx->complete) {
        /* peer missed our ack — repeat it */
        uint32_t all = (rx->seg_n >= 31) ? 0xffffffffu : ((1u << (rx->seg_n + 1)) - 1);
        send_seg_ack(n, src, seq_zero, all, ttl);
        return;
    }

    memcpy(rx->data + seg_o * 12, lower + 4, dlen);
    if (seg_o == seg_n) rx->last_len = (uint8_t)dlen;
    rx->mask |= (1u << seg_o);

    uint32_t all = (seg_n >= 31) ? 0xffffffffu : ((1u << (seg_n + 1)) - 1);
    if ((rx->mask & all) != all) return;

    rx->complete = true;
    send_seg_ack(n, src, seq_zero, all, ttl);
    size_t ulen = (size_t)seg_n * 12 + rx->last_len;
    deliver_upper(n, src, dst, hdr, szmic, seq_auth, iv, rx->data, ulen);
}

void mesh_net_recv(mesh_net_t *n, const uint8_t *net_pdu, size_t len)
{
    uint8_t ctl, ttl, tpdu[16];
    uint32_t seq, iv;
    uint16_t src, dst;
    size_t tlen;
    if (mesh_net_decode(n, net_pdu, len, false, &ctl, &ttl, &seq, &src, &dst, tpdu, &tlen, &iv) != 0) {
        return;
    }
    if (src == n->own_addr) return;                 /* our own echo */
    if (!rpl_check_and_update(n, src, seq)) return;  /* replay */
    if (tlen < 1) return;

    bool seg = (tpdu[0] & 0x80) != 0;
    if (ctl) {
        if (seg) return;                            /* segmented control: not needed */
        uint8_t opcode = tpdu[0] & 0x7f;
        if (opcode == 0x00) handle_seg_ack(n, src, tpdu + 1, tlen - 1);
        return;
    }
    if (seg) {
        handle_segment(n, src, dst, ttl, seq, iv, tpdu, tlen);
    } else {
        deliver_upper(n, src, dst, tpdu[0], 0, seq, iv, tpdu + 1, tlen - 1);
    }
}

void mesh_net_tick(mesh_net_t *n)
{
    if (n->seg_tx.active) {
        if (n->seg_tx.attempts >= SEG_TX_MAX_ATTEMPTS) {
            LOGW("seg tx to 0x%04x gave up", n->seg_tx.dst);
            seg_tx_finish(n, false);
        } else {
            n->seg_tx.attempts++;
            for (uint8_t i = 0; i <= n->seg_tx.seg_n; i++) {
                if (!(n->seg_tx.ack_mask & (1u << i))) seg_tx_send(n, i);
            }
        }
    }
    for (int i = 0; i < MESH_SEG_RX_SLOTS; i++) {
        if (n->seg_rx[i].active && ++n->seg_rx[i].age_ticks > SEG_RX_EXPIRE_TICKS) {
            n->seg_rx[i].active = false;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* beacons                                                                    */
/* ------------------------------------------------------------------------- */

void mesh_net_recv_beacon(mesh_net_t *n, const uint8_t *pdu, size_t len)
{
    /* Secure Network Beacon: type(1) flags(1) netid(8) iv(4) auth(8) */
    if (len < 22 || pdu[0] != 0x01) return;
    if (memcmp(pdu + 2, n->network_id, 8) != 0) return;
    uint8_t auth[16];
    mesh_aes_cmac(n->beacon_key, pdu + 1, 13, auth);
    if (memcmp(auth, pdu + 14, 8) != 0) {
        LOGW("beacon auth mismatch");
        return;
    }
    uint32_t iv = ((uint32_t)pdu[10] << 24) | ((uint32_t)pdu[11] << 16) |
                  ((uint32_t)pdu[12] << 8) | pdu[13];
    if (iv > n->iv_index && iv - n->iv_index <= 42) {
        LOGI("IV index update %lu -> %lu", (unsigned long)n->iv_index, (unsigned long)iv);
        n->iv_index = iv;
        n->seq = 0;
    }
}

/* ------------------------------------------------------------------------- */
/* proxy configuration                                                        */
/* ------------------------------------------------------------------------- */

int mesh_net_build_proxy_cfg(mesh_net_t *n, uint8_t opcode, const uint8_t *params, size_t plen,
                             uint8_t *out, size_t out_cap)
{
    uint8_t tpdu[16];
    if (plen + 1 > sizeof(tpdu)) return -1;
    tpdu[0] = opcode & 0x7f;
    memcpy(tpdu + 1, params, plen);
    return mesh_net_encode(n, 1, 0, mesh_net_next_seq(n), n->own_addr, MESH_ADDR_UNASSIGNED,
                           tpdu, 1 + plen, true, out, out_cap);
}

int mesh_net_decode_proxy_cfg(mesh_net_t *n, const uint8_t *pdu, size_t len,
                              uint8_t *opcode, uint8_t *params, size_t *plen)
{
    uint8_t ctl, ttl, tpdu[16];
    uint32_t seq;
    uint16_t src, dst;
    size_t tlen;
    if (mesh_net_decode(n, pdu, len, true, &ctl, &ttl, &seq, &src, &dst, tpdu, &tlen, NULL) != 0) {
        return -1;
    }
    if (tlen < 1) return -1;
    *opcode = tpdu[0] & 0x7f;
    *plen = tlen - 1;
    memcpy(params, tpdu + 1, *plen);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* proxy advertising identification                                           */
/* ------------------------------------------------------------------------- */

bool mesh_net_match_network_id(const mesh_net_t *n, const uint8_t id[8])
{
    return memcmp(id, n->network_id, 8) == 0;
}

bool mesh_net_match_node_identity(const mesh_net_t *n, const uint8_t hash[8],
                                  const uint8_t random[8], uint16_t addr)
{
    uint8_t in[16] = { 0 }, out[16];
    memcpy(in + 6, random, 8);
    in[14] = (uint8_t)(addr >> 8);
    in[15] = (uint8_t)addr;
    mesh_aes128_encrypt(n->identity_key, in, out);
    return memcmp(out + 8, hash, 8) == 0;
}
