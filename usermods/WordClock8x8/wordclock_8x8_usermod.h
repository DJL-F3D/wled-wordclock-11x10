#pragma once
#include "wled.h"

/*
 * WLED Word Clock Usermod — 8×8 LED matrix
 * ==========================================
 * Overlays the current time as words on whatever WLED effect is running.
 * Uses the same handleOverlayDraw() approach as the 11×10 usermod.
 *
 * Hardware
 * --------
 *   64 LEDs total, no minute indicator dots.
 *   Serpentine wiring starting at BOTTOM-RIGHT.
 *     Physical row 0 (bottom): right → left
 *     Physical row 1:          left  → right
 *     … alternating upward …
 *     Physical row 7 (top):    left  → right
 *
 *   LED 0  = bottom-right corner
 *   LED 63 = top-left corner (first LED of top row: "IT IS")
 *   Wait — let me be precise:
 *     Physical row 0 (bottom) is even → runs right-to-left
 *     LED 0 = phys row 0, col 7 (bottom-right)
 *     LED 7 = phys row 0, col 0 (bottom-left)
 *     Physical row 1 is odd → runs left-to-right
 *     LED 8 = phys row 1, col 0
 *     …
 *     Physical row 7 (top) is odd → runs left-to-right
 *     LED 56 = phys row 7, col 0
 *     LED 63 = phys row 7, col 7
 *
 * Word layout (display row 0 = top, col 0 = left)
 * ------------------------------------------------
 *   Row 0 : [2] IT IS  [4] HALF    [2] TEN(min)
 *   Row 1 : [4] QUARTER            [4] TWENTY
 *   Row 2 : [2] FIVE(min) [4] MINUTES [2] TO
 *   Row 3 : [3] PAST    [2] ONE    [3] THREE
 *   Row 4 : [3] FOUR    [2] TWO    [3] FIVE(hr)
 *   Row 5 : [3] SEVEN   [2] SIX    [3] EIGHT
 *   Row 6 : [2] NINE    [4] ELEVEN [2] TEN(hr)
 *   Row 7 : [4] TWELVE             [4] O'CLOCK
 *
 * Numbers in brackets are the number of LEDs representing each word.
 * Each group of LEDs lights up as a solid indicator for that word —
 * this is a segmented-style display, not individual letters.
 *
 * Config key: "wc8" (distinct from 11×10 "wc" so both can coexist in HA)
 */

struct WC8_Seg { uint8_t row, col, len; };

// clang-format off
//                              display-row  col  len
static constexpr WC8_Seg WC8_ITIS    = {0, 0, 2}; // IT IS
static constexpr WC8_Seg WC8_HALF    = {0, 2, 4}; // HALF
static constexpr WC8_Seg WC8_TEN_MIN = {0, 6, 2}; // TEN (minutes)
static constexpr WC8_Seg WC8_QUARTER = {1, 0, 4}; // QUARTER
static constexpr WC8_Seg WC8_TWENTY  = {1, 4, 4}; // TWENTY
static constexpr WC8_Seg WC8_FIVE_MIN= {2, 0, 2}; // FIVE (minutes)
static constexpr WC8_Seg WC8_MINUTES = {2, 2, 4}; // MINUTES
static constexpr WC8_Seg WC8_TO      = {2, 6, 2}; // TO
static constexpr WC8_Seg WC8_PAST    = {3, 0, 3}; // PAST
static constexpr WC8_Seg WC8_ONE     = {3, 3, 2}; // ONE
static constexpr WC8_Seg WC8_THREE   = {3, 5, 3}; // THREE
static constexpr WC8_Seg WC8_FOUR    = {4, 0, 3}; // FOUR
static constexpr WC8_Seg WC8_TWO     = {4, 3, 2}; // TWO
static constexpr WC8_Seg WC8_FIVE_HR = {4, 5, 3}; // FIVE (hours)
static constexpr WC8_Seg WC8_SEVEN   = {5, 0, 3}; // SEVEN
static constexpr WC8_Seg WC8_SIX     = {5, 3, 2}; // SIX
static constexpr WC8_Seg WC8_EIGHT   = {5, 5, 3}; // EIGHT
static constexpr WC8_Seg WC8_NINE    = {6, 0, 2}; // NINE
static constexpr WC8_Seg WC8_ELEVEN  = {6, 2, 4}; // ELEVEN
static constexpr WC8_Seg WC8_TEN_HR  = {6, 6, 2}; // TEN (hours)
static constexpr WC8_Seg WC8_TWELVE  = {7, 0, 4}; // TWELVE
static constexpr WC8_Seg WC8_OCLOCK  = {7, 4, 4}; // O'CLOCK
// clang-format on

class WordClock8x8Usermod : public Usermod {
public:
  static const char _name[];

private:
  static const int COLS       = 8;
  static const int ROWS       = 8;
  static const int TOTAL_LEDS = ROWS * COLS; // 64

  // ── Configurable settings ──────────────────────────────────────────────────
  bool     enabled         = true;
  uint8_t  wordBrightness  = 255;
  uint8_t  bgBrightness    = 40;
  bool     randomWordColor = false;
  uint16_t transitionMs    = 800;
  uint8_t  wordR = 255, wordG = 200, wordB = 100;

  // ── Runtime state ─────────────────────────────────────────────────────────
  uint8_t  lastMinute = 255;
  bool     firstRun   = true;

  // Per-LED word colour (0 = background, non-zero = word colour for that pixel)
  uint32_t wordPixels[TOTAL_LEDS];
  uint32_t prevWordPixels[TOTAL_LEDS];

  bool     fadingIn  = false;
  uint32_t fadeStart = 0;

  // ── LED mapping ────────────────────────────────────────────────────────────
  //
  // displayRow: 0 = top (ITIS row), 7 = bottom (TWELVE row)
  // col:        0 = left, 7 = right
  //
  // physRow = 7 - displayRow  (0 = bottom, 7 = top)
  // even physRow → right-to-left:  LED = physRow*8 + (7 - col)
  // odd  physRow → left-to-right:  LED = physRow*8 + col
  //
  inline int matrixLed8x8(int displayRow, int col) const {
    int physRow = 7 - displayRow;
    return (physRow % 2 == 0)
      ? physRow * 8 + (7 - col)   // right-to-left
      : physRow * 8 + col;        // left-to-right
  }

  static inline uint32_t packRGB(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }

  static inline uint32_t scaleBri(uint32_t c, uint8_t bri) {
    if (bri == 255) return c;
    if (bri == 0)   return 0;
    return packRGB(
      ((c >> 16) & 0xFF) * bri / 255,
      ((c >>  8) & 0xFF) * bri / 255,
      ( c        & 0xFF) * bri / 255
    ) | ((uint32_t)(((c >> 24) & 0xFF) * bri / 255) << 24);
  }

  static uint32_t vibrantRandom() {
    uint8_t r = random8(), g = random8(), b = random8();
    uint8_t mx = max(r, max(g, b));
    if (mx > 0) { r=(uint16_t)r*255/mx; g=(uint16_t)g*255/mx; b=(uint16_t)b*255/mx; }
    return packRGB(r, g, b);
  }

  // Each call returns a new random colour when randomWordColor=true,
  // so each word segment independently gets its own hue per minute.
  uint32_t nextWordColor() {
    return randomWordColor ? vibrantRandom() : packRGB(wordR, wordG, wordB);
  }

  void lightSeg(uint32_t* mask, const WC8_Seg& seg, uint32_t color) {
    for (int i = 0; i < seg.len; i++) {
      int idx = matrixLed8x8(seg.row, seg.col + i);
      if (idx >= 0 && idx < TOTAL_LEDS) mask[idx] = color;
    }
  }

  void buildWordMask(uint32_t* mask, int hour, int minute) {
    memset(mask, 0, sizeof(uint32_t) * TOTAL_LEDS);

    int block = minute / 5;  // 0-11, no sub-5-min dots on 8×8

    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    if (block >= 7) h12 = (h12 % 12) + 1;

    // IT IS — always lit
    lightSeg(mask, WC8_ITIS, nextWordColor());

    // Minute phrase
    switch (block) {
      case  1: lightSeg(mask, WC8_FIVE_MIN, nextWordColor());
               lightSeg(mask, WC8_MINUTES,  nextWordColor()); break;
      case  2: lightSeg(mask, WC8_TEN_MIN,  nextWordColor());
               lightSeg(mask, WC8_MINUTES,  nextWordColor()); break;
      case  3: lightSeg(mask, WC8_QUARTER,  nextWordColor()); break;
      case  4: lightSeg(mask, WC8_TWENTY,   nextWordColor());
               lightSeg(mask, WC8_MINUTES,  nextWordColor()); break;
      case  5: lightSeg(mask, WC8_TWENTY,   nextWordColor());
               lightSeg(mask, WC8_FIVE_MIN, nextWordColor());
               lightSeg(mask, WC8_MINUTES,  nextWordColor()); break;
      case  6: lightSeg(mask, WC8_HALF,     nextWordColor()); break;
      case  7: lightSeg(mask, WC8_TWENTY,   nextWordColor());
               lightSeg(mask, WC8_FIVE_MIN, nextWordColor());
               lightSeg(mask, WC8_MINUTES,  nextWordColor()); break;
      case  8: lightSeg(mask, WC8_TWENTY,   nextWordColor());
               lightSeg(mask, WC8_MINUTES,  nextWordColor()); break;
      case  9: lightSeg(mask, WC8_QUARTER,  nextWordColor()); break;
      case 10: lightSeg(mask, WC8_TEN_MIN,  nextWordColor());
               lightSeg(mask, WC8_MINUTES,  nextWordColor()); break;
      case 11: lightSeg(mask, WC8_FIVE_MIN, nextWordColor());
               lightSeg(mask, WC8_MINUTES,  nextWordColor()); break;
      default: break; // block 0: o'clock — no minute phrase
    }

    if (block >= 1 && block <= 6) lightSeg(mask, WC8_PAST, nextWordColor());
    if (block >= 7)                lightSeg(mask, WC8_TO,   nextWordColor());

    const WC8_Seg* hr = nullptr;
    switch (h12) {
      case  1: hr = &WC8_ONE;    break;  case  2: hr = &WC8_TWO;    break;
      case  3: hr = &WC8_THREE;  break;  case  4: hr = &WC8_FOUR;   break;
      case  5: hr = &WC8_FIVE_HR;break;  case  6: hr = &WC8_SIX;    break;
      case  7: hr = &WC8_SEVEN;  break;  case  8: hr = &WC8_EIGHT;  break;
      case  9: hr = &WC8_NINE;   break;  case 10: hr = &WC8_TEN_HR; break;
      case 11: hr = &WC8_ELEVEN; break;  case 12: hr = &WC8_TWELVE; break;
    }
    if (hr) lightSeg(mask, *hr, nextWordColor());

    if (block == 0) lightSeg(mask, WC8_OCLOCK, nextWordColor());
  }

  bool wcGetLocalTime(struct tm& t) {
    if (!WLED_CONNECTED) return false;
    updateLocalTime();
    if (localTime == 0) return false;
    struct tm tmp; localtime_r(&localTime, &tmp); t = tmp;
    return true;
  }

public:
  void setup() override {
    memset(wordPixels,     0, sizeof(wordPixels));
    memset(prevWordPixels, 0, sizeof(prevWordPixels));
  }

  void loop() override {
    if (!enabled) return;
    struct tm t;
    if (!wcGetLocalTime(t)) return;

    int min = t.tm_min, hr = t.tm_hour;

    if (firstRun || min != lastMinute) {
      memcpy(prevWordPixels, wordPixels, sizeof(wordPixels));
      buildWordMask(wordPixels, hr, min);
      lastMinute = min;
      if (firstRun) {
        firstRun = false;
        fadingIn = false;
        memcpy(prevWordPixels, wordPixels, sizeof(wordPixels));
      } else {
        fadingIn  = true;
        fadeStart = millis();
      }
    }
  }

  void handleOverlayDraw() override {
    if (!enabled) return;

    uint8_t alpha = 255;
    if (fadingIn) {
      uint32_t elapsed = millis() - fadeStart;
      if (elapsed >= (uint32_t)transitionMs) {
        fadingIn = false;
        memcpy(prevWordPixels, wordPixels, sizeof(wordPixels));
      } else {
        alpha = (uint8_t)((uint32_t)elapsed * 255 / transitionMs);
      }
    }

    for (int i = 0; i < TOTAL_LEDS; i++) {
      uint32_t newC = wordPixels[i];
      uint32_t oldC = prevWordPixels[i];
      bool isWord  = (newC != 0);
      bool wasWord = (oldC != 0);

      if (!isWord && !wasWord) {
        if (bgBrightness < 255)
          strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
      } else if (isWord && !wasWord) {
        uint8_t bri = (uint8_t)((uint32_t)wordBrightness * alpha / 255);
        if (bri > 0) strip.setPixelColor(i, scaleBri(newC, bri));
        else if (bgBrightness < 255)
          strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
      } else if (!isWord && wasWord) {
        uint8_t bri = (uint8_t)((uint32_t)wordBrightness * (255 - alpha) / 255);
        if (bri > 0) strip.setPixelColor(i, scaleBri(oldC, bri));
        else if (bgBrightness < 255)
          strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
      } else {
        strip.setPixelColor(i, scaleBri(newC, wordBrightness));
      }
    }
  }

  // ── JSON state API ─────────────────────────────────────────────────────────

  void addToJsonState(JsonObject& root) override {
    JsonObject o = root.createNestedObject(FPSTR(_name));
    o[F("on")]       = enabled;
    o[F("wordBri")]  = wordBrightness;
    o[F("bgBri")]    = bgBrightness;
    o[F("randWord")] = randomWordColor;
    o[F("tranMs")]   = transitionMs;
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", wordR, wordG, wordB);
    o[F("wordColor")] = buf;
  }

  void readFromJsonState(JsonObject& root) override {
    JsonObject o = root[FPSTR(_name)];
    if (o.isNull()) return;
    getJsonValue(o[F("on")],       enabled);
    getJsonValue(o[F("wordBri")],  wordBrightness);
    getJsonValue(o[F("bgBri")],    bgBrightness);
    getJsonValue(o[F("randWord")], randomWordColor);
    getJsonValue(o[F("tranMs")],   transitionMs);
    if (o[F("wordColor")].is<const char*>()) {
      const char* h = o[F("wordColor")];
      if (h[0]=='#' && strlen(h)==7) {
        uint32_t rgb = strtol(h+1, nullptr, 16);
        wordR=(rgb>>16)&0xFF; wordG=(rgb>>8)&0xFF; wordB=rgb&0xFF;
      }
    }
    lastMinute = 255; firstRun = true;
  }

  // ── Persistent config ──────────────────────────────────────────────────────

  void addToConfig(JsonObject& root) override {
    JsonObject o = root.createNestedObject(FPSTR(_name));
    o[F("enabled")]  = enabled;
    o[F("wordBri")]  = wordBrightness;
    o[F("bgBri")]    = bgBrightness;
    o[F("randWord")] = randomWordColor;
    o[F("tranMs")]   = transitionMs;
    o[F("wordR")]    = wordR;
    o[F("wordG")]    = wordG;
    o[F("wordB")]    = wordB;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject o = root[FPSTR(_name)];
    if (o.isNull()) return false;
    bool c = false;
    c |= getJsonValue(o[F("enabled")],  enabled);
    c |= getJsonValue(o[F("wordBri")],  wordBrightness);
    c |= getJsonValue(o[F("bgBri")],    bgBrightness);
    c |= getJsonValue(o[F("randWord")], randomWordColor);
    c |= getJsonValue(o[F("tranMs")],   transitionMs);
    c |= getJsonValue(o[F("wordR")],    wordR);
    c |= getJsonValue(o[F("wordG")],    wordG);
    c |= getJsonValue(o[F("wordB")],    wordB);
    return c;
  }

  void appendConfigData() override {
    oappend(SET_F("addInfo('wc8:wordBri',1,'Word LED brightness 0-255');"));
    oappend(SET_F("addInfo('wc8:bgBri',1,'Effect dimming: 0=black 128=half 255=full');"));
    oappend(SET_F("addInfo('wc8:randWord',1,'Each word gets its own random colour each minute');"));
    oappend(SET_F("addInfo('wc8:tranMs',1,'Crossfade duration on minute change (ms)');"));
  }

  uint16_t getId() override { return USERMOD_ID_WORDCLOCK_8X8; }
};

const char WordClock8x8Usermod::_name[] PROGMEM = "wc8";
