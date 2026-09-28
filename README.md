# MeshDeck

A standalone touchscreen controller for Aputure / amaran lights that speak
Sidus Bluetooth mesh — no phone, no Sidus Link Pro, no amaran Desktop.

Hardware: **Waveshare ESP32-P4-WIFI6-Touch-LCD-4C** (ESP32-P4 + ESP32-C6
radio co-processor, 4-inch 720×720 round touch display).

Phase 1: pair a light from the touchscreen, control power, intensity,
CCT / green-magenta and HSI colour, and show a live preview of what the
light is doing.  Phase 2 (front end, done ahead of hardware testing): the
round lighting desk — a swipeable channel carousel with a grand master,
vertical level faders, an HSI colour wheel, and a scenes drawer.  See
`docs/UI_DESIGN.md` and the renders in `docs/ui/`.

![MeshDeck screens](docs/ui/contact_sheet.png)

## How it works

The Sidus lights are ordinary Bluetooth SIG Mesh nodes (Telink SIG-mesh
firmware) with one proprietary access opcode, `0x26`, that carries the
10-byte light-control payloads reverse-engineered by the community.
MeshDeck does what the Sidus phone app does:

```
touchscreen (LVGL)  ->  app_core (state machine, one task)
                          |-- mesh_prov   PB-GATT provisioner (ECDH P-256, no-OOB)
                          |-- mesh_cfg    config client: composition, AppKey Add, Model App Bind
                          |-- mesh_net    network/transport layers, segmentation, proxy config
                          |-- telink      0x26 payload encode / status decode
                          `-- ble_central NimBLE central, GATT proxy client, SAR
                                   |
                        ESP-Hosted (HCI over SDIO)
                                   |
                     ESP32-C6 Bluetooth controller  ~~~ BLE ~~~  light (proxy)  ~~ mesh ~~  other lights
```

* MeshDeck **creates its own mesh network** (random NetKey / AppKey) and is
  the provisioner at unicast address `0x0001`.  That is also where the lights
  send their status reports, so state read-back works without any patched
  stack.
* Pairing = standard mesh provisioning over PB-GATT, then AppKey Add and a
  bind of the AppKey to every model in the light's composition data (the same
  "default key bind" the Sidus app performs).
* Control = `0x26` messages sent through whichever paired light MeshDeck is
  connected to as a GATT proxy; the mesh relays them to the others.

The protocol layers (`mesh_crypto`, `mesh_net`, `mesh_prov`, `mesh_cfg`,
`telink`) are plain C with no ESP-IDF dependencies and are covered by host
unit tests against Bluetooth SIG sample data and real captured amaran values.

## Repository layout

```
main/
  app_core.[ch]     link + pairing state machine, command queue, status polling
  ble_central.[ch]  NimBLE scanning/connection, GATT proxy/provisioning chars, SAR
  mesh_crypto.[ch]  AES-CMAC, AES-CCM, s1/k1..k4, ECDH (mbedTLS on target)
  mesh_net.[ch]     network PDU crypto, segmentation/reassembly, proxy config, beacons
  mesh_prov.[ch]    provisioner state machine
  mesh_cfg.[ch]     configuration client messages
  telink.[ch]       Telink/Sidus 0x26 payloads
  lights.[ch]       light registry + preview colour
  products.[ch]     Sidus product codes -> model name and capabilities
  store.[ch]        NVS persistence
  board.[ch]        display (JD9365 over MIPI-DSI), touch (GT911), backlight, LVGL port
  ui/               LVGL screens: home, pair, control
components/
  lvgl/             git submodule, v9.3.0
  esp_hosted/       git submodule, esp-hosted-mcu v2.12.13 (P4 <-> C6 transport + HCI)
  esp-wifi-remote/  git submodule, wifi_remote v0.14.5 (required by esp_hosted; Phase 3 Wi-Fi)
  esp_lvgl_port/    vendored from espressif/esp-bsp
  esp_lcd_touch*/   vendored from espressif/esp-bsp
  esp_lcd_jd9365/   vendored from espressif/esp-iot-solution
tests/host/         host unit tests (gcc + OpenSSL)
tools/sim/          headless LVGL renderer: screenshots of every screen without hardware
docs/               research notes, protocol summary, hardware test plan
```

## Building

Requirements: ESP-IDF **v5.5.x** (the board vendor's supported line; built
and run with v5.5.4 and v5.5.5), Python 3, git.  The IDF Component Manager
is disabled by the project — every dependency is in-tree.

**Chip revision.**  ESP-IDF builds for ESP32-P4 revisions below 3.0 and from
3.0 on are mutually exclusive.  `sdkconfig.defaults` targets **rev v1.x**,
which is what the test board has (`esptool.py chip_id` prints `revision
v1.3`).  For a rev v3 board set `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=n`,
`CONFIG_ESP32P4_REV_MIN_300=y` (and optionally `CONFIG_SPIRAM_SPEED_250M=y`),
delete `sdkconfig` and rebuild.  An image for the wrong revision stops in
the bootloader with a chip-revision error.

```bash
git clone --recursive <this repo> meshdeck
cd meshdeck
. ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor      # USB-C "UART" port of the board
```

Host tests and UI renders (no hardware needed):

```bash
cd tests/host && make test               # protocol unit tests (gcc + OpenSSL; Homebrew's are found on macOS)
cd tools/sim && make && ./render && python3 to_png.py   # writes out/*.png + contact sheets
```

## Before the first flash: the ESP32-C6 co-processor

The P4 has no radio.  Bluetooth comes from the on-board ESP32-C6 running the
**ESP-Hosted slave** firmware, connected over SDIO (P4 GPIO14–19, reset on
GPIO54 — the ESP-Hosted defaults for the P4, confirmed against the Waveshare
schematic).

**The C6 as shipped works.**  Waveshare's image reports ESP-Hosted version
`0.0.0` and `HCI over SDIO / BLE only`, and its Bluetooth controller is
already running.  It does not answer the newer controller RPCs
(`Req_FeatureControl`), each of which costs a 5 s timeout; MeshDeck notices
this on the first boot and skips them afterwards (NVS key `bt_rpc_skip`), so
Bluetooth is up about 3.5 s after power-on.  If Bluetooth ever fails to come
up with the RPCs skipped (e.g. the C6 was reflashed), the flag is cleared
and the board restarts once.

Updating the C6 is optional.  To do it, build the ESP-Hosted slave firmware
for `esp32c6` (its default config enables BLE over SDIO):

```bash
cd components/esp_hosted/slave
idf.py set-target esp32c6
idf.py build
# connect a USB-UART adapter to the board's "ESP32-C6 UART" header, hold the
# C6 in download mode, then:
idf.py -p <c6-port> flash
```

(ESP-Hosted also supports updating the slave from the P4 itself; see
`components/esp_hosted/examples/host_performs_slave_ota`.)

## Pairing a light

1. The light must be **unprovisioned**.  A light that is still in a Sidus
   Link / amaran app network will not appear.  Remove it in the app (light
   settings → remove / unbind) or perform the light's factory reset.
2. On MeshDeck tap **+**.  Lights in pairing mode show up within a few seconds
   with their model (read from the Sidus device UUID, e.g. "amaran PT2c"),
   the tail of their Bluetooth address ("ID DBFA30") and signal strength.
3. Tap the light.  Progress: connect → provisioning (ECDH) → the light restarts
   → MeshDeck reconnects to it → composition → AppKey → model binding → done.
4. The light gets a page on the desk, named after its model ("amaran PT2c",
   "amaran PT2c 2", ...), with only the controls the model supports.

Remove a light from the system sheet (sends Config Node Reset so it goes
back to pairing mode).  A light that was reset by hand while still in
MeshDeck's list shows up for pairing again and reuses its slot.

## Status

**Verified on hardware** (Waveshare ESP32-P4-WIFI6-Touch-LCD-4C, P4 rev
v1.3, stock C6 image, amaran PT2c): display, touch, C6 over SDIO, NimBLE,
scanning, standard PB-GATT provisioning (no Telink fast-provisioning
needed), AppKey Add + model binding, `0x26` control with status read-back,
and reconnecting through the stored network after a reboot.

**Implemented, awaiting a hardware pass** (see `docs/HARDWARE_TEST_PLAN.md`):
built-in effects, battery reports, scene crossfades, identify, a second
light relayed through the mesh, and the pages for bi-colour fixtures.  The
effect and battery encodings are byte-identical to amaran-hacs, which
validated them on T4c and Ace 25c fixtures.

## Using the desk

* **Swipe left / right** between the Master page and each light.  Drag the
  right-hand vertical bar for intensity (on Master it scales every channel).
  Tap the centre disc for power.
* Mode chips show what the fixture can do: **CCT** (temperature, plus G/M on
  fixtures that have it; the range is the model's), **HSI** (colour wheel and
  left-hand saturation bar) and **FX** (a row of the model's built-in
  effects: tap to start, tap the running one to stop; the level bar still
  sets its intensity and the disc pulses while it runs).
* Battery fixtures show their charge in the disc (polled every minute).
* **Two finger swipe up** or tap **Scenes** to open the drawer: tap to recall,
  long-press to overwrite with the current look, pencil to rename, bin to
  delete, "+" to save the current look.  Scenes store effects too.  The **Fade** row sets the
  crossfade (Snap / 1 s / 2 s / 5 s); fades are stepped from the controller
  within a configurable mesh message budget, effects switch at the start.
* **Two finger swipe down** or tap link status for the system sheet: pair a
  light; for the current light its model, ID, range and battery, **Rename**
  (keyboard + studio-name chips; long-pressing the light's name on its page
  does the same), **Identify** (blinks it three times), **Remove**; display
  brightness; factory reset.
* The screen dims after two minutes without a touch; the touch that wakes it
  does nothing else.

## Roadmap

* **Phase 2 (remaining)** — groups; second / third generation effects for
  pro fixtures once validated on hardware; native RGB for the fixtures that
  have it.
* **Phase 3** — Wi-Fi: join existing infrastructure through the C6
  (`esp_wifi_remote` is already in the tree), remote / web control, optional
  import of an existing Sidus network from an amaran Desktop export instead
  of re-pairing.

## Credits

Protocol reverse engineering by the community — see `docs/PROTOCOL.md`:
[wesbos/amaran-BLE-control](https://github.com/wesbos/amaran-BLE-control),
[kevinschaich/amaran-bridge](https://github.com/kevinschaich/amaran-bridge),
[neyako/amaran-hacs](https://github.com/neyako/amaran-hacs).  Board support
from [waveshareteam](https://github.com/waveshareteam/Waveshare-ESP32-components).
