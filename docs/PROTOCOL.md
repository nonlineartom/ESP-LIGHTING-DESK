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
positions, power in bit 8 (byte 1 bit 0).  Type `0x0a` is a power/battery
diagnostics page and is ignored.

Reference vectors (all in `tests/host/test_main.c`):

```
brightness 10%   a8 00 00 00 00 00 00 00 19 8f
CCT 3200K 22%    0e 00 00 00 00 40 01 14 37 82
HSI 45/60/80     fd 00 00 00 00 00 af 05 c8 81
power on         8d 00 00 00 00 00 00 00 01 8c
```

## Configuration after provisioning

MeshDeck sends, with the node's DevKey:

1. Config Composition Data Get (page 0) → parse CID/PID and the model list.
2. Config AppKey Add (NetKey index 0, AppKey index 0) — segmented (24-byte
   upper transport PDU, 2 segments) with SegAck handling.
3. Config Model App Bind for every SIG and vendor model except the Config
   Server/Client.  Errors are tolerated per model.

## Sequence numbers, IV index, replay

* MeshDeck persists its sequence number every 32 messages and adds a margin of
  64 at boot, so lights never see a replayed (lower) sequence number.
* IV index starts at 0 for the new network; Secure Network Beacons are parsed
  and an IV index update is followed (up to +42).
* A per-source replay list drops duplicate PDUs.
