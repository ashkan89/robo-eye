#pragma once
// =====================================================================
//  RoboEye - configuration
//  Target: Arduino Nano / Pro Mini / Pro 5V 16MHz (ATmega328P)
//          + SSD1306 128x64 OLED, one colour or two colour
// =====================================================================

#ifndef F_CPU
#define F_CPU 16000000UL       // only for host builds; the IDE sets this
#endif

// ---------------------------------------------------------------- BUS
// 1 = 4-wire hardware SPI (fast)   0 = hardware I2C (about half the rate)
#define USE_HW_SPI      1

// -- SPI wiring.  SCK and MOSI are fixed by the 328P hardware:
//      OLED SCK / D0 / CLK  -> D13
//      OLED MOSI / D1 / SDA -> D11
//    The three below you can move to any free pin.
#define OLED_CS         10
#define OLED_DC          9
#define OLED_RST         8      // set to -1 if the module has no RST pin

// -- SPI bus clock. ----------------------------------------------------
// Three numbers bound this choice:
//
//   * The SSD1306 datasheet gives a minimum serial clock cycle time of
//     100 ns for the 4-wire SPI interface, i.e. a 10 MHz ceiling.
//   * Adafruit_SSD1306 ships 8 MHz as its default for every board, which
//     is that 10 MHz spec with a little headroom.
//   * The ATmega328P's SPI unit can only divide the system clock, so on a
//     16 MHz Nano / Pro Mini 5V the only rates near the top are
//     F_CPU/2 = 8 MHz and F_CPU/4 = 4 MHz.  Nothing lands in between:
//     asking for 6 MHz gets you 4 MHz.
//
// So the real choice is 8 or 4.  8 MHz is inside spec and fine on a PCB
// or with short leads, but at that rate SCK edges are only ~60 ns apart
// and the 20-30 cm dupont jumpers these modules are usually wired with
// ring badly - which shows up as speckled or torn pixels, not as a clean
// failure.  4 MHz is the safe value for jumper-wired builds and is still
// ~10x I2C, so it is the default here.
//
// Raise to 8000000UL only with short, tidy leads (and consider a 22R
// series resistor in the SCK line).  Never go above F_CPU/2 - the clamp
// below silently holds you there, which also keeps a 3.3V/8 MHz Pro Mini
// at its own 4 MHz ceiling.
#define SPI_CLOCK  4000000UL

#if USE_HW_SPI && (SPI_CLOCK > (F_CPU / 2))
  #undef  SPI_CLOCK
  #define SPI_CLOCK (F_CPU / 2)
#endif

// -- I2C wiring: SDA=A4, SCL=A5 (fixed).  Bus clock: ----------------
#define I2C_CLOCK   400000UL    // try 800000UL if your module is happy

// -------------------------------------------------------------- PANEL
// The cheap "two colour" 0.96" modules are not two panels: they are one
// ordinary mono SSD1306 whose top 16 rows carry yellow phosphor and whose
// remaining 48 carry blue, with a dead unlit band between the two.  The
// controller cannot address the colours, so nothing in the driver
// changes - but a face laid out over all 64 rows gets sliced in two by
// that seam, with the brows and the top of the eyes stranded in yellow.
//
//   PANEL_MONO   - one colour, the face uses all 64 rows.
//   PANEL_SPLIT  - two colour, the face is laid out inside the 48 blue
//                  rows so it reads as one piece.
#define PANEL_MONO       0
#define PANEL_SPLIT      1
#define OLED_PANEL       PANEL_MONO
#define PANEL_SPLIT_ROWS 16     // yellow rows at the top; 16 on every
                                // module I have seen, but measure yours
#define PANEL_SPLIT_GAP   2     // rows either side of the colour boundary
                                // that the mask on the glass swallows.
                                // Nothing is drawn into them, so no shape
                                // ends up half eaten.  0 if yours has none.

// What to do with the yellow strip on a PANEL_SPLIT module.
//   1 = float the effects (sparkles, ZZZ, hearts, steam, question marks)
//       up there, so the strip reads as deliberate yellow accent
//   0 = leave it dark and keep the effects with the face
// Ignored on PANEL_MONO.
#define SPLIT_FX_IN_BAND 1

// Both are switchable at run time - `panel mono|split` and `band on|off`
// on the serial console - so one build can drive either module.

// The frame buffer is split into this many horizontal bands, each drawn
// and shipped in turn.  1 band = a full 1 KB buffer and a single render
// pass, but that leaves a 328P with almost no stack; 2 costs a second
// pass and halves the RAM.  Must divide 8.
#define OLED_BANDS      2

// Frame rate cap.  The SSD1306 scans its own GDDRAM out to the glass at
// roughly 60-100 Hz, asynchronously.  Rewriting that memory much faster
// than it is being read produces tearing: the panel shows the top of one
// frame and the bottom of the next.  40 fps is smooth to the eye and
// leaves the bus mostly idle.  Drop to 25-30 if you still see torn bands.
#define TARGET_FPS      40

// Panel brightness, 0..255.  A face is mostly lit pixels, and an OLED's
// current draw scales with them - at 255 a weak supply or a thin USB lead
// can sag enough to corrupt the display.  160 looks the same to the eye
// and draws noticeably less.  Adjustable live with `contrast <n>`.
#define OLED_CONTRAST  160

// Draw a plain test pattern for ~3 s at power-on (solid fill, then a
// frame with diagonals, then a checkerboard).  If you see nothing at
// all during this, the problem is wiring or the wrong controller - not
// the face code.  Also available any time as the `test` command.
#define STARTUP_SELFTEST 1

// ------------------------------------------------------------ DEFAULTS
#define DEFAULT_FEATURES  (F_BROWS | F_MOUTH | F_GLINT | F_FX | F_TILT | F_BLUSH)
#define DEFAULT_STYLE     STYLE_ROBOT   // STYLE_ROBOT or STYLE_PUPIL
#define DEFAULT_AUTO      1             // autonomous emotion drift on boot
#define BOOT_ANIMATION    1

// ------------------------------------------------------- OPTIONAL I/O
// Every block below is off by default; flip to 1 and set the pins.

#define ENABLE_BUTTONS   0      // 3 momentary buttons to GND (INPUT_PULLUP)
#define BTN_EMOTION      2      //   next emotion
#define BTN_FACE         3      //   cycle face preset (eyes / +brows / full...)
#define BTN_AUTO         4      //   toggle autonomous mode

#define ENABLE_JOYSTICK  0      // 2-axis analog stick steers the gaze
#define JOY_X           A0
#define JOY_Y           A1
#define JOY_DEADZONE    60      // counts around centre that read as "centred"

#define ENABLE_SONAR     0      // HC-SR04: react to someone approaching
#define SONAR_TRIG       6
#define SONAR_ECHO       7

#define ENABLE_LDR       0      // light sensor -> falls asleep in the dark
#define LDR_PIN         A2
#define LDR_DARK        200     // below this = dark

#define ENABLE_EEPROM    1      // "save" / "load" console commands
