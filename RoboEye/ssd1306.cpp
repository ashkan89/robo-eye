#include "ssd1306.h"

#if USE_HW_SPI
  #include <SPI.h>
  static SPISettings spiCfg(SPI_CLOCK, MSBFIRST, SPI_MODE0);
#else
  #include <Wire.h>
  #define OLED_I2C_ADDR 0x3C
#endif

// ---------------------------------------------------------------------
//  Init sequence, 128x64 with the internal charge pump.  Byte for byte
//  the Adafruit_SSD1306 sequence for this panel.
// ---------------------------------------------------------------------
static const uint8_t INIT_SEQ[] PROGMEM = {
  0xAE,             // display off
  0xD5, 0x80,       // clock divide = 1, osc freq = 8
  0xA8, 0x3F,       // multiplex ratio = 63 (height - 1)
  0xD3, 0x00,       // display offset = 0
  0x40,             // start line = 0
  0x8D, 0x14,       // charge pump on (internal VCC)
  0x20, 0x00,       // memory addressing mode = horizontal
  0xA1,             // segment remap
  0xC8,             // COM scan direction reversed
  0xDA, 0x12,       // COM pin configuration
  0x81, 0xCF,       // contrast
  0xD9, 0xF1,       // pre-charge period
  0xDB, 0x40,       // VCOMH deselect level
  0xA4,             // resume from RAM
  0xA6,             // non-inverted
  0x2E,             // deactivate scroll
  0xAF              // display on
};

// ---------------------------------------------------------------- bus
void Oled::command(uint8_t c) {
#if USE_HW_SPI
  SPI.beginTransaction(spiCfg);
  digitalWrite(OLED_DC, LOW);
  digitalWrite(OLED_CS, LOW);
  SPI.transfer(c);
  digitalWrite(OLED_CS, HIGH);
  SPI.endTransaction();
#else
  Wire.beginTransmission(OLED_I2C_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write(c);
  Wire.endTransmission();
#endif
}

void Oled::commandList(const uint8_t *p, uint8_t n) {
#if USE_HW_SPI
  SPI.beginTransaction(spiCfg);
  digitalWrite(OLED_DC, LOW);
  digitalWrite(OLED_CS, LOW);
  while (n--) SPI.transfer(pgm_read_byte(p++));
  digitalWrite(OLED_CS, HIGH);
  SPI.endTransaction();
#else
  Wire.beginTransmission(OLED_I2C_ADDR);
  Wire.write((uint8_t)0x00);
  uint8_t sent = 0;
  while (n--) {
    if (sent == 16) {                      // stay inside the Wire buffer
      Wire.endTransmission();
      Wire.beginTransmission(OLED_I2C_ADDR);
      Wire.write((uint8_t)0x00);
      sent = 0;
    }
    Wire.write(pgm_read_byte(p++));
    sent++;
  }
  Wire.endTransmission();
#endif
}

void Oled::begin() {
#if USE_HW_SPI
  pinMode(OLED_DC, OUTPUT);
  pinMode(OLED_CS, OUTPUT);
  digitalWrite(OLED_CS, HIGH);
  SPI.begin();
  #if OLED_RST >= 0
    pinMode(OLED_RST, OUTPUT);             // hardware reset, per datasheet
    digitalWrite(OLED_RST, HIGH); delay(1);
    digitalWrite(OLED_RST, LOW);  delay(10);
    digitalWrite(OLED_RST, HIGH); delay(10);
  #endif
#else
  Wire.begin();
  Wire.setClock(I2C_CLOCK);
#endif
  commandList(INIT_SEQ, sizeof(INIT_SEQ));
  first();
  while (next()) { }                         // blank every band
}

void Oled::setContrast(uint8_t c) {
  command(0x81);
  command(c);
}

// Ship the current band.  The two addressing commands are re-sent every
// time, which is what makes the panel impossible to leave desynchronised.
void Oled::sendBand() {
  const uint8_t p0 = band * BAND_PAGES;
  const uint8_t p1 = p0 + BAND_PAGES - 1;

  const uint8_t seq[6] = { 0x22, p0, p1, 0x21, 0x00, OLED_W - 1 };
#if USE_HW_SPI
  SPI.beginTransaction(spiCfg);
  digitalWrite(OLED_DC, LOW);
  digitalWrite(OLED_CS, LOW);
  for (uint8_t i = 0; i < 6; i++) SPI.transfer(seq[i]);
  digitalWrite(OLED_CS, HIGH);
  SPI.endTransaction();

  SPI.beginTransaction(spiCfg);
  digitalWrite(OLED_DC, HIGH);
  digitalWrite(OLED_CS, LOW);
  for (uint16_t i = 0; i < sizeof(buf); i++) SPI.transfer(buf[i]);
  digitalWrite(OLED_CS, HIGH);
  SPI.endTransaction();
#else
  Wire.beginTransmission(OLED_I2C_ADDR);
  Wire.write((uint8_t)0x00);
  for (uint8_t i = 0; i < 6; i++) Wire.write(seq[i]);
  Wire.endTransmission();

  uint16_t i = 0;
  while (i < sizeof(buf)) {
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write((uint8_t)0x40);
    for (uint8_t k = 0; k < 16 && i < sizeof(buf); k++) Wire.write(buf[i++]);
    Wire.endTransmission();
  }
#endif
}

void Oled::clearBuffer() { memset(buf, 0, sizeof(buf)); }

void Oled::first() {
  band   = 0;
  bandY0 = 0;
  clearBuffer();
}

bool Oled::next() {
  sendBand();
  band++;
  if (band >= OLED_BANDS) { band = 0; bandY0 = 0; return false; }
  bandY0 = (int16_t)band * BAND_ROWS;
  clearBuffer();
  return true;
}

// ------------------------------------------------------------ clipping
void Oled::setClipWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  x1--; y1--;                                 // callers pass exclusive edges
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > OLED_W - 1) x1 = OLED_W - 1;
  if (y1 > OLED_H - 1) y1 = OLED_H - 1;
  cx0 = x0; cy0 = y0; cx1 = x1; cy1 = y1;
}

void Oled::setMaxClipWindow() {
  cx0 = 0; cy0 = 0; cx1 = OLED_W - 1; cy1 = OLED_H - 1;
}

// ---------------------------------------------------------- primitives
void Oled::drawPixel(int16_t x, int16_t y) {
  if (x < cx0 || x > cx1 || y < cy0 || y > cy1) return;
  y -= bandY0;
  if (y < 0 || y >= BAND_ROWS) return;
  uint8_t *p = &buf[x + ((uint16_t)(y >> 3) << 7)];
  uint8_t  m = 1 << (y & 7);
  if      (color == 1) *p |= m;
  else if (color == 0) *p &= (uint8_t)~m;
  else                 *p ^= m;
}

// The workhorse: a vertical run, filled a byte at a time.  Nearly every
// shape in the face reduces to columns, so this is the hot path.
void Oled::drawVLine(int16_t x, int16_t y, int16_t h) {
  if (x < cx0 || x > cx1 || h <= 0) return;
  int16_t yEnd = y + h - 1;
  if (y < cy0)    y = cy0;
  if (yEnd > cy1) yEnd = cy1;

  y    -= bandY0;                             // into buffer coordinates
  yEnd -= bandY0;
  if (y < 0) y = 0;
  if (yEnd > BAND_ROWS - 1) yEnd = BAND_ROWS - 1;
  if (y > yEnd) return;

  int16_t n = yEnd - y + 1;
  while (n > 0) {
    uint8_t bit   = y & 7;
    uint8_t avail = 8 - bit;
    uint8_t cnt   = (n < (int16_t)avail) ? (uint8_t)n : avail;
    uint8_t mask  = (uint8_t)(((cnt >= 8) ? 0xFF : ((1 << cnt) - 1)) << bit);
    uint8_t *p    = &buf[x + ((uint16_t)(y >> 3) << 7)];
    if      (color == 1) *p |= mask;
    else if (color == 0) *p &= (uint8_t)~mask;
    else                 *p ^= mask;
    y += cnt;
    n -= cnt;
  }
}

void Oled::drawHLine(int16_t x, int16_t y, int16_t w) {
  if (y < cy0 || y > cy1 || w <= 0) return;
  int16_t by = y - bandY0;
  if (by < 0 || by >= BAND_ROWS) return;
  int16_t xEnd = x + w - 1;
  if (x < cx0)    x = cx0;
  if (xEnd > cx1) xEnd = cx1;
  if (x > xEnd) return;

  uint8_t  m = 1 << (by & 7);
  uint8_t *p = &buf[x + ((uint16_t)(by >> 3) << 7)];
  for (int16_t i = x; i <= xEnd; i++, p++) {
    if      (color == 1) *p |= m;
    else if (color == 0) *p &= (uint8_t)~m;
    else                 *p ^= m;
  }
}

void Oled::drawBox(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t i = 0; i < w; i++) drawVLine(x + i, y, h);
}

static uint8_t isqrt16(uint16_t v) {
  uint8_t r = 0;
  while (((uint16_t)(r + 1) * (r + 1)) <= v) r++;
  return r;
}

// Rounded box drawn as columns: outside the corner zones the column is the
// full height, inside them it is inset by the circle equation.  Exact, and
// it costs one integer square root per corner column.
void Oled::drawRBox(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
  int16_t rmax = ((w < h ? w : h) - 1) / 2;
  if (r > rmax) r = rmax;
  if (r < 1) { drawBox(x, y, w, h); return; }

  const uint16_t rr = (uint16_t)r * r;
  for (int16_t i = 0; i < w; i++) {
    int16_t d = 0;                                  // horizontal distance
    if (i < r)              d = r - i;              // into a left corner
    else if (i > w - 1 - r) d = i - (w - 1 - r);    // into a right corner
    int16_t inset = 0;
    if (d) inset = r - (int16_t)isqrt16(rr - (uint16_t)(d * d));
    drawVLine(x + i, y + inset, h - 2 * inset);
  }
}

void Oled::drawDisc(int16_t cx, int16_t cy, int16_t r) {
  if (r < 1) { drawPixel(cx, cy); return; }
  const uint16_t rr = (uint16_t)r * r;
  for (int16_t dx = -r; dx <= r; dx++) {
    int16_t half = (int16_t)isqrt16(rr - (uint16_t)(dx * dx));
    drawVLine(cx + dx, cy - half, 2 * half + 1);
  }
}

void Oled::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  int16_t dx = x1 - x0, dy = y1 - y0;
  int16_t sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  int16_t err = dx - dy;
  for (;;) {
    drawPixel(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = err << 1;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
}
