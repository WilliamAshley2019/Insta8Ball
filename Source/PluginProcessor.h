#pragma once
// PluginProcessor.h - parameters, state, scenes and host-synced MIDI playback.
//
// Threading contract
//   message thread : generation, editing, scene switching, state save/load, publishing playback tables
//   audio thread   : reads immutable Playback tables through atomic pointers. No locks, no allocation
//                    (apart from MidiBuffer growth inside the host's own buffer), no parameter writes.

#include "Engine.h"
#include "GenreLibrary.h"
#include "Score.h"
#include "FscFile.h"
#include "Storage.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace icc
{

namespace ids
{
    constexpr const char* root       = "rootNote";
    constexpr const char* scale      = "scaleIndex";
    constexpr const char* progression = "progressionIndex";
    constexpr const char* bpm        = "bpm";
    constexpr const char* bars       = "bars";
    constexpr const char* seed       = "seed";
    constexpr const char* foliage    = "foliageDensity";
    constexpr const char* snapScale  = "snapScaleEnabled";
    constexpr const char* drumStyle  = "drumStyle";        // choice: 0 = Auto, 1.. = style index + 1
    constexpr const char* drumComplexity = "drumComplexity";
    constexpr const char* swing      = "swing";
    constexpr const char* humanize   = "humanize";
    constexpr const char* fills      = "fills";
    constexpr const char* candidates = "candidates";
    constexpr const char* sendProgram = "sendProgramChange";
    inline juce::String program (int t) { return "program_" + juce::String (t); }
    inline juce::String lock    (int t) { return "lock_"    + juce::String (t); }
    inline juce::String channel (int t) { return "channel_" + juce::String (t); }
    inline juce::String mute    (int t) { return "mute_"    + juce::String (t); }
    inline juce::String solo    (int t) { return "solo_"    + juce::String (t); }
}

class Processor final : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    Processor();
    ~Processor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "InstaComposerClone"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- message-thread API used by the editor ----
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    GenParams paramsFromState() const;
    Routing   routingFromState() const;
    double    bpmFromState() const;
    int       barsFromState() const;

    void generate (uint32_t forceLockMask = 0);   // regenerate from the current parameters into the active scene (extra bits lock more tracks)
    void randomizeSeed();
    Pattern getPattern() const;
    void    setPattern (Pattern);        // editor commits (piano roll); stored into the active scene
    uint32_t getPatternVersion() const noexcept { return patternVersion.load(); }   // bumps when content changes behind the editor's back

    void selectScene (int index);        // quantised to the next bar line while the transport runs
    int  getActiveScene() const noexcept { return activeScene.load(); }
    int  getPendingScene() const noexcept { return pendingScene.load(); }
    bool isScenePopulated (int index) const;

    void setPreviewPlaying (bool);       // free-running internal clock for hosts that are stopped / standalone
    bool isPreviewPlaying() const noexcept { return previewPlaying.load(); }
    bool isTransportRunning() const noexcept { return transportRunning.load(); }
    double getPlayheadTick() const noexcept { return playheadTick.load(); }   // loop-relative

    /** onlyTrack >= 0 renders just that track (used by the per-track drag handles). */
    std::vector<uint8_t> renderMidiFile (bool respectMuteSolo, int onlyTrack = -1) const;

    PatternScore getLastScore() const;                       // score + findings of the last Generate
    juce::String getLastGenerationSummary() const;           // "Best of 4 (variant 3) - score 86"
    std::string  getProgressionText() const;                 // chord names + roman numerals + cadence
    std::string  getMappingText() const;                     // channel / GM program / FL Studio routing guide

    // ---- v2: MIDI import, style profiles (built-in + trained), restyle. All optional; v1 behaviour without them. ----
    struct ImportResult { bool ok = false; juce::String message; };
    ImportResult importMidiFile (const juce::File&, int startBar = 0);   // replaces the active scene's pattern, sets bars and bpm

    int          getNumProfiles() const;                    // built-in styles, then the trained genres
    juce::String getProfileName (int index) const;
    bool         isTrainedProfile (int index) const;
    std::shared_ptr<const StyleProfile> getProfile (int index) const;
    void         setTargetProfile (int index);              // -1 = none
    int          getTargetProfile() const noexcept { return targetProfile.load(); }
    void         setUseTargetForGeneration (bool b) { useTarget.store (b); }
    bool         getUseTargetForGeneration() const noexcept { return useTarget.load(); }
    float        getTargetMatch() const;                    // 0..100 for the current drums, -1 without a target / drums
    juce::String restyleCurrentDrums (float strength);      // moves the Rhythm track toward the target; returns a one-line result
    GenreLibrary& getGenres() noexcept { return genres; }
    void         selectTargetByName (const juce::String& key);   // "b:3" built-in, "g:Name" trained; used by state restore
    juce::String targetKey() const;

    // ---- v2.1: FL Studio scores (.fsc). One score = the notes of one track. The writer copies a real FL-saved score when it has one. ----
    std::vector<uint8_t> renderFsc (int track) const;
    bool         learnFscTemplate (const juce::File&);         // a .fsc saved by FL with at least one note; remembered across sessions
    bool         hasFscTemplate() const;
    juce::String getFscDiagnosis() const { return lastFscDiagnosis; }   // why the last Learn failed (structure dump)
    ImportResult importFscFile (const juce::File&, int track); // replaces that track's notes (grows Bars if the score is longer)
    juce::String describeFlexHints() const;                    // per track: the FLEX browser search word for its GM program

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void parameterChanged (const juce::String&, float) override;
    void timerCallback() override;

    void publish (int sceneTag, bool quantised);          // message thread: build + hand a table to the audio thread
    void captureActiveScene();                            // snapshot live state into the active scene (dataLock held)
    void applySceneToParameters (const Scene&);           // message thread, dataLock NOT held
    void setParam (const juce::String& id, float plainValue);
    void pruneTables();

    // audio-thread helpers
    void releaseAll (juce::MidiBuffer&, int offset);
    void releaseTrack (juce::MidiBuffer&, int track, int offset);
    void renderRange (const Playback&, double a, double b, double blockStart, double ticksPerSample,
                      int numSamples, juce::MidiBuffer&);
    void handleEvent (const Playback&, const PlaybackEvent&, int offset, juce::MidiBuffer&);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* bpmParam = nullptr;

    GenreLibrary          genres;
    mutable juce::CriticalSection fscLock;
    FscTemplate           fscTemplate = defaultFscTemplate();
    juce::String          lastFscDiagnosis;
    void                  loadFscTemplate();
    static juce::File     fscTemplateFile();
    std::atomic<int>      targetProfile { -1 };
    std::atomic<bool>     useTarget { false };

    // message-thread data, guarded by dataLock (state calls may arrive from other threads)
    mutable juce::CriticalSection dataLock;
    Pattern    pattern;
    GenParams  lastParams;
    SceneBank  scenes;
    PatternScore lastScore;
    int          lastVariant = 0, lastVariants = 1;

    struct Owned { std::unique_ptr<Playback> table; double lastActive; };
    std::vector<Owned> owned;

    std::atomic<int>      activeScene { 0 };
    std::atomic<int>      pendingScene { -1 };
    std::atomic<uint32_t> patternVersion { 0 };
    std::atomic<bool>     paramsDirty { false };
    std::atomic<bool>     loading { false };
    std::atomic<bool>     previewPlaying { false };

    // lock-free hand-off to the audio thread
    std::atomic<const Playback*> current { nullptr };
    std::atomic<const Playback*> pendingTable { nullptr };
    std::atomic<int>             appliedSignal { -1 };
    std::atomic<bool>            transportRunning { false };
    std::atomic<double>          playheadTick { 0.0 };

    // audio-thread-only state
    enum class Source { Stopped, Host, Internal };
    Source          lastSource = Source::Stopped;
    const Playback* lastTable = nullptr;
    double          origin = 0.0;          // pattern restarts here (set when a scene switch lands)
    double          currentRate = 0.0;     // written in prepareToPlay (never concurrently with processBlock)
    std::array<int, kNumTracks> sentProgram {}, sentChannel {};   // last program change emitted per track (-1 = none)
    bool            programsPending = true;
    double          internalTick = 0.0;
    double          expectedTick = 0.0;
    bool            haveExpected = false;
    std::array<std::array<signed char, 128>, kNumTracks> onChannel {};   // 0 = silent, else channel the note-on used
    std::array<int, kNumTracks> soundingCount {};
    int             totalSounding = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace icc
