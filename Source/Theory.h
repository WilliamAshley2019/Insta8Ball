#pragma once
// Theory.h - JUCE-free music theory: constants, scales, chord shapes, progressions, voice leading.
// Everything here is pure C++20 so it can be unit-tested without JUCE (see Tests/engine_test.cpp).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace icc
{

// ---- Time base: every note in the plugin is stored in ticks (tempo independent) ----
constexpr int kPPQ         = 960;
constexpr int kBeatsPerBar = 4;
constexpr int kBarTicks    = kPPQ * kBeatsPerBar;   // 3840
constexpr int kMaxBars     = 16;
constexpr int kNumScenes   = 8;
constexpr int kMaxSeed     = 9999999;               // fits a float parameter exactly

// ---- Tracks ----
constexpr int kNumTracks = 7;
enum Track : int { Melody = 0, Chord, Bass, Rhythm, Pad, Arp, Foliage };

const char* trackName (int track) noexcept;
constexpr std::array<int, kNumTracks> kDefaultChannels { 4, 1, 2, 10, 5, 6, 4 };

// ---- Scales ----
struct Scale
{
    std::string      name;
    std::string      category;
    std::vector<int> intervals;   // ascending semitone offsets from the root, first is 0
};

const std::vector<Scale>& scales();
const Scale&              scaleAt (int index) noexcept;               // clamps the index
bool                      inScale (const Scale&, int rootPc, int midiNote) noexcept;
int                       snapToScale (const Scale&, int rootPc, int midiNote) noexcept;  // nearest, ties go down
const char*               noteName (int pitchClass) noexcept;
int                       pcOf (int midiNote) noexcept;               // always 0..11

// ---- Chord shapes ----
struct ChordShape
{
    std::string      name;
    std::string      symbol;
    std::vector<int> intervals;   // ascending, first is 0
};

const std::vector<ChordShape>& chordShapes();
const ChordShape&              chordShapeAt (int index) noexcept;     // clamps the index
int                            chordShapeIndex (const std::string& name) noexcept;   // -1 if unknown

struct ChordInfo
{
    int rootMidi = 60;
    int shape    = 0;
    std::vector<int> notes() const;   // rootMidi + shape intervals
};

// ---- Progressions ----
struct ProgressionDef
{
    std::string      name;
    std::vector<int> degrees;   // 0-based scale degrees, one chord per bar, cycled
    bool             sevenths;
};

const std::vector<ProgressionDef>& progressions();

/** One ChordInfo per bar. Chords are stacked thirds on the *actual* scale (diatonic harmonisation),
    so every progression stays in key for any 7-note scale. Scales with fewer/more notes use the
    major or natural minor scale of the same root for chord choice. */
std::vector<ChordInfo> buildProgression (int progressionIndex, int rootPc, int scaleIndex, int bars);

/** Pick the inversion / octave of `notes` (ascending) that moves least from `previous`, staying in
    [lo, hi]. With no previous voicing it prefers root position near the middle of the range. */
std::vector<int> voiceLead (const std::vector<int>& notes, const std::vector<int>& previous, int lo, int hi);

/** Move `note` by whole octaves into [lo, hi] (clamps if the range is narrower than an octave). */
int wrapIntoRange (int note, int lo, int hi) noexcept;

} // namespace icc
