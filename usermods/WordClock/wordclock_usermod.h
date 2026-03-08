#pragma once
#include "wled.h"

/*
 * WLED Word Clock Usermod
 * ========================
 * Renders the current time as words on an 11×10 LED matrix,
 * overlaid on top of whatever WLED effect is currently running.
 *
 * Rendering model
 * ───────────────
 * The standard usermod loop() runs BEFORE the WLED effect engine
 * each frame, so any pixels written there get overwritten.
 * This usermod uses handleOverlayDraw() instead — a virtual
 * method called by WLED AFTER the effect renders but BEFORE
 * show() sends the frame to the LEDs.
 *
 *   • Background pixels : scale down the effect color by bgBrightness
 *                         0   = background fully black
 *                         128 = half-brightness effect
 *                         255 = effect shown at full brightness (unchanged)
 *   • Word pixels       : overwrite with solid wordColor at wordBrightness
 *   • Minute dots       : same solid color as words, or off
 *
 * On each minute change, old words crossfade out and new words
 * crossfade in over transitionMs milliseconds.
 *
 * Physical layout
 * ───────────────
 *   LEDs 0–3   : Minute indicator dots (one lights per extra minute mod-5)
 *   LEDs 4–113 : 11×10 matrix, serpentine wiring
 *                Even rows 0,2,4… run left→right
 *                Odd  rows 1,3,5… run right→left
 *
 * Stencil
 *   Row 0: I  T  L  I  S  A  S  A  M  P  M
 *   Row 1: A  C  Q  U  A  R  T  E  R  D  C
 *   Row 2: T  W  E  N  T  Y  F  I  V  E  X
 *   Row 3: H  A  L  F  S  T  E  N  J  T  O
 *   Row 4: P  A  S  T  E  B  U  N  I  N  E
 *   Row 5: O  N  E  S  I  X  T  H  R  E  E
 *   Row 6: F  O  U  R  F  I  V  E  T  W  O
 *   Row 7: E  I  G  H  T  E  L  E  V  E  N
 *   Row 8: S  E  V  E  N  T  W  E  L  V  E
 *   Row 9: T  E  N  S  Z  O  C  L  O  C  K
 *
 * Home Assistant: POST to http://<ip>/json/state with {"wc":{...}}
 */

// ──────────────────────────────────────────────────────────────────────────────
// Word segment descriptors — row, start-column, length
// ──────────────────────────────────────────────────────────────────────────────
struct WC_Seg { uint8_t row, col, len; };

// clang-format off
static constexpr WC_Seg WC_IT       = {0,  0, 2};  // IT
static constexpr WC_Seg WC_IS       = {0,  3, 2};  // IS
static constexpr WC_Seg WC_AM       = {0,  7, 2};  // AM
static constexpr WC_Seg WC_PM       = {0,  9, 2};  // PM
static constexpr WC_Seg WC_QUARTER  = {1,  2, 7};  // QUARTER
static constexpr WC_Seg WC_TWENTY   = {2,  0, 6};  // TWENTY
static constexpr WC_Seg WC_FIVE_MIN = {2,  6, 4};  // FIVE  (minutes)
static constexpr WC_Seg WC_HALF     = {3,  0, 4};  // HALF
static constexpr WC_Seg WC_TEN_MIN  = {3,  5, 3};  // TEN   (minutes)
static constexpr WC_Seg WC_TO       = {3,  9, 2};  // TO
static constexpr WC_Seg WC_PAST     = {4,  0, 4};  // PAST
static constexpr WC_Seg WC_NINE     = {4,  7, 4};  // NINE
static constexpr WC_Seg WC_ONE      = {5,  0, 3};  // ONE
static constexpr WC_Seg WC_SIX      = {5,  3, 3};  // SIX
static constexpr WC_Seg WC_THREE    = {5,  6, 5};  // THREE
static constexpr WC_Seg WC_FOUR     = {6,  0, 4};  // FOUR
static constexpr WC_Seg WC_FIVE_HR  = {6,  4, 4};  // FIVE  (hours)
static constexpr WC_Seg WC_TWO      = {6,  8, 3};  // TWO
static constexpr WC_Seg WC_EIGHT    = {7,  0, 5};  // EIGHT
static constexpr WC_Seg WC_ELEVEN   = {7,  5, 6};  // ELEVEN
static constexpr WC_Seg WC_SEVEN    = {8,  0, 5};  // SEVEN
static constexpr WC_Seg WC_TWELVE   = {8,  5, 6};  // TWELVE
static constexpr WC_Seg WC_TEN_HR   = {9,  0, 3};  // TEN   (hours)
static constexpr WC_Seg WC_OCLOCK   = {9,  5, 6};  // O'CLOCK
// clang-format on

// ──────────────────────────────────────────────────────────────────────────────
// Usermod class
// ──────────────────────────────────────────────────────────────────────────────
class WordClockUsermod : public Usermod {
public:
  static const char _name[];

private:
  // ── Matrix geometry constants ──────────────────────────────────────────────
  static const int COLS        = 11;
  static const int ROWS        = 10;
  static const int MINUTE_LEDS = 4;
  static const int MATRIX_LEDS = ROWS * COLS;                 // 110
  static const int TOTAL_LEDS  = MINUTE_LEDS + MATRIX_LEDS;  // 114

  // ── Configurable settings (persisted to flash) ────────────────────────────
  bool     enabled         = true;
  uint8_t  wordBrightness  = 255;  // 0-255: brightness of word pixels
  uint8_t  bgBrightness    = 40;   // 0-255: scale applied to the effect pixels
                                   //   0   → background black
                                   //   128 → background at half effect brightness
                                   //   255 → background unchanged (full effect)
  bool     showAmPm        = true;
  bool     randomWordColor = false; // pick a new random word color each minute
  uint16_t transitionMs    = 800;  // crossfade duration on minute rollover (ms)
  uint8_t  wordR = 255, wordG = 200, wordB = 100; // warm-white default

  // ── Runtime state ─────────────────────────────────────────────────────────
  uint8_t  lastMinute  = 255;
  bool     firstRun    = true;

  // Word pixel masks — true means "this LED index is a lit word pixel"
  bool wordPixels[TOTAL_LEDS];     // current target mask
  bool prevWordPixels[TOTAL_LEDS]; // previous mask (used during crossfade)

  uint8_t  minuteDots = 0;         // how many minute-dot LEDs to light (0-4)

  // Crossfade state
  bool     fadingIn  = false;
  uint32_t fadeStart = 0;

  // Resolved word color (updated each minute, or on settings change)
  uint32_t resolvedWordColor = 0;

  // ── Low-level helpers ─────────────────────────────────────────────────────

  /** Physical LED index for matrix cell (row, col), accounting for serpentine wiring. */
  inline int matrixLed(int row, int col) const {
    int base = MINUTE_LEDS + row * COLS;
    return (row % 2 == 0) ? base + col : base + (COLS - 1 - col);
  }

  static inline uint32_t packRGB(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }

  /**
   * Scale all channels of a packed WRGB color by bri/255.
   * Handles both RGB and RGBW strip types.
   */
  static inline uint32_t scaleBri(uint32_t c, uint8_t bri) {
    if (bri == 255) return c;
    if (bri == 0)   return 0;
    uint8_t r = ((c >> 16) & 0xFF) * bri / 255;
    uint8_t g = ((c >>  8) & 0xFF) * bri / 255;
    uint8_t b = ((c      ) & 0xFF) * bri / 255;
    uint8_t w = ((c >> 24) & 0xFF) * bri / 255;
    return ((uint32_t)w << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }

  /** Generate a vibrant random color (one channel always at 255). */
  static uint32_t vibrantRandom() {
    uint8_t r = random8(), g = random8(), b = random8();
    uint8_t mx = max(r, max(g, b));
    if (mx > 0) {
      r = (uint16_t)r * 255 / mx;
      g = (uint16_t)g * 255 / mx;
      b = (uint16_t)b * 255 / mx;
    }
    return packRGB(r, g, b);
  }

  void resolveWordColor() {
    resolvedWordColor = randomWordColor ? vibrantRandom()
                                       : packRGB(wordR, wordG, wordB);
  }

  // ── Word mask helpers ─────────────────────────────────────────────────────

  void clearMask(bool* mask) {
    memset(mask, 0, sizeof(bool) * TOTAL_LEDS);
  }

  void lightSeg(bool* mask, const WC_Seg& seg) {
    for (int i = 0; i < seg.len; i++) {
      int idx = matrixLed(seg.row, seg.col + i);
      if (idx >= 0 && idx < TOTAL_LEDS) mask[idx] = true;
    }
  }

  // ── Time → word mask conversion ───────────────────────────────────────────

  /**
   * Build a word pixel mask for the given hour (0-23) and minute (0-59).
   * Also sets minuteDots for the sub-5-minute indicator LEDs.
   */
  void buildWordMask(bool* mask, int hour, int minute) {
    clearMask(mask);

    int block  = minute / 5;    // 0-11 (which 5-minute interval)
    minuteDots = minute % 5;    // 0-4 (extra minutes shown as dots)

    // Convert to 12-hour and advance hour for "TO" phrases (blocks 7-11)
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    if (block >= 7) h12 = (h12 % 12) + 1;  // next hour for "X to HOUR"

    // "IT IS" always lit
    lightSeg(mask, WC_IT);
    lightSeg(mask, WC_IS);

    // AM / PM indicator
    if (showAmPm) {
      lightSeg(mask, hour < 12 ? WC_AM : WC_PM);
    }

    // Minute phrase
    switch (block) {
      case  0:                                                           break;
      case  1: lightSeg(mask, WC_FIVE_MIN);                             break;
      case  2: lightSeg(mask, WC_TEN_MIN);                              break;
      case  3: lightSeg(mask, WC_QUARTER);                              break;
      case  4: lightSeg(mask, WC_TWENTY);                               break;
      case  5: lightSeg(mask, WC_TWENTY); lightSeg(mask, WC_FIVE_MIN); break;
      case  6: lightSeg(mask, WC_HALF);                                 break;
      case  7: lightSeg(mask, WC_TWENTY); lightSeg(mask, WC_FIVE_MIN); break;
      case  8: lightSeg(mask, WC_TWENTY);                               break;
      case  9: lightSeg(mask, WC_QUARTER);                              break;
      case 10: lightSeg(mask, WC_TEN_MIN);                              break;
      case 11: lightSeg(mask, WC_FIVE_MIN);                             break;
    }

    // PAST / TO connective
    if (block >= 1 && block <= 6) lightSeg(mask, WC_PAST);
    if (block >= 7)                lightSeg(mask, WC_TO);

    // Hour word
    switch (h12) {
      case  1: lightSeg(mask, WC_ONE);    break;
      case  2: lightSeg(mask, WC_TWO);    break;
      case  3: lightSeg(mask, WC_THREE);  break;
      case  4: lightSeg(mask, WC_FOUR);   break;
      case  5: lightSeg(mask, WC_FIVE_HR);break;
      case  6: lightSeg(mask, WC_SIX);    break;
      case  7: lightSeg(mask, WC_SEVEN);  break;
      case  8: lightSeg(mask, WC_EIGHT);  break;
      case  9: lightSeg(mask, WC_NINE);   break;
      case 10: lightSeg(mask, WC_TEN_HR); break;
      case 11: lightSeg(mask, WC_ELEVEN); break;
      case 12: lightSeg(mask, WC_TWELVE); break;
    }

    // O'CLOCK on the hour
    if (block == 0) lightSeg(mask, WC_OCLOCK);
  }

  // ── Time acquisition ──────────────────────────────────────────────────────

  /**
   * Populate t with the current local time.
   * Returns false if WLED is not connected or NTP not yet synced.
   * Named wcGetLocalTime to avoid colliding with the ESP32 SDK function.
   */
  bool wcGetLocalTime(struct tm& t) {
    if (!WLED_CONNECTED) return false;
    updateLocalTime();              // WLED built-in: refreshes the localTime global
    if (localTime == 0) return false;
    struct tm tmp;
    localtime_r(&localTime, &tmp);
    t = tmp;
    return true;
  }

public:
  // ── Lifecycle ─────────────────────────────────────────────────────────────

  void setup() override {
    clearMask(wordPixels);
    clearMask(prevWordPixels);
    resolveWordColor();
  }

  /**
   * loop() only updates the word mask and triggers fades on minute change.
   * It does NOT write any pixels — that happens in handleOverlayDraw().
   */
  void loop() override {
    if (!enabled) return;

    struct tm t;
    if (!wcGetLocalTime(t)) return;  // wait for NTP sync

    int currentMinute = t.tm_min;
    int currentHour   = t.tm_hour;

    if (firstRun || currentMinute != lastMinute) {
      // Save the current mask so we can crossfade from it
      memcpy(prevWordPixels, wordPixels, sizeof(wordPixels));

      // Build the new mask
      resolveWordColor();
      buildWordMask(wordPixels, currentHour, currentMinute);
      lastMinute = currentMinute;

      if (firstRun) {
        // Snap to current time immediately — no fade on startup
        firstRun  = false;
        fadingIn  = false;
        memcpy(prevWordPixels, wordPixels, sizeof(wordPixels));
      } else {
        fadingIn  = true;
        fadeStart = millis();
      }
    }
  }

  /**
   * handleOverlayDraw() — called by WLED after the current effect has rendered
   * its frame into the strip buffer, and before show() sends it to the LEDs.
   *
   * At this point strip.getPixelColor(i) returns the effect's rendered color
   * for LED i.  We apply two operations:
   *
   *   Background pixels : dim the effect color by bgBrightness
   *   Word pixels       : replace with solid wordColor (crossfading in/out)
   */
  void handleOverlayDraw() override {
    if (!enabled) return;

    // ── Compute crossfade alpha ────────────────────────────────────────────
    // alpha = 0   → showing prevWordPixels fully
    // alpha = 255 → showing wordPixels fully
    uint8_t alpha = 255;
    if (fadingIn) {
      uint32_t elapsed = millis() - fadeStart;
      if (elapsed >= (uint32_t)transitionMs) {
        fadingIn = false;
        // Sync prev mask so next fade starts from the correct state
        memcpy(prevWordPixels, wordPixels, sizeof(wordPixels));
        alpha = 255;
      } else {
        alpha = (uint8_t)((uint32_t)elapsed * 255 / transitionMs);
      }
    }

    // ── Matrix pixels (LEDs 4-113) ────────────────────────────────────────
    for (int i = MINUTE_LEDS; i < TOTAL_LEDS; i++) {
      bool isWord  = wordPixels[i];
      bool wasWord = prevWordPixels[i];

      if (!isWord && !wasWord) {
        // ── Pure background pixel ──────────────────────────────────────
        // Dim whatever the effect rendered; if bgBrightness==255 do nothing
        if (bgBrightness < 255) {
          strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
        }
      } else {
        // ── This pixel is a word pixel in the current or previous display
        // Compute effective word brightness for this pixel
        uint8_t wordBri;
        if (isWord && wasWord) {
          // Stable word (present in both): keep at full word brightness
          wordBri = wordBrightness;
        } else if (isWord) {
          // New word: fade in  0 → wordBrightness
          wordBri = (uint8_t)((uint32_t)wordBrightness * alpha / 255);
        } else {
          // Old word: fade out  wordBrightness → 0
          wordBri = (uint8_t)((uint32_t)wordBrightness * (255 - alpha) / 255);
        }

        if (wordBri > 0) {
          // Word is visible: paint solid word color at computed brightness
          strip.setPixelColor(i, scaleBri(resolvedWordColor, wordBri));
        } else {
          // Word has completely faded — treat as background
          if (bgBrightness < 255) {
            strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
          }
        }
      }
    }

    // ── Minute indicator dots (LEDs 0-3) ──────────────────────────────────
    uint32_t dotColor = scaleBri(resolvedWordColor, wordBrightness);
    for (int i = 0; i < MINUTE_LEDS; i++) {
      if (i < minuteDots) {
        // Lit dot: solid word color
        strip.setPixelColor(i, dotColor);
      } else {
        // Unlit dot: background effect, dimmed
        if (bgBrightness < 255) {
          strip.setPixelColor(i, scaleBri(strip.getPixelColor(i), bgBrightness));
        }
      }
    }
  }

  // ── JSON state API (read/write from WLED app or Home Assistant) ───────────

  void addToJsonState(JsonObject& root) override {
    JsonObject obj = root.createNestedObject(FPSTR(_name));
    obj[F("on")]        = enabled;
    obj[F("wordBri")]   = wordBrightness;
    obj[F("bgBri")]     = bgBrightness;
    obj[F("ampm")]      = showAmPm;
    obj[F("randWord")]  = randomWordColor;
    obj[F("tranMs")]    = transitionMs;
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", wordR, wordG, wordB);
    obj[F("wordColor")] = buf;
  }

  void readFromJsonState(JsonObject& root) override {
    JsonObject obj = root[FPSTR(_name)];
    if (obj.isNull()) return;
    getJsonValue(obj[F("on")],       enabled);
    getJsonValue(obj[F("wordBri")],  wordBrightness);
    getJsonValue(obj[F("bgBri")],    bgBrightness);
    getJsonValue(obj[F("ampm")],     showAmPm);
    getJsonValue(obj[F("randWord")], randomWordColor);
    getJsonValue(obj[F("tranMs")],   transitionMs);
    if (obj[F("wordColor")].is<const char*>()) {
      const char* hex = obj[F("wordColor")];
      if (hex[0] == '#' && strlen(hex) == 7) {
        uint32_t rgb = strtol(hex + 1, nullptr, 16);
        wordR = (rgb >> 16) & 0xFF;
        wordG = (rgb >>  8) & 0xFF;
        wordB =  rgb        & 0xFF;
      }
    }
    // Force redraw on next loop() tick
    lastMinute = 255;
    firstRun   = true;
  }

  // ── Persistent config (saved to flash) ───────────────────────────────────

  void addToConfig(JsonObject& root) override {
    JsonObject obj = root.createNestedObject(FPSTR(_name));
    obj[F("enabled")]   = enabled;
    obj[F("wordBri")]   = wordBrightness;
    obj[F("bgBri")]     = bgBrightness;
    obj[F("ampm")]      = showAmPm;
    obj[F("randWord")]  = randomWordColor;
    obj[F("tranMs")]    = transitionMs;
    obj[F("wordR")]     = wordR;
    obj[F("wordG")]     = wordG;
    obj[F("wordB")]     = wordB;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject obj = root[FPSTR(_name)];
    if (obj.isNull()) return false;
    bool changed = false;
    changed |= getJsonValue(obj[F("enabled")],   enabled);
    changed |= getJsonValue(obj[F("wordBri")],   wordBrightness);
    changed |= getJsonValue(obj[F("bgBri")],     bgBrightness);
    changed |= getJsonValue(obj[F("ampm")],      showAmPm);
    changed |= getJsonValue(obj[F("randWord")],  randomWordColor);
    changed |= getJsonValue(obj[F("tranMs")],    transitionMs);
    changed |= getJsonValue(obj[F("wordR")],     wordR);
    changed |= getJsonValue(obj[F("wordG")],     wordG);
    changed |= getJsonValue(obj[F("wordB")],     wordB);
    return changed;
  }

  // ── WLED config page hints ────────────────────────────────────────────────

  void appendConfigData() override {
    oappend(SET_F("addInfo('wc:wordBri',1,'Word LED brightness 0-255');"));
    oappend(SET_F("addInfo('wc:bgBri',1,'Effect behind words: 0=black 128=half 255=unchanged');"));
    oappend(SET_F("addInfo('wc:tranMs',1,'Crossfade duration on minute change (ms)');"));
  }

  uint16_t getId() override { return USERMOD_ID_WORDCLOCK; }
};

const char WordClockUsermod::_name[] PROGMEM = "wc";
