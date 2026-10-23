#pragma once
// FscFile.h - FL Studio score (.fsc) reader / writer. JUCE-free.
//
// The layout is taken from the open-source fsc2fsc project, during trial testing it is working but bugs may exist such as slide notes:
//   "FLhd" u32=6 | u16 format | u16 channels | u16 ppq | "FLdt" u32 dataSize | events...
//   event id <64: 1 data byte, <128: 2, <192: 4, >=192: varint length + data (7-bit groups, little endian)
//   0xC7 = FL version string, 0xE0 = notes (24-byte records, 20 in FL 3.x files), 0xDF = automation (12-byte records)
// working as much as tested currently the tester is included as it was added as a test bed but actual use in fl studio is showing it to work to basic goals
// of importing and draging and droping the fsc into fl studio channel rack paino roll. 

#include "Engine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace icc
{

struct FscNote
{
    int start = 0, length = 0;      // ticks at kPPQ
    int key = 60, velocity = 100;   // MIDI note 0..127, velocity 1..127 (FL stores 0..128)
    int pan = 64, release = 64, fine = 120, modX = 128, modY = 128;   // FL defaults
};

struct FscScore
{
    bool ok = false;
    std::string error;
    int  format = 0, channels = 1, ppq = 96;
    int  recordSize = 24;           // 20 in old FL (3.x), 24 in newer
    int  automationEvents = 0;      // 0xDF blocks seen (ignored)
    std::vector<FscNote> notes;     // ticks converted to kPPQ, sorted by start then key
};

/** Never throws; untrusted bytes are fine. */
FscScore readFsc (const uint8_t* data, size_t size, size_t maxNotes = 200000);

struct FscTemplate
{
    bool learned = false;                 // false = built-in best guess
    int  format = 0, channels = 1, ppq = 96;
    std::vector<uint8_t> prefix;          // raw event bytes before the notes event (FL version + preamble)
    std::vector<uint8_t> suffix;          // raw event bytes after it, automation removed
    std::vector<uint8_t> record;          // one note record (20 or 24 bytes) whose defaults new notes inherit
};

/** Human-readable structure dump (header fields, every event id / length, note record guess) for when a file will not parse. */
std::string diagnoseFsc (const uint8_t* data, size_t size);

/** Learns from a real FL-saved score. False (and `out` untouched) when the bytes are not a score with >= 1 note. */
bool learnFscTemplate (const uint8_t* data, size_t size, FscTemplate& out);
FscTemplate defaultFscTemplate();
std::vector<uint8_t> templateToBytes (const FscTemplate&);                 // stored in the user's app data
bool templateFromBytes (const uint8_t*, size_t, FscTemplate&);             // magic "ICF1"

/** Notes are ticks at kPPQ; they are rescaled to the template's ppq. */
std::vector<uint8_t> writeFsc (const std::vector<Note>&, const FscTemplate&);
std::vector<Note>    fscToNotes (const FscScore&);

} // namespace icc
