/*
 * MeshDeck user interface — the round lighting desk (see docs/UI_DESIGN.md).
 *
 * One LVGL screen ("desk") holds a horizontal snap carousel (master page +
 * one page per light), a scenes drawer that rises from below and a system
 * sheet that drops from above.  Pairing is a separate screen.
 *
 * ui_init() must run with the LVGL lock held.  The ui_notify_* functions are
 * called from the application task and take the LVGL lock themselves.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_W        720
#define UI_H        720
#define UI_CX       360
#define UI_CY       360
#define UI_R        360

void ui_init(void);

/* from the application task */
void ui_notify_link_changed(void);
void ui_notify_lights_changed(void);
void ui_notify_light_state(uint16_t addr);
void ui_notify_pair_changed(void);

/* navigation (LVGL lock held) */
void ui_show_desk(void);
void ui_show_pair(void);

/* ---- theme ---------------------------------------------------------------- */
extern const lv_font_t *ui_font_small;    /* 20 */
extern const lv_font_t *ui_font_normal;   /* 24 */
extern const lv_font_t *ui_font_medium;   /* 28 */
extern const lv_font_t *ui_font_large;    /* 36 */
extern const lv_font_t *ui_font_title;    /* 48 */
extern const lv_font_t *ui_font_huge;     /* 48 */
extern lv_color_t ui_col_bg, ui_col_panel, ui_col_panel_hi, ui_col_track, ui_col_accent,
       ui_col_accent_dark, ui_col_text, ui_col_muted, ui_col_ok, ui_col_warn;

/* ---- geometry / helpers ---------------------------------------------------- */
uint32_t  ui_now_ms(void);
bool      ui_lock(uint32_t timeout_ms);
void      ui_unlock(void);
int       ui_chord_half_width(int y);       /* half of the usable width at screen row y */
lv_obj_t *ui_make_screen(void);
lv_obj_t *ui_make_round_button(lv_obj_t *parent, const char *symbol, int size, lv_color_t bg);
lv_obj_t *ui_make_pill(lv_obj_t *parent, const char *text, int w, int h, lv_color_t bg, const lv_font_t *font);
lv_obj_t *ui_make_handle(lv_obj_t *parent);
void      ui_anim(lv_obj_t *obj, int32_t from, int32_t to, uint32_t ms, lv_anim_exec_xcb_t exec,
                  lv_anim_path_cb_t path);
void      ui_anim_y(lv_obj_t *obj, int32_t to, uint32_t ms, lv_anim_path_cb_t path);
void      ui_anim_opa(lv_obj_t *obj, lv_opa_t to, uint32_t ms);
void      ui_breathe(lv_obj_t *obj);        /* quick scale pulse */
void      ui_fade_bg(lv_obj_t *obj, lv_color_t to, uint32_t ms);
void      ui_fade_shadow(lv_obj_t *obj, lv_color_t to, uint32_t ms);

/* ---- screen modules -------------------------------------------------------- */
void     ui_desk_create(lv_obj_t *screen);
void     ui_desk_rebuild(void);             /* lights added/removed */
void     ui_desk_refresh_light(uint16_t addr);
void     ui_desk_refresh_link(void);
uint16_t ui_desk_current_addr(void);        /* 0 = master page */
void     ui_desk_goto(int page, bool animate); /* 0 = master, n = n-th light */
lv_obj_t *ui_desk_screen(void);

void ui_scenes_create(lv_obj_t *screen);
void ui_scenes_refresh(void);
void ui_scenes_set_open(bool open, bool animate);
bool ui_scenes_is_open(void);

void ui_system_create(lv_obj_t *screen);
void ui_system_refresh(void);
void ui_system_set_open(bool open, bool animate);
bool ui_system_is_open(void);

void ui_pair_refresh(void);

#ifdef __cplusplus
}
#endif
