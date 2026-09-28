# MeshDeck UI — the round lighting desk

Target: 4-inch 720×720 round IPS, capacitive touch, viewed at arm's length on
a desk or clamped to a stand.  One hand, often gloved or mid-shoot: big
targets, no precision taps, no menus that need reading.

## Circular design principles applied

1. **Everything lives inside the circle.**  The screen is a rectangle, the
   glass is a disc.  No widget is placed where the chord of the circle is
   narrower than the widget: at vertical offset *d* from the centre the usable
   width is `2·√(360² − d²)` and every layout in `ui.c` is checked against
   `ui_chord_width()`.  Nothing important sits in the outer 24 px ring,
   which the bezel and the finger's own width cover.
2. **The rim is a control, not a border.**  The outer ring is the fader: an
   arc slider that follows the glass edge.  It is where a thumb naturally
   rests, it never occludes the preview, and dragging around a rim is the
   most precise gesture a round screen offers (270° of travel ≈ 1000 px).
3. **Centre = what you are controlling.**  The preview disc in the middle
   *is* the light: colour, intensity, on/off, all rendered live from the
   fixture's reported state.  Tap it to toggle power.
4. **Radial hierarchy.**  Importance falls off with radius: preview →
   value → mode chips → secondary rings.  Text is centred and short; the only
   text near the rim is the channel name, drawn *along the arc*.
5. **Navigation is spatial, not modal.**  Channels sit side by side (swipe
   left/right), scenes live underneath (swipe up), the system sheet lives
   above (swipe down).  Position is always shown by an arc of dots that
   hugs the bottom rim, so the user always knows where they are on the desk.
6. **Motion explains structure.**  Every transition moves in the direction of
   the gesture that caused it; the intensity ring and preview interpolate
   between states so a scene recall reads as a crossfade, not a jump.

## Information architecture

```
                 ┌───────────────────────┐
     swipe ↓     │   SYSTEM SHEET        │   link status · pair · rename ·
    from top     │   (drops from above)  │   remove · factory reset
                 └───────────────────────┘
   ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐
   │ MASTER │←→│ Key    │←→│ Fill   │←→│ Back   │   ← the DESK: swipe ←/→
   │ (all)  │  │ ch 1   │  │ ch 2   │  │ ch 3   │     snap to channel
   └────────┘  └────────┘  └────────┘  └────────┘
                 ┌───────────────────────┐
     swipe ↑     │   SCENES DRAWER       │   tap = recall · long-press = save
    from below   │   (rises from below)  │   here · + = save new · swipe → delete
                 └───────────────────────┘
```

### Desk (carousel)

* Page 0 is **Master**: grand-master ring scales every light's intensity
  proportionally; centre disc shows a mosaic of all lights; big all-on /
  all-off; three CCT presets (3200 / 4300 / 5600 K) as a radial trio.
* Page *n* is one **channel** (a paired light):
  * outer ring, 270° from 7 o'clock to 5 o'clock: intensity;
  * centre: preview disc, name curved above it, value under it;
  * below the disc: mode chips **CCT / HSI**;
  * lower band: the mode's controls as *short arcs* hugging the lower rim
    (CCT temperature arc on the left, G/M arc on the right; hue wheel and
    saturation arc in HSI mode);
  * bottom rim: position dots.
* A channel page that has no fixture yet (not paired) does not exist; the
  "+" to pair lives in the system sheet and on the Master page when empty.

### Scenes drawer

* Slides up over the desk (spring/ease-out, 260 ms) covering the lower 75%.
* Scenes are a vertical, snap-scrolling column of pills whose width follows
  the circle's chord at their row (wider in the middle, narrower near the
  bottom rim) so the list itself reads as round.
* Each pill: name, a row of small colour discs (one per light, the scene's
  look), and the recall status.
* **Tap** recalls.  A **Fade** selector (Snap / 1 s / 2 s / 5 s) sets the
  crossfade: the controller steps every light from its current look to the
  scene's look (the Sidus protocol has no transition time of its own), within a
  mesh budget of `FADE_MSGS_PER_SEC` messages per second shared by all lights.  **Long-press** overwrites the scene
  with the current look.  **Swipe right** on a pill reveals delete.
  **+** at the bottom saves the current look as a new scene (name is
  auto-numbered; rename in a later phase).

### System sheet

Drops from the top: link state with the proxy light's name, **Pair a light**
(opens the existing pairing flow), per-light rename / remove for the active
channel, factory reset (with confirm).  Swipe up or tap the handle to close.

## Gesture map

| Gesture | Where | Result |
| --- | --- | --- |
| Swipe ← / → | desk | previous / next channel (snap, 220 ms ease-out) |
| Swipe ↑ | desk | open scenes drawer |
| Swipe ↓ | desk | open system sheet |
| Swipe ↓ / tap handle | scenes drawer | close |
| Swipe ↑ / tap handle | system sheet | close |
| Drag on rim | channel / master | intensity (throttled sends, final on release) |
| Drag on lower arcs | channel | CCT, G/M, hue, saturation |
| Tap centre disc | channel / master | power toggle (disc "breathes") |
| Tap chip | channel | switch CCT / HSI (controls cross-fade) |
| Tap scene | drawer | recall |
| Long-press scene | drawer | overwrite with current look |
| Long-press channel name | channel | rename (system sheet) |

Gestures are recognised by LVGL's indev (`LV_EVENT_GESTURE`) on the screen
object; the carousel uses native scroll-snap so horizontal swipes have
momentum and rubber-banding for free, while vertical gestures are consumed
by the screen and never scroll the carousel.

## Motion spec

| Motion | Duration | Path |
| --- | --- | --- |
| Channel snap | 220 ms | ease-out |
| Drawer / sheet open | 260 ms | overshoot (small) |
| Drawer / sheet close | 200 ms | ease-in-out |
| Ring value follow (reported state) | 300 ms | ease-out |
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
* Montserrat 16 / 20 / 28 / 36 / 48; numbers use the large sizes.
* Rings: 18 px main, inactive track `#1c2230`, knob white with amber glow.
* Preview disc carries a soft outer glow in its own colour — the disc looks
  lit, not painted.

## Scenes data model

`scene_t { used, name[16], entries[LIGHTS_MAX] { addr, on, mode, intensity,
cct, gm, hue, sat } }`, up to 12 scenes, persisted in NVS as one blob.
Recall sends one `0x26` message per light per attribute (mode command
carries intensity; power separately), 60 ms apart, then a status refresh.
