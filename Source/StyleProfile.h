#pragma once
// StyleProfile.h - a drum "genre" as data: how likely each lane hits each 16th step, how loud, how swung,
// how dense. The 16 built-in styles are profiled from the generator itself; trained genres come from folders
// of .mid files (see Corpus.h). A profile can generate drums, score how close a pattern is, and restyle one.
// JUCE-free.

#include "Drums.h"

#include <array>
#include <string>
#include <vector>

namespace icc
{

constexpr int kLanes = 7;
enum Lane : int { LKick = 0, LSnare, LHatClosed, LHatOpen, LCymbal, LTom, LPerc };

const char* laneName (int lane) noexcept;
int         laneOfKey (int gmKey) noexcept;       // GM percussion key -> lane
int         laneDefaultKey (int lane) noexcept;

struct StyleProfile
{
    std::string name;
    int   files = 0;                  // files that contributed (1 for a single pattern)
    int   bars  = 0;                  // bars analysed in total

    std::array<std::array<float, 16>, kLanes>  prob {};       // fraction of bars with a hit on that 16th step
    std::array<float, kLanes>                  velMean {}, velStd {};
    std::array<float, kLanes>                  hitsPerBar {};
    std::array<std::array<float, 128>, kLanes> pitchDist {};  // share of each GM key within the lane (sums to 1 when used)

    float swing = 0.5f;               // 0.5 straight .. ~0.71 shuffle, same meaning as the generator's swing ratio
    float offGrid = 0.0f;             // share of hits that sit off the 16th grid (rolls, flams, triplets)
    float densityMean = 0, densityP05 = 0, densityP95 = 0;    // hits per bar, all lanes
    float bpmMean = 120, bpmP05 = 0, bpmP95 = 0;

    bool laneUsed (int lane) const noexcept { return hitsPerBar[(size_t) lane] >= 0.25f; }
    int  pickKey (int lane, Rng&) const;                       // sampled from pitchDist, default key when empty

    std::vector<uint8_t> toBytes() const;
    bool                 fromBytes (const uint8_t*, size_t);
};

/** Profile of one pattern (files = 1). Only complete bars that contain a drum hit count. */
StyleProfile profileOfDrums (const std::vector<Note>& drumNotes, int bars, const std::string& name = {});

/** Streaming trainer: add one profiled file at a time, then finish(). Equal weight per file. */
class ProfileTrainer
{
public:
    void         add (const StyleProfile& single, double bpm);
    int          fileCount() const noexcept { return count; }
    StyleProfile finish (const std::string& name) const;
private:
    int    count = 0;
    double barsTotal = 0, swingSum = 0, offSum = 0;
    double probSum[kLanes][16] = {}, velMeanSum[kLanes] = {}, velStdSum[kLanes] = {}, hitsSum[kLanes] = {}, laneFiles[kLanes] = {};
    double pitchSum[kLanes][128] = {};
    std::vector<float> density, bpms;
};

/** The profile of built-in style `index` (0..drumStyleCount()-1), cached after the first call. */
const StyleProfile& builtinProfile (int index);

/** 0 = nothing alike, 100 = the same groove statistics. */
float styleMatch (const std::vector<Note>& drumNotes, int bars, const StyleProfile& target);
float profileDistance (const StyleProfile& a, const StyleProfile& b);   // 0..1

/** Drums sampled from the profile. Deterministic in (profile, settings, plan, rng). */
DrumResult generateProfileDrums (const StyleProfile&, const DrumSettings&, const std::vector<PhrasePlan>&, Rng&);

/** Moves existing drum notes toward the profile. strength 0 returns the input; 1 applies the full push.
    Hits unlikely in the target are thinned, likely missing ones are added, velocity and swing shift. */
std::vector<Note> restyleDrums (const std::vector<Note>& in, int bars, const StyleProfile& target, float strength, uint32_t seed);

/** Text view of a profile: one row per used lane, a shade per step (. : - = + * # @), plus the summary numbers. */
std::string describeProfile (const StyleProfile&);

} // namespace icc
