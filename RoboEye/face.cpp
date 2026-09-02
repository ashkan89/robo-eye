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
// NaN-safe: the comparisons fail for NaN, so it comes out as `lo`
static inline float clampf(float v, float lo, float hi) {
  return (v > lo) ? (v < hi ? v : hi) : lo;
}

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

// longest step the spring solver will take in one go; see Face::update
#define SUB_DT          0.025f

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
  panel    = OLED_PANEL;
  bandFx   = SPLIT_FX_IN_BAND;
  recomputeRegions();
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

// ----------------------------------------------------------- regions
// A two-colour module is one mono panel with yellow phosphor over its top
// PANEL_SPLIT_ROWS rows and a dead seam below them.  Keeping the face
// inside the rows underneath is the whole of the "support": the driver is
// untouched, only the geometry moves.
void Face::recomputeRegions() {
  const bool sp = (panel == PANEL_SPLIT);
  faceY0 = sp ? (PANEL_SPLIT_ROWS + PANEL_SPLIT_GAP) : 0;
  faceY1 = OLED_H - 1;

  // The floating effects either own the yellow strip - they are accents,
  // and yellow accents over a blue face read as deliberate - or share the
  // face's region.  On a mono panel they keep the top of the screen,
  // exactly as before.
  if (sp && bandFx) { fxY0 = 0;      fxH = PANEL_SPLIT_ROWS - PANEL_SPLIT_GAP; }
  else              { fxY0 = faceY0; fxH = 32; }
}

void Face::setPanel(uint8_t p) {
  panel = (p == PANEL_SPLIT) ? PANEL_SPLIT : PANEL_MONO;
  recomputeRegions();
  recomputeLayout();
  applyPreset(true);           // snap: morphing across a relayout is noise
}

void Face::setFxInBand(bool b) {
  bandFx = b;
  recomputeRegions();
}

// ------------------------------------------------------------ layout
// Budget the region's height instead of hard-coding rows, so one set of
// rules gives the old 64-row layout on a mono panel and a correctly
// proportioned one inside the 48 blue rows of a split panel.
void Face::recomputeLayout() {
  const bool m = features & F_MOUTH;
  const bool b = features & F_BROWS;
  const bool n = features & F_NOSE;

  const int16_t H = faceY1 - faceY0 + 1;

  // Everything vertical is expressed as a fraction of a full 64-row panel
  // and then scaled, so a face squeezed into 48 blue rows keeps its
  // proportions instead of growing a mouth that dwarfs the eyes.  vs16 is
  // that scale in 1/256ths, and it is exactly 256 on a mono panel - which
  // is why the numbers below are the ones the layout was tuned with.
  vs16   = (uint16_t)((uint32_t)H * 256u / 64u);
  browT  = (int8_t)(((int16_t)4 * vs16) >> 8);   // brow bar thickness
  if (browT < 2) browT = 2;
  browGap = (int8_t)(((int16_t)7 * vs16) >> 8);  // brow baseline above the eye
  if (browGap < browT + 1) browGap = browT + 1;

  const int16_t top = b ? (int16_t)(((int32_t)9 * vs16) >> 8) : 2;
  const int16_t mouthBand = m ? (int16_t)(((int32_t)20 * vs16) >> 8) : 3;
  const int16_t bot = H - mouthBand;       // last row before the mouth
  int16_t avail = bot - top + 1;
  if (avail < 10) avail = 10;

  // `surprised` inflates the eye to 126% of baseH, so sizing baseH at 72%
  // of the space leaves even the biggest eye room to grow into.
  baseH = (int8_t)((avail * 72) / 100);
  if (baseH < 6) baseH = 6;
  eyeCY = (int8_t)(faceY0 + top + avail / 2);

  baseW = (int8_t)(baseH + 12);            // squarer as the eye gets taller
  if (baseW > 44) baseW = 44;              // two eyes plus the gap must fit

  const int8_t gap = n ? 26 : 20;
  eyeCX[0] = 64 - (baseW + gap) / 2;
  eyeCX[1] = 64 + (baseW + gap) / 2;

  mouthCY = (int8_t)(faceY1 - mouthBand / 2);
  browY0  = eyeCY - baseH / 2 - browGap;
  noseCY  = n ? (m ? (int8_t)((eyeCY + baseH / 2 + mouthCY) / 2)
                   : (int8_t)(eyeCY + baseH / 2 + 8))
              : 0;
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

  // Clamp here rather than trusting the caller.  The solver below is
  // stable for any dt it is actually given, but only because the substep
  // count is bounded - hand it a whole second and the substeps get long
  // enough to ring again.  0.05 s is two substeps, which is stable even
  // at `speed 250`.  The test also rejects a NaN or a negative dt.
  if (!(dt > 0.0f)) dt = 0.001f;
  if (dt > 0.05f)   dt = 0.05f;

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
  //
  // The obvious integration - `vel += (f*f*err - 2*z*f*vel) * dt` - is only
  // conditionally stable.  Its damping term alone multiplies the velocity
  // by (1 - 2*z*f*dt) each step, so once 2*z*f*dt passes 2 the sign flips
  // AND the magnitude grows: the value doubles, redoubles, reaches the
  // float range in a second or so and then goes to inf, at which point
  // `tgt - v` is NaN and STAYS NaN.  Nothing recovers from that - a new
  // emotion only writes `tgt` - so the feature is stuck drawing garbage
  // for the rest of the run.  The mouth-open spring is the stiffest at
  // f = 18 rad/s, which put the old code over the limit at `speed 250`
  // (f is scaled by the speed) or at any frame slower than ~55 ms.
  //
  // Two changes make it unconditionally stable:
  //   - the damping is resolved implicitly, as a divide, which can only
  //     ever shrink the velocity whatever dt is;
  //   - the step is subdivided so the remaining explicit term never sees
  //     more than SUB_DT at a time.
  // The range check afterwards is the last line of defence: a value that
  // still leaves its sane range is snapped to its target, so even an
  // unforeseen route to inf costs one frame instead of the whole session.
  uint8_t steps = 1;
  while (steps < 4 && dt > SUB_DT * steps) steps++;
  const float h = dt / steps;

  for (uint8_t i = 0; i < NSPRING; i++) {
    Spring &s = S[i];
    const float f = (float)pgm_read_byte(&SCFG[i * 2]) * sp;
    const float z = (float)pgm_read_byte(&SCFG[i * 2 + 1]) * 0.01f;
    const float damp = 1.0f / (1.0f + 2.0f * z * f * h);
    for (uint8_t k = 0; k < steps; k++) {
      s.vel = (s.vel + f * f * (s.tgt - s.v) * h) * damp;
      s.v  += s.vel * h;
    }
    // written as a failed positive test so that a NaN also trips it
    if (!(s.v > -4000.0f && s.v < 4000.0f)) { s.v = s.tgt; s.vel = 0.0f; }
    if (!(s.vel > -100000.0f && s.vel < 100000.0f)) s.vel = 0.0f;
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
    const int16_t regH = faceY1 - faceY0 + 1;
    int16_t y = faceY0 + ((int16_t)phase(3000, 0) * (regH - 2) >> 8);
    u.setDrawColor(2);
    u.drawBox(0, y, OLED_W, 2);
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

  // Bound the eye by the layout, not just by the panel.  The widest
  // preset is 126% of the base size and a spring overshoots by a few
  // percent on top, so 150% is generous; letting it reach the full 128
  // columns would, on the heart-eye emotions, draw a heart the size of
  // the screen.
  const int16_t regH = faceY1 - faceY0 + 1;
  int16_t w = iround(S[S_EW + i].v);
  int16_t h = iround(S[S_EH + i].v * blinkOpen[i]);
  if (w < 6) w = 6;
  if (h < 2) h = 2;
  if (w > baseW + baseW / 2) w = baseW + baseW / 2;
  if (h > baseH + baseH / 2) h = baseH + baseH / 2;
  if (w > OLED_W - 8) w = OLED_W - 8;
  if (h > regH - 2)   h = regH - 2;

  // Keep the whole eye inside the face region.  A rect that hangs off the
  // edge also means an off-region clip window for the glints, and a big
  // gaze bias plus a `look -100 0` can push it there.
  cx = clampi(cx, w / 2, (int16_t)(OLED_W - 1 - (w - 1 - w / 2)));
  cy = clampi(cy, faceY0 + h / 2, (int16_t)(faceY1 - (h - 1 - h / 2)));

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
  // Everything here is positioned against the band the lids actually
  // leave open, never against the full eye box.  A glint placed at a
  // fixed fraction of the box - which is what this used to do - lands on
  // top of the lid the moment an emotion closes one: the black disc then
  // merges with the black lid and eats a bite out of the eye's edge,
  // leaving a stray disconnected sliver in the corner.  That was the
  // "proud is glitched" report, and sad, focused and skeptical had it too.
  const float lt = S[S_LIDT + i].v, lb = S[S_LIDB + i].v;
  const int16_t vy0 = y0 + (lt > 0.0f ? (int16_t)(h * lt) : 0);  // first open row
  const int16_t vy1 = y1 - (lb > 0.0f ? (int16_t)(h * lb) : 0);  // last open row
  const int16_t vis = vy1 - vy0 + 1;
  const int16_t vcy = (vy0 + vy1) / 2;

  if ((cur.flags & FL_SPIRAL_EYES) && vis > 12) {
    int16_t sr = mn / 2 - 2;
    if (sr > vis / 2 - 1) sr = vis / 2 - 1;
    drawSpiral(cx, vcy, sr);
  } else if (style == STYLE_PUPIL && vis > 12) {
    int16_t pr = mn / 4;
    if (pr > (vis - 2) / 2) pr = (vis - 2) / 2;
    if (pr < 2) pr = 2;
    int16_t px = cx + iround(gxf * 0.004f * w);
    int16_t py = vcy + iround(gyf * 0.004f * vis);
    u.setClipWindow(x0, vy0, x1 + 1, vy1 + 1);
    u.setDrawColor(0);
    u.drawDisc(px, py, pr);
    u.setDrawColor(1);
    int16_t gr = pr / 3;
    if (gr < 1) u.drawPixel(px - pr / 2, py - pr / 2);
    else        u.drawDisc(px - pr / 2, py - pr / 2, gr);
    u.setMaxClipWindow();
  } else if ((features & F_GLINT) && vis >= 16 && w > 12) {
    // Two highlights, both sized and placed so that at least one lit row
    // is left between them and either lid.  Below 16 open rows there is
    // no room for that and the eye is better off plain.
    int16_t g1 = clampi(w / 8, 2, 6);
    int16_t gy = vy0 + vis / 3;
    if (g1 > gy - vy0 - 1) g1 = gy - vy0 - 1;
    int16_t g2 = g1 / 2 > 0 ? g1 / 2 : 1;
    int16_t hy = vy0 + (vis * 2) / 3;
    if (g2 > vy1 - hy - 1) g2 = vy1 - hy - 1;
    if (g1 >= 2) {
      u.setClipWindow(x0, vy0, x1 + 1, vy1 + 1);
      u.setDrawColor(0);
      u.drawDisc(x0 + w / 4, gy, g1);
      if (g2 >= 1) u.drawDisc(x0 + (w * 2) / 3, hy, g2);
      u.setMaxClipWindow();
      u.setDrawColor(1);
    }
  }

  // ---- eyelids ---------------------------------------------------
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
    if ((cur.flags & FL_BROW_ASYM) && i == 1) y -= (5 * vs16) >> 8;

    if (w < 2) w = 2;
    int16_t d  = iround(S[S_BROWAL + i].v * 0.085f * vs16 * (1.0f / 256.0f));
    int16_t ad = d < 0 ? -d : d;
    // keep the whole tilted bar inside the face region, above the eye
    int16_t yLo = faceY0 + 1 + ad;
    int16_t yHi = eyeCY - baseH / 2 - 1 - ad;
    if (yHi < yLo) yHi = yLo;
    y = clampi(y, yLo, yHi);
    int16_t xl = clampi(cx - w / 2, 0, (int16_t)(OLED_W - 1 - w));
    int16_t yL = (i == 0) ? (y - d) : (y + d);
    int16_t yR = (i == 0) ? (y + d) : (y - d);
    for (int16_t k = 0; k <= w; k++)
      u.drawVLine(xl + k, yL + (int16_t)(((int32_t)(yR - yL) * k) / w), browT);
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
  // S_MOUTHW/C/O are spring outputs; clamp before they reach the fixed
  // point maths so one wild frame cannot scale into a screen-wide bar
  int16_t mw  = clampi(iround(0.48f * clampf(S[S_MOUTHW].v, 0.0f, 200.0f)), 12, 74);
  int16_t x0  = 64 - mw / 2;
  int16_t cyb = mouthCY + iround(breath * 0.4f);

  // *vs16 rather than *256 folds the region scale into the fixed point
  const int32_t curveQ = (int32_t)(clampf(S[S_MOUTHC].v, -150.0f, 150.0f) * 0.085f * vs16);
  const int32_t openQ  = (int32_t)(clampf(S[S_MOUTHO].v,    0.0f, 150.0f) * 0.190f * vs16);

  for (int16_t x = x0; x <= x0 + mw; x++) {
    int32_t uq   = ((int32_t)(x - x0) * 512) / mw - 256;
    int32_t p    = (65536 - uq * uq) >> 8;                       // 0..256
    // centre the curve on the baseline: the corners lift as much as the
    // middle drops, so a big grin cannot walk off the bottom of the panel
    int32_t ymid = ((int32_t)cyb << 8) + ((curveQ * p) >> 8) - (curveQ >> 1);
    int32_t half = (openQ * p) >> 9;
    int16_t yt = clampi((int16_t)((ymid - half) >> 8), faceY0, faceY1);
    int16_t yb = clampi((int16_t)((ymid + half) >> 8), faceY0, faceY1);
    if (yb < yt + 1) yb = yt + 1;
    u.drawVLine(x, yt, yb - yt + 1);
  }
}

// -------------------------------------------------------------- blush
// Cheek hatching, parked in whatever gap the layout leaves between the
// bottom of the eye and the top of the mouth.
void Face::drawBlush() {
  const int16_t eyeBot = eyeCY + baseH / 2;
  const int16_t lo = eyeBot + 4, hi = faceY1 - 4;
  int16_t y = (features & F_MOUTH) ? (eyeBot + mouthCY) / 2 : eyeBot + 5;
  y = clampi(y, lo < hi ? lo : hi, hi);
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
// The floating effects live in their own strip - fxY0 .. fxY0+fxH-1 - so
// that on a two-colour panel they can be handed the 16 yellow rows while
// the face keeps the blue ones.  Every offset below is written as a
// fraction of fxH, and fxH is 32 on a mono panel, which is the geometry
// these were originally hand-placed against.  The two effects anchored to
// the face itself, tears and the sweat drop, follow the face instead.
void Face::drawFx() {
  const int16_t fy1 = fxY0 + fxH - 1;

  switch (cur.fx) {

    case FX_ZZZ:
      for (uint8_t k = 0; k < 3; k++) {
        uint8_t ph = phase(2000, k * 87);
        int16_t y  = fxY0 + (fxH * 13 / 16) - ((int16_t)ph * (fxH * 11 / 16) >> 8);
        int16_t x  = 100 + (isin(ph >> 2) >> 5) + k * 5;
        int16_t s  = 4 + k * 2;
        if (y > fxY0 + 1 && y + s <= fy1) drawZ(x, y, s);
      }
      break;

    case FX_QUESTION: {
      int16_t y = fxY0 + 2 + (isin(phase(900, 0) >> 2) >> 6);
      drawQmark(108, y);
      drawQmark(99, clampi(y + fxH / 4, fxY0, (int16_t)(fy1 - 8)));
    } break;

    case FX_SWEAT:                       // hangs beside the brow: face-anchored
      drawDrop(114, clampi(faceY0 + 12 + ((int16_t)phase(1200, 0) * 28 >> 8),
                           faceY0 + 4, faceY1 - 1), 3);
      break;

    case FX_TEAR:                        // runs down the cheek: face-anchored
      for (uint8_t k = 0; k < 2; k++) {
        int16_t y = eyeCY + baseH / 2 + ((int16_t)phase(1700, k * 128) * 20 >> 8);
        if (y < faceY1 - 1) drawDrop(eyeCX[k] + (k ? 8 : -8), y, 2);
      }
      break;

    case FX_SPARKLE:
      for (uint8_t k = 0; k < 3; k++) {
        uint8_t ph = phase(900, k * 95);
        uint8_t a  = (ph < 128) ? ph : (uint8_t)(255 - ph);
        int16_t y  = fxY0 + (int16_t)pgm_read_byte(&SPY[k]) * fxH / 32 + 1;
        int16_t r  = (int16_t)a * 11 >> 7;
        int16_t rm = y - fxY0 < fy1 - y ? y - fxY0 : fy1 - y;   // fit the strip
        if (r > rm) r = rm;
        if (r > 0) drawStar((int8_t)pgm_read_byte(&SPX[k]), y, r);
      }
      break;

    case FX_STEAM:
      for (uint8_t k = 0; k < 2; k++) {
        uint8_t ph = phase(1100, k * 128);
        int16_t y  = fxY0 + fxH / 2 - 1 - ((int16_t)ph * (fxH * 13 / 32) >> 8);
        int16_t r  = 2 + ((int16_t)ph * 3 >> 8);
        if (y - r >= fxY0) { drawRing(7 + k * 3, y, r); drawRing(121 - k * 3, y, r); }
      }
      break;

    case FX_HEART:
      for (uint8_t k = 0; k < 2; k++) {
        uint8_t ph = phase(1800, k * 128);
        int16_t y  = fy1 - 1 - ((int16_t)ph * (fxH * 26 / 32) >> 8);
        int16_t x  = 112 + (isin(ph >> 2) >> 5);
        int16_t s  = 5 + ((int16_t)ph * 4 >> 8);
        if (y - s / 2 > fxY0) drawHeart(x, y, s, s);
      }
      break;

    default: break;
  }
}

// ===================================================================
//  Boot animation - CRT power-on, then the eyes wake up and say hi.
// ===================================================================
void Face::boot() {
  const int16_t midY = (faceY0 + faceY1) / 2;      // centre of the face region
  for (int16_t w = 2; w <= 104; w += 8) {          // hairline stretches out
    u.first();
    do { u.drawBox(64 - w / 2, midY, w, 2); } while (u.next());
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
        int16_t cy = midY + (int16_t)((eyeCY - midY) * p);
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
