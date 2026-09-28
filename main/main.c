/*
 * MeshDeck — standalone touchscreen controller for Aputure / amaran lights
 * over Bluetooth Mesh (Sidus).  Waveshare ESP32-P4-WIFI6-Touch-LCD-4C.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_hosted.h"
#include "esp_hosted_misc.h"

#include "app_core.h"
#include "board.h"
#include "store.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "MeshDeck starting");
    store_init();

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
    if (esp_hosted_bt_controller_init() != ESP_OK)   ESP_LOGE(TAG, "hosted BT controller init failed");
    if (esp_hosted_bt_controller_enable() != ESP_OK) ESP_LOGE(TAG, "hosted BT controller enable failed");

    app_core_start();
}
