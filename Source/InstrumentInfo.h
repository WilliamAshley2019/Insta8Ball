#pragma once
// InstrumentInfo.h - "which instrument is this track?" for MIDI files. Reads the program change / bank select,
// the sound-set SysEx (GM, GM2, GS, XG), drum channel + drum kit, and falls back to the track / instrument name.
// Absent program changes are NOT read as piano (program 0 is only "Acoustic Grand" when the file says so).
// JUCE-free.

#include "MidiImport.h"

#include <string>

namespace icc
{

enum class InstrumentSource { Unknown, ProgramChange, Name, Drums };
constexpr int kDrumFamily = 16;                     // family index for drum kits (0..15 are the GM families)

struct InstrumentId
{
    InstrumentSource source = InstrumentSource::Unknown;
    int  family  = -1;        // 0..15 = GM family, 16 = drums, -1 unknown
    int  program = -1;        // melodic: GM program 0..127; drums: kit program; -1 = family only / unknown
    int  bank    = -1;        // bank MSB when the file sent one
    float confidence = 0.0f;  // 0..1: explicit non-zero program 0.95, program 0 0.6, specific name 0.7, family name 0.5, drums 0.95
    bool  nameOverrodeZero = false;   // program 0 was ignored because the track name clearly says something else

    bool drums() const noexcept { return family == kDrumFamily; }
    std::string key() const;      // stable id for statistics: "P034", "D025", "F05", "U"
    std::string label() const;    // "Fingered Bass", "Drum kit: TR-808", "Strings (from name)", "Unassigned"
};

/** Lower-case words of a name with digits / punctuation removed. */
std::string normaliseInstrumentName (const std::string&);

/** A name like "Track 3", "Untitled", "MIDI", "Channel 1", "Instrument" says nothing about the instrument. */
bool isGenericInstrumentName (const std::string&);

/** GM family (0..16) / program the words in `name` point at, or -1. Specific hits (rhodes, 808, fretless...) return a program. */
int familyFromName (const std::string& name);
int programFromName (const std::string& name);

InstrumentId classifyTrack (const ImportedTrack&, SystemMode);

/** A short search term for the FLEX / FL Studio browser: "piano", "e-piano", "bass", "strings", "pad", ... */
const char* flexSearchHint (int program, bool drums) noexcept;

} // namespace icc
