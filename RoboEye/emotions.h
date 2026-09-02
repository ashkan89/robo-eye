#pragma once
#include <Arduino.h>

// ------------------------------------------------------------ effects
#define FX_NONE      0
#define FX_SPARKLE   1
#define FX_TEAR      2
#define FX_SWEAT     3
#define FX_ZZZ       4
#define FX_QUESTION  5
#define FX_STEAM     6
#define FX_HEART     7
#define FX_SPIRAL    8

// ------------------------------------------------------- preset flags
#define FL_CURVE_B      0x01   // bottom lid is a curve (happy crescent)
#define FL_CURVE_T      0x02   // top lid is a curve   (sad crescent)
#define FL_WINK_R       0x04
#define FL_WINK_L       0x08
#define FL_BROW_ASYM    0x10   // one eyebrow raised
#define FL_HEART_EYES   0x20
#define FL_SPIRAL_EYES  0x40

// ---------------------------------------------------------------------
//  One emotion = 18 bytes, kept in flash.
//  eyeW/eyeH ..... % of the layout's base eye size
//  lidT/lidB ..... lid coverage, % of eye height
//  lidTs/lidBs ... lid slant, % (+ = inner corner covered more -> angry)
//  browA ......... eyebrow angle (+ = inner end down -> angry)
//  browY ......... eyebrow vertical offset in px (+ = lower)
//  mouthC ........ mouth curve (-100 frown .. +100 smile)
//  mouthO ........ mouth opening 0..100
//  mouthW ........ mouth width %
//  gazeX/gazeY ... resting gaze bias %
//  blush ......... 0..100
//  roundv ........ corner roundness %
//  blinkR ........ blink interval scale, % (lower = blinks more)
// ---------------------------------------------------------------------
struct EmoPreset {
  int8_t  eyeW, eyeH;
  int8_t  lidT, lidTs, lidB, lidBs;
  int8_t  browA, browY;
  int8_t  mouthC, mouthO, mouthW;
  int8_t  gazeX, gazeY;
  int8_t  blush;
  uint8_t fx, flags;
  int8_t  roundv;
  uint8_t blinkR;
};

#define EMO_NEUTRAL     0
#define EMO_HAPPY       1
#define EMO_GLEE        2
#define EMO_SAD         3
#define EMO_ANGRY       4
#define EMO_FURIOUS     5
#define EMO_SURPRISED   6
#define EMO_SCARED      7
#define EMO_SLEEPY      8
#define EMO_TIRED       9
#define EMO_SUSPICIOUS 10
#define EMO_SKEPTICAL  11
#define EMO_CONFUSED   12
#define EMO_LOVE       13
#define EMO_EXCITED    14
#define EMO_BORED      15
#define EMO_ANNOYED    16
#define EMO_FOCUSED    17
#define EMO_DIZZY      18
#define EMO_WINK       19
#define EMO_SHY        20
#define EMO_PROUD      21
#define NUM_EMO        22

const EmoPreset EMO[NUM_EMO] PROGMEM = {
// eW  eH  lidT lidTs lidB lidBs brA brY  mC  mO  mW  gX  gY  bl  fx           flags                       rnd blinkR
  {100,100,   0,   0,   0,   0,    0,  0,  10,  0, 100,  0,  0,  0, FX_NONE,     0,                          60, 100}, // neutral
  {100,100,   0,   0,  45,   0,  -10, -2,  80, 10, 110,  0,  0, 30, FX_NONE,     FL_CURVE_B,                 70, 110}, // happy
  {105,105,   0,   0,  62,   0,  -16, -4,  95, 45, 115,  0,  0, 60, FX_SPARKLE,  FL_CURVE_B,                 70, 120}, // glee
  { 95, 92,  34, -55,   0,   0,  -30,  3, -70,  0,  90,  0, 25,  0, FX_TEAR,     FL_CURVE_T,                 65,  90}, // sad
  {100, 86,  40,  70,   0,   0,   40, -1, -50, 10,  95,  0,  0,  0, FX_NONE,     0,                          40, 130}, // angry
  {105, 76,  52,  85,   8, -20,   58, -3, -60, 45, 105,  0,  0,  0, FX_STEAM,    0,                          30, 150}, // furious
  {116,126,   0,   0,   0,   0,   -6, -7,   0, 85,  70,  0, -6,  0, FX_NONE,     0,                          90, 180}, // surprised
  {110,120,   0,   0,   0,   0,  -35, -6, -40, 60,  80,  0, 10,  0, FX_SWEAT,    0,                          90,  60}, // scared
  {100, 96,  62, -10,  10,   0,   -5,  4,  15, 10,  80,  0, 15,  0, FX_ZZZ,      0,                          70,  45}, // sleepy
  {100, 96,  50, -25,   8,   0,  -12,  3, -20,  0,  85,  0, 10,  0, FX_NONE,     0,                          70,  55}, // tired
  {100, 80,  42,  10,  25,   0,   26,  0, -15,  0,  80,-35,  0,  0, FX_NONE,     FL_BROW_ASYM,               45, 140}, // suspicious
  {100, 90,  30,   0,  18,   0,   20, -2, -25,  0,  85, 20,  0,  0, FX_NONE,     FL_BROW_ASYM,               55, 130}, // skeptical
  {100,100,  18, -20,   0,   0,  -18, -4, -10, 25,  75, 25,-10,  0, FX_QUESTION, FL_BROW_ASYM,               65, 100}, // confused
  {110,110,   0,   0,   0,   0,  -12, -4,  85, 25, 110,  0,  0, 70, FX_HEART,    FL_HEART_EYES,              70, 120}, // love
  {112,116,   0,   0,   8,   0,  -20, -6,  90, 60, 115,  0, -8, 40, FX_SPARKLE,  0,                          80,  80}, // excited
  {100, 80,  55,   0,   5,   0,    8,  2, -15,  0,  80,-30, 10,  0, FX_NONE,     0,                          60,  70}, // bored
  {100, 78,  48,  45,   6,   0,   30,  0, -35,  0,  85,-25,  0,  0, FX_NONE,     0,                          45, 120}, // annoyed
  { 92, 90,  22,  20,  14,   0,   22, -1,   0,  0,  70,  0,  0,  0, FX_NONE,     0,                          45, 170}, // focused
  {105,105,   0,   0,   0,   0,    0, -3, -10, 55,  95,  0,  0, 20, FX_SPIRAL,   FL_SPIRAL_EYES,             80,  90}, // dizzy
  {100,100,   0,   0,  30,   0,  -12, -3,  75, 15, 105,  0,  0, 25, FX_NONE,     FL_CURVE_B | FL_WINK_R,     70, 120}, // wink
  { 95, 86,  40, -20,  20,   0,  -18,  2,  45,  0,  80,-30, 15, 90, FX_NONE,     FL_CURVE_B,                 70,  80}, // shy
  {100, 96,  25, -15,  20,   0,  -25, -4,  60,  0, 100,  0,-10,  0, FX_NONE,     FL_CURVE_B,                 60, 130}, // proud
};

const char en00[] PROGMEM = "neutral";     const char en01[] PROGMEM = "happy";
const char en02[] PROGMEM = "glee";        const char en03[] PROGMEM = "sad";
const char en04[] PROGMEM = "angry";       const char en05[] PROGMEM = "furious";
const char en06[] PROGMEM = "surprised";   const char en07[] PROGMEM = "scared";
const char en08[] PROGMEM = "sleepy";      const char en09[] PROGMEM = "tired";
const char en10[] PROGMEM = "suspicious";  const char en11[] PROGMEM = "skeptical";
const char en12[] PROGMEM = "confused";    const char en13[] PROGMEM = "love";
const char en14[] PROGMEM = "excited";     const char en15[] PROGMEM = "bored";
const char en16[] PROGMEM = "annoyed";     const char en17[] PROGMEM = "focused";
const char en18[] PROGMEM = "dizzy";       const char en19[] PROGMEM = "wink";
const char en20[] PROGMEM = "shy";         const char en21[] PROGMEM = "proud";

const char* const EMO_NAMES[NUM_EMO] PROGMEM = {
  en00,en01,en02,en03,en04,en05,en06,en07,en08,en09,en10,
  en11,en12,en13,en14,en15,en16,en17,en18,en19,en20,en21
};
