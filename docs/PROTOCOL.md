# Sidus / amaran mesh protocol — what MeshDeck implements

Sources: wesbos/amaran-BLE-control (`DIRECT-BLE-CONTROL.md`, `telink.ts`),
kevinschaich/amaran-bridge (`mesh.c`, README gotchas), neyako/amaran-hacs
(`docs/protocol.md`, bit-level field map), Bluetooth Mesh Profile 1.0.1.

## Transport

* Lights are Bluetooth SIG Mesh nodes (Telink SIG mesh SDK).
* Unprovisioned: advertise the Mesh Provisioning Service `0x1827`, service
  data = 16-byte device UUID + 2-byte OOB info.  Provisioning over PB-GATT
  uses characteristics `0x2ADB` (Data In, write without response) and `0x2ADC`
  (Data Out, notify).  No-OOB authentication, FIPS P-256.
* Provisioned: advertise the Mesh Proxy Service `0x1828` with Network ID
  (`k3(NetKey)`) or, right after provisioning, Node Identity.  Proxy
  characteristics `0x2ADD` (Data In) / `0x2ADE` (Data Out).
* Proxy PDUs carry SAR bits in the top two bits of the first octet; the low
  six bits are the type: 0 network, 1 beacon, 2 proxy config, 3 provisioning.
* The Telink proxy expects a Set Filter Type (whitelist) followed by Add
  Addresses (`0x0001`, `0xFFFF`) after subscribing; MeshDeck does the same.

## Fixture identification

Composition data gives nothing useful (a PT2c reports CID `0x03F6`, PID
`0x0000`, VID `0x0000`; models SIG `0x0002`, `0x1000`, vendor `0x03F61000`)
and the GATT server only exposes a generic Device Name, `Mesh Device`, with
no Device Information Service.  The **device UUID is ASCII**:

```
34 30 30 48 35 2d 44 42 46 41 33 30 01 0f 0f 0f
"4  0  0  H  5  -  D  B  F  A  3  0"  + 4 bytes
 \_ product code _/  \_ BLE MAC tail _/
```

`400H5` is the Sidus product code for the amaran PT2c (verified on
hardware).  `main/products.c` maps 92 fixture codes to names and
capabilities (CCT range, HSI, G/M, battery, supported effects), generated
from the amaran Desktop 1.1.03 catalogue snapshot in neyako/amaran-hacs.

## Access layer

* All light control uses the 1-byte opcode `0x26` with the AppKey (AKF = 1).
  It is not a standard 3-byte vendor opcode; the Telink stack dispatches on the
  byte directly.
* Status reports come back with opcode `0x26` too, addressed to the
  provisioner unicast `0x0001` — which is MeshDeck.
* Standard models (Generic OnOff, Lightness, CTL, HSL) exist on the lights but
  do not drive the LEDs.

## 0x26 payload (10 bytes)

`p[0]` checksum = sum(p[1..9]) & 0xff, `p[9]` command type (bit 7 = setter).

| Command       | Type   | Fields |
| ------------- | ------ | ------ |
| status request| `0x0e` | none |
| on / off      | `0x8c` | `p[8]` = 1 on, 0 off |
| intensity     | `0x8f` | 0..1000: low 2 bits at `p[7]` bits 6-7, upper 8 bits in `p[8]` |
| CCT + G/M     | `0x82` | kelvin/10 in bits 52-61 (with wrap bit 42 above 10000 K), G/M 0..20 (10 neutral) in bits 45-51, intensity bits 62-71 |
| HSI           | `0x81` | saturation 0..100 bits 46-52, hue 0..360 bits 53-61, intensity bits 62-71 |

Report layout (command type 0x02 CCT / 0x01 HSI, bit 7 clear): same field
positions, power in bit 8 (byte 1 bit 0).

### Built-in effects (`0x87`, first generation)

Only intensity is adjustable; every effect's speed / colour are fixed
presets in the low 64 bits.  `lo = preset | 1<<8 | intensity<<ibit`, bytes
0-7 = lo (little endian, byte 0 then overwritten by the checksum), `p[8]` =
effect id, `p[9]` = `0x87`.  Stop = `96 00 00 00 00 00 00 00 0f 87`.

| id | effect | preset (lo64) | ibit |
| -- | ------ | ------------- | ---- |
| 0 | Club lights | `0014000000000000` | 54 |
| 1 | Paparazzi | `0016301400000000` | 54 |
| 2 | Lightning | `0016301528000000` | 54 |
| 3 / 4 / 5 | TV / Candle / Fire | `0014000000000000` | 54 |
| 6 | Strobe | `1500001920000000` | 46 |
| 7 | Explosion | `1500001910000000` | 46 |
| 8 / 9 | Faulty bulb / Pulsing | `1500001925000000` | 46 |
| 10 | Welding | `1500001922400000` | 46 |
| 11 | Cop car | `0014800000000000` | 54 |
| 12 / 13 | Color chase / Party lights | `0017200000000000` | 54 |
| 14 | Fireworks | `0014000000000000` | 54 |

Reports come back as type `0x07` (effect id in `p[8]`, 15 = off, power in
bit 8).  amaran-hacs confirmed all fifteen on a T4c by device reports.  The
second / third generation effects (`0xa2`, pro fixtures) are not implemented:
they are only validated against the SDK, not hardware.  A colour command
does not end an effect reliably, so MeshDeck sends the stop packet first and
then the CCT / HSI look (as amaran-hacs does).

### Power / battery (`0x0a`)

Request `0a 00 00 00 00 00 00 00 00 0a`.  Report: `p[3] | (p[4]&1)<<8` =
minutes (meaning unverified), `p[4]>>1` = battery %, `p[5..6]` battery mV,
`p[7..8]` external supply mV (non-zero = on external power; 0 % while on
external power means "no reading").  Polled every 60 s for battery fixtures
(Ace 25c, PT1c/2c/4c).

Reference vectors (all in `tests/host/test_main.c`):

```
brightness 10%   a8 00 00 00 00 00 00 00 19 8f
CCT 3200K 22%    0e 00 00 00 00 40 01 14 37 82
HSI 45/60/80     fd 00 00 00 00 00 af 05 c8 81
power on         8d 00 00 00 00 00 00 00 01 8c
effect Fire 100% 9b 01 00 00 00 00 14 fa 05 87
power report     ae 00 00 22 6a fc 1c 00 00 0a   (53 %, 7420 mV, battery)
```

## Configuration after provisioning

MeshDeck sends, with the node's DevKey:

1. Config Composition Data Get (page 0) → parse CID/PID and the model list.
2. Config AppKey Add (NetKey index 0, AppKey index 0) — segmented (24-byte
   upper transport PDU, 2 segments) with SegAck handling.
3. Config Model App Bind for every SIG and vendor model except the Config
   Server/Client.  Errors are tolerated per model.

## Sequence numbers, IV index, replay

* MeshDeck reserves sequence numbers ahead: NVS holds a value at least 128
  above anything already sent (topped up 512 at a time), and a reboot
  resumes from it.  Every network PDU - access messages, segment
  retransmits and acks, proxy configuration - passes through the check, so
  lights never see a replayed (lower) sequence number.
* IV index starts at 0 for the new network; Secure Network Beacons are parsed
  and an IV index update is followed (up to +42) and persisted immediately.
* A per-source replay list drops duplicate PDUs.
