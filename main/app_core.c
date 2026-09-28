#include "app_core.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "ble_central.h"
#include "mesh_cfg.h"
#include "mesh_crypto.h"
#include "mesh_net.h"
#include "mesh_prov.h"
#include "store.h"
#include "telink.h"
#include "ui.h"

static const char *TAG = "core";

#define OWN_ADDR            0x0001
#define FIRST_LIGHT_ADDR    0x0002
#define TICK_MS             400
#define SEQ_SAVE_INTERVAL   32
#define SEQ_BOOT_MARGIN     64
#define STATUS_POLL_TICKS   20      /* 8 s */
#define PROXY_SCAN_TIMEOUT  75      /* 30 s before we restart the scan */
#define CFG_TIMEOUT_TICKS   8       /* ~3 s per config request */
#define CFG_RETRIES         3
#define UNPROV_EXPIRE_MS    15000

/* ------------------------------------------------------------------------- */
/* events                                                                     */
/* ------------------------------------------------------------------------- */

typedef enum { EV_BLE, EV_CMD, EV_TICK } ev_kind_t;

typedef enum {
    CMD_PAIR_START, CMD_PAIR_SELECT, CMD_PAIR_CANCEL,
    CMD_POWER, CMD_INTENSITY, CMD_CCT, CMD_HSI, CMD_STATUS,
    CMD_REMOVE, CMD_FACTORY_RESET, CMD_RENAME,
    CMD_MASTER, CMD_ALL_POWER, CMD_ALL_CCT,
    CMD_SCENE_SAVE, CMD_SCENE_RECALL, CMD_SCENE_DELETE,
} cmd_type_t;

typedef struct {
    ev_kind_t kind;
    union {
        struct {
            ble_evt_t evt;
            union {
                ble_adv_t adv;
                ble_proxy_pdu_t pdu;
            } d;
        } ble;
        struct {
            cmd_type_t type;
            uint16_t addr;
            uint16_t a, b;
            int c;
            bool on;
            uint8_t uuid[16];
            char    name[LIGHT_NAME_LEN];
        } cmd;
    };
} app_event_t;

typedef enum {
    CS_BOOT = 0,
    CS_NO_NET,
    CS_PROXY_SCAN,
    CS_PROXY_CONNECTING,
    CS_ONLINE,
    CS_PAIR_DISCONNECTING,
    CS_PAIR_SCAN,
    CS_PAIR_CONNECTING,
    CS_PAIR_PROV,
    CS_PAIR_WAIT_DISC,
    CS_PAIR_PROXY_SCAN,
    CS_PAIR_PROXY_CONNECTING,
    CS_PAIR_CONFIG,
} core_state_t;

typedef enum { CFG_NONE = 0, CFG_COMP, CFG_APPKEY_SEG, CFG_APPKEY_STATUS, CFG_BIND } cfg_step_t;

static QueueHandle_t      s_queue;
static SemaphoreHandle_t  s_lock;
static esp_timer_handle_t s_tick_timer;

static core_state_t       s_state = CS_BOOT;
static mesh_network_cfg_t s_netcfg;
static mesh_net_t         s_net;
static mesh_prov_t        s_prov;
static uint32_t           s_seq_saved;
static uint16_t           s_master = 1000;   /* grand master 0..1000 */
static int                s_last_scene = -1;

static uint16_t s_proxy_addr;          /* light we are connected through */
static uint16_t s_target_addr;         /* light we want to connect to (0 = any) */
static uint32_t s_state_ticks;
static uint32_t s_poll_ticks;
static int      s_poll_index;
static int      s_refresh_in;          /* ticks until a post-command status refresh */
static uint16_t s_refresh_addr;

/* pairing */
static pair_state_t s_pair_state = PAIR_IDLE;
static char         s_pair_text[48] = "";
static int          s_pair_pct;
static unprov_dev_t s_unprov[UNPROV_MAX];
static int          s_unprov_n;
static bool         s_pair_dirty;
static struct {
    uint8_t uuid[16];
    uint8_t addr[6];
    uint8_t addr_type;
    char    name[24];
    uint16_t unicast;
} s_pending;

/* configuration of a freshly provisioned node */
static struct {
    uint16_t   addr;
    cfg_step_t step;
    int        ticks;
    int        retries;
    mesh_comp_t comp;
    uint32_t   models[MESH_COMP_MAX_ELEMENTS * (MESH_COMP_MAX_SIG_MODELS + MESH_COMP_MAX_VND_MODELS)];
    uint16_t   model_elem[MESH_COMP_MAX_ELEMENTS * (MESH_COMP_MAX_SIG_MODELS + MESH_COMP_MAX_VND_MODELS)];
    bool       model_vendor[MESH_COMP_MAX_ELEMENTS * (MESH_COMP_MAX_SIG_MODELS + MESH_COMP_MAX_VND_MODELS)];
    int        model_count;
    int        bind_index;
} s_cfg;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ------------------------------------------------------------------------- */
/* locking + state accessors                                                  */
/* ------------------------------------------------------------------------- */

void app_core_lock(void)   { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void app_core_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

link_state_t app_core_link_state(void)
{
    switch (s_state) {
    case CS_BOOT:             return LINK_STARTING;
    case CS_NO_NET:           return LINK_NO_LIGHTS;
    case CS_PROXY_SCAN:       return LINK_SCANNING;
    case CS_PROXY_CONNECTING: return LINK_CONNECTING;
    case CS_ONLINE:           return LINK_ONLINE;
    default:                  return LINK_PAIRING;
    }
}

const char *app_core_link_text(void)
{
    static char buf[48];
    switch (s_state) {
    case CS_BOOT:             return "Starting Bluetooth";
    case CS_NO_NET:           return "No lights paired";
    case CS_PROXY_SCAN:       return "Looking for lights";
    case CS_PROXY_CONNECTING: return "Connecting";
    case CS_ONLINE: {
        light_t *l = lights_find_by_addr(s_proxy_addr);
        snprintf(buf, sizeof(buf), "Online via %s", l ? l->name : "light");
        return buf;
    }
    default:                  return "Pairing";
    }
}

uint16_t     app_core_proxy_addr(void) { return s_state == CS_ONLINE ? s_proxy_addr : 0; }
pair_state_t app_core_pair_state(void) { return s_pair_state; }
const char  *app_core_pair_text(void)  { return s_pair_text; }
int          app_core_pair_percent(void) { return s_pair_pct; }

int app_core_unprov_list(unprov_dev_t *out, int max)
{
    int n = s_unprov_n < max ? s_unprov_n : max;
    memcpy(out, s_unprov, n * sizeof(unprov_dev_t));
    return n;
}

static void set_pair(pair_state_t st, const char *text, int pct)
{
    s_pair_state = st;
    if (text) snprintf(s_pair_text, sizeof(s_pair_text), "%s", text);
    s_pair_pct = pct;
    ESP_LOGI(TAG, "pair: %s (%d%%)", s_pair_text, pct);
    ui_notify_pair_changed();
}

static void set_state(core_state_t st)
{
    if (st != s_state) ESP_LOGI(TAG, "state %d -> %d", s_state, st);
    s_state = st;
    s_state_ticks = 0;
    ui_notify_link_changed();
}

/* ------------------------------------------------------------------------- */
/* event posting                                                              */
/* ------------------------------------------------------------------------- */

static void post(const app_event_t *ev)
{
    if (xQueueSend(s_queue, ev, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full (kind %d)", ev->kind);
    }
}

static void ble_cb(ble_evt_t evt, const void *data, size_t len)
{
    app_event_t ev = { .kind = EV_BLE };
    ev.ble.evt = evt;
    if (data && len <= sizeof(ev.ble.d)) memcpy(&ev.ble.d, data, len);
    post(&ev);
}

static void tick_cb(void *arg)
{
    (void)arg;
    app_event_t ev = { .kind = EV_TICK };
    xQueueSend(s_queue, &ev, 0);
}

static void post_cmd(cmd_type_t type, uint16_t addr, uint16_t a, uint16_t b, int c, bool on, const uint8_t *uuid)
{
    app_event_t ev = { .kind = EV_CMD };
    ev.cmd.type = type;
    ev.cmd.addr = addr;
    ev.cmd.a = a;
    ev.cmd.b = b;
    ev.cmd.c = c;
    ev.cmd.on = on;
    if (uuid) memcpy(ev.cmd.uuid, uuid, 16);
    post(&ev);
}

void app_cmd_pair_start(void)                        { post_cmd(CMD_PAIR_START, 0, 0, 0, 0, false, NULL); }
void app_cmd_pair_select(const uint8_t uuid[16])     { post_cmd(CMD_PAIR_SELECT, 0, 0, 0, 0, false, uuid); }
void app_cmd_pair_cancel(void)                       { post_cmd(CMD_PAIR_CANCEL, 0, 0, 0, 0, false, NULL); }
void app_cmd_set_power(uint16_t addr, bool on)       { post_cmd(CMD_POWER, addr, 0, 0, 0, on, NULL); }
void app_cmd_set_intensity(uint16_t addr, uint16_t v){ post_cmd(CMD_INTENSITY, addr, v, 0, 0, false, NULL); }
void app_cmd_set_cct(uint16_t addr, uint16_t k, uint16_t v, int gm) { post_cmd(CMD_CCT, addr, k, v, gm, false, NULL); }
void app_cmd_set_hsi(uint16_t addr, uint16_t h, uint8_t s, uint16_t v) { post_cmd(CMD_HSI, addr, h, v, s, false, NULL); }
void app_cmd_request_status(uint16_t addr)           { post_cmd(CMD_STATUS, addr, 0, 0, 0, false, NULL); }
void app_cmd_remove_light(uint16_t addr)             { post_cmd(CMD_REMOVE, addr, 0, 0, 0, false, NULL); }
void app_cmd_factory_reset(void)                     { post_cmd(CMD_FACTORY_RESET, 0, 0, 0, 0, false, NULL); }
void app_cmd_master_level(uint16_t level)            { post_cmd(CMD_MASTER, 0, level, 0, 0, false, NULL); }
void app_cmd_all_power(bool on)                      { post_cmd(CMD_ALL_POWER, 0, 0, 0, 0, on, NULL); }
void app_cmd_all_cct(uint16_t kelvin)                { post_cmd(CMD_ALL_CCT, 0, kelvin, 0, 0, false, NULL); }
void app_cmd_scene_save(int idx)                     { post_cmd(CMD_SCENE_SAVE, 0, 0, 0, idx, false, NULL); }
void app_cmd_scene_recall(int idx)                   { post_cmd(CMD_SCENE_RECALL, 0, 0, 0, idx, false, NULL); }
void app_cmd_scene_delete(int idx)                   { post_cmd(CMD_SCENE_DELETE, 0, 0, 0, idx, false, NULL); }
uint16_t app_core_master_level(void)                 { return s_master; }
int      app_core_last_scene(void)                   { return s_last_scene; }

void app_cmd_rename_light(uint16_t addr, const char *name)
{
    app_event_t ev = { .kind = EV_CMD };
    ev.cmd.type = CMD_RENAME;
    ev.cmd.addr = addr;
    snprintf(ev.cmd.name, sizeof(ev.cmd.name), "%s", name ? name : "");
    post(&ev);
}

/* ------------------------------------------------------------------------- */
/* mesh network callbacks                                                     */
/* ------------------------------------------------------------------------- */

static void net_tx(void *ctx, const uint8_t *pdu, size_t len)
{
    (void)ctx;
    if (ble_central_send_proxy_pdu(MESH_PROXY_TYPE_NETWORK, pdu, len) != 0) {
        ESP_LOGW(TAG, "proxy tx failed (not ready)");
    }
}

static const uint8_t *net_devkey_lookup(void *ctx, uint16_t src)
{
    (void)ctx;
    light_t *l = lights_find_by_addr(src);
    return l ? l->devkey : NULL;
}

static void cfg_handle_access(uint16_t src, const uint8_t *access, size_t len);
static void cfg_seg_done(bool ok);

static void net_access_rx(void *ctx, uint16_t src, uint16_t dst, bool devkey,
                          const uint8_t *access, size_t len)
{
    (void)ctx; (void)dst;
    uint32_t opcode;
    size_t oplen = mesh_access_opcode(access, len, &opcode);
    if (!oplen) return;

    if (opcode == TELINK_OPCODE && !devkey) {
        if (len < 1 + TELINK_PAYLOAD_LEN) return;
        telink_status_t st;
        if (!telink_decode_status(access + 1, &st)) {
            ESP_LOGD(TAG, "telink non-state page 0x%02x from 0x%04x", access[10] & 0x7f, src);
            return;
        }
        app_core_lock();
        light_t *l = lights_find_by_addr(src);
        if (l) {
            lights_apply_status(l, &st, now_ms());
            uint32_t lv = s_master ? (uint32_t)st.intensity * 1000 / s_master : st.intensity;
            l->state.level = lv > 1000 ? 1000 : (uint16_t)lv;
        }
        app_core_unlock();
        ESP_LOGI(TAG, "status 0x%04x: %s %s int=%u cct=%u gm=%d hue=%u sat=%u", src,
                 st.on ? "on" : "off", st.is_hsi ? "hsi" : "cct", st.intensity, st.cct_kelvin,
                 st.gm, st.hue, st.sat);
        if (l) ui_notify_light_state(l->addr);
        return;
    }
    if (devkey) {
        cfg_handle_access(src, access, len);
        return;
    }
    ESP_LOGI(TAG, "access rx src=0x%04x opcode=0x%06lx len=%u", src, (unsigned long)opcode, (unsigned)len);
}

static void net_seg_done(void *ctx, bool ok)
{
    (void)ctx;
    cfg_seg_done(ok);
}

/* ------------------------------------------------------------------------- */
/* helpers                                                                    */
/* ------------------------------------------------------------------------- */

static void maybe_save_seq(void)
{
    if (s_net.seq - s_seq_saved >= SEQ_SAVE_INTERVAL || s_net.seq < s_seq_saved) {
        s_seq_saved = s_net.seq;
        store_save_seq(s_net.seq);
    }
}

static void send_telink(uint16_t addr, const uint8_t payload[10])
{
    if (s_state != CS_ONLINE && s_state != CS_PAIR_CONFIG) return;
    uint8_t access[1 + TELINK_PAYLOAD_LEN];
    access[0] = TELINK_OPCODE;
    memcpy(access + 1, payload, TELINK_PAYLOAD_LEN);
    mesh_net_send_access(&s_net, addr, 0xff, NULL, access, sizeof(access));
    maybe_save_seq();
}

static void send_status_request(uint16_t addr)
{
    uint8_t p[10];
    telink_status_request(p);
    send_telink(addr, p);
}

static void proxy_filter_setup(void)
{
    uint8_t pdu[40], params[6];
    params[0] = MESH_PROXY_FILTER_WHITELIST;
    int len = mesh_net_build_proxy_cfg(&s_net, MESH_PROXY_CFG_SET_FILTER_TYPE, params, 1, pdu, sizeof(pdu));
    if (len > 0) ble_central_send_proxy_pdu(MESH_PROXY_TYPE_PROXY_CFG, pdu, (size_t)len);
    vTaskDelay(pdMS_TO_TICKS(150));
    params[0] = OWN_ADDR >> 8; params[1] = OWN_ADDR & 0xff;
    params[2] = 0xff; params[3] = 0xff;
    len = mesh_net_build_proxy_cfg(&s_net, MESH_PROXY_CFG_ADD_ADDRESSES, params, 4, pdu, sizeof(pdu));
    if (len > 0) ble_central_send_proxy_pdu(MESH_PROXY_TYPE_PROXY_CFG, pdu, (size_t)len);
}

static void start_proxy_scan(uint16_t target, bool pairing)
{
    s_target_addr = target;
    set_state(pairing ? CS_PAIR_PROXY_SCAN : CS_PROXY_SCAN);
    ble_central_scan_start();
}

static void go_idle_or_scan(void)
{
    if (lights_count() == 0) {
        set_state(CS_NO_NET);
    } else {
        start_proxy_scan(0, false);
    }
}

static void save_lights(void)
{
    app_core_lock();
    store_save_lights();
    app_core_unlock();
    ui_notify_lights_changed();
}

/* ------------------------------------------------------------------------- */
/* configuration client state machine                                         */
/* ------------------------------------------------------------------------- */

static void cfg_send_current(void);

static void cfg_begin(uint16_t addr)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.addr = addr;
    s_cfg.step = CFG_COMP;
    set_state(CS_PAIR_CONFIG);
    set_pair(PAIR_IN_PROGRESS, "Reading light capabilities", 70);
    cfg_send_current();
}

static void cfg_finish(bool ok)
{
    light_t *l = lights_find_by_addr(s_cfg.addr);
    if (ok && l) {
        app_core_lock();
        l->configured = true;
        app_core_unlock();
        save_lights();
        set_pair(PAIR_DONE, "Paired and ready", 100);
    } else {
        set_pair(PAIR_FAILED, "Configuration failed - try again", 0);
    }
    s_cfg.step = CFG_NONE;
    s_proxy_addr = s_cfg.addr;
    set_state(CS_ONLINE);
    s_poll_ticks = 0;
    s_refresh_addr = s_cfg.addr;
    s_refresh_in = 2;
}

static void cfg_send_current(void)
{
    light_t *l = lights_find_by_addr(s_cfg.addr);
    if (!l) {
        cfg_finish(false);
        return;
    }
    uint8_t msg[24];
    size_t n = 0;
    s_cfg.ticks = 0;
    switch (s_cfg.step) {
    case CFG_COMP:
        n = mesh_cfg_build_composition_get(msg, 0);
        break;
    case CFG_APPKEY_SEG:
        n = mesh_cfg_build_appkey_add(msg, 0, 0, s_net.appkey);
        break;
    case CFG_BIND: {
        if (s_cfg.bind_index >= s_cfg.model_count) {
            cfg_finish(true);
            return;
        }
        int i = s_cfg.bind_index;
        char t[48];
        snprintf(t, sizeof(t), "Binding models (%d/%d)", i + 1, s_cfg.model_count);
        set_pair(PAIR_IN_PROGRESS, t, 85 + (15 * i) / (s_cfg.model_count ? s_cfg.model_count : 1));
        n = mesh_cfg_build_model_app_bind(msg, s_cfg.model_elem[i], 0, s_cfg.models[i], s_cfg.model_vendor[i]);
        break;
    }
    default:
        return;
    }
    int rc = mesh_net_send_access(&s_net, s_cfg.addr, 0xff, l->devkey, msg, n);
    maybe_save_seq();
    if (rc < 0) {
        ESP_LOGW(TAG, "cfg send failed at step %d", s_cfg.step);
        if (++s_cfg.retries > CFG_RETRIES) cfg_finish(false);
    }
}

static void cfg_seg_done(bool ok)
{
    if (s_state != CS_PAIR_CONFIG || s_cfg.step != CFG_APPKEY_SEG) return;
    if (ok) {
        s_cfg.step = CFG_APPKEY_STATUS;
        s_cfg.ticks = 0;
    } else if (++s_cfg.retries > CFG_RETRIES) {
        cfg_finish(false);
    } else {
        cfg_send_current();
    }
}

static void cfg_handle_access(uint16_t src, const uint8_t *access, size_t len)
{
    if (s_state != CS_PAIR_CONFIG || src != s_cfg.addr) return;
    uint32_t opcode;
    if (!mesh_access_opcode(access, len, &opcode)) return;

    if (s_cfg.step == CFG_COMP && opcode == MESH_OP_COMPOSITION_DATA_STATUS) {
        if (mesh_cfg_parse_composition(access, len, &s_cfg.comp) != 0) {
            ESP_LOGW(TAG, "composition parse failed");
            return;
        }
        light_t *l = lights_find_by_addr(s_cfg.addr);
        app_core_lock();
        if (l) {
            l->cid = s_cfg.comp.cid;
            l->pid = s_cfg.comp.pid;
            l->elem_count = s_cfg.comp.elem_count;
        }
        app_core_unlock();
        ESP_LOGI(TAG, "composition: cid=0x%04x pid=0x%04x vid=0x%04x elements=%u",
                 s_cfg.comp.cid, s_cfg.comp.pid, s_cfg.comp.vid, s_cfg.comp.elem_count);
        s_cfg.model_count = 0;
        for (int e = 0; e < s_cfg.comp.elem_count; e++) {
            const mesh_comp_element_t *el = &s_cfg.comp.elems[e];
            for (int i = 0; i < el->num_sig; i++) {
                uint16_t m = el->sig[i];
                if (m == 0x0000 || m == 0x0001) continue;   /* config server/client: not bindable */
                s_cfg.models[s_cfg.model_count] = m;
                s_cfg.model_elem[s_cfg.model_count] = s_cfg.addr + e;
                s_cfg.model_vendor[s_cfg.model_count] = false;
                s_cfg.model_count++;
                ESP_LOGI(TAG, "  elem %d sig model 0x%04x", e, m);
            }
            for (int i = 0; i < el->num_vnd; i++) {
                s_cfg.models[s_cfg.model_count] = el->vnd[i];
                s_cfg.model_elem[s_cfg.model_count] = s_cfg.addr + e;
                s_cfg.model_vendor[s_cfg.model_count] = true;
                s_cfg.model_count++;
                ESP_LOGI(TAG, "  elem %d vendor model 0x%08lx", e, (unsigned long)el->vnd[i]);
            }
        }
        s_cfg.step = CFG_APPKEY_SEG;
        s_cfg.retries = 0;
        set_pair(PAIR_IN_PROGRESS, "Adding application key", 80);
        cfg_send_current();
        return;
    }
    if ((s_cfg.step == CFG_APPKEY_STATUS || s_cfg.step == CFG_APPKEY_SEG) && opcode == MESH_OP_APPKEY_STATUS) {
        uint8_t status = 0xff;
        mesh_cfg_parse_appkey_status(access, len, &status);
        ESP_LOGI(TAG, "appkey status 0x%02x", status);
        if (status == 0x00 || status == 0x06 /* key index already stored */) {
            mesh_net_seg_tx_abort(&s_net);
            s_cfg.step = CFG_BIND;
            s_cfg.bind_index = 0;
            s_cfg.retries = 0;
            cfg_send_current();
        } else {
            cfg_finish(false);
        }
        return;
    }
    if (s_cfg.step == CFG_BIND && opcode == MESH_OP_MODEL_APP_STATUS) {
        uint8_t status; uint16_t ea; uint32_t mid; bool vnd;
        if (mesh_cfg_parse_model_app_status(access, len, &status, &ea, &mid, &vnd) == 0) {
            ESP_LOGI(TAG, "bind status 0x%02x model 0x%08lx", status, (unsigned long)mid);
        }
        s_cfg.bind_index++;
        s_cfg.retries = 0;
        cfg_send_current();
        return;
    }
}

static void cfg_tick(void)
{
    if (s_state != CS_PAIR_CONFIG || s_cfg.step == CFG_NONE) return;
    if (++s_cfg.ticks < CFG_TIMEOUT_TICKS) return;
    if (s_cfg.step == CFG_APPKEY_SEG && mesh_net_seg_tx_busy(&s_net)) return;   /* still retransmitting */
    if (s_cfg.step == CFG_APPKEY_STATUS) {
        /* the key was acknowledged at the transport layer; Telink nodes reply
         * with AppKey Status but if it is lost, carry on to binding */
        ESP_LOGW(TAG, "no AppKey Status, continuing");
        s_cfg.step = CFG_BIND;
        s_cfg.bind_index = 0;
        s_cfg.retries = 0;
        cfg_send_current();
        return;
    }
    if (s_cfg.step == CFG_BIND) {
        if (++s_cfg.retries > 2) {
            ESP_LOGW(TAG, "bind %d timed out, skipping", s_cfg.bind_index);
            s_cfg.bind_index++;
            s_cfg.retries = 0;
        }
        cfg_send_current();
        return;
    }
    if (++s_cfg.retries > CFG_RETRIES) {
        cfg_finish(false);
        return;
    }
    ESP_LOGW(TAG, "cfg step %d timeout, retry %d", s_cfg.step, s_cfg.retries);
    cfg_send_current();
}

/* ------------------------------------------------------------------------- */
/* pairing                                                                    */
/* ------------------------------------------------------------------------- */

static void prov_send(void *ctx, const uint8_t *pdu, size_t len)
{
    (void)ctx;
    ble_central_send_proxy_pdu(MESH_PROXY_TYPE_PROVISIONING, pdu, len);
}

static void pair_begin_scan(void)
{
    s_unprov_n = 0;
    set_state(CS_PAIR_SCAN);
    set_pair(PAIR_SCANNING, "Looking for lights in pairing mode", 0);
    ble_central_scan_start();
}

static void pair_abort(const char *reason)
{
    ESP_LOGW(TAG, "pairing aborted: %s", reason);
    mesh_prov_abort(&s_prov);
    set_pair(PAIR_FAILED, reason, 0);
    ble_central_scan_stop();
    if (ble_central_is_connected()) {
        ble_central_disconnect();
        set_state(CS_PAIR_DISCONNECTING);
        s_pending.unicast = 0;   /* marks "return to normal after disconnect" */
    } else {
        go_idle_or_scan();
    }
}

static void handle_unprov_adv(const ble_adv_t *adv)
{
    if (s_state != CS_PAIR_SCAN) return;
    if (lights_find_by_uuid(adv->unprov.uuid)) return;    /* already ours */
    app_core_lock();
    int idx = -1;
    for (int i = 0; i < s_unprov_n; i++) {
        if (memcmp(s_unprov[i].uuid, adv->unprov.uuid, 16) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        if (s_unprov_n >= UNPROV_MAX) { app_core_unlock(); return; }
        idx = s_unprov_n++;
        memset(&s_unprov[idx], 0, sizeof(s_unprov[idx]));
        memcpy(s_unprov[idx].uuid, adv->unprov.uuid, 16);
        ESP_LOGI(TAG, "unprovisioned device: %s rssi=%d", adv->name[0] ? adv->name : "(no name)", adv->rssi);
    }
    memcpy(s_unprov[idx].addr, adv->addr, 6);
    s_unprov[idx].addr_type = adv->addr_type;
    s_unprov[idx].rssi = adv->rssi;
    if (adv->name[0]) snprintf(s_unprov[idx].name, sizeof(s_unprov[idx].name), "%s", adv->name);
    s_unprov[idx].last_seen_ms = now_ms();
    app_core_unlock();
    s_pair_dirty = true;
}

static void handle_proxy_adv(const ble_adv_t *adv)
{
    if (s_state != CS_PROXY_SCAN && s_state != CS_PAIR_PROXY_SCAN) return;

    light_t *match = NULL;
    if (adv->proxy.id_type == 0x01) {
        for (int i = 0; i < LIGHTS_MAX; i++) {
            light_t *l = &g_lights[i];
            if (!l->used) continue;
            if (mesh_net_match_node_identity(&s_net, adv->proxy.data, adv->proxy.data + 8, l->addr)) {
                match = l;
                break;
            }
        }
    } else if (adv->proxy.id_type == 0x00) {
        if (!mesh_net_match_network_id(&s_net, adv->proxy.data)) return;
        /* Network ID does not say which node: use the BLE address as a hint */
        for (int i = 0; i < LIGHTS_MAX; i++) {
            light_t *l = &g_lights[i];
            if (l->used && memcmp(l->ble_addr, adv->addr, 6) == 0) { match = l; break; }
        }
    } else {
        return;
    }

    if (s_target_addr) {
        if (!match || match->addr != s_target_addr) {
            /* unknown node in our network while we look for a specific one:
             * only accept it if it is the address we just provisioned */
            bool by_ble = memcmp(s_pending.addr, adv->addr, 6) == 0;
            if (!by_ble) return;
            match = lights_find_by_addr(s_target_addr);
            if (!match) return;
        }
    } else if (!match && adv->proxy.id_type != 0x00) {
        return;
    }

    if (match) {
        app_core_lock();
        memcpy(match->ble_addr, adv->addr, 6);
        match->ble_addr_type = adv->addr_type;
        app_core_unlock();
    }
    s_proxy_addr = match ? match->addr : 0;
    ESP_LOGI(TAG, "proxy candidate %s rssi=%d", match ? match->name : "(network id)", adv->rssi);
    if (ble_central_connect(adv->addr, adv->addr_type, false) == 0) {
        set_state(s_state == CS_PAIR_PROXY_SCAN ? CS_PAIR_PROXY_CONNECTING : CS_PROXY_CONNECTING);
        if (s_state == CS_PAIR_PROXY_CONNECTING) set_pair(PAIR_IN_PROGRESS, "Connecting to the light", 65);
    }
}

static void handle_prov_pdu(const ble_proxy_pdu_t *pdu)
{
    if (s_state != CS_PAIR_PROV) return;
    int rc = mesh_prov_rx(&s_prov, pdu->data, pdu->len);
    if (rc == 0) {
        int pct = 30;
        switch (s_prov.state) {
        case MESH_PROV_WAIT_PUBKEY:       pct = 35; break;
        case MESH_PROV_WAIT_CONFIRMATION: pct = 42; break;
        case MESH_PROV_WAIT_RANDOM:       pct = 48; break;
        case MESH_PROV_WAIT_COMPLETE:     pct = 52; break;
        default: break;
        }
        set_pair(PAIR_IN_PROGRESS, "Provisioning", pct);
        return;
    }
    if (rc < 0) {
        char t[48];
        snprintf(t, sizeof(t), "Provisioning failed: %s", s_prov.error_text ? s_prov.error_text : "error");
        pair_abort(t);
        return;
    }

    /* success: register the node */
    app_core_lock();
    light_t *l = lights_add(s_pending.uuid, s_prov.unicast, s_prov.num_elements, s_prov.dev_key, s_pending.name);
    if (l) {
        memcpy(l->ble_addr, s_pending.addr, 6);
        l->ble_addr_type = s_pending.addr_type;
    }
    s_netcfg.next_addr = s_prov.unicast + s_prov.num_elements;
    s_netcfg.seq = s_net.seq;
    app_core_unlock();
    store_save_network(&s_netcfg);
    save_lights();

    set_pair(PAIR_IN_PROGRESS, "Waiting for the light to restart", 55);
    set_state(CS_PAIR_WAIT_DISC);
    s_pending.unicast = s_prov.unicast;
}

/* ------------------------------------------------------------------------- */
/* BLE events                                                                 */
/* ------------------------------------------------------------------------- */

static void on_ble(ble_evt_t evt, const void *d)
{
    switch (evt) {
    case BLE_EVT_SYNCED:
        if (s_state == CS_BOOT) go_idle_or_scan();
        break;

    case BLE_EVT_ADV_UNPROV:
        handle_unprov_adv((const ble_adv_t *)d);
        break;

    case BLE_EVT_ADV_PROXY:
        handle_proxy_adv((const ble_adv_t *)d);
        break;

    case BLE_EVT_CONNECTED:
        if (s_state == CS_PAIR_CONNECTING) set_pair(PAIR_IN_PROGRESS, "Connected, starting provisioning", 25);
        break;

    case BLE_EVT_CONNECT_FAILED:
        if (s_state == CS_PAIR_CONNECTING) {
            pair_abort("Could not connect to the light");
        } else if (s_state == CS_PAIR_PROXY_CONNECTING) {
            start_proxy_scan(s_target_addr, true);
        } else {
            go_idle_or_scan();
        }
        break;

    case BLE_EVT_READY:
        if (s_state == CS_PAIR_CONNECTING) {
            if (mesh_prov_start(&s_prov, prov_send, NULL, s_net.netkey, 0, s_net.iv_index,
                                s_netcfg.next_addr) == 0) {
                set_state(CS_PAIR_PROV);
                set_pair(PAIR_IN_PROGRESS, "Provisioning", 30);
            } else {
                pair_abort("Could not start provisioning");
            }
        } else if (s_state == CS_PROXY_CONNECTING || s_state == CS_PAIR_PROXY_CONNECTING) {
            proxy_filter_setup();
            light_t *l = lights_find_by_addr(s_proxy_addr);
            if (s_state == CS_PAIR_PROXY_CONNECTING || (l && !l->configured)) {
                cfg_begin(l ? l->addr : s_target_addr);
            } else {
                set_state(CS_ONLINE);
                s_poll_ticks = STATUS_POLL_TICKS - 2;   /* poll soon */
                s_poll_index = 0;
                mesh_net_seg_tx_abort(&s_net);
                for (int i = 0; i < MESH_SEG_RX_SLOTS; i++) s_net.seg_rx[i].active = false;
            }
        }
        break;

    case BLE_EVT_DISCONNECTED:
        switch (s_state) {
        case CS_PAIR_DISCONNECTING:
            if (s_pending.unicast == 0 && s_pair_state == PAIR_FAILED) go_idle_or_scan();
            else pair_begin_scan();
            break;
        case CS_PAIR_CONNECTING:
        case CS_PAIR_PROV:
            pair_abort("Light disconnected during provisioning");
            break;
        case CS_PAIR_WAIT_DISC:
            set_pair(PAIR_IN_PROGRESS, "Looking for the light", 60);
            start_proxy_scan(s_pending.unicast, true);
            break;
        case CS_PAIR_CONFIG:
            set_pair(PAIR_FAILED, "Light disconnected during setup", 0);
            s_cfg.step = CFG_NONE;
            go_idle_or_scan();
            break;
        case CS_PAIR_PROXY_CONNECTING:
            start_proxy_scan(s_target_addr, true);
            break;
        default:
            go_idle_or_scan();
            break;
        }
        break;

    case BLE_EVT_PROXY_PDU: {
        const ble_proxy_pdu_t *pdu = (const ble_proxy_pdu_t *)d;
        switch (pdu->type) {
        case MESH_PROXY_TYPE_NETWORK:
            mesh_net_recv(&s_net, pdu->data, pdu->len);
            break;
        case MESH_PROXY_TYPE_BEACON:
            mesh_net_recv_beacon(&s_net, pdu->data, pdu->len);
            break;
        case MESH_PROXY_TYPE_PROXY_CFG: {
            uint8_t op, params[16];
            size_t plen;
            if (mesh_net_decode_proxy_cfg(&s_net, pdu->data, pdu->len, &op, params, &plen) == 0) {
                ESP_LOGI(TAG, "proxy cfg rx opcode 0x%02x len %u", op, (unsigned)plen);
            }
            break;
        }
        case MESH_PROXY_TYPE_PROVISIONING:
            handle_prov_pdu(pdu);
            break;
        }
        break;
    }
    }
}

/* ------------------------------------------------------------------------- */
/* commands                                                                   */
/* ------------------------------------------------------------------------- */

static void optimistic_update(uint16_t addr, const telink_status_t *st, bool apply_mode)
{
    app_core_lock();
    light_t *l = lights_find_by_addr(addr);
    if (l) {
        if (st->command_type == TELINK_CMD_ONOFF) {
            l->state.on = st->on;
        } else {
            l->state.intensity = st->intensity;
            l->state.level = s_master ? (uint16_t)((uint32_t)st->intensity * 1000 / s_master) : st->intensity;
            if (l->state.level > 1000) l->state.level = 1000;
            if (apply_mode) {
                l->state.mode = st->is_hsi ? LIGHT_MODE_HSI : LIGHT_MODE_CCT;
                if (st->is_hsi) { l->state.hue = st->hue; l->state.sat = st->sat; }
                else            { l->state.cct = st->cct_kelvin; l->state.gm = st->gm; }
            }
        }
    }
    app_core_unlock();
    if (l) ui_notify_light_state(l->addr);
}

static uint16_t scaled(uint16_t level)
{
    uint32_t v = (uint32_t)level * s_master / 1000;
    return v > 1000 ? 1000 : (uint16_t)v;
}

/* Send a light's whole look (mode + level + power).  Used by scenes and the
 * desk-wide commands.  Spaced so the proxy and the mesh keep up. */
static void send_look(light_t *l, bool on, light_mode_t mode, uint16_t level,
                      uint16_t cct, int gm, uint16_t hue, uint8_t sat)
{
    uint8_t p[10];
    uint16_t out = scaled(level);
    if (mode == LIGHT_MODE_HSI) telink_hsi(hue, sat, out, p);
    else                        telink_cct(cct, out, gm, p);
    send_telink(l->addr, p);
    vTaskDelay(pdMS_TO_TICKS(60));
    telink_onoff(on, p);
    send_telink(l->addr, p);
    vTaskDelay(pdMS_TO_TICKS(60));

    app_core_lock();
    l->state.on = on;
    l->state.mode = mode;
    l->state.level = level;
    l->state.intensity = out;
    if (mode == LIGHT_MODE_HSI) { l->state.hue = hue; l->state.sat = sat; }
    else                        { l->state.cct = cct; l->state.gm = (int8_t)gm; }
    app_core_unlock();
    ui_notify_light_state(l->addr);
}

static void apply_master(void)
{
    for (int i = 0; i < LIGHTS_MAX; i++) {
        light_t *l = &g_lights[i];
        if (!l->used) continue;
        uint8_t p[10];
        uint16_t out = scaled(l->state.level);
        telink_intensity(out, p);
        send_telink(l->addr, p);
        app_core_lock();
        l->state.intensity = out;
        app_core_unlock();
        ui_notify_light_state(l->addr);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void recall_scene(int idx)
{
    scene_t *sc = scenes_get(idx);
    if (!sc) return;
    ESP_LOGI(TAG, "recall scene %d '%s'", idx, sc->name);
    s_master = 1000;
    for (int i = 0; i < sc->count; i++) {
        const scene_entry_t *e = &sc->e[i];
        light_t *l = lights_find_by_addr(e->addr);
        if (!l) continue;
        send_look(l, e->on, (light_mode_t)e->mode, e->intensity, e->cct, e->gm, e->hue, e->sat);
    }
    s_last_scene = idx;
    ui_notify_lights_changed();
    s_poll_ticks = STATUS_POLL_TICKS - 3;
}

static void schedule_refresh(uint16_t addr)
{
    s_refresh_addr = addr;
    s_refresh_in = 3;
}

static void on_cmd(const app_event_t *ev)
{
    uint8_t p[10];
    telink_status_t st = { 0 };
    switch (ev->cmd.type) {
    case CMD_PAIR_START:
        if (s_state == CS_PAIR_SCAN) return;
        if (s_state != CS_NO_NET && s_state != CS_PROXY_SCAN && s_state != CS_ONLINE &&
            s_state != CS_PROXY_CONNECTING) {
            return;
        }
        ble_central_scan_stop();
        if (ble_central_is_connected()) {
            s_pending.unicast = 1;      /* non-zero: continue to pairing after disconnect */
            set_pair(PAIR_SCANNING, "Preparing", 0);
            set_state(CS_PAIR_DISCONNECTING);
            ble_central_disconnect();
        } else {
            pair_begin_scan();
        }
        break;

    case CMD_PAIR_SELECT: {
        if (s_state != CS_PAIR_SCAN) return;
        int idx = -1;
        for (int i = 0; i < s_unprov_n; i++) {
            if (memcmp(s_unprov[i].uuid, ev->cmd.uuid, 16) == 0) { idx = i; break; }
        }
        if (idx < 0) return;
        memcpy(s_pending.uuid, s_unprov[idx].uuid, 16);
        memcpy(s_pending.addr, s_unprov[idx].addr, 6);
        s_pending.addr_type = s_unprov[idx].addr_type;
        snprintf(s_pending.name, sizeof(s_pending.name), "%s", s_unprov[idx].name);
        s_pending.unicast = 0;
        ble_central_scan_stop();
        if (ble_central_connect(s_pending.addr, s_pending.addr_type, true) == 0) {
            set_state(CS_PAIR_CONNECTING);
            set_pair(PAIR_IN_PROGRESS, "Connecting to the light", 15);
        } else {
            pair_abort("Could not start the connection");
        }
        break;
    }

    case CMD_PAIR_CANCEL:
        if (s_state == CS_PAIR_SCAN) {
            ble_central_scan_stop();
            set_pair(PAIR_IDLE, "", 0);
            go_idle_or_scan();
        } else if (s_state == CS_PAIR_CONNECTING || s_state == CS_PAIR_PROV) {
            pair_abort("Cancelled");
        } else if (s_pair_state == PAIR_DONE || s_pair_state == PAIR_FAILED) {
            set_pair(PAIR_IDLE, "", 0);
        }
        break;

    case CMD_POWER:
        s_last_scene = -1;
        telink_onoff(ev->cmd.on, p);
        send_telink(ev->cmd.addr, p);
        st.command_type = TELINK_CMD_ONOFF; st.on = ev->cmd.on;
        optimistic_update(ev->cmd.addr, &st, false);
        schedule_refresh(ev->cmd.addr);
        break;

    case CMD_INTENSITY:
        telink_intensity(scaled(ev->cmd.a), p);
        send_telink(ev->cmd.addr, p);
        st.command_type = TELINK_CMD_INTENSITY; st.intensity = scaled(ev->cmd.a);
        optimistic_update(ev->cmd.addr, &st, false);
        schedule_refresh(ev->cmd.addr);
        break;

    case CMD_CCT:
        telink_cct(ev->cmd.a, scaled(ev->cmd.b), ev->cmd.c, p);
        send_telink(ev->cmd.addr, p);
        st.command_type = TELINK_CMD_CCT; st.is_hsi = false; st.cct_kelvin = ev->cmd.a;
        st.intensity = scaled(ev->cmd.b); st.gm = (int8_t)ev->cmd.c;
        optimistic_update(ev->cmd.addr, &st, true);
        schedule_refresh(ev->cmd.addr);
        break;

    case CMD_HSI:
        telink_hsi(ev->cmd.a, (uint8_t)ev->cmd.c, scaled(ev->cmd.b), p);
        send_telink(ev->cmd.addr, p);
        st.command_type = TELINK_CMD_HSI; st.is_hsi = true; st.hue = ev->cmd.a;
        st.sat = (uint8_t)ev->cmd.c; st.intensity = scaled(ev->cmd.b);
        optimistic_update(ev->cmd.addr, &st, true);
        schedule_refresh(ev->cmd.addr);
        break;

    case CMD_STATUS:
        send_status_request(ev->cmd.addr);
        break;

    case CMD_REMOVE: {
        light_t *l = lights_find_by_addr(ev->cmd.addr);
        if (!l) return;
        if (s_state == CS_ONLINE) {
            uint8_t msg[4];
            size_t n = mesh_cfg_build_node_reset(msg);
            mesh_net_send_access(&s_net, l->addr, 0xff, l->devkey, msg, n);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        bool was_proxy = (l->addr == s_proxy_addr);
        app_core_lock();
        lights_remove(l);
        app_core_unlock();
        save_lights();
        if (was_proxy && ble_central_is_connected()) {
            ble_central_disconnect();          /* DISCONNECTED -> rescan */
        } else if (lights_count() == 0 && s_state == CS_PROXY_SCAN) {
            ble_central_scan_stop();
            set_state(CS_NO_NET);
        }
        break;
    }

    case CMD_FACTORY_RESET:
        store_erase();
        esp_restart();
        break;

    case CMD_RENAME: {
        light_t *l = lights_find_by_addr(ev->cmd.addr);
        if (!l || !ev->cmd.name[0]) return;
        app_core_lock();
        snprintf(l->name, sizeof(l->name), "%s", ev->cmd.name);
        app_core_unlock();
        save_lights();
        break;
    }

    case CMD_MASTER:
        s_master = ev->cmd.a > 1000 ? 1000 : ev->cmd.a;
        apply_master();
        break;

    case CMD_ALL_POWER:
        for (int i = 0; i < LIGHTS_MAX; i++) {
            light_t *l = &g_lights[i];
            if (!l->used) continue;
            telink_onoff(ev->cmd.on, p);
            send_telink(l->addr, p);
            app_core_lock();
            l->state.on = ev->cmd.on;
            app_core_unlock();
            ui_notify_light_state(l->addr);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        s_last_scene = -1;
        s_poll_ticks = STATUS_POLL_TICKS - 3;
        break;

    case CMD_ALL_CCT:
        for (int i = 0; i < LIGHTS_MAX; i++) {
            light_t *l = &g_lights[i];
            if (!l->used) continue;
            uint16_t k = ev->cmd.a;
            if (k < l->cct_min) k = l->cct_min;
            if (k > l->cct_max) k = l->cct_max;
            telink_cct(k, scaled(l->state.level), 0, p);
            send_telink(l->addr, p);
            app_core_lock();
            l->state.mode = LIGHT_MODE_CCT;
            l->state.cct = k;
            l->state.gm = 0;
            l->state.intensity = scaled(l->state.level);
            app_core_unlock();
            ui_notify_light_state(l->addr);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        s_last_scene = -1;
        s_poll_ticks = STATUS_POLL_TICKS - 3;
        break;

    case CMD_SCENE_SAVE: {
        app_core_lock();
        int idx = scenes_capture(ev->cmd.c, NULL);
        app_core_unlock();
        if (idx >= 0) {
            store_save_scenes();
            s_last_scene = idx;
            ESP_LOGI(TAG, "scene %d saved", idx);
        }
        ui_notify_lights_changed();
        break;
    }

    case CMD_SCENE_RECALL:
        recall_scene(ev->cmd.c);
        break;

    case CMD_SCENE_DELETE:
        app_core_lock();
        scenes_delete(ev->cmd.c);
        app_core_unlock();
        store_save_scenes();
        if (s_last_scene == ev->cmd.c) s_last_scene = -1;
        ui_notify_lights_changed();
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* periodic tick                                                              */
/* ------------------------------------------------------------------------- */

static void on_tick(void)
{
    s_state_ticks++;
    mesh_net_tick(&s_net);
    mesh_prov_tick(&s_prov);
    cfg_tick();

    if (s_pair_dirty) {
        s_pair_dirty = false;
        ui_notify_pair_changed();
    }

    switch (s_state) {
    case CS_PROXY_SCAN:
    case CS_PAIR_PROXY_SCAN:
        if (s_state_ticks > PROXY_SCAN_TIMEOUT) {
            if (s_state == CS_PAIR_PROXY_SCAN) {
                set_pair(PAIR_FAILED, "Light did not come back - power-cycle it", 0);
                go_idle_or_scan();
            } else {
                ble_central_scan_stop();
                start_proxy_scan(0, false);
            }
        }
        break;
    case CS_PAIR_SCAN: {
        /* expire stale entries */
        uint32_t now = now_ms();
        app_core_lock();
        for (int i = 0; i < s_unprov_n;) {
            if (now - s_unprov[i].last_seen_ms > UNPROV_EXPIRE_MS) {
                memmove(&s_unprov[i], &s_unprov[i + 1], (s_unprov_n - i - 1) * sizeof(unprov_dev_t));
                s_unprov_n--;
                s_pair_dirty = true;
            } else {
                i++;
            }
        }
        app_core_unlock();
        break;
    }
    case CS_PAIR_PROV:
        if (s_prov.state == MESH_PROV_FAILED) pair_abort(s_prov.error_text ? s_prov.error_text : "Provisioning failed");
        break;
    case CS_PAIR_WAIT_DISC:
        if (s_state_ticks == 4 && ble_central_is_connected()) ble_central_disconnect();
        break;
    case CS_PAIR_CONNECTING:
    case CS_PROXY_CONNECTING:
    case CS_PAIR_PROXY_CONNECTING:
        if (s_state_ticks > 40) {   /* 16 s: NimBLE's own timeout should have fired; be safe */
            ble_central_disconnect();
        }
        break;
    case CS_ONLINE:
        if (s_refresh_in > 0 && --s_refresh_in == 0) {
            send_status_request(s_refresh_addr);
        }
        if (++s_poll_ticks >= STATUS_POLL_TICKS) {
            int n = lights_count();
            if (n) {
                int seen = 0;
                for (int i = 0; i < LIGHTS_MAX; i++) {
                    if (!g_lights[i].used) continue;
                    if (seen++ == s_poll_index) {
                        send_status_request(g_lights[i].addr);
                        break;
                    }
                }
                s_poll_index = (s_poll_index + 1) % n;
                s_poll_ticks = STATUS_POLL_TICKS - (STATUS_POLL_TICKS / n);
            }
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* task + start                                                               */
/* ------------------------------------------------------------------------- */

static void core_task(void *arg)
{
    (void)arg;
    app_event_t ev;
    for (;;) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        switch (ev.kind) {
        case EV_BLE:  on_ble(ev.ble.evt, &ev.ble.d); break;
        case EV_CMD:  on_cmd(&ev); break;
        case EV_TICK: on_tick(); break;
        }
    }
}

static void init_network(void)
{
    if (store_load_network(&s_netcfg) != 0) {
        ESP_LOGI(TAG, "creating a new mesh network");
        memset(&s_netcfg, 0, sizeof(s_netcfg));
        mesh_random(s_netcfg.netkey, 16);
        mesh_random(s_netcfg.appkey, 16);
        s_netcfg.iv_index = 0;
        s_netcfg.seq = 0;
        s_netcfg.next_addr = FIRST_LIGHT_ADDR;
        s_netcfg.valid = true;
        store_save_network(&s_netcfg);
    }
    store_load_lights();
    store_load_scenes();

    uint32_t seq = s_netcfg.seq + SEQ_BOOT_MARGIN;
    mesh_net_callbacks_t cb = {
        .tx = net_tx,
        .access_rx = net_access_rx,
        .devkey_lookup = net_devkey_lookup,
        .seg_done = net_seg_done,
    };
    mesh_net_init(&s_net, s_netcfg.netkey, s_netcfg.appkey, s_netcfg.iv_index, seq, OWN_ADDR, &cb);
    s_seq_saved = 0;
    maybe_save_seq();
    ESP_LOGI(TAG, "network ready: nid=0x%02x aid=0x%02x iv=%lu seq=%lu lights=%d next_addr=0x%04x",
             s_net.nid, s_net.aid, (unsigned long)s_net.iv_index, (unsigned long)s_net.seq,
             lights_count(), s_netcfg.next_addr);
}

void app_core_start(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    s_queue = xQueueCreate(24, sizeof(app_event_t));
    init_network();

    const esp_timer_create_args_t targs = { .callback = tick_cb, .name = "core_tick" };
    esp_timer_create(&targs, &s_tick_timer);
    esp_timer_start_periodic(s_tick_timer, TICK_MS * 1000);

    xTaskCreatePinnedToCore(core_task, "app_core", 8192, NULL, 6, NULL, 1);

    if (ble_central_init(ble_cb) != 0) {
        ESP_LOGE(TAG, "BLE init failed");
    }
}
