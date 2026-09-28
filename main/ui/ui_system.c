/*
 * System sheet: drops from the top rim over a dimmed desk.  Link status,
 * pairing, and for the current channel's light: what it is (product, ID,
 * range, battery), rename, identify, remove.  Factory reset at the bottom.
 */
#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "products.h"
#include "ui.h"

#define SHEET_H        600
#define SHEET_OPEN_Y   0
#define SHEET_CLOSED_Y (-SHEET_H - 40)

static lv_obj_t *s_scrim;
static lv_obj_t *s_sheet;
static lv_obj_t *s_link_text;
static lv_obj_t *s_btn_pair;
static lv_obj_t *s_card, *s_card_name, *s_card_info, *s_card_info2;
static lv_obj_t *s_btn_rename, *s_btn_identify;
static lv_obj_t *s_btn_remove;
static lv_obj_t *s_btn_reset;
static lv_obj_t *s_title;
static lv_obj_t *s_diag_status;
static bool      s_open;

/* shared rename screen (lights and scenes) */
static lv_obj_t *s_rename, *s_rename_ta, *s_rename_title, *s_rename_chips;
static ui_rename_cb_t s_rename_cb;
static void          *s_rename_ctx;

static const char *const k_names[] = {
    "Key", "Fill", "Back", "Rim", "Hair", "Top", "Kicker", "Practical",
};
#define NAME_COUNT (sizeof(k_names) / sizeof(k_names[0]))

static bool current_light(light_t *copy)
{
    uint16_t addr = ui_desk_current_addr();
    if (!addr) return false;
    bool have = false;
    app_core_lock();
    light_t *l = lights_find_by_addr(addr);
    if (l) { *copy = *l; have = true; }
    app_core_unlock();
    return have;
}

/* ------------------------------------------------------------------------- */
/* rename screen                                                              */
/* ------------------------------------------------------------------------- */

static void rename_close(void)
{
    if (s_rename) lv_obj_add_flag(s_rename, LV_OBJ_FLAG_HIDDEN);
    s_rename_cb = NULL;
}

static void rename_commit(void)
{
    const char *txt = lv_textarea_get_text(s_rename_ta);
    while (*txt == ' ') txt++;
    ui_rename_cb_t cb = s_rename_cb;
    void *ctx = s_rename_ctx;
    rename_close();
    if (cb && txt[0]) cb(txt, ctx);
}

static void on_rename_kb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) rename_commit();
    else if (code == LV_EVENT_CANCEL) rename_close();
}

static void on_rename_save(lv_event_t *e)   { (void)e; rename_commit(); }
static void on_rename_cancel(lv_event_t *e) { (void)e; rename_close(); }

static void on_rename_chip(lv_event_t *e)
{
    lv_obj_t *chip = lv_event_get_target(e);
    lv_textarea_set_text(s_rename_ta, lv_label_get_text(lv_obj_get_child(chip, 0)));
}

static void build_rename(void)
{
    s_rename = lv_obj_create(ui_desk_screen());
    lv_obj_set_size(s_rename, UI_W, UI_H);
    lv_obj_set_pos(s_rename, 0, 0);
    lv_obj_set_style_bg_color(s_rename, ui_col_bg, 0);
    lv_obj_set_style_bg_opa(s_rename, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_rename, 0, 0);
    lv_obj_set_style_radius(s_rename, 0, 0);
    lv_obj_set_style_pad_all(s_rename, 0, 0);
    lv_obj_remove_flag(s_rename, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_rename_title = lv_label_create(s_rename);
    lv_obj_set_style_text_font(s_rename_title, ui_font_medium, 0);
    lv_obj_align(s_rename_title, LV_ALIGN_TOP_MID, 0, 62);

    s_rename_ta = lv_textarea_create(s_rename);
    lv_textarea_set_one_line(s_rename_ta, true);
    lv_obj_set_size(s_rename_ta, 440, 60);
    lv_obj_align(s_rename_ta, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_style_text_font(s_rename_ta, ui_font_medium, 0);
    lv_obj_set_style_bg_color(s_rename_ta, ui_col_panel, 0);
    lv_obj_set_style_border_color(s_rename_ta, ui_col_accent, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(s_rename_ta, 18, 0);

    /* suggestion chips, up to two rows of four */
    s_rename_chips = lv_obj_create(s_rename);
    lv_obj_set_size(s_rename_chips, 500, 108);
    lv_obj_align(s_rename_chips, LV_ALIGN_TOP_MID, 0, 188);
    lv_obj_set_style_bg_opa(s_rename_chips, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_rename_chips, 0, 0);
    lv_obj_set_style_pad_all(s_rename_chips, 0, 0);
    lv_obj_remove_flag(s_rename_chips, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *kb = lv_keyboard_create(s_rename);
    lv_obj_set_size(kb, 560, 250);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 306);
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(kb, ui_font_normal, 0);
    lv_keyboard_set_textarea(kb, s_rename_ta);
    lv_obj_add_event_cb(kb, on_rename_kb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, on_rename_kb, LV_EVENT_CANCEL, NULL);

    lv_obj_t *cancel = ui_make_pill(s_rename, "Cancel", 140, 52, ui_col_panel, ui_font_normal);
    lv_obj_set_pos(cancel, UI_CX - 150, 574);
    lv_obj_add_event_cb(cancel, on_rename_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save = ui_make_pill(s_rename, "Save", 140, 52, ui_col_accent, ui_font_normal);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), ui_col_accent_dark, 0);
    lv_obj_set_pos(save, UI_CX + 10, 574);
    lv_obj_add_event_cb(save, on_rename_save, LV_EVENT_CLICKED, NULL);

    lv_obj_add_flag(s_rename, LV_OBJ_FLAG_HIDDEN);
}

void ui_rename_open(const char *title, const char *initial, int max_len,
                    const char *const *chips, int nchips, ui_rename_cb_t cb, void *ctx)
{
    if (!s_rename) build_rename();
    lv_label_set_text(s_rename_title, title);
    lv_textarea_set_max_length(s_rename_ta, (uint32_t)max_len);
    lv_textarea_set_text(s_rename_ta, initial ? initial : "");
    lv_obj_clean(s_rename_chips);
    for (int i = 0; i < nchips && i < 8; i++) {
        lv_obj_t *c = ui_make_pill(s_rename_chips, chips[i], 112, 44, ui_col_panel_hi, ui_font_small);
        lv_obj_set_pos(c, 6 + (i % 4) * 122, (i / 4) * 54);
        lv_obj_add_event_cb(c, on_rename_chip, LV_EVENT_CLICKED, NULL);
    }
    s_rename_cb = cb;
    s_rename_ctx = ctx;
    lv_obj_remove_flag(s_rename, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_rename);
}

static void rename_light_done(const char *name, void *ctx)
{
    uint16_t addr = (uint16_t)(uintptr_t)ctx;
    app_cmd_rename_light(addr, name);
    if (s_card_name) lv_label_set_text(s_card_name, name);
}

static void on_rename(lv_event_t *e)
{
    (void)e;
    ui_system_show_rename();
}

void ui_system_show_rename(void)
{
    light_t l;
    if (!current_light(&l)) return;
    ui_rename_open("Rename light", l.name, LIGHT_NAME_LEN - 1, k_names, (int)NAME_COUNT,
                   rename_light_done, (void *)(uintptr_t)l.addr);
}

/* ------------------------------------------------------------------------- */
/* sheet actions                                                              */
/* ------------------------------------------------------------------------- */

static void on_pair(lv_event_t *e)
{
    (void)e;
    ui_system_set_open(false, false);
    app_cmd_pair_start();
    ui_show_pair();
}

static void on_identify(lv_event_t *e)
{
    (void)e;
    uint16_t addr = ui_desk_current_addr();
    if (addr) app_cmd_identify(addr);
    ui_breathe(s_btn_identify);
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

/* Hidden diagnostic: long-press "SYSTEM" to send a standard Light Lightness
 * Set with a 1 s transition to the current channel (alternating 50 % /
 * 100 %).  Watch the fixture and the serial log. */
static void on_diag(lv_event_t *e)
{
    (void)e;
    static bool high;
    uint16_t addr = ui_desk_current_addr();
    if (!addr) return;
    high = !high;
    app_cmd_diag_lightness(addr, high ? 0xFFFF : 0x8000, 10 /* 10 x 100 ms */);
    lv_label_set_text_fmt(s_diag_status, "Lightness test: %s", high ? "100%" : "50%");
    lv_obj_remove_flag(s_diag_status, LV_OBJ_FLAG_HIDDEN);
}

static void on_close(lv_event_t *e) { (void)e; ui_system_set_open(false, true); }

static lv_obj_t *centered_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int w, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

void ui_system_create(lv_obj_t *screen)
{
    /* dims the desk while the sheet is down; a tap anywhere on it closes */
    s_scrim = lv_obj_create(screen);
    lv_obj_set_size(s_scrim, UI_W, UI_H);
    lv_obj_set_pos(s_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_scrim, 0, 0);
    lv_obj_set_style_radius(s_scrim, 0, 0);
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_scrim, on_close, LV_EVENT_CLICKED, NULL);

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

    s_title = centered_label(s_sheet, ui_font_medium, ui_col_text, 300, 30);
    lv_label_set_text(s_title, "SYSTEM");
    lv_obj_set_style_pad_all(s_title, 8, 0);
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_title, on_diag, LV_EVENT_LONG_PRESSED, NULL);

    s_link_text = centered_label(s_sheet, ui_font_normal, ui_col_ok, 420, 88);

    s_btn_pair = ui_make_pill(s_sheet, LV_SYMBOL_PLUS "  Pair a light", 300, 56, ui_col_accent, ui_font_normal);
    lv_obj_set_style_text_color(lv_obj_get_child(s_btn_pair, 0), ui_col_accent_dark, 0);
    lv_obj_align(s_btn_pair, LV_ALIGN_TOP_MID, 0, 134);
    lv_obj_add_event_cb(s_btn_pair, on_pair, LV_EVENT_CLICKED, NULL);

    /* the current channel's light */
    s_card = lv_obj_create(s_sheet);
    lv_obj_set_size(s_card, 500, 116);
    lv_obj_align(s_card, LV_ALIGN_TOP_MID, 0, 208);
    lv_obj_set_style_radius(s_card, 26, 0);
    lv_obj_set_style_bg_color(s_card, ui_col_panel, 0);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_border_color(s_card, ui_col_panel_hi, 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_card_name = centered_label(s_card, ui_font_medium, ui_col_text, 460, 10);
    s_card_info = centered_label(s_card, ui_font_small, ui_col_muted, 460, 50);
    s_card_info2 = centered_label(s_card, ui_font_small, ui_col_muted, 460, 78);

    s_btn_rename = ui_make_pill(s_sheet, LV_SYMBOL_EDIT "  Rename", 200, 56, ui_col_panel_hi, ui_font_normal);
    lv_obj_set_pos(s_btn_rename, UI_CX - 210, 340);
    lv_obj_add_event_cb(s_btn_rename, on_rename, LV_EVENT_CLICKED, NULL);
    s_btn_identify = ui_make_pill(s_sheet, LV_SYMBOL_EYE_OPEN "  Identify", 200, 56, ui_col_panel_hi, ui_font_normal);
    lv_obj_set_pos(s_btn_identify, UI_CX + 10, 340);
    lv_obj_add_event_cb(s_btn_identify, on_identify, LV_EVENT_CLICKED, NULL);

    s_btn_remove = ui_make_pill(s_sheet, LV_SYMBOL_TRASH "  Remove", 410, 52, ui_col_panel, ui_font_normal);
    lv_obj_set_style_text_color(lv_obj_get_child(s_btn_remove, 0), ui_col_warn, 0);
    lv_obj_align(s_btn_remove, LV_ALIGN_TOP_MID, 0, 410);
    lv_obj_add_event_cb(s_btn_remove, on_remove, LV_EVENT_CLICKED, NULL);

    s_btn_reset = centered_label(s_sheet, ui_font_small, ui_col_muted, 220, 478);
    lv_label_set_text(s_btn_reset, "Factory reset");
    lv_obj_set_style_pad_all(s_btn_reset, 10, 0);
    lv_obj_add_flag(s_btn_reset, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_btn_reset, on_reset, LV_EVENT_CLICKED, NULL);

    s_diag_status = centered_label(s_sheet, ui_font_small, ui_col_warn, 420, 526);
    lv_obj_add_flag(s_diag_status, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *handle = ui_make_handle(s_sheet);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(handle, 30);
    lv_obj_add_event_cb(handle, on_close, LV_EVENT_CLICKED, NULL);

    ui_system_refresh();
}

void ui_system_refresh(void)
{
    if (!s_sheet) return;
    link_state_t st = app_core_link_state();
    lv_color_t col = ui_col_muted;
    if (st == LINK_ONLINE) col = ui_col_ok;
    else if (st == LINK_SCANNING || st == LINK_CONNECTING || st == LINK_PAIRING) col = ui_col_accent;
    lv_obj_set_style_text_color(s_link_text, col, 0);
    lv_label_set_text_fmt(s_link_text, LV_SYMBOL_BLUETOOTH "  %s", app_core_link_text());

    light_t l;
    lv_obj_t *per_light[] = { s_card, s_btn_rename, s_btn_identify, s_btn_remove };
    if (!current_light(&l)) {
        for (unsigned i = 0; i < sizeof(per_light) / sizeof(per_light[0]); i++)
            lv_obj_add_flag(per_light[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    for (unsigned i = 0; i < sizeof(per_light) / sizeof(per_light[0]); i++)
        lv_obj_remove_flag(per_light[i], LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(s_card_name, l.name);
    const char *product = lights_product_name(&l);
    char tail[7];
    if (product && sidus_uuid_mac_tail(l.uuid, tail))
        lv_label_set_text_fmt(s_card_info, "%s  \xE2\x80\xA2  ID %s", product, tail);
    else
        lv_label_set_text_fmt(s_card_info, "Mesh address 0x%04X", l.addr);

    char extra[64];
    int n = snprintf(extra, sizeof(extra), "%u-%uK", l.cct_min, l.cct_max);
    if (l.has_hsi) n += snprintf(extra + n, sizeof(extra) - n, "  \xE2\x80\xA2  colour");
    if (l.fx_mask) n += snprintf(extra + n, sizeof(extra) - n, "  \xE2\x80\xA2  effects");
    if (l.state.batt_pct >= 0)
        snprintf(extra + n, sizeof(extra) - n, "  \xE2\x80\xA2  %s %d%%",
                 l.state.ext_power ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_2, l.state.batt_pct);
    lv_label_set_text(s_card_info2, extra);
    lv_label_set_text_fmt(lv_obj_get_child(s_btn_remove, 0), LV_SYMBOL_TRASH "  Remove %s", l.name);
    bool online = (st == LINK_ONLINE);
    if (online) lv_obj_remove_state(s_btn_identify, LV_STATE_DISABLED);
    else        lv_obj_add_state(s_btn_identify, LV_STATE_DISABLED);
}

void ui_system_set_open(bool open, bool animate)
{
    if (!s_sheet) return;
    s_open = open;
    int y = open ? SHEET_OPEN_Y : SHEET_CLOSED_Y;
    if (open) {
        lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_scrim);
        lv_obj_move_foreground(s_sheet);
        if (animate) {
            lv_obj_set_style_opa(s_scrim, LV_OPA_TRANSP, 0);
            ui_anim_opa(s_scrim, LV_OPA_COVER, 220);
        } else {
            lv_obj_set_style_opa(s_scrim, LV_OPA_COVER, 0);
        }
        ui_system_refresh();
    } else {
        lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
        rename_close();
    }
    if (animate) ui_anim_y(s_sheet, y, open ? 260 : 200, open ? lv_anim_path_overshoot : lv_anim_path_ease_in_out);
    else lv_obj_set_y(s_sheet, y);
}

bool ui_system_is_open(void) { return s_open; }
