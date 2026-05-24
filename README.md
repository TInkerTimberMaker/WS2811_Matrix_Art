# Animated Hex Display + 8×12 Pixel Matrix

A desktop "blinkenlights" art piece built around an Arduino Nano. Eight
7-segment digits cycle through random hexadecimal characters with occasional
easter-egg words (`DEADBEEF`, `REBOOT`, `HALTC0DE`, ...) that roll into place
and dissolve away. Behind them, a 96-pixel WS2811 LED matrix runs ten
selectable animation modes, all driven by four front-panel knobs.

![hero shot](<docs/animated_display.jpg>)

## Video
[Watch the video →](<https://youtu.be/your-video-id>)

## Features

- **8 seven-segment digits** driven by a single MAX7219 multiplexer
- **96 RGB pixels** (8 wide × 12 tall) on a single data pin, serpentine wiring
- **10 animation modes** selectable from a knob — see [Pixel modes](#pixel-modes)
- **Easter-egg words** roll into the 7-seg display occasionally, hold for a few
  seconds, then dissolve back to random hex
- **Four live controls** (program, hue, rate, brightness) — no menu diving
- **Encoder-style program selection** from a regular pot, using band detection +
  hysteresis + settle time
- **Single brightness knob** dims both displays together; fully off enters
  MAX7219 low-power shutdown
- All effects run at ~33 FPS on a stock Arduino Nano with room to spare
  (~42% flash, ~30% SRAM used)

## Hardware

| Qty | Part                                                |
|-----|-----------------------------------------------------|
| 1   | Arduino Nano (ATmega328P)                           |
| 1   | MAX7219 in a 28-pin DIP socket                      |
| 8   | 7-segment LEDs, common cathode                      |
| 1   | String of 100 WS2811 RGB pixels, 12 V               |
| 4   | 10 kΩ linear potentiometers (program, hue, rate, brightness) |
| 1   | 12 V, 8 A power supply with barrel connector        |
| 1   | Iset resistor for MAX7219 (sets segment current; ~10 kΩ for typical brightness) |
| 1   | ~330 Ω series resistor on the WS2811 data line      |
| 2   | Perfboard ("greenboard"): 5×7 cm (main) + 3×5 cm (digit landing pad) |
| —   | Hookup wire, headers, knob caps, etc.               |

## 3D Printed Parts

STL files are in [`stl/`](stl/). They form the front panel and digit/pixel housings:

| File                       | Purpose                                          |
|----------------------------|--------------------------------------------------|
| `stl/<pixel_matrix.stl>`   | Frame that holds and aligns the 8×12 pixel grid  |
| `stl/<digit_bezel.stl>`    | Bezel and spacer for the 8 seven-segment digits  |
| `stl/<pot_panel.stl>`      | Front panel with mounting holes for the 4 pots   |
| `stl/<enclosure_back.stl>` | Back/case (optional)                             |

**Print settings I used:** 0.2 mm layer height, 3 walls, 15% infill, PETG.
None of the parts need supports.

## Wiring
Arduino Nano MAX7219 / pixels / pots

D6 ────────────► WS2811 DATA (through ~330 Ω series resistor)
D11 ────────────► MAX7219 CS (LOAD)
D12 ────────────► MAX7219 DIN
D13 ────────────► MAX7219 CLK
A0 ◄────────── Brightness pot wiper
A1 ◄────────── Rate (speed) pot wiper
A2 ◄────────── Hue pot wiper
A3 ◄────────── Program pot wiper
5V ────────────► All pot top ends + MAX7219 V+
GND ────────────► Common ground for everything

The MAX7219 drives the 8 digits with 16 lines (8 segments + 8 digit-enables)
using time-domain multiplexing — only one digit is lit at any instant, but the
refresh is fast enough that your eye sees all eight as continuously on. One
external resistor on `ISET` sets the per-segment current limit.

The 12 V supply powers the WS2811 string directly. The Arduino Nano can be
fed from the same supply via Vin (it has an onboard regulator) or powered
separately from USB during development. **Ground must be common between the
Nano, the pixel string, and the MAX7219.**

> ⚠️ **Logic level on the WS2811 data line:** Some 12 V WS2811 strips work fine
> with the Nano's 5 V data output, but if you see flicker or wrong colors,
> add a 74AHCT125 buffer or level shifter on the data pin.

## Software

### Install

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Install the **Adafruit NeoPixel** library via Library Manager.
3. Open `Animated_Matrix_Display.ino` from this repo.
4. Select **Tools → Board → Arduino Nano** (and the correct processor variant
   if asked — usually ATmega328P, "Old Bootloader" for the cheap clones).
5. Upload.

### Pixel modes

| # | Mode               | What it does                                                       |
|---|--------------------|--------------------------------------------------------------------|
| 0 | Solid + twinkles   | Solid color with sparse white sparkles                             |
| 1 | Sweep ↑            | Brightness wave traveling bottom-to-top in a single color          |
| 2 | Sweep →            | Same but moving left-to-right                                      |
| 3 | Retro data lights  | 1950s mainframe "blinkenlights" with occasional partial-row bursts |
| 4 | Data Page          | Latches a random 96-bit page, holds, blanks, then re-latches       |
| 5 | Matrix rain        | Falling drops with fading trails                                   |
| 6 | Scrolling waveform | Smooth random-walk samples flowing right-to-left                   |
| 7 | Diagonal scroll    | Rainbow stripes scrolling diagonally                               |
| 8 | Snake walker       | A bright head walks the grid with a fading tail                    |
| 9 | Plasma             | Smooth flowing color field                                         |

### How the knobs interact

- **Program** snaps to one of 10 discrete positions with hysteresis — turn it,
  pause briefly, and the new mode latches in. The 7-seg briefly shows
  `PROG-#`.
- **Hue** is continuous 0-255 in modes that take a single color.
- **Rate** is continuous 0-255 and each mode interprets it for its own pace.
- **Brightness** dims both displays together; fully counterclockwise puts the
  MAX7219 into shutdown.

### Customizing

Most of the per-mode tuning knobs are commented in the source. The big ones:

- **Add or remove easter-egg words** — edit the `WORDS[]` table near the top
  of the sketch. Each word is 8 characters of 7-seg-renderable text.
- **Easter-egg frequency** — `EGG_PERMILLE` (chance per check) and
  `EGG_CHECK_MS` (poll interval).
- **Matrix orientation** — flip `MATRIX_ORIGIN_RIGHT` and `MATRIX_SERPENTINE`
  to match how you wired your strip.
- **Color order** — if reds and greens look swapped, change `NEO_RGB` to
  `NEO_GRB` on the `Adafruit_NeoPixel` constructor.


## Dependencies

- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)

## License

MIT — do whatever, attribution appreciated. 

## Acknowledgments

- Build sequence and animation tuning iterated with the help of an AI coding
  assistant — the project served as a great test of how far you can push
  an Arduino Nano with a chatty co-pilot.
- The seven-segment "scramble-into-word" reveal is loosely inspired by every
  hacker movie ever made.
