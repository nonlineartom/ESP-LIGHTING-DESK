#include "products.h"

#include <string.h>

/*
 * Sidus product codes -> fixture names.
 * Generated from product.json in neyako/amaran-hacs (MIT), which snapshots the
 * amaran Desktop 1.1.03 product catalogue.  Fixtures only.
 */
static const struct { char code[6]; const char *name; } s_products[] = {
    { "00005", "LS C300d II" },
    { "00015", "LS 50d" },
    { "00025", "LS 50x" },
    { "00035", "LS 150x" },
    { "00045", "LS 300x" },
    { "00055", "LS 600d Pro" },
    { "00065", "LS 1500d" },
    { "00075", "LS 60d" },
    { "00085", "LS 60x" },
    { "00095", "LS 600x" },
    { "000B5", "LS 350d" },
    { "000C5", "LS 600d" },
    { "000D5", "LS 600X Pro" },
    { "000E5", "LS 1200d Pro" },
    { "000F5", "MT Pro" },
    { "000G5", "LS 600c Pro" },
    { "000I5", "LS 1500x Pro" },
    { "000J5", "Electro Storm XT26" },
    { "000K5", "Electro Storm CS15" },
    { "000L5", "LS 600c Pro II" },
    { "000M5", "STORM 1200x" },
    { "000N5", "STORM 80c" },
    { "000P5", "STORM 1000c" },
    { "000Q5", "STORM 400x" },
    { "000R5", "STORM XT52" },
    { "000S5", "STORM 700x" },
    { "02005", "Nova P300c" },
    { "02035", "Nova P600C" },
    { "02065", "NOVA II 2x1" },
    { "02075", "NOVA 9° 2×1" },
    { "02085", "NOVA II 1x1" },
    { "04005", "B7c" },
    { "05005", "MC" },
    { "05006", "MC" },
    { "05007", "MC" },
    { "05008", "MC" },
    { "05010", "MC Pro" },
    { "07006", "INFINIBAR PB3" },
    { "07007", "INFINIBAR PB6" },
    { "07008", "INFINIBAR PB12" },
    { "09005", "INFINIMAT 4" },
    { "09015", "INFINIMAT 16" },
    { "20005", "SGC-H60" },
    { "20015", "SGC-H120" },
    { "20025", "SGC-P60" },
    { "20035", "SGC-P120" },
    { "40005", "amaran P60x" },
    { "40015", "amaran P60c" },
    { "40025", "amaran 100d" },
    { "40035", "amaran 100x" },
    { "40045", "amaran 200d" },
    { "40055", "amaran 200x" },
    { "40065", "amaran COB 60d" },
    { "40075", "amaran COB 60x" },
    { "40085", "amaran T2c" },
    { "40095", "amaran T4c" },
    { "400A5", "amaran F7c" },
    { "400B5", "amaran F21x" },
    { "400C5", "amaran F21c" },
    { "400D5", "amaran F22x" },
    { "400E5", "amaran F22c" },
    { "400F5", "amaran SM5c" },
    { "400G5", "amaran PT1c" },
    { "400H5", "amaran PT2c" },
    { "400I5", "amaran PT4c" },
    { "400J5", "amaran 150c" },
    { "400K5", "amaran 300c" },
    { "400L5", "amaran 60d S" },
    { "400M5", "amaran 60x S" },
    { "400N5", "amaran 100d S" },
    { "400O5", "amaran 100x S" },
    { "400P5", "amaran 200d S" },
    { "400Q5", "amaran 200x S" },
    { "400R5", "amaran 200c" },
    { "400S5", "amaran P40x" },
    { "400T5", "amaran Ace 25x" },
    { "400U5", "amaran Ace 25c" },
    { "400V5", "amaran Go" },
    { "400W5", "amaran Pano 60c" },
    { "400X5", "amaran Pano 120c" },
    { "400Y5", "amaran Verge" },
    { "400Z5", "amaran Verge Max" },
    { "40145", "amaran Ray 60c" },
    { "40165", "amaran Ray 120c" },
    { "40185", "amaran Ray 360c" },
    { "40195", "amaran 300x" },
    { "401A5", "amaran Ray 660c" },
    { "401B5", "amaran Halo 60x" },
    { "401C5", "amaran Halo 100x" },
    { "401D5", "amaran Halo 200x" },
    { "401E5", "amaran Halo 300x" },
    { "401F5", "amaran Halo 600x" },
    { "EM005", "Motorized Yoke" },
    { "EM015", "Motorized F14 Fresnel" },
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

const char *sidus_product_name(const char *code)
{
    for (size_t i = 0; i < sizeof(s_products) / sizeof(s_products[0]); i++) {
        if (strcmp(s_products[i].code, code) == 0) return s_products[i].name;
    }
    return NULL;
}

const char *sidus_product_from_uuid(const uint8_t uuid[16])
{
    char code[6];
    return sidus_uuid_product_code(uuid, code) ? sidus_product_name(code) : NULL;
}
