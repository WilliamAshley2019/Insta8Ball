#pragma once
// Engine.h - JUCE-free generation + data model: notes, patterns, generators, playback tables,
// pattern (de)serialisation and a standard MIDI file writer.

#include "Theory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace icc
{

// ---- Data model ----
struct Note
{
    int start    = 0;    // ticks from the start of the pattern
    int length   = 240;  // ticks
    int pitch    = 60;   // 0..127
    int velocity = 90;   // 1..127
    bool operator== (const Note& o) const noexcept
    {
        return start == o.start && length == o.length && pitch == o.pitch && velocity == o.velocity;
    }
};

using Pattern = std::array<std::vector<Note>, kNumTracks>;

void   sortPattern (Pattern&);                 // by start, then pitch
size_t countNotes (const Pattern&);
void   clampPattern (Pattern&, int bars);      // drop notes outside the loop, trim overhangs, fix ranges

// ---- Deterministic RNG (identical on every platform, unlike std:: distributions) ----
class Rng
{
public:
    explicit Rng (uint32_t seed = 1) { reseed (seed); }
    void     reseed (uint32_t seed);
    uint32_t next();
    float    nextFloat();               // [0, 1)
    int      nextInt (int n);           // [0, n)
    bool     nextBool() { return (next() & 0x80000000u) != 0; }
private:
    uint64_t state = 1;
};

// ---- Generation ----
struct GenParams
{
    int      rootPc           = 0;
    int      scaleIndex       = 0;
    int      progressionIndex = 0;
    int      bars             = 4;
    uint32_t seed             = 12345;
    float    foliageDensity   = 0.35f;
};

/** Pure function of its inputs: same params -> identical Pattern, on any machine. */
Pattern generatePattern (const GenParams&);

// ---- Routing and playback tables ----
struct Routing
{
    uint32_t mute = 0;                   // bit i = track i muted
    uint32_t solo = 0;                   // bit i = track i soloed
    std::array<int, kNumTracks> channels = kDefaultChannels;   // 1..16

    bool audible (int track) const noexcept
    {
        const uint32_t bit = 1u << (unsigned) track;
        return solo != 0 ? (solo & bit) != 0 : (mute & bit) == 0;
    }
    int channelOf (int track) const noexcept
    {
        const int c = channels[(size_t) track];
        return c < 1 ? 1 : (c > 16 ? 16 : c);
    }
};

struct PlaybackEvent
{
    int tick;       // 0 .. loopTicks-1
    int track;
    int pitch;
    int velocity;   // 0 = note off
};

/** Immutable table the audio thread reads. Built on the message thread, never modified afterwards. */
struct Playback
{
    std::vector<PlaybackEvent> events;   // sorted by tick; note-offs sort before note-ons at equal ticks
    int     loopTicks = 4 * kBarTicks;
    Routing routing;
    int     sceneTag  = -1;   // scene index this table belongs to (-1 = none); read by the UI via the audio thread
};

Playback buildPlayback (const Pattern&, int bars, const Routing&);

// ---- Serialisation (little-endian, versioned) ----
std::vector<uint8_t> encodePattern (const Pattern&);
bool                 decodePattern (const uint8_t* data, size_t size, Pattern& out);

// ---- Standard MIDI File, format 1: conductor track + one named track per instrument track ----
std::vector<uint8_t> writeMidiFile (const Pattern&, double bpm, const Routing&, int bars, bool respectMuteSolo);

} // namespace icc
