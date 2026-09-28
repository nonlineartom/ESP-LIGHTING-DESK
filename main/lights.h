/*
 * Light registry: the provisioned fixtures, their mesh identity, capabilities
 * and last known state.  Persisted through store.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "telink.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHTS_MAX      8
#define LIGHT_NAME_LEN  20

typedef enum {
    LIGHT_MODE_CCT = 0,
    LIGHT_MODE_HSI = 1,
} light_mode_t;

typedef struct {
    bool         on;
    light_mode_t mode;
    uint16_t     intensity;   /* 0..1000 output level as reported/commanded */
    uint16_t     level;       /* 0..1000 channel fader before the grand master */
    uint16_t     cct;         /* kelvin */
    int8_t       gm;          /* -10..+10 */
    uint16_t     hue;         /* 0..360 */
    uint8_t      sat;         /* 0..100 */
    bool         reported;    /* at least one status report received */
    uint32_t     last_report_ms;
} light_state_t;

typedef struct {
    bool     used;
    uint8_t  uuid[16];
    uint16_t addr;            /* primary element unicast address */
    uint8_t  elem_count;
    uint8_t  devkey[16];
    uint8_t  ble_addr[6];
    uint8_t  ble_addr_type;
    char     name[LIGHT_NAME_LEN];
    uint16_t cid, pid;
    bool     configured;      /* AppKey added and bound */

    /* capabilities (conservative defaults: full colour, 2500..7500 K, G/M) */
    uint16_t cct_min, cct_max;
    bool     has_hsi;
    bool     has_gm;

    light_state_t state;      /* runtime only */
} light_t;

typedef struct {
    bool     valid;
    uint8_t  netkey[16];
    uint8_t  appkey[16];
    uint32_t iv_index;
    uint32_t seq;
    uint16_t next_addr;
} mesh_network_cfg_t;

/* The registry itself (owned by app_core; guard with app_core_lock()). */
extern light_t g_lights[LIGHTS_MAX];

int      lights_count(void);
light_t *lights_find_by_addr(uint16_t addr);          /* any element address of the node */
light_t *lights_find_by_uuid(const uint8_t uuid[16]);
light_t *lights_add(const uint8_t uuid[16], uint16_t addr, uint8_t elem_count,
                    const uint8_t devkey[16], const char *name);
void     lights_remove(light_t *l);
void     lights_apply_status(light_t *l, const telink_status_t *st, uint32_t now_ms);

/* Approximate RGB (0..255) of what the fixture is emitting, for the UI. */
void     light_preview_rgb(const light_t *l, uint8_t *r, uint8_t *g, uint8_t *b);
void     cct_to_rgb(uint16_t kelvin, uint8_t *r, uint8_t *g, uint8_t *b);
void     hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val, uint8_t *r, uint8_t *g, uint8_t *b);

#ifdef __cplusplus
}
#endif
