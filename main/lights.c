#include "lights.h"
#include "md_attr.h"

#include "products.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

MD_EXT_BSS light_t g_lights[LIGHTS_MAX];

int lights_count(void)
{
    int n = 0;
    for (int i = 0; i < LIGHTS_MAX; i++) if (g_lights[i].used) n++;
    return n;
}

light_t *lights_find_by_addr(uint16_t addr)
{
    for (int i = 0; i < LIGHTS_MAX; i++) {
        light_t *l = &g_lights[i];
        if (!l->used) continue;
        uint8_t n = l->elem_count ? l->elem_count : 1;
        if (addr >= l->addr && addr < l->addr + n) return l;
    }
    return NULL;
}

light_t *lights_find_by_uuid(const uint8_t uuid[16])
{
    for (int i = 0; i < LIGHTS_MAX; i++) {
        if (g_lights[i].used && memcmp(g_lights[i].uuid, uuid, 16) == 0) return &g_lights[i];
    }
    return NULL;
}

static bool name_taken(const light_t *self, const char *name)
{
    for (int i = 0; i < LIGHTS_MAX; i++) {
        const light_t *o = &g_lights[i];
        if (o != self && o->used && strcmp(o->name, name) == 0) return true;
    }
    return false;
}

/* base, then "base 2", "base 3", ... */
static void set_unique_name(light_t *l, const char *base)
{
    snprintf(l->name, sizeof(l->name), "%s", base);
    for (int n = 2; name_taken(l, l->name) && n <= LIGHTS_MAX + 1; n++) {
        snprintf(l->name, sizeof(l->name), "%.*s %d", (int)sizeof(l->name) - 4, base, n);
    }
}

static bool is_generic_name(const char *name)
{
    /* "Light N" from older firmware, or empty */
    if (!name[0]) return true;
    if (strncmp(name, "Light ", 6) != 0) return false;
    for (const char *p = name + 6; *p; p++) if (!isdigit((unsigned char)*p)) return false;
    return name[6] != 0;
}

bool lights_apply_product_name(light_t *l)
{
    if (!is_generic_name(l->name)) return false;
    const char *product = sidus_product_from_uuid(l->uuid);
    if (!product) return false;
    set_unique_name(l, product);
    return true;
}

bool lights_apply_product_caps(light_t *l)
{
    const sidus_product_t *p = sidus_product_for_uuid(l->uuid);
    if (!p) return false;
    if (p->cct_min && p->cct_max >= p->cct_min) {
        l->cct_min = p->cct_min;
        l->cct_max = p->cct_max;
    }
    l->has_hsi = (p->flags & P_HSI) != 0;
    l->has_gm = (p->flags & P_GM) != 0;
    l->has_battery = (p->flags & P_BATT) != 0;
    l->fx_mask = p->fx;
    return true;
}

const char *lights_product_name(const light_t *l)
{
    return sidus_product_from_uuid(l->uuid);
}

light_t *lights_add(const uint8_t uuid[16], uint16_t addr, uint8_t elem_count,
                    const uint8_t devkey[16], const char *name)
{
    light_t *l = lights_find_by_uuid(uuid);
    if (!l) {
        for (int i = 0; i < LIGHTS_MAX; i++) {
            if (!g_lights[i].used) {
                l = &g_lights[i];
                break;
            }
        }
    }
    if (!l) return NULL;
    memset(l, 0, sizeof(*l));
    l->used = true;
    memcpy(l->uuid, uuid, 16);
    l->addr = addr;
    l->elem_count = elem_count ? elem_count : 1;
    memcpy(l->devkey, devkey, 16);
    const char *product = sidus_product_from_uuid(uuid);
    if (product) {
        set_unique_name(l, product);
    } else if (name && name[0]) {
        set_unique_name(l, name);
    } else {
        snprintf(l->name, sizeof(l->name), "Light %d", (int)(l - g_lights) + 1);
    }
    l->cct_min = 2500;
    l->cct_max = 7500;
    l->has_hsi = true;
    l->has_gm = true;
    l->fx_mask = 0;
    lights_apply_product_caps(l);
    l->state.fx = TELINK_FX_NONE;
    l->state.batt_pct = -1;
    /* sensible defaults until the fixture reports */
    l->state.mode = LIGHT_MODE_CCT;
    l->state.intensity = 500;
    l->state.level = 500;
    l->state.cct = 5600;
    l->state.sat = 100;
    return l;
}

void lights_remove(light_t *l)
{
    if (l) memset(l, 0, sizeof(*l));
}

void lights_apply_status(light_t *l, const telink_status_t *st, uint32_t now_ms)
{
    l->state.on = st->on;
    l->state.intensity = st->intensity;
    l->state.fx = TELINK_FX_NONE;
    if (st->is_hsi) {
        l->state.mode = LIGHT_MODE_HSI;
        l->state.hue = st->hue;
        l->state.sat = st->sat;
    } else {
        l->state.mode = LIGHT_MODE_CCT;
        l->state.cct = st->cct_kelvin;
        l->state.gm = st->gm;
    }
    l->state.reported = true;
    l->state.last_report_ms = now_ms;
}

/* Tanner Helland's black-body approximation, good enough for a preview disc. */
void cct_to_rgb(uint16_t kelvin, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double t = kelvin / 100.0;
    double rr, gg, bb;
    if (t <= 66) {
        rr = 255;
        gg = 99.4708025861 * log(t) - 161.1195681661;
        bb = (t <= 19) ? 0 : 138.5177312231 * log(t - 10) - 305.0447927307;
    } else {
        rr = 329.698727446 * pow(t - 60, -0.1332047592);
        gg = 288.1221695283 * pow(t - 60, -0.0755148492);
        bb = 255;
    }
    *r = (uint8_t)(rr < 0 ? 0 : rr > 255 ? 255 : rr);
    *g = (uint8_t)(gg < 0 ? 0 : gg > 255 ? 255 : gg);
    *b = (uint8_t)(bb < 0 ? 0 : bb > 255 ? 255 : bb);
}

void hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double h = (hue % 360) / 60.0, s = sat / 100.0, v = val / 100.0;
    int i = (int)h;
    double f = h - i;
    double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double rr, gg, bb;
    switch (i) {
    case 0: rr = v; gg = t; bb = p; break;
    case 1: rr = q; gg = v; bb = p; break;
    case 2: rr = p; gg = v; bb = t; break;
    case 3: rr = p; gg = q; bb = v; break;
    case 4: rr = t; gg = p; bb = v; break;
    default: rr = v; gg = p; bb = q; break;
    }
    *r = (uint8_t)(rr * 255);
    *g = (uint8_t)(gg * 255);
    *b = (uint8_t)(bb * 255);
}

void light_preview_rgb(const light_t *l, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t rr, gg, bb;
    if (l->state.mode == LIGHT_MODE_FX) {
        /* effects flicker between presets; show a warm "effect" amber */
        rr = 255; gg = 150; bb = 60;
    } else if (l->state.mode == LIGHT_MODE_HSI) {
        hsv_to_rgb(l->state.hue, l->state.sat, 100, &rr, &gg, &bb);
    } else {
        cct_to_rgb(l->state.cct, &rr, &gg, &bb);
        /* green/magenta tint: nudge the green channel */
        int gadj = -l->state.gm * 6;
        int gv = gg + gadj;
        gg = (uint8_t)(gv < 0 ? 0 : gv > 255 ? 255 : gv);
    }
    /* scale by intensity with a perceptual floor so dim lights stay visible */
    double k = l->state.on ? (0.15 + 0.85 * (l->state.intensity / 1000.0)) : 0.0;
    *r = (uint8_t)(rr * k);
    *g = (uint8_t)(gg * k);
    *b = (uint8_t)(bb * k);
}
