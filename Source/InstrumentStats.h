#pragma once
// InstrumentStats.h - streamed per-instrument statistics over many MIDI files: for every instrument key
// (GM program, drum kit, family-only, unassigned) how many files / tracks / notes, register, velocity, note
// length, polyphony. Accumulators only: memory does not grow with the number of files. JUCE-free.

#include "InstrumentInfo.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace icc
{

struct InstrumentRow
{
    std::string key, label;
    int    family = -1, program = -1;
    int    files = 0, tracks = 0;
    double notes = 0;
    double notesPerBar = 0;
    float  pitchMean = 0, pitchP05 = 0, pitchP95 = 0, velMean = 0, velStd = 0;
    float  lengthBeats = 0;          // mean note length in quarter notes
    float  polyphony = 1;            // mean notes starting together (1 = monophonic)
    float  confidence = 0;           // mean classification confidence
    int    nameOverrides = 0;        // tracks whose program 0 was overridden by the name
};

class InstrumentTable
{
public:
    void beginFile (SystemMode);
    void addTrack (const ImportedTrack&, const InstrumentId&, int tsNum = 4, int tsDen = 4);
    void endFile();

    int  fileCount() const noexcept { return files; }
    int  modeFiles (SystemMode m) const noexcept { return modes[(size_t) m]; }
    std::vector<InstrumentRow> rows() const;                    // sorted by note count, biggest first
    std::string toCsv() const;
    std::string describe (int maxRows = 60) const;              // text chart

    std::vector<uint8_t> toBytes() const;                       // magic "ICI1"
    bool                 fromBytes (const uint8_t*, size_t);

private:
    struct Acc
    {
        int    files = 0, tracks = 0, nameOverrides = 0;
        double notes = 0, bars = 0, velSum = 0, velSq = 0, lenSum = 0, polySum = 0, confSum = 0;
        std::array<double, 128> pitch {};
        int family = -1, program = -1;
    };
    std::map<std::string, Acc> accs;
    std::vector<std::string>   seenInFile;
    int files = 0;
    std::array<int, 5> modes {};
};

} // namespace icc
