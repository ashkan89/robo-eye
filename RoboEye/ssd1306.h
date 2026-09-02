#pragma once
#include <Arduino.h>
#include "config.h"

// =====================================================================
//  Minimal SSD1306 driver + column-oriented graphics.
//
//  The command sequences here follow Adafruit_SSD1306, the reference
//  implementation for this controller.  The part that matters is how the
//  pixels get addressed: before every write this driver re-sends the page
//  address range (0x22) and column address range (0x21).  Those are the
//  addressing commands the datasheet defines for horizontal addressing
//  mode, so the controller's write pointer is re-established from scratch
//  each time and a disturbed byte can never leave the panel permanently
//  out of step.
//
//  (For contrast: u8g2's SSD1306 path selects horizontal addressing mode
//  and then addresses with 0x00-0x0F / 0x10-0x1F / 0xB0-0xB7, which are
//  page-addressing-mode commands.  The controller ignores them in
//  horizontal mode and merely auto-advances its own pointer, so once that
//  pointer slips there is nothing to pull it back.)
//
//  The buffer is split into OLED_BANDS horizontal bands to keep RAM down
//  on a 328P; each band is drawn and shipped in turn, with its own
//  addressing commands.
// =====================================================================

#define OLED_W      128
#define OLED_H       64
#define OLED_PAGES  (OLED_H / 8)
#define BAND_PAGES  (OLED_PAGES / OLED_BANDS)
#define BAND_ROWS   (BAND_PAGES * 8)

class Oled {
public:
  void begin();
  void setContrast(uint8_t c);

  // Render loop:  first();  do { ...draw... } while (next());
  void first();
  bool next();

  // draw colour: 1 = set, 0 = clear, 2 = xor
  void setDrawColor(uint8_t c) { color = c; }

  // Clip window, x1/y1 exclusive.
  void setClipWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
  void setMaxClipWindow();

  void drawPixel(int16_t x, int16_t y);
  void drawVLine(int16_t x, int16_t y, int16_t h);
  void drawHLine(int16_t x, int16_t y, int16_t w);
  void drawBox(int16_t x, int16_t y, int16_t w, int16_t h);
  void drawRBox(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r);
  void drawDisc(int16_t cx, int16_t cy, int16_t r);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

private:
  uint8_t buf[OLED_W * BAND_PAGES];
  uint8_t color = 1;
  uint8_t band  = 0;                 // index of the band being drawn
  int16_t bandY0 = 0;                // first screen row held in buf
  int16_t cx0 = 0, cy0 = 0, cx1 = OLED_W - 1, cy1 = OLED_H - 1;  // inclusive

  void clearBuffer();
  void command(uint8_t c);
  void commandList(const uint8_t *p, uint8_t n);   // from PROGMEM
  void sendBand();
};
