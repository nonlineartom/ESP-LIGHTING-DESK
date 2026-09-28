#include "scenes.h"
#include "md_attr.h"

#include <stdio.h>
#include <string.h>

MD_EXT_BSS scene_t g_scenes[SCENES_MAX];

int scenes_count(void)
{
    int n = 0;
    for (int i = 0; i < SCENES_MAX; i++) if (g_scenes[i].used) n++;
    return n;
}

scene_t *scenes_get(int idx)
{
    if (idx < 0 || idx >= SCENES_MAX || !g_scenes[idx].used) return NULL;
    return &g_scenes[idx];
}

void scenes_suggest_name(char *out, int cap)
{
    for (int n = 1; n < 100; n++) {
        char cand[SCENE_NAME_LEN];
        snprintf(cand, sizeof(cand), "Scene %d", n);
        bool taken = false;
        for (int i = 0; i < SCENES_MAX; i++) {
            if (g_scenes[i].used && strcmp(g_scenes[i].name, cand) == 0) { taken = true; break; }
        }
        if (!taken) {
            snprintf(out, cap, "%s", cand);
            return;
        }
    }
    snprintf(out, cap, "Scene");
}

int scenes_capture(int idx, const char *name)
{
    if (idx < 0) {
        for (int i = 0; i < SCENES_MAX; i++) {
            if (!g_scenes[i].used) { idx = i; break; }
        }
        if (idx < 0) return -1;
    }
    if (idx >= SCENES_MAX) return -1;
    scene_t *s = &g_scenes[idx];
    bool keep_name = s->used && (!name || !name[0]);
    char old[SCENE_NAME_LEN];
    memcpy(old, s->name, sizeof(old));
    memset(s, 0, sizeof(*s));
    s->used = true;
    if (keep_name) memcpy(s->name, old, sizeof(old));
    else if (name && name[0]) snprintf(s->name, sizeof(s->name), "%s", name);
    else scenes_suggest_name(s->name, sizeof(s->name));

    for (int i = 0; i < LIGHTS_MAX && s->count < LIGHTS_MAX; i++) {
        const light_t *l = &g_lights[i];
        if (!l->used) continue;
        scene_entry_t *e = &s->e[s->count++];
        e->addr = l->addr;
        e->on = l->state.on;
        e->mode = (uint8_t)l->state.mode;
        e->intensity = l->state.intensity;
        e->cct = l->state.cct;
        e->gm = l->state.gm;
        e->hue = l->state.mode == LIGHT_MODE_FX ? l->state.fx : l->state.hue;   /* FX: effect id */
        e->sat = l->state.sat;
    }
    return idx;
}

void scenes_delete(int idx)
{
    if (idx >= 0 && idx < SCENES_MAX) memset(&g_scenes[idx], 0, sizeof(g_scenes[idx]));
}
