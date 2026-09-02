#pragma once
#include <Arduino.h>
#include "ssd1306.h"
#include "emotions.h"
#include "config.h"

// ------------------------------------------------------- feature bits
#define F_MOUTH   0x01
#define F_BROWS   0x02
#define F_NOSE    0x04
#define F_BLUSH   0x08
#define F_GLINT   0x10
#define F_SCAN    0x20   // travelling CRT scan band
#define F_TILT    0x40   // head-tilt illusion when looking sideways
#define F_FX      0x80   // thought bubbles, sparkles, tears, steam...

#define STYLE_ROBOT 0    // solid Vector-style eye + specular glints
#define STYLE_PUPIL 1    // white eye + moving iris/pupil

// face presets for the "preset" command / face button
#define FACE_EYES     0
#define FACE_BROWS    1
#define FACE_MOUTH    2
#define FACE_FULL     3
#define FACE_FULLNOSE 4
#define NUM_FACE      5

struct Spring { float v, tgt, vel; };

// every animated quantity lives in one array so the solver, the preset
// loader and the tuning table can all just walk it
#define S_GAZEX   0
#define S_GAZEY   1
#define S_ROUND   2
#define S_BROWAL  3
#define S_BROWAR  4
#define S_BROWY   5
#define S_MOUTHC  6
#define S_MOUTHO  7
#define S_MOUTHW  8
#define S_BLUSH   9
#define S_EW     10   // +eye
#define S_EH     12
#define S_LIDT   14
#define S_LIDB   16
#define S_LIDTS  18
#define S_LIDBS  20
#define NSPRING  22

class Face {
public:
  explicit Face(Oled &g);

  void begin();
  void boot();                                   // power-on animation

  void setEmotion(uint8_t e, bool instant = false);
  uint8_t emotion() const { return emo; }

  void setFeatures(uint8_t f);
  void feature(uint8_t bit, bool on);
  uint8_t featureMask() const { return features; }
  void setFacePreset(uint8_t p);
  uint8_t facePreset() const { return facePre; }

  void setStyle(uint8_t s) { style = s; }
  uint8_t styleId() const { return style; }

  void setAuto(bool a) { autoMode = a; }
  bool autoOn() const { return autoMode; }

  void look(int8_t x, int8_t y, uint16_t holdMs = 2500);
  void doBlink(uint8_t eye = 0);                 // 0 both / 1 left / 2 right
  void talk(uint16_t ms);
  void setSpeed(uint8_t pct);
  uint8_t speed() const { return speedPct; }
  void startDemo();

  void update(float dt);
  void render();

private:
  Oled &u;
  EmoPreset cur;

  uint8_t emo, features, style, speedPct, facePre;
  bool    autoMode;

  // layout (recomputed whenever the feature set changes)
  int8_t eyeCX[2];
  int8_t eyeCY, baseW, baseH, mouthCY, browY0, noseCY;

  // animated state
  Spring S[NSPRING];
  float  blinkOpen[2];
  float  microX, microY, breath;
  uint32_t tFrame;            // time sampled once per frame
  int8_t biasX, biasY;

  // schedulers
  uint8_t  blinkState, blinkEye, demoIdx;
  uint32_t tBlink0, tNextBlink, tNextSac, tNextEmo, tGazeHold;
  uint32_t tTalk, tTalkStep, tDemo;

  // Animation phase 0..255 over `per` ms.  Reads tFrame, NOT millis():
  // with a paged buffer drawAll() runs once per page, and a live clock
  // would give each band a different time and shear the effects apart.
  // out-of-line on purpose: 32-bit modulo + divide, called from a dozen
  // places, so one copy is far cheaper in flash than a dozen inlined ones
  __attribute__((noinline)) uint8_t phase(uint16_t per, uint8_t offset) const;
  void recomputeLayout();
  void applyPreset(bool instant);

  void drawAll();
  void drawEye(uint8_t i);
  void drawLid(uint8_t i, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
               float cov, float slant, bool top, uint8_t mode);
  void drawBrows();
  void drawNose();
  void drawMouth();
  void drawBlush();
  void drawFx();
  void fillTri(int16_t ax, int16_t ay, int16_t bx0, int16_t bx1, int16_t by);
  void drawHeart(int16_t cx, int16_t cy, int16_t w, int16_t h);
  void drawSpiral(int16_t cx, int16_t cy, int16_t r);
  void drawDrop(int16_t x, int16_t y, int16_t r);
  void drawStar(int16_t x, int16_t y, int16_t r);
  void drawRing(int16_t x, int16_t y, int16_t r);
  void drawZ(int16_t x, int16_t y, int16_t s);
  void drawQmark(int16_t x, int16_t y);
};
