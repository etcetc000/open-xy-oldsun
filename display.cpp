// The screen: a cassette at sunset. Two reels turn, the left emptying as the right fills; between them
// the tape is the waveform (WAVE, SHAPE, SUB) with its chorus copy behind it in blue (CHORUS). Behind,
// a hexagon sun over the mountains. The tape knobs each show: WOW makes the reels lurch, the tape bend
// and its copy wander off, FLUTTER makes it shiver, REVERSE runs the reels backwards, DEGRADE dims
// the sun, scratches the tape and brings the dust. A note flashes the sun and sends out two rings.
// Two dots in the label: the page (encoder 1 shows WAVE, or WOW).
#include "display.h"
#include "tables.h"

namespace oldsun {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

enum { N_WAVE, N_CHORUS, N_SUN, N_PACK_L, N_SLOT_L, N_PACK_R = 7, N_SLOT_R, N_PAGE = 11, N_RING = 13, N_SPECK = 15 };
enum { C_BLACK, C_DARK, C_GREY, C_WHITE, C_ORANGE, C_BLUE, C_YELLOW, C_DEEP, C_BRIGHT, C_MID, C_LIGHT, C_SUN,
       C_EMBER, C_NAVY };
const i32 RL = 88, RR = 392, RY = 120, SUN_X = 240, SUN_Y = 102, TAPE_Y = 124;
/* the three stars that always shine */
const i32 STAR_X[3] = {164, 318, 286}, STAR_Y[3] = {78, 70, 92};

inline i32 q10(i32 q31) { return q31 < 0 ? 0 : q31 >> 21; }           /* 0..1023 */
inline i32 sin10(u32 phase) { return sine_table[phase >> 24] >> 21; }    /* -1024..1023 */
inline u32 hash(u32 x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

/* the waveform at a phase 0..1023, Q10 -1024..1024: as the engine, sine into triangle, saw, saw and
 * pulse, pulse, then the two DCOs (drawn as saw and pulse) */
i32 osc_at(i32 ph, i32 wave, i32 width)
{
    const i32 sn = sin10(static_cast<u32>(ph) << 22);
    const i32 tri = ph < 512 ? -1024 + 4 * ph : 3072 - 4 * ph;
    const i32 saw = 1024 - 2 * ph;
    const i32 sq = ph < width ? 1024 : -1024;
    const int zone = wave < 205 ? 0 : wave < 410 ? 1 : wave < 614 ? 2 : wave < 819 ? 3 : 4;
    const i32 f = (wave - zone * 205) * 5 > 1023 ? 1024 : (wave - zone * 205) * 5;
    if (zone == 0) return (sn * (1024 - f) + tri * f) >> 10;
    if (zone == 1) return (tri * (1024 - f) + saw * f) >> 10;
    if (zone == 2) return (saw * (1024 - f / 3) + sq * (f / 3)) >> 10;
    if (zone == 3) return (saw * (683 - f * 2 / 3) + sq * (341 + f * 2 / 3)) >> 10;
    return (saw * (f / 3) + sq * (1024 - f / 3)) >> 10;
}

i32 wave_at(i32 u, i32 wave, i32 width, i32 sub)
{
    const i32 v = osc_at(u & 1023, wave, width);
    const i32 s = (((u >> 10) & 1) ? -1024 : 1024) * sub >> 11;       /* the sub: a step every cycle */
    return (v * (1024 - (sub >> 2)) >> 10) + s;
}

struct Motion { i32 wow, flutter, rev, deg; u32 t; u32 seed; };

/* the tape, reel to reel: 32 points out (the left reel, 30 on three cycles, the right reel), the same
 * back. lift: the whole line's offset (Q10 px); shift: its phase along the tape */
void tape(Frame::path_type& path, i32 wave, i32 width, i32 sub, const Motion& m, i32 lift, i32 shift, u32 salt)
{
    i32 us[30];
    int n = 0, c, k;
    for (c = 0; c < 3; c++) {                         /* ten a cycle: its edges either side, the rest evenly */
        i32 v[10] = {1, width - 2, width + 2, 1022, 170, 340, 512, 680, 850, 940};
        for (k = 0; k < 10; k++) v[k] = v[k] < 1 ? 1 : v[k] > 1022 ? 1022 : v[k];
        for (k = 1; k < 10; k++) {                    /* in order */
            const i32 x = v[k];
            int j = k - 1;
            while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
            v[j + 1] = x;
        }
        for (k = 0; k < 10; k++) us[n++] = c * 1024 + v[k];
    }
    Point fwd[32];
    fwd[0].x = -64 * unit; fwd[0].y = -2 * unit;
    for (int i = 0; i < 30; i++) {
        const i32 u = us[i] + shift;
        const u32 along = static_cast<u32>(u) * 1400000u;
        i32 y = -(wave_at(us[i], wave, width, sub) * 6) + lift;                       /* +-6 px, Q10 */
        y += sin10(m.t * 5u + along) * m.wow / 1024 * 5;                             /* WOW: a slow bend */
        y += sin10(m.t * 90u + along * 5u + salt) * m.flutter / 1024 * 2;            /* FLUTTER: a shiver */
        if (m.deg > 250) y += static_cast<i32>(hash(m.seed + salt + static_cast<u32>(i)) % 2048u - 1024) * (m.deg - 250) / 1024 * 3;
        fwd[i + 1].x = -48 * unit + (us[i] * 96 / 3072) * unit;
        fwd[i + 1].y = clamp(y, -60 * unit, 60 * unit);
    }
    fwd[31].x = 64 * unit; fwd[31].y = -2 * unit;
    for (int i = 0; i < 32; i++) path.points[i] = fwd[i];
    for (int i = 0; i < 32; i++) path.points[32 + i] = fwd[31 - i];
}

void reel(Frame& f, u32 pack, u32 slot, i32 cx, u32 angle, i32 wound)
{
    place(f, pack, cx * unit, RY * unit, 1100 + wound * 780 / 1024, C_ORANGE, keep);   /* r 28..48 */
    for (u32 k = 0; k < 3; k++) {
        const u32 a = angle + k * 0x55555555u;
        place(f, slot + k, cx * unit + sin10(a + 0x40000000u) * 14, RY * unit + sin10(a) * 14, unit, C_BLACK, keep);
    }
}
}

inline void display_reset(DisplayState& state)
{
    state.time = state.angle = state.serial = 0;
    state.flash = 0;
    state.key = 1;
    state.seed = 1;
}

void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame)
{
    const i32 dt = input.reduced_motion ? 0 : clamp(input.elapsed, 0, 102);
    const i32 wave = q10(input.slots[0]), pwm = q10(input.slots[1]), sub = q10(input.slots[2]), chorus = q10(input.slots[3]);
    Motion m;
    m.wow = q10(input.slots[4]); m.flutter = q10(input.slots[5]); m.rev = q10(input.slots[6]); m.deg = q10(input.slots[7]);
    const i32 e = pwm * 2 - pwm * pwm / 1024;                                       /* the ease-out, Q10 */
    const i32 bent = e * 6 / 10 + e * 4 / 10 * (sin10(state.time * (1500000u + static_cast<u32>(pwm * pwm) / 8u)) + 1024) / 2048;
    const i32 width = 512 - bent * 450 / 1024;
    state.time += static_cast<u32>(dt);
    m.t = state.time * 600u;

    /* the reels: half a turn a second; WOW makes them lurch, FLUTTER stutter */
    {
        const i32 lurch = sin10(state.time * 2400000u) * m.wow / 1024;            /* -1024..1024 */
        const i32 stutter = sin10(state.time * 40000000u) * m.flutter / 2048;
        /* REVERSE: the reels slow, stop at a third, then run backwards */
        const i32 dir = 1024 - m.rev * 3;
        state.angle += static_cast<u32>(dt * (dir + lurch * 3 / 4 + stutter)) * 2048u;
    }
    if (input.note_serial != state.serial) { state.serial = input.note_serial; state.flash = 1024; }
    else state.flash = state.flash > dt * 3 ? state.flash - dt * 3 : 0;
    if (!input.reduced_motion) state.seed = hash(state.seed + static_cast<u32>(dt));
    m.seed = state.seed;
    state.key = state.key + 1 ? state.key + 1 : 1;

    /* the tape and its chorus copy: WOW lets them wander apart, each voice its own way */
    {
        const i32 d1 = sin10(state.time * 900000u) * m.wow / 1024 * 4;
        const i32 d2 = sin10(state.time * 650000u + 0x60000000u) * m.wow / 1024 * 4;
        const i32 vib = chorus > 682 ? sin10(state.time * 40000000u) * 2 : 0;      /* I+II: it trembles */
        tape(frame.paths[N_WAVE], wave, width, sub, m, d1, 0, 0);
        tape(frame.paths[N_CHORUS], wave, width, sub, m, d2 + 3 * unit + vib, 40 + chorus / 6, 0x9e3779b9u);
        frame.paths[N_WAVE].key = state.key;
        frame.paths[N_CHORUS].key = state.key;
        /* a dropout: the tape dims for a moment */
        const bool drop = m.deg > 600 && (hash(state.seed) & 1023u) < static_cast<u32>(m.deg - 600) / 4;
        place(frame, N_WAVE, SUN_X * unit, TAPE_Y * unit, 2 * unit, drop ? C_MID : C_ORANGE, stroke);
        if (chorus > 10) place(frame, N_CHORUS, SUN_X * unit, TAPE_Y * unit, 2 * unit, chorus > 341 ? C_BLUE : C_NAVY, stroke);
        else hide(frame, N_CHORUS);
    }

    /* the sun: a note flashes it; DEGRADE dims it */
    {
        u32 shade = m.deg > 800 ? C_MID : m.deg > 500 ? C_ORANGE : C_SUN;
        if (state.flash > 700) shade = C_YELLOW;
        else if (state.flash > 300) shade = C_BRIGHT;
        place(frame, N_SUN, SUN_X * unit, SUN_Y * unit, unit, shade, fill_and_stroke);
        /* two rings out from it */
        for (u32 k = 0; k < 2; k++) {
            const i32 age = 1024 - state.flash - static_cast<i32>(k) * 200;
            if (state.flash && age > 0) {
                const u32 ring = age < 300 ? C_ORANGE : age < 600 ? C_MID : C_EMBER;
                place(frame, N_RING + k, SUN_X * unit, SUN_Y * unit, unit + age * 3 / 4, ring, stroke);
            } else hide(frame, N_RING + k);
        }
    }

    /* the left reel empties as the right one fills, round and round */
    {
        const i32 wound = static_cast<i32>((state.time >> 6) % 1024u);
        reel(frame, N_PACK_L, N_SLOT_L, RL, state.angle, 1024 - wound);
        reel(frame, N_PACK_R, N_SLOT_R, RR, state.angle + 0x1234567u, wound);
    }

    /* the sky: three stars always, twinkling; DEGRADE brings the dust, all over the window */
    for (u32 k = 0; k < 10; k++) {
        if (k < 3) {
            const bool lit = (sin10(state.time * (1100000u + k * 370000u) + k * 0x50000000u) > -300);
            place(frame, N_SPECK + k, STAR_X[k] * unit, STAR_Y[k] * unit, unit, lit ? C_LIGHT : C_GREY, fill_and_stroke);
        } else if (m.deg > static_cast<i32>(110 * (k - 2))) {
            const u32 h = hash(state.seed * 31u + k);
            place(frame, N_SPECK + k, static_cast<i32>(146 + h % 188u) * unit, static_cast<i32>(72 + (h >> 8) % 100u) * unit,
                  unit + static_cast<i32>((h >> 20) & 511u), (h >> 16) & 1 ? C_GREY : C_WHITE, fill_and_stroke);
        } else hide(frame, N_SPECK + k);
    }

    /* the page: synth (encoder 1 shows WAVE), or the tape (it shows WOW) */
    {
        const bool second = input.shown[0] == 4;
        place(frame, N_PAGE, 232 * unit, 194 * unit, unit, second ? C_GREY : C_ORANGE, fill_and_stroke);
        place(frame, N_PAGE + 1, 248 * unit, 194 * unit, unit, second ? C_ORANGE : C_GREY, fill_and_stroke);
    }
}
}
