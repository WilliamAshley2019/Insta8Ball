#pragma once
// Corpus.h - train a StyleProfile from a folder of .mid files. JUCE-free (std::filesystem), so the same code
// runs in the plugin's background thread and in the unit tests. Needs no training data to exist: the plugin
// works without any corpus and only uses it when the user trains / loads a genre.

#include "MidiImport.h"
#include "InstrumentInfo.h"
#include "InstrumentStats.h"
#include "StyleProfile.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace icc
{

struct TrainOptions
{
    size_t maxFileBytes  = 4u << 20;    // larger files are skipped
    int    maxBarsPerFile = 64;         // only the first bars of long files are analysed
    int    minDrumNotes  = 8;           // files with fewer drum hits are skipped
    bool   skipOddMeter  = true;        // not 4/4 -> skipped (the plugin is 4/4)
    bool   dropDuplicates = true;       // identical drum parts are counted once
    bool   collectInstruments = false;  // also stream per-instrument statistics (works on files without drums / odd meter)
};

struct TrainReport
{
    StyleProfile profile;
    int  scanned = 0, used = 0, noDrums = 0, unreadable = 0, oddMeter = 0, tooBig = 0, duplicates = 0;
    InstrumentTable instruments;
    int  instrumentFiles = 0, instrumentDuplicates = 0;
    bool cancelled = false;
    std::string summary() const;
};

/** .mid / .midi files under `root` (case-insensitive), sorted, at most `maxFiles`. Never throws. */
std::vector<std::filesystem::path> findMidiFiles (const std::filesystem::path& root, bool recursive, size_t maxFiles = 500000);

/** Channel 10 notes, or the notes of a track named like drums / percussion / kit. False when there are none. */
bool extractDrums (const ImportedMidi&, std::vector<Note>& out);

/** progress(done, total) returns false to cancel. Never throws. */
TrainReport trainFromFiles (const std::vector<std::filesystem::path>& files, const std::string& name,
                            const TrainOptions& = {}, const std::function<bool (int done, int total)>& progress = {});

} // namespace icc
