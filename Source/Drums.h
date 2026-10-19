#pragma once
// Drums.h - the rhythm section: 16 grooves written as step grids, ghost notes, swing, phrase-aware
// fills and crashes, humanisation, plus the kick/anchor lists that bass and melody lock to. JUCE-free.

#include "Engine.h"
#include "Phrase.h"

#include <vector>

namespace icc
{

enum class BassMode { Eighths, KickLock, Offbeat, Pulse };

struct DrumSettings
{
    int   style      = -1;      // -1 = pick from the seed, otherwise an index into the style list
    float complexity = 0.5f;    // 0..1  ghosts, extra kicks, rolls, hat variations
    float swing      = 0.0f;    // 0..1  adds on top of the style's own swing (0 = style default)
    float humanize   = 0.3f;    // 0..1  timing + velocity looseness
    float fills      = 0.5f;    // 0..1  how often phrase ends get a fill (and crash)
    int   bars       = 4;
};

struct DrumResult
{
    std::vector<Note>             notes;
    std::vector<std::vector<int>> anchors;   // per bar: grid ticks of kick/snare hits (melody rhythm anchors)
    std::vector<std::vector<int>> kicks;     // per bar: grid ticks of kick hits (bass locks to these)
    int      style     = 0;
    BassMode bassMode  = BassMode::Eighths;
    float    sustain   = 0.6f;               // bass note length as a fraction of the gap to the next kick
};

int         drumStyleCount() noexcept;
const char* drumStyleName (int index) noexcept;   // clamps
int         drumStyleBpm (int index) noexcept;    // the tempo the groove was written for

DrumResult generateDrums (const DrumSettings&, const std::vector<PhrasePlan>&, Rng&);

/** Rebuilds anchors/kicks from existing (locked or hand-edited) drum notes so bass and melody still lock to them. */
DrumResult analyseDrums (const std::vector<Note>& drumNotes, int bars);

} // namespace icc
