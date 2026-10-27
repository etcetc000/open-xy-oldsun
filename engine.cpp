// OLDSUN for the OP-XY: a Juno-inspired DCO (sine, triangle, saw, pulse, a second detuned DCO, all
// shaped by SHAPE, and a sub) and its chorus, played
// through a worn cassette: WOW (the slow warp, and each voice drifting on its own), FLUTTER (the fast
// tremble), REVERSE (the note swelling in as if the tape ran backwards) and DEGRADE (saturation, lost highs, hiss,
// dropouts, grain). The OP-XY's own filter, envelopes and LFO follow.
//
// Every control is shaped so its first steps are heard (an ease-out curve: 2x - x^2).
// Fixed point throughout: Q31 audio, u32 phases, 32-bit only (the Blackfin module links no 64-bit
// helpers). No allocation, no floating point.
#include "core.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace oldsun {
namespace {

using opxy::q31::multiply;

const i32 one = 0x7fffffff;

inline i32 sat_add(i32 a, i32 b)
{
    const i32 r = static_cast<i32>(static_cast<u32>(a) + static_cast<u32>(b));
    if (((a ^ r) & (b ^ r)) < 0) return a < 0 ? -one : one;
    return r;
}
inline i32 sat_sub(i32 a, i32 b)
{
    const i32 r = static_cast<i32>(static_cast<u32>(a) - static_cast<u32>(b));
    if (((a ^ b) & (a ^ r)) < 0) return a < 0 ? -one : one;
    return r;
}
inline i32 sat_shl3(i32 x) { return x > (one >> 3) ? one : x < -(one >> 3) ? -one : x * 8; }
inline i32 nonneg(i32 x) { return x < 0 ? 0 : x; }

// the knobs' curve: 2x - x^2, steep at first, gentle at the top
inline i32 ease(i32 x) { return x + multiply(x, one - x); }

inline u32 rnd(u32& s) { s = s * 1664525u + 1013904223u; return s; }

inline i32 sine(u32 phase)
{
    const u32 i = phase >> 24;
    const i32 f = static_cast<i32>((phase & 0xffffffu) << 7);
    return sine_table[i] + multiply(sine_table[i + 1] - sine_table[i], f);
}

inline i32 hash(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<i32>(x);
}

// a smooth random wander, -1..1: a new value every 2^shift samples, eased between them (smoothstep)
inline i32 wander(u32 t, int shift, u32 salt)
{
    const u32 seg = t >> shift;
    const i32 f = static_cast<i32>((t & ((1u << shift) - 1u)) << (31 - shift));
    const i32 f2 = multiply(f, f);
    i32 w = 3 * (f2 >> 2) - 2 * (multiply(f2, f) >> 2);          /* Q29 */
    w = w >= (1 << 29) ? one : w * 4;
    const i32 a = hash(seg * 0x9e3779b9u + salt) >> 1, b = hash((seg + 1u) * 0x9e3779b9u + salt) >> 1;
    return (a + multiply(b - a, w)) * 2;
}

// a sawtooth, -1..1, band-limited at its wrap (polyBLEP in Q15); a division only at the wrap
inline i32 saw(u32 ph, u32 inc)
{
    const i32 y = static_cast<i32>(ph - 0x80000000u);
    const u32 q = inc >> 16;
    if (q == 0) return y;
    if (ph < inc) {
        const i32 x = static_cast<i32>((ph >> 1) / q);
        i32 corr = 2 * x - ((x * x) >> 15) - 32768;
        corr = corr > 32767 ? 32767 : corr < -32767 ? -32767 : corr;
        return sat_sub(y, corr * 65536);
    }
    if (ph > 0xffffffffu - inc) {
        const i32 x = -static_cast<i32>(((0xffffffffu - ph + 1u) >> 1) / q);
        i32 corr = ((x * x) >> 15) + 2 * x + 32768;
        corr = corr > 32767 ? 32767 : corr < -32767 ? -32767 : corr;
        return sat_sub(y, corr * 65536);
    }
    return y;
}

// cents (thousandths) as a Q28 frequency ratio, near 1 (2^28 * ln 2 / 1200000 = 155)
inline i32 cents_q28(i32 millicents) { return 0x10000000 + millicents * 155; }

// The oscillators for one block, into out[]: only the parts the WAVE position uses, each choice made
// once per block (no tests per sample). SP: saw and pulse; SIN, TRI: sine and triangle; FAT: the second
// DCO; SUB: the sub-octave (its flip-flop follows the DCO either way).
struct Osc {
    i32 saw_g, pulse_g, comb, sin_g, tri_g, bend, fat, sub_g;
    u32 inc, inc2, width;
    i32 step, step2;                   /* the increments' slide per sample */
};

template <bool SP, bool SIN, bool TRI, bool FAT, bool SUB>
void oscillators(Voice& v, const Osc& o, i32* out)
{
    u32 ph = v.phase, ph2 = v.phase2, sub = v.sub, inc = o.inc, inc2 = o.inc2;
    const i32 fg = FAT ? multiply(o.saw_g + o.pulse_g, o.fat) : 0;      /* the FAT DCO's level */
    for (unsigned s = 0; s < opxy::block_frames; ++s) {
        const u32 sinc = inc >> 1;
        i32 y = 0;
        if (SP) {
            const i32 sw = saw(ph, inc);
            const i32 shifted = saw(ph + o.width, inc);
            /* the comb, sw - comb x shifted, wraps around past full scale (a fold that is part of the saw's
             * sound since the first version): worked in unsigned so the wrap is defined */
            const i32 combed = static_cast<i32>(static_cast<u32>(sw) - static_cast<u32>(multiply(shifted, o.comb)));
            y = multiply((sw >> 1) - (shifted >> 1), o.pulse_g) + multiply(combed, o.saw_g);
        }
        if (SIN || TRI) {
            const u32 fold0 = ph < 0x80000000u ? ph : 0xffffffffu - ph;
            const u32 bent = ph + static_cast<u32>(multiply(static_cast<i32>(fold0 * 2u - 0x80000000u), o.bend));
            if (TRI) {
                const u32 fold = bent < 0x80000000u ? bent : 0xffffffffu - bent;
                y += multiply(static_cast<i32>(fold * 2u - 0x80000000u), o.tri_g);
            }
            if (SIN) y += multiply(sine(bent), o.sin_g);
        }
        if (FAT) {
            y += multiply(saw(ph2, inc2), fg);
            ph2 += inc2;
            inc2 += static_cast<u32>(o.step2);
        }
        y >>= 1;
        if (SUB) {
            const u32 sp = (ph >> 1) + (sub << 31);
            y += multiply((saw(sp, sinc) >> 1) - (saw(sp + 0x80000000u, sinc) >> 1), o.sub_g) >> 2;
        }
        if (ph + inc < ph) sub ^= 1u;
        ph += inc;
        inc += static_cast<u32>(o.step);
        out[s] = y;
    }
    v.phase = ph; v.phase2 = ph2; v.sub = sub;
}

/* the block for what the WAVE position uses, by its parts (1 saw and pulse, 2 sine, 4 triangle, 8 FAT;
 * then x 2 with SUB): sine, sine and triangle, triangle, triangle and saw, saw and pulse, with FAT;
 * anything else (never reached) takes the one with everything, which is right for any gains */
typedef void (*OscBlock)(Voice&, const Osc&, i32*);
const OscBlock osc_blocks[32] = {
    oscillators<false, false, false, false, false>, oscillators<true, false, false, false, false>,
    oscillators<false, true, false, false, false>, oscillators<true, true, true, true, false>,
    oscillators<false, false, true, false, false>, oscillators<true, false, true, false, false>,
    oscillators<false, true, true, false, false>, oscillators<true, true, true, true, false>,
    oscillators<true, true, true, true, false>, oscillators<true, false, false, true, false>,
    oscillators<true, true, true, true, false>, oscillators<true, true, true, true, false>,
    oscillators<true, true, true, true, false>, oscillators<true, true, true, true, false>,
    oscillators<true, true, true, true, false>, oscillators<true, true, true, true, false>,
    oscillators<false, false, false, false, true>, oscillators<true, false, false, false, true>,
    oscillators<false, true, false, false, true>, oscillators<true, true, true, true, true>,
    oscillators<false, false, true, false, true>, oscillators<true, false, true, false, true>,
    oscillators<false, true, true, false, true>, oscillators<true, true, true, true, true>,
    oscillators<true, true, true, true, true>, oscillators<true, false, false, true, true>,
    oscillators<true, true, true, true, true>, oscillators<true, true, true, true, true>,
    oscillators<true, true, true, true, true>, oscillators<true, true, true, true, true>,
    oscillators<true, true, true, true, true>, oscillators<true, true, true, true, true>,
};

/* the tape's front, sample by sample in one loop (each stage's filter a chain of its own, so they
 * overlap): REVERSE's swell (when on) into the chorus's line, and the chorus's tap (when on): a bucket
 * brigade, one tap, its two filters as one pole, mixed with the dry */
struct Front {
    bool rev;
    i32 g, gstep, k, kstep, lp;                       /* REVERSE */
    i32 bbd, dryg, wetg, dry_step, wetg_step, dpos, dstep;   /* the chorus */
    u32 w;
};

template <bool REV, bool CHORUS>
void front(Voice& v, Front& f, i32* y)
{
    i32 g = f.g, k = f.k, lp = f.lp, bbd = f.bbd, dryg = f.dryg, wetg = f.wetg, dpos = f.dpos;
    u32 w = f.w;
    for (unsigned s = 0; s < opxy::block_frames; ++s, ++w) {
        i32 x = y[s];
        if (REV) {
            g += f.gstep; k += f.kstep;
            lp += multiply(x - lp, k);
            x = multiply(lp, g);
        }
        v.delay[w & (chorus_len - 1)] = static_cast<i16>(x >> 16);
        if (CHORUS) {
            const u32 di = static_cast<u32>(dpos >> 16), fr = static_cast<u32>(dpos & 0xffff);
            const i32 a = v.delay[(w - di) & (chorus_len - 1)];
            const i32 b = v.delay[(w - di - 1) & (chorus_len - 1)];
            const i32 tap = (a + (((b - a) * static_cast<i32>(fr >> 2)) >> 14)) * 65536;
            bbd += multiply(tap - bbd, 0x5c000000);  /* the BBD's filters: about 9 kHz */
            dryg += f.dry_step; wetg += f.wetg_step;
            x = multiply(x, dryg) + multiply(bbd, wetg);   /* each at most 0.71 of full scale (with DEGRADE,
                                                            * its drive taken in the gains) */
            dpos += f.dstep;
        }
        y[s] = x;
    }
    f.lp = lp; f.bbd = bbd; f.w = w;
}

void begin(Voice& v, Shared& sh)
{
    v.seed = sh.seed = sh.seed * 747796405u + 2891336453u;
    v.salt = hash(v.seed);
    v.detune = cents_q28(static_cast<i32>(rnd(v.seed) >> 20) - 2048);   /* about +-2 cents */
    v.phase = rnd(v.seed);
    v.phase2 = rnd(v.seed);
    v.shape_ph = sh.epoch * opxy::block_frames * 29218u;
    v.sub = 0;
    v.worn = 0;
    v.epoch = sh.epoch - 1u;                         /* joins the block in progress */
    v.age = 0;
    v.rev_t = v.rev_looped = 0;
    v.tier = 0;
    v.rev_g = 0;
    v.rev_lp = 0;
    v.started = 1;
}

} // namespace

inline void start(Voice& voice, Shared&, u32)
{
    int i;
    voice.started = 0;
    voice.write = 0;
    voice.tone = voice.tone2 = voice.hiss = voice.bbd = voice.bump_hi = voice.bump_lo = voice.cpos = voice.cwet = voice.dc = voice.dslope = 0;
    voice.drop = voice.drop_target = one;
    voice.drop_left = 0;
    voice.held = 0;
    voice.hold = 0;
    for (i = 0; i < chorus_len; i++) voice.delay[i] = 0;
}
inline void initialize(Voice& voice, Shared& shared, u32 owner)
{
    start(voice, shared, owner);
    if (owner == 1) {                                 /* the engine loaded on a track: a fresh budget */
        shared.used = shared.last_used = shared.fresh = shared.count = shared.need = 0;
        voice.epoch = shared.epoch - 1u;
        shared.max_age = shared.last_max_age = 0;
    }
}
inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& v, Shared& sh, u32, opxy::Output output, opxy::Parameters params,
                   opxy::Pitch pitch)
{
    unsigned s;
    if (!v.started) begin(v, sh);

    const i32 wave = nonneg(params[P_WAVE]), pwm = nonneg(params[P_SHAPE]);
    const i32 subl = ease(nonneg(params[P_SUB]));
    const i32 chorus = nonneg(params[P_CHORUS]);
    const i32 wow = ease(nonneg(params[P_WOW])), flutter = ease(nonneg(params[P_FLUTTER]));
    const i32 rev = ease(nonneg(params[P_REVERSE])), deg = nonneg(params[P_DEGRADE]);
    /* the blocks, counted: a voice about to play a second time in the same count means the OP-XY has
     * moved on to the next block (a note starting midway joins the count in progress). The tape's clock
     * comes from it, so every voice rides the same wow and flutter */
    const bool next_block = v.epoch == sh.epoch;
    if (next_block) ++sh.epoch;
    v.epoch = sh.epoch;
    const u32 T = sh.epoch * opxy::block_frames;      /* the tape's clock, the same for every voice */

    /* the CPU budget, as the OP-XY's own engines do it: every voice keeps its whole sound, and when the
     * notes held need more work than Oldsun's share of the processor, the oldest voices are faded out
     * (1.5 ms) to make room, so a new note always plays. Each voice costs by what it has on (in rough
     * units of work per block); the budget, measured on an OP-XY (glitches began near 220 units), is seven
     * voices of the init sound, five with every control at the top, sixteen at most. Turning knobs up under held notes frees room the same way. */
    if (next_block) {
        if (sh.used > 165u + 24u) sh.need += sh.used - 165u;            /* knobs turned up: make room */
        sh.last_used = sh.used; sh.used = 0; sh.fresh = 0; sh.count = 0;
        sh.last_max_age = sh.max_age; sh.max_age = 0;
    }
    const bool lite = false;
    {
        const i32 w = wave >> 16;
        /* weights measured per voice (with every control at the top a voice is 32 units, the scale the
         * budget was calibrated in on an OP-XY: glitches near 220) */
        const u32 cost = 6u + (w >= 26214 ? 2u : 0u) + (subl ? 3u : 0u) + (chorus ? 4u : 0u) + (rev ? 3u : 0u)
                       + ((wow | flutter) ? 1u : 0u) + (deg ? (deg > 0x39999999 ? 13u : 9u) : 0u);
        const u32 budget = 165u;
        if (v.tier == 0) {                            /* a new note always plays; room is made for it */
            const u32 committed = sh.last_used + sh.fresh;
            v.tier = 1u;
            sh.fresh += cost;
            if (committed + cost > budget) sh.need += committed + cost - (committed > budget ? committed : budget);
            if (sh.count >= 16u) sh.need += cost;
        }
        if (v.tier == 3u) {
            for (s = 0; s < opxy::block_frames; ++s) output[s] = 0;
            v.age += opxy::block_frames;
            return;
        }
        if (v.tier == 1u && sh.need && v.age > 0 && v.age >= sh.last_max_age) {   /* the oldest: fade it out */
            v.tier = 4u;
            sh.need = sh.need > cost ? sh.need - cost : 0u;
        }
        ++sh.count;
        sh.used += cost;
        if (v.tier == 1u && v.age > sh.max_age) sh.max_age = v.age;
    }

    /* the tape's pitch, in thousandths of a cent */
    i32 mc = 0;
    if (wow) {                                        /* a warped reel: 0.55 Hz and 1.3 Hz, wandering, sagging
                                                       * more than it rises (the motor drags): up to 90 cents;
                                                       * and every voice drifting on its own, like old DCOs */
        i32 w = multiply(sine(T * 53568u), 0x3999999a) + multiply(sine(T * 126608u + 0x40000000u), 0x1999999a)
              + multiply(wander(T, 14, 0x3a7u), 0x2ccccccd);
        if (w > 0) w = multiply(w, 0x4ccccccd);
        mc += multiply(w, multiply(wow, 110000));
        mc += multiply(wander(T, 16, v.salt), multiply(wow, 30000));     /* and each voice its own way: up to 30 */
    }
    i32 flut_gain = one;
    if (flutter) {
        /* the capstan, a little eccentric: a wobble with its 2nd and 3rd harmonics, at 4.3 Hz (where the ear
         * is most sensitive) turning into 8.1 Hz as you turn; its depth breathes at random, the pinch roller
         * adds an irregular sway, and the tape scraping over the head a fine grain (each voice its own).
         * Up to about 55 cents; the level dips with it, slightly (about 1 dB), as the tape loses the head */
        const u32 pa = T * 418781u, pb = T * 788867u;
        const i32 cap_a = multiply(sine(pa), 0x4ccccccd) + multiply(sine(pa * 2u + 0x30000000u), 0x2147ae14)
                        + multiply(sine(pa * 3u + 0x60000000u), 0x0f5c28f6);
        const i32 cap_b = multiply(sine(pb), 0x4ccccccd) + multiply(sine(pb * 2u + 0x50000000u), 0x2147ae14)
                        + multiply(sine(pb * 3u + 0x20000000u), 0x0f5c28f6);
        const i32 cap = multiply(cap_a, one - flutter) + multiply(cap_b, flutter);
        const i32 breath = 0x4ccccccd + multiply(wander(T, 14, 0x5au), 0x33333333);      /* 0.2..1 */
        const i32 f = (multiply(cap, breath) >> 1) + (wander(T, 12, 0xc3u) >> 3);
        const i32 scrape = static_cast<i32>(hash(T * 0x9e3779b9u + v.salt));
        mc += multiply(f, multiply(flutter, 90000)) + multiply(scrape, multiply(flutter, 3000));
        flut_gain = one - multiply(multiply(f + 0x40000000, flutter), 0x0f5c28f6);
    }
    const i32 ratio = multiply(v.detune, cents_q28(mc)) * 8;     /* Q28 */

    /* WAVE, a fifth of the knob each: sine into triangle, into saw, saw joined by the pulse (the Juno's
     * two buttons), saw out to the pulse alone; then FAT: saw and pulse again with a second DCO
     * joining, a little detuned (3 to 12 cents), beating against the first */
    i32 sin_g = 0, tri_g = 0, saw_g = 0, pulse_g = 0, fat = 0;
    {
        const i32 w = wave >> 16;                     /* 0..32767 */
        const int zone = w < 6554 ? 0 : w < 13107 ? 1 : w < 19661 ? 2 : w < 26214 ? 3 : 4;
        const i32 into = (w - zone * 6554) * 5;
        const i32 f = into > 32767 ? one : into < 0 ? 0 : into << 16;
        if (zone == 0) { sin_g = ease(one - f); tri_g = ease(f); }
        else if (zone == 1) { tri_g = ease(one - f); saw_g = ease(f); }
        else if (zone == 2) { saw_g = one; pulse_g = f; }
        else if (zone == 3) { saw_g = one - f; pulse_g = one; }
        else { saw_g = f; pulse_g = one; fat = lite ? 0 : f; }
        const i32 both = multiply(saw_g < pulse_g ? saw_g : pulse_g, 0x26666666);     /* -0.3 when both */
        saw_g = multiply(saw_g, one - both) >> 1;
        pulse_g = multiply(pulse_g, one - both) >> 1;
        tri_g >>= 1;
        sin_g >>= 1;
        if (fat) {                                    /* the pair a little quieter than twice one */
            const i32 down = one - multiply(fat, 0x2ccccccd);
            saw_g = multiply(saw_g, down); pulse_g = multiply(pulse_g, down);
        }
    }

    /* SHAPE, on every wave, moving from the first step as the Juno's PWM on its LFO: turning it bends
     * the wave further and lets a free LFO sweep it deeper and faster, from a
     * slow breath (0.35 Hz) to 3 Hz. The pulse's width (50% to 6%); the saw a second, shifted saw
     * taken from it (a moving comb); sine and triangle a bent phase (brighter, nasal) */
    i32 width_q;                                      /* Q31, 0..0.5 */
    {
        const i32 e = ease(pwm);
        const i32 lfo = (sine(v.shape_ph) >> 1) + 0x40000000;                       /* 0..1 */
        const u32 rate = 34088u + static_cast<u32>(multiply(pwm, pwm) >> 16) * 9u;  /* 0.35 Hz to 3 Hz */
        v.shape_ph += rate * opxy::block_frames;
        const i32 bent = multiply(e, 0x4ccccccd) + multiply(multiply(e, 0x33333333), lfo);  /* 0..1 */
        width_q = 0x40000000 - multiply(bent, 0x3851eb85);
    }
    const i32 amount = (0x40000000 - width_q) >= 0x3851eb85 ? one : static_cast<i32>(
        static_cast<u32>(0x40000000 - width_q) / 0x3851ebu * 0x800000u);            /* 0..1: how far it is shaped */
    const i32 comb = multiply(amount, 0x5999999a);   /* the saw's: up to 0.7 */
    const i32 bend = multiply(amount, 0x26666666);   /* sine and triangle: up to 0.3 of a cycle */
    const u32 width = static_cast<u32>(width_q) << 1;

    /* the chorus, after the Juno-60's bucket brigade, for a mono output: one tap swept by a triangle
     * (the Juno's two opposite taps would cancel their movement when summed to mono). I 0.51 Hz and
     * II 0.86 Hz, deep (about 1-6 ms); I+II both taps at once, I's slower sweep and a 6 Hz warble.
     * A third of the knob each; within each, deeper and wetter as you turn. The wet path is dark
     * (the BBD's filters) and carries its faint hiss. */
    const i32 c = chorus >> 16;                       /* 0..32767 */
    const int mode = c < 10923 ? 0 : c < 21845 ? 1 : 2;
    const i32 z = (c - mode * 10923) * 3;             /* 0..32767 within the mode */
    const i32 wet = (c == 0 || lite) ? 0 : 26214 + z * 6553 / 32767;           /* Q15: 0.8..1 */
    const i32 cdepth = 120 * (16384 + z / 2) / 32767;                /* samples: 60..120 */
    const u32 crate = mode == 0 ? 49961u : mode == 1 ? 84049u : 49961u;

    /* DEGRADE, from clean to a ruined cassette. Its first 30% is the tape's colour: the head bump (the
     * lows around 60-120 Hz swelling, up to about +4 dB) with a gentle saturation; past it the wear:
     * more drive, lost highs, hiss, then dropouts from 45% and grain from 75% */
    const i32 dg = deg >> 16;                         /* 0..32767 */
    const i32 d = lite ? 0 : dg;
    /* DEGRADE turned on or off: its tape is never quite transparent (even at its first step it saturates
     * a little), so the block where it starts or stops crossfades from the dry to the tape or back,
     * instead of jumping (a click). A note starts with it as it is */
    if (v.age == 0) v.worn = d ? 1u : 0u;
    const bool wear = d || v.worn;
    const bool turning = (d != 0) != (v.worn != 0);
    const i32 col = dg >= 9830 ? 32767 : dg * 32767 / 9830;                  /* the colour, Q15 */
    const i32 bump = col << 16;                       /* its gain, x 1.19 in the loop */
    const i32 e = ease((dg > 9830 ? (dg - 9830) * 32767 / 22937 : 0) << 16) >> 16;     /* the wear, Q15 */
    const i32 hiss_level = (e * e / 32767) * 1966;    /* Q31: 0.03 at the top */
    const i32 drive = 0x10000000 + col * 4915 + e * 36045;                  /* Q28: 1..6 */
    const i32 den = 32768 + col + col / 12 + e * 3;
    const i32 post = (32767 * 32768) / den << 16;    /* its loudness back */
    i32 tone_a;                                       /* 1 (open) .. 0.15 (about 1 kHz) */
    {
        const i32 open = one - (e << 16), half = one - (e << 15);
        tone_a = 0x13333333 + multiply(multiply(open, half), 0x6ccccccc);
    }
    /* the colour also makes the tape asymmetric (an even, warm 2nd harmonic, about -24 dB at 30% on a
     * sine) and rounds the top a little, as even a new cassette does (about -3 dB at 9 kHz) */
    const i32 asym = multiply(col << 16, 0x2ccccccd) + multiply(e << 16, 0x13333333);
    tone_a = multiply(tone_a, one - multiply(col << 16, 0x13333333));
    const int crush = d > 24576 ? (d - 24576) / 2048 + 1 > 4 ? 4 : (d - 24576) / 2048 + 1 : 0;   /* from 75%: 1..4 */

    /* REVERSE: the note as if the tape ran backwards, swelling in from nothing (a decay played back,
     * x^2) and opening as it rises, then full for as long as it is held: no cuts. The knob sets the
     * swell's length, 0.1 s to 2.5 s; past half way the swell also starts flat and rises into tune, like a
     * tape coming up to speed. A note swells only if REVERSE was on when it started: turned on
     * under a held note it leaves that note full; turned off, the note eases back to full */
    i32 rev_end = one, rev_step = 0, rev_dark = 0, tape_start = 0;
    if (v.age == 0) v.rev_looped = rev ? 1u : 0u;    /* (here: whether this note swells) */
    if (v.rev_looped && rev) {
        const u32 len = 4410u + static_cast<u32>(rev >> 16) * 105840u / 32767u;
        const u32 x15 = v.rev_t >= len ? 32767u : v.rev_t * 32768u / len;
        const i32 x = static_cast<i32>(x15 << 16);
        rev_end = multiply(x, x);
        rev_dark = multiply(rev, 0x6ccccccd);
        const i32 knob = nonneg(params[P_REVERSE]);
        if (knob > 0x40000000) {                      /* past half way, the tape starting: the swell begins
                                                       * flat and rises into tune, up to 2 semitones down */
            const i32 left = one - x;
            tape_start = -multiply(multiply(left, left), multiply((knob - 0x40000000) * 2, 200000));
        }
    }
    if (v.rev_t < 0x100000u) v.rev_t += opxy::block_frames;
    rev_step = (rev_end - v.rev_g) / static_cast<i32>(opxy::block_frames);

    const i32 played = static_cast<i32>(pitch[0] > 0x7fffffffu ? 0x7fffffffu : pitch[0]);
    /* the pitch this block ends on; the oscillators slide to it sample by sample from where the last
     * block ended, so wow, flutter and the scrape move it smoothly, not in 1.5 ms steps */
    const u32 inc = static_cast<u32>(multiply(played, tape_start ? multiply(ratio, cents_q28(tape_start)) * 8 : ratio)) << 3;
    const u32 inc2 = fat ? static_cast<u32>(multiply(static_cast<i32>(inc >> 1), cents_q28(3000 + (fat >> 16) * 9000 / 32767))) << 4 : 0;
    const u32 inc_from = (v.age == 0 || !v.last_inc) ? inc : v.last_inc;
    const u32 inc2_from = (v.age == 0 || !v.last_inc2 || !inc2) ? inc2 : v.last_inc2;
    v.last_inc = inc; v.last_inc2 = inc2;

    /* the work in stages over the block, each a tight loop: the oscillators, REVERSE, the chorus, the
     * wear, the flutter's level and the output */
    i32* const y = output;                            /* the block, built in place */
    {
        Osc o;
        o.saw_g = saw_g; o.pulse_g = pulse_g; o.comb = saw_g ? comb : 0; o.sin_g = sin_g; o.tri_g = tri_g;
        o.bend = bend; o.fat = fat; o.sub_g = subl; o.inc = inc_from; o.inc2 = inc2_from; o.width = width;
        o.step = static_cast<i32>(inc - inc_from) / static_cast<i32>(opxy::block_frames);
        o.step2 = static_cast<i32>(inc2 - inc2_from) / static_cast<i32>(opxy::block_frames);
        osc_blocks[(saw_g | pulse_g ? 1 : 0) + (sin_g ? 2 : 0) + (tri_g ? 4 : 0) + (fat ? 8 : 0) + (subl ? 16 : 0)](v, o, y);
    }
    /* REVERSE's swell and darkening, worked in the chorus's loop below: its gain and the low-pass's
     * coefficient drawn straight across the block */
    Front f = Front();
    f.rev = rev_end != one || v.rev_g != one;         /* swelling, or easing back to full */
    if (f.rev) {
        f.g = v.rev_g; f.lp = v.rev_lp; f.gstep = rev_step;
        f.k = one - multiply(rev_dark, one - f.g);
        f.kstep = (one - multiply(rev_dark, one - rev_end) - f.k) / static_cast<i32>(opxy::block_frames);
    }
    bool chorus_on = false;
    {                                                 /* chorus: the line always fills, the tap only when on */
        u32 w = v.write;
        if (wet || v.cwet) {                         /* on, or fading out */
            /* the dry dips as the wet comes in (0.71 each at full), so turning it on or off is smooth: both
             * gains drawn straight across the block */
            const i32 wet_from = v.cwet << 16, wet_to = wet << 16;
            i32 dryg = one - multiply(wet_from, 0x25810625), wetg = multiply(wet_from, 0x5a82799a);
            i32 dry_step = (one - multiply(wet_to, 0x25810625) - dryg) / static_cast<i32>(opxy::block_frames);
            i32 wetg_step = (multiply(wet_to, 0x5a82799a) - wetg) / static_cast<i32>(opxy::block_frames);
            v.cwet = wet;
            if (wear && !turning) {                  /* the wear's drive (Q28) taken here, with the gains */
                dryg = multiply(dryg, drive); wetg = multiply(wetg, drive);
                dry_step = multiply(dry_step, drive); wetg_step = multiply(wetg_step, drive);
            }
            /* the sweep, worked out at the block's two ends and drawn straight between them */
            i32 ends[2];
            for (int k = 0; k < 2; k++) {
                const u32 t = T + static_cast<u32>(k) * opxy::block_frames;
                const u32 p = t * crate;
                const i32 tri = static_cast<i32>(p < 0x80000000u ? p : 0xffffffffu - p) - 0x40000000;
                ends[k] = (154 << 16) + (tri >> 15) * cdepth * 2;
                if (mode == 2) {                     /* I+II: both sweeps in one tap, and a 6 Hz warble */
                    const u32 q = t * 584000u;
                    const i32 tri2 = static_cast<i32>(q < 0x80000000u ? q : 0xffffffffu - q) - 0x40000000;
                    ends[k] += (tri2 >> 15) * 28;
                }
            }
            /* from where the last block left off toward the sweep's end, at most 3 samples a block: the
             * sweep itself never needs more, and a turn of the knob (depth, or a change of mode) glides
             * there like a tape changing speed instead of jumping (a click) */
            i32 target = ends[1];
            if (!v.cpos) v.cpos = ends[0];
            {
                const i32 most = 3 << 16, move = target - v.cpos;
                target = v.cpos + (move > most ? most : move < -most ? -most : move);
            }
            f.dstep = (target - v.cpos) / static_cast<i32>(opxy::block_frames);
            f.dpos = v.cpos;
            v.cpos = target;
            f.bbd = v.bbd; f.dryg = dryg; f.wetg = wetg; f.dry_step = dry_step; f.wetg_step = wetg_step;
            chorus_on = true;
        }
        f.w = w;
        if (f.rev) { if (chorus_on) front<true, true>(v, f, y); else front<true, false>(v, f, y); }
        else { if (chorus_on) front<false, true>(v, f, y); else front<false, false>(v, f, y); }
        if (chorus_on) v.bbd = f.bbd;
        if (f.rev) v.rev_lp = f.lp;
        v.write = f.w;
    }
    i32 dry[opxy::block_frames];                      /* the block without the tape, when crossfading */
    if (turning) for (s = 0; s < opxy::block_frames; ++s) dry[s] = y[s];
    if (wear) {                                       /* the tape's wear */
        i32 tone = v.tone, tone2 = v.tone2, bump_hi = v.bump_hi, bump_lo = v.bump_lo, dc = v.dc;
        u32 seed = v.seed;
        /* saturation x - x^3/3 plus the asymmetry, as x + x^2 (asym - x/3), worked at half scale so no step
         * can overflow (no tests per sample). Everything after it is linear up to the grain, so the tape
         * runs at that half scale and its level (post, x 2) comes back once, at the end, with the flutter's.
         * The offset and the head bump live below 150 Hz: their filters step every 4 samples, on the 4
         * samples' mean (the same filters at a quarter of the rate: coefficients 1 - (1 - a)^4). In between,
         * the bump (the band between them, g (hi - lo)) moves sample by sample by its first-order step,
         * g (a_hi - a_lo) h - g (a_hi hi - a_lo lo): one multiply a sample instead of four */
        if (!chorus_on || turning) for (s = 0; s < opxy::block_frames; ++s) y[s] = multiply(y[s], drive);   /* the drive, Q28
                                                       * (with the chorus on, taken in its gains) */
        const i32 ga = multiply(bump, 0x02a628ab), gb = multiply(bump, 0x00e6f0be), c1 = ga - gb;   /* g a/4 */
        i32 dslope = v.dslope;
        for (s = 0; s < opxy::block_frames; s += 4) {
            i32 mk = multiply(bump_hi - bump_lo, bump), sum = 0, dk = dc;
            const i32 q = multiply(bump_hi, ga) - multiply(bump_lo, gb);
            for (int k = 0; k < 4; ++k) {
                const i32 x = sat_shl3(y[s + k]);
                dk += dslope;
                const i32 h = (x >> 1) + (multiply(multiply(x, x), asym - multiply(x, 0x2aaaaaab)) >> 1) - dk;
                sum += h >> 2;
                mk += multiply(h, c1) - q;
                tone += multiply(h + mk + (mk >> 2) - (mk >> 4) - tone, tone_a);   /* the highs lost: two poles */
                tone2 += multiply(tone - tone2, tone_a);
                y[s + k] = tone2;
            }
            dslope = multiply(sum + (((dk - dc) >> 3) * 5), 0x0173e094) >> 2;
            dc += dslope * 4;                        /* the asymmetry's offset (below 20 Hz) out; between its
                                                      * steps, drawn on along its last one */
            bump_hi += multiply(sum - bump_hi, 0x0a98a2ac);     /* the head bump: below 150 Hz ... */
            bump_lo += multiply(sum - bump_lo, 0x039bc2f9);     /* ... but above 50 Hz */
        }
        v.tone = tone; v.tone2 = tone2; v.bump_hi = bump_hi; v.bump_lo = bump_lo; v.dc = dc; v.dslope = dslope;
        /* then the level back (x 2 post), with the flutter's; and the hiss, only once the wear begins, its
         * level coming and going at random, slowly, as on a real tape (0.2 to 1, the same for every voice:
         * one cassette); added at the tape's half scale, so over 2 x post (whose divisor is `den`) */
        const i32 g = flutter ? multiply(post, flut_gain) : post;
        if (hiss_level) {
            const i32 drift = (wander(T, 15, 0x2bu) >> 1) + 0x40000000;                 /* 0..1 */
            const i32 level = (multiply(hiss_level, 0x1999999a + multiply(drift, 0x66666666)) >> 8) * (den >> 8);
            i32 hiss = v.hiss;
            for (s = 0; s < opxy::block_frames; ++s) {
                hiss += ((static_cast<i32>(rnd(seed)) >> 1) - hiss) >> 2;
                const i32 r = multiply(y[s] + multiply(hiss, level), g);
                y[s] = r > 0x3fffffff ? one : r < -0x3fffffff ? -one : r * 2;
            }
            v.hiss = hiss;
        } else {
            for (s = 0; s < opxy::block_frames; ++s) {
                const i32 r = multiply(y[s], g);
                y[s] = r > 0x3fffffff ? one : r < -0x3fffffff ? -one : r * 2;
            }
        }
        if (crush) {                                  /* grain: fewer bits, a held sample */
            const i32 mask = ~((1 << (19 + crush)) - 1);
            for (s = 0; s < opxy::block_frames; ++s) {
                if (++v.hold > static_cast<u32>(crush)) { v.hold = 0; v.held = y[s] & mask; }
                y[s] = v.held;
            }
        }
        if (d > 14746) {                              /* dropouts, from 45% */
            for (s = 0; s < opxy::block_frames; ++s) {
                if (!v.drop_left && rnd(seed) < static_cast<u32>((d - 14746) * 4)) {
                    v.drop_left = 1323 + (rnd(seed) >> 22) * 4;               /* 30-120 ms */
                    v.drop_target = 0x20000000 + static_cast<i32>(rnd(seed) >> 2);
                }
                if (v.drop_left) { if (!--v.drop_left) v.drop_target = one; }
                else if (v.drop == one) continue;     /* (no dropout: the level is whole) */
                v.drop += (v.drop_target - v.drop) >> 9;
                if (!v.drop_left && one - v.drop < 512) v.drop = one;     /* back, the last step */
                y[s] = multiply(y[s], v.drop);
            }
        }
        v.seed = seed;
    }
    if (flutter && !wear) for (s = 0; s < opxy::block_frames; ++s) y[s] = multiply(y[s], flut_gain);   /* the flutter's level */
    if (turning) {                                    /* in (DEGRADE on) or out, over the block */
        for (s = 0; s < opxy::block_frames; ++s) {
            const i32 a = flutter ? multiply(dry[s], flut_gain) : dry[s];
            const i32 f = static_cast<i32>(d ? s : opxy::block_frames - 1 - s) << 25;
            y[s] = a + multiply((y[s] >> 1) - (a >> 1), f) * 2;
        }
        v.worn = d ? 1u : 0u;
    }
    {                                                 /* at most half scale out, the last eighth rounded into it
                                                       * (a soft knee, x - (x - 0.4375)^2 / (4 x 0.0625)), so loud
                                                       * notes bend into the limit instead of hitting a wall, which
                                                       * crackled: untouched below 0.4375 (only looked at when a
                                                       * sample of the block passes it) */
        u32 over = 0;
        for (s = 0; s < opxy::block_frames; ++s) over |= static_cast<u32>(y[s]) + 0x38000000u > 0x70000000u ? 1u : 0u;
        if (over)
            for (s = 0; s < opxy::block_frames; ++s) {
                const i32 v = y[s];
                const i32 a = v < 0 ? (v < -one ? one : -v) : v;
                if (a > 0x38000000) {
                    const i32 e = a - 0x38000000;
                    const i32 lim = e >= 0x10000000 ? 0x3fffffff : a - (multiply(e, e) << 2);
                    y[s] = v < 0 ? -lim : lim;
                }
            }
    }
    if (v.tier == 4u) {                               /* stolen: out over this block, then silent */
        for (s = 0; s < opxy::block_frames; ++s) y[s] = (y[s] >> 6) * static_cast<i32>(opxy::block_frames - s);
        v.tier = 3u;
    }
    if (v.age == 0)                                   /* a note's first block fades in (1.5 ms): the DCO runs
                                                       * free, so a plucked note would otherwise click */
        for (s = 0; s < opxy::block_frames; ++s) y[s] = (y[s] >> 6) * static_cast<i32>(s);
    v.rev_g = rev_end;
    v.age += opxy::block_frames;
}

} // namespace oldsun
