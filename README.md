# Oldsun

A Juno-inspired synth played through a worn cassette, for the OP-XY with [open-xy](https://github.com/etcetc000/open-xy).
Inspired by the sound of Boards of Canada: a warm DCO and its chorus, then a tape that warps, shivers, plays backwards and falls apart.

The OP-XY's own filter, envelopes, LFO and effects follow the engine, so it plays like any stock instrument.

![The screen](../oldsun-screen.png)

Version 0.20.

## Controls

Two pages of four. **Click an encoder** to switch between them (see [Pages](#pages)). Shift + turn is fine adjustment.

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
| --- | --- | --- | --- | --- |
| **1: Synth** | WAVE | SHAPE | SUB | CHORUS |
| **2: Tape** | WOW | FLUTTER | REVERSE | DEGRADE |

- **WAVE:** morphs sine → triangle → saw → saw + pulse together (the Juno's two buttons) → pulse → **FAT**, where a second DCO joins, detuned 3 to 12 cents, beating against the first.
- **SHAPE:** bends every wave, not only the pulse, and moves it from the first step, like the Juno's PWM on its LFO: sine and triangle get a bent phase (brighter, nasal), the saw a moving comb (saw PWM), the pulse its width (50% down to 6%). Turning it bends further and lets a free-running LFO sweep it deeper and faster (0.35 Hz to 3 Hz). At 0 it is still. For tempo-synced movement, point the OP-XY's own LFO at SHAPE.
- **SUB:** a square sub-oscillator one octave down, locked to the main one.
- **CHORUS:** after the Juno-60's bucket brigade: **I** (slow), **II** (faster, lusher) and **I+II** (with a 6 Hz warble), a third of the knob each, deeper and wetter as you turn within each third. The wet path is dark and carries the faint BBD hiss. The OP-XY's synth output is mono, so this is a mono chorus, voiced for mono.
- **WOW:** a warped reel. Slow, irregular pitch bends that sag more than they rise, like a dragging motor (up to about 1.5 semitones peak to peak), and every voice drifting out of tune on its own, like old DCOs, so chords rub against themselves.
- **FLUTTER:** the capstan, a little eccentric: a wobble with its harmonics that moves from 4.3 Hz (where the ear is most sensitive) to 8.1 Hz as you turn, its depth breathing at random, with the pinch roller's sway and a fine scrape from the head. The pitch moves smoothly, sample by sample, and the level dips slightly with it.
- **REVERSE:** each note as if the tape ran backwards: it swells in from nothing and opens as it rises, then stays full while held, with no cuts. The knob sets the swell's length, from 0.1 s to 2.5 s; past halfway the swell also starts flat (up to 2 semitones at the top) and rises into tune, like a tape coming up to speed. Turned on under a held note it leaves that note full; the notes that follow swell.
- **DEGRADE:** up to 30% it is the tape's colour, even on a sine: the head bump (the lows around 60–120 Hz swelling), the tape's asymmetry (a warm 2nd harmonic) and a gently rounded top, as on a new cassette. Past it, the wear: lost highs and a hiss whose level comes and goes at random, slowly, as on a real tape, then dropouts from about 45% and grainy bit loss from about 75%.

The wow and flutter are shared by every voice, as on one cassette. Every control has an ease-out curve, so the first steps are already audible.

## The screen

A cassette at sunset. The reels turn, the left emptying as the right fills; the tape between them is the live waveform, with the chorus copy behind it; a hexagon sun sits over the mountains.
WOW makes the reels lurch, the tape bend and its copy wander off, FLUTTER makes it shiver, REVERSE slows the reels, stops them and runs them backwards, DEGRADE dims the sun, scratches the tape and brings the dust.
A note flashes the sun and sends out two rings. The two dots in the label show the page.

## Pages

The machine targets the current open-xy (`"sdk": "1.3"`) and declares `"press": "page"`: a click on any encoder
turns all four to the other page together, and the OP-XY's LFO can reach all eight controls. Nothing to patch.

## Build and install

Oldsun is a standard open-xy machine.

1. Get [open-xy](https://github.com/etcetc000/open-xy) (staging branch, SDK 1.3 or later). Keep the `oldsun` folder anywhere outside it.
2. Preview it in the browser, or test it (from the open-xy folder):
   ```sh
   python sdk.py preview --machine <path>/oldsun --out local/oldsun-preview
   python sdk.py test --machine <path>/oldsun
   ```
3. Build: `python sdk.py build --machine <path>/oldsun --stock-os <your stock>/os.ldr --out <builds>/oldsun-001`
4. Install following open-xy's [native build guide](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md), using the OS and menu file from your own OP-XY.
5. Command-line builds do not add the init preset, and without it the engine list crashes (`patch.cpp ... is_preset`). Copy `../init-preset/default.json` (included) to `content/presets/oldsun/default.json` on the device; the browser builder writes it for you. It matches the defaults in `definition.json`.

Identity: engine 1, screen page 84. Choose another free ID if your collection already uses these.

The presets in `definition.json` (Init, Clean, Roygbiv, Olson, Warm tape, Seasick, Sub bass, Fat Juno, Strings, Flute, Backwards, Worn VHS) are for the browser preview.

## CPU budget

Like the OP-XY's own engines, Oldsun keeps within a fixed share of the processor whatever the settings: each voice costs by what it has switched on, and when the notes held need more than the budget, the oldest voices fade out over 1.5 ms so new notes always play; every sounding voice keeps its whole sound. The budget was measured on an OP-XY (glitches began near 220 units of work with every control at the top) and set with a margin: about 7 voices of the init sound, 4–5 with everything at the top, 16 at most. The voices' weights were measured feature by feature (DEGRADE is the heaviest, then CHORUS, SUB and REVERSE). Blocks are counted from the voices themselves (a voice about to play twice means a new block), so the tape's wow and flutter are the same for every voice.

Version 0.20 does the same work with fewer operations: the tape's saturation rewritten (one multiply less), its
level restored once per block, the offset and head bump filters stepped every 4 samples with their in-between
drawn exactly to first order, REVERSE and the chorus in one loop, and only the waveforms that sound computed.
The output rounds softly into its limit (above 0.4375 of full scale) instead of clipping, and DEGRADE crossfades
when switched on or off. Against version 0.19 the difference is 50–70 dB below the signal.

## No firmware included

This folder is Oldsun's source code only: no OS image (`os.ldr`), firmware or OP-XY files are distributed.
Build your own firmware with open-xy, from your own OP-XY's OS.

## Status

Experimental. It runs on an OP-XY with OS 1.2.0 and open-xy, and passes the SDK's checks (ABI, fixed memory, peak bounds, lifecycle, drawing).
CPU load has not been measured on the device. Installing modified firmware is at your own risk; keep the official OS at hand (te-boot: hold COM at power-on).

## Files

| File | What |
| --- | --- |
| `engine.cpp`, `core.h` | The sound: the DCO, chorus and tape, 32-bit fixed point |
| `tables.h` | Lookup tables |
| `display.cpp`, `display.h` | The screen's animation |
| `panel.svg.fragment` | The screen's artwork (grayscale; the colour one is in `../colour`) |
| `definition.json` | Controls, preview presets, memory sizes |
| `machine.json` | Kind, identity, description |

## License

MIT, like the open-xy SDK.

Copyright (c) 2026 the Oldsun authors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

"Juno" and "Juno-60" are trademarks of Roland Corporation. Oldsun is not affiliated with or endorsed by Roland or Boards of Canada; their names are used only to describe the sound that inspired it.
