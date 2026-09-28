/*
 * Scenes drawer: rises from the bottom rim over the desk.  Tap = recall,
 * long-press = overwrite with the current look, bin = delete, "+" = save new.
 */
#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "ui.h"

#define DRAWER_H       600
#define DRAWER_OPEN_Y  174   /* keeps the channel title readable above the sheet */
#define DRAWER_CLOSED_Y UI_H
#define ROW_W          400
#define ROW_H          78

static lv_obj_t *s_drawer;
static lv_obj_t *s_list;
static lv_obj_t *s_title;
static lv_obj_t *s_rows[SCENES_MAX];
static lv_obj_t *s_row_name[SCENES_MAX];
static lv_obj_t *s_row_discs[SCENES_MAX][LIGHTS_MAX];
static lv_obj_t *s_row_del[SCENES_MAX];
static lv_obj_t *s_add;
static lv_obj_t *s_empty;
static lv_obj_t *s_fade_btn[4];
static const uint16_t k_fade_ms[4] = { 0, 1000, 2000, 5000 };
static const char *const k_fade_txt[4] = { "Snap", "1 s", "2 s", "5 s" };
static bool      s_open;
static int       s_pending_delete = -1;

static void on_row_clicked(lv_event_t *e)
{
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    app_cmd_scene_recall((int)idx);
    ui_breathe(s_rows[idx]);
}

static void on_row_long(lv_event_t *e)
{
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    app_cmd_scene_save((int)idx);
    /* flash the row amber to acknowledge */
    lv_obj_set_style_bg_color(s_rows[idx], ui_col_accent, 0);
    ui_fade_bg(s_rows[idx], ui_col_panel, 500);
}

static const char *const k_scene_names[] = {
    "Interview", "Daylight", "Golden hour", "Night", "Moody", "Studio", "Party", "Off",
};

static void rename_scene_done(const char *name, void *ctx)
{
    app_cmd_scene_rename((int)(uintptr_t)ctx, name);
}

static void on_row_rename(lv_event_t *e)
{
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    char cur[SCENE_NAME_LEN];
    app_core_lock();
    snprintf(cur, sizeof(cur), "%s", g_scenes[idx].name);
    app_core_unlock();
    ui_rename_open("Rename scene", cur, SCENE_NAME_LEN - 1, k_scene_names,
                   (int)(sizeof(k_scene_names) / sizeof(k_scene_names[0])), rename_scene_done, (void *)idx);
}

static void on_delete_confirm(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (s_pending_delete >= 0) app_cmd_scene_delete(s_pending_delete);
    s_pending_delete = -1;
    lv_msgbox_close(mbox);
}

static void on_row_delete(lv_event_t *e)
{
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    s_pending_delete = (int)idx;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Delete scene?");
    lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, "Delete");
    lv_obj_set_style_bg_color(btn, ui_col_warn, 0);
    lv_obj_add_event_cb(btn, on_delete_confirm, LV_EVENT_CLICKED, mbox);
    lv_msgbox_add_close_button(mbox);
    lv_obj_set_width(mbox, 360);
    lv_obj_center(mbox);
}

static void refresh_fade_buttons(void)
{
    uint16_t cur = app_core_fade_ms();
    for (int i = 0; i < 4; i++) {
        bool sel = (k_fade_ms[i] == cur);
        lv_obj_set_style_bg_color(s_fade_btn[i], sel ? ui_col_accent : ui_col_panel, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_fade_btn[i], 0), sel ? ui_col_accent_dark : ui_col_text, 0);
    }
}

static void on_fade(lv_event_t *e)
{
    uintptr_t i = (uintptr_t)lv_event_get_user_data(e);
    app_cmd_set_fade_ms(k_fade_ms[i]);
    refresh_fade_buttons();
}

static void on_add(lv_event_t *e)
{
    (void)e;
    app_cmd_scene_save(-1);
    ui_breathe(s_add);
}

static void on_handle(lv_event_t *e) { (void)e; ui_scenes_set_open(false, true); }

/* ------------------------------------------------------------------------- */

static lv_obj_t *make_row(int idx)
{
    lv_obj_t *row = lv_button_create(s_list);
    lv_obj_set_size(row, ROW_W, ROW_H);
    lv_obj_set_style_radius(row, ROW_H / 2, 0);
    lv_obj_set_style_bg_color(row, ui_col_panel, 0);
    lv_obj_set_style_bg_color(row, ui_col_panel_hi, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(row, ui_col_panel, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_event_cb(row, on_row_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)idx);
    lv_obj_add_event_cb(row, on_row_long, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)idx);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_style_text_font(name, ui_font_medium, 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 26, -13);
    s_row_name[idx] = name;

    for (int i = 0; i < LIGHTS_MAX; i++) {
        lv_obj_t *d = lv_obj_create(row);
        lv_obj_set_size(d, 18, 18);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(d, LV_ALIGN_LEFT_MID, 26 + i * 24, 16);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        s_row_discs[idx][i] = d;
    }

    lv_obj_t *del = lv_label_create(row);
    lv_label_set_text(del, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_color(del, ui_col_muted, 0);
    lv_obj_set_style_pad_all(del, 14, 0);
    lv_obj_add_flag(del, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_add_event_cb(del, on_row_delete, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);
    s_row_del[idx] = del;

    lv_obj_t *ren = lv_label_create(row);
    lv_label_set_text(ren, LV_SYMBOL_EDIT);
    lv_obj_set_style_text_color(ren, ui_col_muted, 0);
    lv_obj_set_style_pad_all(ren, 14, 0);
    lv_obj_add_flag(ren, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ren, LV_ALIGN_RIGHT_MID, -64, 0);
    lv_obj_add_event_cb(ren, on_row_rename, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);
    return row;
}

void ui_scenes_create(lv_obj_t *screen)
{
    s_drawer = lv_obj_create(screen);
    lv_obj_set_size(s_drawer, UI_W, DRAWER_H);
    lv_obj_set_pos(s_drawer, 0, DRAWER_CLOSED_Y);
    lv_obj_set_style_bg_color(s_drawer, ui_col_bg, 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_drawer, 56, 0);
    lv_obj_set_style_border_width(s_drawer, 2, 0);
    lv_obj_set_style_border_color(s_drawer, ui_col_panel_hi, 0);
    lv_obj_set_style_border_side(s_drawer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_shadow_width(s_drawer, 40, 0);
    lv_obj_set_style_shadow_color(s_drawer, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_drawer, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_drawer, 0, 0);
    lv_obj_remove_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *handle = ui_make_handle(s_drawer);
    lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(handle, 30);
    lv_obj_add_event_cb(handle, on_handle, LV_EVENT_CLICKED, NULL);

    s_title = lv_label_create(s_drawer);
    lv_label_set_text(s_title, "Scenes");
    lv_obj_set_style_text_font(s_title, ui_font_large, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 34);

    /* fade time selector: Snap / 1 s / 2 s / 5 s */
    lv_obj_t *fl = lv_label_create(s_drawer);
    lv_label_set_text(fl, "Fade");
    lv_obj_set_style_text_font(fl, ui_font_small, 0);
    lv_obj_set_style_text_color(fl, ui_col_muted, 0);
    lv_obj_align(fl, LV_ALIGN_TOP_MID, -180, 118);
    for (int i = 0; i < 4; i++) {
        s_fade_btn[i] = ui_make_pill(s_drawer, k_fade_txt[i], 78, 36, ui_col_panel, ui_font_small);
        lv_obj_align(s_fade_btn[i], LV_ALIGN_TOP_MID, -84 + i * 86, 108);
        lv_obj_add_event_cb(s_fade_btn[i], on_fade, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    s_list = lv_obj_create(s_drawer);
    lv_obj_set_size(s_list, ROW_W + 24, 330);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 154);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 6, 0);
    lv_obj_set_style_pad_row(s_list, 12, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_remove_flag(s_list, LV_OBJ_FLAG_GESTURE_BUBBLE);

    for (int i = 0; i < SCENES_MAX; i++) {
        s_rows[i] = make_row(i);
        lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_add = ui_make_pill(s_list, LV_SYMBOL_PLUS "  Save current look", ROW_W, 60, ui_col_panel_hi, ui_font_normal);
    lv_obj_add_event_cb(s_add, on_add, LV_EVENT_CLICKED, NULL);

    s_empty = lv_label_create(s_drawer);
    lv_label_set_text(s_empty, "No scenes yet.\nSet the lights, then save the look.");
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_empty, ui_col_muted, 0);
    lv_obj_set_style_text_font(s_empty, ui_font_small, 0);
    lv_obj_align(s_empty, LV_ALIGN_TOP_MID, 0, 198);
    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

    ui_scenes_refresh();
}

void ui_scenes_refresh(void)
{
    if (!s_drawer) return;
    refresh_fade_buttons();
    int last = app_core_last_scene();
    app_core_lock();
    int shown = 0;
    for (int i = 0; i < SCENES_MAX; i++) {
        const scene_t *sc = &g_scenes[i];
        if (!sc->used) { lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_row_name[i], sc->name);
        lv_obj_set_style_border_color(s_rows[i], i == last ? ui_col_accent : ui_col_panel, 0);
        lv_obj_set_style_text_color(s_row_name[i], i == last ? ui_col_accent : ui_col_text, 0);
        for (int k = 0; k < LIGHTS_MAX; k++) {
            if (k < sc->count) {
                light_t tmp = { 0 };
                tmp.state.on = sc->e[k].on;
                tmp.state.mode = sc->e[k].mode;
                tmp.state.intensity = sc->e[k].intensity;
                tmp.state.cct = sc->e[k].cct;
                tmp.state.gm = sc->e[k].gm;
                tmp.state.hue = sc->e[k].hue;
                tmp.state.sat = sc->e[k].sat;
                uint8_t r, g, b;
                light_preview_rgb(&tmp, &r, &g, &b);
                lv_obj_remove_flag(s_row_discs[i][k], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(s_row_discs[i][k], sc->e[k].on ? lv_color_make(r, g, b) : lv_color_hex(0x2a3140), 0);
            } else {
                lv_obj_add_flag(s_row_discs[i][k], LV_OBJ_FLAG_HIDDEN);
            }
        }
        shown++;
    }
    app_core_unlock();
    if (shown == 0) lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    if (shown >= SCENES_MAX) lv_obj_add_flag(s_add, LV_OBJ_FLAG_HIDDEN);
    else                     lv_obj_remove_flag(s_add, LV_OBJ_FLAG_HIDDEN);
    /* keep "+" as the last row */
    lv_obj_move_to_index(s_add, -1);
}

void ui_scenes_set_open(bool open, bool animate)
{
    if (!s_drawer) return;
    s_open = open;
    int y = open ? DRAWER_OPEN_Y : DRAWER_CLOSED_Y;
    if (animate) ui_anim_y(s_drawer, y, open ? 260 : 200, open ? lv_anim_path_overshoot : lv_anim_path_ease_in_out);
    else lv_obj_set_y(s_drawer, y);
    if (open) {
        ui_scenes_refresh();
        lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    }
}

bool ui_scenes_is_open(void) { return s_open; }
