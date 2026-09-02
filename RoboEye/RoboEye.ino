/* =====================================================================
   RoboEye - an expressive robotic face for Arduino Pro 5V (ATmega328P)
             + SSD1306 0.96" 128x64 OLED (I2C or 4-wire hardware SPI)

   22 emotions, spring-physics motion, autonomous blinks / saccades /
   breathing, and a serial console to drive everything live.

   No external libraries: the SSD1306 driver is in ssd1306.cpp.
   Wiring + full command list: see README.md
   ===================================================================== */

#include <string.h>
#include "config.h"
#include "ssd1306.h"
#include "face.h"
#if ENABLE_EEPROM
  #include <EEPROM.h>
#endif

// ------------------------------------------------------------ display
Oled oled;

Face face(oled);

// ------------------------------------------------------------- timing
static uint32_t tPrev;
static bool     frozen;          // diagnostic: stop advancing the animation
static uint16_t frames;
static uint32_t tFps;
static uint16_t fps;

// ------------------------------------------------------- serial input
static char    cmd[32];
static uint8_t cmdLen;

// =====================================================================
//  EEPROM
// =====================================================================
#if ENABLE_EEPROM
#define EE_MAGIC 0xE7
static void saveCfg() {
  EEPROM.update(0, EE_MAGIC);
  EEPROM.update(1, face.featureMask());
  EEPROM.update(2, face.styleId());
  EEPROM.update(3, face.emotion());
  EEPROM.update(4, face.autoOn() ? 1 : 0);
  EEPROM.update(5, face.speed());
  EEPROM.update(6, face.facePreset());
}
static bool loadCfg() {
  if (EEPROM.read(0) != EE_MAGIC) return false;
  face.setFeatures(EEPROM.read(1));
  face.setStyle(EEPROM.read(2));
  face.setEmotion(EEPROM.read(3), true);
  face.setAuto(EEPROM.read(4));
  face.setSpeed(EEPROM.read(5));
  return true;
}
#endif

// =====================================================================
//  helpers
// =====================================================================
int freeRam();

static int8_t emoByName(const char *s) {
  for (uint8_t i = 0; i < NUM_EMO; i++) {
    PGM_P p = (PGM_P)pgm_read_word(&EMO_NAMES[i]);
    if (strcasecmp_P(s, p) == 0) return (int8_t)i;
  }
  return -1;
}

static void printEmoName(uint8_t i) {
  PGM_P p = (PGM_P)pgm_read_word(&EMO_NAMES[i]);
  char b[12];
  strncpy_P(b, p, sizeof(b) - 1);
  b[sizeof(b) - 1] = 0;
  Serial.print(b);
}

static void listEmotions() {
  for (uint8_t i = 0; i < NUM_EMO; i++) {
    if (i < 10) Serial.print(' ');
    Serial.print(i);
    Serial.print(F(" "));
    printEmoName(i);
    Serial.println();
  }
}

static void showHelp() {
  Serial.println(F("\n-- RoboEye console --"));
  Serial.println(F("e <n|name>   set emotion      list        list emotions"));
  Serial.println(F("next / prev  cycle emotions   demo        play all 22"));
  Serial.println(F("auto on|off  autonomous mood  info        status + fps"));
  Serial.println(F("face 0..4    eyes/brows/mouth/full/full+nose"));
  Serial.println(F("style robot|pupil"));
  Serial.println(F("mouth|brows|nose|blush|glint|scan|tilt|fx  on|off"));
  Serial.println(F("look <x> <y> gaze -100..100    blink       blink now"));
  Serial.println(F("wink l|r     one eye          talk <ms>   mouth chatter"));
  Serial.println(F("speed <pct>  20..250"));
#if ENABLE_EEPROM
  Serial.println(F("save / load  persist settings"));
#endif
}

static void printInfo() {
  printEmoName(face.emotion());
  Serial.print(F(" face=")); Serial.print(face.facePreset());
  Serial.print(F(" style=")); Serial.print(face.styleId());
  Serial.print(F(" auto=")); Serial.print(face.autoOn());
  Serial.print(F(" spd=")); Serial.print(face.speed());
  Serial.print(F(" mask=0x")); Serial.print(face.featureMask(), HEX);
  Serial.print(F(" fps=")); Serial.print(fps);
  Serial.print(F(" ram=")); Serial.println(freeRam());
}

int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// Plain geometry, no face code: solid fill, then a frame with diagonals,
// then a checkerboard.  Blank screen here == wiring or controller.
static void selfTest() {
  for (uint8_t k = 0; k < 3; k++) {
    oled.first();
    do {
      if (k == 0) {
        oled.drawBox(0, 0, 128, 64);
      } else if (k == 1) {
        oled.drawHLine(0, 0, 128);  oled.drawHLine(0, 63, 128);
        oled.drawVLine(0, 0, 64);   oled.drawVLine(127, 0, 64);
        oled.drawLine(0, 0, 127, 63);
        oled.drawLine(0, 63, 127, 0);
      } else {
        for (uint8_t y = 0; y < 64; y += 2)
          for (uint8_t x = (y >> 1) & 1; x < 128; x += 2) oled.drawPixel(x, y);
      }
    } while (oled.next());
    delay(900);
  }
}

// returns true when the word matched a feature name
static bool featureCmd(const char *w, bool on) {
  if      (!strcasecmp(w, "mouth")) face.feature(F_MOUTH, on);
  else if (!strcasecmp(w, "brows")) face.feature(F_BROWS, on);
  else if (!strcasecmp(w, "nose"))  face.feature(F_NOSE,  on);
  else if (!strcasecmp(w, "blush")) face.feature(F_BLUSH, on);
  else if (!strcasecmp(w, "glint")) face.feature(F_GLINT, on);
  else if (!strcasecmp(w, "scan"))  face.feature(F_SCAN,  on);
  else if (!strcasecmp(w, "tilt"))  face.feature(F_TILT,  on);
  else if (!strcasecmp(w, "fx"))    face.feature(F_FX,    on);
  else return false;
  return true;
}

// =====================================================================
//  serial console
// =====================================================================
static void runCommand(char *line) {
  char *w = strtok(line, " \t");
  if (!w) return;
  char *a1 = strtok(NULL, " \t");
  char *a2 = strtok(NULL, " \t");

  if (!strcasecmp(w, "help") || !strcasecmp(w, "?")) { showHelp(); return; }
  if (!strcasecmp(w, "list")) { listEmotions(); return; }
  if (!strcasecmp(w, "info")) { printInfo(); return; }
  if (!strcasecmp(w, "demo")) { face.startDemo(); Serial.println(F("demo")); return; }
  if (!strcasecmp(w, "blink")) { face.doBlink(0); return; }
  if (!strcasecmp(w, "test")) { selfTest(); return; }
  if (!strcasecmp(w, "contrast") && a1) {
    oled.setContrast(constrain(atoi(a1), 0, 255));
    Serial.println(F("ok"));
    return;
  }
  if (!strcasecmp(w, "freeze") && a1) {
    frozen = !strcasecmp(a1, "on");
    Serial.println(frozen ? F("frozen - every frame is now identical")
                          : F("running"));
    return;
  }

  if (!strcasecmp(w, "e") && a1) {
    int8_t id = (a1[0] >= '0' && a1[0] <= '9') ? (int8_t)atoi(a1) : emoByName(a1);
    if (id < 0 || id >= NUM_EMO) { Serial.println(F("? unknown emotion")); return; }
    face.setAuto(false);
    face.setEmotion(id);
    Serial.print(F("-> ")); printEmoName(id); Serial.println();
    return;
  }
  if (!strcasecmp(w, "next") || !strcasecmp(w, "prev")) {
    uint8_t id = face.emotion();
    id = (w[0] == 'n' || w[0] == 'N') ? (id + 1) % NUM_EMO
                                      : (id + NUM_EMO - 1) % NUM_EMO;
    face.setAuto(false);
    face.setEmotion(id);
    Serial.print(F("-> ")); printEmoName(id); Serial.println();
    return;
  }
  if (!strcasecmp(w, "auto") && a1) {
    face.setAuto(!strcasecmp(a1, "on"));
    Serial.println(face.autoOn() ? F("auto on") : F("auto off"));
    return;
  }
  if (!strcasecmp(w, "face") && a1) {
    face.setFacePreset(atoi(a1));
    Serial.print(F("face ")); Serial.println(face.facePreset());
    return;
  }
  if (!strcasecmp(w, "style") && a1) {
    face.setStyle(!strcasecmp(a1, "pupil") ? STYLE_PUPIL : STYLE_ROBOT);
    Serial.println(F("ok"));
    return;
  }
  if (!strcasecmp(w, "look") && a1 && a2) {
    face.setAuto(false);
    face.look(constrain(atoi(a1), -100, 100), constrain(atoi(a2), -100, 100), 4000);
    return;
  }
  if (!strcasecmp(w, "wink")) {
    face.doBlink((a1 && (a1[0] == 'l' || a1[0] == 'L')) ? 1 : 2);
    return;
  }
  if (!strcasecmp(w, "talk")) { face.talk(a1 ? atoi(a1) : 1500); return; }
  if (!strcasecmp(w, "speed") && a1) {
    face.setSpeed(atoi(a1));
    Serial.print(F("speed ")); Serial.println(face.speed());
    return;
  }
#if ENABLE_EEPROM
  if (!strcasecmp(w, "save")) { saveCfg(); Serial.println(F("saved")); return; }
  if (!strcasecmp(w, "load")) {
    Serial.println(loadCfg() ? F("loaded") : F("nothing stored"));
    return;
  }
#endif
  if (a1 && featureCmd(w, !strcasecmp(a1, "on"))) { Serial.println(F("ok")); return; }

  Serial.println(F("? type help"));
}

static void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      cmd[cmdLen] = 0;
      if (cmdLen) runCommand(cmd);
      cmdLen = 0;
    } else if (cmdLen < sizeof(cmd) - 1) {
      cmd[cmdLen++] = c;
    }
  }
}

// =====================================================================
//  optional hardware inputs
// =====================================================================
#if ENABLE_BUTTONS
static uint32_t btnLock;
static void pollButtons() {
  if (millis() < btnLock) return;
  if (!digitalRead(BTN_EMOTION)) {
    face.setAuto(false);
    face.setEmotion((face.emotion() + 1) % NUM_EMO);
    btnLock = millis() + 220;
  } else if (!digitalRead(BTN_FACE)) {
    face.setFacePreset(face.facePreset() + 1);
    btnLock = millis() + 220;
  } else if (!digitalRead(BTN_AUTO)) {
    face.setAuto(!face.autoOn());
    btnLock = millis() + 220;
  }
}
#endif

#if ENABLE_JOYSTICK
static void pollJoystick() {
  int x = analogRead(JOY_X) - 512;
  int y = analogRead(JOY_Y) - 512;
  if (abs(x) > JOY_DEADZONE || abs(y) > JOY_DEADZONE) {
    face.look(constrain(x / 5, -100, 100), constrain(y / 5, -100, 100), 700);
  }
}
#endif

#if ENABLE_SONAR
static uint32_t tSonar;
static uint8_t  sonarMood;
static void pollSonar() {
  if (millis() < tSonar) return;
  tSonar = millis() + 120;
  digitalWrite(SONAR_TRIG, LOW);  delayMicroseconds(3);
  digitalWrite(SONAR_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(SONAR_TRIG, LOW);
  unsigned long d = pulseIn(SONAR_ECHO, HIGH, 20000UL);
  if (!d) return;
  uint16_t cm = d / 58;
  uint8_t want = (cm < 15) ? EMO_SURPRISED : (cm < 45 ? EMO_FOCUSED : EMO_NEUTRAL);
  if (want != sonarMood) {
    sonarMood = want;
    face.setAuto(false);
    face.setEmotion(want);
  }
}
#endif

#if ENABLE_LDR
static uint32_t tLdr;
static bool sleeping;
static void pollLdr() {
  if (millis() < tLdr) return;
  tLdr = millis() + 500;
  bool dark = analogRead(LDR_PIN) < LDR_DARK;
  if (dark != sleeping) {
    sleeping = dark;
    face.setAuto(!dark);
    face.setEmotion(dark ? EMO_SLEEPY : EMO_SURPRISED);
  }
}
#endif

// =====================================================================
void setup() {
  Serial.begin(115200);

  oled.begin();
  oled.setContrast(OLED_CONTRAST);

#if USE_HW_SPI
  Serial.print(F("bus=SPI sck=13 mosi=11 cs=")); Serial.print(OLED_CS);
  Serial.print(F(" dc=")); Serial.print(OLED_DC);
  Serial.print(F(" rst=")); Serial.println(OLED_RST);
#else
  Serial.println(F("bus=I2C sda=A4 scl=A5"));
#endif
#if STARTUP_SELFTEST
  selfTest();
#endif

  randomSeed(analogRead(A3) ^ (uint32_t)micros());

#if ENABLE_BUTTONS
  pinMode(BTN_EMOTION, INPUT_PULLUP);
  pinMode(BTN_FACE,    INPUT_PULLUP);
  pinMode(BTN_AUTO,    INPUT_PULLUP);
#endif
#if ENABLE_SONAR
  pinMode(SONAR_TRIG, OUTPUT);
  pinMode(SONAR_ECHO, INPUT);
#endif

  face.begin();
#if ENABLE_EEPROM
  loadCfg();
#endif
#if BOOT_ANIMATION
  face.boot();
#endif

  showHelp();
  tPrev = micros();
  tFps  = millis();
}

void loop() {
  pollSerial();
#if ENABLE_BUTTONS
  pollButtons();
#endif
#if ENABLE_JOYSTICK
  pollJoystick();
#endif
#if ENABLE_SONAR
  pollSonar();
#endif
#if ENABLE_LDR
  pollLdr();
#endif

  // Hold to TARGET_FPS.  Redrawing faster than the panel scans itself out
  // buys nothing the eye can see and costs torn frames.
  uint32_t now = micros();
  if ((uint32_t)(now - tPrev) < (1000000UL / TARGET_FPS)) return;

  float dt = (now - tPrev) * 1e-6f;
  tPrev = now;
  if (dt > 0.08f) dt = 0.08f;      // never let a stall fast-forward the face

  if (!frozen) face.update(dt);
  face.render();

  frames++;
  if (millis() - tFps >= 1000) { fps = frames; frames = 0; tFps += 1000; }

}
