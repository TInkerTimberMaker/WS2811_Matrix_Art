/* ============================================================================
 *  Animated Hex Display + 8x12 Pixel Matrix
 *  ----------------------------------------------------------------------------
 *  An Arduino Nano "blinkenlights" art piece combining:
 *    - 8x seven-segment digits (MAX7219) cycling through random hex characters
 *      with occasional "easter egg" words (DEADBEEF, REBOOT, etc.) that roll
 *      into place, hold for a few seconds, then dissolve back to random hex.
 *    - 96 pixel WS2811 LED matrix (8 wide x 12 tall, serpentine wiring) running
 *      ten selectable animation modes.
 *
 *  Four front-panel potentiometers act as live controls:
 *    A0  Brightness   -- dims 7-segs and matrix together; fully off enters
 *                        MAX7219 low-power shutdown.
 *    A1  Speed        -- animation rate; each mode interprets this differently.
 *    A2  Color        -- 0..255 hue for matrix modes that take a single color.
 *    A3  Program      -- 0..9 latched selection. The pot is treated like a
 *                        discrete 10-position rotary encoder via band
 *                        detection + hysteresis + settle time.
 *
 *  Wiring
 *    MAX7219:   DIN = D12,  CLK = D13,  CS = D11
 *    Pixels:    DATA = D6  (use a level shifter / 74AHCT125 for 12V WS2811)
 *    Pots:      All four on A0..A3 as listed above
 *
 *  Pixel modes
 *    0  Solid color + sparse twinkle overlay
 *    1  Solid color sweep, bottom -> top (brightness wave)
 *    2  Solid color sweep, left -> right (brightness wave)
 *    3  Retro mainframe "blinkenlights": discrete random blinks with bursts
 *    4  Data Page: latch a random 96-bit page, hold, blank, repeat
 *    5  Matrix rain (Cascade of single-color drops with fading trails)
 *    6  Scrolling waveform (right-to-left smooth random-walk samples)
 *    7  Diagonal rainbow scroll
 *    8  Snake walker with fading tail and random direction changes
 *    9  Plasma (sin-approximation color field)
 *
 *  Dependencies
 *    - Adafruit_NeoPixel library  (install via Arduino Library Manager)
 *
 * Copyright 2026 - Tinker & Timber
 *
 * ============================================================================ */

#include <avr/pgmspace.h>
#include <string.h>


// ============================================================================
//  Build-time configuration
// ============================================================================

#define DEBUG          1            // 1 = serial debug, 0 = silent release build
#define DEBUG_BAUD     115200

#define USE_NEOPIXEL   1            // 0 = disable all WS2811 code

// Debug macros: compile down to nothing when DEBUG = 0
#if DEBUG
  #define DBG_BEGIN()    Serial.begin(DEBUG_BAUD)
  #define DBG(x)         Serial.print(x)
  #define DBGLN(x)       Serial.println(x)
  #define DBG_F(x)       Serial.print(F(x))
  #define DBGLN_F(x)     Serial.println(F(x))
#else
  #define DBG_BEGIN()
  #define DBG(x)
  #define DBGLN(x)
  #define DBG_F(x)
  #define DBGLN_F(x)
#endif


// ============================================================================
//  Pixel matrix (WS2811) configuration
// ============================================================================
#if USE_NEOPIXEL
  #include <Adafruit_NeoPixel.h>

  #define LED_PIN              6
  #define MATRIX_W             8
  #define MATRIX_H             12
  #define NUM_LED              (MATRIX_W * MATRIX_H)

  #define MATRIX_ORIGIN_RIGHT  0    // 0: chain starts lower-LEFT,  1: lower-RIGHT
  #define MATRIX_SERPENTINE    1    // 0: all rows wired left-to-right
                                    // 1: zig-zag (every other row reversed)

  // NEO_RGB + 800kHz is correct for most 12V WS2811 strips.
  // If reds and greens look swapped, try NEO_GRB.
  // If the strip lights up solid white on every update, swap to NEO_KHZ400.
  Adafruit_NeoPixel strip(NUM_LED, LED_PIN, NEO_RGB + NEO_KHZ800);
#endif


// ============================================================================
//  MAX7219 wiring and segment mapping
// ============================================================================

#define DIN        12
#define CLK        13
#define CS         11
#define N_DIGITS   8

// Raw MAX7219 hardware segment bits. Adjust if your display's pinout differs.
#define SEG_DP     0x80
#define SEG_A      0x40
#define SEG_B      0x20
#define SEG_C      0x10
#define SEG_D      0x08
#define SEG_E      0x04
#define SEG_F      0x02
#define SEG_G      0x01

// Logical (wiring-agnostic) segment bits. Used by the glyph table below;
// remapped to hardware bits via SEG_MAP_PGM at draw time.
#define L_A        0x01
#define L_B        0x02
#define L_C        0x04
#define L_D        0x08
#define L_E        0x10
#define L_F        0x20
#define L_G        0x40
#define L_DP       0x80

// Maps display position (0..7, left to right) -> MAX7219 digit register (1..8).
// Customize if your digits are wired in a different physical order.
const uint8_t ORDER_PGM[8] PROGMEM = { 1, 5, 7, 3, 4, 8, 6, 2 };

// Maps a logical segment (L_A..L_DP) to a hardware segment bit (SEG_A..SEG_DP).
// Tweak this if individual segments of your display light up incorrectly.
const uint8_t SEG_MAP_PGM[8] PROGMEM = {
  /* A  */ SEG_B,
  /* B  */ SEG_G,
  /* C  */ SEG_DP,
  /* D  */ SEG_E,
  /* E  */ SEG_C,
  /* F  */ SEG_F,
  /* G  */ SEG_A,
  /* DP */ SEG_D
};

// Hex glyphs 0..F expressed as logical segment masks.
const uint8_t HEXL_PGM[16] PROGMEM = {
  /* 0 */ L_A|L_B|L_C|L_D|L_E|L_F,
  /* 1 */ L_B|L_C,
  /* 2 */ L_A|L_B|L_D|L_E|L_G,
  /* 3 */ L_A|L_B|L_C|L_D|L_G,
  /* 4 */ L_F|L_G|L_B|L_C,
  /* 5 */ L_A|L_F|L_G|L_C|L_D,
  /* 6 */ L_A|L_F|L_E|L_D|L_C|L_G,
  /* 7 */ L_A|L_B|L_C,
  /* 8 */ L_A|L_B|L_C|L_D|L_E|L_F|L_G,
  /* 9 */ L_A|L_B|L_C|L_D|L_F|L_G,
  /* A */ L_A|L_B|L_C|L_E|L_F|L_G,
  /* B */ L_C|L_D|L_E|L_F|L_G,
  /* C */ L_A|L_F|L_E|L_D,
  /* D */ L_B|L_C|L_D|L_E|L_G,
  /* E */ L_A|L_F|L_E|L_D|L_G,
  /* F */ L_A|L_F|L_E|L_G
};


// ============================================================================
//  Timing constants
// ============================================================================

// 7-segment digit cycling
#define MIN_MS            100      // shortest hold time for a single hex digit
#define MAX_MS            2500     // longest hold time for a single hex digit

// Easter-egg word reveal behaviour
#define EGG_ENABLED       1        // 0 = never show easter-egg words
#define EGG_HOLD_MS       8000     // how long the word stays on screen
#define EGG_CHECK_MS      10000    // poll interval for "should we trigger?"
#define EGG_COOLDOWN_MS   30000    // minimum gap between two egg appearances
#define EGG_PERMILLE      159      // chance per check, 0..1000 (159 = ~16%)


// ============================================================================
//  Program selector (A3 -> 0..9 latched mode)
// ============================================================================
// The program pot is binned into N_PROGRAMS equal-sized bands across the ADC
// range. To prevent flicker between adjacent bands, the pot value must remain
// inside the "core" of a band (shrunk by PROG_HYST_PCT on each side) for at
// least PROG_SETTLE_MS milliseconds before the new selection latches.

#define N_PROGRAMS            10
#define PROG_SETTLE_MS        120
#define PROG_HYST_PCT         15
#define ADC_FULL              1024
#define PROG_BAND             (ADC_FULL / N_PROGRAMS)
#define PROG_HYST             ((PROG_BAND * PROG_HYST_PCT + 50) / 100)

#define SHOW_PROGRAM_ON_MAX   1        // briefly show "PROG-#" on 7-seg on change
#define PROG_SHOW_MS          1000     // duration of that "PROG-#" overlay


// ============================================================================
//  Potentiometer pins and presence flags
// ============================================================================

#define POT_BRIGHT   A0
#define POT_SPEED    A1
#define POT_COLOR    A2
#define POT_MODE     A3

// Toggle to 0 if you haven't wired a pot on that input; the code will use the
// DEFAULT_* value instead and keep the pin pulled up (quiet) at runtime.
#define POT_BRIGHT_HAS  1
#define POT_COLOR_HAS   1
#define POT_SPEED_HAS   1
#define POT_MODE_HAS    1

// Fallback values when a pot is marked as not present.
#define DEFAULT_BRIGHT    5
#define DEFAULT_COLOR     128
#define DEFAULT_SPEED     128
#define DEFAULT_PROGRAM   0


// ============================================================================
//  Easter-egg word list (PROGMEM, 8 chars each)
// ============================================================================
#define WDEF(n,txt) const char n[] PROGMEM = txt;
WDEF(W00,"DEADBEEF")  WDEF(W01,"DEADC0DE")  WDEF(W02,"C0DEBASE")
WDEF(W03,"B00TL0AD")  WDEF(W04,"DEBUGGER")  WDEF(W05,"L0ADBYTE")
WDEF(W06,"C0REDR0P")  WDEF(W07,"C0FFEE")    WDEF(W08,"CACHED")
WDEF(W09,".0UTPUT.")  WDEF(W10,"1NPUT")     WDEF(W11,"5LA5HD0T")
WDEF(W12,"CLEARED")   WDEF(W13,"..5T0P..")  WDEF(W14,"5TART")
WDEF(W15,"--NERD--")  WDEF(W16,"--HALT--")  WDEF(W17,"C0NNECT")
WDEF(W18,"L0ADER")    WDEF(W19,"..L0AD..")  WDEF(W20,"..BU5Y..")
WDEF(W21,"HALTC0DE")  WDEF(W22,"READBYTE")  WDEF(W23,"..B00T..")
WDEF(W24,"NULLBYTE")  WDEF(W25,"L0AD.8.1")  WDEF(W26,"PUT.BYTE")
WDEF(W27,"IF..THEN")  WDEF(W28,"ENDIF")     WDEF(W29,"ELSEIF")
WDEF(W30,"D0.UNTIL")  WDEF(W31,"DIGITAL")   WDEF(W32,"ANALOG")
WDEF(W33,"JSR..RTS")  WDEF(W34,".REB00T.")  WDEF(W35,"LDA..STA")
WDEF(W36,"ERR0R.42")  WDEF(W37,"ERR0R")     WDEF(W38,"L0ADC0DE")
WDEF(W39,"UPDATING")  WDEF(W40,".UPDATE.")  WDEF(W41,"GET.BYTE")
WDEF(W42,"5318008")   WDEF(W43,".HALTED.")  

#undef WDEF

const char* const WORDS[] PROGMEM = {
  W00,W01,W02,W03,W04,W05,W06,W07,W08,W09,W10,W11,W12,W13,W14,W15,W16,W17,W18,W19,
  W20,W21,W22,W23,W24,W25,W26,W27,W28,W29,W30,W31,W32,W33,W34,W35,W36,W37,W38,W39,
  W40,W41,W42,W43
};
#define N_WORDS (sizeof(WORDS) / sizeof(WORDS[0]))


// ============================================================================
//  Low-level helpers
// ============================================================================

// Forward decls (Arduino auto-prototypes are usually fine, but explicit beats
// implicit when a header function uses one defined further down).
static inline void putMask_ifChanged(uint8_t pos, uint8_t mask);

// Bit-bang one register/data pair to the MAX7219.
static inline void send(uint8_t reg, uint8_t data) {
  digitalWrite(CS, LOW);
  shiftOut(DIN, CLK, MSBFIRST, reg);
  shiftOut(DIN, CLK, MSBFIRST, data);
  digitalWrite(CS, HIGH);
}

static inline uint8_t  rd8_P(const uint8_t* p)   { return pgm_read_byte(p); }
static inline uint8_t  orderReg(uint8_t pos)     { return rd8_P(&ORDER_PGM[pos]); }

// Time helpers in 16-bit and 32-bit flavors. The 16-bit forms wrap every
// ~65 seconds, which is fine for short-duration timers in the 7-seg loop and
// keeps the math cheap on AVR.
static inline uint16_t now16()                   { return (uint16_t)millis(); }
static inline bool     due(uint16_t t)           { return (int16_t)(now16() - t) >= 0; }
static inline uint32_t now32()                   { return millis(); }
static inline bool     due32(uint32_t t)         { return (int32_t)(now32() - t) >= 0; }

static inline void setIntensity(uint8_t lvl) {
  if (lvl > 15) lvl = 15;
  send(0x0A, lvl);
}

// Read an ADC pin with a throw-away read first to allow the MUX to settle.
static inline uint16_t adcReadStable(uint8_t pin) {
  analogRead(pin);
  delayMicroseconds(8);
  return analogRead(pin);
}

// Median of three stable reads -- cheap de-noise for jittery pots.
static uint16_t readStableADC3(uint8_t pin) {
  uint16_t a = adcReadStable(pin), b = adcReadStable(pin), c = adcReadStable(pin);
  if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
  return (a < c) ? c : a;
}

// Apply 4-bit brightness to the MAX7219, with true power-off at nibble == 0.
static bool gMaxIsOff = false;
static inline void max7_applyBrightnessNib(uint8_t nib) {
  if (nib == 0) {
    if (!gMaxIsOff) { send(0x0C, 0x00); gMaxIsOff = true; }   // shutdown
  } else {
    if (gMaxIsOff)  { send(0x0C, 0x01); gMaxIsOff = false; }  // wake
    setIntensity(nib);
  }
}

// Remap logical segment bits (L_A..L_DP) to MAX7219 hardware bits.
static inline uint8_t remap(uint8_t logical) {
  uint8_t m = 0;
  if (logical & L_A ) m |= rd8_P(&SEG_MAP_PGM[0]);
  if (logical & L_B ) m |= rd8_P(&SEG_MAP_PGM[1]);
  if (logical & L_C ) m |= rd8_P(&SEG_MAP_PGM[2]);
  if (logical & L_D ) m |= rd8_P(&SEG_MAP_PGM[3]);
  if (logical & L_E ) m |= rd8_P(&SEG_MAP_PGM[4]);
  if (logical & L_F ) m |= rd8_P(&SEG_MAP_PGM[5]);
  if (logical & L_G ) m |= rd8_P(&SEG_MAP_PGM[6]);
  if (logical & L_DP) m |= rd8_P(&SEG_MAP_PGM[7]);
  return m;
}

static inline void putHex(uint8_t pos, uint8_t n) {
  putMask_ifChanged(pos, remap(pgm_read_byte(&HEXL_PGM[n & 0xF])));
}

// Looks up the segment mask for a printable ASCII character. Unsupported
// characters return 0 (blank). The 7-seg can't render everything; this is the
// curated subset used by the easter-egg words above.
static uint8_t glyphFor(char ch) {
  switch (ch) {
    case '0': return L_A|L_B|L_C|L_D|L_E|L_F;
    case '1': return L_B|L_C;
    case '2': return L_A|L_B|L_D|L_E|L_G;
    case '3': return L_A|L_B|L_C|L_D|L_G;
    case '4': return L_F|L_G|L_B|L_C;
    case '5': case 'S': case 's': return L_A|L_F|L_G|L_C|L_D;
    case '6': return L_A|L_F|L_E|L_D|L_C|L_G;
    case '7': return L_A|L_B|L_C;
    case '8': return L_A|L_B|L_C|L_D|L_E|L_F|L_G;
    case '9': return L_A|L_B|L_C|L_D|L_F|L_G;
    case 'A': case 'a': return L_A|L_B|L_C|L_E|L_F|L_G;
    case 'B': case 'b': return L_C|L_D|L_E|L_F|L_G;
    case 'C': case 'c': return L_A|L_F|L_E|L_D;
    case 'D': case 'd': return L_B|L_C|L_D|L_E|L_G;
    case 'E': case 'e': return L_A|L_F|L_E|L_D|L_G;
    case 'F': case 'f': return L_A|L_F|L_E|L_G;
    case 'G': case 'g': return L_A|L_F|L_E|L_D|L_C;
    case 'H': case 'h': return L_B|L_C|L_E|L_F|L_G;
    case 'I': case 'i': return L_B|L_C;
    case 'J':           return L_B|L_C|L_D|L_E;
    case 'L':           return L_F|L_E|L_D;
    case 'N': case 'n': return L_C|L_E|L_G;
    case 'O': case 'o': return L_A|L_B|L_C|L_D|L_E|L_F;
    case 'P':           return L_A|L_B|L_E|L_F|L_G;
    case 'R': case 'r': return L_E|L_G;
    case 'T': case 't': return L_D|L_E|L_F|L_G;
    case 'U':           return L_B|L_C|L_D|L_E|L_F;
    case 'Y': case 'y': return L_B|L_C|L_D|L_F|L_G;
    case 'Z': case 'z': return L_A|L_B|L_D|L_E|L_G;
    case '-':           return L_G;
    case '.':           return L_DP;
    default:            return 0;
  }
}

// Read up to 8 characters from WORDS[idx] into buf[]; null-terminates.
static void readWord8(uint8_t idx, char buf[9]) {
  const char* p = (const char*)pgm_read_word(&WORDS[idx]);
  uint8_t n = 0;
  char c;
  while (n < 8 && (c = pgm_read_byte(p++))) buf[n++] = c;
  buf[n] = '\0';
}

// Pad a word out to 8 characters with '-' and pick a random horizontal offset.
static void buildDashed8_P(uint8_t idx, char out8[9]) {
  char tmp[9]; readWord8(idx, tmp);
  for (uint8_t i = 0; tmp[i]; ++i) if (tmp[i] == ' ') tmp[i] = '-';
  uint8_t len   = strlen(tmp); if (len > 8) len = 8;
  uint8_t room  = 8 - len;
  uint8_t start = room ? random(room + 1) : 0;
  for (uint8_t i = 0; i < 8; i++) out8[i] = '-';
  for (uint8_t i = 0; i < len; i++) out8[start + i] = tmp[i];
  out8[8] = '\0';
}


// ============================================================================
//  MAX7219 write-cache (skip redundant SPI traffic)
// ============================================================================
static uint8_t shadowMask[8];
static bool    shadowInit = false;

static inline void putMask_ifChanged(uint8_t pos, uint8_t mask) {
  if (!shadowInit) {
    for (uint8_t i = 0; i < 8; i++) shadowMask[i] = 0xFF;
    shadowInit = true;
  }
  if (shadowMask[pos] == mask) return;
  shadowMask[pos] = mask;
  send(orderReg(pos), mask);
}


// ============================================================================
//  Program selector (A3 pot acting like a discrete 10-position encoder)
// ============================================================================
struct ProgSel {
  uint8_t  latched;       // currently confirmed program (0..9)
  uint8_t  pending;       // candidate awaiting settle (255 = none)
  uint32_t pendUntil;     // millis() time when candidate would latch
} gProgSel = {0, 255, 0};

static uint32_t progShowUntil = 0;       // "PROG-#" overlay end time on 7-seg

// Returns the band index 0..N_PROGRAMS-1 that contains the given ADC value.
static inline uint8_t progBandOf(uint16_t adc) {
  uint16_t idx = (uint32_t)adc * N_PROGRAMS / ADC_FULL;
  if (idx >= N_PROGRAMS) idx = N_PROGRAMS - 1;
  return (uint8_t)idx;
}

static inline uint16_t bandStart(uint8_t idx) {
  return (uint16_t)((uint32_t)idx * ADC_FULL / N_PROGRAMS);
}
static inline uint16_t bandEnd(uint8_t idx) {
  return (uint16_t)(((uint32_t)(idx + 1) * ADC_FULL / N_PROGRAMS) - 1);
}

// True if the ADC value sits inside the band core (band shrunk by hysteresis).
static inline bool progInEnterWindow(uint16_t adc, uint8_t idx) {
  uint16_t s = bandStart(idx), e = bandEnd(idx);
  uint16_t width = e - s + 1;
  uint16_t h     = (uint16_t)((width * PROG_HYST_PCT + 50) / 100);
  uint16_t coreS = s + h;
  uint16_t coreE = (e > h) ? (e - h) : e;
  if (coreE < coreS) coreE = coreS;
  return (adc >= coreS && adc <= coreE);
}

// Shows "PROG-#" briefly on the 7-seg whenever the program latches to a new value.
static void max7_showProgram(uint8_t idx) {
  putMask_ifChanged(0, 0);
  putMask_ifChanged(1, remap(glyphFor('P')));
  putMask_ifChanged(2, remap(glyphFor('R')));
  putMask_ifChanged(3, remap(glyphFor('0')));
  putMask_ifChanged(4, remap(glyphFor('G')));
  putMask_ifChanged(5, remap(glyphFor('-')));
  putHex          (6, idx % 10);
  putMask_ifChanged(7, 0);
}

static void onProgramChanged(uint8_t idx) {
#if SHOW_PROGRAM_ON_MAX
  progShowUntil = millis() + PROG_SHOW_MS;
  max7_showProgram(idx);
#endif
}

// Core state machine: returns the currently latched program.
// Updates pending/latch state as the pot moves.
static uint8_t programSelector_updateADC(uint16_t adc) {
  uint8_t cand = progBandOf(adc);
  uint32_t now = millis();

  if (cand == gProgSel.latched) {
    gProgSel.pending   = 255;
    gProgSel.pendUntil = 0;
    return gProgSel.latched;
  }

  if (progInEnterWindow(adc, cand)) {
    if (gProgSel.pending != cand) {
      gProgSel.pending   = cand;
      gProgSel.pendUntil = now + PROG_SETTLE_MS;
    } else if ((int32_t)(now - gProgSel.pendUntil) >= 0) {
      gProgSel.latched   = cand;
      gProgSel.pending   = 255;
      gProgSel.pendUntil = 0;
      DBG_F("[prog] adc=");      DBG((int)adc);
      DBG_F(" latched=");        DBG((int)gProgSel.latched);
      DBG_F(" tms=");            DBGLN((unsigned)millis());
      onProgramChanged(gProgSel.latched);
    }
  } else {
    gProgSel.pending = 255;
  }
  return gProgSel.latched;
}


// ============================================================================
//  Pot reading + global control struct
// ============================================================================
struct Controls {
  uint8_t brightNib;     // A0, 0..15
  uint8_t color;         // A2, 0..255
  uint8_t speed;         // A1, 0..255
  uint8_t program;       // A3 raw (kept for any legacy use), 0..255
  uint8_t progIndex;     // A3 latched, 0..N_PROGRAMS-1
} gCtl;

// Cubic gamma curve (perceptual) for the WS2811 brightness, no floats:
// out = round(255 * (in / 15)^3)
static inline uint8_t gamma8_fromNib(uint8_t i) {
  uint16_t ii  = (uint16_t)i * i;
  uint32_t iii = (uint32_t)ii * i;
  return (uint8_t)((iii * 255UL + 1687UL) / 3375UL);
}

static void controls_begin() {
  #if POT_BRIGHT_HAS
    pinMode(POT_BRIGHT, INPUT);
  #else
    pinMode(POT_BRIGHT, INPUT_PULLUP);
  #endif
  #if POT_COLOR_HAS
    pinMode(POT_COLOR, INPUT);
  #else
    pinMode(POT_COLOR, INPUT_PULLUP);
  #endif
  #if POT_SPEED_HAS
    pinMode(POT_SPEED, INPUT);
  #else
    pinMode(POT_SPEED, INPUT_PULLUP);
  #endif
  #if POT_MODE_HAS
    pinMode(POT_MODE, INPUT);
  #else
    pinMode(POT_MODE, INPUT_PULLUP);
  #endif
}

static void controls_update() {
  #if POT_BRIGHT_HAS
    gCtl.brightNib = (uint8_t)(adcReadStable(POT_BRIGHT) >> 6);     // 0..15
  #else
    gCtl.brightNib = DEFAULT_BRIGHT;
  #endif

  #if POT_COLOR_HAS
    gCtl.color = (uint8_t)(adcReadStable(POT_COLOR) >> 2);          // 0..255
  #else
    gCtl.color = DEFAULT_COLOR;
  #endif

  #if POT_SPEED_HAS
    gCtl.speed = (uint8_t)(adcReadStable(POT_SPEED) >> 2);          // 0..255
  #else
    gCtl.speed = DEFAULT_SPEED;
  #endif

  #if POT_MODE_HAS
    uint16_t rawProg = readStableADC3(POT_MODE);                     // 0..1023
    gCtl.program   = (uint8_t)(rawProg >> 2);                        // 0..255 legacy
    gCtl.progIndex = programSelector_updateADC(rawProg);             // 0..9 stable
  #else
    gCtl.program   = DEFAULT_PROGRAM;
    gCtl.progIndex = DEFAULT_PROGRAM % N_PROGRAMS;
  #endif

  // Drive both displays from the same brightness pot.
  max7_applyBrightnessNib(gCtl.brightNib);

  #if USE_NEOPIXEL
    strip.setBrightness(gamma8_fromNib(gCtl.brightNib));
  #endif
}


// ============================================================================
//  7-segment animation: random hex + occasional easter-egg word reveal
// ============================================================================
enum Mode { MODE_NORMAL, MODE_ROLLIN, MODE_HOLD, MODE_ROLLOUT };
static Mode mode = MODE_NORMAL;

// Per-digit timing and current value
static uint8_t   curNib[8];                  // current displayed nibble (0..15)
static uint16_t  nextAt16[8];                // when this digit will reshuffle
static uint16_t  nextScr16[8];               // scramble tick during ROLLIN

// Phase timers
static uint16_t  phaseStart16 = 0;
static uint16_t  holdEnd16    = 0;
static uint16_t  nextDbg16    = 0;
static uint32_t  nextEggCheck32 = 0;
static uint32_t  notBefore32  = 0;           // egg cooldown gate

// Egg-word reveal state
static char      eggWord[9];
static uint8_t   eggTarget[8];               // final segment masks per position
static uint8_t   bgMask[8];                  // snapshot of background hex on roll-in
static uint8_t   relJit[8];                  // small per-position rollout jitter
static uint8_t   rank8[8];                   // randomized roll-in order per position
static uint16_t  startAt16[8];               // when scramble starts for this position
static uint16_t  lockAt16[8];                // when this position locks to its final glyph

static inline uint8_t rndNibNo08() {
  // Random hex digit but skip 0 and 8 (they look identical to many segments)
  uint8_t n;
  do { n = (uint8_t)random(16); } while (n == 0 || n == 8);
  return n;
}

// Scale the [MIN_MS .. MAX_MS] hold range by the speed pot.
// Note: speed is divided by 1.5 to keep the upper end from being too snappy.
static inline uint16_t spanBySpeed() {
  uint16_t span   = MAX_MS - MIN_MS;
  uint16_t scaled = (uint16_t)((uint32_t)span * (255 - (gCtl.speed / 1.5)) / 255);
  return (uint16_t)(MIN_MS + scaled);
}

static inline uint16_t rhold() { return (uint16_t)random(spanBySpeed() + 1); }


// ============================================================================
//  Boot
// ============================================================================
void max7_begin() {
  DBG_BEGIN();
  DBGLN_F("[boot] MAX7219 + WS2811 + debug");

  randomSeed(analogRead(A6));
  controls_begin();

  pinMode(DIN, OUTPUT);
  pinMode(CLK, OUTPUT);
  pinMode(CS,  OUTPUT);
  digitalWrite(CS, HIGH);

  // MAX7219 init
  send(0x0F, 0x00);                  // display test off
  send(0x0C, 0x01);                  // shutdown register: normal operation
  send(0x09, 0x00);                  // decode mode: no decode (raw segments)
  send(0x0B, N_DIGITS - 1);          // scan limit: drive all 8 digits
  setIntensity(3);                   // initial brightness (pot overrides each frame)
  for (uint8_t d = 1; d <= 8; d++) send(d, 0x00);   // blank all

  #if USE_NEOPIXEL
    strip.begin();
    strip.setBrightness(64);         // pot overrides each frame
    strip.show();
  #endif

  // Seed per-digit timers with random first-cycle holds
  uint16_t now = now16();
  for (uint8_t i = 0; i < N_DIGITS; i++) {
    curNib[i]   = random(16);
    putHex(i, curNib[i]);
    nextAt16[i] = now + rhold();
  }
  nextEggCheck32 = now32() + EGG_CHECK_MS;
  notBefore32    = 0;                // cooldown cleared at boot
  nextDbg16      = now + 600;

  DBG_F("[boot] words=");        DBGLN((unsigned)N_WORDS);
  DBG_F("[boot] egg permille="); DBGLN((unsigned)EGG_PERMILLE);
  DBGLN_F("[boot] ready");
}


// ============================================================================
//  Easter-egg word reveal
//  Pick a word, snapshot the current digits as the "background", schedule each
//  position to scramble in turn and then lock to its final glyph.
// ============================================================================
static void startEgg() {
  uint8_t idx = random(N_WORDS);
  char orig[9]; readWord8(idx, orig);
  buildDashed8_P(idx, eggWord);

  for (uint8_t pos = 0; pos < 8; pos++) {
    bgMask[pos]    = remap(pgm_read_byte(&HEXL_PGM[curNib[pos] & 0xF]));
    eggTarget[pos] = remap(glyphFor(eggWord[pos]));
  }

  // Fisher-Yates shuffle of [0..7] -> rank8[pos] = animation slot for that position
  uint8_t seq[8]; for (uint8_t i = 0; i < 8; i++) seq[i] = i;
  for (int i = 7; i > 0; i--) {
    int j = random(i + 1);
    uint8_t t = seq[i]; seq[i] = seq[j]; seq[j] = t;
  }
  for (uint8_t r = 0; r < 8; r++) rank8[seq[r]] = r;

  uint16_t now = now16();
  phaseStart16 = now;

  const uint16_t base = 200, step = 140, win = 120;
  for (uint8_t pos = 0; pos < 8; pos++) {
    uint8_t  r       = rank8[pos];
    uint16_t jitter  = (uint16_t)random(0, 35);
    startAt16[pos]   = (uint16_t)(base + r * step + jitter);
    lockAt16[pos]    = (uint16_t)(startAt16[pos] + win);
    nextScr16[pos]   = now + (uint16_t)random(40, 90);
  }

  mode = MODE_ROLLIN;
  DBG_F("[egg] pick idx=");  DBG(idx);
  DBG_F(" orig=\"");          DBG(orig);
  DBG_F("\" dashed=\"");      DBG(eggWord);
  DBGLN_F("\"");
}


// ============================================================================
//  WS2811 helpers + 10 pixel-matrix modes
// ============================================================================
#if USE_NEOPIXEL

// Translate logical (x,y) -> chain index, accounting for origin and serpentine
// wiring. y=0 is the bottom row by convention.
static inline uint16_t pixIndex(uint8_t x, uint8_t y) {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  if (MATRIX_ORIGIN_RIGHT) x = MATRIX_W - 1 - x;
  if (MATRIX_SERPENTINE && (y & 1)) x = MATRIX_W - 1 - x;
  return (uint16_t)y * MATRIX_W + x;
}

// Standard 0..255 "color wheel" returning packed RGB. r->g->b->r as input rises.
static uint32_t wheel(uint8_t w) {
  w = 255 - w;
  if (w < 85)   return strip.Color(255 - w * 3, 0, w * 3);
  if (w < 170) { w -= 85;  return strip.Color(0, w * 3, 255 - w * 3); }
                 w -= 170; return strip.Color(w * 3, 255 - w * 3, 0);
}

// ----- Shared per-mode state ------------------------------------------------
static uint8_t  pxScratch[NUM_LED];                   // per-pixel scratch (heat / trail / brightness)
static uint8_t  prevProgIndex = 0xFF;                 // detect mode change
static uint8_t  mrDrops[MATRIX_W];                    // matrix rain head position (in 16ths)
static uint8_t  mrJitter[MATRIX_W];                   // per-column speed jitter
static uint8_t  saHeight[MATRIX_W];                   // waveform column heights
static uint8_t  snakeX = 0, snakeY = 0, snakeDir = 1; // 0=N, 1=E, 2=S, 3=W
static uint8_t  dataPage[MATRIX_H];                   // 1 row = 1 byte of bits
static uint32_t pageHoldUntil = 0;
static uint32_t blankUntil    = 0;
static bool     pageInBlank   = true;                 // start by rolling a page

static inline void clearScratch() { memset(pxScratch, 0, NUM_LED); }

// Multiply a packed RGB color by a 0..255 fade value (cheap per-channel scale).
static inline uint32_t fadeColor(uint32_t c, uint8_t fade) {
  uint16_t r = ((c >> 16) & 0xFF) * fade;
  uint16_t g = ((c >>  8) & 0xFF) * fade;
  uint16_t b = ( c        & 0xFF) * fade;
  return ((uint32_t)(r >> 8) << 16) | ((uint32_t)(g >> 8) << 8) | (b >> 8);
}

static inline void setXY(uint8_t x, uint8_t y, uint32_t c) {
  strip.setPixelColor(pixIndex(x, y), c);
}

// Cheap triangle-wave proxy for sin(): 0..255 in -> 0..255 out, peak at 128.
static inline uint8_t triWave(uint8_t t) {
  return (t < 128) ? (t << 1) : ((255 - t) << 1);
}


// --- Mode 0: Solid color + sparse twinkle overlay ---------------------------
static void mode_solid_twinkle() {
  uint32_t base = wheel(gCtl.color);
  uint8_t r0 = (base >> 16) & 0xFF, g0 = (base >> 8) & 0xFF, b0 = base & 0xFF;

  // Spawn rate scales with speed; one frame in ~14 (mid) to ~4 (max).
  static uint16_t spawnAcc = 0;
  spawnAcc += (uint16_t)gCtl.speed / 4 + 4;
  while (spawnAcc >= 256) { spawnAcc -= 256; pxScratch[random(NUM_LED)] = 255; }

  // Fast decay -> snappy sparkle pop (~0.4s lifetime).
  for (uint16_t i = 0; i < NUM_LED; i++)
    pxScratch[i] = (pxScratch[i] > 18) ? pxScratch[i] - 18 : 0;

  // Draw: base color + saturated white overlay where twinkles are active.
  for (uint16_t i = 0; i < NUM_LED; i++) {
    uint8_t t = pxScratch[i];
    uint16_t r = r0 + t, g = g0 + t, b = b0 + t;
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    strip.setPixelColor(i, strip.Color((uint8_t)r, (uint8_t)g, (uint8_t)b));
  }
}


// --- Mode 1: Solid color brightness wave moving bottom -> top ---------------
static void mode_sweep_up() {
  static uint16_t scroll = 0;
  scroll += (uint16_t)gCtl.speed / 4 + 1;
  const uint8_t stretch = 18;
  uint32_t color = wheel(gCtl.color);
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    uint8_t t = (uint8_t)((scroll >> 3) - y * stretch);
    uint8_t v = 15 + ((uint16_t)triWave(t) * 240 >> 8);   // never fully off
    uint32_t c = fadeColor(color, v);
    for (uint8_t x = 0; x < MATRIX_W; x++) setXY(x, y, c);
  }
}


// --- Mode 2: Solid color brightness wave moving left -> right ---------------
static void mode_sweep_side() {
  static uint16_t scroll = 0;
  scroll += (uint16_t)gCtl.speed / 4 + 1;
  const uint8_t stretch = 24;
  uint32_t color = wheel(gCtl.color);
  for (uint8_t x = 0; x < MATRIX_W; x++) {
    uint8_t t = (uint8_t)((scroll >> 3) - x * stretch);
    uint8_t v = 15 + ((uint16_t)triWave(t) * 240 >> 8);
    uint32_t c = fadeColor(color, v);
    for (uint8_t y = 0; y < MATRIX_H; y++) setXY(x, y, c);
  }
}


// --- Mode 3: Retro mainframe blinkenlights ----------------------------------
// Discrete blinks: pick random pixels, give them a short countdown lifetime,
// occasionally fire a partial-row burst for variety.
static void mode_retro_data() {
  uint32_t baseColor = wheel(gCtl.color);

  // Decrement all active blink countdowns.
  for (uint16_t i = 0; i < NUM_LED; i++)
    if (pxScratch[i]) pxScratch[i]--;

  // Spawn new blinks. Density scales strongly with the speed pot.
  static uint16_t spawnAcc = 0;
  spawnAcc += (uint16_t)gCtl.speed * 2 + 240;
  while (spawnAcc >= 256) {
    spawnAcc -= 256;
    uint8_t lifeCap = 80 - (gCtl.speed >> 2);           // longer lives at low speed
    pxScratch[random(NUM_LED)] = (uint8_t)random(20, lifeCap + 1);
  }

  // Occasional partial-row burst (not full row -- keeps it sparse looking).
  static uint32_t nextBurst = 0;
  if (millis() >= nextBurst) {
    nextBurst = millis() + 3000 + random(7000);
    uint8_t y     = random(MATRIX_H);
    uint8_t count = random(2, 5);
    for (uint8_t k = 0; k < count; k++)
      pxScratch[pixIndex(random(MATRIX_W), y)] = (uint8_t)random(5, 20);
  }

  // Render: binary on/off (no fade) -- gives the discrete "computer thinking" look.
  for (uint16_t i = 0; i < NUM_LED; i++)
    strip.setPixelColor(i, pxScratch[i] ? baseColor : 0);
}


// --- Mode 4: Data Page ------------------------------------------------------
// Latches a random 96-bit "memory page" onto the matrix, holds for a while,
// blanks briefly, then rolls a new page. Mimics a CPU front-panel updating.
static void mode_datapage() {
  uint32_t now = millis();

  // Blank phase: between pages, briefly clear the matrix.
  if (pageInBlank) {
    if (now >= blankUntil) {
      // Roll a fresh page at ~75% fill via OR of two random bytes per row.
      for (uint8_t y = 0; y < MATRIX_H; y++)
        dataPage[y] = (uint8_t)random(256) | (uint8_t)random(256);
      // Hold duration: STRONG dependence on speed (slow = long hold).
      uint16_t holdMin = 200 + (uint16_t)(255 - gCtl.speed) * 4;
      uint16_t holdMax = 800 + (uint16_t)(255 - gCtl.speed) * 8;
      pageHoldUntil = now + random(holdMin, holdMax);
      pageInBlank   = false;
      // fall through and draw the new page this frame
    } else {
      for (uint16_t i = 0; i < NUM_LED; i++) strip.setPixelColor(i, 0);
      return;
    }
  }

  // End of hold: enter the blank phase.
  if (now >= pageHoldUntil) {
    // Blank duration: speed-scaled center with per-event randomness for variety.
    uint16_t base     = 30 + (uint16_t)(255 - gCtl.speed);
    uint16_t blankDur = random(base / 2, base * 2 + 1);
    blankUntil  = now + blankDur;
    pageInBlank = true;
    for (uint16_t i = 0; i < NUM_LED; i++) strip.setPixelColor(i, 0);
    return;
  }

  // Mid-hold: just keep drawing the current page.
  uint32_t color = wheel(gCtl.color);
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    uint8_t row = dataPage[y];
    for (uint8_t x = 0; x < MATRIX_W; x++)
      setXY(x, y, ((row >> x) & 1) ? color : 0);
  }
}


// --- Mode 5: Matrix rain ----------------------------------------------------
// One drop per column, falling top-to-bottom at slightly varied speeds.
// Trail brightness fades out as each frame ticks the scratch buffer down.
static void mode_matrix_rain() {
  // Trail decay
  for (uint16_t i = 0; i < NUM_LED; i++)
    pxScratch[i] = (pxScratch[i] > 10) ? pxScratch[i] - 10 : 0;

  uint8_t baseStep = (gCtl.speed >> 4) + 1;
  for (uint8_t x = 0; x < MATRIX_W; x++) {
    uint16_t next = (uint16_t)mrDrops[x] + baseStep + mrJitter[x];
    if (next >= (uint16_t)MATRIX_H * 16) {
      mrDrops[x]  = (uint8_t)random(8);       // small head start at the top
      mrJitter[x] = (uint8_t)random(4);       // re-randomize per-drop speed
    } else {
      mrDrops[x] = (uint8_t)next;
    }
    uint8_t rowFromTop = mrDrops[x] / 16;
    uint8_t yPix       = MATRIX_H - 1 - rowFromTop;
    pxScratch[pixIndex(x, yPix)] = 255;       // bright head
  }

  uint32_t color = wheel(gCtl.color);
  for (uint16_t i = 0; i < NUM_LED; i++)
    strip.setPixelColor(i, fadeColor(color, pxScratch[i]));
}


// --- Mode 6: Scrolling waveform ---------------------------------------------
// Each column has a height. On every "tick" the array shifts left and a new
// sample (a small random walk from the previous one) enters from the right.
// Looks like a smooth waveform flowing right-to-left.
static void mode_spectrum() {
  static uint32_t nextShift  = 0;
  static uint8_t  lastSample = MATRIX_H * 6;   // start mid-low

  uint16_t interval = 40 + (uint16_t)(255 - gCtl.speed);   // ~40..295 ms
  if (millis() >= nextShift) {
    nextShift = millis() + interval;
    for (uint8_t x = 0; x < MATRIX_W - 1; x++) saHeight[x] = saHeight[x + 1];

    int16_t nh = (int16_t)lastSample + (int16_t)random(-40, 41);
    if (nh < 0)                       nh = 0;
    if (nh > (int16_t)(MATRIX_H * 14)) nh = MATRIX_H * 14;
    lastSample = (uint8_t)nh;
    saHeight[MATRIX_W - 1] = lastSample;
  }

  // Render columns with subpixel-precision top edge.
  for (uint8_t x = 0; x < MATRIX_W; x++) {
    uint8_t hPix = saHeight[x] / 16;
    uint8_t hSub = saHeight[x] & 0x0F;
    for (uint8_t y = 0; y < MATRIX_H; y++) {
      uint32_t c = wheel((uint8_t)(gCtl.color + y * 10));    // gradient up the bar
      if      (y <  hPix) setXY(x, y, c);
      else if (y == hPix) setXY(x, y, fadeColor(c, hSub * 16));
      else                setXY(x, y, 0);
    }
  }
}


// --- Mode 7: Diagonal rainbow scroll ---------------------------------------
static void mode_diagonal() {
  static uint16_t scroll = 0;
  scroll += (uint16_t)gCtl.speed / 3 + 2;
  const uint8_t stretch = 14;
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      uint8_t h = (uint8_t)(gCtl.color + (scroll >> 3) - (x + y) * stretch);
      setXY(x, y, wheel(h));
    }
  }
}


// --- Mode 8: Snake walker ---------------------------------------------------
// A head walks the grid, choosing a new direction when it hits a wall or rolls
// a random "turn" event. Trail brightness in pxScratch fades behind it.
static void mode_snake() {
  for (uint16_t i = 0; i < NUM_LED; i++)
    pxScratch[i] = (pxScratch[i] > 10) ? pxScratch[i] - 10 : 0;

  static uint32_t nextStep = 0;
  uint16_t interval = 40 + (uint16_t)(255 - gCtl.speed) / 2;   // ~40..167 ms
  if (millis() >= nextStep) {
    nextStep = millis() + interval;

    static const int8_t dx[4] = {  0, 1, 0, -1 };
    static const int8_t dy[4] = {  1, 0,-1,  0 };

    bool wantTurn = random(10) < 2;     // 20% turn chance per step
    int8_t nx = (int8_t)snakeX + dx[snakeDir];
    int8_t ny = (int8_t)snakeY + dy[snakeDir];
    bool wall = (nx < 0 || nx >= MATRIX_W || ny < 0 || ny >= MATRIX_H);

    if (wall || wantTurn) {
      for (uint8_t k = 0; k < 8; k++) {
        snakeDir = random(4);
        nx = (int8_t)snakeX + dx[snakeDir];
        ny = (int8_t)snakeY + dy[snakeDir];
        if (nx >= 0 && nx < MATRIX_W && ny >= 0 && ny < MATRIX_H) break;
      }
    }
    if (nx >= 0 && nx < MATRIX_W && ny >= 0 && ny < MATRIX_H) {
      snakeX = (uint8_t)nx; snakeY = (uint8_t)ny;
    }
    pxScratch[pixIndex(snakeX, snakeY)] = 255;
  }

  uint32_t color = wheel(gCtl.color);
  for (uint16_t i = 0; i < NUM_LED; i++)
    strip.setPixelColor(i, fadeColor(color, pxScratch[i]));
}


// --- Mode 9: Plasma ---------------------------------------------------------
// Three triangle-wave gradients summed and fed into the color wheel.
static void mode_plasma() {
  static uint16_t phase = 0;
  phase += (uint16_t)gCtl.speed / 4 + 2;
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      uint8_t a = triWave((uint8_t)(x * 28       + (phase >> 3)));
      uint8_t b = triWave((uint8_t)(y * 20       + (phase >> 4)));
      uint8_t c = triWave((uint8_t)((x + y) * 18 + (phase >> 5)));
      uint16_t mix = (uint16_t)a + b + c;
      setXY(x, y, wheel((uint8_t)(gCtl.color + (mix >> 2))));
    }
  }
}


// --- Dispatcher: throttle, detect mode change, route to current mode --------
static void pixels_update() {
  static uint16_t nextPixMs = 0;
  uint16_t now = now16();
  if (!due(nextPixMs)) return;
  nextPixMs = now + 30;                     // ~33 FPS cap

  // Mode change: clear all shared state so the next mode starts clean.
  if (gCtl.progIndex != prevProgIndex) {
    clearScratch();
    for (uint16_t i = 0; i < NUM_LED; i++) strip.setPixelColor(i, 0);

    for (uint8_t k = 0; k < MATRIX_W; k++) {
      mrDrops[k]  = 0;
      mrJitter[k] = (uint8_t)random(4);
      saHeight[k] = 0;
    }

    // Force the data-page mode to roll a fresh page on entry.
    pageInBlank = true;
    blankUntil  = 0;

    // Drop the snake at a new random position/direction.
    snakeX   = random(MATRIX_W);
    snakeY   = random(MATRIX_H);
    snakeDir = random(4);

    prevProgIndex = gCtl.progIndex;
    DBG_F("[pix] mode -> "); DBGLN(gCtl.progIndex);
  }

  switch (gCtl.progIndex) {
    case 0:  mode_solid_twinkle(); break;
    case 1:  mode_sweep_up();      break;
    case 2:  mode_sweep_side();    break;
    case 3:  mode_retro_data();    break;
    case 4:  mode_datapage();      break;
    case 5:  mode_matrix_rain();   break;
    case 6:  mode_spectrum();      break;
    case 7:  mode_diagonal();      break;
    case 8:  mode_snake();         break;
    case 9:  mode_plasma();        break;
    default: mode_solid_twinkle(); break;
  }

  strip.show();
}

#else   // USE_NEOPIXEL == 0
static void pixels_update() {}      // no-op when WS2811 support is compiled out
#endif


// ============================================================================
//  Main frame update: 7-seg state machine + pixel update
// ============================================================================
void max7_update() {
  controls_update();         // reads pots, applies brightness to both displays
  pixels_update();           // one frame of the current matrix animation

  // Hold the "PROG-#" overlay on the 7-seg for PROG_SHOW_MS after a mode change.
  #if SHOW_PROGRAM_ON_MAX
    if (mode == MODE_NORMAL && (int32_t)(millis() - progShowUntil) < 0) {
      return;
    }
  #endif

  uint16_t now = now16();

  // Periodic debug snapshot of all control values.
  if (DEBUG && due(nextDbg16)) {
    nextDbg16 = now + 4000;
    DBG_F("[pots] nib=");   DBG(gCtl.brightNib);
    DBG_F(" hue=");          DBG(gCtl.color);
    DBG_F(" speed=");        DBG(gCtl.speed);
    DBG_F(" prog=");         DBG(gCtl.program);
    DBG_F(" mode=");         DBGLN(gCtl.progIndex);
  }

  // ----- Easter-egg state machine ------------------------------------------
  if (mode == MODE_ROLLIN) {
    bool allLocked = true;
    uint16_t el = now - phaseStart16;

    for (uint8_t pos = 0; pos < 8; pos++) {
      if ((int16_t)(el - startAt16[pos]) < 0) {
        allLocked = false;
        putMask_ifChanged(pos, bgMask[pos]);          // hold background hex
        continue;
      }
      if ((int16_t)(el - lockAt16[pos]) < 0) {
        allLocked = false;
        if (due(nextScr16[pos])) {                    // scramble between transitions
          putHex(pos, rndNibNo08());
          nextScr16[pos] = now + (uint16_t)random(120, spanBySpeed() + 220);
        }
        continue;
      }
      putMask_ifChanged(pos, eggTarget[pos]);         // locked to final glyph
    }
    if (allLocked) {
      mode      = MODE_HOLD;
      holdEnd16 = now + EGG_HOLD_MS;
      DBGLN_F("[egg] roll-in complete -> HOLD");
    }
    return;
  }

  if (mode == MODE_HOLD) {
    if (due(holdEnd16)) {
      mode         = MODE_ROLLOUT;
      phaseStart16 = now;
      DBGLN_F("[egg] HOLD done -> ROLLOUT");
    }
    return;
  }

  if (mode == MODE_ROLLOUT) {
    bool allReleased = true;
    uint16_t el = now - phaseStart16;

    for (uint8_t pos = 0; pos < 8; pos++) {
      uint16_t relT = (uint16_t)(200 + pos * 120 + relJit[pos]);
      if ((int16_t)(el - relT) < 0) {
        allReleased = false;
        putMask_ifChanged(pos, eggTarget[pos]);       // hold word until release moment
      }
    }
    if (allReleased) {
      notBefore32 = now32() + EGG_COOLDOWN_MS;
      for (uint8_t pos = 0; pos < N_DIGITS; ++pos)
        nextAt16[pos] = now + (uint16_t)random(120, spanBySpeed() + 220);
      mode = MODE_NORMAL;
      DBG_F("[egg] cooldown set until +"); DBGLN((unsigned)notBefore32);
      DBGLN_F("[egg] rollout complete -> NORMAL (cooldown)");
    }
    return;
  }

  // ----- MODE_NORMAL: poll for egg trigger, then update each digit ---------
  if (EGG_ENABLED && due(nextEggCheck32)) {
    nextEggCheck32 = now32() + EGG_CHECK_MS;
    uint16_t roll = (uint16_t)random(1000);
    bool cooled   = due32(notBefore32);
    DBG_F("[egg] roll=");   DBG(roll);
    DBG_F(" < ");           DBG((unsigned)EGG_PERMILLE);
    DBG_F(" cooled=");      DBGLN(cooled ? "yes" : "no");
    if (cooled && roll < EGG_PERMILLE) {
      DBGLN_F("[egg] HIT -> start");
      startEgg();
      return;
    }
  }
  for (uint8_t pos = 0; pos < N_DIGITS; pos++) {
    if (due(nextAt16[pos])) {
      curNib[pos] = random(16);
      putHex(pos, curNib[pos]);
      nextAt16[pos] = now + rhold();
    }
  }
}


// ============================================================================
//  Arduino entry points
// ============================================================================
void setup() { max7_begin(); }
void loop()  { max7_update(); }