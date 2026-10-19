#pragma once
// Mapping.h - General MIDI names (drum keys + 128 programs), default per-track programs and the
// human-readable FL Studio routing guide shown by the editor's "Map" button. JUCE-free.

#include "Theory.h"

#include <array>
#include <string>

namespace icc
{

/** General MIDI percussion keys the drum generator uses (channel 10 / FPC "General MIDI" layout). */
namespace gm
{
    constexpr int kick = 36, rim = 37, snare = 38, clap = 39, tomFloorLo = 41, hatClosed = 42, tomFloorHi = 43,
                  hatPedal = 44, tomLo = 45, hatOpen = 46, tomLoMid = 47, tomHiMid = 48, crash = 49, tomHi = 50,
                  ride = 51, rideBell = 53, tambourine = 54, splash = 55, cowbell = 56, congaHi = 63, congaLo = 64,
                  maracas = 70, claves = 75, shaker = 82;
}

constexpr int kFirstDrumKey = 27, kLastDrumKey = 87;

const char* gmDrumName (int note) noexcept;          // "" outside 27..87
const char* gmProgramName (int program) noexcept;    // program 0..127 (clamped)
const char* gmFamilyName (int program) noexcept;     // "Piano", "Bass", "Synth Pad", ...
std::string gmDrumKitName (int program);              // channel 10: the program number picks the kit (0 Standard, 25 TR-808, ...)

/** Default GM program per ICC track (0-based: 0 = Acoustic Grand). Rhythm's value selects the drum kit on ch.10. */
constexpr std::array<int, kNumTracks> kDefaultPrograms { 80, 4, 33, 0, 89, 81, 46 };

/** One line per track: channel, GM program and a short hint for what to load in FL Studio. */
std::string describeMapping (const std::array<int, kNumTracks>& channels,
                             const std::array<int, kNumTracks>& programs, bool programChangesSent);

/** The drum keys the Rhythm track can produce, with the FPC / MIDI Out name for each. */
std::string describeDrumKeys();

} // namespace icc
