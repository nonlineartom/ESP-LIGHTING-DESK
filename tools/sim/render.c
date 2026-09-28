/*
 * Headless renderer: drives the MeshDeck UI on a 720x720 virtual display and
 * writes raw frames (BGRA) for each scenario into out/.  Convert with to_png.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lvgl.h"
#include "app_core.h"
#include "scenes.h"
#include "ui.h"

void sim_set_link(link_state_t st, const char *text);
void sim_set_pair(pair_state_t st, const char *text, int pct);
void sim_add_unprov(const char *name, int rssi);
void sim_clear_unprov(void);

static uint32_t s_ms;
static uint32_t tick_cb(void) { return s_ms; }
static lv_display_t *s_disp;
static uint8_t *s_fb;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area; (void)px_map;
    lv_display_flush_ready(disp);
}

static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_ms += 5;
        lv_timer_handler();
    }
}

static void snap(const char *name)
{
    lv_refr_now(s_disp);
    char path[256];
    snprintf(path, sizeof(path), "out/%s.raw", name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite(s_fb, 1, UI_W * UI_H * 4, f);
    fclose(f);
    printf("wrote %s\n", path);
}

static light_t *add_light(const char *name, uint16_t addr, bool on, light_mode_t mode, uint16_t level,
                          uint16_t cct, int gm, uint16_t hue, uint8_t sat)
{
    uint8_t uuid[16] = { (uint8_t)addr, 1, 2, 3 }, key[16] = { 0 };
    light_t *l = lights_add(uuid, addr, 1, key, name);
    l->configured = true;
    l->state.on = on;
    l->state.mode = mode;
    l->state.level = level;
    l->state.intensity = level;
    l->state.cct = cct;
    l->state.gm = (int8_t)gm;
    l->state.hue = hue;
    l->state.sat = sat;
    l->state.reported = true;
    l->state.last_report_ms = 0;
    return l;
}

static void studio(void)
{
    memset(g_lights, 0, sizeof(g_lights));
    memset(g_scenes, 0, sizeof(g_scenes));
    add_light("Key",  2, true,  LIGHT_MODE_CCT, 720, 5600, 0, 0, 0);
    add_light("Fill", 3, true,  LIGHT_MODE_CCT, 380, 4300, 3, 0, 0);
    add_light("Back", 4, true,  LIGHT_MODE_HSI, 550, 5600, 0, 212, 78);
    scenes_capture(-1, "Interview");
    g_lights[0].state.cct = 3200; g_lights[1].state.cct = 3200; g_lights[2].state.hue = 28; g_lights[2].state.sat = 90;
    scenes_capture(-1, "Golden hour");
    g_lights[0].state.on = false; g_lights[1].state.level = 150; g_lights[1].state.intensity = 150;
    g_lights[2].state.hue = 260; g_lights[2].state.sat = 100;
    scenes_capture(-1, "Night");
    /* back to the first look */
    g_lights[0].state.on = true; g_lights[0].state.cct = 5600;
    g_lights[1].state.level = 380; g_lights[1].state.intensity = 380; g_lights[1].state.cct = 4300;
    g_lights[2].state.hue = 212; g_lights[2].state.sat = 78;
}

int main(void)
{
    mkdir("out", 0755);
    lv_init();
    lv_tick_set_cb(tick_cb);
    s_disp = lv_display_create(UI_W, UI_H);
    s_fb = calloc(UI_W * UI_H, 4);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(s_disp, s_fb, NULL, UI_W * UI_H * 4, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(s_disp, flush_cb);

    /* 1. fresh device, nothing paired */
    memset(g_lights, 0, sizeof(g_lights));
    sim_set_link(LINK_NO_LIGHTS, "No lights paired");
    ui_init();
    run(400);
    snap("01_empty");

    /* 2. studio with three lights: master page */
    studio();
    sim_set_link(LINK_ONLINE, "Online via Key");
    ui_notify_lights_changed();
    run(600);
    snap("02_master");

    /* 3. channel page, CCT light */
    ui_desk_goto(1, false);
    run(500);
    snap("03_channel_cct");

    /* 4. channel page, HSI light */
    ui_desk_goto(3, false);
    run(500);
    snap("04_channel_hsi");

    /* 5. scenes drawer mid-animation and open */
    ui_desk_goto(1, false);
    run(200);
    ui_scenes_set_open(true, true);
    run(110);
    snap("05_scenes_opening");
    run(600);
    snap("06_scenes_open");
    app_cmd_scene_recall(1);
    run(700);
    snap("07_scene_recalled");
    ui_scenes_set_open(false, true);
    run(500);

    /* 8. system sheet */
    ui_system_set_open(true, true);
    run(600);
    snap("08_system");
    ui_system_set_open(false, false);
    run(200);

    /* 9-11. pairing */
    sim_set_pair(PAIR_SCANNING, "Looking for lights in pairing mode", 0);
    sim_add_unprov("amaran P2c", -52);
    sim_add_unprov("amaran 100x", -71);
    ui_show_pair();
    run(700);
    snap("09_pair_scan");
    sim_set_pair(PAIR_IN_PROGRESS, "Binding models (3/7)", 88);
    ui_notify_pair_changed();
    run(700);
    snap("10_pair_progress");
    sim_set_pair(PAIR_DONE, "Paired and ready", 100);
    ui_notify_pair_changed();
    run(700);
    snap("11_pair_done");

    /* motion: carousel snap Key -> Fill, and the drawer rising */
    ui_show_desk();
    run(600);
    ui_desk_goto(1, false);
    run(300);
    ui_desk_goto(2, true);
    for (int i = 0; i < 5; i++) {
        char n[32];
        snprintf(n, sizeof(n), "anim_swipe_%d", i);
        snap(n);
        run(60);
    }
    run(400);
    ui_scenes_set_open(true, true);
    for (int i = 0; i < 5; i++) {
        char n[32];
        snprintf(n, sizeof(n), "anim_drawer_%d", i);
        snap(n);
        run(65);
    }
    run(400);
    ui_scenes_set_open(false, false);
    run(100);

    /* 12. master with everything off */
    ui_show_desk();
    run(600);
    ui_desk_goto(0, false);
    app_cmd_all_power(false);
    run(600);
    snap("12_master_off");
    return 0;
}
