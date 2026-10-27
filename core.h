#pragma once
#include <opxy/audio.hpp>

namespace oldsun {
using opxy::i32;
using opxy::u32;
using opxy::i16;

// the controls, in parameter order: page 1 (the synth), then page 2 (the tape)
enum { P_WAVE, P_SHAPE, P_SUB, P_CHORUS, P_WOW, P_FLUTTER, P_REVERSE, P_DEGRADE };

const int chorus_len = 512;             // the chorus's delay line: 11.6 ms

struct Voice {
    u32 started;                        // set up on the first block (the pitch arrives there)
    u32 phase;                          // the DCO
    u32 phase2;                         // FAT: the second DCO, a little detuned
    u32 last_inc, last_inc2;            // the increments the last block ended on (pitch moves smoothly)
    u32 shape_ph;                       // SHAPE's LFO
    u32 sub;                            // its sub-octave: which half of its cycle (0, 1)
    i32 detune;                         // this voice's own tuning, Q28 ratio
    u32 salt;                           // this voice's own drift
    u32 tier;                           // 0 new, 1 sounding, 3 stolen (silent), 4 fading out this block
    u32 rev_t, rev_looped;              // REVERSE: samples into the swell, and whether it has cut once
    i32 rev_g, rev_lp;                  // its gain (Q31) at the block's start, its darkening low-pass
    u32 worn, age;                      // whether DEGRADE was on last block, samples since the note started
    u32 epoch;                          // the last block it played in
    u32 seed;
    i16 delay[chorus_len];              // the chorus
    u32 write;
    i32 bbd;                            // the chorus's wet path: the bucket brigade's low-pass
    i32 cpos;                           // where the chorus reads, Q16 samples (it glides, never jumps)
    i32 cwet;                           // its level now, Q15 (it fades, never jumps)
    i32 tone, tone2;                    // the tape's tone (two one-pole low-passes)
    i32 bump_hi, bump_lo;               // the head bump: the band between two low-passes
    i32 dc, dslope;                     // the offset the tape's asymmetry leaves, taken out, and its last step
    i32 hiss;                           // the hiss, smoothed
    i32 drop, drop_target;              // a dropout's gain (Q31) and where it goes
    u32 drop_left;                      // samples until it ends
    i32 held;                           // the sample held (degrade's grain)
    u32 hold;
};

// one for every voice: the clock the tape follows (every voice the same cassette)
struct Shared {
    u32 clock;
    u32 seed;
    u32 epoch, count, used;             // the block (counted), its voices so far and their work (the CPU budget)
    u32 last_used, fresh;               // the last block's work, and the voices that started since
    u32 need;                           // work to free: the oldest voices are faded out to make room
    u32 max_age, last_max_age;          // the oldest sounding voice's age, this block and the last
};
}
