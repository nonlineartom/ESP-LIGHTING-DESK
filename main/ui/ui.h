/*
 * LVGL user interface for the 720x720 round display.
 *
 * ui_init() must be called with the LVGL lock held.  The ui_notify_* functions
 * are called from the application task and take the LVGL lock themselves.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);

void ui_notify_link_changed(void);
void ui_notify_lights_changed(void);
void ui_notify_light_state(uint16_t addr);
void ui_notify_pair_changed(void);

/* screens (call with LVGL lock held) */
void ui_show_home(void);
void ui_show_pair(void);
void ui_show_control(uint16_t addr);

/* shared helpers / theme (ui.c) */
#include "lvgl.h"
extern const lv_font_t *ui_font_small;
extern const lv_font_t *ui_font_normal;
extern const lv_font_t *ui_font_large;
extern const lv_font_t *ui_font_title;
extern lv_color_t ui_col_bg;
extern lv_color_t ui_col_panel;
extern lv_color_t ui_col_accent;
extern lv_color_t ui_col_text;
extern lv_color_t ui_col_muted;
extern lv_color_t ui_col_ok;
extern lv_color_t ui_col_warn;

lv_obj_t *ui_make_screen(void);
lv_obj_t *ui_make_round_button(lv_obj_t *parent, const char *symbol, int size, lv_color_t bg);
lv_obj_t *ui_make_status_bar(lv_obj_t *screen);   /* returns the label to update */
void      ui_update_status_label(lv_obj_t *label);

/* screen-level refresh hooks (implemented in the screen files) */
void ui_home_refresh(void);
void ui_pair_refresh(void);
void ui_control_refresh(uint16_t addr);

#ifdef __cplusplus
}
#endif
