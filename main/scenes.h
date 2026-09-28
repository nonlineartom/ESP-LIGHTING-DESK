/* Scenes: snapshots of every light's look, saved and recalled from the desk. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lights.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCENES_MAX      12
#define SCENE_NAME_LEN  16

typedef struct {
    uint16_t addr;
    bool     on;
    uint8_t  mode;        /* light_mode_t */
    uint16_t intensity;   /* 0..1000 (output level) */
    uint16_t cct;
    int8_t   gm;
    uint16_t hue;
    uint8_t  sat;
} scene_entry_t;

typedef struct {
    bool          used;
    char          name[SCENE_NAME_LEN];
    uint8_t       count;
    scene_entry_t e[LIGHTS_MAX];
} scene_t;

extern scene_t g_scenes[SCENES_MAX];

int      scenes_count(void);
scene_t *scenes_get(int idx);
/* Capture the current look of all lights into slot idx (-1 = first free). Returns slot or -1. */
int      scenes_capture(int idx, const char *name);
void     scenes_delete(int idx);
/* Suggest a name for a new scene ("Scene 3"). */
void     scenes_suggest_name(char *out, int cap);

#ifdef __cplusplus
}
#endif
