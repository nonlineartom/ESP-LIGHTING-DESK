/*
 * System sheet: drops from the top rim.  Link status, pairing, naming and
 * removing the current channel's light, factory reset.
 */
#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "ui.h"

#define SHEET_H        470
#define SHEET_OPEN_Y   0
#define SHEET_CLOSED_Y (-SHEET_H)

static lv_obj_t *s_sheet;
static lv_obj_t *s_link_icon, *s_link_text;
static lv_obj_t *s_btn_pair;
static lv_obj_t *s_name_btn, *s_name_lbl;
static lv_obj_t *s_btn_remove;
static lv_obj_t *s_btn_reset;
static lv_obj_t *s_hint;
static bool      s_open;

static const char *const k_names[] = {
    "Key", "Fill", "Back", "Rim", "Top", "Hair", "Kicker", "Background", "Practical", "Light",
};
#define NAME_COUNT (sizeof(k_names) / sizeof(k_names[0]))

static void on_pair(lv_event_t *e)
{
    (void)e;
    ui_system_set_open(false, false);
    app_cmd_pair_start();
    ui_show_pair();
}

static void on_name(lv_event_t *e)
{
    (void)e;
    uint16_t addr = ui_desk_current_addr();
    if (!addr) return;
    light_t copy;
    bool have = false;
    app_core_lock();
    light_t *l = lights_find_by_addr(addr);
    if (l) { copy = *l; have = true; }
    app_core_unlock();
    if (!have) return;
    /* cycle to the next preset name after the current one */
    int cur = -1;
    for (unsigned i = 0; i < NAME_COUNT; i++) if (strcmp(copy.name, k_names[i]) == 0) cur = (int)i;
    const char *next = k_names[(cur + 1) % NAME_COUNT];
    app_cmd_rename_light(addr, next);
    lv_label_set_text_fmt(s_name_lbl, "Name: %s   " LV_SYMBOL_RIGHT, next);
    ui_breathe(s_name_btn);
}

static void on_remove_confirm(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    uint16_t addr = ui_desk_current_addr();
    lv_msgbox_close(mbox);
    if (addr) app_cmd_remove_light(addr);
    ui_system_set_open(false, true);
}

static void on_remove(lv_event_t *e)
{
    (void)e;
    if (!ui_desk_current_addr()) return;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Remove this light?");
    lv_msgbox_add_text(mbox, "It is reset and can be paired again later.");
    lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, "Remove");
    lv_obj_set_style_bg_color(btn, ui_col_warn, 0);
    lv_obj_add_event_cb(btn, on_remove_confirm, LV_EVENT_CLICKED, mbox);
    lv_msgbox_add_close_button(mbox);
    lv_obj_set_width(mbox, 400);
    lv_obj_center(mbox);
}

static void on_reset_confirm(lv_event_t *e)
{
    (void)e;
    app_cmd_factory_reset();
}

static void on_reset(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Factory reset?");
    lv_msgbox_add_text(mbox, "Forgets every light and scene and creates a new mesh network. Lights must be re-paired.");
    lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, "Reset");
    lv_obj_set_style_bg_color(btn, ui_col_warn, 0);
    lv_obj_add_event_cb(btn, on_reset_confirm, LV_EVENT_CLICKED, NULL);
    lv_msgbox_add_close_button(mbox);
    lv_obj_set_width(mbox, 420);
    lv_obj_center(mbox);
}

static void on_sheet_gesture(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) ui_system_set_open(false, true);
}

static void on_handle(lv_event_t *e) { (void)e; ui_system_set_open(false, true); }

void ui_system_create(lv_obj_t *screen)
{
    s_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_sheet, UI_W, SHEET_H);
    lv_obj_set_pos(s_sheet, 0, SHEET_CLOSED_Y);
    lv_obj_set_style_bg_color(s_sheet, ui_col_bg, 0);
    lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_sheet, 56, 0);
    lv_obj_set_style_border_width(s_sheet, 2, 0);
    lv_obj_set_style_border_color(s_sheet, ui_col_panel_hi, 0);
    lv_obj_set_style_border_side(s_sheet, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_shadow_width(s_sheet, 40, 0);
    lv_obj_set_style_shadow_color(s_sheet, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_sheet, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_sheet, 0, 0);
    lv_obj_remove_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_sheet, on_sheet_gesture, LV_EVENT_GESTURE, NULL);

    s_link_icon = lv_label_create(s_sheet);
    lv_label_set_text(s_link_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(s_link_icon, ui_font_title, 0);
    lv_obj_align(s_link_icon, LV_ALIGN_TOP_MID, 0, 62);

    s_link_text = lv_label_create(s_sheet);
    lv_obj_set_style_text_font(s_link_text, ui_font_normal, 0);
    lv_obj_set_style_text_align(s_link_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_link_text, 340);
    lv_obj_align(s_link_text, LV_ALIGN_TOP_MID, 0, 112);

    s_btn_pair = ui_make_pill(s_sheet, LV_SYMBOL_PLUS "  Pair a light", 300, 56, ui_col_accent, ui_font_normal);
    lv_obj_set_style_text_color(s_btn_pair, ui_col_accent_dark, 0);
    lv_obj_align(s_btn_pair, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_add_event_cb(s_btn_pair, on_pair, LV_EVENT_CLICKED, NULL);

    s_name_btn = ui_make_pill(s_sheet, "Name", 360, 52, ui_col_panel, ui_font_normal);
    lv_obj_align(s_name_btn, LV_ALIGN_TOP_MID, 0, 234);
    lv_obj_add_event_cb(s_name_btn, on_name, LV_EVENT_CLICKED, NULL);
    s_name_lbl = lv_obj_get_child(s_name_btn, 0);

    s_btn_remove = ui_make_pill(s_sheet, LV_SYMBOL_TRASH "  Remove this light", 360, 52, ui_col_panel, ui_font_normal);
    lv_obj_set_style_text_color(s_btn_remove, ui_col_warn, 0);
    lv_obj_align(s_btn_remove, LV_ALIGN_TOP_MID, 0, 300);
    lv_obj_add_event_cb(s_btn_remove, on_remove, LV_EVENT_CLICKED, NULL);

    s_btn_reset = lv_label_create(s_sheet);
    lv_label_set_text(s_btn_reset, "Factory reset");
    lv_obj_set_style_text_font(s_btn_reset, ui_font_small, 0);
    lv_obj_set_style_text_color(s_btn_reset, ui_col_muted, 0);
    lv_obj_set_style_pad_all(s_btn_reset, 10, 0);
    lv_obj_add_flag(s_btn_reset, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_btn_reset, LV_ALIGN_TOP_MID, 0, 372);
    lv_obj_add_event_cb(s_btn_reset, on_reset, LV_EVENT_CLICKED, NULL);

    s_hint = lv_label_create(s_sheet);
    lv_label_set_text(s_hint, "MeshDeck");
    lv_obj_set_style_text_font(s_hint, ui_font_small, 0);
    lv_obj_set_style_text_color(s_hint, ui_col_track, 0);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *handle = ui_make_handle(s_sheet);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(handle, 30);
    lv_obj_add_event_cb(handle, on_handle, LV_EVENT_CLICKED, NULL);

    ui_system_refresh();
}

void ui_system_refresh(void)
{
    if (!s_sheet) return;
    link_state_t st = app_core_link_state();
    lv_color_t col = ui_col_muted;
    if (st == LINK_ONLINE) col = ui_col_ok;
    else if (st == LINK_SCANNING || st == LINK_CONNECTING || st == LINK_PAIRING) col = ui_col_accent;
    lv_obj_set_style_text_color(s_link_icon, col, 0);
    lv_label_set_text(s_link_text, app_core_link_text());

    uint16_t addr = ui_desk_current_addr();
    light_t copy;
    bool have = false;
    app_core_lock();
    light_t *l = addr ? lights_find_by_addr(addr) : NULL;
    if (l) { copy = *l; have = true; }
    app_core_unlock();
    if (have) {
        lv_obj_remove_flag(s_name_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_btn_remove, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_name_lbl, "Name: %s   " LV_SYMBOL_RIGHT, copy.name);
        lv_label_set_text_fmt(lv_obj_get_child(s_btn_remove, 0), LV_SYMBOL_TRASH "  Remove %s", copy.name);
    } else {
        lv_obj_add_flag(s_name_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_btn_remove, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_system_set_open(bool open, bool animate)
{
    if (!s_sheet) return;
    s_open = open;
    int y = open ? SHEET_OPEN_Y : SHEET_CLOSED_Y;
    if (animate) ui_anim_y(s_sheet, y, open ? 260 : 200, open ? lv_anim_path_overshoot : lv_anim_path_ease_in_out);
    else lv_obj_set_y(s_sheet, y);
    if (open) ui_system_refresh();
}

bool ui_system_is_open(void) { return s_open; }
