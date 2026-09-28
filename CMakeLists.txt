# MeshDeck — standalone Bluetooth-mesh controller for Aputure / amaran (Sidus) lights
# Target: Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (ESP32-P4 + ESP32-C6 over SDIO, 4" 720x720 round MIPI-DSI)
cmake_minimum_required(VERSION 3.16)

# Every dependency is vendored or a git submodule under components/.  Disable the
# IDF Component Manager so the build never reaches for the online registry.
set(ENV{IDF_COMPONENT_MANAGER} 0)

# esp-hosted-mcu's Kconfig references $(ESP_IDF_VERSION), which ESP-IDF only
# exports to kconfgen from v6.x.  Provide it ourselves on v5.5.
include($ENV{IDF_PATH}/tools/cmake/version.cmake)
set(ENV{ESP_IDF_VERSION} "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/components/esp-wifi-remote/components/esp_wifi_remote
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(meshdeck)
