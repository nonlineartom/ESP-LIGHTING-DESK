/* Home screen: the project's lights, tap a light to control it, "+" to pair. */
#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "ui.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_status;
static lv_obj_t *s_list;
static lv_obj_t *s_empty;
static lv_obj_t *s_cards[LIGHTS_MAX];
static lv_obj_t *s_card_disc[LIGHTS_MAX];
static lv_obj_t *s_card_name[LIGHTS_MAX];
static lv_obj_t *s_card_sub[LIGHTS_MAX];
static uint16_t  s_card_addr[LIGHTS_MAX];

static void on_card_clicked(lv_event_t *e)
{
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx < LIGHTS_MAX && s_card_addr[idx]) ui_show_control(s_card_addr[idx]);
}

static void on_pair_clicked(lv_event_t *e)
{
    (void)e;
    app_cmd_pair_start();
    ui_show_pair();
}

static void build(void)
{
    s_scr = ui_make_screen();

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "MeshDeck");
    lv_obj_set_style_text_font(title, ui_font_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 78);

    s_status = ui_make_status_bar(s_scr);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 128);

    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 520, 370);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 12, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    s_empty = lv_label_create(s_scr);
    lv_label_set_text(s_empty, "No lights paired yet.\nTap + to pair a light.");
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_empty, ui_col_muted, 0);
    lv_obj_align(s_empty, LV_ALIGN_CENTER, 0, -10);

    for (int i = 0; i < LIGHTS_MAX; i++) {
        lv_obj_t *card = lv_button_create(s_list);
        lv_obj_set_size(card, 500, 92);
        lv_obj_set_style_radius(card, 24, 0);
        lv_obj_set_style_bg_color(card, ui_col_panel, 0);
        lv_obj_set_style_bg_color(card, lv_color_lighten(ui_col_panel, 30), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_add_event_cb(card, on_card_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *disc = lv_obj_create(card);
        lv_obj_set_size(disc, 60, 60);
        lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(disc, 2, 0);
        lv_obj_set_style_border_color(disc, lv_color_hex(0x2a3140), 0);
        lv_obj_set_style_shadow_width(disc, 24, 0);
        lv_obj_set_style_shadow_opa(disc, LV_OPA_60, 0);
        lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(disc, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t *name = lv_label_create(card);
        lv_obj_set_style_text_font(name, ui_font_normal, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 84, -14);

        lv_obj_t *sub = lv_label_create(card);
        lv_obj_set_style_text_font(sub, ui_font_small, 0);
        lv_obj_set_style_text_color(sub, ui_col_muted, 0);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 84, 16);

        lv_obj_t *chev = lv_label_create(card);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chev, ui_col_muted, 0);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -6, 0);

        s_cards[i] = card;
        s_card_disc[i] = disc;
        s_card_name[i] = name;
        s_card_sub[i] = sub;
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *pair = ui_make_round_button(s_scr, LV_SYMBOL_PLUS, 84, ui_col_accent);
    lv_obj_set_style_text_color(pair, lv_color_hex(0x1a1408), 0);
    lv_obj_align(pair, LV_ALIGN_BOTTOM_MID, 0, -78);
    lv_obj_add_event_cb(pair, on_pair_clicked, LV_EVENT_CLICKED, NULL);
}

static void describe(const light_t *l, char *buf, size_t n)
{
    const light_state_t *s = &l->state;
    if (!l->configured) {
        snprintf(buf, n, "Setup not finished");
    } else if (!s->reported) {
        snprintf(buf, n, "Waiting for status");
    } else if (!s->on) {
        snprintf(buf, n, "Off");
    } else if (s->mode == LIGHT_MODE_HSI) {
        snprintf(buf, n, "Hue %u\xC2\xB0  Sat %u%%  %u%%", s->hue, s->sat, s->intensity / 10);
    } else {
        snprintf(buf, n, "%uK  %s%d  %u%%", s->cct, s->gm >= 0 ? "G/M +" : "G/M ", s->gm, s->intensity / 10);
    }
}

void ui_home_refresh(void)
{
    if (!s_scr) build();
    ui_update_status_label(s_status);

    app_core_lock();
    int n = 0;
    for (int i = 0; i < LIGHTS_MAX && n < LIGHTS_MAX; i++) {
        const light_t *l = &g_lights[i];
        if (!l->used) continue;
        lv_obj_clear_flag(s_cards[n], LV_OBJ_FLAG_HIDDEN);
        s_card_addr[n] = l->addr;
        lv_label_set_text(s_card_name[n], l->name);
        char sub[48];
        describe(l, sub, sizeof(sub));
        lv_label_set_text(s_card_sub[n], sub);
        uint8_t r, g, b;
        light_preview_rgb(l, &r, &g, &b);
        lv_color_t c = lv_color_make(r, g, b);
        lv_obj_set_style_bg_color(s_card_disc[n], c, 0);
        lv_obj_set_style_shadow_color(s_card_disc[n], c, 0);
        lv_obj_set_style_shadow_opa(s_card_disc[n], l->state.on ? LV_OPA_60 : LV_OPA_TRANSP, 0);
        n++;
    }
    app_core_unlock();
    for (int i = n; i < LIGHTS_MAX; i++) {
        lv_obj_add_flag(s_cards[i], LV_OBJ_FLAG_HIDDEN);
        s_card_addr[i] = 0;
    }
    if (n == 0) lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

    if (lv_screen_active() != s_scr) lv_screen_load(s_scr);
}
