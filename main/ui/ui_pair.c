/* Pairing screen: lights in pairing mode, then provisioning progress. */
#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "ui.h"
#include "products.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_hint;
static lv_obj_t *s_list;
static lv_obj_t *s_rows[UNPROV_MAX];
static lv_obj_t *s_row_name[UNPROV_MAX];
static lv_obj_t *s_row_sub[UNPROV_MAX];
static uint8_t   s_row_uuid[UNPROV_MAX][16];
static lv_obj_t *s_progress_box;
static lv_obj_t *s_arc;
static lv_obj_t *s_pct;
static lv_obj_t *s_text;
static lv_obj_t *s_spinner;
static lv_obj_t *s_back;
static lv_obj_t *s_back_lbl;

static void arc_anim_exec(void *obj, int32_t v) { lv_arc_set_value((lv_obj_t *)obj, v); }

static void on_row_clicked(lv_event_t *e)
{
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx < UNPROV_MAX) app_cmd_pair_select(s_row_uuid[idx]);
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    pair_state_t st = app_core_pair_state();
    if (st == PAIR_FAILED) {
        app_cmd_pair_cancel();
        app_cmd_pair_start();
        return;
    }
    app_cmd_pair_cancel();
    ui_show_desk();
}

static void build(void)
{
    s_scr = ui_make_screen();

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "Pair a light");
    lv_obj_set_style_text_font(title, ui_font_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 74);

    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "Remove the light from Sidus Link\nor reset it, then switch it on nearby.");
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_hint, ui_font_small, 0);
    lv_obj_set_style_text_color(s_hint, ui_col_muted, 0);
    lv_obj_set_width(s_hint, 460);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 138);

    s_spinner = lv_spinner_create(s_scr);
    lv_obj_set_size(s_spinner, 36, 36);
    lv_spinner_set_anim_params(s_spinner, 1200, 200);
    lv_obj_set_style_arc_color(s_spinner, ui_col_accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spinner, ui_col_track, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 4, LV_PART_MAIN);
    lv_obj_align(s_spinner, LV_ALIGN_TOP_MID, 0, 220);

    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 500, 300);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 270);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 10, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < UNPROV_MAX; i++) {
        lv_obj_t *row = lv_button_create(s_list);
        lv_obj_set_size(row, 460, 76);
        lv_obj_set_style_radius(row, 38, 0);
        lv_obj_set_style_bg_color(row, ui_col_panel, 0);
        lv_obj_set_style_bg_color(row, ui_col_panel_hi, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, on_row_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *name = lv_label_create(row);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 14, -12);
        lv_obj_t *sub = lv_label_create(row);
        lv_obj_set_style_text_font(sub, ui_font_small, 0);
        lv_obj_set_style_text_color(sub, ui_col_muted, 0);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 14, 16);
        lv_obj_t *plus = lv_label_create(row);
        lv_label_set_text(plus, LV_SYMBOL_PLUS);
        lv_obj_set_style_text_color(plus, ui_col_accent, 0);
        lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -14, 0);
        s_rows[i] = row;
        s_row_name[i] = name;
        s_row_sub[i] = sub;
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    s_progress_box = lv_obj_create(s_scr);
    lv_obj_set_size(s_progress_box, 400, 340);
    lv_obj_align(s_progress_box, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_opa(s_progress_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_progress_box, 0, 0);
    lv_obj_remove_flag(s_progress_box, LV_OBJ_FLAG_SCROLLABLE);

    s_arc = lv_arc_create(s_progress_box);
    lv_obj_set_size(s_arc, 230, 230);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 0);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, ui_col_track, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, ui_col_accent, LV_PART_INDICATOR);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, 0);

    s_pct = lv_label_create(s_progress_box);
    lv_obj_set_style_text_font(s_pct, ui_font_title, 0);
    lv_obj_align_to(s_pct, s_arc, LV_ALIGN_CENTER, 0, 0);

    s_text = lv_label_create(s_progress_box);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_text, 380);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_text, LV_ALIGN_TOP_MID, 0, 250);

    s_back = ui_make_pill(s_scr, "Cancel", 200, 56, ui_col_panel, ui_font_normal);
    lv_obj_align(s_back, LV_ALIGN_BOTTOM_MID, 0, -66);
    lv_obj_add_event_cb(s_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    s_back_lbl = lv_obj_get_child(s_back, 0);
}

lv_obj_t *ui_pair_screen(void)
{
    if (!s_scr) build();
    return s_scr;
}

void ui_pair_refresh(void)
{
    if (!s_scr) build();

    pair_state_t st = app_core_pair_state();
    bool scanning = (st == PAIR_SCANNING || st == PAIR_IDLE);

    if (scanning) {
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_progress_box, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_back_lbl, "Cancel");

        unprov_dev_t devs[UNPROV_MAX];
        app_core_lock();
        int n = app_core_unprov_list(devs, UNPROV_MAX);
        app_core_unlock();
        for (int i = 0; i < UNPROV_MAX; i++) {
            if (i < n) {
                lv_obj_remove_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
                memcpy(s_row_uuid[i], devs[i].uuid, 16);
                lv_label_set_text(s_row_name[i], devs[i].name[0] ? devs[i].name : "Sidus light");
                const char *sig = devs[i].rssi > -60 ? "strong" : devs[i].rssi > -75 ? "good" : "weak";
                char tail[7];
                if (sidus_uuid_mac_tail(devs[i].uuid, tail)) {
                    lv_label_set_text_fmt(s_row_sub[i], "ID %s  \xE2\x80\xA2  signal %s (%d dBm)",
                                          tail, sig, devs[i].rssi);
                } else {
                    lv_label_set_text_fmt(s_row_sub[i], "%02X%02X%02X%02X  \xE2\x80\xA2  signal %s (%d dBm)",
                                          devs[i].uuid[0], devs[i].uuid[1], devs[i].uuid[2], devs[i].uuid[3],
                                          sig, devs[i].rssi);
                }
            } else {
                lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_progress_box, LV_OBJ_FLAG_HIDDEN);
        int pct = app_core_pair_percent();
        lv_label_set_text(s_text, app_core_pair_text());
        lv_anim_delete(s_arc, arc_anim_exec);
        if (st == PAIR_DONE) {
            lv_label_set_text(s_pct, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(s_pct, ui_col_ok, 0);
            lv_obj_set_style_arc_color(s_arc, ui_col_ok, LV_PART_INDICATOR);
            ui_anim(s_arc, lv_arc_get_value(s_arc), 100, 400, arc_anim_exec, lv_anim_path_ease_out);
            lv_label_set_text(s_back_lbl, "Done");
        } else if (st == PAIR_FAILED) {
            lv_label_set_text(s_pct, LV_SYMBOL_WARNING);
            lv_obj_set_style_text_color(s_pct, ui_col_warn, 0);
            lv_obj_set_style_arc_color(s_arc, ui_col_warn, LV_PART_INDICATOR);
            lv_arc_set_value(s_arc, 100);
            lv_label_set_text(s_back_lbl, "Try again");
        } else {
            lv_label_set_text_fmt(s_pct, "%d%%", pct);
            lv_obj_set_style_text_color(s_pct, ui_col_text, 0);
            lv_obj_set_style_arc_color(s_arc, ui_col_accent, LV_PART_INDICATOR);
            ui_anim(s_arc, lv_arc_get_value(s_arc), pct, 400, arc_anim_exec, lv_anim_path_ease_out);
            lv_label_set_text(s_back_lbl, "Cancel");
        }
        lv_obj_align_to(s_pct, s_arc, LV_ALIGN_CENTER, 0, 0);
    }
}
