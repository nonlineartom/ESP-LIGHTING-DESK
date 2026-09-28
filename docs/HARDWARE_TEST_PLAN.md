# Hardware test plan

Board: Waveshare ESP32-P4-WIFI6-Touch-LCD-4C.  Keep the serial monitor open
(`idf.py monitor`, 115200 baud, USB-C "UART" port).

## Session 1 (2026-09-28) — results

Board: ESP32-P4 **rev v1.3**, 32 MB flash, stock C6 image (ESP-Hosted
`0.0.0`).  Light: amaran **PT2c**.

| Step | Result |
| --- | --- |
| Boot, display (JD9365, LCD ID `93 65 04`), touch (GT911 at 0x5D) | ✅ |
| C6 over SDIO, `HCI over SDIO / BLE only`, NimBLE sync | ✅ (controller RPCs time out - now learned and skipped) |
| Unprovisioned beacon, standard PB-GATT provisioning | ✅ no fast-provisioning needed, no OOB |
| Composition, AppKey Add, binding of SIG 0x0002, 0x1000 and vendor 0x03F61000 | ✅ all status 0x00 |
| `0x26` control and status read-back | ✅ |
| Reconnect via the stored network after reboot | ✅ |
| Fixture identification | ✅ UUID `400H5-DBFA30` = amaran PT2c |

Fixed during the session: the build targeted rev v3 silicon, IDF 5.5.4
build break in `esp_lvgl_port`, a boot-loop (UI read the light registry
before its lock existed), and a boot-loop from internal-RAM exhaustion before
`app_main` (large tables moved to PSRAM).

## 0. Flash and boot

```
idf.py -p <port> flash monitor
```

Expected boot (first boot after a fresh flash may take ~15 s to reach
`host synced` while it learns that the C6 ignores the controller RPCs; later
boots ~3.5 s):

```
main: MeshDeck starting (internal RAM free ~316000, largest block ~221000)
store: loaded N light(s)
core: network ready: nid=.. aid=.. iv=0 seq=.. lights=N next_addr=..
core:   light 0x0002 'amaran PT2c' cid=0x03f6 pid=0x0000 uuid=34303048352d...
board: touch at 0x5D
board: display ready
ui: ui ready
main: co-processor runs its BT controller already; skipping controller RPCs
ble: host synced, addr type 0
core: proxy candidate amaran PT2c rssi=..
core: status 0x0002: on cct int=.. cct=.. gm=..
```

| Symptom | Meaning / next step |
| --- | --- |
| Bootloader stops with a chip revision error | Image built for the other P4 revision family; see README "Chip revision". |
| `assert failed: vApplicationGetTimerTaskMemory` before `app_main` | Internal RAM exhausted before the scheduler starts; move new large static data to PSRAM (`MD_EXT_BSS`). |
| `no paired light in range yet; still scanning` every 30 s | The paired light is off or out of range. |
| `Bluetooth did not start without the controller RPCs; retrying with them` | The C6 image changed; the board restarts once and relearns. |

## 1. Session 2 checklist (new since session 1)

### Effects (PT2c: 9 effects)
1. Light page → **FX** → tap **Fire**.  Log `core: effect Fire on 0x0002`,
   then a report `status 0x0002: effect Fire (5) on int=..`.  The disc pulses.
2. Drag the level bar: the effect's intensity follows (the effect is re-sent).
3. Tap **Fire** again: stop, then the previous CCT look is re-sent.  Log
   `effect off on 0x0002`, then a CCT status.
4. With an effect running, tap **CCT** and move TEMP: the effect stops first.
5. Master level with an effect running: the effect dims with the master.
6. amaran-hacs decodes `0x07` effect reports, so a status poll during an
   effect should answer with one.  If the PT2c answers with a CCT / HSI page
   instead, the page drops back from FX after the next poll (~8 s): note it,
   the core would then need to keep FX until an effect-off report.

### Battery (PT2c)
~2 s after connecting and then every 60 s: `core: power 0x0002: battery NN% ...`.
The disc shows the battery line; plugged in shows the charge symbol.  If the
light never answers `0a 00 .. 0a`, nothing is shown (expected for mains
fixtures).  Record the reported minutes value next to the light's own
battery display - its meaning is not confirmed.

### System sheet
* Fixture card: "amaran PT2c • ID DBFA30", "2700-10000K • colour • effects",
  battery.
* **Identify**: the light blinks three times and returns to its state.
* **Rename** (or long-press the name on the light's page): keyboard entry,
  studio chips, Save; survives a reboot.

### Display
* **Display** slider in the system sheet changes the backlight live and is
  kept after a reboot.
* Leave the desk untouched for 2 minutes: the backlight dims; the next touch
  only wakes it (tapping a light's disc while dimmed must not toggle it).

### Scenes and fades
* Pencil on a scene row: rename with the keyboard; survives a reboot.
* Save a scene with an effect running and one with a CCT look; recall each
  with Fade 2 s: effects switch at the start, CCT/HSI ramp in ~8 steps.  If
  steps are lost (the light stops short of the target), lower
  `FADE_MSGS_PER_SEC` in `app_core.c`; if it is perfectly smooth, raise it.
* **Diagnostic:** in the system sheet on a light's page, long-press
  "SYSTEM".  This sends a standard Light Lightness Set with a 1 s transition
  (alternating 50 % / 100 %).  If the fixture dims smoothly on its own, fades
  could be offloaded to the light.  Log line: `DIAG Light Lightness`.

### Second light
Pair a second light (e.g. the 100x) while the first is online: MeshDeck
disconnects, pairs, reconnects, and controls both through the mesh.  The
100x page has CCT only (2700-6500 K), no G/M, no HSI, no FX.

### Resilience
* Switch the proxy light off while online: `ble: disconnected`, then scanning,
  and reconnection through another paired light if one is in range.
* Power-cycle the board mid-fade and repeatedly: lights must keep obeying
  (sequence numbers are reserved ahead in NVS; `seq=` in the boot log jumps
  by ~576 per boot).

## Reference: pairing log

```
core: unprovisioned device: amaran PT2c rssi=-56 uuid=34303048-352d-4442-4641-3330010f0f0f oob=0x0000
ble: connecting to 70:3e:97:db:fa:30 (provisioning)
mesh_prov: invite sent (unicast 0x0002)
mesh_prov: capabilities: elements=1 algorithms=0x0001 pubkey=0 static_oob=0
mesh_prov: provisioning complete: unicast 0x0002, 1 element(s)
ble: disconnected reason=534
core: proxy candidate amaran PT2c rssi=-33
core: composition: cid=0x03f6 pid=0x0000 vid=0x0000 elements=1
core:   elem 0 sig model 0x0002
core:   elem 0 sig model 0x1000
core:   elem 0 vendor model 0x03f61000
core: appkey status 0x00
core: bind status 0x00 model 0x00000002 / 0x00001000 / 0x03f61000
core: pair: Paired and ready (100%)
core: status 0x0002: on cct int=130 cct=4700 gm=0 hue=0 sat=0
```

| Where pairing stops | Interpretation |
| --- | --- |
| Light not listed | Still in a Sidus network (remove it in the app or reset it), or out of range. |
| Capabilities never arrive | The light did not accept PB-GATT Invite; capture what the Sidus app writes to 0x2ADB. |
| `confirmation failed` | Static OOB (check `static_oob` in the capabilities line). |
| Light never re-advertises | Give it 10–20 s; some fixtures need a power cycle after provisioning. |
| AppKey / bind status non-zero | 0x03/0x04 = invalid index. |
