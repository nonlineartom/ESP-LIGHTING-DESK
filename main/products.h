/*
 * Fixture identification from the Sidus mesh device UUID.
 *
 * Sidus lights carry no product ID in their composition data (PID 0) and
 * expose only a generic "Mesh Device" GATT name, but their device UUID is
 * ASCII: "<5-char product code>-<last 6 hex digits of the BLE MAC>".
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define P_HSI     0x01   /* hue / saturation colour mode */
#define P_GM      0x02   /* green / magenta tint in CCT mode */
#define P_BATT    0x04   /* battery powered: answers power / battery queries */
#define P_NO_CCT  0x08   /* no white (CCT) control */
#define P_GUESS   0x10   /* no catalogue capability record; modes guessed */

typedef struct {
    char        code[6];
    const char *name;
    uint16_t    cct_min, cct_max;   /* kelvin */
    uint16_t    fx;                 /* first-generation effect ids, bit mask */
    uint8_t     flags;              /* P_* */
} sidus_product_t;

/* code: 5 chars + NUL, e.g. "400H5".  False if the UUID is not Sidus-style. */
bool sidus_uuid_product_code(const uint8_t uuid[16], char code[6]);
/* tail: 6 hex chars + NUL, e.g. "DBFA30". */
bool sidus_uuid_mac_tail(const uint8_t uuid[16], char tail[7]);

const sidus_product_t *sidus_product_find(const char *code);
const sidus_product_t *sidus_product_for_uuid(const uint8_t uuid[16]);
/* "amaran PT2c", or NULL for an unknown code. */
const char *sidus_product_name(const char *code);
const char *sidus_product_from_uuid(const uint8_t uuid[16]);
