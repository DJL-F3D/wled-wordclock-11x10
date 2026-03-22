#pragma once
#include "wled.h"

/*
 * WLED Word Clock Usermod — 11×10 LED matrix
 * ===========================================
 * Overlays the current time as words on whatever WLED effect is running.
 *
 * Rendering model (handleOverlayDraw)
 * ------------------------------------
 * WLED calls handleOverlayDraw() AFTER the effect renders each frame but
 * BEFORE show() sends it to the LEDs.  At that point:
 *   strip.getPixelColor(i) = what the effect just painted
 * We then:
 *   Background pixels → dim the effect colour by bgBrightness
 *     0   = background fully black
 *     128 = half-brightness effect
 *     255 = effect at full brightness (unchanged)
 *   Word pixels → overwrite with the word's own colour at wordBrightness
 *     When randomWordColor = true, each word gets a different random colour
 *     each minute; otherwise all words share wordColor.
 *
 * Hardware
 * --------
 *   LEDs 0-3   : minute indicator dots (1 per extra minute mod 5)
 *   LEDs 4-113 : 11×10 serpentine matrix
 *                even rows (0,2,4…) left→right, odd rows (1,3,5…) right→left
 *
 * Stencil (row 0 = top)
 *   Row 0: I T L I S A S A M P M
 *   Row 1: A C Q U A R T E R D C
 *   Row 2: T W E N T Y F I V E X
 *   Row 3: H A L F S T E N J T O
 *   Row 4: P A S T E B U N I N E
 *   Row 5: O N E S I X T H R E E
 *   Row 6: F O U R F I V E T W O
 *   Row 7: E I G H T E L E V E N
 *   Row 8: S E V E N T W E L V E
 *   Row 9: T E N S Z O C L O C K
 */

struct WC_Seg { uint8_t row, col, len; };

// clang-format off
static constexpr WC_Seg WC_IT       = {0,  0, 2};
static constexpr WC_Seg WC_IS       = {0,  3, 2};
static constexpr WC_Seg WC_AM       = {0,  7, 2};
static constexpr WC_Seg WC_PM       = {0,  9, 2};
static constexpr WC_Seg WC_QUARTER  = {1,  2, 7};
static constexpr WC_Seg WC_TWENTY   = {2,  0, 6};
static constexpr WC_Seg WC_FIVE_MIN = {2,  6, 4};
static constexpr WC_Seg WC_HALF     = {3,  0, 4};
static constexpr WC_Seg WC_TEN_MIN  = {3,  5, 3};
static constexpr WC_Seg WC_TO       = {3,  9, 2};
static constexpr WC_Seg WC_PAST     = {4,  0, 4};
static constexpr WC_Seg WC_NINE     = {4,  7, 4};
static constexpr WC_Seg WC_ONE      = {5,  0, 3};
static constexpr WC_Seg WC_SIX      = {5,  3, 3};
static constexpr WC_Seg WC_THREE    = {5,  6, 5};
static constexpr WC_Seg WC_FOUR     = {6,  0, 4};
static constexpr WC_Seg WC_FIVE_HR  = {6,  4, 4};
static constexpr WC_Seg WC_TWO      = {6,  8, 3};
static constexpr WC_Seg WC_EIGHT    = {7,  0, 5};
static constexpr WC_Seg WC_ELEVEN   = {7,  5, 6};
static constexpr WC_Seg WC_SEVEN    = {8,  0, 5};
static constexpr WC_Seg WC_TWELVE   = {8,  5, 6};
static constexpr WC_Seg WC_TEN_HR   = {9,  0, 3};
static constexpr WC_Seg WC_OCLOCK   = {9,  5, 6};
// clang-format on

class WordClockUsermod : public Usermod {
public:
  static const char _name[];

private:
  static const int COLS        = 11;
  static const int ROWS        = 10;
  static const int MINUTE_LEDS = 4;
  static const int MATRIX_LEDS = ROWS * COLS;
  static const int TOTAL_LEDS  = MINUTE_LEDS + MATRIX_LEDS; // 114

  // ── Configurable settings ──────────────────────────────────────────────────
  bool     enabled         = true;
  uint8_t  wordBrightness  = 255;
  uint8_t  bgBrightness    = 40;
  bool     showAmPm        = true;
  bool     randomWordColor = false; // each word gets its own random colour/min
  uint16_t transitionMs    = 800;
  uint8_t  wordR = 255, wordG = 200, wordB = 100;

  // ── Runtime state ─────────────────────────────────────────────────────────
  uint8_t  lastMinute  = 255;
  bool     firstRun    = true;
  uint8_t  minuteDots  = 0;

  // Per-LED word colour — 0 means background pixel, non-zero means word pixel.
  // Storing colour here (not just bool) is what enables per-word random colours.
  uint32_t wordPixels[TOTAL_LEDS];
  uint32_t prevWordPixels[TOTAL_LEDS];

  bool     fadingIn  = false;
  uint32_t fadeStart = 0;

  // ── Helpers ────────────────────────────────────────────────────────────────

  inline int matrixLed(int row, int col) const {
    int base = MINUTE_LEDS + row * COLS;
    return (row % 2 == 0) ? base + col : base + (COLS - 1 - col);
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
    if (mx > 0) { r = (uint16_t)r*255/mx; g = (uint16_t)g*255/mx; b = (uint16_t)b*255/mx; }
    return packRGB(r, g, b);
  }

  // Returns either a random colour or the configured word colour.
  // Each call to this when randomWordColor=true returns a NEW colour,
  // so calling it once per word gives each word its own distinct hue.
  uint32_t nextWordColor() {
    return randomWordColor ? vibrantRandom() : packRGB(wordR, wordG, wordB);
  }

  void lightSeg(uint32_t* mask, const WC_Seg& seg, uint32_t color) {
    for (int i = 0; i < seg.len; i++) {
      int idx = matrixLed(seg.row, seg.col + i);
      if (idx >= 0 && idx < TOTAL_LEDS) mask[idx] = color;
    }
  }

  void buildWordMask(uint32_t* mask, int hour, int minute) {
    memset(mask, 0, sizeof(uint32_t) * TOTAL_LEDS);

    int block  = minute / 5;
    minuteDots = minute % 5;

    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    if (block >= 7) h12 = (h12 % 12) + 1;

    lightSeg(mask, WC_IT, nextWordColor());
    lightSeg(mask, WC_IS, nextWordColor());

    if (showAmPm)
      lightSeg(mask, hour < 12 ? WC_AM : WC_PM, nextWordColor());

    switch (block) {
      case  1: lightSeg(mask, WC_FIVE_MIN, nextWordColor()); break;
      case  2: lightSeg(mask, WC_TEN_MIN,  nextWordColor()); break;
      case  3: lightSeg(mask, WC_QUARTER,  nextWordColor()); break;
      case  4: lightSeg(mask, WC_TWENTY,   nextWordColor()); break;
      case  5: { uint32_t c = nextWordColor();
                 lightSeg(mask, WC_TWENTY,   c);
                 lightSeg(mask, WC_FIVE_MIN, nextWordColor()); break; }
      case  6: lightSeg(mask, WC_HALF,     nextWordColor()); break;
      case  7: { lightSeg(mask, WC_TWENTY,   nextWordColor());
                 lightSeg(mask, WC_FIVE_MIN, nextWordColor()); break; }
      case  8: lightSeg(mask, WC_TWENTY,   nextWordColor()); break;
      case  9: lightSeg(mask, WC_QUARTER,  nextWordColor()); break;
      case 10: lightSeg(mask, WC_TEN_MIN,  nextWordColor()); break;
      case 11: lightSeg(mask, WC_FIVE_MIN, nextWordColor()); break;
      default: break;
    }

    if (block >= 1 && block <= 6) lightSeg(mask, WC_PAST, nextWordColor());
    if (block >= 7)                lightSeg(mask, WC_TO,   nextWordColor());

    const WC_Seg* hr = nullptr;
    switch (h12) {
      case  1: hr = &WC_ONE;    break;  case  2: hr = &WC_TWO;    break;
      case  3: hr = &WC_THREE;  break;  case  4: hr = &WC_FOUR;   break;
      case  5: hr = &WC_FIVE_HR;break;  case  6: hr = &WC_SIX;    break;
      case  7: hr = &WC_SEVEN;  break;  case  8: hr = &WC_EIGHT;  break;
      case  9: hr = &WC_NINE;   break;  case 10: hr = &WC_TEN_HR; break;
      case 11: hr = &WC_ELEVEN; break;  case 12: hr = &WC_TWELVE; break;
    }
    if (hr) lightSeg(mask, *hr, nextWordColor());

    if (block == 0) lightSeg(mask, WC_OCLOCK, nextWordColor());
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

    // Compute crossfade alpha (0 = prev fully shown, 255 = new fully shown)
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

    // Matrix pixels
    for (int i = MINUTE_LEDS; i < TOTAL_LEDS; i++) {
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
        else if (bgBrightness < 255) strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
      } else if (!isWord && wasWord) {
        uint8_t bri = (uint8_t)((uint32_t)wordBrightness * (255 - alpha) / 255);
        if (bri > 0) strip.setPixelColor(i, scaleBri(oldC, bri));
        else if (bgBrightness < 255) strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
      } else {
        // Pixel is a word in both old and new — show new colour at full brightness
        strip.setPixelColor(i, scaleBri(newC, wordBrightness));
      }
    }

    // Minute dots
    uint32_t dotColor = scaleBri(packRGB(wordR, wordG, wordB), wordBrightness);
    for (int i = 0; i < MINUTE_LEDS; i++) {
      if (i < minuteDots)
        strip.setPixelColor(i, dotColor);
      else if (bgBrightness < 255)
        strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
    }
  }

  // ── JSON state API ─────────────────────────────────────────────────────────

  void addToJsonState(JsonObject& root) override {
    JsonObject o = root.createNestedObject(FPSTR(_name));
    o[F("on")]       = enabled;
    o[F("wordBri")]  = wordBrightness;
    o[F("bgBri")]    = bgBrightness;
    o[F("ampm")]     = showAmPm;
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
    getJsonValue(o[F("ampm")],     showAmPm);
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
    o[F("ampm")]     = showAmPm;
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
    c |= getJsonValue(o[F("ampm")],     showAmPm);
    c |= getJsonValue(o[F("randWord")], randomWordColor);
    c |= getJsonValue(o[F("tranMs")],   transitionMs);
    c |= getJsonValue(o[F("wordR")],    wordR);
    c |= getJsonValue(o[F("wordG")],    wordG);
    c |= getJsonValue(o[F("wordB")],    wordB);
    return c;
  }

  void appendConfigData() override {
    oappend(SET_F("addInfo('wc:wordBri',1,'Word LED brightness 0-255');"));
    oappend(SET_F("addInfo('wc:bgBri',1,'Effect dimming: 0=black 128=half 255=full');"));
    oappend(SET_F("addInfo('wc:randWord',1,'Each word gets its own random colour each minute');"));
    oappend(SET_F("addInfo('wc:tranMs',1,'Crossfade duration on minute change (ms)');"));
  }

  uint16_t getId() override { return USERMOD_ID_WORDCLOCK; }
};

const char WordClockUsermod::_name[] PROGMEM = "wc";
