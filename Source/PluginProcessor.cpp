#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Drums.h"
#include "Harmony.h"
#include "Mapping.h"

#include <algorithm>
#include <cmath>

namespace icc
{

// ======================================================================= parameters
juce::AudioProcessorValueTreeState::ParameterLayout Processor::makeLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr int v = 1;

    StringArray notes, scaleNames, progNames, channelNames;
    for (int i = 0; i < 12; ++i) notes.add (noteName (i));
    for (const auto& s : scales()) scaleNames.add (s.name);
    for (const auto& p : progressions()) progNames.add (p.name);
    for (int c = 1; c <= 16; ++c) channelNames.add (String (c));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::root, v }, "Root", notes, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, v }, "Scale", scaleNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::progression, v }, "Progression", progNames, 0));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ids::bpm, v }, "BPM",
                                                        NormalisableRange<float> (40.0f, 240.0f, 0.01f), 120.0f));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { ids::bars, v }, "Bars", 1, kMaxBars, 4));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { ids::seed, v }, "Seed", 0, kMaxSeed, 12345));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ids::foliage, v }, "Foliage Density",
                                                        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ids::snapScale, v }, "Snap To Scale", false));

    StringArray styleNames { "Auto (by seed)" }, programNames;
    for (int i = 0; i < drumStyleCount(); ++i) styleNames.add (drumStyleName (i));
    StringArray kitNames;
    for (int i = 0; i < 128; ++i)
    {
        programNames.add (String (i + 1) + "  " + gmProgramName (i));
        kitNames.add (String (i + 1) + "  " + String (gmDrumKitName (i)));
    }
    auto unit = [] { return NormalisableRange<float> (0.0f, 1.0f, 0.01f); };
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::drumStyle, v }, "Drum Style", styleNames, 0));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ids::drumComplexity, v }, "Drum Complexity", unit(), 0.5f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ids::swing, v }, "Swing", unit(), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ids::humanize, v }, "Humanize", unit(), 0.3f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ids::fills, v }, "Fills", unit(), 0.5f));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { ids::candidates, v }, "Variants", 1, 16, 4));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ids::sendProgram, v }, "Send Program Change", false));

    for (int t = 0; t < kNumTracks; ++t)
    {
        const String n = trackName (t);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::channel (t), v }, n + " Channel",
                                                            channelNames, kDefaultChannels[(size_t) t] - 1));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::mute (t), v }, n + " Mute", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::solo (t), v }, n + " Solo", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::program (t), v }, n + (t == Rhythm ? " Drum Kit" : " GM Program"),
                                                            t == Rhythm ? kitNames : programNames,
                                                            kDefaultPrograms[(size_t) t]));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::lock (t), v }, n + " Lock", false));
    }
    return layout;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ICC_STATE", makeLayout())
{
    bpmParam = apvts.getRawParameterValue (ids::bpm);
    loadFscTemplate();

    apvts.addParameterListener (ids::bars, this);
    apvts.addParameterListener (ids::sendProgram, this);
    sentProgram.fill (-1);
    sentChannel.fill (-1);
    for (int t = 0; t < kNumTracks; ++t)
    {
        apvts.addParameterListener (ids::program (t), this);
        apvts.addParameterListener (ids::channel (t), this);
        apvts.addParameterListener (ids::mute (t), this);
        apvts.addParameterListener (ids::solo (t), this);
    }

    lastParams = paramsFromState();
    publish (-1, false);
    cleanupOldTempMidiFiles();
    startTimer (25);
}

Processor::~Processor()
{
    stopTimer();
    apvts.removeParameterListener (ids::bars, this);
    apvts.removeParameterListener (ids::sendProgram, this);
    for (int t = 0; t < kNumTracks; ++t)
    {
        apvts.removeParameterListener (ids::program (t), this);
        apvts.removeParameterListener (ids::channel (t), this);
        apvts.removeParameterListener (ids::mute (t), this);
        apvts.removeParameterListener (ids::solo (t), this);
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }

bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return l.getMainInputChannelSet().isDisabled()
        && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono() || out.isDisabled());
}

void Processor::prepareToPlay (double sampleRate, int)
{
    currentRate = sampleRate;
    lastTable = nullptr;       // forces a clean note release on the first block
    haveExpected = false;
}

// ======================================================================= state readers
GenParams Processor::paramsFromState() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    GenParams g;
    g.rootPc           = juce::jlimit (0, 11, (int) std::lround (get (ids::root)));
    g.scaleIndex       = juce::jmax (0, (int) std::lround (get (ids::scale)));
    g.progressionIndex = juce::jmax (0, (int) std::lround (get (ids::progression)));
    g.bars             = juce::jlimit (1, kMaxBars, (int) std::lround (get (ids::bars)));
    g.seed             = (uint32_t) juce::jlimit (0, kMaxSeed, (int) std::lround (get (ids::seed)));
    g.foliageDensity   = juce::jlimit (0.0f, 1.0f, get (ids::foliage));
    g.drumStyle        = juce::jlimit (-1, drumStyleCount() - 1, (int) std::lround (get (ids::drumStyle)) - 1);
    g.drumComplexity   = juce::jlimit (0.0f, 1.0f, get (ids::drumComplexity));
    g.swing            = juce::jlimit (0.0f, 1.0f, get (ids::swing));
    g.humanize         = juce::jlimit (0.0f, 1.0f, get (ids::humanize));
    g.fills            = juce::jlimit (0.0f, 1.0f, get (ids::fills));
    g.candidates       = juce::jlimit (1, 16, (int) std::lround (get (ids::candidates)));
    return g;
}

Routing Processor::routingFromState() const
{
    Routing r;
    for (int t = 0; t < kNumTracks; ++t)
    {
        if (apvts.getRawParameterValue (ids::mute (t))->load() > 0.5f) r.mute |= 1u << t;
        if (apvts.getRawParameterValue (ids::solo (t))->load() > 0.5f) r.solo |= 1u << t;
        r.channels[(size_t) t] = juce::jlimit (1, 16, (int) std::lround (apvts.getRawParameterValue (ids::channel (t))->load()) + 1);
        r.programs[(size_t) t] = juce::jlimit (0, 127, (int) std::lround (apvts.getRawParameterValue (ids::program (t))->load()));
    }
    r.sendProgram = apvts.getRawParameterValue (ids::sendProgram)->load() > 0.5f;
    return r;
}

double Processor::bpmFromState() const { return (double) bpmParam->load(); }

int Processor::barsFromState() const
{
    return juce::jlimit (1, kMaxBars, (int) std::lround (apvts.getRawParameterValue (ids::bars)->load()));
}

void Processor::setParam (const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

// ======================================================================= message-thread API
void Processor::captureActiveScene()
{
    lastParams.bars = barsFromState();
    auto& s = scenes.at (activeScene.load());
    if (! s.populated && countNotes (pattern) == 0) return;   // don't turn an untouched empty slot into a stored scene
    s.populated = true;
    s.params    = lastParams;
    s.bpm       = bpmFromState();
    s.routing   = routingFromState();
    s.pattern   = pattern;
}

void Processor::generate (uint32_t forceLockMask)
{
    auto gp = paramsFromState();
    if (useTarget.load()) gp.genre = getProfile (targetProfile.load());

    uint32_t lockMask = forceLockMask;
    for (int t = 0; t < kNumTracks; ++t)
        if (apvts.getRawParameterValue (ids::lock (t))->load() > 0.5f) lockMask |= 1u << t;

    const Pattern existing = getPattern();
    auto result = generateBest (gp, &existing, lockMask);
    {
        const juce::ScopedLock sl (dataLock);
        pattern      = std::move (result.pattern);
        lastParams   = gp;
        lastScore    = std::move (result.score);
        lastVariant  = result.variant;
        lastVariants = result.variants;
        captureActiveScene();
    }
    ++patternVersion;
    publish (-1, false);
}

void Processor::randomizeSeed()
{
    setParam (ids::seed, (float) juce::Random::getSystemRandom().nextInt (kMaxSeed + 1));
}

Pattern Processor::getPattern() const
{
    const juce::ScopedLock sl (dataLock);
    return pattern;
}

void Processor::setPattern (Pattern p)
{
    clampPattern (p, barsFromState());
    {
        const juce::ScopedLock sl (dataLock);
        pattern = std::move (p);
        captureActiveScene();
    }
    publish (-1, false);
}

bool Processor::isScenePopulated (int index) const
{
    const juce::ScopedLock sl (dataLock);
    return scenes.at (index).populated;
}

void Processor::applySceneToParameters (const Scene& s)
{
    setParam (ids::root, (float) s.params.rootPc);
    setParam (ids::scale, (float) s.params.scaleIndex);
    setParam (ids::progression, (float) s.params.progressionIndex);
    setParam (ids::bars, (float) s.params.bars);
    setParam (ids::seed, (float) s.params.seed);
    setParam (ids::foliage, s.params.foliageDensity);
    setParam (ids::drumStyle, (float) (s.params.drumStyle + 1));
    setParam (ids::drumComplexity, s.params.drumComplexity);
    setParam (ids::swing, s.params.swing);
    setParam (ids::humanize, s.params.humanize);
    setParam (ids::fills, s.params.fills);
    setParam (ids::candidates, (float) s.params.candidates);
    setParam (ids::sendProgram, s.routing.sendProgram ? 1.0f : 0.0f);
    setParam (ids::bpm, (float) s.bpm);
    for (int t = 0; t < kNumTracks; ++t)
    {
        setParam (ids::channel (t), (float) (s.routing.channels[(size_t) t] - 1));
        setParam (ids::program (t), (float) s.routing.programs[(size_t) t]);
        setParam (ids::mute (t), (s.routing.mute >> t) & 1u ? 1.0f : 0.0f);
        setParam (ids::solo (t), (s.routing.solo >> t) & 1u ? 1.0f : 0.0f);
    }
}

void Processor::selectScene (int index)
{
    if (index < 0 || index >= kNumScenes) return;

    Scene scene;
    bool populated = false;
    {
        const juce::ScopedLock sl (dataLock);
        activeScene.store (index);
        populated = scenes.at (index).populated;
        if (populated)
        {
            scene      = scenes.at (index);
            pattern    = scene.pattern;
            lastParams = scene.params;
        }
    }

    if (! populated)
    {
        pendingTable.exchange (nullptr);   // an older queued switch must not land on an empty slot
        pendingScene.store (-1);
        return;                            // the slot fills on the next Generate; current playback continues
    }

    loading.store (true);
    applySceneToParameters (scene);
    loading.store (false);
    paramsDirty.store (false);
    ++patternVersion;
    publish (index, true);
}

void Processor::setPreviewPlaying (bool shouldPlay) { previewPlaying.store (shouldPlay); }

std::vector<uint8_t> Processor::renderMidiFile (bool respectMuteSolo, int onlyTrack) const
{
    Pattern p;
    {
        const juce::ScopedLock sl (dataLock);
        p = pattern;
    }
    auto routing = routingFromState();
    if (onlyTrack >= 0 && onlyTrack < kNumTracks)
    {
        routing.solo = 1u << onlyTrack;
        routing.mute = 0;
        respectMuteSolo = true;
    }
    return writeMidiFile (p, bpmFromState(), routing, barsFromState(), respectMuteSolo);
}

PatternScore Processor::getLastScore() const
{
    const juce::ScopedLock sl (dataLock);
    return lastScore;
}

juce::String Processor::getLastGenerationSummary() const
{
    const juce::ScopedLock sl (dataLock);
    if (lastScore.total <= 0.0f) return {};
    juce::String s = "Score " + juce::String (juce::roundToInt (lastScore.total));
    if (lastVariants > 1) s += "  (best of " + juce::String (lastVariants) + ", variant " + juce::String (lastVariant + 1) + ")";
    if (! lastScore.notes.empty()) s += "  -  " + juce::String (lastScore.notes.front());
    return s;
}

std::string Processor::getProgressionText() const
{
    const auto gp = paramsFromState();
    return describeProgression (buildProgression (gp.progressionIndex, gp.rootPc, gp.scaleIndex, gp.bars), gp.rootPc, gp.scaleIndex);
}

std::string Processor::getMappingText() const
{
    const auto r = routingFromState();
    const auto gp = paramsFromState();
    std::string s = describeMapping (r.channels, r.programs, r.sendProgram);
    s += "\n" + describeDrumKeys();
    if (gp.drumStyle >= 0)
        s += "\nDrum style: " + std::string (drumStyleName (gp.drumStyle)) + " (written for about " + std::to_string (drumStyleBpm (gp.drumStyle)) + " BPM)\n";
    s += "\n" + describeFlexHints().toStdString();
    return s;
}

// ======================================================================= publishing
void Processor::publish (int sceneTag, bool quantised)
{
    Playback* table = nullptr;
    {
        const juce::ScopedLock sl (dataLock);
        auto up = std::make_unique<Playback> (buildPlayback (pattern, barsFromState(), routingFromState()));
        table = up.get();
        owned.push_back ({ std::move (up), juce::Time::getMillisecondCounterHiRes() });
    }

    const bool wantQueue = sceneTag >= 0 && quantised && transportRunning.load();
    const bool queued    = pendingTable.load() != nullptr;

    if (wantQueue || queued)
    {
        table->sceneTag = wantQueue ? sceneTag : pendingScene.load();
        pendingTable.store (table);
        if (wantQueue) pendingScene.store (sceneTag);
    }
    else
    {
        current.store (table);
        if (sceneTag >= 0) pendingScene.store (-1);
    }
}

void Processor::pruneTables()
{
    // Tables are freed here (never on the audio thread). One that is no longer referenced is kept for
    // a few seconds so a block that was mid-render when the pointer changed can safely finish.
    const double now = juce::Time::getMillisecondCounterHiRes();
    const Playback* cur  = current.load();
    const Playback* pend = pendingTable.load();

    const juce::ScopedLock sl (dataLock);
    for (auto& o : owned)
        if (o.table.get() == cur || o.table.get() == pend)
            o.lastActive = now;

    owned.erase (std::remove_if (owned.begin(), owned.end(), [now] (const Owned& o) { return now - o.lastActive > 3000.0; }),
                 owned.end());
}

void Processor::parameterChanged (const juce::String&, float)
{
    if (! loading.load())
        paramsDirty.store (true);   // may be called from the audio thread: only set a flag
}

void Processor::timerCallback()
{
    const int applied = appliedSignal.exchange (-1);
    if (applied >= 0 && applied == pendingScene.load())
        pendingScene.store (-1);

    if (paramsDirty.exchange (false))
    {
        {
            const juce::ScopedLock sl (dataLock);
            captureActiveScene();
        }
        publish (-1, false);
    }
    pruneTables();
}

// ======================================================================= persistence
void Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock sl (dataLock);
        juce::ValueTree icc ("ICC");
        icc.setProperty ("version", 1, nullptr);
        icc.setProperty ("activeScene", activeScene.load(), nullptr);
        icc.setProperty ("target", targetKey(), nullptr);
        icc.setProperty ("useTarget", useTarget.load(), nullptr);
        icc.setProperty ("pattern", patternToBase64 (pattern), nullptr);
        paramsToTree (icc, lastParams);
        icc.addChild (scenes.toValueTree(), -1, nullptr);
        state.addChild (icc, -1, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void Processor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid() || tree.getType() != apvts.state.getType()) return;

    auto icc = tree.getChildWithName ("ICC");
    tree.removeChild (icc, nullptr);

    loading.store (true);
    apvts.replaceState (tree);
    loading.store (false);
    paramsDirty.store (false);

    {
        const juce::ScopedLock sl (dataLock);
        scenes.clear();
        pattern = {};
        pendingTable.exchange (nullptr);
        pendingScene.store (-1);

        if (icc.isValid())
        {
            scenes.fromValueTree (icc.getChildWithName ("SCENES"));
            activeScene.store (juce::jlimit (0, kNumScenes - 1, (int) icc.getProperty ("activeScene", 0)));

            Pattern p;
            if (patternFromBase64 (icc.getProperty ("pattern").toString(), p))
            {
                clampPattern (p, barsFromState());
                pattern = std::move (p);
            }
            paramsFromTree (icc, lastParams);
            lastParams.bars = barsFromState();
            selectTargetByName (icc.getProperty ("target").toString());
            useTarget.store ((bool) icc.getProperty ("useTarget", false) && targetProfile.load() >= 0);
        }
        else
        {
            activeScene.store (0);
            lastParams = paramsFromState();
        }
    }
    ++patternVersion;
    publish (-1, false);
}

// ======================================================================= audio thread
void Processor::releaseTrack (juce::MidiBuffer& midi, int track, int offset)
{
    if (soundingCount[(size_t) track] == 0) return;
    auto& row = onChannel[(size_t) track];
    for (int p = 0; p < 128; ++p)
        if (row[(size_t) p] > 0)
        {
            midi.addEvent (juce::MidiMessage::noteOff ((int) row[(size_t) p], p), offset);
            row[(size_t) p] = 0;
            --totalSounding;
        }
    soundingCount[(size_t) track] = 0;
}

void Processor::releaseAll (juce::MidiBuffer& midi, int offset)
{
    if (totalSounding <= 0) { totalSounding = 0; return; }
    for (int t = 0; t < kNumTracks; ++t) releaseTrack (midi, t, offset);
}

void Processor::handleEvent (const Playback& pb, const PlaybackEvent& e, int offset, juce::MidiBuffer& midi)
{
    auto& slot = onChannel[(size_t) e.track][(size_t) e.pitch];

    if (e.velocity == 0)
    {
        if (slot > 0)
        {
            midi.addEvent (juce::MidiMessage::noteOff ((int) slot, e.pitch), offset);
            slot = 0;
            --soundingCount[(size_t) e.track];
            --totalSounding;
        }
        return;
    }

    if (! pb.routing.audible (e.track)) return;

    if (slot > 0)   // retrigger of a still-sounding pitch: close it first
    {
        midi.addEvent (juce::MidiMessage::noteOff ((int) slot, e.pitch), offset);
        --soundingCount[(size_t) e.track];
        --totalSounding;
    }
    const int ch = pb.routing.channelOf (e.track);
    midi.addEvent (juce::MidiMessage::noteOn (ch, e.pitch, (juce::uint8) e.velocity), offset);
    slot = (signed char) ch;
    ++soundingCount[(size_t) e.track];
    ++totalSounding;
}

void Processor::renderRange (const Playback& pb, double a, double b, double blockStart, double tps,
                             int numSamples, juce::MidiBuffer& midi)
{
    if (b <= a || pb.events.empty() || pb.loopTicks <= 0) return;

    const double L  = (double) pb.loopTicks;
    const double la = a - origin, lb = b - origin;

    long long k = (long long) std::floor (la / L);
    for (int guard = 0; guard < 8 && (double) k * L < lb; ++guard, ++k)
    {
        const double base   = (double) k * L;
        const double segLo  = std::max (la, base) - base;
        const double segHi  = std::min (lb, base + L) - base;

        auto it = std::lower_bound (pb.events.begin(), pb.events.end(), (int) std::ceil (segLo),
                                    [] (const PlaybackEvent& e, int tick) { return e.tick < tick; });
        for (; it != pb.events.end() && (double) it->tick < segHi; ++it)
        {
            const double abs = base + (double) it->tick + origin;
            const int offset = juce::jlimit (0, numSamples - 1, (int) ((abs - blockStart) / tps));
            handleEvent (pb, *it, offset, midi);
        }
    }
}

void Processor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();                                   // generator: silent audio, incoming MIDI passes through

    const int    n  = audio.getNumSamples();
    const double sr = currentRate > 0.0 ? currentRate : getSampleRate();
    if (n <= 0 || sr <= 0.0) return;

    // ---- transport ----
    double bpm = (double) bpmParam->load();
    bool hostRunning = false, havePpq = false;
    double hostTick = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostRunning = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) { havePpq = true; hostTick = *p * (double) kPPQ; }
        }
    bpm = juce::jlimit (20.0, 400.0, bpm);

    const Source src = (hostRunning && havePpq) ? Source::Host
                     : (previewPlaying.load() ? Source::Internal : Source::Stopped);

    const Playback* cur = current.load (std::memory_order_acquire);
    if (cur != lastTable) { releaseAll (midi, 0); lastTable = cur; }

    if (src != lastSource)
    {
        releaseAll (midi, 0);
        lastSource = src;
        programsPending = true;
        haveExpected = false;
        origin = 0.0;
        if (src == Source::Internal) internalTick = 0.0;
    }

    transportRunning.store (src != Source::Stopped, std::memory_order_relaxed);

    if (src == Source::Stopped)
    {
        if (auto* p = pendingTable.exchange (nullptr))    // nothing is playing, so a queued switch can land now
        {
            current.store (p, std::memory_order_release);
            lastTable = cur = p;
            appliedSignal.store (p->sceneTag);
        }
        return;
    }

    if (cur != nullptr && cur->routing.sendProgram)   // GM program per channel, at the start and whenever it changes
    {
        for (int t = 0; t < kNumTracks; ++t)
        {
            const int ch = cur->routing.channelOf (t), pr = juce::jlimit (0, 127, cur->routing.programs[(size_t) t]);
            if (programsPending || sentProgram[(size_t) t] != pr || sentChannel[(size_t) t] != ch)
            {
                midi.addEvent (juce::MidiMessage::programChange (ch, pr), 0);
                sentProgram[(size_t) t] = pr;
                sentChannel[(size_t) t] = ch;
            }
        }
        programsPending = false;
    }

    const double t0  = (src == Source::Host) ? hostTick : internalTick;
    const double tps = bpm / 60.0 * (double) kPPQ / sr;
    const double t1  = t0 + (double) n * tps;

    if (src == Source::Host && haveExpected && std::abs (t0 - expectedTick) > 2.0)
        releaseAll (midi, 0);                         // host jumped (locate / loop)

    if (cur == nullptr) { expectedTick = t1; haveExpected = true; return; }

    for (int t = 0; t < kNumTracks; ++t)              // a mute / solo change must silence sounding notes
        if (soundingCount[(size_t) t] > 0 && ! cur->routing.audible (t))
            releaseTrack (midi, t, 0);

    double split = -1.0;
    if (pendingTable.load (std::memory_order_acquire) != nullptr)
    {
        const double bar = std::ceil (t0 / (double) kBarTicks) * (double) kBarTicks;
        if (bar < t1) split = bar;
    }

    if (split >= 0.0)
    {
        renderRange (*cur, t0, split, t0, tps, n, midi);
        const int offset = juce::jlimit (0, n - 1, (int) ((split - t0) / tps));
        if (auto* p = pendingTable.exchange (nullptr))
        {
            releaseAll (midi, offset);
            current.store (p, std::memory_order_release);
            lastTable = cur = p;
            origin = split;                           // the new scene starts from its first bar
            appliedSignal.store (p->sceneTag);
        }
        renderRange (*cur, split, t1, t0, tps, n, midi);
    }
    else
        renderRange (*cur, t0, t1, t0, tps, n, midi);

    expectedTick = t1;
    haveExpected = true;
    if (src == Source::Internal) internalTick = t1;

    const double L = (double) std::max (1, cur->loopTicks);
    double local = std::fmod (t1 - origin, L);
    if (local < 0.0) local += L;
    playheadTick.store (local, std::memory_order_relaxed);
}

} // namespace icc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new icc::Processor();
}
