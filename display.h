#pragma once
#include <opxy/draw.hpp>

namespace oldsun {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette. Eight text
// slots: the page not shown, under the header, titles then values.
using Frame = opxy::draw::Frame<2, 64, 25, 8, 16>;
struct DisplayState {
    opxy::u32 time;                     // Q10 seconds, wrapping
    opxy::u32 angle;                    // the reels' turn, a full turn is 2^32
    opxy::u32 serial;                   // the last note start seen
    opxy::i32 flash;                    // a note's flash, Q10, decaying
    opxy::u32 key;                      // the paths' geometry key
    opxy::u32 seed;
    opxy::u32 page;                     // the page in the header, plus one; 0 before the first frame
    opxy::u32 track;
    opxy::i32 since;                    // Q10 seconds since the page last switched
};
}
