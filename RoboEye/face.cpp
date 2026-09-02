#include "face.h"
#include <math.h>

// ===================================================================
//  small helpers
// ===================================================================
static inline int16_t iround(float v) {
  return (int16_t)(v + (v < 0.0f ? -0.5f : 0.5f));
}
static inline int16_t clampi(int16_t v, int16_t lo, int16_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline float ease01(float p) { return p * p * (3.0f - 2.0f * p); }

// quarter sine, 0..90 deg in 16 steps, scaled to 0..127
static const int8_t SIN16[17] PROGMEM = {
  0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127
};
// a = 0..63 is one full turn -> -127..127
static int8_t isin(uint8_t a) {
  uint8_t q = (a >> 4) & 3, x = a & 15;
  int8_t v = (int8_t)pgm_read_byte(&SIN16[(q & 1) ? (16 - x) : x]);
  return (q & 2) ? (int8_t)-v : v;
}

// spring tuning: stiffness (rad/s) and damping ratio x100, one pair per
// entry of Face::S - z below 1.0 gives the motion a little pop
static const uint8_t SCFG[NSPRING * 2] PROGMEM = {
  14, 72,  14, 72,  12,100,  13, 65,  13, 65,  12, 60,   // gaze, round, brows
  14, 80,  18, 70,  13, 90,   8,100,                     // mouth, blush
  16, 80,  16, 80,  16, 78,  16, 78,                     // eye w, eye h
  15, 88,  15, 88,  15, 88,  15, 88,                     // lid t, lid b
  14, 95,  14, 95,  14, 95,  14, 95                      // lid slants
};

static const int8_t SPX[3] PROGMEM = { 12, 116, 64 };    // sparkle anchors
static const int8_t SPY[3] PROGMEM = {  9,  11,  5 };

#define BLINK_CLOSE_MS  55.0f
#define BLINK_OPEN_MS  115.0f

// phase of a repeating animation: 0..255 over `per` milliseconds
uint8_t Face::phase(uint16_t per, uint8_t offset) const {
  return (uint8_t)((tFrame % per) * 256UL / per) + offset;
}

// ===================================================================
Face::Face(Oled &g) : u(g) {}

void Face::begin() {
  features = DEFAULT_FEATURES;
  style    = DEFAULT_STYLE;
  autoMode = DEFAULT_AUTO;
  speedPct = 100;
  emo      = EMO_NEUTRAL;
  facePre  = FACE_FULL;
  blinkOpen[0] = blinkOpen[1] = 1.0f;
  blinkState = 0; blinkEye = 0; demoIdx = 255;
  microX = microY = breath = 0.0f;
  tFrame = millis();

  recomputeLayout();
  applyPreset(true);
  for (uint8_t i = 0; i < 2; i++) {
    S[S_GAZEX + i].v = S[S_GAZEX + i].tgt = S[S_GAZEX + i].vel = 0.0f;
  }

  uint32_t now = millis();
  tNextBlink = now + 1800;
  tNextSac   = now + 1200;
  tNextEmo   = now + 9000;
  tGazeHold  = 0;
  tTalk = tTalkStep = tDemo = 0;
}

// ------------------------------------------------------------ layout
void Face::recomputeLayout() {
  bool m = features & F_MOUTH;
  bool b = features & F_BROWS;
  bool n = features & F_NOSE;

  if (m && b)      { baseW = 38; baseH = 28; eyeCY = 27; }
  else if (m)      { baseW = 40; baseH = 32; eyeCY = 24; }
  else if (b)      { baseW = 42; baseH = 38; eyeCY = 36; }
  else             { baseW = 44; baseH = 42; eyeCY = 32; }

  int8_t gap = n ? 26 : 20;
  eyeCX[0] = 64 - (baseW + gap) / 2;
  eyeCX[1] = 64 + (baseW + gap) / 2;

  mouthCY = 53;
  browY0  = eyeCY - baseH / 2 - 7;
  noseCY  = n ? (m ? 43 : (int8_t)(eyeCY + baseH / 2 + 8)) : 0;
}

void Face::setFeatures(uint8_t f) {
  features = f;
  recomputeLayout();
  applyPreset(false);
}

void Face::feature(uint8_t bit, bool on) {
  setFeatures(on ? (uint8_t)(features | bit) : (uint8_t)(features & ~bit));
}

void Face::setFacePreset(uint8_t p) {
  facePre = p % NUM_FACE;
  uint8_t keep = features & F_SCAN;
  uint8_t f = F_GLINT | F_FX | F_TILT | keep;
  if (facePre == FACE_BROWS)                     f |= F_BROWS;
  else if (facePre == FACE_MOUTH)                f |= F_MOUTH;
  else if (facePre >= FACE_FULL)                 f |= F_BROWS | F_MOUTH | F_BLUSH;
  if (facePre == FACE_FULLNOSE)                  f |= F_NOSE;
  setFeatures(f);
}

// ------------------------------------------------------------ preset
void Face::applyPreset(bool instant) {
  memcpy_P(&cur, &EMO[emo], sizeof(EmoPreset));

  float t[10];
  t[S_ROUND]  = cur.roundv;
  t[S_BROWAL] = cur.browA;
  t[S_BROWAR] = (cur.flags & FL_BROW_ASYM) ? -cur.browA * 0.35f : (float)cur.browA;
  t[S_BROWY]  = cur.browY;
  t[S_MOUTHC] = cur.mouthC;
  t[S_MOUTHO] = cur.mouthO;
  t[S_MOUTHW] = cur.mouthW;
  t[S_BLUSH]  = (features & F_BLUSH) ? cur.blush : 0;
  for (uint8_t k = S_ROUND; k <= S_BLUSH; k++) {
    S[k].tgt = t[k];
    if (instant) { S[k].v = t[k]; S[k].vel = 0.0f; }
  }

  float ew  = baseW * cur.eyeW * 0.01f;
  float eh  = baseH * cur.eyeH * 0.01f;
  float lt  = cur.lidT  * 0.01f;
  float lb  = cur.lidB  * 0.01f;
  float lts = cur.lidTs * 0.01f;
  float lbs = cur.lidBs * 0.01f;

  for (uint8_t i = 0; i < 2; i++) {
    bool wink = ((cur.flags & FL_WINK_R) && i) || ((cur.flags & FL_WINK_L) && !i);
    float v[6] = { ew, eh, wink ? 0.0f : lt, wink ? 0.88f : lb,
                   lts, wink ? 0.0f : lbs };
    for (uint8_t k = 0; k < 6; k++) {
      Spring &s = S[S_EW + k * 2 + i];
      s.tgt = v[k];
      if (instant) { s.v = v[k]; s.vel = 0.0f; }
    }
  }

  biasX = cur.gazeX;
  biasY = cur.gazeY;
}

void Face::setEmotion(uint8_t e, bool instant) {
  if (e >= NUM_EMO) return;
  emo = e;
  applyPreset(instant);
  // a new expression usually arrives with a blink - it hides the morph
  if (!instant && random(100) < 65) doBlink(0);
  tNextEmo = millis() + random(7000, 16000);
}

// ------------------------------------------------------------ actions
void Face::look(int8_t x, int8_t y, uint16_t holdMs) {
  S[S_GAZEX].tgt = clampi(x, -100, 100);
  S[S_GAZEY].tgt = clampi(y, -100, 100);
  tGazeHold = millis() + holdMs;
  tNextSac  = tGazeHold;
}

void Face::doBlink(uint8_t eye) {
  if (blinkState) return;
  blinkEye   = eye;
  blinkState = 1;
  tBlink0    = millis();
}

void Face::talk(uint16_t ms) { tTalk = millis() + ms; }

void Face::setSpeed(uint8_t pct) {
  speedPct = pct < 20 ? 20 : (pct > 250 ? 250 : pct);
}

void Face::startDemo() { demoIdx = 0; tDemo = 0; }

// ===================================================================
//  UPDATE
// ===================================================================
void Face::update(float dt) {
  const uint32_t now = millis();
  const float sp = speedPct * 0.01f;

  // ---- demo reel -------------------------------------------------
  if (demoIdx < NUM_EMO && now >= tDemo) {
    setEmotion(demoIdx++, false);
    tDemo = now + 2400;
    if (demoIdx >= NUM_EMO) demoIdx = 255;
  }

  // ---- autonomous mood drift -------------------------------------
  if (autoMode && demoIdx == 255 && now >= tNextEmo) {
    uint8_t e;
    do { e = random(NUM_EMO); } while (e == emo);
    setEmotion(e, false);
  }

  // ---- blinking --------------------------------------------------
  if (!blinkState && now >= tNextBlink) {
    doBlink(0);
    uint32_t base = (2400UL + random(3600)) * cur.blinkR / 100;
    tNextBlink = now + ((random(100) < 20) ? 260UL : base);   // 1-in-5 double
  }
  if (blinkState) {
    const bool closing = (blinkState == 1);
    float p = (float)(now - tBlink0) * sp /
              (closing ? BLINK_CLOSE_MS : BLINK_OPEN_MS);
    if (p >= 1.0f) {
      p = 1.0f;
      if (closing) { blinkState = 2; tBlink0 = now; }
      else           blinkState = 0;
    }
    float o = closing ? 1.0f - ease01(p) : ease01(p);
    blinkOpen[0] = (blinkEye == 2) ? 1.0f : o;
    blinkOpen[1] = (blinkEye == 1) ? 1.0f : o;
  } else {
    blinkOpen[0] = blinkOpen[1] = 1.0f;
  }

  // ---- saccades / idle gaze --------------------------------------
  if (now >= tNextSac && now >= tGazeHold) {
    bool small = random(100) < 38;             // stay engaged vs. look around
    S[S_GAZEX].tgt = small ? random(-14, 15) : random(-42, 43);
    S[S_GAZEY].tgt = small ? random(-9, 10)  : random(-26, 27);
    tNextSac = now + (900UL + random(2800)) * cur.blinkR / 100;
    if (random(100) < 25) doBlink(0);          // big saccades carry a blink
  }

  // ---- talking ---------------------------------------------------
  if (tTalk && now < tTalk) {
    if (now >= tTalkStep) {
      S[S_MOUTHO].tgt = 12 + random(70);
      tTalkStep = now + 70 + random(70);
    }
  } else if (tTalk) {
    S[S_MOUTHO].tgt = cur.mouthO;
    tTalk = 0;
  }

  // ---- solve every spring ----------------------------------------
  for (uint8_t i = 0; i < NSPRING; i++) {
    Spring &s = S[i];
    float f = (float)pgm_read_byte(&SCFG[i * 2]) * sp;
    float z = (float)pgm_read_byte(&SCFG[i * 2 + 1]) * 0.01f;
    s.vel += (f * f * (s.tgt - s.v) - 2.0f * z * f * s.vel) * dt;
    s.v   += s.vel * dt;
  }
}

// ===================================================================
//  RENDER
//  firstPage/nextPage works for both the paged and the full buffer, so
//  the same code runs on a 328P and on a board with RAM to spare.
// ===================================================================
void Face::render() {
  tFrame = millis();          // one timestamp for every page of this frame

  // living micro-motion: detuned oscillators that never visibly repeat
  microX = isin(phase(3300, 0)) * 0.0075f + isin(phase(1430, 20)) * 0.0038f;
  microY = isin(phase(2320, 40)) * 0.0060f;
  breath = isin(phase(4050, 11)) * 0.0130f;
  if (emo == EMO_DIZZY) { microX *= 3.0f; microY *= 3.0f; }

  u.first();
  do { drawAll(); } while (u.next());
}

void Face::drawAll() {
  u.setDrawColor(1);
  drawEye(0);
  drawEye(1);
  if (features & F_BROWS) drawBrows();
  if (features & F_NOSE)  drawNose();
  if (features & F_MOUTH) drawMouth();
  if ((features & F_BLUSH) && S[S_BLUSH].v > 6.0f) drawBlush();
  if (features & F_FX)    drawFx();

  if (features & F_SCAN) {                 // CRT scan band (XOR)
    int16_t y = (int16_t)phase(3000, 0) * 62 >> 8;      // stays on-panel
    u.setDrawColor(2);
    u.drawBox(0, y, 128, 2);
    u.setDrawColor(1);
  }
}

// ---------------------------------------------------------------- eye
void Face::drawEye(uint8_t i) {
  const float gxf = S[S_GAZEX].v + biasX;
  const float gyf = S[S_GAZEY].v + biasY;

  int16_t tilt = 0;
  if (features & F_TILT) tilt = iround(gxf * 0.035f) * (i ? 1 : -1);

  int16_t cx = eyeCX[i] + iround(gxf * 0.10f + microX);
  int16_t cy = eyeCY    + iround(gyf * 0.06f + microY + breath) + tilt;

  int16_t w = iround(S[S_EW + i].v);
  int16_t h = iround(S[S_EH + i].v * blinkOpen[i]);
  if (w < 6) w = 6;
  if (h < 2) h = 2;
  if (w > 120) w = 120;
  if (h > 62) h = 62;

  // Keep the whole eye on the panel.  A rect that hangs off the edge also
  // means an off-panel clip window for the glints, and a big gaze bias
  // plus a `look -100 0` can push it there.
  cx = clampi(cx, w / 2, (int16_t)(127 - (w - 1 - w / 2)));
  cy = clampi(cy, h / 2, (int16_t)(63 - (h - 1 - h / 2)));

  int16_t x0 = cx - w / 2, y0 = cy - h / 2;
  int16_t x1 = x0 + w - 1, y1 = y0 + h - 1;

  if (cur.flags & FL_HEART_EYES) { drawHeart(cx, cy, w, h); return; }

  int16_t mn = (w < h) ? w : h;
  int16_t r  = (int16_t)((mn / 2) * S[S_ROUND].v * 0.01f);
  int16_t rmax = (mn - 1) / 2;
  if (r > rmax) r = rmax;
  if (r < 0) r = 0;

  u.setDrawColor(1);
  if (r < 1) u.drawBox(x0, y0, w, h);
  else       u.drawRBox(x0, y0, w, h, r);

  // ---- iris / specular glints ------------------------------------
  // Gate these on how much of the eye the lids actually leave showing,
  // not on its full height: a glint punched through a nearly shut eye
  // erases the thin band that is the whole expression.
  float opening = 1.0f - S[S_LIDT + i].v - S[S_LIDB + i].v;
  int16_t vis = (opening > 0.0f) ? (int16_t)(h * opening) : 0;

  if ((cur.flags & FL_SPIRAL_EYES) && vis > 12) {
    drawSpiral(cx, cy, mn / 2 - 2);
  } else if (style == STYLE_PUPIL && vis > 12) {
    int16_t pr = mn / 4;
    if (pr < 2) pr = 2;
    int16_t px = cx + iround(gxf * 0.004f * w);
    int16_t py = cy + iround(gyf * 0.004f * h);
    u.setClipWindow(x0, y0, x1 + 1, y1 + 1);
    u.setDrawColor(0);
    u.drawDisc(px, py, pr);
    u.setDrawColor(1);
    int16_t gr = pr / 3;
    if (gr < 1) u.drawPixel(px - pr / 2, py - pr / 2);
    else        u.drawDisc(px - pr / 2, py - pr / 2, gr);
    u.setMaxClipWindow();
  } else if ((features & F_GLINT) && vis > 11 && w > 12) {
    int16_t g1 = clampi(w / 8, 2, 6);
    u.setClipWindow(x0, y0, x1 + 1, y1 + 1);
    u.setDrawColor(0);
    u.drawDisc(x0 + w / 4, y0 + h / 4, g1);
    u.drawDisc(x0 + (w * 2) / 3, y0 + (h * 5) / 8, g1 / 2 > 0 ? g1 / 2 : 1);
    u.setMaxClipWindow();
    u.setDrawColor(1);
  }

  // ---- eyelids ---------------------------------------------------
  const float lt = S[S_LIDT + i].v, lb = S[S_LIDB + i].v;
  u.setDrawColor(0);   // lids are cut out of the eye, so draw them black
  if (lt > 0.01f)
    drawLid(i, x0, y0, x1, y1, lt, S[S_LIDTS + i].v, true,
            (cur.flags & FL_CURVE_T) ? 1 : 0);
  if (lb > 0.01f) {
    if (cur.flags & FL_CURVE_B) {           // arch: shave both edges
      drawLid(i, x0, y0, x1, y1, lb, 0.0f, true, 2);
      drawLid(i, x0, y0, x1, y1, lb, S[S_LIDBS + i].v, false, 1);
    } else {
      drawLid(i, x0, y0, x1, y1, lb, S[S_LIDBS + i].v, false, 0);
    }
  }
  u.setDrawColor(1);
}

// A lid is a field of black columns eating into the eye.
//   mode 0 - flat, the whole lid drops by the same amount
//   mode 1 - parabola biting deepest in the middle
//   mode 2 - parabola biting deepest at the corners
// An arched "^" happy eye is mode 2 on the top lid plus mode 1 on the
// bottom: both edges then rise towards the centre and the white left
// between them is a band that follows the arch.  `slant` tips the inner
// corner down (angry) or up (sad).  Fixed point - no floats in the loop.
void Face::drawLid(uint8_t i, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                   float cov, float slant, bool top, uint8_t mode) {
  const int16_t h = y1 - y0 + 1;
  const int16_t W = (x1 > x0) ? (x1 - x0) : 1;

  const int32_t baseD = (int32_t)(h * cov * 256.0f);
  const int32_t slD   = (int32_t)(h * slant * 0.5f * 256.0f);

  for (int16_t x = x0; x <= x1; x++) {
    int32_t uq = ((int32_t)(x - x0) * 512) / W - 256;        // -256..+256
    int32_t bump = 256;                                      //  0..256
    if (mode == 1)      bump = (65536 - uq * uq) >> 8;
    else if (mode == 2) bump = (uq * uq) >> 8;
    int32_t d256 = ((baseD * bump) >> 8) + ((slD * (i == 0 ? uq : -uq)) >> 8);
    int16_t d = (int16_t)(d256 >> 8);
    if (d <= 0) continue;
    if (d > h) d = h;
    if (top) u.drawVLine(x, y0, d);
    else     u.drawVLine(x, y1 - d + 1, d);
  }
}

// -------------------------------------------------------------- brows
void Face::drawBrows() {
  const float gxf = S[S_GAZEX].v + biasX;
  for (uint8_t i = 0; i < 2; i++) {
    int16_t w  = iround(S[S_EW + i].v * 0.98f);
    int16_t cx = eyeCX[i] + iround(gxf * 0.05f + microX * 0.5f);
    int16_t y  = browY0 + iround(S[S_BROWY].v + breath * 0.6f);
    if ((cur.flags & FL_BROW_ASYM) && i == 1) y -= 5;

    if (w < 2) w = 2;
    int16_t d  = iround(S[S_BROWAL + i].v * 0.085f);
    int16_t ad = d < 0 ? -d : d;
    y = clampi(y, 1 + ad, 40);                 // keep the tilt on the panel
    int16_t xl = clampi(cx - w / 2, 0, (int16_t)(127 - w));
    int16_t yL = (i == 0) ? (y - d) : (y + d);
    int16_t yR = (i == 0) ? (y + d) : (y - d);
    for (int16_t k = 0; k <= w; k++)
      u.drawVLine(xl + k, yL + (int16_t)(((int32_t)(yR - yL) * k) / w), 4);
  }
}

// --------------------------------------------------------------- nose
void Face::drawNose() {
  int16_t y = noseCY + iround(breath * 0.4f);
  fillTri(64, y - 5, 60, 68, y + 3);
}

// -------------------------------------------------------------- mouth
// Two parabolic lips filled column by column: one shape covers smiles,
// frowns, open grins and the surprised "O".
void Face::drawMouth() {
  int16_t mw  = clampi(iround(0.48f * S[S_MOUTHW].v), 12, 74);
  int16_t x0  = 64 - mw / 2;
  int16_t cyb = mouthCY + iround(breath * 0.4f);

  const int32_t curveQ = (int32_t)(S[S_MOUTHC].v * 0.085f * 256.0f);  // px << 8
  const int32_t openQ  = (int32_t)(S[S_MOUTHO].v * 0.190f * 256.0f);  // px << 8

  for (int16_t x = x0; x <= x0 + mw; x++) {
    int32_t uq   = ((int32_t)(x - x0) * 512) / mw - 256;
    int32_t p    = (65536 - uq * uq) >> 8;                       // 0..256
    // centre the curve on the baseline: the corners lift as much as the
    // middle drops, so a big grin cannot walk off the bottom of the panel
    int32_t ymid = ((int32_t)cyb << 8) + ((curveQ * p) >> 8) - (curveQ >> 1);
    int32_t half = (openQ * p) >> 9;
    int16_t yt = clampi((int16_t)((ymid - half) >> 8), 0, 63);
    int16_t yb = clampi((int16_t)((ymid + half) >> 8), 0, 63);
    if (yb < yt + 1) yb = yt + 1;
    u.drawVLine(x, yt, yb - yt + 1);
  }
}

// -------------------------------------------------------------- blush
void Face::drawBlush() {
  int16_t y = eyeCY + baseH / 2 + 5;
  uint8_t n = (S[S_BLUSH].v > 55.0f) ? 3 : 2;
  for (uint8_t i = 0; i < 2; i++) {
    int16_t cx = eyeCX[i] + (i ? 6 : -6);
    for (uint8_t k = 0; k < n; k++) {
      int16_t x = cx - 5 + k * 5;
      u.drawLine(x, y + 3, x + 3, y - 3);
    }
  }
}

// ------------------------------------------------------------- shapes
// filled isoceles triangle: apex at (ax,ay), base along row `by`
void Face::fillTri(int16_t ax, int16_t ay, int16_t bx0, int16_t bx1, int16_t by) {
  int16_t half = (bx1 - bx0) / 2;
  if (half < 1) return;
  for (int16_t x = bx0; x <= bx1; x++) {
    int16_t t  = x > ax ? x - ax : ax - x;              // 0..half
    int16_t ye = ay + (int16_t)(((int32_t)(by - ay) * t) / half);
    int16_t y0 = ye < by ? ye : by;
    u.drawVLine(x, y0, (ye < by ? by - ye : ye - by) + 1);
  }
}

void Face::drawHeart(int16_t cx, int16_t cy, int16_t w, int16_t h) {
  int16_t r = w / 4;
  if (r < 2) r = 2;
  u.setDrawColor(1);
  u.drawDisc(cx - r, cy - h / 6, r);
  u.drawDisc(cx + r, cy - h / 6, r);
  fillTri(cx, cy + h / 2, cx - 2 * r, cx + 2 * r, cy - h / 8);
}

void Face::drawSpiral(int16_t cx, int16_t cy, int16_t r) {
  if (r < 3) return;
  uint8_t a = phase(2600, 0) >> 2;
  float dx = r * isin(a + 16) * 0.00787f;      // cos
  float dy = r * isin(a)      * 0.00787f;
  const float c = 0.825336f, s = 0.564642f;    // rotate by 0.6 rad
  int16_t px = cx + iround(dx), py = cy + iround(dy);
  u.setDrawColor(0);
  for (uint8_t n = 0; n < 22; n++) {
    float nx = 0.90f * (dx * c - dy * s);      // rotate and shrink
    float ny = 0.90f * (dx * s + dy * c);
    dx = nx; dy = ny;
    int16_t qx = cx + iround(dx), qy = cy + iround(dy);
    u.drawLine(px, py, qx, qy);
    px = qx; py = qy;
    if (dx * dx + dy * dy < 1.5f) break;
  }
  u.setDrawColor(1);
}

void Face::drawDrop(int16_t x, int16_t y, int16_t r) {
  if (r < 1) r = 1;
  u.drawDisc(x, y, r);
  fillTri(x, y - r * 3, x - r, x + r, y - 1);
}

void Face::drawStar(int16_t x, int16_t y, int16_t r) {
  if (r < 1) return;
  u.drawLine(x - r, y, x + r, y);
  u.drawLine(x, y - r, x, y + r);
  int16_t d = r / 2;
  if (d) {
    u.drawLine(x - d, y - d, x + d, y + d);
    u.drawLine(x - d, y + d, x + d, y - d);
  }
}

// hollow ring = white disc with a black one punched out (cheaper in
// flash than pulling in the circle primitive)
void Face::drawRing(int16_t x, int16_t y, int16_t r) {
  if (r < 2) return;
  u.setDrawColor(1); u.drawDisc(x, y, r);
  u.setDrawColor(0); u.drawDisc(x, y, r - 1);
  u.setDrawColor(1);
}

// a scalable "Z" drawn from three strokes
void Face::drawZ(int16_t x, int16_t y, int16_t s) {
  if (s < 3) return;
  u.drawHLine(x, y, s);
  u.drawLine(x + s - 1, y, x, y + s - 1);
  u.drawHLine(x, y + s - 1, s);
  if (s >= 7) { u.drawHLine(x, y + 1, s); u.drawHLine(x, y + s - 2, s); }
}

// a hand-plotted question mark, 5 x 9
void Face::drawQmark(int16_t x, int16_t y) {
  u.drawHLine(x + 1, y, 3);
  u.drawPixel(x, y + 1);      u.drawPixel(x + 4, y + 1);
  u.drawPixel(x + 4, y + 2);  u.drawPixel(x + 3, y + 3);
  u.drawPixel(x + 2, y + 4);  u.drawPixel(x + 2, y + 5);
  u.drawPixel(x + 2, y + 8);
}

// ------------------------------------------------------------ effects
void Face::drawFx() {
  switch (cur.fx) {

    case FX_ZZZ:
      for (uint8_t k = 0; k < 3; k++) {
        uint8_t ph = phase(2000, k * 87);
        int16_t y  = 26 - ((int16_t)ph * 22 >> 8);
        int16_t x  = 100 + (isin(ph >> 2) >> 5) + k * 5;
        if (y > 2) drawZ(x, y, 4 + k * 2);
      }
      break;

    case FX_QUESTION: {
      int16_t y = 4 + (isin(phase(900, 0) >> 2) >> 6);
      drawQmark(108, y);
      drawQmark(99, y + 8);
    } break;

    case FX_SWEAT:
      drawDrop(114, 12 + ((int16_t)phase(1200, 0) * 28 >> 8), 3);
      break;

    case FX_TEAR:
      for (uint8_t k = 0; k < 2; k++) {
        int16_t y = eyeCY + baseH / 2 + ((int16_t)phase(1700, k * 128) * 20 >> 8);
        if (y < 62) drawDrop(eyeCX[k] + (k ? 8 : -8), y, 2);
      }
      break;

    case FX_SPARKLE:
      for (uint8_t k = 0; k < 3; k++) {
        uint8_t ph = phase(900, k * 95);
        uint8_t a  = (ph < 128) ? ph : (uint8_t)(255 - ph);
        drawStar((int8_t)pgm_read_byte(&SPX[k]),
                 (int8_t)pgm_read_byte(&SPY[k]), (int16_t)a * 11 >> 7);
      }
      break;

    case FX_STEAM:
      for (uint8_t k = 0; k < 2; k++) {
        uint8_t ph = phase(1100, k * 128);
        int16_t y  = 15 - ((int16_t)ph * 13 >> 8);
        int16_t r  = 2 + ((int16_t)ph * 3 >> 8);
        if (y - r > 0) { drawRing(7 + k * 3, y, r); drawRing(121 - k * 3, y, r); }
      }
      break;

    case FX_HEART:
      for (uint8_t k = 0; k < 2; k++) {
        uint8_t ph = phase(1800, k * 128);
        int16_t y  = 30 - ((int16_t)ph * 26 >> 8);
        int16_t x  = 112 + (isin(ph >> 2) >> 5);
        if (y > 5) { int16_t s = 5 + ((int16_t)ph * 4 >> 8); drawHeart(x, y, s, s); }
      }
      break;

    default: break;
  }
}

// ===================================================================
//  Boot animation - CRT power-on, then the eyes wake up and say hi.
// ===================================================================
void Face::boot() {
  for (int16_t w = 2; w <= 104; w += 8) {          // hairline stretches out
    u.first();
    do { u.drawBox(64 - w / 2, 31, w, 2); } while (u.next());
  }
  for (uint8_t s = 0; s <= 10; s++) {              // splits into two eyes
    float p = ease01(s / 10.0f);
    int16_t h = 2 + (int16_t)(p * baseH);
    int16_t w = 4 + (int16_t)(p * baseW);
    int16_t r = (int16_t)(((w < h ? w : h) / 2) * 0.7f);
    u.first();
    do {
      for (uint8_t i = 0; i < 2; i++) {
        int16_t cx = 64 + (int16_t)((eyeCX[i] - 64) * p);
        int16_t cy = 31 + (int16_t)((eyeCY - 31) * p);
        if (r < 1) u.drawBox(cx - w / 2, cy - h / 2, w, h);
        else       u.drawRBox(cx - w / 2, cy - h / 2, w, h, r);
      }
    } while (u.next());
  }
  static const int8_t seq[3] = { -60, 60, 0 };     // a quick "hello" glance
  for (uint8_t k = 0; k < 3; k++) {
    look(seq[k], (k == 2) ? 0 : -10, 400);
    uint32_t t0 = millis();
    while (millis() - t0 < 340) { update(0.02f); render(); }
  }
  doBlink(0);
}
