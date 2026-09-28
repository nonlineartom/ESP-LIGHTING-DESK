#include "store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "store";
#define NS "meshdeck"

/* Persisted subset of light_t (runtime state excluded). */
typedef struct __attribute__((packed)) {
    uint8_t  uuid[16];
    uint16_t addr;
    uint8_t  elem_count;
    uint8_t  devkey[16];
    uint8_t  ble_addr[6];
    uint8_t  ble_addr_type;
    char     name[LIGHT_NAME_LEN];
    uint16_t cid, pid;
    uint8_t  configured;
    uint16_t cct_min, cct_max;
    uint8_t  has_hsi, has_gm;
} light_rec_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  netkey[16];
    uint8_t  appkey[16];
    uint32_t iv_index;
    uint16_t next_addr;
} net_rec_t;

#define NET_MAGIC 0x4d444b31u   /* "MDK1" */

/* "lights" and "scenes" blobs start with this header.  Blobs written by the
 * first firmware have none; they are recognised by their size and rewritten
 * with a header on the next save.  Bump a version when its record changes. */
typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t rec_size;
    uint16_t count;
} blob_hdr_t;

#define LIGHTS_VERSION 1
#define SCENES_VERSION 1

int store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err == ESP_OK ? 0 : -1;
}

int store_load_network(mesh_network_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return -1;
    net_rec_t rec;
    size_t len = sizeof(rec);
    esp_err_t err = nvs_get_blob(h, "net", &rec, &len);
    if (err == ESP_OK && len == sizeof(rec) && rec.magic == NET_MAGIC) {
        memcpy(cfg->netkey, rec.netkey, 16);
        memcpy(cfg->appkey, rec.appkey, 16);
        cfg->iv_index = rec.iv_index;
        cfg->next_addr = rec.next_addr;
        uint32_t seq = 0;
        nvs_get_u32(h, "seq", &seq);
        cfg->seq = seq;
        cfg->valid = true;
    }
    nvs_close(h);
    return cfg->valid ? 0 : -1;
}

int store_save_network(const mesh_network_cfg_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    net_rec_t rec = { .magic = NET_MAGIC, .iv_index = cfg->iv_index, .next_addr = cfg->next_addr };
    memcpy(rec.netkey, cfg->netkey, 16);
    memcpy(rec.appkey, cfg->appkey, 16);
    esp_err_t err = nvs_set_blob(h, "net", &rec, sizeof(rec));
    if (err == ESP_OK) err = nvs_set_u32(h, "seq", cfg->seq);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save network: %s", esp_err_to_name(err));
    return err == ESP_OK ? 0 : -1;
}

int store_save_seq(uint32_t seq)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_set_u32(h, "seq", seq);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}

int store_load_lights(void)
{
    memset(g_lights, 0, sizeof(g_lights));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return -1;
    uint8_t buf[sizeof(blob_hdr_t) + LIGHTS_MAX * sizeof(light_rec_t)];
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_blob(h, "lights", buf, &len);
    nvs_close(h);
    if (err != ESP_OK) return -1;
    const light_rec_t *recs;
    int n;
    blob_hdr_t hdr;
    memcpy(&hdr, buf, len >= sizeof(hdr) ? sizeof(hdr) : 0);
    if (len >= sizeof(hdr) && hdr.version == LIGHTS_VERSION && hdr.rec_size == sizeof(light_rec_t) &&
        len == sizeof(hdr) + hdr.count * sizeof(light_rec_t)) {
        recs = (const light_rec_t *)(buf + sizeof(hdr));
        n = hdr.count;
    } else if (len % sizeof(light_rec_t) == 0) {
        recs = (const light_rec_t *)buf;          /* legacy, no header */
        n = (int)(len / sizeof(light_rec_t));
    } else {
        ESP_LOGE(TAG, "lights blob not recognised (%u bytes); ignoring it", (unsigned)len);
        return -1;
    }
    if (n > LIGHTS_MAX) n = LIGHTS_MAX;
    for (int i = 0; i < n; i++) {
        light_t *l = lights_add(recs[i].uuid, recs[i].addr, recs[i].elem_count, recs[i].devkey, recs[i].name);
        if (!l) break;
        /* lights_add() prefers the product name; a stored name wins */
        if (recs[i].name[0]) {
            memcpy(l->name, recs[i].name, LIGHT_NAME_LEN);
            l->name[LIGHT_NAME_LEN - 1] = 0;
        }
        memcpy(l->ble_addr, recs[i].ble_addr, 6);
        l->ble_addr_type = recs[i].ble_addr_type;
        l->cid = recs[i].cid;
        l->pid = recs[i].pid;
        l->configured = recs[i].configured;
        l->cct_min = recs[i].cct_min ? recs[i].cct_min : 2500;
        l->cct_max = recs[i].cct_max ? recs[i].cct_max : 7500;
        l->has_hsi = recs[i].has_hsi;
        l->has_gm = recs[i].has_gm;
        lights_apply_product_caps(l);    /* the product table wins when known */
    }
    ESP_LOGI(TAG, "loaded %d light(s)", n);
    return n;
}

int store_save_lights(void)
{
    uint8_t buf[sizeof(blob_hdr_t) + LIGHTS_MAX * sizeof(light_rec_t)];
    light_rec_t *recs = (light_rec_t *)(buf + sizeof(blob_hdr_t));
    int n = 0;
    for (int i = 0; i < LIGHTS_MAX; i++) {
        const light_t *l = &g_lights[i];
        if (!l->used) continue;
        light_rec_t *r = &recs[n++];
        memset(r, 0, sizeof(*r));
        memcpy(r->uuid, l->uuid, 16);
        r->addr = l->addr;
        r->elem_count = l->elem_count;
        memcpy(r->devkey, l->devkey, 16);
        memcpy(r->ble_addr, l->ble_addr, 6);
        r->ble_addr_type = l->ble_addr_type;
        memcpy(r->name, l->name, LIGHT_NAME_LEN);
        r->cid = l->cid;
        r->pid = l->pid;
        r->configured = l->configured;
        r->cct_min = l->cct_min;
        r->cct_max = l->cct_max;
        r->has_hsi = l->has_hsi;
        r->has_gm = l->has_gm;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    blob_hdr_t hdr = { .version = LIGHTS_VERSION, .rec_size = sizeof(light_rec_t), .count = (uint16_t)n };
    memcpy(buf, &hdr, sizeof(hdr));
    esp_err_t err = n ? nvs_set_blob(h, "lights", buf, sizeof(hdr) + n * sizeof(light_rec_t))
                      : nvs_erase_key(h, "lights");
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save lights: %s", esp_err_to_name(err));
    return err == ESP_OK ? 0 : -1;
}

int store_load_scenes(void)
{
    memset(g_scenes, 0, sizeof(g_scenes));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return -1;
    uint8_t buf[sizeof(blob_hdr_t) + sizeof(g_scenes)];
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_blob(h, "scenes", buf, &len);
    nvs_close(h);
    if (err != ESP_OK) return -1;
    blob_hdr_t hdr;
    memcpy(&hdr, buf, len >= sizeof(hdr) ? sizeof(hdr) : 0);
    if (len == sizeof(buf) && hdr.version == SCENES_VERSION && hdr.rec_size == sizeof(scene_t) &&
        hdr.count == SCENES_MAX) {
        memcpy(g_scenes, buf + sizeof(hdr), sizeof(g_scenes));
    } else if (len == sizeof(g_scenes)) {
        memcpy(g_scenes, buf, sizeof(g_scenes));  /* legacy, no header */
    } else {
        ESP_LOGE(TAG, "scenes blob not recognised (%u bytes); ignoring it", (unsigned)len);
        return -1;
    }
    return scenes_count();
}

int store_save_scenes(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    uint8_t buf[sizeof(blob_hdr_t) + sizeof(g_scenes)];
    blob_hdr_t hdr = { .version = SCENES_VERSION, .rec_size = sizeof(scene_t), .count = SCENES_MAX };
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), g_scenes, sizeof(g_scenes));
    esp_err_t err = nvs_set_blob(h, "scenes", buf, sizeof(buf));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save scenes: %s", esp_err_to_name(err));
    return err == ESP_OK ? 0 : -1;
}

int store_load_u16(const char *key, uint16_t *out)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_get_u16(h, key, out);
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}

int store_save_u16(const char *key, uint16_t v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_set_u16(h, key, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}

int store_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    return 0;
}
