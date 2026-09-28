/*
 * The desk: a horizontal snap carousel of pages — page 0 is the grand master,
 * the others are one channel per paired light.  Vertical faders flank the
 * preview; HSI adds a colour wheel around it.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_core.h"
#include "ui.h"

#define PAGES_MAX      (LIGHTS_MAX + 1)
#define DISC_SIZE      240
#define DISC_CY        330
#define SEND_THROTTLE  110
#define DOT_RADIUS     326

typedef struct {
    lv_obj_t *page;
    lv_obj_t *ring;           /* vertical intensity / master fader */
    lv_obj_t *level_label;
    lv_obj_t *disc;
    lv_obj_t *value;
    lv_obj_t *sub;            /* small line under the value (mode / status) */
    lv_obj_t *name;
    uint16_t  addr;           /* 0 = master */

    /* channel only */
    lv_obj_t *chip_cct, *chip_hsi;
    lv_obj_t *band_cct, *band_hsi;
    lv_obj_t *arc_cct, *arc_gm, *arc_sat;
    lv_obj_t *lbl_cct, *lbl_gm;
    lv_obj_t *card_gm;
    lv_obj_t *wheel, *wheel_knob, *sat_value;
    uint16_t hue_value;
    lv_point_t disc_press;
    bool disc_dragged;
    light_mode_t ui_mode;

    /* master only */
    lv_obj_t *mosaic[LIGHTS_MAX];
    lv_obj_t *btn_all_on, *btn_all_off, *btn_pair;
    lv_obj_t *preset[3];
} page_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_carousel;
static lv_obj_t *s_link;
static lv_obj_t *s_dots[PAGES_MAX];
static lv_obj_t *s_hint_up;
static page_t    s_pages[PAGES_MAX];
static int       s_page_count;
static int       s_current;
static uint32_t  s_last_send;
static bool      s_refreshing;

static uint32_t s_light_count_built;

/* ------------------------------------------------------------------------- */
/* helpers                                                                    */
/* ------------------------------------------------------------------------- */

static bool throttled(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) return false;
    uint32_t t = ui_now_ms();
    if (t - s_last_send < SEND_THROTTLE) return true;
    s_last_send = t;
    return false;
}

static light_t *page_light(page_t *pg, light_t *copy)
{
    app_core_lock();
    light_t *l = lights_find_by_addr(pg->addr);
    if (l) *copy = *l;
    app_core_unlock();
    return l ? copy : NULL;
}

static void place_polar(lv_obj_t *obj, int cx, int cy, int radius, double deg)
{
    double rad = deg * M_PI / 180.0;
    int w = lv_obj_get_width(obj), h = lv_obj_get_height(obj);
    lv_obj_set_pos(obj, cx + (int)lround(radius * cos(rad)) - w / 2, cy + (int)lround(radius * sin(rad)) - h / 2);
}

static void ring_anim_exec(void *obj, int32_t v) { lv_slider_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF); }

static void ring_set(lv_obj_t *arc, int value, bool animate)
{
    if (lv_obj_has_state(arc, LV_STATE_PRESSED)) return;
    lv_anim_delete(arc, ring_anim_exec);
    if (animate) ui_anim(arc, lv_slider_get_value(arc), value, 300, ring_anim_exec, lv_anim_path_ease_out);
    else lv_slider_set_value(arc, value, LV_ANIM_OFF);
}

static void slider_set(lv_obj_t *slider, int value)
{
    if (!lv_obj_has_state(slider, LV_STATE_PRESSED)) lv_slider_set_value(slider, value, LV_ANIM_OFF);
}

static void wheel_place_marker(page_t *pg)
{
    double a = pg->hue_value * M_PI / 180.0;
    lv_obj_set_pos(pg->wheel_knob,
                   170 + (int)lround(154 * sin(a)) - 15,
                   170 + (int)lround(154 * cos(a)) - 15);
}

static void wheel_draw(lv_event_t *e)
{
    lv_obj_t *wheel = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(wheel, &a);
    for (int i = 0; i < 72; i++) {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = a.x1 + 170;
        d.center.y = a.y1 + 170;
        d.radius = 154;
        d.width = 26;
        d.start_angle = i * 5;
        d.end_angle = (i + 1) * 5 + 1;
        d.color = lv_color_hsv_to_rgb(i * 5, 100, 100);
        lv_draw_arc(layer, &d);
    }
}

/* ------------------------------------------------------------------------- */
/* events                                                                     */
/* ------------------------------------------------------------------------- */

static void send_cct(page_t *pg)
{
    app_cmd_set_cct(pg->addr, (uint16_t)lv_slider_get_value(pg->arc_cct), (uint16_t)lv_slider_get_value(pg->ring) * 10,
                    lv_slider_get_value(pg->arc_gm));
}

static void send_hsi(page_t *pg)
{
    app_cmd_set_hsi(pg->addr, pg->hue_value, (uint8_t)lv_slider_get_value(pg->arc_sat),
                    (uint16_t)lv_slider_get_value(pg->ring) * 10);
}

static void on_ring(lv_event_t *e)
{
    page_t *pg = (page_t *)lv_event_get_user_data(e);
    int v = lv_slider_get_value(pg->ring);
    lv_label_set_text_fmt(pg->value, "%d%%", v);
    if (s_refreshing || throttled(e)) return;
    if (pg->addr == 0) app_cmd_master_level((uint16_t)v * 10);
    else               app_cmd_set_intensity(pg->addr, (uint16_t)v * 10);
}

static void on_disc(lv_event_t *e)
{
    page_t *pg = (page_t *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        pg->disc_dragged = false;
        lv_indev_get_point(lv_indev_active(), &pg->disc_press);
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        lv_point_t now;
        lv_indev_get_point(lv_indev_active(), &now);
        if (abs(now.x - pg->disc_press.x) > 24 || abs(now.y - pg->disc_press.y) > 24)
            pg->disc_dragged = true;
        return;
    }
    if (pg->disc_dragged) return;
    ui_breathe(pg->disc);
    if (pg->addr == 0) {
        /* master disc: toggle everything (on if any light is off) */
        bool any_off = false;
        app_core_lock();
        for (int i = 0; i < LIGHTS_MAX; i++) if (g_lights[i].used && !g_lights[i].state.on) any_off = true;
        app_core_unlock();
        app_cmd_all_power(any_off);
        return;
    }
    light_t l;
    if (page_light(pg, &l)) app_cmd_set_power(pg->addr, !l.state.on);
}

static void on_cct(lv_event_t *e)
{
    page_t *pg = (page_t *)lv_event_get_user_data(e);
    lv_label_set_text_fmt(pg->lbl_cct, "%dK", (int)lv_slider_get_value(pg->arc_cct));
    if (s_refreshing || throttled(e)) return;
    send_cct(pg);
}

static void on_gm(lv_event_t *e)
{
    page_t *pg = (page_t *)lv_event_get_user_data(e);
    int gm = lv_slider_get_value(pg->arc_gm);
    lv_label_set_text_fmt(pg->lbl_gm, "%s%d", gm > 0 ? "+" : "", gm);
    if (s_refreshing || throttled(e)) return;
    send_cct(pg);
}

static void on_wheel(lv_event_t *e)
{
    page_t *pg = (page_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(pg->wheel, &a);
    int dx = p.x - (a.x1 + 170), dy = p.y - (a.y1 + 170);
    int r2 = dx * dx + dy * dy;
    if (r2 < 126 * 126 || r2 > 182 * 182) return;
    double degrees = atan2((double)dx, (double)dy) * 180.0 / M_PI;
    if (degrees < 0) degrees += 360.0;
    pg->hue_value = (uint16_t)lround(degrees) % 360;
    wheel_place_marker(pg);
    lv_label_set_text_fmt(pg->sub, "%u\xC2\xB0  %d%%", pg->hue_value,
                          (int)lv_slider_get_value(pg->arc_sat));
    if (s_refreshing || throttled(e)) return;
    send_hsi(pg);
}

static void on_sat(lv_event_t *e)
{
    page_t *pg = (page_t *)lv_event_get_user_data(e);
    lv_label_set_text_fmt(pg->sat_value, "%d%%", (int)lv_slider_get_value(pg->arc_sat));
    if (s_refreshing || throttled(e)) return;
    send_hsi(pg);
}

static void show_mode(page_t *pg, light_mode_t m, bool animate)
{
    pg->ui_mode = m;
    bool cct = (m == LIGHT_MODE_CCT);
    lv_obj_t *show = cct ? pg->band_cct : pg->band_hsi;
    lv_obj_t *hide = cct ? pg->band_hsi : pg->band_cct;
    lv_obj_remove_flag(show, LV_OBJ_FLAG_HIDDEN);
    if (animate) {
        lv_obj_set_style_opa(show, LV_OPA_TRANSP, 0);
        ui_anim_opa(show, LV_OPA_COVER, 180);
        lv_obj_add_flag(hide, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_opa(show, LV_OPA_COVER, 0);
        lv_obj_add_flag(hide, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(pg->chip_cct, cct ? ui_col_accent : ui_col_panel, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(pg->chip_cct, 0), cct ? ui_col_accent_dark : ui_col_text, 0);
    lv_obj_set_style_bg_color(pg->chip_hsi, !cct ? ui_col_accent : ui_col_panel, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(pg->chip_hsi, 0), !cct ? ui_col_accent_dark : ui_col_text, 0);
    lv_obj_set_y(pg->chip_cct, cct ? 466 : 510);
    lv_obj_set_y(pg->chip_hsi, cct ? 466 : 510);
}

static void on_chip_cct(lv_event_t *e)
{
    page_t *pg = (page_t *)lv_event_get_user_data(e);
    if (pg->ui_mode == LIGHT_MODE_CCT) return;
    show_mode(pg, LIGHT_MODE_CCT, true);
    send_cct(pg);
}

static void on_chip_hsi(lv_event_t *e)
{
    page_t *pg = (page_t *)lv_event_get_user_data(e);
    if (pg->ui_mode == LIGHT_MODE_HSI) return;
    show_mode(pg, LIGHT_MODE_HSI, true);
    send_hsi(pg);
}

static void on_all_on(lv_event_t *e)  { (void)e; app_cmd_all_power(true); }
static void on_all_off(lv_event_t *e) { (void)e; app_cmd_all_power(false); }
static void on_pair(lv_event_t *e)    { (void)e; app_cmd_pair_start(); ui_show_pair(); }
static void on_scenes(lv_event_t *e)  { (void)e; ui_scenes_set_open(true, true); }
static void on_system(lv_event_t *e)  { (void)e; ui_system_set_open(true, true); }
static void on_preset(lv_event_t *e)
{
    uintptr_t k = (uintptr_t)lv_event_get_user_data(e);
    app_cmd_all_cct((uint16_t)k);
}

static void update_dots(void)
{
    for (int i = 0; i < PAGES_MAX; i++) {
        if (!s_dots[i]) continue;
        if (i >= s_page_count) { lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
        bool active = (i == s_current);
        lv_obj_set_size(s_dots[i], active ? 14 : 8, active ? 14 : 8);
        lv_obj_set_style_bg_color(s_dots[i], active ? ui_col_accent : ui_col_muted, 0);
        double span = 12.0 * (s_page_count - 1);
        double deg = 90.0 + span / 2 - 12.0 * i;
        place_polar(s_dots[i], UI_CX, UI_CY, DOT_RADIUS, deg);
    }
}

static void on_scroll_end(lv_event_t *e)
{
    (void)e;
    int x = lv_obj_get_scroll_x(s_carousel);
    int idx = (x + UI_W / 2) / UI_W;
    if (idx < 0) idx = 0;
    if (idx >= s_page_count) idx = s_page_count - 1;
    if (idx != s_current) {
        s_current = idx;
        update_dots();
        ui_system_refresh();
    }
}

/* ------------------------------------------------------------------------- */
/* page builders                                                              */
/* ------------------------------------------------------------------------- */

static lv_obj_t *make_page(void)
{
    lv_obj_t *p = lv_obj_create(s_carousel);
    lv_obj_set_size(p, UI_W, UI_H);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *make_page_title(lv_obj_t *page, const char *title, bool master)
{
    lv_obj_t *label = lv_label_create(page);
    lv_obj_set_width(label, 420);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, ui_font_large, 0);
    lv_obj_set_style_text_color(label, master ? ui_col_accent : ui_col_text, 0);
    lv_label_set_text(label, title);
    lv_obj_set_pos(label, 150, 124);
    return label;
}

static lv_obj_t *make_ring(lv_obj_t *page, int max, lv_event_cb_t cb, page_t *pg)
{
    pg->level_label = lv_label_create(page);
    lv_obj_set_width(pg->level_label, 110);
    lv_obj_set_pos(pg->level_label, 525, 142);
    lv_obj_set_style_text_align(pg->level_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(pg->level_label, ui_font_small, 0);
    lv_obj_set_style_text_color(pg->level_label, ui_col_muted, 0);
    lv_label_set_text(pg->level_label, "LEVEL");

    lv_obj_t *bar = lv_slider_create(page);
    lv_obj_set_size(bar, 32, 260);
    lv_obj_set_pos(bar, 564, 215);
    lv_slider_set_range(bar, 0, max);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, ui_col_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, ui_col_accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, ui_col_text, LV_PART_KNOB);
    lv_obj_set_style_pad_all(bar, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(bar, 20);
    lv_obj_add_event_cb(bar, cb, LV_EVENT_VALUE_CHANGED, pg);
    lv_obj_add_event_cb(bar, cb, LV_EVENT_RELEASED, pg);
    return bar;
}

static lv_obj_t *make_disc(lv_obj_t *page, page_t *pg)
{
    lv_obj_t *disc = lv_obj_create(page);
    lv_obj_set_size(disc, DISC_SIZE, DISC_SIZE);
    lv_obj_set_pos(disc, UI_CX - DISC_SIZE / 2, DISC_CY - DISC_SIZE / 2);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(disc, 3, 0);
    lv_obj_set_style_border_color(disc, lv_color_hex(0x2a3140), 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(0x1c2230), 0);
    lv_obj_set_style_shadow_width(disc, 70, 0);
    lv_obj_set_style_shadow_opa(disc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_color(disc, ui_col_accent, 0);
    lv_obj_set_style_pad_all(disc, 0, 0);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(disc, on_disc, LV_EVENT_PRESSED, pg);
    lv_obj_add_event_cb(disc, on_disc, LV_EVENT_PRESSING, pg);
    lv_obj_add_event_cb(disc, on_disc, LV_EVENT_CLICKED, pg);

    pg->value = lv_label_create(disc);
    lv_obj_set_style_text_font(pg->value, ui_font_huge, 0);
    lv_obj_set_style_text_color(pg->value, ui_col_bg, 0);
    lv_obj_align(pg->value, LV_ALIGN_CENTER, 0, -8);
    pg->sub = lv_label_create(disc);
    lv_obj_set_style_text_font(pg->sub, ui_font_small, 0);
    lv_obj_set_style_text_color(pg->sub, ui_col_bg, 0);
    lv_obj_align(pg->sub, LV_ALIGN_CENTER, 0, 30);
    return disc;
}

static lv_obj_t *make_band(lv_obj_t *page)
{
    lv_obj_t *b = lv_obj_create(page);
    lv_obj_set_size(b, UI_W, UI_H);
    lv_obj_set_pos(b, 0, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static void make_hsi_tools(page_t *pg)
{
    pg->band_hsi = make_band(pg->page);
    pg->wheel = lv_obj_create(pg->band_hsi);
    lv_obj_set_size(pg->wheel, 340, 340);
    lv_obj_set_pos(pg->wheel, 190, 160);
    lv_obj_set_style_bg_opa(pg->wheel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pg->wheel, 0, 0);
    lv_obj_set_style_pad_all(pg->wheel, 0, 0);
    lv_obj_remove_flag(pg->wheel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pg->wheel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pg->wheel, wheel_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(pg->wheel, on_wheel, LV_EVENT_PRESSED, pg);
    lv_obj_add_event_cb(pg->wheel, on_wheel, LV_EVENT_PRESSING, pg);
    lv_obj_add_event_cb(pg->wheel, on_wheel, LV_EVENT_RELEASED, pg);

    pg->wheel_knob = lv_obj_create(pg->wheel);
    lv_obj_set_size(pg->wheel_knob, 30, 30);
    lv_obj_set_style_radius(pg->wheel_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pg->wheel_knob, ui_col_text, 0);
    lv_obj_set_style_border_width(pg->wheel_knob, 3, 0);
    lv_obj_set_style_border_color(pg->wheel_knob, ui_col_bg, 0);
    lv_obj_remove_flag(pg->wheel_knob, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    wheel_place_marker(pg);

    lv_obj_t *caption = lv_label_create(pg->band_hsi);
    lv_label_set_text(caption, "SAT");
    lv_obj_set_width(caption, 100);
    lv_obj_set_pos(caption, 85, 142);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(caption, ui_font_small, 0);
    lv_obj_set_style_text_color(caption, ui_col_muted, 0);

    pg->arc_sat = lv_slider_create(pg->band_hsi);
    lv_obj_set_size(pg->arc_sat, 32, 260);
    lv_obj_set_pos(pg->arc_sat, 119, 215);
    lv_slider_set_range(pg->arc_sat, 0, 100);
    lv_obj_set_style_radius(pg->arc_sat, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(pg->arc_sat, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(pg->arc_sat, ui_col_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pg->arc_sat, lv_color_hex(0xe58abb), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(pg->arc_sat, ui_col_text, LV_PART_KNOB);
    lv_obj_set_style_pad_all(pg->arc_sat, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(pg->arc_sat, 18);
    lv_obj_add_event_cb(pg->arc_sat, on_sat, LV_EVENT_VALUE_CHANGED, pg);
    lv_obj_add_event_cb(pg->arc_sat, on_sat, LV_EVENT_RELEASED, pg);

    pg->sat_value = lv_label_create(pg->band_hsi);
    lv_obj_set_width(pg->sat_value, 100);
    lv_obj_set_pos(pg->sat_value, 85, 503);
    lv_obj_set_style_text_align(pg->sat_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(pg->sat_value, ui_font_small, 0);
    lv_obj_set_style_text_color(pg->sat_value, ui_col_text, 0);
}

static lv_obj_t *make_control(lv_obj_t *band, int x, const char *caption, int min, int max,
                              lv_color_t color, lv_event_cb_t cb, page_t *pg,
                              lv_obj_t **value, lv_obj_t **slider)
{
    lv_obj_t *card = lv_obj_create(band);
    lv_obj_set_size(card, 190, 98);
    lv_obj_set_pos(card, x, 520);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_bg_color(card, ui_col_panel, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, ui_col_panel_hi, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, caption);
    lv_obj_set_style_text_font(title, ui_font_small, 0);
    lv_obj_set_style_text_color(title, ui_col_muted, 0);
    lv_obj_set_pos(title, 16, 13);

    *value = lv_label_create(card);
    lv_obj_set_width(*value, 106);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(*value, ui_font_normal, 0);
    lv_obj_set_style_text_color(*value, ui_col_text, 0);
    lv_obj_set_pos(*value, 68, 10);

    *slider = lv_slider_create(card);
    lv_obj_set_size(*slider, 150, 14);
    lv_obj_set_pos(*slider, 20, 63);
    lv_slider_set_range(*slider, min, max);
    lv_obj_set_style_radius(*slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(*slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*slider, ui_col_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*slider, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*slider, ui_col_text, LV_PART_KNOB);
    lv_obj_set_style_pad_all(*slider, 7, LV_PART_KNOB);
    lv_obj_set_ext_click_area(*slider, 12);
    lv_obj_add_event_cb(*slider, cb, LV_EVENT_VALUE_CHANGED, pg);
    lv_obj_add_event_cb(*slider, cb, LV_EVENT_RELEASED, pg);
    return card;
}

static void build_channel(page_t *pg, const light_t *l)
{
    pg->page = make_page();
    pg->addr = l->addr;
    pg->ring = make_ring(pg->page, 100, on_ring, pg);
    pg->hue_value = l->state.hue;
    make_hsi_tools(pg);
    pg->disc = make_disc(pg->page, pg);
    pg->name = make_page_title(pg->page, l->name, false);

    pg->chip_cct = ui_make_pill(pg->page, "CCT", 100, 40, ui_col_panel, ui_font_normal);
    lv_obj_set_pos(pg->chip_cct, UI_CX - 108, 466);
    lv_obj_add_event_cb(pg->chip_cct, on_chip_cct, LV_EVENT_CLICKED, pg);
    pg->chip_hsi = ui_make_pill(pg->page, "HSI", 100, 40, ui_col_panel, ui_font_normal);
    lv_obj_set_pos(pg->chip_hsi, UI_CX + 8, 466);
    lv_obj_add_event_cb(pg->chip_hsi, on_chip_hsi, LV_EVENT_CLICKED, pg);

    /* Two fixed control cards keep values and touch targets clear of the rim. */
    pg->band_cct = make_band(pg->page);
    make_control(pg->band_cct, 165, "TEMP", l->cct_min, l->cct_max,
                 lv_color_hex(0xffcc7e), on_cct, pg, &pg->lbl_cct, &pg->arc_cct);
    pg->card_gm = make_control(pg->band_cct, 365, "G/M", -10, 10,
                               lv_color_hex(0x9ddd9d), on_gm, pg, &pg->lbl_gm, &pg->arc_gm);

    show_mode(pg, l->state.mode, false);
}

static void build_master(page_t *pg)
{
    pg->page = make_page();
    pg->addr = 0;
    pg->ring = make_ring(pg->page, 100, on_ring, pg);
    pg->disc = make_disc(pg->page, pg);
    lv_obj_set_style_bg_color(pg->disc, ui_col_panel, 0);
    pg->name = make_page_title(pg->page, "MASTER", true);

    /* mosaic of the lights inside the disc */
    for (int i = 0; i < LIGHTS_MAX; i++) {
        lv_obj_t *m = lv_obj_create(pg->disc);
        lv_obj_set_size(m, 30, 30);
        lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(m, 0, 0);
        lv_obj_set_style_shadow_width(m, 16, 0);
        lv_obj_set_style_shadow_opa(m, LV_OPA_50, 0);
        lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
        pg->mosaic[i] = m;
    }
    lv_obj_align(pg->value, LV_ALIGN_CENTER, 0, -22);
    lv_obj_set_style_text_font(pg->value, ui_font_huge, 0);
    lv_obj_set_style_text_color(pg->value, ui_col_text, 0);
    lv_obj_add_flag(pg->sub, LV_OBJ_FLAG_HIDDEN);

    pg->btn_all_on = ui_make_pill(pg->page, "All on", 124, 40, ui_col_panel, ui_font_normal);
    lv_obj_set_pos(pg->btn_all_on, UI_CX - 134, 468);
    lv_obj_add_event_cb(pg->btn_all_on, on_all_on, LV_EVENT_CLICKED, NULL);
    pg->btn_all_off = ui_make_pill(pg->page, "All off", 124, 40, ui_col_panel, ui_font_normal);
    lv_obj_set_pos(pg->btn_all_off, UI_CX + 10, 468);
    lv_obj_add_event_cb(pg->btn_all_off, on_all_off, LV_EVENT_CLICKED, NULL);
    pg->btn_pair = ui_make_pill(pg->page, LV_SYMBOL_PLUS "  Pair a light", 240, 56, ui_col_accent, ui_font_normal);
    lv_obj_set_pos(pg->btn_pair, 240, 496);
    lv_obj_set_style_text_color(lv_obj_get_child(pg->btn_pair, 0), ui_col_accent_dark, 0);
    lv_obj_add_event_cb(pg->btn_pair, on_pair, LV_EVENT_CLICKED, NULL);

    static const uint16_t presets[3] = { 3200, 4300, 5600 };
    for (int i = 0; i < 3; i++) {
        char t[8];
        snprintf(t, sizeof(t), "%dK", presets[i]);
        pg->preset[i] = ui_make_pill(pg->page, t, 104, 48, ui_col_panel, ui_font_small);
        lv_obj_set_pos(pg->preset[i], 192 + i * 116, 542);
        lv_obj_add_event_cb(pg->preset[i], on_preset, LV_EVENT_CLICKED, (void *)(uintptr_t)presets[i]);
    }
}

/* ------------------------------------------------------------------------- */
/* refresh                                                                    */
/* ------------------------------------------------------------------------- */

static void refresh_channel(page_t *pg, bool animate)
{
    light_t l;
    if (!page_light(pg, &l)) return;
    const light_state_t *st = &l.state;
    s_refreshing = true;

    uint8_t r, g, b;
    light_preview_rgb(&l, &r, &g, &b);
    lv_color_t c = lv_color_make(r, g, b);
    lv_color_t bg = st->on ? c : lv_color_hex(0x1c2230);
    if (animate) {
        ui_fade_bg(pg->disc, bg, 300);
        ui_fade_shadow(pg->disc, c, 300);
    } else {
        lv_obj_set_style_bg_color(pg->disc, bg, 0);
        lv_obj_set_style_shadow_color(pg->disc, c, 0);
    }
    lv_obj_set_style_shadow_opa(pg->disc, st->on ? LV_OPA_70 : LV_OPA_TRANSP, 0);
    int luminance = (r * 299 + g * 587 + b * 114) / 1000;
    lv_color_t ink = st->on ? (luminance < 150 ? ui_col_text : ui_col_bg) : ui_col_muted;
    lv_obj_set_style_text_color(pg->value, ink, 0);
    lv_obj_set_style_text_color(pg->sub, ink, 0);
    lv_label_set_text(pg->name, l.name);

    if (!lv_obj_has_state(pg->ring, LV_STATE_PRESSED)) {
        ring_set(pg->ring, st->level / 10, animate);
        lv_label_set_text_fmt(pg->value, st->on ? "%u%%" : "OFF", st->level / 10);
    }
    if (!l.configured)      lv_label_set_text(pg->sub, "setup incomplete");
    else if (!st->reported) lv_label_set_text(pg->sub, "no report yet");
    else if (st->mode == LIGHT_MODE_HSI) lv_label_set_text_fmt(pg->sub, "%u\xC2\xB0 %u%%", st->hue, st->sat);
    else lv_label_set_text_fmt(pg->sub, "%uK %s%d", st->cct, st->gm > 0 ? "+" : "", st->gm);

    if (st->reported && st->mode != pg->ui_mode && !lv_obj_has_state(pg->arc_cct, LV_STATE_PRESSED) &&
        !lv_obj_has_state(pg->wheel, LV_STATE_PRESSED)) {
        show_mode(pg, st->mode, animate);
    }
    lv_slider_set_range(pg->arc_cct, l.cct_min, l.cct_max);
    if (!lv_obj_has_state(pg->arc_cct, LV_STATE_PRESSED)) {
        slider_set(pg->arc_cct, st->cct);
        lv_label_set_text_fmt(pg->lbl_cct, "%uK", st->cct);
    }
    if (!lv_obj_has_state(pg->arc_gm, LV_STATE_PRESSED)) {
        slider_set(pg->arc_gm, st->gm);
        lv_label_set_text_fmt(pg->lbl_gm, "%s%d", st->gm > 0 ? "+" : "", st->gm);
    }
    if (!lv_obj_has_state(pg->wheel, LV_STATE_PRESSED)) {
        pg->hue_value = st->hue;
        wheel_place_marker(pg);
    }
    if (!lv_obj_has_state(pg->arc_sat, LV_STATE_PRESSED)) {
        slider_set(pg->arc_sat, st->sat);
        lv_label_set_text_fmt(pg->sat_value, "%u%%", st->sat);
    }
    if (l.has_hsi) lv_obj_remove_flag(pg->chip_hsi, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_add_flag(pg->chip_hsi, LV_OBJ_FLAG_HIDDEN);
    if (l.has_gm) lv_obj_remove_flag(pg->card_gm, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(pg->card_gm, LV_OBJ_FLAG_HIDDEN);
    s_refreshing = false;
}

static void refresh_master(page_t *pg, bool animate)
{
    s_refreshing = true;
    int n = 0, on = 0;
    app_core_lock();
    for (int i = 0; i < LIGHTS_MAX; i++) if (g_lights[i].used) n++;
    int k = 0;
    for (int i = 0; i < LIGHTS_MAX; i++) {
        const light_t *l = &g_lights[i];
        if (!l->used) continue;
        lv_obj_t *m = pg->mosaic[k];
        lv_obj_remove_flag(m, LV_OBJ_FLAG_HIDDEN);
        uint8_t r, g, b;
        light_preview_rgb(l, &r, &g, &b);
        lv_color_t c = l->state.on ? lv_color_make(r, g, b) : lv_color_hex(0x2a3140);
        if (animate) ui_fade_bg(m, c, 300); else lv_obj_set_style_bg_color(m, c, 0);
        lv_obj_set_style_shadow_color(m, c, 0);
        lv_obj_set_style_shadow_opa(m, l->state.on ? LV_OPA_50 : LV_OPA_TRANSP, 0);
        int dot_size = n > 6 ? 18 : 24;
        lv_obj_set_size(m, dot_size, dot_size);
        lv_obj_set_pos(m, (DISC_SIZE - n * (dot_size + 4) + 4) / 2 + k * (dot_size + 4), 168);
        if (l->state.on) on++;
        k++;
    }
    for (; k < LIGHTS_MAX; k++) lv_obj_add_flag(pg->mosaic[k], LV_OBJ_FLAG_HIDDEN);
    app_core_unlock();

    int master = app_core_master_level() / 10;
    if (!lv_obj_has_state(pg->ring, LV_STATE_PRESSED)) {
        ring_set(pg->ring, master, animate);
        lv_label_set_text_fmt(pg->value, "%d%%", master);
    }
    for (int i = 0; i < 3; i++) {
        if (n == 0) lv_obj_add_flag(pg->preset[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_remove_flag(pg->preset[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (n == 0) { lv_obj_add_flag(pg->btn_all_on, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(pg->btn_all_off, LV_OBJ_FLAG_HIDDEN); }
    else        { lv_obj_remove_flag(pg->btn_all_on, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(pg->btn_all_off, LV_OBJ_FLAG_HIDDEN); }
    if (n == 0) {
        lv_obj_add_flag(pg->ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pg->level_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(pg->btn_pair, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(pg->value, "");
        lv_obj_remove_flag(pg->sub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(pg->sub, ui_col_muted, 0);
        lv_label_set_text(pg->sub, "No lights paired");
        lv_obj_set_style_text_align(pg->sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(pg->sub, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_remove_flag(pg->ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(pg->level_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pg->btn_pair, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pg->sub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(pg->value, LV_ALIGN_CENTER, 0, -22);
        lv_obj_set_style_text_color(pg->value, on ? ui_col_text : ui_col_muted, 0);
    }
    s_refreshing = false;
}

void ui_desk_refresh_link(void)
{
    if (!s_link) return;
    link_state_t st = app_core_link_state();
    lv_color_t col = ui_col_muted;
    if (st == LINK_ONLINE) col = ui_col_ok;
    else if (st == LINK_SCANNING || st == LINK_CONNECTING || st == LINK_PAIRING) col = ui_col_accent;
    lv_label_set_text_fmt(s_link, LV_SYMBOL_BLUETOOTH "  %s  " LV_SYMBOL_DOWN, app_core_link_text());
    lv_obj_set_style_text_color(s_link, col, 0);
}

void ui_desk_refresh_light(uint16_t addr)
{
    for (int i = 0; i < s_page_count; i++) {
        if (s_pages[i].addr == addr) refresh_channel(&s_pages[i], true);
    }
    if (s_page_count) refresh_master(&s_pages[0], true);
}

uint16_t ui_desk_current_addr(void)
{
    return (s_current >= 0 && s_current < s_page_count) ? s_pages[s_current].addr : 0;
}

lv_obj_t *ui_desk_screen(void) { return s_scr; }

void ui_desk_goto(int page, bool animate)
{
    if (page < 0) page = 0;
    if (page >= s_page_count) page = s_page_count - 1;
    s_current = page;
    lv_obj_scroll_to_x(s_carousel, page * UI_W, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    update_dots();
    ui_system_refresh();
}

/* ------------------------------------------------------------------------- */
/* build / rebuild                                                            */
/* ------------------------------------------------------------------------- */

void ui_desk_rebuild(void)
{
    /* Rebuild only when the set of lights changed; otherwise refresh in place. */
    uint32_t sig = 0;
    app_core_lock();
    for (int i = 0; i < LIGHTS_MAX; i++) if (g_lights[i].used) sig = sig * 31 + g_lights[i].addr + 7;
    app_core_unlock();
    bool same = (sig == s_light_count_built) && s_page_count > 0;

    if (!same) {
        uint16_t keep_addr = ui_desk_current_addr();
        lv_obj_clean(s_carousel);
        memset(s_pages, 0, sizeof(s_pages));
        s_page_count = 0;
        build_master(&s_pages[s_page_count++]);
        app_core_lock();
        light_t copies[LIGHTS_MAX];
        int n = 0;
        for (int i = 0; i < LIGHTS_MAX; i++) if (g_lights[i].used) copies[n++] = g_lights[i];
        app_core_unlock();
        for (int i = 0; i < n; i++) build_channel(&s_pages[s_page_count++], &copies[i]);
        s_light_count_built = sig;
        s_current = 0;
        for (int i = 0; i < s_page_count; i++) if (s_pages[i].addr == keep_addr) s_current = i;
        lv_obj_update_layout(s_carousel);
        lv_obj_scroll_to_x(s_carousel, s_current * UI_W, LV_ANIM_OFF);
    }
    for (int i = 0; i < s_page_count; i++) {
        if (s_pages[i].addr) refresh_channel(&s_pages[i], false);
    }
    refresh_master(&s_pages[0], false);
    update_dots();
    ui_desk_refresh_link();
}

void ui_desk_create(lv_obj_t *screen)
{
    s_scr = screen;
    s_carousel = lv_obj_create(screen);
    lv_obj_set_size(s_carousel, UI_W, UI_H);
    lv_obj_set_pos(s_carousel, 0, 0);
    lv_obj_set_style_bg_opa(s_carousel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_carousel, 0, 0);
    lv_obj_set_style_pad_all(s_carousel, 0, 0);
    lv_obj_set_style_radius(s_carousel, 0, 0);
    lv_obj_set_flex_flow(s_carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(s_carousel, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_carousel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_carousel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_carousel, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_add_event_cb(s_carousel, on_scroll_end, LV_EVENT_SCROLL_END, NULL);

    s_link = lv_label_create(screen);
    lv_obj_set_style_text_font(s_link, ui_font_small, 0);
    lv_obj_set_style_text_color(s_link, ui_col_muted, 0);
    lv_obj_set_width(s_link, 430);
    lv_obj_set_style_text_align(s_link, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(s_link, 10, 0);
    lv_obj_align(s_link, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_add_flag(s_link, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_link, on_system, LV_EVENT_CLICKED, NULL);

    s_hint_up = ui_make_pill(screen, "SCENES  " LV_SYMBOL_UP, 172, 48, ui_col_panel_hi, ui_font_small);
    lv_obj_set_pos(s_hint_up, 274, 628);
    lv_obj_add_event_cb(s_hint_up, on_scenes, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < PAGES_MAX; i++) {
        lv_obj_t *d = lv_obj_create(screen);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_bg_color(d, ui_col_muted, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        s_dots[i] = d;
    }

    s_light_count_built = 0xffffffff;
    ui_desk_rebuild();
}
