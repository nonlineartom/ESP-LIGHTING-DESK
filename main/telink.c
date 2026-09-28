#include "telink.h"

#include <string.h>

static uint8_t checksum(const uint8_t p[10])
{
    uint16_t s = 0;
    for (int i = 1; i < 10; i++) s += p[i];
    return (uint8_t)(s & 0xff);
}

void telink_status_request(uint8_t out[10])
{
    memset(out, 0, 10);
    out[9] = TELINK_CMD_STATUS_REQ;
    out[0] = checksum(out);
}

void telink_onoff(bool on, uint8_t out[10])
{
    memset(out, 0, 10);
    out[8] = on ? 0x01 : 0x00;
    out[9] = TELINK_CMD_ONOFF;
    out[0] = checksum(out);
}

void telink_intensity(uint16_t intensity_0_1000, uint8_t out[10])
{
    uint16_t v = intensity_0_1000 > 1000 ? 1000 : intensity_0_1000;
    memset(out, 0, 10);
    out[7] = (uint8_t)((v & 3) << 6);
    out[8] = (uint8_t)((v >> 2) & 0xff);
    out[9] = TELINK_CMD_INTENSITY;
    out[0] = checksum(out);
}

/*
 * CCT + green/magenta.  The CCT field is kelvin/10 packed into bits 52..61 of
 * the low 64 bits; above 10000 K the value wraps with a marker bit (bit 42).
 * gm maps -10..+10 onto the fixture's 0..20 scale (10 = neutral).
 */
void telink_cct(uint16_t kelvin, uint16_t intensity_0_1000, int gm, uint8_t out[10])
{
    int v = intensity_0_1000 > 1000 ? 1000 : intensity_0_1000;

    int tcct = ((int)kelvin + 5) / 10;
    if (tcct < 80) tcct = 80;
    if (tcct > 2000) tcct = 2000;

    int g = gm + 10;
    if (g < 0) g = 0;
    if (g > 20) g = 20;
    const int gm_flag = 0;

    uint64_t low64 = ((uint64_t)(v & 0x03)) << 62;
    uint16_t high16 = (uint16_t)(0x8200 | ((v >> 2) & 0xff));
    if (tcct < 1001) {
        low64 |= (uint64_t)tcct << 52;
        high16 |= (uint16_t)((tcct >> 12) & 0xff);
    } else {
        low64 |= ((uint64_t)((tcct + 0x18) & 0x3ff)) << 52;
        low64 |= 0x0000040000000000ULL;
    }
    low64 |= (uint64_t)(gm_flag & 0x01) << 43;
    low64 |= (uint64_t)(g & 0x7f) << 45;

    for (int i = 0; i < 8; i++) out[i] = (uint8_t)((low64 >> (i * 8)) & 0xff);
    out[8] = (uint8_t)(high16 & 0xff);
    out[9] = (uint8_t)((high16 >> 8) & 0xff);
    out[0] = checksum(out);
}

void telink_hsi(uint16_t hue_deg, uint8_t saturation, uint16_t intensity_0_1000, uint8_t out[10])
{
    uint16_t v = intensity_0_1000 > 1000 ? 1000 : intensity_0_1000;
    uint16_t h = (hue_deg > 360 ? 360 : hue_deg) & 0x1ff;
    uint8_t  s = (saturation > 100 ? 100 : saturation) & 0x7f;

    memset(out, 0, 10);
    out[5] = (uint8_t)((s & 3) << 6);
    out[6] = (uint8_t)(((h & 7) << 5) | ((s >> 2) & 0x1f));
    out[7] = (uint8_t)(((h >> 3) & 0x3f) | ((v & 3) << 6));
    out[8] = (uint8_t)((v >> 2) & 0xff);
    out[9] = TELINK_CMD_HSI;
    out[0] = checksum(out);
}

bool telink_decode_status(const uint8_t p[10], telink_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (p[0] != checksum(p)) return false;

    uint8_t cmd = p[9] & 0x7f;
    out->command_type = cmd;

    uint64_t low64 = 0;
    for (int i = 0; i < 8; i++) low64 |= (uint64_t)p[i] << (i * 8);
    uint16_t high16 = (uint16_t)p[8] | ((uint16_t)p[9] << 8);

    out->on = (low64 >> 8) & 0x01;

    if (cmd == 0x02) {                      /* CCT report */
        out->is_hsi = false;
        int cct_raw  = (int)((low64 >> 52) & 0x3ff);
        int cct_flag = (int)((low64 >> 42) & 0x01);
        int tcct = cct_flag ? cct_raw + 1000 : cct_raw;
        out->cct_kelvin = (uint16_t)(tcct * 10);
        out->intensity = (uint16_t)((((uint32_t)high16 << 2) | (uint32_t)((low64 >> 62) & 0x03)) & 0x3ff);
        int gm = (int)((low64 >> 45) & 0x7f) - 10;
        if (gm < -10) gm = -10;
        if (gm > 10) gm = 10;
        out->gm = (int8_t)gm;
        return true;
    }
    if (cmd == 0x01) {                      /* HSI report */
        out->is_hsi = true;
        uint16_t sat = (uint16_t)(((p[6] & 0x1f) << 2) | ((p[5] >> 6) & 0x03));
        uint16_t hue = (uint16_t)(((p[7] & 0x3f) << 3) | ((p[6] >> 5) & 0x07));
        uint16_t v   = (uint16_t)(((uint16_t)p[8] << 2) | ((p[7] >> 6) & 0x03));
        out->sat = (uint8_t)(sat > 100 ? 100 : sat);
        out->hue = hue > 360 ? 360 : hue;
        out->intensity = v > 1000 ? 1000 : v;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* first-generation effects, power reports                                    */
/* ------------------------------------------------------------------------- */

static const struct {
    const char *name;
    uint64_t    defaults;     /* preset parameters in the low 64 bits */
    uint8_t     ibit;         /* position of the 10-bit intensity field */
} s_fx[TELINK_FX_COUNT] = {
    { "Club lights",  0x0014000000000000ull, 54 },
    { "Paparazzi",    0x0016301400000000ull, 54 },
    { "Lightning",    0x0016301528000000ull, 54 },
    { "TV",           0x0014000000000000ull, 54 },
    { "Candle",       0x0014000000000000ull, 54 },
    { "Fire",         0x0014000000000000ull, 54 },
    { "Strobe",       0x1500001920000000ull, 46 },
    { "Explosion",    0x1500001910000000ull, 46 },
    { "Faulty bulb",  0x1500001925000000ull, 46 },
    { "Pulsing",      0x1500001925000000ull, 46 },
    { "Welding",      0x1500001922400000ull, 46 },
    { "Cop car",      0x0014800000000000ull, 54 },
    { "Color chase",  0x0017200000000000ull, 54 },
    { "Party lights", 0x0017200000000000ull, 54 },
    { "Fireworks",    0x0014000000000000ull, 54 },
};

#define FX_STOP_ID 15

const char *telink_effect_name(uint8_t fx_id)
{
    return fx_id < TELINK_FX_COUNT ? s_fx[fx_id].name : "";
}

void telink_effect(uint8_t fx_id, uint16_t intensity_0_1000, uint8_t out[10])
{
    if (fx_id >= TELINK_FX_COUNT) { telink_effect_stop(out); return; }
    uint64_t v = intensity_0_1000 > 1000 ? 1000 : intensity_0_1000;
    uint64_t lo = s_fx[fx_id].defaults | (1ull << 8) | (v << s_fx[fx_id].ibit);
    for (int i = 0; i < 8; i++) out[i] = (uint8_t)(lo >> (i * 8));
    out[8] = fx_id;
    out[9] = TELINK_CMD_EFFECT;
    out[0] = checksum(out);
}

void telink_effect_stop(uint8_t out[10])
{
    memset(out, 0, 10);
    out[8] = FX_STOP_ID;
    out[9] = TELINK_CMD_EFFECT;
    out[0] = checksum(out);
}

void telink_power_request(uint8_t out[10])
{
    memset(out, 0, 10);
    out[9] = TELINK_CMD_POWER_REQ;
    out[0] = checksum(out);
}

bool telink_decode_effect(const uint8_t p[10], telink_effect_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (p[0] != checksum(p) || (p[9] & 0x7f) != 0x07) return false;
    uint64_t lo = 0;
    for (int i = 0; i < 8; i++) lo |= (uint64_t)p[i] << (i * 8);
    out->on = (lo >> 8) & 1;
    uint8_t id = p[8];
    if (id >= TELINK_FX_COUNT) {
        out->fx_id = TELINK_FX_NONE;
        return true;
    }
    out->fx_id = id;
    uint16_t v = (uint16_t)((lo >> s_fx[id].ibit) & 0x3ff);
    out->intensity = v > 1000 ? 1000 : v;
    return true;
}

bool telink_decode_power(const uint8_t p[10], telink_power_t *out)
{
    memset(out, 0, sizeof(*out));
    if (p[0] != checksum(p) || (p[9] & 0x7f) != TELINK_CMD_POWER_REQ) return false;
    out->battery_minutes = (uint16_t)(p[3] | ((p[4] & 1) << 8));
    uint8_t pct = p[4] >> 1;
    out->battery_pct = pct > 100 ? 100 : pct;
    out->battery_mv = (uint16_t)(p[5] | (p[6] << 8));
    out->external_mv = (uint16_t)(p[7] | (p[8] << 8));
    return true;
}
