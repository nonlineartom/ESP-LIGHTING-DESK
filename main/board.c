#include "board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "board";

/* Pins (Waveshare BSP esp32_p4_wifi6_touch_lcd_xc.h) */
#define PIN_I2C_SDA        GPIO_NUM_7
#define PIN_I2C_SCL        GPIO_NUM_8
#define PIN_LCD_BACKLIGHT  GPIO_NUM_26
#define PIN_LCD_RST        GPIO_NUM_27
#define I2C_PORT           1
#define I2C_SPEED_HZ       400000

#define LCD_LANES          2
#define LCD_LANE_MBPS      1500
#define LCD_DPI_CLK_MHZ    80
#define DSI_LDO_CHAN       3        /* LDO_VO3 -> VDD_MIPI_DPHY */
#define DSI_LDO_MV         2500
#define LCD_FB_COUNT       2

#define BL_LEDC_TIMER      LEDC_TIMER_1
#define BL_LEDC_CH         LEDC_CHANNEL_1

#include "board_lcd_init.inc"

static lv_display_t          *s_disp;
static i2c_master_bus_handle_t s_i2c;
static esp_lcd_touch_handle_t  s_touch;

/* ------------------------------------------------------------------------- */

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");
    const ledc_channel_config_t ch = {
        .gpio_num = PIN_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = { .output_invert = 1 },     /* backlight driver is active-low on this board */
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "ledc channel");
    return ESP_OK;
}

void board_backlight_set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (1023 * percent) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH);
}

static esp_err_t dsi_phy_power(void)
{
    static esp_ldo_channel_handle_t ldo;
    esp_ldo_channel_config_t cfg = {
        .chan_id = DSI_LDO_CHAN,
        .voltage_mv = DSI_LDO_MV,
    };
    return esp_ldo_acquire_channel(&cfg, &ldo);
}

static esp_err_t display_init(esp_lcd_panel_io_handle_t *io_out, esp_lcd_panel_handle_t *panel_out)
{
    ESP_RETURN_ON_ERROR(dsi_phy_power(), TAG, "DSI PHY LDO");

    esp_lcd_dsi_bus_handle_t bus;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = LCD_LANES,
        .phy_clk_src = 0,                 /* let IDF pick per silicon revision */
        .lane_bit_rate_mbps = LCD_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "dsi bus");

    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &io), TAG, "dbi io");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_DPI_CLK_MHZ,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = LCD_FB_COUNT,
        .video_timing = {
            .h_size = BOARD_LCD_H_RES,
            .v_size = BOARD_LCD_V_RES,
            .hsync_back_porch = 20,
            .hsync_pulse_width = 20,
            .hsync_front_porch = 40,
            .vsync_back_porch = 12,
            .vsync_pulse_width = 4,
            .vsync_front_porch = 24,
        },
        .flags.use_dma2d = true,
    };
    jd9365_vendor_config_t vendor = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = bus,
            .dpi_config = &dpi_cfg,
            .lane_num = LCD_LANES,
        },
    };
    esp_lcd_panel_dev_config_t dev_cfg = {
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = PIN_LCD_RST,
        .vendor_config = &vendor,
    };
    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(io, &dev_cfg, &panel), TAG, "jd9365");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "panel on");

    *io_out = io;
    *panel_out = panel;
    return ESP_OK;
}

static esp_err_t touch_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .i2c_port = I2C_PORT,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c), TAG, "i2c bus");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    if (i2c_master_probe(s_i2c, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 50) == ESP_OK) {
        ESP_LOGI(TAG, "touch at 0x5D");
    } else if (i2c_master_probe(s_i2c, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 50) == ESP_OK) {
        ESP_LOGI(TAG, "touch at 0x14");
        io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    } else {
        ESP_LOGE(TAG, "touch controller not found");
        return ESP_ERR_NOT_FOUND;
    }
    io_cfg.scl_speed_hz = I2C_SPEED_HZ;

    esp_lcd_panel_io_handle_t tp_io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &io_cfg, &tp_io), TAG, "touch io");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,      /* TP_RST strap must stay untouched (see BSP notes) */
        .int_gpio_num = GPIO_NUM_NC,      /* TP_INT is not routed to the P4 */
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    return esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(display_init(&io, &panel), TAG, "display");

    esp_err_t terr = touch_init();
    if (terr != ESP_OK) ESP_LOGW(TAG, "continuing without touch (%s)", esp_err_to_name(terr));

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack = 12288;
    port_cfg.task_affinity = 0;
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .control_handle = NULL,
        .buffer_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_dma = false,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = true,
            .direct_mode = false,
        },
    };
    lvgl_port_display_dsi_cfg_t dsi_cfg = { .flags.avoid_tearing = true };
    s_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl display add failed");
        return ESP_FAIL;
    }

    if (s_touch) {
        lvgl_port_touch_cfg_t touch_cfg = { .disp = s_disp, .handle = s_touch };
        if (!lvgl_port_add_touch(&touch_cfg)) ESP_LOGW(TAG, "lvgl touch add failed");
    }

    board_backlight_set(100);
    ESP_LOGI(TAG, "display ready");
    return ESP_OK;
}

lv_display_t *board_display(void) { return s_disp; }
bool board_lvgl_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void board_lvgl_unlock(void) { lvgl_port_unlock(); }
