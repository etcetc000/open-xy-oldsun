# Oldsun – quick guide

Version 0.20.

**Oldsun** is a synth engine for the OP-XY (running [open-xy](https://github.com/etcetc000/open-xy)):
a Juno-inspired synth played through a worn cassette. Warm oscillators and a Juno-style chorus,
then a tape that warps, wobbles, plays backwards and falls apart. Inspired by the sound of Boards of Canada.

It plays like any OP-XY instrument: the OP-XY's own **filter, envelopes, LFO and FX** all work on it.

## Getting to the engine

1. Put Oldsun on a track: **Shift + M1**, then pick **oldsun** from the engine list.
2. Press **M1** to see the engine's page. The screen shows a cassette at sunset.

## Two pages of knobs

Oldsun has **8 controls on two pages** of four encoders.

**To switch page, click (press) an encoder.** All four encoders jump to the other page together;
click it again to come back. The two dots on the cassette's label show which page you are on.

> This needs the current open-xy (SDK 1.3 or later).

Shift + turn gives fine adjustment.

### Page 1 – Synth

| Encoder | Control | What it does |
| --- | --- | --- |
| 1 | **WAVE** | Morphs **sine → triangle → saw → saw + pulse** (the Juno's two buttons) **→ pulse → FAT**, where a second, slightly detuned oscillator joins and beats against the first. |
| 2 | **SHAPE** | Bends and moves the wave from the first step: pulse width on the pulse, a moving comb on the saw, a brighter bent shape on sine and triangle. A free LFO sweeps it, deeper and faster as you turn. At 0 it is still. |
| 3 | **SUB** | A square sub-oscillator one octave down. |
| 4 | **CHORUS** | Juno-60 style chorus: **I** (slow), **II** (lusher), **I+II** (with a fast warble). A third of the knob each; deeper as you turn within each. |

### Page 2 – Tape

| Encoder | Control | What it does |
| --- | --- | --- |
| 1 | **WOW** | A warped reel: slow, uneven pitch bends that sag like a tired motor, and each voice drifting out of tune on its own, so chords rub. |
| 2 | **FLUTTER** | The tape trembling: an irregular capstan wobble, from a slow 4 Hz sway to a faster 8 Hz flutter as you turn. |
| 3 | **REVERSE** | Notes swell in like tape played backwards, opening as they rise, then stay full while held. The knob sets the swell's length, from 0.1 s to 2.5 s; past halfway the swell also starts flat and rises into tune, like a tape coming up to speed. |
| 4 | **DEGRADE** | 0 is clean. Up to 30%: a cassette's colour, even on a sine – fuller lows (head bump), a warm 2nd harmonic, a softly rounded top. Past 30%: wear – lost highs, a hiss whose level comes and goes at random, then dropouts and grain. |

Every knob is shaped so that the first steps are already audible.

## What's new in version 0.20

Same sound, same controls, same presets as version 0.19, now lighter and cleaner:

- **About 20–35% less CPU per voice** (the SDK's host estimate; how much depends on the computer measuring it).
- **No more crackle on loud sounds:** with FAT, SUB and CHORUS up the output used to hit its limit hard; it now
  rounds softly into it. Below that point nothing changes.
- **No click when DEGRADE is switched on or off:** it fades in and out over 1.5 ms.

## Tips

- **Tempo-synced movement:** point the OP-XY's LFO at **SHAPE** (it is on page 1). Leave SHAPE at 0 for synced movement only, or higher to add Oldsun's own free LFO.
- **Backwards pads:** REVERSE around 60–90% (long swells), a little WOW, CHORUS I.
- **Fat Juno:** WAVE at the top (FAT), SHAPE around 60%, CHORUS II.
- **Sub bass:** WAVE at 0 (sine), SUB up, everything on page 2 low.
- **Lost tape:** WOW and FLUTTER around 30%, DEGRADE 70% or more.

## The screen

The reels turn; the tape between them is your waveform, with the chorus copy behind it.
WOW makes the reels lurch, FLUTTER makes the tape shiver, REVERSE slows the reels and runs them backwards,
DEGRADE dims the sun and brings the dust. Every note flashes the sun.

**Colour screen:** the screen is grayscale. For the colour version, replace `oldsun/panel.svg.fragment` with
`colour/panel.svg.fragment` before building. The sound does not change.

## Presets

The `presets` folder holds Oldsun sounds. With the OP-XY in file transfer (MTP) mode, copy each preset into the
**presets** folder at the top of the OP-XY (not the one inside `content`). Oldsun must be installed for them to load.

## Good to know

- **The OP-XY's LFO** reaches all eight controls, page 2 too.
- **CPU safety:** like the OP-XY's own engines, Oldsun never overloads the processor: every voice keeps its full sound, and when the notes held need more work than Oldsun's share, the oldest voices fade out (1.5 ms) to make room, so a new note always plays. About 7 voices of the init sound, 4–5 with every control at the top, up to 16 with light sounds.
- **No clicks:** notes fade in over 1.5 ms (so plucks don't click), and turning CHORUS glides instead of jumping.

## What's in this package

| | |
| --- | --- |
| `oldsun/` | The engine (for open-xy), with the technical README: build, install, details |
| `init-preset/default.json` | The engine's init preset, needed on the device (`content/presets/oldsun/default.json`) |
| `colour/` | The colour screen, optional |
| `presets/` | Oldsun sounds |
| `demos/` | Audio examples |

**No firmware is shared here.** This package holds only Oldsun's own source code, presets and demos: no OS image
(`os.ldr`), no firmware and no files taken from the OP-XY. You build the firmware yourself with open-xy, from the
OS of your own OP-XY.

Experimental: install modified firmware at your own risk, and keep the official OS at hand
(hold COM at power-on for te-boot). MIT license.

"Juno" and "Juno-60" are trademarks of Roland Corporation. Oldsun is not affiliated with or endorsed by Roland
or Boards of Canada; their names are used only to describe the sound that inspired it.
