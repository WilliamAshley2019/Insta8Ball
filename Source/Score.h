#pragma once
// Score.h - rates a generated pattern (melody shape, chord voicing, rhythm section, cadence, tension) and
// picks the best of N deterministic candidates. JUCE-free; message-thread use only.

#include "Engine.h"

#include <string>
#include <vector>

namespace icc
{

struct PatternScore
{
    float total   = 0.0f;   // 0..100
    float melody  = 0.0f;   // each part 0..1
    float voicing = 0.0f;
    float rhythm  = 0.0f;
    float cadence = 0.0f;
    float tension = 0.0f;
    std::vector<std::string> notes;   // plain-language findings, e.g. "Parallel fifths in the chords, bar 3 to 4"
};

PatternScore scorePattern (const Pattern&, const GenParams&);

struct GenerationResult
{
    Pattern      pattern;
    PatternScore score;
    uint32_t     seed    = 0;   // seed of the winning candidate
    int          variant = 0;   // 0-based index of the winner
    int          variants = 1;
};

/** Generates params.candidates patterns (seed, seed+7919, ...), scores each and returns the best.
    Candidate 0 is exactly generatePattern(params), so candidates == 1 changes nothing. Deterministic. */
GenerationResult generateBest (const GenParams&, const Pattern* locked = nullptr, uint32_t lockMask = 0);

} // namespace icc
