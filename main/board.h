/*
 * Board bring-up for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4C:
 * 4-inch 720x720 round MIPI-DSI panel (JD9365), GT911-compatible touch on
 * I2C, PWM backlight, and the LVGL port task.
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_LCD_H_RES   720
#define BOARD_LCD_V_RES   720

esp_err_t     board_init(void);
lv_display_t *board_display(void);
void          board_backlight_set(int percent);
bool          board_lvgl_lock(uint32_t timeout_ms);
void          board_lvgl_unlock(void);

#ifdef __cplusplus
}
#endif
