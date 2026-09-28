/*
 * Placement attributes that also compile on the host (tests / simulator).
 *
 * MD_EXT_BSS puts a large zero-initialised table in PSRAM.  Internal RAM is
 * scarce before the scheduler starts: ESP-Hosted's transport pool and task
 * stacks take most of the only region usable that early, so static data in
 * internal RAM directly reduces the margin that keeps the timer task
 * allocation (and boot) working.
 */
#pragma once

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define MD_EXT_BSS EXT_RAM_BSS_ATTR
#else
#define MD_EXT_BSS
#endif
