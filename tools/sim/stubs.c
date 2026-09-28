/*
 * Host stand-in for app_core: fake lights, scenes and link state so the UI can
 * be rendered and exercised without hardware.  Commands are logged to stdout
 * and applied to the fake state so the screens react like the real thing.
 */
#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "products.h"
#include "scenes.h"
#include "ui.h"

static link_state_t s_link = LINK_ONLINE;
static char         s_link_text[48] = "Online via Key";
static pair_state_t s_pair = PAIR_IDLE;
static char         s_pair_text[48] = "";
static int          s_pair_pct;
static uint16_t     s_master = 1000;
static int          s_last_scene = -1;
static uint16_t     s_fade_ms = 1000;
static unprov_dev_t s_unprov[UNPROV_MAX];
static int          s_unprov_n;

/* ---- scenario setters used by render.c ---- */
void sim_set_link(link_state_t st, const char *text)
{
    s_link = st;
    snprintf(s_link_text, sizeof(s_link_text), "%s", text);
}
void sim_set_pair(pair_state_t st, const char *text, int pct)
{
    s_pair = st;
    snprintf(s_pair_text, sizeof(s_pair_text), "%s", text);
    s_pair_pct = pct;
}
/* uuid_ascii: a Sidus device UUID ("400H5-DBFA30"); the name shown is the
 * product name, as app_core does. */
void sim_add_unprov(const char *uuid_ascii, int rssi)
{
    if (s_unprov_n >= UNPROV_MAX) return;
    unprov_dev_t *d = &s_unprov[s_unprov_n++];
    memset(d, 0, sizeof(*d));
    memcpy(d->uuid, uuid_ascii, strlen(uuid_ascii) < 16 ? strlen(uuid_ascii) : 16);
    d->rssi = (int8_t)rssi;
    const char *product = sidus_product_from_uuid(d->uuid);
    snprintf(d->name, sizeof(d->name), "%s", product ? product : "");
}
void sim_clear_unprov(void) { s_unprov_n = 0; }

void app_core_lock(void) {}
void app_core_unlock(void) {}
link_state_t app_core_link_state(void) { return s_link; }
const char  *app_core_link_text(void) { return s_link_text; }
uint16_t     app_core_proxy_addr(void) { return 2; }
pair_state_t app_core_pair_state(void) { return s_pair; }
const char  *app_core_pair_text(void) { return s_pair_text; }
int          app_core_pair_percent(void) { return s_pair_pct; }
uint16_t     app_core_master_level(void) { return s_master; }
int          app_core_last_scene(void) { return s_last_scene; }
uint16_t     app_core_fade_ms(void) { return s_fade_ms; }
bool         app_core_fade_active(void) { return false; }
void app_cmd_set_fade_ms(uint16_t ms) { printf("[cmd] fade %u\n", ms); s_fade_ms = ms; }
void app_cmd_diag_lightness(uint16_t addr, uint16_t l, uint8_t t) { printf("[cmd] diag lightness 0x%04x %u %u\n", addr, l, t); }
int app_core_unprov_list(unprov_dev_t *out, int max)
{
    int n = s_unprov_n < max ? s_unprov_n : max;
    memcpy(out, s_unprov, n * sizeof(unprov_dev_t));
    return n;
}

static uint16_t scaled(uint16_t level) { return (uint16_t)((uint32_t)level * s_master / 1000); }

void app_cmd_pair_start(void)  { printf("[cmd] pair start\n"); }
void app_cmd_pair_select(const uint8_t uuid[16]) { printf("[cmd] pair select %02x%02x\n", uuid[0], uuid[1]); }
void app_cmd_pair_cancel(void) { printf("[cmd] pair cancel\n"); }
void app_cmd_request_status(uint16_t addr) { printf("[cmd] status 0x%04x\n", addr); }
void app_cmd_factory_reset(void) { printf("[cmd] factory reset\n"); }

void app_cmd_set_power(uint16_t addr, bool on)
{
    printf("[cmd] power 0x%04x %s\n", addr, on ? "on" : "off");
    light_t *l = lights_find_by_addr(addr);
    if (l) { l->state.on = on; ui_notify_light_state(addr); }
    s_last_scene = -1;
}
void app_cmd_set_intensity(uint16_t addr, uint16_t v)
{
    printf("[cmd] intensity 0x%04x %u\n", addr, v);
    light_t *l = lights_find_by_addr(addr);
    if (l) { l->state.level = v; l->state.intensity = scaled(v); ui_notify_light_state(addr); }
}
void app_cmd_set_cct(uint16_t addr, uint16_t k, uint16_t v, int gm)
{
    printf("[cmd] cct 0x%04x %uK %u gm%d\n", addr, k, v, gm);
    light_t *l = lights_find_by_addr(addr);
    if (l) { l->state.mode = LIGHT_MODE_CCT; l->state.cct = k; l->state.gm = (int8_t)gm; l->state.level = v;
             l->state.intensity = scaled(v); ui_notify_light_state(addr); }
}
void app_cmd_set_hsi(uint16_t addr, uint16_t h, uint8_t s, uint16_t v)
{
    printf("[cmd] hsi 0x%04x %u %u %u\n", addr, h, s, v);
    light_t *l = lights_find_by_addr(addr);
    if (l) { l->state.mode = LIGHT_MODE_HSI; l->state.hue = h; l->state.sat = s; l->state.level = v;
             l->state.intensity = scaled(v); ui_notify_light_state(addr); }
}
void app_cmd_identify(uint16_t addr) { printf("[cmd] identify 0x%04x\n", addr); }
void app_cmd_set_effect(uint16_t addr, uint8_t fx)
{
    printf("[cmd] effect 0x%04x %u\n", addr, fx);
    light_t *l = lights_find_by_addr(addr);
    if (!l) return;
    if (fx < TELINK_FX_COUNT) {
        if (l->state.mode != LIGHT_MODE_FX) l->state.base_mode = l->state.mode;
        l->state.mode = LIGHT_MODE_FX; l->state.fx = fx; l->state.on = true;
    } else if (l->state.mode == LIGHT_MODE_FX) {
        l->state.mode = (light_mode_t)l->state.base_mode; l->state.fx = TELINK_FX_NONE;
    }
    ui_notify_light_state(addr);
}
void app_cmd_remove_light(uint16_t addr)
{
    printf("[cmd] remove 0x%04x\n", addr);
    light_t *l = lights_find_by_addr(addr);
    if (l) { lights_remove(l); ui_notify_lights_changed(); }
}
void app_cmd_rename_light(uint16_t addr, const char *name)
{
    printf("[cmd] rename 0x%04x %s\n", addr, name);
    light_t *l = lights_find_by_addr(addr);
    if (l) { snprintf(l->name, sizeof(l->name), "%s", name); ui_notify_lights_changed(); }
}
void app_cmd_master_level(uint16_t level)
{
    printf("[cmd] master %u\n", level);
    s_master = level;
    for (int i = 0; i < LIGHTS_MAX; i++) if (g_lights[i].used) {
        g_lights[i].state.intensity = scaled(g_lights[i].state.level);
        ui_notify_light_state(g_lights[i].addr);
    }
}
void app_cmd_all_power(bool on)
{
    printf("[cmd] all power %d\n", on);
    for (int i = 0; i < LIGHTS_MAX; i++) if (g_lights[i].used) { g_lights[i].state.on = on; ui_notify_light_state(g_lights[i].addr); }
}
void app_cmd_all_cct(uint16_t k)
{
    printf("[cmd] all cct %u\n", k);
    for (int i = 0; i < LIGHTS_MAX; i++) if (g_lights[i].used) {
        g_lights[i].state.mode = LIGHT_MODE_CCT; g_lights[i].state.cct = k; g_lights[i].state.gm = 0;
        ui_notify_light_state(g_lights[i].addr);
    }
}
void app_cmd_scene_save(int idx)
{
    int r = scenes_capture(idx, NULL);
    printf("[cmd] scene save %d -> %d\n", idx, r);
    s_last_scene = r;
    ui_notify_lights_changed();
}
void app_cmd_scene_recall(int idx)
{
    printf("[cmd] scene recall %d\n", idx);
    scene_t *sc = scenes_get(idx);
    if (!sc) return;
    for (int i = 0; i < sc->count; i++) {
        light_t *l = lights_find_by_addr(sc->e[i].addr);
        if (!l) continue;
        l->state.on = sc->e[i].on; l->state.mode = sc->e[i].mode; l->state.intensity = sc->e[i].intensity;
        l->state.level = sc->e[i].intensity; l->state.cct = sc->e[i].cct; l->state.gm = sc->e[i].gm;
        l->state.hue = sc->e[i].hue; l->state.sat = sc->e[i].sat;
        ui_notify_light_state(l->addr);
    }
    s_last_scene = idx;
    ui_notify_lights_changed();
}
uint16_t app_core_setting(const char *key, uint16_t def) { (void)key; return def; }
void app_cmd_save_setting(const char *key, uint16_t v) { printf("[cmd] setting %s=%u\n", key, v); }
void app_cmd_scene_rename(int idx, const char *name)
{
    scene_t *sc = scenes_get(idx);
    if (sc) snprintf(sc->name, sizeof(sc->name), "%s", name);
    ui_notify_lights_changed();
}
void app_cmd_scene_delete(int idx)
{
    printf("[cmd] scene delete %d\n", idx);
    scenes_delete(idx);
    if (s_last_scene == idx) s_last_scene = -1;
    ui_notify_lights_changed();
}
