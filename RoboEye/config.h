#pragma once
// =====================================================================
//  RoboEye - configuration
//  Target: Arduino Pro Mini / Pro 5V 16MHz (ATmega328P) + SSD1306 128x64
// =====================================================================

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
// 4 MHz is already ~10x I2C and survives breadboards and dupont jumpers.
// 8000000UL is the ceiling on a 16 MHz AVR - only worth it with short,
// tidy leads; if the picture tears or speckles, come back down.
#define SPI_CLOCK  4000000UL

// -- I2C wiring: SDA=A4, SCL=A5 (fixed).  Bus clock: ----------------
#define I2C_CLOCK   400000UL    // try 800000UL if your module is happy

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
