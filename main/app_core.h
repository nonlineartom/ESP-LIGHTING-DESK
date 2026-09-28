/*
 * Application core: owns the mesh network, the light registry and the BLE
 * link state machine.  Runs in its own task; the UI talks to it through the
 * app_cmd_* functions (thread-safe, queued) and reads state under
 * app_core_lock().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lights.h"
#include "scenes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINK_STARTING = 0,
    LINK_NO_LIGHTS,        /* nothing paired yet */
    LINK_SCANNING,         /* looking for a paired light to use as proxy */
    LINK_CONNECTING,
    LINK_ONLINE,           /* proxy connection up, mesh reachable */
    LINK_PAIRING,
} link_state_t;

typedef enum {
    PAIR_IDLE = 0,
    PAIR_SCANNING,
    PAIR_IN_PROGRESS,
    PAIR_DONE,
    PAIR_FAILED,
} pair_state_t;

typedef struct {
    uint8_t  uuid[16];
    uint8_t  addr[6];
    uint8_t  addr_type;
    int8_t   rssi;
    char     name[24];
    uint32_t last_seen_ms;
} unprov_dev_t;

#define UNPROV_MAX 12

/* app_core_init: create the state lock and load the stored network + lights.
 * Call before ui_init() (the UI reads g_lights under the lock).
 * app_core_start: timers, the core task and BLE. */
void app_core_init(void);
void app_core_start(void);

/* state access (lock while reading g_lights or the pairing list) */
void         app_core_lock(void);
void         app_core_unlock(void);
link_state_t app_core_link_state(void);
const char  *app_core_link_text(void);         /* short human-readable status */
uint16_t     app_core_proxy_addr(void);        /* light we are connected through, or 0 */
pair_state_t app_core_pair_state(void);
const char  *app_core_pair_text(void);
int          app_core_pair_percent(void);
int          app_core_unprov_list(unprov_dev_t *out, int max);

/* commands from the UI */
void app_cmd_pair_start(void);
void app_cmd_pair_select(const uint8_t uuid[16]);
void app_cmd_pair_cancel(void);
void app_cmd_set_power(uint16_t addr, bool on);
void app_cmd_set_intensity(uint16_t addr, uint16_t intensity);
void app_cmd_set_cct(uint16_t addr, uint16_t kelvin, uint16_t intensity, int gm);
void app_cmd_set_hsi(uint16_t addr, uint16_t hue, uint8_t sat, uint16_t intensity);
void app_cmd_request_status(uint16_t addr);
void app_cmd_remove_light(uint16_t addr);
void app_cmd_factory_reset(void);
void app_cmd_rename_light(uint16_t addr, const char *name);

/* desk-level control */
void app_cmd_master_level(uint16_t level_0_1000);      /* grand master, scales every channel */
void app_cmd_all_power(bool on);
void app_cmd_all_cct(uint16_t kelvin);                 /* every light to CCT mode at this temperature */
/* Start built-in effect fx (0..TELINK_FX_COUNT-1) at the light's level, or
 * stop the running effect (TELINK_FX_NONE) and restore its CCT / HSI look. */
void app_cmd_set_effect(uint16_t addr, uint8_t fx);
/* Blink a light three times so it can be found in the rig. */
void app_cmd_identify(uint16_t addr);
uint16_t app_core_master_level(void);

/* scenes */
void app_cmd_scene_save(int idx);                      /* -1 = new slot; existing = overwrite */
void app_cmd_scene_recall(int idx);
void app_cmd_scene_delete(int idx);
void app_cmd_scene_rename(int idx, const char *name);
/* Small persisted UI settings (NVS u16; key up to 15 characters).  Reading
 * is direct, saving goes through the core task. */
uint16_t app_core_setting(const char *key, uint16_t def);
void     app_cmd_save_setting(const char *key, uint16_t value);
int  app_core_last_scene(void);                        /* last recalled scene or -1 */
void app_cmd_set_fade_ms(uint16_t ms);                 /* scene crossfade time, 0 = snap; persisted */
uint16_t app_core_fade_ms(void);
bool app_core_fade_active(void);

/* Diagnostics for the hardware session: standard-model Light Lightness Set
 * with a transition time.  If the fixture dims smoothly, fades can be
 * offloaded to the light instead of stepped from here. */
void app_cmd_diag_lightness(uint16_t addr, uint16_t lightness_0_65535, uint8_t transition_100ms);

#ifdef __cplusplus
}
#endif
