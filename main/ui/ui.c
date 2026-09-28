#include "ui.h"

#include "app_core.h"
#include "board.h"
#include "esp_log.h"

static const char *TAG = "ui";

const lv_font_t *ui_font_small;
const lv_font_t *ui_font_normal;
const lv_font_t *ui_font_large;
const lv_font_t *ui_font_title;
lv_color_t ui_col_bg, ui_col_panel, ui_col_accent, ui_col_text, ui_col_muted, ui_col_ok, ui_col_warn;

static uint16_t s_control_addr;
static enum { SCR_HOME, SCR_PAIR, SCR_CONTROL } s_current;

/* ------------------------------------------------------------------------- */
/* theme + helpers                                                            */
/* ------------------------------------------------------------------------- */

lv_obj_t *ui_make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ui_col_bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, ui_col_text, 0);
    lv_obj_set_style_text_font(scr, ui_font_normal, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_make_round_button(lv_obj_t *parent, const char *symbol, int size, lv_color_t bg)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, lv_color_lighten(bg, 40), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, ui_font_large, 0);
    lv_obj_center(lbl);
    return btn;
}

lv_obj_t *ui_make_status_bar(lv_obj_t *screen)
{
    lv_obj_t *lbl = lv_label_create(screen);
    lv_obj_set_style_text_font(lbl, ui_font_small, 0);
    lv_obj_set_style_text_color(lbl, ui_col_muted, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 44);
    ui_update_status_label(lbl);
    return lbl;
}

void ui_update_status_label(lv_obj_t *label)
{
    link_state_t st = app_core_link_state();
    const char *sym = LV_SYMBOL_BLUETOOTH;
    lv_color_t col = ui_col_muted;
    switch (st) {
    case LINK_ONLINE:    col = ui_col_ok; break;
    case LINK_SCANNING:
    case LINK_CONNECTING: col = ui_col_accent; break;
    case LINK_PAIRING:   col = ui_col_accent; break;
    default: break;
    }
    lv_label_set_text_fmt(label, "%s  %s", sym, app_core_link_text());
    lv_obj_set_style_text_color(label, col, 0);
}

/* ------------------------------------------------------------------------- */
/* init + navigation                                                          */
/* ------------------------------------------------------------------------- */

void ui_init(void)
{
    ui_font_small  = &lv_font_montserrat_16;
    ui_font_normal = &lv_font_montserrat_20;
    ui_font_large  = &lv_font_montserrat_28;
    ui_font_title  = &lv_font_montserrat_36;
    ui_col_bg     = lv_color_hex(0x0b0e14);
    ui_col_panel  = lv_color_hex(0x181d27);
    ui_col_accent = lv_color_hex(0xffb347);
    ui_col_text   = lv_color_hex(0xf3f4f6);
    ui_col_muted  = lv_color_hex(0x8a93a6);
    ui_col_ok     = lv_color_hex(0x4cd964);
    ui_col_warn   = lv_color_hex(0xff5f5f);

    ui_show_home();
    ESP_LOGI(TAG, "ui ready");
}

void ui_show_home(void)
{
    s_current = SCR_HOME;
    ui_home_refresh();
}

void ui_show_pair(void)
{
    s_current = SCR_PAIR;
    ui_pair_refresh();
}

void ui_show_control(uint16_t addr)
{
    s_current = SCR_CONTROL;
    s_control_addr = addr;
    ui_control_refresh(addr);
}

/* ------------------------------------------------------------------------- */
/* notifications from the application task                                   */
/* ------------------------------------------------------------------------- */

#define WITH_LOCK(body) do { if (board_lvgl_lock(200)) { body; board_lvgl_unlock(); } } while (0)

void ui_notify_link_changed(void)
{
    WITH_LOCK({
        if (s_current == SCR_HOME) ui_home_refresh();
        else if (s_current == SCR_CONTROL) ui_control_refresh(s_control_addr);
        else ui_pair_refresh();
    });
}

void ui_notify_lights_changed(void)
{
    WITH_LOCK({
        if (s_current == SCR_HOME) ui_home_refresh();
        else if (s_current == SCR_CONTROL) ui_control_refresh(s_control_addr);
    });
}

void ui_notify_light_state(uint16_t addr)
{
    WITH_LOCK({
        if (s_current == SCR_HOME) ui_home_refresh();
        else if (s_current == SCR_CONTROL && addr == s_control_addr) ui_control_refresh(addr);
    });
}

void ui_notify_pair_changed(void)
{
    WITH_LOCK({
        if (s_current == SCR_PAIR) ui_pair_refresh();
    });
}
