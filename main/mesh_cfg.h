/*
 * Configuration Client message builders / parsers (Mesh Profile 4.3).
 * All messages are sent with the node's DevKey via mesh_net_send_access().
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MESH_OP_APPKEY_ADD              0x00
#define MESH_OP_COMPOSITION_DATA_STATUS 0x02
#define MESH_OP_APPKEY_STATUS           0x8003
#define MESH_OP_COMPOSITION_DATA_GET    0x8008
#define MESH_OP_MODEL_APP_BIND          0x803D
#define MESH_OP_MODEL_APP_STATUS        0x803E
#define MESH_OP_NODE_RESET              0x8049
#define MESH_OP_NODE_RESET_STATUS       0x804A

#define MESH_COMP_MAX_ELEMENTS   4
#define MESH_COMP_MAX_SIG_MODELS 16
#define MESH_COMP_MAX_VND_MODELS 6

typedef struct {
    uint16_t loc;
    uint8_t  num_sig;
    uint8_t  num_vnd;
    uint16_t sig[MESH_COMP_MAX_SIG_MODELS];
    uint32_t vnd[MESH_COMP_MAX_VND_MODELS];   /* (company id << 16) | model id */
} mesh_comp_element_t;

typedef struct {
    uint16_t cid, pid, vid, crpl, features;
    uint8_t  elem_count;
    mesh_comp_element_t elems[MESH_COMP_MAX_ELEMENTS];
} mesh_comp_t;

/* Parse the opcode at the start of an access PDU.  Returns opcode length (1..3) or 0. */
size_t mesh_access_opcode(const uint8_t *access, size_t len, uint32_t *opcode);

size_t mesh_cfg_build_composition_get(uint8_t *out, uint8_t page);
size_t mesh_cfg_build_appkey_add(uint8_t *out, uint16_t net_idx, uint16_t app_idx, const uint8_t appkey[16]);
size_t mesh_cfg_build_model_app_bind(uint8_t *out, uint16_t elem_addr, uint16_t app_idx,
                                     uint32_t model_id, bool vendor);
size_t mesh_cfg_build_node_reset(uint8_t *out);

/* Parsers return 0 on success. */
int mesh_cfg_parse_composition(const uint8_t *access, size_t len, mesh_comp_t *out);
int mesh_cfg_parse_appkey_status(const uint8_t *access, size_t len, uint8_t *status);
int mesh_cfg_parse_model_app_status(const uint8_t *access, size_t len, uint8_t *status,
                                    uint16_t *elem_addr, uint32_t *model_id, bool *vendor);

#ifdef __cplusplus
}
#endif
