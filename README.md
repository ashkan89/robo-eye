# RoboEye

An expressive robotic face for the **Arduino Pro 5V (ATmega328P, 16 MHz)** driving a
**0.96" SSD1306 128x64 OLED** over I2C or 4-wire hardware SPI.

22 emotions, spring-physics motion, autonomous blinks / saccades / breathing, and a
serial console to drive the whole thing live.

```
RoboEye/
  RoboEye.ino    board setup, serial console, optional sensor inputs
  config.h       every knob you are likely to touch
  emotions.h     the 22 emotion presets (18 bytes each, in flash)
  ssd1306.h
  ssd1306.cpp    SSD1306 driver + graphics primitives (no external library)
  face.h
  face.cpp       the animation engine and renderer
```

**No libraries to install.** The display driver is part of the sketch.

---

## 1. Install

1. Open `RoboEye/RoboEye.ino`. Nothing to install — only `SPI.h` / `Wire.h` from
   the core are used.
2. **Tools > Board** -> *Arduino Pro or Pro Mini*, **Processor** -> *ATmega328P (5V, 16 MHz)*.
3. Upload, then open **Serial Monitor at 115200 baud** with line ending set to
   *Newline*.

Verified build (arduino-cli, `arduino:avr` 1.8.8):

| Configuration | Flash | RAM |
|---|---|---|
| default (hardware SPI, 2 bands) | 24166 / 30720 (78%) | 1292 / 2048 (63%) |
| I2C instead (`USE_HW_SPI 0`) | 25596 (83%) | 1505 (73%) |
| SPI + every optional input | 25152 (81%) | 1306 (63%) |
| `OLED_BANDS 1` (full 1 KB buffer) | 24162 (78%) | 1804 (88%) |



Dropping u8g2 for the built-in driver freed about 5.5 KB of flash and 700 bytes of
RAM, so there is now room to build on.

## 2. Wiring

### 4-wire hardware SPI (default, `USE_HW_SPI 1`)

Your panel needs **7 pins** for this. A 4-pin module (GND/VCC/SCL/SDA only) is
I2C-only and physically cannot do SPI — set `USE_HW_SPI 0` and use the I2C table
below instead.

| OLED pin (common labels) | Pro Mini | Note |
|---|---|---|
| GND | GND | |
| VCC / VDD | VCC | |
| D0 / SCK / CLK / SCL | **D13** | fixed by the SPI peripheral |
| D1 / MOSI / SDA | **D11** | fixed |
| RES / RST | D8 | `OLED_RST` — or `U8X8_PIN_NONE` if absent |
| DC | D9 | `OLED_DC` |
| CS | D10 | `OLED_CS` |

Bus clock is `SPI_CLOCK`, 4 MHz by default — already about 10x I2C at 400 kHz, and
tolerant of breadboards and dupont jumpers. 8000000UL is the ceiling on a 16 MHz AVR
and worth trying if your leads are short and tidy; if the picture speckles or tears,
come back down. 2000000UL is the setting to reach for on long or messy wiring.

### I2C (`USE_HW_SPI 0`)

| OLED | Pro Mini |
|---|---|
| VCC | VCC (5V) |
| GND | GND |
| SDA | **A4** |
| SCL | **A5** |

On I2C you can try `#define I2C_CLOCK 800000UL` — most SSD1306 modules run fine at
800 kHz even though the datasheet says 400 kHz.

### Nothing on the screen?

At power-on the sketch draws a 3-second test pattern before the face: a solid fill,
then a frame with diagonals, then a checkerboard. It is plain geometry with no face
code behind it, so **if you see nothing during that, the fault is below the sketch.**
Type `test` on the console to replay it. Turn it off with `STARTUP_SELFTEST 0`.

Serial also prints the bus and pins it actually compiled for. Check that line first —
driving I2C into SPI wiring, or the reverse, is the usual cause.

Then, in order of likelihood:

1. **`USE_HW_SPI` doesn't match your wiring.** The single most common cause.
2. **Only 4 pins on the module.** That board is I2C-only; use `USE_HW_SPI 0`.
3. **RST.** If your board has no RES pin, set `OLED_RST` to `U8X8_PIN_NONE`. If it
   does have one, it must be connected — left floating, the panel often never
   initialises.
4. **Wrong controller.** The sketch drives an SSD1306. A 1.3" panel is almost always
   an SH1106 — swap the constructor at the top of `RoboEye.ino` for
   `U8G2_SH1106_128X64_NONAME_2_4W_HW_SPI`, same arguments.
5. **Some modules ship jumpered for I2C.** Look for solder blobs marked BS0-BS2 or
   R1/R3/R8 on the back; SPI needs them in the SPI position.
6. **D13 is also the Pro Mini's LED pin.** Its LED plus resistor can load SCK enough
   to matter on a long or unshielded jumper. Keep the SPI leads short.

### Glitchy, torn or misshapen picture

**First, find out whether it is the code or the hardware.** Two console commands
settle it:

```
freeze on     stops the animation - every frame is now byte-for-byte identical
test          static test pattern, no face code behind it at all
```

If artefacts still crawl, flicker or come and go while the picture is frozen, nothing
in the sketch is changing — the fault is electrical or the controller. If the frozen
image is rock steady and the glitches only appear once it is moving again, it is
software, and worth reporting.

**Why the display was glitching, and what changed.** The sketch used to render
through u8g2. Its SSD1306 path selects *horizontal* addressing mode (`0x20,0x00`) at
init, then addresses each page with `0x00-0x0F` / `0x10-0x1F` (column) and
`0xB0-0xB7` (page) — commands the datasheet defines **for page addressing mode
only**. In horizontal mode the controller ignores them and simply auto-advances its
own write pointer, so the image is correct only while that pointer stays in step.
When it slips — one disturbed byte is enough — nothing can pull it back, and every
later write lands at the wrong address.

Adafruit_SSD1306, the reference driver for this controller, does it the other way
round: before every refresh it re-sends the page-address range (`0x22`) and the
column-address range (`0x21`), which *are* the correct commands for horizontal mode.
The pointer is therefore rebuilt from scratch on each write and cannot stay lost.

`ssd1306.cpp` follows that reference. The init sequence is Adafruit's byte for byte,
and every band ships with its own `0x22`/`0x21` pair, so the addressing is
re-established several times per frame. Dropping the library also removed about
5.5 KB of flash and 700 bytes of RAM.

**Also check you are not looking at features.** The face
draws marks that can read as glitches if you are not expecting them: the blush is two
or three diagonal hatch strokes beside each eye, and the effects layer puts sparkles,
steam rings, tear drops and floating hearts near the screen edges. Turn both off with

```
blush off
fx off
```

If the mystery lines vanish, they were never glitches.

**If it really is electrical**, in order:

1. **Lower `TARGET_FPS`** in `config.h` to 25. Tearing is the panel scanning itself
   out while you are still writing to it.
2. **Lower `OLED_CONTRAST`**, or type `contrast 100`. Panel current scales with lit
   pixels, and this face is mostly lit.
3. **Lower `SPI_CLOCK`** to 2000000UL. Signal integrity on jumper wire is the usual
   cause of speckle, and it costs almost nothing in frame rate.
4. **Decouple the panel.** A 10-100 uF capacitor across the OLED's VCC and GND stops
   its brightness-dependent current draw from disturbing the bus. Feed it from a
   supply that is not the USB-serial adapter if you can.
5. **Shorten the leads**, especially SCK, and keep it away from the MOSI run.
6. **Check `info`.** The `ram=` figure is free RAM in bytes. If it is under ~150 the
   stack is colliding with the frame buffer, which looks exactly like random
   corruption. It should read around 600.

Tearing in horizontal bands 16 pixels tall is characteristic of the paged renderer:
the panel is refreshed in four strips. Bands that are each internally clean but
misaligned with one another point at software; bands full of noise and dropped pixels
point at the bus.

## 3. Serial console

Type `help` for the list. Commands are case-insensitive.

| Command | What it does |
|---|---|
| `e <n>` / `e happy` | set an emotion (by index or name) |
| `list` | print all 22 emotions with their index |
| `next` / `prev` | step through the emotions |
| `demo` | play all 22, ~2.4 s each |
| `auto on` / `auto off` | autonomous mood drift (a new mood every 7-16 s) |
| `face 0..4` | eyes only / +brows / +mouth / full / full+nose |
| `style robot` / `style pupil` | solid Vector-style eye, or a moving iris |
| `mouth\|brows\|nose\|blush\|glint\|scan\|tilt\|fx  on\|off` | toggle one feature |
| `look <x> <y>` | aim the gaze, -100..100 on each axis |
| `blink`, `wink l`, `wink r` | one-shot |
| `talk <ms>` | chatter the mouth for that long |
| `speed <pct>` | 20..250, global animation tempo |
| `info` | current state, frame rate, free RAM |
| `test` | static display test pattern |
| `freeze on\|off` | stop the animation, so every frame is identical |
| `contrast <0-255>` | panel brightness, live |
| `save` / `load` | persist the current setup in EEPROM |

Note that `e`, `look` and `face` switch **auto off**, so the face stays where you put
it. `auto on` hands it back to its own devices.

## 4. Emotions

```
 0 neutral      6 surprised   12 confused    18 dizzy
 1 happy        7 scared      13 love        19 wink
 2 glee         8 sleepy      14 excited     20 shy
 3 sad          9 tired       15 bored       21 proud
 4 angry       10 suspicious  16 annoyed
 5 furious     11 skeptical   17 focused
```

Each one is a row in `emotions.h`. The fields are documented above the table — eye
size, lid coverage and slant, brow angle and height, mouth curve/opening/width,
resting gaze bias, blush, a particle effect and a few shape flags. **Changing a
number in that table is all it takes to redesign an expression**; nothing else in the
code needs to know.

Effects that ride along with a mood: floating `z`s (sleepy), a bobbing `?`
(confused), a sweat drop (scared), tears (sad), twinkling sparkles (glee, excited),
steam puffs (furious), rising hearts plus heart-shaped eyes (love), and spinning
spirals in the eyes (dizzy).

## 5. How the motion works

Nothing is a keyframe. Every animated quantity — gaze, eye width and height, four
lid parameters per eye, brow angle and height, mouth curve/opening/width, corner
roundness, blush — is a **damped spring** with its own stiffness and damping ratio,
solved once per frame with the real elapsed `dt`:

```
vel += (f*f*(target - value) - 2*z*f*vel) * dt
value += vel * dt
```

Damping ratios below 1.0 (the `SCFG` table in `face.cpp`) let a value overshoot
slightly and settle — that little bit of follow-through is what stops it looking like
a slideshow. Set an emotion and every parameter races to its new target on its own
timing, so expressions cross-fade instead of cutting.

On top of that the face is never still:

- **Blinks** on a randomised schedule, per-emotion rate, ~1 in 5 a double blink, with
  a fast close (55 ms) and a slower ease-out open (115 ms).
- **Saccades** — a mix of small "still listening" corrections and full look-arounds,
  a quarter of them carrying a blink, the way real eyes move.
- **Micro-drift and breathing** from detuned integer oscillators that never visibly
  repeat.
- **Head tilt** faked by shifting the two eyes in opposite directions when the gaze
  swings sideways.
- A **blink on expression change**, most of the time, which hides the morph exactly
  the way animators use one.

The eyelids are the interesting part of the renderer. Rather than compositing
sprites, a lid is a field of black columns eaten into the eye: each column's depth is
a base coverage times a shaping curve, plus a linear slant that tips the inner corner
down (angry) or up (sad). Three shapes fall out of one routine — flat, a parabola
biting deepest in the middle, and a parabola biting deepest at the corners. Put the
corner-biting one on the top lid and the middle-biting one on the bottom and both
edges rise towards the centre, which is exactly the classic arched `^ ^` happy eye;
crank the coverage and the same construction becomes a wink. All fixed point, no
floats in the inner loop.

The mouth works the same way: two parabolic lips filled column by column, with the
curve centred on the baseline so the corners lift as much as the middle drops.
Smiles, frowns, open grins and the surprised "O" are one shape with different
numbers.

## 6. Tuning

Everything worth changing is in `config.h`:

| Define | Meaning |
|---|---|
| `USE_HW_SPI` | 1 = 4-wire hardware SPI (default), 0 = I2C |
| `SPI_CLOCK` | 8000000 — the maximum a 16 MHz AVR can clock out |
| `STARTUP_SELFTEST` | draw the 3 s test pattern before the face |
| `OLED_BANDS` | 2 — buffer bands; 1 uses a full 1 KB buffer, needs a roomier board |
| `TARGET_FPS` | 40 — redraw cap, lower it if you see torn bands |
| `OLED_CONTRAST` | 160 — panel brightness and therefore current draw |
| `I2C_CLOCK` | 400000 default, try 800000 |
| `DEFAULT_FEATURES` | which parts of the face are on at boot |
| `DEFAULT_STYLE` | `STYLE_ROBOT` or `STYLE_PUPIL` |
| `DEFAULT_AUTO` | start in autonomous mood mode |
| `BOOT_ANIMATION` | the CRT power-on + "hello" glance |

Feel and timing live in `face.cpp`: the `SCFG` spring table (stiffness, damping),
`BLINK_CLOSE_MS` / `BLINK_OPEN_MS`, and the saccade/blink interval ranges in
`Face::update`.

### Why the buffer is banded

A full 128x64 frame buffer is 1024 bytes. Add the `HardwareSerial` ring buffers, the
face's own state and Wire's buffers on I2C, and a 328P has almost no stack left — the
one-band build reports 244 bytes free, which is asking for trouble. So the buffer
holds `8 / OLED_BANDS` pages and the face is drawn once per band, each band shipped
with its own addressing commands. At the default of 2 that is 512 bytes and two
render passes, leaving 756 bytes of headroom.

Banding costs nothing in correctness: the drawing primitives take absolute screen
coordinates and clip to the band, so the output is bit-identical whatever
`OLED_BANDS` is set to (verified for 1, 2, 4 and 8 across all 22 emotions and all
three face layouts). On a board with RAM to spare, set it to 1 for a single pass.

### How the graphics work

Everything in the face is columns, so `drawVLine` is the only hot primitive and it
fills the page-packed buffer a byte at a time with a shift-and-mask rather than
per-pixel. The rest is built on it: a box is a run of columns; a rounded box insets
each column by the circle equation; a disc is a column of half-heights from an
integer square root. No floating point anywhere in the driver.

## 7. Optional hardware

All off by default; flip the `#define` in `config.h` and set the pins. With all four
enabled the sketch still fits, at 98% of flash.

- **`ENABLE_BUTTONS`** — three momentary buttons to GND: next emotion, cycle face
  preset, toggle autonomous mode. Handy if you want it standalone with no laptop.
- **`ENABLE_JOYSTICK`** — a 2-axis analog stick steers the gaze directly.
- **`ENABLE_SONAR`** — HC-SR04. Under 15 cm it looks *surprised*, 15-45 cm *focused*,
  otherwise *neutral*. It reacts to you walking up to it.
- **`ENABLE_LDR`** — a light sensor on a divider: it gets *sleepy* in the dark and
  wakes up *surprised* when the lights come on.
- **`ENABLE_EEPROM`** — the `save` / `load` console commands.

## 8. Driving it from your own code

`Face` is self-contained, so you can wire it to whatever your robot is actually
doing:

```cpp
face.setAuto(false);              // take manual control
face.setEmotion(EMO_HAPPY);       // springs animate to the new expression
face.look(-40, 20);               // glance down-left
face.talk(1200);                  // mouth chatter while a buzzer plays
face.doBlink(2);                  // wink with the right eye
face.setSpeed(160);               // 1.6x tempo - excitable
```

Call `face.update(dt)` and `face.render()` once per loop and never block; the engine
uses the real `dt`, so the motion keeps the same speed whatever frame rate you get.
