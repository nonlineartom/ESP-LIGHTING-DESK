/*
 * Telink / Sidus proprietary light-control payloads carried in mesh access
 * opcode 0x26.  Wire format reverse-engineered by the community:
 *   - wesbos/amaran-BLE-control (telink.ts / esp32-firmware/main/telink.c)
 *   - kevinschaich/amaran-bridge
 *   - neyako/amaran-hacs (docs/protocol.md, bit-level field map)
 *
 * Every payload is 10 bytes: p[0] = checksum (sum of p[1..9] & 0xff),
 * p[9] = command type (bit 7 = setter, clear = report).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELINK_OPCODE          0x26
#define TELINK_PAYLOAD_LEN     10

#define TELINK_CMD_STATUS_REQ  0x0e
#define TELINK_CMD_HSI         0x81
#define TELINK_CMD_CCT         0x82
#define TELINK_CMD_ONOFF       0x8c
#define TELINK_CMD_INTENSITY   0x8f

/* Builders — all write exactly TELINK_PAYLOAD_LEN bytes to out. */
void telink_status_request(uint8_t out[10]);
void telink_onoff(bool on, uint8_t out[10]);
void telink_intensity(uint16_t intensity_0_1000, uint8_t out[10]);
void telink_cct(uint16_t kelvin, uint16_t intensity_0_1000, int gm_minus10_plus10, uint8_t out[10]);
void telink_hsi(uint16_t hue_0_360, uint8_t sat_0_100, uint16_t intensity_0_1000, uint8_t out[10]);

typedef struct {
    bool     on;
    bool     is_hsi;        /* true: HSI report (0x01), false: CCT report (0x02) */
    uint16_t intensity;     /* 0..1000 */
    uint16_t cct_kelvin;    /* CCT mode */
    int8_t   gm;            /* -10..+10 (CCT mode) */
    uint16_t hue;           /* 0..360 (HSI mode) */
    uint8_t  sat;           /* 0..100 (HSI mode) */
    uint8_t  command_type;  /* p[9] & 0x7f */
} telink_status_t;

/* Decode a 10-byte status report.  Returns false for bad checksum or a
 * non-light-state page (e.g. 0x0a power diagnostics). */
bool telink_decode_status(const uint8_t p[10], telink_status_t *out);

#ifdef __cplusplus
}
#endif
