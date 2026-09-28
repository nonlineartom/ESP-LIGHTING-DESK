/*
 * MeshDeck — standalone touchscreen controller for Aputure / amaran lights
 * over Bluetooth Mesh (Sidus).  Waveshare ESP32-P4-WIFI6-Touch-LCD-4C.
 */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_hosted.h"
#include "esp_hosted_misc.h"

#include "app_core.h"
#include "ble_central.h"
#include "board.h"
#include "store.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "MeshDeck starting (internal RAM free %u, largest block %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    store_init();
    app_core_init();

    if (board_init() != ESP_OK) {
        ESP_LOGE(TAG, "board init failed; continuing headless");
    } else if (board_lvgl_lock(1000)) {
        ui_init();
        board_lvgl_unlock();
    }

    /* Bring up the ESP32-C6 co-processor (SDIO) and its Bluetooth controller
     * before the NimBLE host starts on this chip. */
    int rc = esp_hosted_connect_to_slave();
    if (rc != 0) ESP_LOGE(TAG, "esp_hosted_connect_to_slave rc=%d", rc);

    /* The C6 image Waveshare ships (ESP-Hosted "0.0.0") already runs its BT
     * controller and does not answer the controller RPCs: each one costs a
     * 5 s timeout.  Remember that and skip them; if Bluetooth then fails to
     * come up (the C6 was reflashed), forget it and restart. */
    uint16_t skip_rpc = 0;
    store_load_u16("bt_rpc_skip", &skip_rpc);
    bool rpc_ok = true;
    if (!skip_rpc) {
        if (esp_hosted_bt_controller_init() != ESP_OK)   { ESP_LOGW(TAG, "hosted BT controller init: no answer"); rpc_ok = false; }
        if (esp_hosted_bt_controller_enable() != ESP_OK) { ESP_LOGW(TAG, "hosted BT controller enable: no answer"); rpc_ok = false; }
    } else {
        ESP_LOGI(TAG, "co-processor runs its BT controller already; skipping controller RPCs");
    }

    app_core_start();

    for (int i = 0; i < 100 && !ble_central_is_synced(); i++) vTaskDelay(pdMS_TO_TICKS(100));
    bool synced = ble_central_is_synced();
    if (!skip_rpc && !rpc_ok && synced) {
        store_save_u16("bt_rpc_skip", 1);
    } else if (skip_rpc && !synced) {
        ESP_LOGW(TAG, "Bluetooth did not start without the controller RPCs; retrying with them");
        store_save_u16("bt_rpc_skip", 0);
        esp_restart();
    } else if (!synced) {
        ESP_LOGE(TAG, "Bluetooth host did not sync with the co-processor");
    }
}
