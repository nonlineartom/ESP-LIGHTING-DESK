#include "mesh_cfg.h"

#include <string.h>

size_t mesh_access_opcode(const uint8_t *access, size_t len, uint32_t *opcode)
{
    if (len < 1) return 0;
    uint8_t b0 = access[0];
    if (b0 < 0x80) {
        *opcode = b0;
        return 1;
    }
    if (b0 < 0xc0) {
        if (len < 2) return 0;
        *opcode = (uint32_t)((b0 << 8) | access[1]);
        return 2;
    }
    if (len < 3) return 0;
    *opcode = ((uint32_t)b0 << 16) | ((uint32_t)access[1] << 8) | access[2];
    return 3;
}

static size_t put_op2(uint8_t *out, uint16_t op)
{
    out[0] = (uint8_t)(op >> 8);
    out[1] = (uint8_t)op;
    return 2;
}

size_t mesh_cfg_build_composition_get(uint8_t *out, uint8_t page)
{
    size_t n = put_op2(out, MESH_OP_COMPOSITION_DATA_GET);
    out[n++] = page;
    return n;
}

size_t mesh_cfg_build_appkey_add(uint8_t *out, uint16_t net_idx, uint16_t app_idx, const uint8_t appkey[16])
{
    out[0] = MESH_OP_APPKEY_ADD;
    /* two 12-bit indexes packed little-endian into 3 octets */
    out[1] = (uint8_t)(net_idx & 0xff);
    out[2] = (uint8_t)(((net_idx >> 8) & 0x0f) | ((app_idx & 0x0f) << 4));
    out[3] = (uint8_t)((app_idx >> 4) & 0xff);
    memcpy(out + 4, appkey, 16);
    return 20;
}

size_t mesh_cfg_build_model_app_bind(uint8_t *out, uint16_t elem_addr, uint16_t app_idx,
                                     uint32_t model_id, bool vendor)
{
    size_t n = put_op2(out, MESH_OP_MODEL_APP_BIND);
    out[n++] = (uint8_t)elem_addr;
    out[n++] = (uint8_t)(elem_addr >> 8);
    out[n++] = (uint8_t)app_idx;
    out[n++] = (uint8_t)(app_idx >> 8);
    if (vendor) {
        uint16_t cid = (uint16_t)(model_id >> 16), mid = (uint16_t)model_id;
        out[n++] = (uint8_t)cid;
        out[n++] = (uint8_t)(cid >> 8);
        out[n++] = (uint8_t)mid;
        out[n++] = (uint8_t)(mid >> 8);
    } else {
        out[n++] = (uint8_t)model_id;
        out[n++] = (uint8_t)(model_id >> 8);
    }
    return n;
}

size_t mesh_cfg_build_node_reset(uint8_t *out)
{
    return put_op2(out, MESH_OP_NODE_RESET);
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int mesh_cfg_parse_composition(const uint8_t *access, size_t len, mesh_comp_t *out)
{
    uint32_t op;
    size_t oplen = mesh_access_opcode(access, len, &op);
    if (!oplen || op != MESH_OP_COMPOSITION_DATA_STATUS) return -1;
    const uint8_t *p = access + oplen;
    size_t rem = len - oplen;
    if (rem < 1 + 10) return -1;
    if (p[0] != 0x00) return -1;   /* page 0 only */
    p++; rem--;

    memset(out, 0, sizeof(*out));
    out->cid = le16(p); out->pid = le16(p + 2); out->vid = le16(p + 4);
    out->crpl = le16(p + 6); out->features = le16(p + 8);
    p += 10; rem -= 10;

    while (rem >= 4 && out->elem_count < MESH_COMP_MAX_ELEMENTS) {
        mesh_comp_element_t *e = &out->elems[out->elem_count];
        e->loc = le16(p);
        uint8_t ns = p[2], nv = p[3];
        p += 4; rem -= 4;
        if (rem < (size_t)ns * 2 + (size_t)nv * 4) return -1;
        e->num_sig = ns;
        e->num_vnd = nv;
        for (uint8_t i = 0; i < ns; i++, p += 2, rem -= 2) {
            if (i < MESH_COMP_MAX_SIG_MODELS) e->sig[i] = le16(p);
        }
        for (uint8_t i = 0; i < nv; i++, p += 4, rem -= 4) {
            if (i < MESH_COMP_MAX_VND_MODELS) e->vnd[i] = ((uint32_t)le16(p) << 16) | le16(p + 2);
        }
        if (e->num_sig > MESH_COMP_MAX_SIG_MODELS) e->num_sig = MESH_COMP_MAX_SIG_MODELS;
        if (e->num_vnd > MESH_COMP_MAX_VND_MODELS) e->num_vnd = MESH_COMP_MAX_VND_MODELS;
        out->elem_count++;
    }
    return out->elem_count ? 0 : -1;
}

int mesh_cfg_parse_appkey_status(const uint8_t *access, size_t len, uint8_t *status)
{
    uint32_t op;
    size_t oplen = mesh_access_opcode(access, len, &op);
    if (!oplen || op != MESH_OP_APPKEY_STATUS || len < oplen + 4) return -1;
    *status = access[oplen];
    return 0;
}

int mesh_cfg_parse_model_app_status(const uint8_t *access, size_t len, uint8_t *status,
                                    uint16_t *elem_addr, uint32_t *model_id, bool *vendor)
{
    uint32_t op;
    size_t oplen = mesh_access_opcode(access, len, &op);
    if (!oplen || op != MESH_OP_MODEL_APP_STATUS || len < oplen + 7) return -1;
    const uint8_t *p = access + oplen;
    *status = p[0];
    *elem_addr = le16(p + 1);
    size_t rem = len - oplen - 5;
    if (rem >= 4) {
        *vendor = true;
        *model_id = ((uint32_t)le16(p + 5) << 16) | le16(p + 7);
    } else {
        *vendor = false;
        *model_id = le16(p + 5);
    }
    return 0;
}
