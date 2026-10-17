#pragma once
// Storage.h - scene bank (8 snapshots), ValueTree/base64 persistence and temp-file MIDI export.
// Message-thread only; the audio thread never touches anything in here.

#include "Engine.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>

namespace icc
{

struct Scene
{
    bool      populated = false;
    GenParams params;                 // parameters the pattern was generated with
    double    bpm = 120.0;
    Routing   routing;
    Pattern   pattern;
};

class SceneBank
{
public:
    Scene&       at (int i)       { return scenes[(size_t) juce::jlimit (0, kNumScenes - 1, i)]; }
    const Scene& at (int i) const { return scenes[(size_t) juce::jlimit (0, kNumScenes - 1, i)]; }

    juce::ValueTree toValueTree() const;
    void            fromValueTree (const juce::ValueTree&);   // tolerant: bad/missing data leaves defaults
    void            clear() { scenes = {}; }

private:
    std::array<Scene, kNumScenes> scenes;
};

juce::String patternToBase64 (const Pattern&);
bool         patternFromBase64 (const juce::String&, Pattern&);

/** Writes bytes to a uniquely named icc_<uuid>.mid in the temp folder (unique names avoid file
    locking in hosts that keep dropped files open). Returns an invalid File on failure. */
juce::File writeTempMidiFile (const std::vector<uint8_t>& bytes);
void       cleanupOldTempMidiFiles (int maxAgeHours = 24);

} // namespace icc
