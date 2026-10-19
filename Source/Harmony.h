#pragma once
// Harmony.h - chord names, roman numerals, harmonic function, cadence detection and a simple tension
// model (Krumhansl-Kessler key profiles + dissonance + function). JUCE-free and purely analytical:
// nothing here changes what the generators produce; it labels, explains and scores.

#include "Theory.h"

#include <string>
#include <vector>

namespace icc
{

enum class HarmonicFunction { Tonic, Predominant, Dominant, Other };
enum class CadenceType { None, PerfectAuthentic, Authentic, Plagal, Backdoor, Deceptive, Half };

struct ChordLabel
{
    std::string      name;        // "Am7", "F#dim"
    std::string      roman;       // "vi7", "V", "bVII"
    HarmonicFunction function = HarmonicFunction::Other;
    int              offset   = 0;      // semitones above the tonic (0..11)
    bool             diatonic = true;   // root is in the scale
    bool             majorThird = true; // chord has a major third (used for V-I vs v-i)
};

const char* functionName (HarmonicFunction) noexcept;
const char* cadenceName (CadenceType) noexcept;

std::string chordName (const ChordInfo&);
ChordLabel  labelChord (const ChordInfo&, int rootPc, int scaleIndex);
std::vector<ChordLabel> labelProgression (const std::vector<ChordInfo>&, int rootPc, int scaleIndex);

/** Looks at the chord into the loop point first (last -> first, the loop repeats), then the last two chords. */
CadenceType detectCadence (const std::vector<ChordLabel>&);

/** 0 (settled) .. 1 (restless) for one chord in the given key. */
float chordTension (const ChordInfo&, int rootPc, int scaleIndex);

/** "| Am7 vi7 | F IV | ...", for the editor's chord strip. */
std::string describeProgression (const std::vector<ChordInfo>&, int rootPc, int scaleIndex);

} // namespace icc
