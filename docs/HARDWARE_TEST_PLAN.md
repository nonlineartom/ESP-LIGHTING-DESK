# First hardware session — test plan

Bring one **amaran / Aputure P2C** and the Waveshare ESP32-P4-WIFI6-Touch-LCD-4C.
Keep the serial monitor open (`idf.py monitor`, 115200 baud).

## 0. Flash

```
idf.py -p <port> flash monitor
```

Expected boot lines (order may vary):

```
main: MeshDeck starting
board: touch at 0x5D            (or 0x14)
board: display ready
ui: ui ready
core: creating a new mesh network          (first boot only)
core: network ready: nid=0x.. aid=0x.. iv=0 seq=64 lights=0 next_addr=0x0002
H_SDIO_DRV: ... / transport: capabilities ... HCI over SDIO
ble: host synced, addr type 0
```

| Symptom | Meaning / next step |
| --- | --- |
| Screen dark, `board:` errors | DSI/JD9365 bring-up; compare `board.c` with the Waveshare BSP (`bsp_display_new_with_handles`). Check `CONFIG_ESP32P4_REV_MIN_300` matches the silicon. |
| Touch not found | The GT911 address strap; the BSP probes 0x5D then 0x14 — both are tried. |
| SDIO timeouts, no `host synced` | The C6 is not running ESP-Hosted with BT, or is in a bad state. Power-cycle the board first; then flash the C6 slave firmware (README). |
| `hosted BT controller init failed` | The C6 image lacks the BT controller: flash the slave firmware. |

## 1. Pairing

1. In Sidus Link / amaran app: remove the P2C from its network (or factory
   reset the light).  The light should be discoverable by a fresh app install —
   that is the state MeshDeck needs.
2. On MeshDeck: **+**.  Within ~5 s expect `core: unprovisioned device: <name> rssi=..`
   and a row on screen.  If nothing appears: is the light still in the Sidus
   network?  Is Bluetooth range OK?  Check that the light advertises `0x1827`
   with a phone BLE scanner (nRF Connect).
3. Tap the light.  Expected log:

```
ble: connecting to xx:xx:.. (provisioning)
ble: MTU 247 / notifications enabled
mesh_prov: invite sent (unicast 0x0002)
mesh_prov: capabilities: elements=1 algorithms=0x0001 pubkey=0 static_oob=0
mesh_prov: provisioning complete: unicast 0x0002, 1 element(s)
ble: disconnected ...
core: proxy candidate <name> rssi=..           (Node Identity match)
ble: notifications enabled (in=.. out=..)
core: composition: cid=0x0211 pid=0x.... vid=0x.... elements=1
core:   elem 0 sig model 0x1000 ...
core:   elem 0 vendor model 0x0211....
core: appkey status 0x00
core: bind status 0x00 model ...
core: pair: Paired and ready (100%)
core: status 0x0002: on cct int=500 cct=5600 ...
```

Failure points and what they tell us:

| Where it stops | Interpretation |
| --- | --- |
| Capabilities never arrive | The light did not accept PB-GATT Invite — it may require Telink fast-provision; capture with nRF Connect what the Sidus app writes to 0x2ADB. |
| `provisioning failed: confirmation failed` | Auth value mismatch — the light may use a static OOB key (check `static_oob` in the capabilities log). |
| Data sent, no Complete | Provisioning Data rejected: check IV index / flags / address; Telink firmware also validates the key index. |
| Light never re-advertises | Give it 10–20 s; some fixtures need a power cycle after provisioning. |
| Composition OK, AppKey status non-zero | Look at the status code (0x03/0x04 = invalid index). |
| Everything binds, but no `0x26` status and no reaction | The AppKey landed on the wrong models or the fixture wants a different key index. Try binding only the vendor model 0x0211:0000 — or the reverse. |

## 2. Control

On the control screen: power, intensity arc, CCT / G-M sliders, HSI.  Each
command logs nothing by itself; the follow-up status request ~1.2 s later
should log `core: status 0x0002: ...` matching what was sent, and the preview
disc should follow the reported state.

## 3. Persistence

Reboot the board: it should reconnect through the light without pairing
(`core: network ready ... lights=1`, `Online via <name>`), and status polls
every ~8 s.

## 4. Second light (stretch)

Pair a second light while the first is online.  MeshDeck disconnects, pairs,
reconnects through the new light, and controls both through the mesh.
