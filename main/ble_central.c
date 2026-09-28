#include "ble_central.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_att.h"
#include "host/util/util.h"

static const char *TAG = "ble";

#define UUID_MESH_PROV_SVC     0x1827
#define UUID_MESH_PROV_IN      0x2ADB
#define UUID_MESH_PROV_OUT     0x2ADC
#define UUID_MESH_PROXY_SVC    0x1828
#define UUID_MESH_PROXY_IN     0x2ADD
#define UUID_MESH_PROXY_OUT    0x2ADE
#define UUID_CCCD              0x2902

static ble_evt_cb_t s_cb;
static uint8_t  s_own_addr_type;
static bool     s_synced;
static bool     s_scanning;

static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static bool     s_ready;
static bool     s_want_prov;           /* which service we are looking for */
static uint16_t s_svc_start, s_svc_end;
static uint16_t s_in_handle, s_out_handle, s_out_end;
static uint16_t s_cccd_handle;

/* SAR reassembly */
static uint8_t  s_rx_type;
static uint8_t  s_rx_buf[BLE_PROXY_PDU_MAX];
static size_t   s_rx_len;
static bool     s_rx_active;

static int gap_event(struct ble_gap_event *event, void *arg);

static void emit(ble_evt_t evt, const void *data, size_t len)
{
    if (s_cb) s_cb(evt, data, len);
}

/* ------------------------------------------------------------------------- */
/* scanning                                                                   */
/* ------------------------------------------------------------------------- */

/* Lights put their name in the scan response, which carries no service data,
 * so remember names per address and attach them to the mesh adverts. */
#define NAME_CACHE_N 16
static struct { uint8_t addr[6]; char name[BLE_ADV_NAME_LEN]; } s_names[NAME_CACHE_N];
static int s_names_next;

static void name_cache_put(const uint8_t addr[6], const uint8_t *name, size_t len)
{
    int slot = -1;
    for (int i = 0; i < NAME_CACHE_N; i++) {
        if (memcmp(s_names[i].addr, addr, 6) == 0 && s_names[i].name[0]) { slot = i; break; }
    }
    if (len > BLE_ADV_NAME_LEN - 1) len = BLE_ADV_NAME_LEN - 1;
    if (slot >= 0 && strlen(s_names[slot].name) == len && memcmp(s_names[slot].name, name, len) == 0) return;
    if (slot < 0) { slot = s_names_next; s_names_next = (s_names_next + 1) % NAME_CACHE_N; }
    memcpy(s_names[slot].addr, addr, 6);
    memcpy(s_names[slot].name, name, len);
    s_names[slot].name[len] = 0;
}

static const char *name_cache_get(const uint8_t addr[6])
{
    for (int i = 0; i < NAME_CACHE_N; i++) {
        if (s_names[i].name[0] && memcmp(s_names[i].addr, addr, 6) == 0) return s_names[i].name;
    }
    return NULL;
}

static void handle_disc(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) return;
    if (fields.name && fields.name_len) name_cache_put(disc->addr.val, fields.name, fields.name_len);
    if (!fields.svc_data_uuid16 || fields.svc_data_uuid16_len < 2) return;

    uint16_t uuid = (uint16_t)(fields.svc_data_uuid16[0] | (fields.svc_data_uuid16[1] << 8));
    const uint8_t *sd = fields.svc_data_uuid16 + 2;
    size_t sdlen = fields.svc_data_uuid16_len - 2;

    ble_adv_t adv;
    memset(&adv, 0, sizeof(adv));
    memcpy(adv.addr, disc->addr.val, 6);
    adv.addr_type = disc->addr.type;
    adv.rssi = disc->rssi;
    if (fields.name && fields.name_len) {
        size_t n = fields.name_len < BLE_ADV_NAME_LEN - 1 ? fields.name_len : BLE_ADV_NAME_LEN - 1;
        memcpy(adv.name, fields.name, n);
    } else {
        const char *cached = name_cache_get(disc->addr.val);
        if (cached) snprintf(adv.name, sizeof(adv.name), "%s", cached);
    }

    if (uuid == UUID_MESH_PROV_SVC && sdlen >= 16) {
        memcpy(adv.unprov.uuid, sd, 16);
        if (sdlen >= 18) adv.unprov.oob_info = (uint16_t)((sd[16] << 8) | sd[17]);
        emit(BLE_EVT_ADV_UNPROV, &adv, sizeof(adv));
    } else if (uuid == UUID_MESH_PROXY_SVC && sdlen >= 9) {
        adv.proxy.id_type = sd[0];
        size_t n = sdlen - 1;
        if (n > 16) n = 16;
        memcpy(adv.proxy.data, sd + 1, n);
        emit(BLE_EVT_ADV_PROXY, &adv, sizeof(adv));
    }
}

int ble_central_scan_start(void)
{
    if (!s_synced) return -1;
    if (s_scanning) return 0;
    struct ble_gap_disc_params params = {
        .itvl = 0x0060,           /* 60 ms */
        .window = 0x0050,         /* 50 ms */
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,             /* active: we want scan responses (names) */
        .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
        return -1;
    }
    s_scanning = true;
    return 0;
}

int ble_central_scan_stop(void)
{
    if (!s_scanning) return 0;
    ble_gap_disc_cancel();
    s_scanning = false;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* discovery chain                                                            */
/* ------------------------------------------------------------------------- */

static int on_cccd_written(uint16_t conn_handle, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle; (void)attr; (void)arg;
    if (error->status != 0) {
        ESP_LOGE(TAG, "CCCD write failed status=%d", error->status);
        ble_central_disconnect();
        return 0;
    }
    s_ready = true;
    ESP_LOGI(TAG, "notifications enabled (in=0x%04x out=0x%04x mtu=%u)",
             s_in_handle, s_out_handle, ble_att_mtu(s_conn));
    emit(BLE_EVT_READY, NULL, 0);
    return 0;
}

static int on_dsc(uint16_t conn_handle, const struct ble_gatt_error *error,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)conn_handle; (void)chr_val_handle; (void)arg;
    if (error->status == 0) {
        if (ble_uuid_u16(&dsc->uuid.u) == UUID_CCCD) s_cccd_handle = dsc->handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_cccd_handle) {
            ESP_LOGE(TAG, "no CCCD on data-out characteristic");
            ble_central_disconnect();
            return 0;
        }
        uint8_t v[2] = { 0x01, 0x00 };
        int rc = ble_gattc_write_flat(s_conn, s_cccd_handle, v, 2, on_cccd_written, NULL);
        if (rc) {
            ESP_LOGE(TAG, "CCCD write rc=%d", rc);
            ble_central_disconnect();
        }
        return 0;
    }
    ESP_LOGE(TAG, "descriptor discovery failed status=%d", error->status);
    ble_central_disconnect();
    return 0;
}

static int on_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn_handle; (void)arg;
    uint16_t in_uuid  = s_want_prov ? UUID_MESH_PROV_IN  : UUID_MESH_PROXY_IN;
    uint16_t out_uuid = s_want_prov ? UUID_MESH_PROV_OUT : UUID_MESH_PROXY_OUT;

    if (error->status == 0) {
        uint16_t u = ble_uuid_u16(&chr->uuid.u);
        if (u == in_uuid)  s_in_handle = chr->val_handle;
        if (u == out_uuid) s_out_handle = chr->val_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_in_handle || !s_out_handle) {
            ESP_LOGE(TAG, "mesh characteristics not found (in=%u out=%u)", s_in_handle, s_out_handle);
            ble_central_disconnect();
            return 0;
        }
        /* descriptors of the data-out characteristic live between its value
         * handle and the service end handle (or next characteristic) */
        s_cccd_handle = 0;
        s_out_end = s_svc_end;
        int rc = ble_gattc_disc_all_dscs(s_conn, s_out_handle, s_out_end, on_dsc, NULL);
        if (rc) {
            ESP_LOGE(TAG, "disc dscs rc=%d", rc);
            ble_central_disconnect();
        }
        return 0;
    }
    ESP_LOGE(TAG, "characteristic discovery failed status=%d", error->status);
    ble_central_disconnect();
    return 0;
}

static int on_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *service, void *arg)
{
    (void)conn_handle; (void)arg;
    if (error->status == 0) {
        s_svc_start = service->start_handle;
        s_svc_end = service->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_svc_start) {
            ESP_LOGE(TAG, "mesh service 0x%04x not found", s_want_prov ? UUID_MESH_PROV_SVC : UUID_MESH_PROXY_SVC);
            ble_central_disconnect();
            return 0;
        }
        s_in_handle = s_out_handle = 0;
        int rc = ble_gattc_disc_all_chrs(s_conn, s_svc_start, s_svc_end, on_chr, NULL);
        if (rc) {
            ESP_LOGE(TAG, "disc chrs rc=%d", rc);
            ble_central_disconnect();
        }
        return 0;
    }
    ESP_LOGE(TAG, "service discovery failed status=%d", error->status);
    ble_central_disconnect();
    return 0;
}

static int on_mtu(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    (void)conn_handle; (void)arg;
    if (error->status == 0) ESP_LOGI(TAG, "MTU %u", mtu);
    s_svc_start = s_svc_end = 0;
    ble_uuid16_t svc = BLE_UUID16_INIT(s_want_prov ? UUID_MESH_PROV_SVC : UUID_MESH_PROXY_SVC);
    int rc = ble_gattc_disc_svc_by_uuid(s_conn, &svc.u, on_svc, NULL);
    if (rc) {
        ESP_LOGE(TAG, "disc svc rc=%d", rc);
        ble_central_disconnect();
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* notifications: SAR reassembly                                              */
/* ------------------------------------------------------------------------- */

static void handle_notify(struct os_mbuf *om)
{
    uint8_t buf[BLE_PROXY_PDU_MAX + 1];
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len < 1 || len > sizeof(buf)) return;
    os_mbuf_copydata(om, 0, len, buf);

    uint8_t sar = buf[0] >> 6;
    uint8_t type = buf[0] & 0x3f;
    const uint8_t *payload = buf + 1;
    size_t plen = len - 1;

    switch (sar) {
    case 0:  /* complete */
        s_rx_active = false;
        if (plen <= BLE_PROXY_PDU_MAX) {
            ble_proxy_pdu_t pdu = { .type = type, .len = (uint8_t)plen };
            memcpy(pdu.data, payload, plen);
            emit(BLE_EVT_PROXY_PDU, &pdu, sizeof(pdu));
        }
        return;
    case 1:  /* first */
        s_rx_active = true;
        s_rx_type = type;
        s_rx_len = 0;
        break;
    case 2:  /* continuation */
    case 3:  /* last */
        if (!s_rx_active || s_rx_type != type) {
            s_rx_active = false;
            return;
        }
        break;
    }
    if (s_rx_len + plen > BLE_PROXY_PDU_MAX) {
        s_rx_active = false;
        return;
    }
    memcpy(s_rx_buf + s_rx_len, payload, plen);
    s_rx_len += plen;
    if (sar == 3) {
        s_rx_active = false;
        ble_proxy_pdu_t pdu = { .type = type, .len = (uint8_t)s_rx_len };
        memcpy(pdu.data, s_rx_buf, s_rx_len);
        emit(BLE_EVT_PROXY_PDU, &pdu, sizeof(pdu));
    }
}

/* ------------------------------------------------------------------------- */
/* GAP events                                                                 */
/* ------------------------------------------------------------------------- */

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        handle_disc(&event->disc);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scanning = false;
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            s_ready = false;
            s_rx_active = false;
            ESP_LOGI(TAG, "connected handle=%u", s_conn);
            emit(BLE_EVT_CONNECTED, NULL, 0);
            int rc = ble_gattc_exchange_mtu(s_conn, on_mtu, NULL);
            if (rc) {
                ESP_LOGW(TAG, "mtu exchange rc=%d, continuing", rc);
                struct ble_gatt_error e = { .status = 0 };
                on_mtu(s_conn, &e, 23, NULL);
            }
        } else {
            ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            emit(BLE_EVT_CONNECT_FAILED, NULL, 0);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_ready = false;
        emit(BLE_EVT_DISCONNECTED, NULL, 0);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.conn_handle == s_conn && event->notify_rx.attr_handle == s_out_handle) {
            handle_notify(event->notify_rx.om);
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        return 0;

    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* connection management                                                      */
/* ------------------------------------------------------------------------- */

int ble_central_connect(const uint8_t addr[6], uint8_t addr_type, bool provisioning_service)
{
    if (!s_synced) return -1;
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) return -1;
    ble_central_scan_stop();

    s_want_prov = provisioning_service;
    s_svc_start = s_svc_end = 0;
    s_in_handle = s_out_handle = s_cccd_handle = 0;

    ble_addr_t peer = { .type = addr_type };
    memcpy(peer.val, addr, 6);
    ESP_LOGI(TAG, "connecting to %02x:%02x:%02x:%02x:%02x:%02x (%s)",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
             provisioning_service ? "provisioning" : "proxy");
    int rc = ble_gap_connect(s_own_addr_type, &peer, 8000, NULL, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect rc=%d", rc);
        return -1;
    }
    return 0;
}

int ble_central_disconnect(void)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_conn_cancel();
        return 0;
    }
    int rc = ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0 && rc != BLE_HS_ENOTCONN) ESP_LOGW(TAG, "terminate rc=%d", rc);
    return 0;
}

bool ble_central_is_connected(void) { return s_conn != BLE_HS_CONN_HANDLE_NONE; }
bool ble_central_is_ready(void) { return s_ready; }

int ble_central_send_proxy_pdu(uint8_t type, const uint8_t *data, size_t len)
{
    if (!s_ready) return -1;
    uint16_t mtu = ble_att_mtu(s_conn);
    if (mtu < 23) mtu = 23;
    size_t chunk = (size_t)mtu - 3 - 1;   /* ATT header + proxy header */
    uint8_t buf[BLE_PROXY_PDU_MAX + 1];

    if (len <= chunk) {
        buf[0] = (uint8_t)(type & 0x3f);
        memcpy(buf + 1, data, len);
        return ble_gattc_write_no_rsp_flat(s_conn, s_in_handle, buf, len + 1) ? -1 : 0;
    }
    size_t off = 0;
    while (off < len) {
        size_t take = (len - off) < chunk ? (len - off) : chunk;
        uint8_t sar = (off == 0) ? 1 : ((off + take >= len) ? 3 : 2);
        buf[0] = (uint8_t)((sar << 6) | (type & 0x3f));
        memcpy(buf + 1, data + off, take);
        int rc = ble_gattc_write_no_rsp_flat(s_conn, s_in_handle, buf, take + 1);
        if (rc) return -1;
        off += take;
        vTaskDelay(pdMS_TO_TICKS(10));   /* let the ATT flow breathe between SAR fragments */
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* host init                                                                  */
/* ------------------------------------------------------------------------- */

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset reason=%d", reason);
    s_synced = false;
    s_scanning = false;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc) ESP_LOGE(TAG, "ensure_addr rc=%d", rc);
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc) ESP_LOGE(TAG, "infer_auto rc=%d", rc);
    s_synced = true;
    ESP_LOGI(TAG, "host synced, addr type %u", s_own_addr_type);
    emit(BLE_EVT_SYNCED, NULL, 0);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int ble_central_init(ble_evt_cb_t cb)
{
    s_cb = cb;
    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init rc=%d", rc);
        return -1;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    nimble_port_freertos_init(host_task);
    return 0;
}
