/*
 * Control screen for one light: live preview disc, power, intensity arc,
 * CCT/G-M or HSI controls.  The preview reflects the light's reported state
 * (optimistically updated as soon as a command is sent).
 */
#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "esp_timer.h"
#include "ui.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_status;
static lv_obj_t *s_name;
static lv_obj_t *s_disc;
static lv_obj_t *s_arc;
static lv_obj_t *s_pct;
static lv_obj_t *s_power;
static lv_obj_t *s_power_lbl;
static lv_obj_t *s_tab_cct, *s_tab_hsi;
static lv_obj_t *s_box_cct, *s_box_hsi;
static lv_obj_t *s_sl_cct, *s_lbl_cct;
static lv_obj_t *s_sl_gm, *s_lbl_gm;
static lv_obj_t *s_sl_hue, *s_lbl_hue;
static lv_obj_t *s_sl_sat, *s_lbl_sat;
static lv_obj_t *s_report;

static uint16_t     s_addr;
static light_t      s_snapshot;        /* copy of the light for the UI */
static bool         s_have;
static light_mode_t s_ui_mode;         /* which tab is shown */
static uint32_t     s_last_send_ms;
static bool         s_refreshing;      /* suppress value-changed events while syncing */

#define SEND_THROTTLE_MS 120

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool throttled(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) return false;
    uint32_t t = now_ms();
    if (t - s_last_send_ms < SEND_THROTTLE_MS) return true;
    s_last_send_ms = t;
    return false;
}

/* --------------------------------------------------------------- actions */

static void send_cct(void)
{
    uint16_t k = (uint16_t)lv_slider_get_value(s_sl_cct);
    int gm = lv_slider_get_value(s_sl_gm);
    uint16_t v = (uint16_t)lv_arc_get_value(s_arc) * 10;
    app_cmd_set_cct(s_addr, k, v, gm);
}

static void send_hsi(void)
{
    uint16_t h = (uint16_t)lv_slider_get_value(s_sl_hue);
    uint8_t s = (uint8_t)lv_slider_get_value(s_sl_sat);
    uint16_t v = (uint16_t)lv_arc_get_value(s_arc) * 10;
    app_cmd_set_hsi(s_addr, h, s, v);
}

static void on_arc(lv_event_t *e)
{
    if (s_refreshing || throttled(e)) return;
    int pct = lv_arc_get_value(s_arc);
    lv_label_set_text_fmt(s_pct, "%d%%", pct);
    app_cmd_set_intensity(s_addr, (uint16_t)pct * 10);
}

static void on_power(lv_event_t *e)
{
    (void)e;
    bool on = s_have ? !s_snapshot.state.on : true;
    app_cmd_set_power(s_addr, on);
}

static void on_cct(lv_event_t *e)
{
    lv_label_set_text_fmt(s_lbl_cct, "%dK", (int)lv_slider_get_value(s_sl_cct));
    if (s_refreshing || throttled(e)) return;
    send_cct();
}

static void on_gm(lv_event_t *e)
{
    int gm = lv_slider_get_value(s_sl_gm);
    lv_label_set_text_fmt(s_lbl_gm, "G/M %s%d", gm > 0 ? "+" : "", gm);
    if (s_refreshing || throttled(e)) return;
    send_cct();
}

static void on_hue(lv_event_t *e)
{
    lv_label_set_text_fmt(s_lbl_hue, "Hue %d\xC2\xB0", (int)lv_slider_get_value(s_sl_hue));
    if (s_refreshing || throttled(e)) return;
    send_hsi();
}

static void on_sat(lv_event_t *e)
{
    lv_label_set_text_fmt(s_lbl_sat, "Sat %d%%", (int)lv_slider_get_value(s_sl_sat));
    if (s_refreshing || throttled(e)) return;
    send_hsi();
}

static void show_mode(light_mode_t m)
{
    s_ui_mode = m;
    bool cct = (m == LIGHT_MODE_CCT);
    if (cct) { lv_obj_clear_flag(s_box_cct, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_box_hsi, LV_OBJ_FLAG_HIDDEN); }
    else     { lv_obj_add_flag(s_box_cct, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_box_hsi, LV_OBJ_FLAG_HIDDEN); }
    lv_obj_set_style_bg_color(s_tab_cct, cct ? ui_col_accent : ui_col_panel, 0);
    lv_obj_set_style_text_color(s_tab_cct, cct ? lv_color_hex(0x1a1408) : ui_col_text, 0);
    lv_obj_set_style_bg_color(s_tab_hsi, !cct ? ui_col_accent : ui_col_panel, 0);
    lv_obj_set_style_text_color(s_tab_hsi, !cct ? lv_color_hex(0x1a1408) : ui_col_text, 0);
}

static void on_tab_cct(lv_event_t *e) { (void)e; show_mode(LIGHT_MODE_CCT); send_cct(); }
static void on_tab_hsi(lv_event_t *e) { (void)e; show_mode(LIGHT_MODE_HSI); send_hsi(); }

static void on_back(lv_event_t *e) { (void)e; ui_show_home(); }

static void on_remove_confirm(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    app_cmd_remove_light(s_addr);
    lv_msgbox_close(mbox);
    ui_show_home();
}

static void on_remove(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Remove light?");
    lv_msgbox_add_text(mbox, "The light is reset and can be paired again later.");
    lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, "Remove");
    lv_obj_add_event_cb(btn, on_remove_confirm, LV_EVENT_CLICKED, mbox);
    lv_msgbox_add_close_button(mbox);
    lv_obj_set_width(mbox, 420);
    lv_obj_center(mbox);
}

/* ------------------------------------------------------------------ build */

static lv_obj_t *make_slider(lv_obj_t *parent, int y, int min, int max, lv_event_cb_t cb, lv_obj_t **label)
{
    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_set_size(sl, 420, 22);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, y);
    lv_slider_set_range(sl, min, max);
    lv_obj_set_style_bg_color(sl, ui_col_panel, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, ui_col_accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ui_col_text, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_RELEASED, NULL);
    *label = lv_label_create(parent);
    lv_obj_set_style_text_font(*label, ui_font_small, 0);
    lv_obj_align_to(*label, sl, LV_ALIGN_OUT_TOP_MID, 0, -6);
    return sl;
}

static lv_obj_t *make_tab(lv_obj_t *parent, const char *text, int x, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 120, 44);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, ui_col_panel, 0);
    lv_obj_align(b, LV_ALIGN_TOP_MID, x, 392);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void build(void)
{
    s_scr = ui_make_screen();
    s_status = ui_make_status_bar(s_scr);

    lv_obj_t *back = ui_make_round_button(s_scr, LV_SYMBOL_LEFT, 64, ui_col_panel);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 120, 88);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    lv_obj_t *rm = ui_make_round_button(s_scr, LV_SYMBOL_TRASH, 64, ui_col_panel);
    lv_obj_align(rm, LV_ALIGN_TOP_RIGHT, -120, 88);
    lv_obj_add_event_cb(rm, on_remove, LV_EVENT_CLICKED, NULL);

    s_name = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_name, ui_font_large, 0);
    lv_obj_align(s_name, LV_ALIGN_TOP_MID, 0, 96);

    /* intensity arc around the preview disc */
    s_arc = lv_arc_create(s_scr);
    lv_obj_set_size(s_arc, 250, 250);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, 128);
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);
    lv_arc_set_range(s_arc, 0, 100);
    lv_obj_set_style_arc_width(s_arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, ui_col_panel, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, ui_col_accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_arc, ui_col_text, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_arc, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(s_arc, on_arc, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_arc, on_arc, LV_EVENT_RELEASED, NULL);

    s_disc = lv_obj_create(s_scr);
    lv_obj_set_size(s_disc, 168, 168);
    lv_obj_set_style_radius(s_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_disc, 3, 0);
    lv_obj_set_style_border_color(s_disc, lv_color_hex(0x2a3140), 0);
    lv_obj_set_style_shadow_width(s_disc, 60, 0);
    lv_obj_set_style_shadow_opa(s_disc, LV_OPA_70, 0);
    lv_obj_clear_flag(s_disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(s_disc, s_arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_disc, on_power, LV_EVENT_CLICKED, NULL);

    s_pct = lv_label_create(s_disc);
    lv_obj_set_style_text_font(s_pct, ui_font_title, 0);
    lv_obj_set_style_text_color(s_pct, lv_color_hex(0x0b0e14), 0);
    lv_obj_center(s_pct);

    s_power = ui_make_round_button(s_scr, LV_SYMBOL_POWER, 72, ui_col_panel);
    lv_obj_align(s_power, LV_ALIGN_TOP_MID, 190, 300);
    lv_obj_add_event_cb(s_power, on_power, LV_EVENT_CLICKED, NULL);
    s_power_lbl = lv_obj_get_child(s_power, 0);

    s_tab_cct = make_tab(s_scr, "CCT", -66, on_tab_cct);
    s_tab_hsi = make_tab(s_scr, "HSI", 66, on_tab_hsi);

    s_box_cct = lv_obj_create(s_scr);
    lv_obj_set_size(s_box_cct, 480, 190);
    lv_obj_align(s_box_cct, LV_ALIGN_TOP_MID, 0, 440);
    lv_obj_set_style_bg_opa(s_box_cct, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_box_cct, 0, 0);
    lv_obj_clear_flag(s_box_cct, LV_OBJ_FLAG_SCROLLABLE);
    s_sl_cct = make_slider(s_box_cct, 28, 2500, 7500, on_cct, &s_lbl_cct);
    s_sl_gm  = make_slider(s_box_cct, 110, -10, 10, on_gm, &s_lbl_gm);

    s_box_hsi = lv_obj_create(s_scr);
    lv_obj_set_size(s_box_hsi, 480, 190);
    lv_obj_align(s_box_hsi, LV_ALIGN_TOP_MID, 0, 440);
    lv_obj_set_style_bg_opa(s_box_hsi, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_box_hsi, 0, 0);
    lv_obj_clear_flag(s_box_hsi, LV_OBJ_FLAG_SCROLLABLE);
    s_sl_hue = make_slider(s_box_hsi, 28, 0, 360, on_hue, &s_lbl_hue);
    lv_obj_set_style_bg_color(s_sl_hue, lv_color_hex(0xff0000), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(s_sl_hue, lv_color_hex(0x0000ff), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(s_sl_hue, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sl_hue, LV_OPA_TRANSP, LV_PART_INDICATOR);
    s_sl_sat = make_slider(s_box_hsi, 110, 0, 100, on_sat, &s_lbl_sat);

    s_report = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_report, ui_font_small, 0);
    lv_obj_set_style_text_color(s_report, ui_col_muted, 0);
    lv_obj_align(s_report, LV_ALIGN_BOTTOM_MID, 0, -52);

    show_mode(LIGHT_MODE_CCT);
}

/* ---------------------------------------------------------------- refresh */

void ui_control_refresh(uint16_t addr)
{
    if (!s_scr) build();
    ui_update_status_label(s_status);
    s_addr = addr;

    app_core_lock();
    light_t *l = lights_find_by_addr(addr);
    if (l) { s_snapshot = *l; s_have = true; } else { s_have = false; }
    app_core_unlock();

    if (!s_have) {
        ui_show_home();
        return;
    }
    const light_state_t *st = &s_snapshot.state;
    s_refreshing = true;

    lv_label_set_text(s_name, s_snapshot.name);

    uint8_t r, g, b;
    light_preview_rgb(&s_snapshot, &r, &g, &b);
    lv_color_t c = lv_color_make(r, g, b);
    lv_obj_set_style_bg_color(s_disc, st->on ? c : lv_color_hex(0x1c2230), 0);
    lv_obj_set_style_shadow_color(s_disc, c, 0);
    lv_obj_set_style_shadow_opa(s_disc, st->on ? LV_OPA_70 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_pct, st->on ? lv_color_hex(0x0b0e14) : ui_col_muted, 0);

    if (!lv_obj_has_state(s_arc, LV_STATE_PRESSED)) {
        lv_arc_set_value(s_arc, st->intensity / 10);
        lv_label_set_text_fmt(s_pct, st->on ? "%u%%" : "off", st->intensity / 10);
    }
    lv_obj_set_style_bg_color(s_power, st->on ? ui_col_accent : ui_col_panel, 0);
    lv_obj_set_style_text_color(s_power_lbl, st->on ? lv_color_hex(0x1a1408) : ui_col_text, 0);

    /* follow the light's mode unless the user is mid-interaction */
    if (st->reported && st->mode != s_ui_mode &&
        !lv_obj_has_state(s_sl_cct, LV_STATE_PRESSED) && !lv_obj_has_state(s_sl_hue, LV_STATE_PRESSED)) {
        show_mode(st->mode);
    }
    lv_slider_set_range(s_sl_cct, s_snapshot.cct_min, s_snapshot.cct_max);
    if (!lv_obj_has_state(s_sl_cct, LV_STATE_PRESSED)) {
        lv_slider_set_value(s_sl_cct, st->cct, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_lbl_cct, "%uK", st->cct);
    }
    if (!lv_obj_has_state(s_sl_gm, LV_STATE_PRESSED)) {
        lv_slider_set_value(s_sl_gm, st->gm, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_lbl_gm, "G/M %s%d", st->gm > 0 ? "+" : "", st->gm);
    }
    if (!lv_obj_has_state(s_sl_hue, LV_STATE_PRESSED)) {
        lv_slider_set_value(s_sl_hue, st->hue, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_lbl_hue, "Hue %u\xC2\xB0", st->hue);
    }
    if (!lv_obj_has_state(s_sl_sat, LV_STATE_PRESSED)) {
        lv_slider_set_value(s_sl_sat, st->sat, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_lbl_sat, "Sat %u%%", st->sat);
    }
    if (s_snapshot.has_hsi) lv_obj_clear_flag(s_tab_hsi, LV_OBJ_FLAG_HIDDEN);
    else                    lv_obj_add_flag(s_tab_hsi, LV_OBJ_FLAG_HIDDEN);
    if (s_snapshot.has_gm)  lv_obj_clear_flag(s_sl_gm, LV_OBJ_FLAG_HIDDEN);
    else                    lv_obj_add_flag(s_sl_gm, LV_OBJ_FLAG_HIDDEN);

    if (!s_snapshot.configured) {
        lv_label_set_text(s_report, "Setup not finished - re-pair this light");
    } else if (!st->reported) {
        lv_label_set_text(s_report, "No status from the light yet");
    } else {
        uint32_t age = (now_ms() - st->last_report_ms) / 1000;
        lv_label_set_text_fmt(s_report, "Light reported %lus ago", (unsigned long)age);
    }

    s_refreshing = false;
    if (lv_screen_active() != s_scr) lv_screen_load(s_scr);
}
