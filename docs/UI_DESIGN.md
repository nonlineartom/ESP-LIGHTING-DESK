# MeshDeck UI — the round lighting desk

Target: 4-inch 720×720 round IPS, capacitive touch, viewed at arm's length on
a desk or clamped to a stand.  One hand, often gloved or mid-shoot: big
targets, no precision taps, no menus that need reading.

## Circular design principles applied

1. **Keep controls inside the circle.**  The panel is 720×720 but the glass is
   round.  Buttons, labels and slider knobs sit in the usable central area.
2. **Use straight faders for levels.**  Master intensity, channel intensity
   and HSI saturation are vertical 0–100 sliders.  Their values remain visible
   while dragging.  The outer rim stays clear.
3. **Centre = what you are controlling.**  The preview disc in the middle
   *is* the light: colour, intensity, on/off, all rendered live from the
   fixture's reported state.  Tap it to toggle power.  Preview text changes
   to white on darker colours so intensity remains readable.
4. **Fixed reading zones.**  A horizontal fixture name sits above the preview.
   CCT keeps temperature and G/M in two cards below it.  HSI puts an
   interactive hue wheel around the preview, with saturation to its left and
   intensity to its right.
5. **Navigation is spatial and visible.**  Channels sit side by side (swipe
   left/right).  The Scenes button opens the lower drawer; tapping link status
   opens the upper system sheet.  Opening either sheet by gesture requires a
   two finger vertical swipe.  One finger horizontal swipes change pages.
   Position dots sit below the Scenes button.
6. **Motion explains structure.**  Every transition moves in the direction of
   the gesture that caused it; the level fader and preview interpolate
   between states so a scene recall reads as a crossfade, not a jump.

## Information architecture

```
                 ┌───────────────────────┐
  two fingers ↓  │   SYSTEM SHEET        │   link status · pair · rename ·
    from top     │   (drops from above)  │   remove · factory reset
                 └───────────────────────┘
   ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐
   │ MASTER │←→│ Key    │←→│ Fill   │←→│ Back   │   ← the DESK: swipe ←/→
   │ (all)  │  │ ch 1   │  │ ch 2   │  │ ch 3   │     snap to channel
   └────────┘  └────────┘  └────────┘  └────────┘
                 ┌───────────────────────┐
  two fingers ↑  │   SCENES DRAWER       │   tap = recall · long-press = save
    from below   │   (rises from below)  │   here · + = save new · bin = delete
                 └───────────────────────┘
```

### Desk (carousel)

* Page 0 is **Master**: vertical grand-master fader scales every light's intensity
  proportionally; centre disc shows a row of light colours; all-on /
  all-off and three aligned CCT presets (3200 / 4300 / 5600 K).  When empty,
  the fader and unused controls are hidden and a Pair a light button appears.
* Page *n* is one **channel** (a paired light):
  * right-hand vertical fader: intensity;
  * centre: preview disc with a large intensity value; a straight name above;
  * below the disc: mode chips **CCT / HSI**;
  * CCT: two labelled slider cards for temperature and G/M;
  * HSI: interactive colour wheel around the preview and left-hand vertical
    saturation fader;
  * bottom: Scenes button and position dots.
* A channel page that has no fixture yet (not paired) does not exist; the
  "+" to pair lives in the system sheet and on the Master page when empty.

### Scenes drawer

* Slides up over the desk (spring/ease-out, 260 ms) covering the lower 75%.
* Scenes are a vertical scrolling column of 400 px pills inside the circle.
* Each pill: name, a row of small colour discs (one per light, the scene's
  look), and the recall status.
* **Tap** recalls.  A **Fade** selector (Snap / 1 s / 2 s / 5 s) sets the
  crossfade: the controller steps every light from its current look to the
  scene's look (the Sidus protocol has no transition time of its own), within a
  mesh budget of `FADE_MSGS_PER_SEC` messages per second shared by all lights.  **Long-press** overwrites the scene
  with the current look.  The bin button deletes after confirmation.
  **+** at the bottom saves the current look as a new scene (name is
  auto-numbered; rename in a later phase).

### System sheet

Drops from the top: link state with the proxy light's name, **Pair a light**
(opens the existing pairing flow), per-light rename / remove for the active
channel, factory reset (with confirm).  Two finger swipe up or tap the handle
to close.

## Gesture map

| Gesture | Where | Result |
| --- | --- | --- |
| Swipe ← / → | desk | previous / next channel (snap, 220 ms ease-out) |
| Two finger swipe ↑ | desk | open scenes drawer |
| Two finger swipe ↓ | desk | open system sheet |
| Two finger swipe ↓ / tap handle | scenes drawer | close |
| Two finger swipe ↑ / tap handle | system sheet | close |
| Drag right-hand vertical bar | channel / master | intensity (throttled sends, final on release) |
| Drag left-hand vertical bar | HSI channel | saturation |
| Drag colour wheel | HSI channel | hue |
| Drag a lower slider | CCT channel | temperature or G/M |
| Tap centre disc | channel / master | power toggle (disc "breathes") |
| Tap chip | channel | switch CCT / HSI (controls cross-fade) |
| Tap scene | drawer | recall |
| Long-press scene | drawer | overwrite with current look |

The GT911 driver supplies two touch points to LVGL's gesture recognizer.
One finger vertical gestures are ignored by the drawer handlers.  The
carousel uses native scroll-snap for horizontal navigation.  The Scenes
button and link status remain deliberate tap shortcuts.

## Motion spec

| Motion | Duration | Path |
| --- | --- | --- |
| Channel snap | 220 ms | ease-out |
| Drawer / sheet open | 260 ms | overshoot (small) |
| Drawer / sheet close | 200 ms | ease-in-out |
| Level value follow (reported state) | 300 ms | ease-out |
| Preview colour crossfade | 300 ms (600 ms on scene recall) | linear |
| Power toggle "breathe" | 180 ms scale 100→108→100 | ease-in-out |
| Chip switch, control band swap | 180 ms opacity | linear |
| Pair progress arc | 400 ms per step | ease-out |

All animations are LVGL `lv_anim` driven at the 5 ms tick; the frame
buffer is double-buffered with tearing avoidance, so 60 Hz is expected on
the P4 with RGB565.

## Visual language

* Background near-black `#0b0e14`, panels `#181d27`, accent amber `#ffb347`
  (reads as "tungsten" on a lighting desk), success green, alert red.
* Montserrat 20 / 24 / 28 / 36 / 48; numbers use the large sizes.
* Vertical level bars use amber, saturation uses pink, and the hue wheel
  displays the full spectrum.  CCT parameter sliders stay in 190×98 px cards.
* Preview disc carries a soft outer glow in its own colour — the disc looks
  lit, not painted.

## Scenes data model

`scene_t { used, name[16], entries[LIGHTS_MAX] { addr, on, mode, intensity,
cct, gm, hue, sat } }`, up to 12 scenes, persisted in NVS as one blob.
Recall sends one `0x26` message per light per attribute (mode command
carries intensity; power separately), 60 ms apart, then a status refresh.
