#include "products.h"

#include <string.h>

/*
 * Sidus product codes -> fixture name and capabilities.
 *
 * Generated from neyako/amaran-hacs (MIT): product.json joined with
 * product_capabilities.json (amaran Desktop 1.1.03 catalogue), using their
 * own derivation rules.  Accessories (motorized yokes / Fresnels) are left
 * out.  P_GUESS rows had no capability record: modes come from the product
 * name and the CCT range is the catalogue's 2300-10000 K fallback.
 * fx is a bit mask of the first-generation (0x87) effects the fixture lists,
 * bit n = effect id n (see telink.h); only those have been confirmed on
 * hardware by the community.
 */
static const sidus_product_t s_products[] = {
    { "00005", "LS C300d II", 5600, 5600, 0x0000, P_NO_CCT },
    { "00015", "LS 50d", 2300, 10000, 0x0000, P_NO_CCT | P_GUESS },
    { "00025", "LS 50x", 2300, 10000, 0x0000, P_GUESS },
    { "00035", "LS 150x", 2300, 10000, 0x0000, P_GUESS },
    { "00045", "LS 300x", 2700, 6500, 0x0000, 0 },
    { "00055", "LS 600d Pro", 5600, 5600, 0x0000, P_NO_CCT },
    { "00065", "LS 1500d", 2300, 10000, 0x0000, P_NO_CCT | P_GUESS },
    { "00075", "LS 60d", 5600, 5600, 0x0000, P_NO_CCT },
    { "00085", "LS 60x", 2700, 6500, 0x0000, 0 },
    { "00095", "LS 600x", 2300, 10000, 0x0000, P_GUESS },
    { "000B5", "LS 350d", 2300, 10000, 0x0000, P_NO_CCT | P_GUESS },
    { "000C5", "LS 600d", 5600, 5600, 0x0000, P_NO_CCT },
    { "000D5", "LS 600X Pro", 2700, 6500, 0x0000, 0 },
    { "000E5", "LS 1200d Pro", 5600, 5600, 0x0000, P_NO_CCT },
    { "000F5", "MT Pro", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "000G5", "LS 600c Pro", 2300, 10000, 0x7fff, P_HSI | P_GM },
    { "000I5", "LS 1500x Pro", 2300, 10000, 0x0000, P_GUESS },
    { "000J5", "Electro Storm XT26", 2700, 6500, 0x0000, P_GM },
    { "000K5", "Electro Storm CS15", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "000L5", "LS 600c Pro II", 2300, 10000, 0x7fff, P_HSI | P_GM },
    { "000M5", "STORM 1200x", 2500, 10000, 0x0000, P_GM },
    { "000N5", "STORM 80c", 1800, 20000, 0x7fff, P_HSI | P_GM },
    { "000P5", "STORM 1000c", 1800, 20000, 0x7fff, P_HSI | P_GM },
    { "000Q5", "STORM 400x", 2500, 10000, 0x0000, P_GM },
    { "000R5", "STORM XT52", 2500, 10000, 0x43ee, P_HSI | P_GM },
    { "000S5", "STORM 700x", 2500, 10000, 0x43fe, P_HSI | P_GM },
    { "02005", "Nova P300c", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "02035", "Nova P600C", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "02065", "NOVA II 2x1", 1800, 20000, 0x1011, P_HSI | P_GM },
    { "02075", "NOVA 9\xC2\xB0" " 2x1", 1800, 20000, 0x1011, P_HSI | P_GM },
    { "02085", "NOVA II 1x1", 1800, 20000, 0x1011, P_HSI | P_GM },
    { "04005", "B7c", 2000, 10000, 0x6b2e, P_HSI | P_GM },
    { "05005", "MC", 3200, 6500, 0x6b2e, P_HSI },
    { "05006", "MC", 3200, 6500, 0x6b2e, P_HSI },
    { "05007", "MC", 3200, 6500, 0x6b2e, P_HSI },
    { "05008", "MC", 3200, 6500, 0x6b2e, P_HSI },
    { "05010", "MC Pro", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "07006", "INFINIBAR PB3", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "07007", "INFINIBAR PB6", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "07008", "INFINIBAR PB12", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "09005", "INFINIMAT 4", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "09015", "INFINIMAT 16", 2000, 10000, 0x7fff, P_HSI | P_GM },
    { "20005", "SGC-H60", 2300, 10000, 0x0000, P_GUESS },
    { "20015", "SGC-H120", 2300, 10000, 0x0000, P_GUESS },
    { "20025", "SGC-P60", 2300, 10000, 0x0000, P_GUESS },
    { "20035", "SGC-P120", 2300, 10000, 0x0000, P_GUESS },
    { "40005", "amaran P60x", 3200, 6500, 0x0000, 0 },
    { "40015", "amaran P60c", 2500, 7500, 0x6b2e, P_HSI | P_GM },
    { "40025", "amaran 100d", 5600, 5600, 0x0000, P_NO_CCT },
    { "40035", "amaran 100x", 2700, 6500, 0x0000, 0 },
    { "40045", "amaran 200d", 5600, 5600, 0x0000, P_NO_CCT },
    { "40055", "amaran 200x", 2700, 6500, 0x0000, 0 },
    { "40065", "amaran COB 60d", 5600, 5600, 0x0000, P_NO_CCT },
    { "40075", "amaran COB 60x", 2700, 6500, 0x0000, 0 },
    { "40085", "amaran T2c", 2500, 7500, 0x7fff, P_HSI | P_GM },
    { "40095", "amaran T4c", 2500, 7500, 0x7fff, P_HSI | P_GM },
    { "400A5", "amaran F7c", 2300, 10000, 0x0000, P_HSI | P_GUESS },
    { "400B5", "amaran F21x", 2500, 7500, 0x0000, 0 },
    { "400C5", "amaran F21c", 2500, 7500, 0x6b2e, P_HSI | P_GM },
    { "400D5", "amaran F22x", 2500, 7500, 0x0000, 0 },
    { "400E5", "amaran F22c", 2500, 7500, 0x6b2e, P_HSI | P_GM },
    { "400F5", "amaran SM5c", 3200, 6500, 0x6b2e, P_HSI | P_GM },
    { "400G5", "amaran PT1c", 2700, 10000, 0x6b2e, P_HSI | P_GM | P_BATT },
    { "400H5", "amaran PT2c", 2700, 10000, 0x6b2e, P_HSI | P_GM | P_BATT },
    { "400I5", "amaran PT4c", 2700, 10000, 0x6b2e, P_HSI | P_GM | P_BATT },
    { "400J5", "amaran 150c", 2500, 7500, 0x6b2e, P_HSI | P_GM },
    { "400K5", "amaran 300c", 2500, 7500, 0x6b2e, P_HSI | P_GM },
    { "400L5", "amaran 60d S", 5600, 5600, 0x0000, P_NO_CCT },
    { "400M5", "amaran 60x S", 2700, 6500, 0x0000, 0 },
    { "400N5", "amaran 100d S", 5600, 5600, 0x0000, P_NO_CCT },
    { "400O5", "amaran 100x S", 2700, 6500, 0x0000, 0 },
    { "400P5", "amaran 200d S", 5600, 5600, 0x0000, P_NO_CCT },
    { "400Q5", "amaran 200x S", 2700, 6500, 0x0000, 0 },
    { "400R5", "amaran 200c", 2500, 7500, 0x6fee, P_HSI | P_GM },
    { "400S5", "amaran P40x", 2700, 6500, 0x0000, 0 },
    { "400T5", "amaran Ace 25x", 2700, 6500, 0x0000, 0 },
    { "400U5", "amaran Ace 25c", 2300, 10000, 0x6fee, P_HSI | P_GM | P_BATT },
    { "400V5", "amaran Go", 2700, 6500, 0x0000, 0 },
    { "400W5", "amaran Pano 60c", 1800, 20000, 0x6fee, P_HSI | P_GM },
    { "400X5", "amaran Pano 120c", 1800, 20000, 0x6fee, P_HSI | P_GM },
    { "400Y5", "amaran Verge", 2700, 6500, 0x0000, 0 },
    { "400Z5", "amaran Verge Max", 2700, 6500, 0x0000, 0 },
    { "40145", "amaran Ray 60c", 1800, 20000, 0x6fee, P_HSI | P_GM },
    { "40165", "amaran Ray 120c", 1800, 20000, 0x6fee, P_HSI | P_GM },
    { "40185", "amaran Ray 360c", 1800, 20000, 0x6fee, P_HSI | P_GM },
    { "40195", "amaran 300x", 2700, 6500, 0x0000, 0 },
    { "401A5", "amaran Ray 660c", 1800, 20000, 0x6fee, P_HSI | P_GM },
    { "401B5", "amaran Halo 60x", 2700, 6500, 0x0000, 0 },
    { "401C5", "amaran Halo 100x", 2700, 6500, 0x0000, 0 },
    { "401D5", "amaran Halo 200x", 2700, 6500, 0x0000, 0 },
    { "401E5", "amaran Halo 300x", 2700, 6500, 0x0000, 0 },
    { "401F5", "amaran Halo 600x", 2700, 6500, 0x0000, 0 },
};

bool sidus_uuid_product_code(const uint8_t uuid[16], char code[6])
{
    /* Sidus lights use an ASCII device UUID: "<code>-<MAC tail>" + 4 bytes,
     * e.g. "400H5-DBFA30" + 01 0f 0f 0f for an amaran PT2c. */
    for (int i = 0; i < 5; i++) {
        uint8_t c = uuid[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        if (!ok) return false;
        code[i] = (char)c;
    }
    if (uuid[5] != '-') return false;
    code[5] = 0;
    return true;
}

bool sidus_uuid_mac_tail(const uint8_t uuid[16], char tail[7])
{
    char code[6];
    if (!sidus_uuid_product_code(uuid, code)) return false;
    for (int i = 0; i < 6; i++) {
        uint8_t c = uuid[6 + i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        if (!ok) return false;
        tail[i] = (char)c;
    }
    tail[6] = 0;
    return true;
}

const sidus_product_t *sidus_product_find(const char *code)
{
    for (size_t i = 0; i < sizeof(s_products) / sizeof(s_products[0]); i++) {
        if (strcmp(s_products[i].code, code) == 0) return &s_products[i];
    }
    return NULL;
}

const sidus_product_t *sidus_product_for_uuid(const uint8_t uuid[16])
{
    char code[6];
    return sidus_uuid_product_code(uuid, code) ? sidus_product_find(code) : NULL;
}

const char *sidus_product_name(const char *code)
{
    const sidus_product_t *p = sidus_product_find(code);
    return p ? p->name : NULL;
}

const char *sidus_product_from_uuid(const uint8_t uuid[16])
{
    const sidus_product_t *p = sidus_product_for_uuid(uuid);
    return p ? p->name : NULL;
}
