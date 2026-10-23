// PluginProcessorV2.cpp - the optional v2 layer of the processor: MIDI import, style profiles
// (the 16 built-in grooves plus any trained genres), restyle. Nothing here runs unless the user asks for it.

#include "PluginProcessor.h"
#include "Corpus.h"
#include "FscFile.h"
#include "InstrumentInfo.h"
#include "MidiImport.h"
#include "StyleProfile.h"

namespace icc
{

int Processor::getNumProfiles() const { return drumStyleCount() + genres.count(); }

bool Processor::isTrainedProfile (int index) const { return index >= drumStyleCount() && index < getNumProfiles(); }

juce::String Processor::getProfileName (int index) const
{
    if (index < 0 || index >= getNumProfiles()) return {};
    if (index < drumStyleCount()) return juce::String (drumStyleName (index)) + " (built-in)";
    return genres.nameAt (index - drumStyleCount());
}

std::shared_ptr<const StyleProfile> Processor::getProfile (int index) const
{
    if (index < 0 || index >= getNumProfiles()) return nullptr;
    if (index < drumStyleCount()) return std::make_shared<StyleProfile> (builtinProfile (index));
    return genres.profileAt (index - drumStyleCount());
}

void Processor::setTargetProfile (int index)
{
    targetProfile.store (index >= 0 && index < getNumProfiles() ? index : -1);
    if (targetProfile.load() < 0) useTarget.store (false);
}

juce::String Processor::targetKey() const
{
    const int t = targetProfile.load();
    if (t < 0 || t >= getNumProfiles()) return {};
    return t < drumStyleCount() ? "b:" + juce::String (t) : "g:" + genres.nameAt (t - drumStyleCount());
}

void Processor::selectTargetByName (const juce::String& key)
{
    if (key.startsWith ("b:")) { setTargetProfile (key.substring (2).getIntValue()); return; }
    if (key.startsWith ("g:")) { const int i = genres.indexOfName (key.substring (2)); setTargetProfile (i < 0 ? -1 : drumStyleCount() + i); return; }
    setTargetProfile (-1);
}

float Processor::getTargetMatch() const
{
    const auto prof = getProfile (targetProfile.load());
    if (prof == nullptr) return -1.0f;
    const auto p = getPattern();
    if (p[Rhythm].empty()) return -1.0f;
    return styleMatch (p[Rhythm], barsFromState(), *prof);
}

juce::String Processor::restyleCurrentDrums (float strength)
{
    const auto prof = getProfile (targetProfile.load());
    if (prof == nullptr) return "Pick a target style first.";
    auto p = getPattern();
    if (p[Rhythm].empty()) return "There are no drums to restyle. Generate or import some first.";
    const int bars = barsFromState();
    const float before = styleMatch (p[Rhythm], bars, *prof);
    p[Rhythm] = icc::restyleDrums (p[Rhythm], bars, *prof, strength, paramsFromState().seed);
    const float after = styleMatch (p[Rhythm], bars, *prof);
    setPattern (std::move (p));
    ++patternVersion;
    return "Restyled toward " + getProfileName (targetProfile.load()) + ": match " + juce::String (juce::roundToInt (before)) + "% -> "
           + juce::String (juce::roundToInt (after)) + "%";
}

Processor::ImportResult Processor::importMidiFile (const juce::File& f, int startBar)
{
    juce::MemoryBlock mb;
    if (! f.existsAsFile() || f.getSize() > 32 * 1024 * 1024 || ! f.loadFileAsData (mb)) return { false, "Could not read that file." };

    const auto m = importMidi (static_cast<const uint8_t*> (mb.getData()), mb.getSize());
    if (! m.ok) return { false, "Not a usable MIDI file: " + juce::String (m.error) };

    auto mapped = importToPattern (m, juce::jmax (0, startBar), kMaxBars);
    const int notes = (int) countNotes (mapped.pattern);
    if (notes == 0) return { false, "No notes found" + juce::String (startBar > 0 ? " after bar " + juce::String (startBar + 1) : "") + "." };

    juce::String instrumentLine;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int src = mapped.source[(size_t) t];
        if (src < 0 || src >= (int) m.tracks.size()) continue;
        const auto id = classifyTrack (m.tracks[(size_t) src], m.systemMode);
        if (id.program >= 0) setParam (ids::program (t), (float) id.program);   // Send PC stays as the user left it
        if (id.source != InstrumentSource::Unknown)
            instrumentLine += (instrumentLine.isEmpty() ? "" : ", ") + juce::String (trackName (t)) + " = " + juce::String (id.label()) + (id.nameOverrodeZero ? " (from name; file said program 1)" : "");
    }

    setParam (ids::bars, (float) mapped.bars);
    if (m.bpm >= 40.0 && m.bpm <= 240.0) setParam (ids::bpm, (float) m.bpm);
    setPattern (std::move (mapped.pattern));
    ++patternVersion;

    int used = 0;
    const auto p = getPattern();
    for (const auto& t : p) used += t.empty() ? 0 : 1;

    juce::String msg = "Imported " + juce::String (notes) + " notes in " + juce::String (used) + " tracks, " + juce::String (mapped.bars)
                       + (mapped.bars == 1 ? " bar" : " bars") + " at " + juce::String (juce::roundToInt (m.bpm)) + " BPM.";
    if (mapped.oddMeter)            msg += " The file is " + juce::String (m.tsNum) + "/" + juce::String (m.tsDen) + ", ICC is 4/4: bars were cut at 4/4 boundaries.";
    if (mapped.droppedTracks > 0)   msg += " " + juce::String (mapped.droppedTracks) + " extra track(s) did not fit and were left out.";
    if (m.barsNeeded() >= kMaxBars && m.lengthTicks > kMaxBars * kBarTicks) msg += " Only " + juce::String (kMaxBars) + " bars fit; use Start bar for later sections.";
    if (m.multiTempo)               msg += " Tempo changes were ignored.";
    if (m.systemMode != SystemMode::Unknown) msg += " Sound set: " + juce::String (systemModeName (m.systemMode)) + ".";
    if (instrumentLine.isNotEmpty()) msg += " Instruments: " + instrumentLine + ".";
    if (m.hangingNotes + m.orphanOffs + m.retriggers > 0)
        msg += " Repaired " + juce::String (m.hangingNotes) + " hanging, " + juce::String (m.orphanOffs) + " orphan, " + juce::String (m.retriggers) + " retriggered notes.";
    return { true, msg };
}


// ======================================================================= FL Studio scores
juce::File Processor::fscTemplateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("AlphaAudio").getChildFile ("InstaComposerClone").getChildFile ("fsc_template.icf");
}

void Processor::loadFscTemplate()
{
    juce::MemoryBlock mb;
    FscTemplate t;
    if (fscTemplateFile().existsAsFile() && fscTemplateFile().getSize() < (4 << 20) && fscTemplateFile().loadFileAsData (mb)
        && templateFromBytes (static_cast<const uint8_t*> (mb.getData()), mb.getSize(), t))
    {
        const juce::ScopedLock sl (fscLock);
        fscTemplate = std::move (t);
    }
}

bool Processor::hasFscTemplate() const { const juce::ScopedLock sl (fscLock); return fscTemplate.learned; }

bool Processor::learnFscTemplate (const juce::File& f)
{
    lastFscDiagnosis.clear();
    juce::MemoryBlock mb;
    if (! f.existsAsFile() || f.getSize() > 64 * 1024 * 1024 || ! f.loadFileAsData (mb)) { lastFscDiagnosis = "Could not read the file."; return false; }
    FscTemplate t;
    if (! icc::learnFscTemplate (static_cast<const uint8_t*> (mb.getData()), mb.getSize(), t))
    {
        lastFscDiagnosis = juce::String (diagnoseFsc (static_cast<const uint8_t*> (mb.getData()), mb.getSize()));
        return false;
    }
    const auto bytes = templateToBytes (t);
    fscTemplateFile().getParentDirectory().createDirectory();
    fscTemplateFile().replaceWithData (bytes.data(), bytes.size());
    const juce::ScopedLock sl (fscLock);
    fscTemplate = std::move (t);
    return true;
}

std::vector<uint8_t> Processor::renderFsc (int track) const
{
    track = juce::jlimit (0, kNumTracks - 1, track);
    std::vector<Note> notes;
    {
        const juce::ScopedLock sl (dataLock);
        notes = pattern[(size_t) track];
    }
    FscTemplate t;
    {
        const juce::ScopedLock sl (fscLock);
        t = fscTemplate;
    }
    return writeFsc (notes, t);
}

Processor::ImportResult Processor::importFscFile (const juce::File& f, int track)
{
    juce::MemoryBlock mb;
    if (! f.existsAsFile() || f.getSize() > 64 * 1024 * 1024 || ! f.loadFileAsData (mb)) return { false, "Could not read that file." };
    const auto* d = static_cast<const uint8_t*> (mb.getData());
    const auto s = readFsc (d, mb.getSize());
    if (! s.ok) return { false, "Not a usable FL score: " + juce::String (s.error) };

    track = juce::jlimit (0, kNumTracks - 1, track);
    auto notes = fscToNotes (s);
    int end = 0;
    for (const auto& n : notes) end = juce::jmax (end, n.start + n.length);
    const int bars = juce::jlimit (1, kMaxBars, (end + kBarTicks - 1) / kBarTicks);
    const int curBars = barsFromState();
    const int useBars = juce::jmax (curBars, bars);
    const int limit = useBars * kBarTicks;
    notes.erase (std::remove_if (notes.begin(), notes.end(), [limit] (const Note& n) { return n.start >= limit; }), notes.end());
    for (auto& n : notes) n.length = juce::jmin (n.length, limit - n.start);

    if (! hasFscTemplate()) learnFscTemplate (f);      // a real FL score teaches the writer its layout
    if (useBars != curBars) setParam (ids::bars, (float) useBars);
    auto p = getPattern();
    p[(size_t) track] = std::move (notes);
    setPattern (std::move (p));
    ++patternVersion;
    return { true, "Imported " + juce::String ((int) s.notes.size()) + " notes into " + juce::String (trackName (track)) + " (score resolution " + juce::String (s.ppq) + " PPQ"
                   + (s.automationEvents > 0 ? ", automation ignored" : "") + ")." };
}

juce::String Processor::describeFlexHints() const
{
    const auto r = routingFromState();
    juce::String s = "FLEX / FL Studio instrument search words (a score cannot select the sound, so load these yourself):\n";
    for (int t = 0; t < kNumTracks; ++t)
        s += juce::String ("  ") + trackName (t) + ": " + flexSearchHint (r.programs[(size_t) t], t == Rhythm) + "  [" + (t == Rhythm ? juce::String (gmDrumKitName (r.programs[(size_t) t])) : juce::String (gmProgramName (r.programs[(size_t) t]))) + "]\n";
    return s;
}

} // namespace icc
