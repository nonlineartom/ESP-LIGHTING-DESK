# Research notes (2026-09-28)

## Open-source work reviewed

| Project | What it gives us | What it lacks |
| --- | --- | --- |
| wesbos/amaran-BLE-control | Full mesh proxy-client crypto in TypeScript, the `0x26` opcode discovery, Telink payload builders/decoder, an ESP32 (classic) node firmware, real key/derivation values usable as test vectors | No provisioning: keys are lifted from the amaran Desktop database. ESP32 firmware needs an ESP-IDF core patch and only builds for chips with a native BT radio. |
| kevinschaich/amaran-bridge | ESP32-C5 bridge built on ESP-BLE-MESH; excellent operational notes (replay list, IV index window, adv buffer starvation, Wi-Fi power-save coexistence, hardware-AES failure) | Same key-borrowing approach; ESP-BLE-MESH stack. |
| neyako/amaran-hacs | Bit-exact field map of the `0x26` payloads incl. reports, effects (0x87/0xa2), native RGB (0x84), CCT+ up to 20000 K, per-product capability table, cloud key import | Python / Home Assistant. |
| waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC + Waveshare-ESP32-components | Board schematic, pinout, JD9365 init sequence, GT911 handling, esp_hosted/esp_wifi_remote versions | BSP pulls audio/USB deps we do not need; registry-only distribution. |
| espressif/esp-hosted-mcu | P4↔C6 SDIO transport, NimBLE VHCI transport, BT controller init API, slave firmware | Which build the Waveshare factory C6 image is, is undocumented. |

## Key decisions

1. **Own provisioner instead of ESP-BLE-MESH.**  ESP-IDF's mesh stack is
   gated on `SOC_BLE_MESH_SUPPORTED`, which the ESP32-P4 does not set (its
   controller is remote).  Writing a compact proxy-client stack was the only
   path, and it mirrors what the Sidus app itself does (it is a GATT proxy
   client too).  Side benefit: being the provisioner (address 0x0001) means
   status reports come straight to us — no stack patch needed.
2. **Standard PB-GATT, no-OOB.**  Telink's SIG-mesh SDK supports standard
   provisioning (the TelinkSigMesh app and nRF Mesh can provision Telink
   devices); Sidus's app bundles the same SigMeshLib.  Telink also has a
   proprietary "fast provision"; if the P2C insists on it, that is the first
   thing to capture on hardware.
3. **Bind the AppKey to every model.**  The community firmware bound a vendor
   model 0x0211:0000 and the lights obeyed; the Sidus/Telink app's default
   key-bind binds everything.  Binding all is the superset.
4. **Vendored display components, submodules for the big three** (LVGL,
   esp-hosted-mcu, esp-wifi-remote).  The IDF component manager is disabled
   so the build is reproducible offline.

## Hardware facts (from schematic / BSP)

* ESP32-P4NRW32: 32 MB PSRAM, 32 MB flash (project configures 16 MB for esptool).
* Display: JD9365, 2-lane MIPI-DSI @ 1500 Mbps, DPI 80 MHz, 720×720,
  porches H 20/20/40 V 4/12/24; DSI PHY power from LDO channel 3 @ 2.5 V.
* LCD reset GPIO27, backlight PWM GPIO26 (inverted), I2C SDA 7 / SCL 8 (port 1).
* Touch GT9271 with GT911-compatible driver at 0x5D or 0x14; no INT/RST routed.
* C6 over SDIO: CLK 18, CMD 19, D0–D3 14–17, C6 CHIP_PU on GPIO54.

## Things to verify on hardware / possible follow-ups

* Does the C6 factory image expose the BT controller over SDIO?
* Does the P2C advertise 0x1827 after removal from Sidus, and does it accept
  standard provisioning?
* Telink status reports are unicast to 0x0001; confirm the proxy forwards them
  with our whitelist (the filter also lists 0xFFFF).
* Proxy config PDUs use an 8-byte NetMIC (CTL = 1) per spec; the TypeScript
  implementation used 4 bytes.  If the Telink proxy ignores our filter
  messages, try 4.
* Product capability table (amaran-hacs `product_capabilities.json`) keyed by
  Sidus product code; the composition PID may map to it — worth recording the
  PID the P2C reports.
* Phase 3 alternative to re-pairing: import an existing Sidus network from the
  amaran Desktop export (keys + addresses), as amaran-hacs does.
