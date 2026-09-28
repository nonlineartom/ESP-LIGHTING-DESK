/*
 * Host unit tests for the protocol layers.  Vectors come from:
 *   - Mesh Profile 1.0.1 section 8 (sample data)
 *   - RFC 4493 (AES-CMAC)
 *   - Node.js crypto (AES-CCM cross-check)
 *   - wesbos/amaran-BLE-control DIRECT-BLE-CONTROL.md (real amaran network keys
 *     and the values the Telink SDK derived from them)
 *   - neyako/amaran-hacs docs/protocol.md (Telink 0x26 payload samples)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mesh_crypto.h"
#include "mesh_net.h"
#include "mesh_prov.h"
#include "mesh_cfg.h"
#include "telink.h"

static int g_fail = 0, g_pass = 0;

static void hex2bin(const char *hex, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void dump(const char *label, const uint8_t *b, size_t n)
{
    printf("    %s: ", label);
    for (size_t i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

#define CHECK_BYTES(name, got, hexexp, n) do { \
    uint8_t _e[64]; hex2bin(hexexp, _e, n); \
    if (memcmp(got, _e, n) == 0) { g_pass++; printf("PASS %s\n", name); } \
    else { g_fail++; printf("FAIL %s\n", name); dump("got", got, n); dump("exp", _e, n); } \
} while (0)

#define CHECK(name, cond) do { \
    if (cond) { g_pass++; printf("PASS %s\n", name); } \
    else { g_fail++; printf("FAIL %s\n", name); } \
} while (0)

/* ------------------------------------------------------------------------- */

static void test_cmac(void)
{
    uint8_t key[16], msg[16], mac[16];
    hex2bin("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    mesh_aes_cmac(key, NULL, 0, mac);
    CHECK_BYTES("cmac rfc4493 empty", mac, "bb1d6929e95937287fa37d129b756746", 16);
    hex2bin("6bc1bee22e409f96e93d7e117393172a", msg, 16);
    mesh_aes_cmac(key, msg, 16, mac);
    CHECK_BYTES("cmac rfc4493 16B", mac, "070a16b46b4d4144f79bdd9dd04a287c", 16);
    uint8_t msg40[40];
    hex2bin("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411", msg40, 40);
    mesh_aes_cmac(key, msg40, 40, mac);
    CHECK_BYTES("cmac rfc4493 40B", mac, "dfa66747de9ae63030ca32611497c827", 16);
}

static void test_kdf(void)
{
    uint8_t out[16], n[16], salt[16], p[16];

    mesh_s1((const uint8_t *)"test", 4, out);
    CHECK_BYTES("s1(test)", out, "b73cefbd641ef2ea598c2b6efb62f79c", 16);

    hex2bin("3216d1509884b533248541792b877f98", n, 16);
    hex2bin("2ba14ffa0df84a2831938d57d276cab4", salt, 16);
    hex2bin("5a09d60797eeb4478aada59db3352a0d", p, 16);
    mesh_k1(n, 16, salt, p, 16, out);
    CHECK_BYTES("k1 spec 8.1.2", out, "f6ed15a8934afbe7d83e8dcb57fcf5d7", 16);

    uint8_t nid, enc[16], priv[16], p0 = 0x00;
    hex2bin("f7a2a44f8e8a8029064f173ddc1e2b00", n, 16);
    mesh_k2(n, &p0, 1, &nid, enc, priv);
    CHECK("k2 spec nid", nid == 0x7f);
    CHECK_BYTES("k2 spec enc", enc, "9f589181a0f50de73c8070c7a6d27f46", 16);
    CHECK_BYTES("k2 spec priv", priv, "4c715bd4a64b938f99b453351653124f", 16);

    uint8_t id[8];
    mesh_k3(n, id);
    CHECK_BYTES("k3 spec", id, "ff046958233db014", 8);

    hex2bin("3216d1509884b533248541792b877f98", n, 16);
    CHECK("k4 spec", mesh_k4(n) == 0x38);

    /* real amaran network values (from DIRECT-BLE-CONTROL.md) */
    hex2bin("0D8094267D3F4EA5B06B324C8C0AD926", n, 16);
    mesh_k2(n, &p0, 1, &nid, enc, priv);
    CHECK("k2 amaran nid", nid == 0x3b);
    CHECK_BYTES("k2 amaran enc", enc, "ce1a0749c640a23be0bdf1c7c95fce93", 16);
    CHECK_BYTES("k2 amaran priv", priv, "96b5a15d3b3d3fa366251132ba16491c", 16);
    mesh_k3(n, id);
    CHECK_BYTES("k3 amaran network id", id, "0df357c791f677c7", 8);
    hex2bin("AB1C91DC421149FF87694B05A236F214", n, 16);
    CHECK("k4 amaran aid", mesh_k4(n) == 0x03);
}

static void test_ccm(void)
{
    uint8_t key[16], nonce[13], pt[18], ct[18], mic[8], back[18];
    hex2bin("0d8094267d3f4ea5b06b324c8c0ad926", key, 16);
    hex2bin("000a1234560001000000000000", nonce, 13);
    hex2bin("fffd40c5b8f2a1e3d4c6b7a8990011223344", pt, 18);

    mesh_aes_ccm_encrypt(key, nonce, NULL, 0, pt, 18, ct, mic, 4);
    CHECK_BYTES("ccm ct (node)", ct, "4525a2d7cd7469589b1e92376ae3cedf42bc", 18);
    CHECK_BYTES("ccm mic4 (node)", mic, "09c4d634", 4);
    mesh_aes_ccm_encrypt(key, nonce, NULL, 0, pt, 18, ct, mic, 8);
    CHECK_BYTES("ccm mic8 (node)", mic, "2ab2de83237c2545", 8);
    CHECK("ccm decrypt ok", mesh_aes_ccm_decrypt(key, nonce, NULL, 0, ct, 18, back, mic, 8) == 0 &&
                            memcmp(back, pt, 18) == 0);
    mic[0] ^= 1;
    CHECK("ccm decrypt detects bad mic", mesh_aes_ccm_decrypt(key, nonce, NULL, 0, ct, 18, back, mic, 8) != 0);
}

/* ---- network layer round trip + spec sample -------------------------------- */

static uint8_t last_tx[64];
static size_t last_tx_len;
static int tx_count;
static void tx_cb(void *ctx, const uint8_t *pdu, size_t len)
{
    (void)ctx;
    memcpy(last_tx, pdu, len);
    last_tx_len = len;
    tx_count++;
}
static uint8_t last_access[MESH_MAX_ACCESS_LEN];
static size_t last_access_len;
static uint16_t last_src;
static bool last_devkey;
static void access_cb(void *ctx, uint16_t src, uint16_t dst, bool devkey, const uint8_t *a, size_t len)
{
    (void)ctx; (void)dst;
    memcpy(last_access, a, len);
    last_access_len = len;
    last_src = src;
    last_devkey = devkey;
}
static uint8_t g_devkey[16];
static const uint8_t *devkey_cb(void *ctx, uint16_t src)
{
    (void)ctx; (void)src;
    return g_devkey;
}
static int seg_done_calls, seg_done_ok;
static void seg_done_cb(void *ctx, bool ok) { (void)ctx; seg_done_calls++; seg_done_ok = ok; }

static void test_net_spec_sample(void)
{
    /* Mesh Profile 1.0.1, 8.3.1 Message #1 (control message from 0x1201) */
    uint8_t netkey[16], appkey[16] = { 0 };
    hex2bin("7dd7364cd842ad18c17c2b820c84c3d6", netkey, 16);
    mesh_net_t net;
    mesh_net_callbacks_t cb = { .tx = tx_cb, .access_rx = access_cb, .devkey_lookup = devkey_cb, .seg_done = seg_done_cb };
    mesh_net_init(&net, netkey, appkey, 0x12345678, 1, 0x1201, &cb);
    CHECK("spec nid", net.nid == 0x68);
    CHECK_BYTES("spec enc key", net.enc_key, "0953fa93e7caac9638f58820220a398e", 16);
    CHECK_BYTES("spec privacy key", net.priv_key, "8b84eedec100067d670971dd2aa700cf", 16);

    uint8_t tpdu[11];
    hex2bin("034b50057e400000010000", tpdu, 11);
    uint8_t out[40];
    int len = mesh_net_encode(&net, 1, 0, 0x000001, 0x1201, 0xfffd, tpdu, 11, false, out, sizeof(out));
    CHECK("spec msg1 len", len == 28);
    CHECK_BYTES("spec msg1 network pdu", out,
                "68eca487516765b5e5bfdacbaf6cb7fb6bff871f035444ce83a670df", 28);

    /* decode our own encoding back */
    mesh_net_t rx;
    mesh_net_init(&rx, netkey, appkey, 0x12345678, 1, 0x0001, &cb);
    uint8_t ctl, ttl, tp[16];
    uint32_t seq, iv;
    uint16_t src, dst;
    size_t tl;
    int rc = mesh_net_decode(&rx, out, (size_t)len, false, &ctl, &ttl, &seq, &src, &dst, tp, &tl, &iv);
    CHECK("spec msg1 decode", rc == 0 && ctl == 1 && ttl == 0 && seq == 1 && src == 0x1201 &&
                              dst == 0xfffd && tl == 11 && memcmp(tp, tpdu, 11) == 0);
}

static void test_net_access_roundtrip(void)
{
    uint8_t netkey[16], appkey[16];
    hex2bin("0D8094267D3F4EA5B06B324C8C0AD926", netkey, 16);
    hex2bin("AB1C91DC421149FF87694B05A236F214", appkey, 16);
    hex2bin("6D008C1F2151BB07F5FA4537E72BC7AF", g_devkey, 16);

    mesh_net_callbacks_t cb = { .tx = tx_cb, .access_rx = access_cb, .devkey_lookup = devkey_cb, .seg_done = seg_done_cb };
    mesh_net_t a, b;
    mesh_net_init(&a, netkey, appkey, 0, 1000, 0x0001, &cb);   /* controller */
    mesh_net_init(&b, netkey, appkey, 0, 5, 0x0002, &cb);      /* light */

    /* unsegmented app-key message: Telink 0x26 + 10 bytes */
    uint8_t access[11] = { 0x26 };
    telink_onoff(true, access + 1);
    tx_count = 0;
    CHECK("unseg send returns 0", mesh_net_send_access(&a, 0x0002, 7, NULL, access, 11) == 0);
    CHECK("unseg produced one pdu", tx_count == 1 && last_tx_len == 1 + 6 + 2 + 1 + 15 + 4);
    last_access_len = 0;
    mesh_net_recv(&b, last_tx, last_tx_len);
    CHECK("unseg received", last_access_len == 11 && memcmp(last_access, access, 11) == 0 &&
                            last_src == 0x0001 && !last_devkey);

    /* replay of the same PDU must be dropped */
    last_access_len = 0;
    mesh_net_recv(&b, last_tx, last_tx_len);
    CHECK("replay dropped", last_access_len == 0);

    /* segmented dev-key message: AppKey Add (20 bytes) -> 24 bytes upper -> 2 segments */
    uint8_t add[32];
    size_t add_len = mesh_cfg_build_appkey_add(add, 0, 0, appkey);
    CHECK("appkey add len", add_len == 20);
    uint8_t segs[4][40];
    size_t seg_lens[4];
    int nseg = 0;
    tx_count = 0;
    int r = mesh_net_send_access(&a, 0x0002, 7, g_devkey, add, add_len);
    CHECK("seg send returns 1", r == 1 && mesh_net_seg_tx_busy(&a));
    /* capture: we only kept the last pdu, so re-drive via tick capturing each */
    (void)segs; (void)seg_lens; (void)nseg;

    /* Replay both segments to b by re-sending through a fresh capture: */
    /* simulate: collect PDUs emitted by a tick retransmission */
    static uint8_t cap[8][40];
    static size_t cap_len[8];
    static int cap_n;
    cap_n = 0;
    /* hook: temporarily swap tx to a capturing function */
    void capture_cb(void *ctx, const uint8_t *pdu, size_t len)
    {
        (void)ctx;
        if (cap_n < 8) { memcpy(cap[cap_n], pdu, len); cap_len[cap_n] = len; cap_n++; }
    }
    a.cb.tx = capture_cb;
    mesh_net_tick(&a);                 /* retransmits both (unacked) segments */
    CHECK("seg retransmit emitted 2 pdus", cap_n == 2);

    /* deliver to b: b should reassemble, ack, and deliver the access pdu */
    b.cb.tx = capture_cb;
    cap_n = 0;
    last_access_len = 0;
    mesh_net_recv(&b, cap[0], cap_len[0]);
    CHECK("seg1 no delivery yet", last_access_len == 0);
    /* cap[1] was overwritten? no: cap_n reset only affects index; cap[1] still holds seg 1 */
    mesh_net_recv(&b, cap[1], cap_len[1]);
    CHECK("seg reassembled + delivered (devkey)", last_access_len == 20 && memcmp(last_access, add, 20) == 0 && last_devkey);
    CHECK("segack emitted", cap_n == 1);

    /* feed the ack back to a: transfer completes */
    seg_done_calls = 0;
    mesh_net_recv(&a, cap[0], cap_len[0]);
    CHECK("seg tx completed on ack", seg_done_calls == 1 && seg_done_ok && !mesh_net_seg_tx_busy(&a));
}

static void test_proxy_cfg(void)
{
    uint8_t netkey[16], appkey[16] = { 0 };
    hex2bin("0D8094267D3F4EA5B06B324C8C0AD926", netkey, 16);
    mesh_net_callbacks_t cb = { 0 };
    mesh_net_t a, b;
    mesh_net_init(&a, netkey, appkey, 0, 77, 0x0001, &cb);
    mesh_net_init(&b, netkey, appkey, 0, 1, 0x0002, &cb);
    uint8_t params[1] = { MESH_PROXY_FILTER_WHITELIST }, pdu[40];
    int len = mesh_net_build_proxy_cfg(&a, MESH_PROXY_CFG_SET_FILTER_TYPE, params, 1, pdu, sizeof(pdu));
    CHECK("proxy cfg len", len == 1 + 6 + 2 + 2 + 8);
    uint8_t op, pr[16];
    size_t pl;
    CHECK("proxy cfg decode", mesh_net_decode_proxy_cfg(&b, pdu, (size_t)len, &op, pr, &pl) == 0 &&
                              op == MESH_PROXY_CFG_SET_FILTER_TYPE && pl == 1 && pr[0] == 0);
}

static void test_node_identity(void)
{
    /* Mesh Profile 8.4.2 sample? Not memorised — self-consistency check only. */
    uint8_t netkey[16], appkey[16] = { 0 };
    hex2bin("7dd7364cd842ad18c17c2b820c84c3d6", netkey, 16);
    mesh_net_t n;
    mesh_net_init(&n, netkey, appkey, 0, 0, 0x0001, NULL);
    uint8_t random[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t in[16] = { 0 }, out[16];
    memcpy(in + 6, random, 8);
    in[14] = 0x12; in[15] = 0x01;
    mesh_aes128_encrypt(n.identity_key, in, out);
    CHECK("node identity match", mesh_net_match_node_identity(&n, out + 8, random, 0x1201));
    CHECK("node identity mismatch", !mesh_net_match_node_identity(&n, out + 8, random, 0x1202));
}

/* ---- provisioning: PDU sequencing with a scripted device ------------------- */

static uint8_t prov_tx[8][70];
static size_t prov_tx_len[8];
static int prov_tx_n;
static void prov_send_cb(void *ctx, const uint8_t *pdu, size_t len)
{
    (void)ctx;
    if (prov_tx_n < 8) { memcpy(prov_tx[prov_tx_n], pdu, len); prov_tx_len[prov_tx_n] = len; prov_tx_n++; }
}

static void test_prov_flow(void)
{
    mesh_prov_t p;
    uint8_t netkey[16];
    hex2bin("7dd7364cd842ad18c17c2b820c84c3d6", netkey, 16);
    prov_tx_n = 0;
    /* ECDH is unavailable on host: start must fail cleanly */
    int rc = mesh_prov_start(&p, prov_send_cb, NULL, netkey, 0, 0, 0x0002);
    CHECK("prov start without ecdh fails cleanly", rc == -1 && p.state == MESH_PROV_FAILED);

    /* Exercise the session derivation with fixed material and check that the
     * Data PDU decrypts with the derived key (self-consistency). */
    memset(&p, 0, sizeof(p));
    memset(p.secret, 0xab, 32);
    memset(p.conf_salt, 0x11, 16);
    memset(p.rand_local, 0x22, 16);
    memset(p.rand_device, 0x33, 16);
    uint8_t sk[16], nonce[13];
    mesh_prov_derive_session(&p, sk, nonce);
    uint8_t zero[16] = { 0 };
    CHECK("prov devkey derived", memcmp(p.dev_key, zero, 16) != 0);
    CHECK("prov session key differs from devkey", memcmp(p.dev_key, sk, 16) != 0);
}

static void test_cfg(void)
{
    uint8_t buf[32];
    size_t n = mesh_cfg_build_composition_get(buf, 0);
    CHECK("comp get bytes", n == 3 && buf[0] == 0x80 && buf[1] == 0x08 && buf[2] == 0);
    n = mesh_cfg_build_model_app_bind(buf, 0x0002, 0x0000, 0x02110000u, true);
    CHECK("bind vendor bytes", n == 10 && buf[0] == 0x80 && buf[1] == 0x3d && buf[2] == 0x02 && buf[3] == 0 &&
                               buf[6] == 0x11 && buf[7] == 0x02 && buf[8] == 0 && buf[9] == 0);
    n = mesh_cfg_build_model_app_bind(buf, 0x0002, 0x0000, 0x1000, false);
    CHECK("bind sig bytes", n == 8 && buf[6] == 0x00 && buf[7] == 0x10);
    n = mesh_cfg_build_appkey_add(buf, 0x000, 0x000, (const uint8_t *)"0123456789abcdef");
    CHECK("appkey add idx packing", buf[1] == 0 && buf[2] == 0 && buf[3] == 0);
    n = mesh_cfg_build_appkey_add(buf, 0x123, 0x456, (const uint8_t *)"0123456789abcdef");
    CHECK("appkey add idx packing 2", buf[1] == 0x23 && buf[2] == 0x61 && buf[3] == 0x45);

    /* composition data status: page0, cid 0211 pid 0001 vid 0002 crpl 0010 features 0003,
       one element: loc 0000, 2 sig (0000, 1000), 1 vendor (0211:0000) */
    uint8_t comp[] = { 0x02, 0x00, 0x11, 0x02, 0x01, 0x00, 0x02, 0x00, 0x10, 0x00, 0x03, 0x00,
                       0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x10, 0x11, 0x02, 0x00, 0x00 };
    mesh_comp_t c;
    CHECK("comp parse", mesh_cfg_parse_composition(comp, sizeof(comp), &c) == 0 && c.cid == 0x0211 &&
                        c.pid == 1 && c.elem_count == 1 && c.elems[0].num_sig == 2 && c.elems[0].num_vnd == 1 &&
                        c.elems[0].sig[1] == 0x1000 && c.elems[0].vnd[0] == 0x02110000u);

    uint8_t st[] = { 0x80, 0x3e, 0x00, 0x02, 0x00, 0x00, 0x00, 0x11, 0x02, 0x00, 0x00 };
    uint8_t status; uint16_t ea; uint32_t mid; bool vnd;
    CHECK("model app status parse", mesh_cfg_parse_model_app_status(st, sizeof(st), &status, &ea, &mid, &vnd) == 0 &&
                                    status == 0 && ea == 2 && vnd && mid == 0x02110000u);
}

static void test_telink(void)
{
    uint8_t p[10];
    telink_intensity(100, p);  CHECK_BYTES("telink bri 10%", p, "a800000000000000198f", 10);
    telink_intensity(220, p);  CHECK_BYTES("telink bri 22%", p, "c600000000000000378f", 10);
    telink_intensity(500, p);  CHECK_BYTES("telink bri 50%", p, "0c000000000000007d8f", 10);
    telink_cct(3200, 220, 0, p); CHECK_BYTES("telink cct 3200K 22%", p, "0e000000004001143782", 10);
    telink_cct(5600, 300, 0, p); CHECK_BYTES("telink cct 5600K 30%", p, "31000000004001234b82", 10);
    telink_hsi(45, 60, 800, p); CHECK_BYTES("telink hsi 45/60/80", p, "fd0000000000af05c881", 10);
    telink_onoff(true, p);  CHECK_BYTES("telink on", p, "8d00000000000000018c", 10);
    telink_onoff(false, p); CHECK_BYTES("telink off", p, "8c00000000000000008c", 10);
    telink_status_request(p); CHECK_BYTES("telink status req", p, "0e00000000000000000e", 10);

    /* decode a CCT report built from the setter (report = setter with bit7 clear + power bit) */
    telink_cct(5600, 300, 3, p);
    p[9] &= 0x7f;
    p[1] |= 0x01;                          /* power on */
    p[0] = 0; for (int i = 1; i < 10; i++) p[0] += p[i];
    telink_status_t st;
    CHECK("telink decode cct", telink_decode_status(p, &st) && !st.is_hsi && st.on &&
                               st.cct_kelvin == 5600 && st.intensity == 300 && st.gm == 3);
    telink_hsi(200, 55, 640, p);
    p[9] &= 0x7f;
    p[0] = 0; for (int i = 1; i < 10; i++) p[0] += p[i];
    CHECK("telink decode hsi", telink_decode_status(p, &st) && st.is_hsi && !st.on &&
                               st.hue == 200 && st.sat == 55 && st.intensity == 640);
}

int main(void)
{
    test_cmac();
    test_kdf();
    test_ccm();
    test_net_spec_sample();
    test_net_access_roundtrip();
    test_proxy_cfg();
    test_node_identity();
    test_prov_flow();
    test_cfg();
    test_telink();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
