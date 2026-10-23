#pragma once
// GenreLibrary.h - the trained-genre folder on disk (*.icgenre), plus the background training job.
// The plugin never needs this to work: with an empty library everything behaves like v1.

#include "Corpus.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace icc
{

class GenreLibrary
{
public:
    explicit GenreLibrary (juce::File directory = defaultDirectory());
    ~GenreLibrary();

    static juce::File defaultDirectory();            // <user app data>/AlphaAudio/InstaComposerClone/Genres
    juce::File        directory() const { return dir; }
    void              setDirectory (const juce::File&);

    // ---- message thread ----
    void refresh();                                  // re-read the folder
    int  count() const noexcept { return (int) entries.size(); }
    juce::String nameAt (int index) const;
    std::shared_ptr<const StyleProfile> profileAt (int index) const;
    int  indexOfName (const juce::String&) const;    // -1 when missing
    bool remove (int index);                         // deletes the file
    bool importFile (const juce::File&);             // copy an .icgenre into the library

    // ---- training (one job at a time) ----
    bool startTraining (const juce::File& folder, const juce::String& name, bool recursive, const TrainOptions& = {});
    void cancelTraining();
    bool isTraining() const noexcept { return running.load(); }
    double progress() const noexcept;                // 0..1
    juce::String statusText() const;                 // "Scanning...", "412 / 9000 files", ...

    /** Returns true once per finished job; saves the genre (unless cancelled / empty) and refreshes the list. */
    bool takeFinished (TrainReport& out, juce::String& savedName);

    /** Latest instrument statistics (from a training run with "collect instruments", or a loaded .icinst). May be null. */
    std::shared_ptr<const InstrumentTable> instruments() const;
    bool loadInstrumentFile (const juce::File&);

private:
    struct Entry { juce::String name; juce::File file; std::shared_ptr<const StyleProfile> profile; };
    class Job;

    juce::File         dir;
    std::vector<Entry> entries;

    std::unique_ptr<Job>    job;
    std::atomic<bool>       running { false };
    std::atomic<int>        done { 0 }, total { 0 };
    std::atomic<bool>       finishedFlag { false };
    mutable juce::CriticalSection lock;
    juce::String            status;
    TrainReport             report;
    std::shared_ptr<const InstrumentTable> lastInstruments;
    bool saveInstruments (const InstrumentTable&, const juce::String& name);
};

} // namespace icc
