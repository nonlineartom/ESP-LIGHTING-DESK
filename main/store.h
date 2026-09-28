/* NVS persistence for the mesh network configuration and the light registry. */
#pragma once

#include "lights.h"
#include "scenes.h"

#ifdef __cplusplus
extern "C" {
#endif

int  store_init(void);
int  store_load_network(mesh_network_cfg_t *cfg);
int  store_save_network(const mesh_network_cfg_t *cfg);
int  store_save_seq(uint32_t seq);
int  store_load_lights(void);       /* into g_lights */
int  store_save_lights(void);       /* from g_lights */
int  store_load_scenes(void);      /* into g_scenes */
int  store_save_scenes(void);
int  store_erase(void);

#ifdef __cplusplus
}
#endif
