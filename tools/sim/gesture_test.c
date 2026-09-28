/*
 * Exercise the LVGL input path with one and two contact points.  The GT911
 * port uses the same recognizer calls when it reads the physical controller.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"
#include "ui.h"

static uint32_t s_ms;
static lv_indev_t *s_pointer;
static lv_indev_touch_data_t s_points[2];
static int s_contacts;

static uint32_t tick_cb(void) { return s_ms; }
static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(d);
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    lv_indev_gesture_recognizers_update(indev, s_points, 2);
    lv_indev_gesture_recognizers_set_data(indev, data);
    data->point = s_points[0].point;
    data->state = s_contacts ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void feed(int contacts, int y)
{
    s_ms += 25;
    s_contacts = contacts;
    for (int i = 0; i < 2; i++) {
        s_points[i].id = i + 1;
        s_points[i].point.x = i ? 450 : 270;
        s_points[i].point.y = y;
        s_points[i].state = i < contacts ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        s_points[i].timestamp = s_ms;
    }
    lv_indev_read(s_pointer);
    lv_timer_handler();
}

static void swipe(int contacts, int from, int to)
{
    for (int i = 0; i <= 8; i++) feed(contacts, from + (to - from) * i / 8);
    feed(0, to);
}

static int check(const char *name, bool system, bool scenes)
{
    bool got_system = ui_system_is_open(), got_scenes = ui_scenes_is_open();
    if (got_system == system && got_scenes == scenes) {
        printf("PASS %s\n", name);
        return 0;
    }
    fprintf(stderr, "FAIL %s: system=%d scenes=%d\n", name, got_system, got_scenes);
    return 1;
}

int main(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *display = lv_display_create(720, 720);
    uint8_t *buffer = calloc(720 * 720, 4);
    if (!buffer) return 1;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, buffer, NULL, 720 * 720 * 4, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush_cb);
    s_pointer = lv_indev_create();
    lv_indev_set_type(s_pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_mode(s_pointer, LV_INDEV_MODE_EVENT);
    lv_indev_set_read_cb(s_pointer, touch_read);
    lv_indev_set_display(s_pointer, display);
    ui_init();

    int failures = 0;
    swipe(1, 240, 430);
    failures += check("one finger keeps sheets closed", false, false);
    swipe(2, 240, 500);
    failures += check("two finger down opens system", true, false);
    swipe(2, 500, 180);
    failures += check("two finger up closes system", false, false);
    swipe(2, 500, 180);
    failures += check("two finger up opens scenes", false, true);
    swipe(2, 240, 500);
    failures += check("two finger down closes scenes", false, false);
    return failures ? 1 : 0;
}
