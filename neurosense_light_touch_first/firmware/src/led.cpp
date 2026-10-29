#include "led.h"
#include <driver/gpio.h>
#include "config.h"

// Colour bits for the optional RGB LED. The on-board LED lights for any non-zero colour.
enum : uint8_t { C_R = 1, C_G = 2, C_B = 4, C_Y = C_R | C_G, C_W = C_R | C_G | C_B };

struct Step { uint16_t onMs; uint16_t offMs; uint8_t rgb; };
struct Pattern { const Step* steps; uint8_t count; uint8_t repeat; };  // repeat 0 = forever

// ---- pattern tables (flash memory, no runtime allocation) ---------------------------
static const Step S_BOOT[]      = {{100, 100, C_W}, {100, 100, C_W}, {100, 100, C_W}};
static const Step S_ADVERT[]    = {{80, 1920, C_B}};
static const Step S_CONNECTED[] = {{80, 120, C_G}, {80, 200, C_G}};
static const Step S_UNDO[]      = {{400, 0, C_W}};
static const Step S_NOTSENT[]   = {{60, 60, C_R}, {60, 60, C_R}, {60, 60, C_R}, {60, 60, C_R}, {60, 60, C_R}};
static const Step S_SLEEP[]     = {{400, 0, C_W}};
#if HAS_RGB_LED
static const Step S_SCORE0[]    = {{200, 0, C_R}};
static const Step S_SCORE1[]    = {{200, 0, C_Y}};
static const Step S_SCORE2[]    = {{200, 0, C_G}};
#else
static const Step S_SCORE0[]    = {{120, 120, C_W}};
static const Step S_SCORE1[]    = {{120, 120, C_W}, {120, 120, C_W}};
static const Step S_SCORE2[]    = {{120, 120, C_W}, {120, 120, C_W}, {120, 120, C_W}};
#endif

#define PAT(a, rep) {a, (uint8_t)(sizeof(a) / sizeof(a[0])), rep}
static const Pattern FLASHES[] = {
  /* LP_BOOT      */ PAT(S_BOOT, 1),
  /* LP_CONNECTED */ PAT(S_CONNECTED, 1),
  /* LP_SCORE0    */ PAT(S_SCORE0, 1),
  /* LP_SCORE1    */ PAT(S_SCORE1, 1),
  /* LP_SCORE2    */ PAT(S_SCORE2, 1),
  /* LP_UNDO      */ PAT(S_UNDO, 1),
  /* LP_NOTSENT   */ PAT(S_NOTSENT, 1),
  /* LP_SLEEP     */ PAT(S_SLEEP, 1),
};
static const Pattern STATUS_ADVERT = PAT(S_ADVERT, 0);

// Error pattern is rebuilt in place: N blinks, then a long pause, repeated.
static constexpr uint8_t ERR_MAX_BLINKS = 8;
static Step errSteps[ERR_MAX_BLINKS];
static Pattern errPattern;

// ---- player state ---------------------------------------------------------------------
static const Pattern* cur = nullptr;  // pattern being played (flash/error/status), null = dark
static const Pattern* base = nullptr; // status pattern to return to
static bool curIsFlash = false;
static uint8_t stepIdx, repIdx;
static bool phaseOn;
static uint32_t phaseEnd;

static void write(uint8_t rgb) {
  digitalWrite(LED_PIN, (rgb != 0) != LED_ACTIVE_LOW ? HIGH : LOW);
#if HAS_RGB_LED
  auto lvl = [](bool on) { return (on == RGB_ACTIVE_HIGH) ? HIGH : LOW; };
  digitalWrite(RGB_PIN_R, lvl(rgb & C_R));
  digitalWrite(RGB_PIN_G, lvl(rgb & C_G));
  digitalWrite(RGB_PIN_B, lvl(rgb & C_B));
#endif
}

static void startStep(uint32_t now) {
  const Step& s = cur->steps[stepIdx];
  if (s.onMs) { phaseOn = true; phaseEnd = now + s.onMs; write(s.rgb); }
  else { phaseOn = false; phaseEnd = now + s.offMs; write(0); }
}

static void play(const Pattern* p, bool isFlash, uint32_t now) {
  cur = p; curIsFlash = isFlash; stepIdx = 0; repIdx = 0;
  startStep(now);
}

void led_init() {
  gpio_hold_dis((gpio_num_t)LED_PIN);   // release the latch from a previous deep sleep
  pinMode(LED_PIN, OUTPUT);
#if HAS_RGB_LED
  gpio_hold_dis((gpio_num_t)RGB_PIN_R); gpio_hold_dis((gpio_num_t)RGB_PIN_G); gpio_hold_dis((gpio_num_t)RGB_PIN_B);
  pinMode(RGB_PIN_R, OUTPUT); pinMode(RGB_PIN_G, OUTPUT); pinMode(RGB_PIN_B, OUTPUT);
#endif
  write(0);
}

void led_set_status(LedStatus s) {
  base = (s == LS_ADVERTISING) ? &STATUS_ADVERT : nullptr;
  if (!curIsFlash) {                    // never interrupt a flash/error in progress
    if (base) play(base, false, millis());
    else { cur = nullptr; write(0); }
  }
}

void led_flash(LedPattern p) { play(&FLASHES[p], true, millis()); }

void led_error(uint8_t code) {
  if (code > ERR_MAX_BLINKS) code = ERR_MAX_BLINKS;
  for (uint8_t i = 0; i < code; i++) errSteps[i] = {150, (uint16_t)(i == code - 1 ? 1500 : 250), C_R};
  uint32_t cycle = code * 150u + (code - 1) * 250u + 1500u;
  errPattern = {errSteps, code, (uint8_t)((10000 + cycle - 1) / cycle)};  // ~10 s total
  play(&errPattern, true, millis());
}

bool led_busy() { return cur && curIsFlash; }

uint32_t led_next_deadline() { return cur ? phaseEnd : 0; }

void led_service(uint32_t now) {
  while (cur && (int32_t)(now - phaseEnd) >= 0) {
    const Step& s = cur->steps[stepIdx];
    if (phaseOn && s.offMs) {            // on -> off
      phaseOn = false; phaseEnd += s.offMs; write(0);
      continue;
    }
    write(0);                            // step finished
    if (++stepIdx >= cur->count) {
      stepIdx = 0;
      if (cur->repeat && ++repIdx >= cur->repeat) {   // pattern finished
        if (base) play(base, false, now); else { cur = nullptr; curIsFlash = false; }
        return;
      }
    }
    startStep(phaseEnd);                 // keep timing drift-free
  }
}

void led_hold_for_sleep() {
  write(0);
  gpio_hold_en((gpio_num_t)LED_PIN);
#if HAS_RGB_LED
  gpio_hold_en((gpio_num_t)RGB_PIN_R); gpio_hold_en((gpio_num_t)RGB_PIN_G); gpio_hold_en((gpio_num_t)RGB_PIN_B);
#endif
}
