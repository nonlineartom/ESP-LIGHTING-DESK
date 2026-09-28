#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_core.h"

#ifndef MESHDECK_HOST_SIM
#include "esp_log.h"
#include "esp_lvgl_port.h"
static const char *TAG = "ui";
#define UI_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define UI_LOGI(...) do { } while (0)
#endif

const lv_font_t *ui_font_small, *ui_font_normal, *ui_font_medium, *ui_font_large, *ui_font_title, *ui_font_huge;
lv_color_t ui_col_bg, ui_col_panel, ui_col_panel_hi, ui_col_track, ui_col_accent, ui_col_accent_dark,
           ui_col_text, ui_col_muted, ui_col_ok, ui_col_warn;

static lv_obj_t *s_desk;
static lv_obj_t *s_pair_scr;
static enum { SCR_DESK, SCR_PAIR } s_current;

/* LVGL reports recognized multi-touch motion on the input device on every
 * sample.  Object gesture events can stop after the first recognition sample,
 * before the direction/distance has been populated. */
static void on_touch_gesture(lv_event_t *e)
{
    if (s_current != SCR_DESK ||
        lv_event_get_gesture_type(e) != LV_INDEV_GESTURE_TWO_FINGERS_SWIPE ||
        lv_event_get_gesture_state(e, LV_INDEV_GESTURE_TWO_FINGERS_SWIPE) != LV_INDEV_GESTURE_STATE_RECOGNIZED ||
        lv_event_get_two_fingers_swipe_distance(e) < 80) return;

    lv_dir_t dir = lv_event_get_two_fingers_swipe_dir(e);
    if (ui_scenes_is_open()) {
        if (dir == LV_DIR_BOTTOM) ui_scenes_set_open(false, true);
    } else if (ui_system_is_open()) {
        if (dir == LV_DIR_TOP) ui_system_set_open(false, true);
    } else {
        if (dir == LV_DIR_TOP) ui_scenes_set_open(true, true);
        else if (dir == LV_DIR_BOTTOM) ui_system_set_open(true, true);
    }
    /* A two finger navigation gesture must not click the control under the
     * primary contact when the fingers lift. */
    lv_indev_wait_release(lv_indev_active());
}

/* ------------------------------------------------------------------------- */
/* platform glue                                                              */
/* ------------------------------------------------------------------------- */

uint32_t ui_now_ms(void) { return lv_tick_get(); }

/* Set once ui_init() has built the screens.  If the display failed to come
 * up the firmware runs headless and every ui_notify_* is a no-op (LVGL and
 * its port mutex do not exist then). */
static bool s_ui_ready;

bool ui_lock(uint32_t timeout_ms)
{
    if (!s_ui_ready) return false;
#ifdef MESHDECK_HOST_SIM
    (void)timeout_ms;
    return true;
#else
    return lvgl_port_lock(timeout_ms);
#endif
}

void ui_unlock(void)
{
#ifndef MESHDECK_HOST_SIM
    lvgl_port_unlock();
#endif
}

/* ------------------------------------------------------------------------- */
/* geometry                                                                   */
/* ------------------------------------------------------------------------- */

int ui_chord_half_width(int y)
{
    int d = y - UI_CY;
    int r2 = UI_R * UI_R - d * d;
    return r2 > 0 ? (int)sqrt((double)r2) : 0;
}

/* ------------------------------------------------------------------------- */
/* widgets                                                                    */
/* ------------------------------------------------------------------------- */

lv_obj_t *ui_make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ui_col_bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, ui_col_text, 0);
    lv_obj_set_style_text_font(scr, ui_font_normal, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
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

lv_obj_t *ui_make_pill(lv_obj_t *parent, const char *text, int w, int h, lv_color_t bg, const lv_font_t *font)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, h / 2, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, lv_color_lighten(bg, 30), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font ? font : ui_font_normal, 0);
    lv_obj_center(lbl);
    return btn;
}

static void pulse_exec(void *obj, int32_t v) { lv_obj_set_style_shadow_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

void ui_pulse(lv_obj_t *obj, bool on)
{
    bool running = lv_anim_get(obj, pulse_exec) != NULL;
    if (on == running) return;
    if (!on) {
        lv_anim_delete(obj, pulse_exec);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, pulse_exec);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_90);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_reverse_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

lv_obj_t *ui_make_handle(lv_obj_t *parent)
{
    lv_obj_t *h = lv_obj_create(parent);
    lv_obj_set_size(h, 64, 6);
    lv_obj_set_style_radius(h, 3, 0);
    lv_obj_set_style_bg_color(h, ui_col_muted, 0);
    lv_obj_set_style_border_width(h, 0, 0);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return h;
}

/* ------------------------------------------------------------------------- */
/* animation helpers                                                          */
/* ------------------------------------------------------------------------- */

void ui_anim(lv_obj_t *obj, int32_t from, int32_t to, uint32_t ms, lv_anim_exec_xcb_t exec,
             lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void anim_set_y(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); }
static void anim_set_opa(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }
static void anim_set_scale(void *obj, int32_t v) { lv_obj_set_style_transform_scale((lv_obj_t *)obj, v, 0); }

void ui_anim_y(lv_obj_t *obj, int32_t to, uint32_t ms, lv_anim_path_cb_t path)
{
    lv_anim_delete(obj, anim_set_y);
    ui_anim(obj, lv_obj_get_y(obj), to, ms, anim_set_y, path);
}

void ui_anim_opa(lv_obj_t *obj, lv_opa_t to, uint32_t ms)
{
    lv_anim_delete(obj, anim_set_opa);
    ui_anim(obj, lv_obj_get_style_opa(obj, 0), to, ms, anim_set_opa, lv_anim_path_linear);
}

void ui_breathe(lv_obj_t *obj)
{
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, 256, 276);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_playback_duration(&a, 90);
    lv_anim_set_exec_cb(&a, anim_set_scale);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* colour crossfades keep the target colour in the object's user data */
typedef struct { lv_color_t from, to; } fade_t;
static void fade_bg_exec(void *obj, int32_t v)
{
    fade_t *f = (fade_t *)lv_obj_get_user_data((lv_obj_t *)obj);
    if (!f) return;
    lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_mix(f->to, f->from, (lv_opa_t)v), 0);
}
static void fade_sh_exec(void *obj, int32_t v)
{
    fade_t *f = (fade_t *)lv_obj_get_user_data((lv_obj_t *)obj);
    if (!f) return;
    lv_obj_set_style_shadow_color((lv_obj_t *)obj, lv_color_mix(f->to, f->from, (lv_opa_t)v), 0);
}

void ui_fade_bg(lv_obj_t *obj, lv_color_t to, uint32_t ms)
{
    fade_t *f = (fade_t *)lv_obj_get_user_data(obj);
    if (!f) {
        f = lv_malloc(sizeof(fade_t));
        f->from = f->to = lv_obj_get_style_bg_color(obj, 0);
        lv_obj_set_user_data(obj, f);
    }
    f->from = lv_obj_get_style_bg_color(obj, 0);
    f->to = to;
    lv_anim_delete(obj, fade_bg_exec);
    ui_anim(obj, 0, 255, ms, fade_bg_exec, lv_anim_path_linear);
}

void ui_fade_shadow(lv_obj_t *obj, lv_color_t to, uint32_t ms)
{
    fade_t *f = (fade_t *)lv_obj_get_user_data(obj);
    if (!f) {
        f = lv_malloc(sizeof(fade_t));
        f->from = f->to = lv_obj_get_style_shadow_color(obj, 0);
        lv_obj_set_user_data(obj, f);
    }
    /* shadow shares the same target as bg for preview discs */
    f->to = to;
    lv_anim_delete(obj, fade_sh_exec);
    ui_anim(obj, 0, 255, ms, fade_sh_exec, lv_anim_path_linear);
}

/* ------------------------------------------------------------------------- */
/* init + navigation                                                          */
/* ------------------------------------------------------------------------- */

void ui_init(void)
{
    ui_font_small  = &lv_font_montserrat_20;
    ui_font_normal = &lv_font_montserrat_24;
    ui_font_medium = &lv_font_montserrat_28;
    ui_font_large  = &lv_font_montserrat_36;
    ui_font_title  = &lv_font_montserrat_48;
    ui_font_huge   = &lv_font_montserrat_48;
    ui_col_bg          = lv_color_hex(0x0b0e14);
    ui_col_panel       = lv_color_hex(0x161b25);
    ui_col_panel_hi    = lv_color_hex(0x232a38);
    ui_col_track       = lv_color_hex(0x1e2430);
    ui_col_accent      = lv_color_hex(0xffb347);
    ui_col_accent_dark = lv_color_hex(0x1a1408);
    ui_col_text        = lv_color_hex(0xf3f4f6);
    ui_col_muted       = lv_color_hex(0x8a93a6);
    ui_col_ok          = lv_color_hex(0x4cd964);
    ui_col_warn        = lv_color_hex(0xff5f5f);

    /* dark default theme so containers/msgboxes inherit light text */
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_theme_t *th = lv_theme_default_init(disp, ui_col_accent, ui_col_panel_hi, true, ui_font_normal);
        lv_display_set_theme(disp, th);
    }

    s_desk = ui_make_screen();
    ui_desk_create(s_desk);
    ui_scenes_create(s_desk);
    ui_system_create(s_desk);
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER)
            lv_indev_add_event_cb(indev, on_touch_gesture, LV_EVENT_GESTURE, NULL);
    }
    lv_screen_load(s_desk);
    s_current = SCR_DESK;
    s_ui_ready = true;
    UI_LOGI("ui ready");
}

void ui_show_desk(void)
{
    if (s_current == SCR_DESK) return;
    s_current = SCR_DESK;
    ui_desk_rebuild();
    lv_screen_load_anim(s_desk, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 260, 0, false);
}

void ui_show_pair(void)
{
    s_current = SCR_PAIR;
    ui_system_set_open(false, false);
    ui_scenes_set_open(false, false);
    ui_pair_refresh();
    extern lv_obj_t *ui_pair_screen(void);
    s_pair_scr = ui_pair_screen();
    lv_screen_load_anim(s_pair_scr, LV_SCR_LOAD_ANIM_MOVE_TOP, 260, 0, false);
}

/* ------------------------------------------------------------------------- */
/* notifications from the application task                                   */
/* ------------------------------------------------------------------------- */

#define WITH_LOCK(body) do { if (ui_lock(200)) { body; ui_unlock(); } } while (0)

void ui_notify_link_changed(void)
{
    WITH_LOCK({
        ui_desk_refresh_link();
        ui_system_refresh();
        if (s_current == SCR_PAIR) ui_pair_refresh();
    });
}

void ui_notify_lights_changed(void)
{
    WITH_LOCK({
        ui_desk_rebuild();
        ui_scenes_refresh();
        ui_system_refresh();
    });
}

void ui_notify_light_state(uint16_t addr)
{
    WITH_LOCK({
        ui_desk_refresh_light(addr);
        ui_scenes_refresh();
    });
}

void ui_notify_pair_changed(void)
{
    WITH_LOCK({
        if (s_current == SCR_PAIR) ui_pair_refresh();
    });
}
