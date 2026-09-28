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

/* code: 5 chars + NUL, e.g. "400H5".  False if the UUID is not Sidus-style. */
bool sidus_uuid_product_code(const uint8_t uuid[16], char code[6]);
/* tail: 6 hex chars + NUL, e.g. "DBFA30". */
bool sidus_uuid_mac_tail(const uint8_t uuid[16], char tail[7]);
/* "amaran PT2c", or NULL for an unknown code. */
const char *sidus_product_name(const char *code);
const char *sidus_product_from_uuid(const uint8_t uuid[16]);
