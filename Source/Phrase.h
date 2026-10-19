#pragma once
// Phrase.h - phrase roles (opening / continuation / development / climax / cadence) that shape how busy,
// how high and how tense each stretch of the loop is. JUCE-free; generators read the plan, nothing else.

#include "Engine.h"

#include <vector>

namespace icc
{

enum class PhraseRole { Opening, Continuation, Development, Climax, Cadence };

struct PhrasePlan
{
    int        startBar      = 0;
    int        lengthBars    = 2;
    PhraseRole role          = PhraseRole::Opening;
    int        targetPitch   = 72;      // centre of the melody register for this phrase
    float      activity      = 0.6f;    // 0..1: note density / drum energy
    float      tension       = 0.3f;    // 0..1: target harmonic tension (used by the scorer)
    bool       shouldResolve = false;   // melody should land on a chord tone at the end
    bool       rideSection   = false;   // drums move to the ride cymbal
};

const char* phraseRoleName (PhraseRole) noexcept;

/** 2-bar phrases up to 12 bars, 4-bar phrases from 13 bars up (the last one may be shorter).
    First = Opening, last = Cadence, a Climax roughly two thirds of the way through when there is room. */
std::vector<PhrasePlan> makePhrasePlan (int bars, Rng&);

const PhrasePlan& planAtBar (const std::vector<PhrasePlan>&, int bar) noexcept;   // plan must be non-empty
bool isFirstBarOfPhrase (const PhrasePlan&, int bar) noexcept;
bool isLastBarOfPhrase (const PhrasePlan&, int bar) noexcept;

} // namespace icc
