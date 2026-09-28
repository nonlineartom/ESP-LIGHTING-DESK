/*
 * BLE central on NimBLE: scanning for mesh devices, one GATT connection at a
 * time to either the Mesh Provisioning Service (0x1827) or the Mesh Proxy
 * Service (0x1828), and Proxy-PDU SAR (segmentation and reassembly).
 *
 * Callbacks run on the NimBLE host task; the application must hand them off
 * to its own task (app_core does that through a queue).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_ADV_NAME_LEN     24
#define BLE_PROXY_PDU_MAX    128

typedef enum {
    BLE_EVT_SYNCED = 1,      /* host stack ready */
    BLE_EVT_ADV_UNPROV,      /* ble_adv_t: unprovisioned device beacon (0x1827) */
    BLE_EVT_ADV_PROXY,       /* ble_adv_t: proxy service advert (0x1828) */
    BLE_EVT_CONNECTED,       /* connected, discovery started */
    BLE_EVT_CONNECT_FAILED,
    BLE_EVT_READY,           /* characteristics found and notifications enabled */
    BLE_EVT_DISCONNECTED,
    BLE_EVT_PROXY_PDU,       /* ble_proxy_pdu_t: complete (reassembled) proxy PDU */
} ble_evt_t;

typedef struct {
    uint8_t addr[6];         /* as reported by NimBLE (little-endian order) */
    uint8_t addr_type;
    int8_t  rssi;
    char    name[BLE_ADV_NAME_LEN];
    union {
        struct {
            uint8_t  uuid[16];
            uint16_t oob_info;
        } unprov;
        struct {
            uint8_t id_type;     /* 0 = Network ID, 1 = Node Identity */
            uint8_t data[16];    /* net id (8) or hash (8) + random (8) */
        } proxy;
    };
} ble_adv_t;

typedef struct {
    uint8_t type;            /* proxy PDU message type (0..3) */
    uint8_t len;
    uint8_t data[BLE_PROXY_PDU_MAX];
} ble_proxy_pdu_t;

typedef void (*ble_evt_cb_t)(ble_evt_t evt, const void *data, size_t len);

int  ble_central_init(ble_evt_cb_t cb);
int  ble_central_scan_start(void);
int  ble_central_scan_stop(void);
int  ble_central_connect(const uint8_t addr[6], uint8_t addr_type, bool provisioning_service);
int  ble_central_disconnect(void);
bool ble_central_is_connected(void);
bool ble_central_is_ready(void);
int  ble_central_send_proxy_pdu(uint8_t type, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
