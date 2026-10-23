#pragma once
// MidiImport.h - standard MIDI file reader for the v2 corpus / import features. JUCE-free.
// Pairs note-ons with note-offs (velocity-0 note-ons count as offs), rescales any PPQ / SMPTE
// division to kPPQ and repairs the usual defects (hanging notes, orphan offs, retriggers).

#include "Engine.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace icc
{

enum class SystemMode { Unknown, GM1, GM2, GS, XG };   // from the file's reset SysEx messages
const char* systemModeName (SystemMode) noexcept;

struct ImportedTrack
{
    std::string       name;            // MIDI track name (meta 0x03)
    std::string       instrumentName;  // MIDI instrument name (meta 0x04), often empty
    int               channel = 0;     // 1..16 (the channel the notes were on)
    int               program = -1;    // first program change seen for this channel (this file track, else anywhere in the file); -1 = none
    int               bankMsb = -1, bankLsb = -1;   // bank select (CC 0 / CC 32) seen before the program, -1 = none
    bool              multiProgram = false;         // the channel changes program more than once
    std::vector<Note> notes;           // ticks at kPPQ, sorted by start, then pitch
};

struct ImportedMidi
{
    bool        ok = false;
    std::string error;
    int         format = 1;
    int         sourcePPQ = 480;
    double      bpm = 120.0;           // first tempo event (120 when the file has none)
    int         tsNum = 4, tsDen = 4;  // first time signature (4/4 when the file has none)
    bool        multiTempo = false;
    SystemMode  systemMode = SystemMode::Unknown;
    int         lengthTicks = 0;       // end of the last note, at kPPQ
    int         hangingNotes = 0, orphanOffs = 0, retriggers = 0;
    std::vector<ImportedTrack> tracks; // one entry per (file track, channel) that holds notes

    int totalNotes() const noexcept;
    int barsNeeded() const noexcept;   // ceil(length / bar), clamped to 1..kMaxBars
};

/** Never throws; untrusted bytes are fine. Files above `maxNotes` notes are rejected. */
ImportedMidi importMidi (const uint8_t* data, size_t size, size_t maxNotes = 2000000);

struct ImportMapping
{
    Pattern pattern;
    int     bars = 1;
    int     droppedTracks = 0;         // non-drum tracks that did not fit the 7 slots
    std::array<int, kNumTracks> source { -1, -1, -1, -1, -1, -1, -1 };   // index into ImportedMidi::tracks that fed each slot (Rhythm: the first drum track), -1 = empty
    bool    oddMeter = false;          // the file is not 4/4: bars were cut at 4/4 boundaries
};

/** Places an imported file into ICC tracks. Channel 10 -> Rhythm; the rest are classified by register and
    polyphony (low = Bass, chordal = Chord/Pad, the busiest line = Melody, then Arp / Foliage).
    `startBar` skips the first bars of long files; the result is trimmed to `maxBars` (1..16). */
ImportMapping importToPattern (const ImportedMidi&, int startBar = 0, int maxBars = kMaxBars);

} // namespace icc
