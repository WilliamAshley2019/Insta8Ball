#include "Storage.h"

namespace icc
{

juce::String patternToBase64 (const Pattern& p)
{
    const auto bytes = encodePattern (p);
    return juce::Base64::toBase64 (bytes.data(), bytes.size());
}

bool patternFromBase64 (const juce::String& text, Pattern& out)
{
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, text)) return false;
    return decodePattern (static_cast<const uint8_t*> (mo.getData()), mo.getDataSize(), out);
}

void paramsToTree (juce::ValueTree& n, const GenParams& g)
{
    n.setProperty ("root", g.rootPc, nullptr);
    n.setProperty ("scale", g.scaleIndex, nullptr);
    n.setProperty ("prog", g.progressionIndex, nullptr);
    n.setProperty ("bars", g.bars, nullptr);
    n.setProperty ("seed", (juce::int64) g.seed, nullptr);
    n.setProperty ("foliage", (double) g.foliageDensity, nullptr);
    n.setProperty ("drumStyle", g.drumStyle, nullptr);
    n.setProperty ("drumComplexity", (double) g.drumComplexity, nullptr);
    n.setProperty ("swing", (double) g.swing, nullptr);
    n.setProperty ("humanize", (double) g.humanize, nullptr);
    n.setProperty ("fills", (double) g.fills, nullptr);
    n.setProperty ("candidates", g.candidates, nullptr);
}

void paramsFromTree (const juce::ValueTree& n, GenParams& g)
{
    auto f = [&n] (const char* id, double def, double lo, double hi) { return (float) juce::jlimit (lo, hi, (double) n.getProperty (id, def)); };
    g.rootPc           = juce::jlimit (0, 11, (int) n.getProperty ("root", 0));
    g.scaleIndex       = juce::jmax (0, (int) n.getProperty ("scale", 0));
    g.progressionIndex = juce::jmax (0, (int) n.getProperty ("prog", 0));
    g.bars             = juce::jlimit (1, kMaxBars, (int) n.getProperty ("bars", 4));
    g.seed             = (uint32_t) juce::jlimit ((juce::int64) 0, (juce::int64) kMaxSeed, (juce::int64) n.getProperty ("seed", 1));
    g.foliageDensity   = f ("foliage", 0.35, 0.0, 1.0);
    g.drumStyle        = juce::jlimit (-1, 255, (int) n.getProperty ("drumStyle", -1));
    g.drumComplexity   = f ("drumComplexity", 0.5, 0.0, 1.0);
    g.swing            = f ("swing", 0.0, 0.0, 1.0);
    g.humanize         = f ("humanize", 0.3, 0.0, 1.0);
    g.fills            = f ("fills", 0.5, 0.0, 1.0);
    g.candidates       = juce::jlimit (1, 16, (int) n.getProperty ("candidates", 1));
}

// ------------------------------------------------------------------ SceneBank
juce::ValueTree SceneBank::toValueTree() const
{
    juce::ValueTree tree ("SCENES");
    for (int i = 0; i < kNumScenes; ++i)
    {
        const auto& s = scenes[(size_t) i];
        juce::ValueTree n ("Scene");
        n.setProperty ("index", i, nullptr);
        n.setProperty ("populated", s.populated, nullptr);
        paramsToTree (n, s.params);
        n.setProperty ("bpm", s.bpm, nullptr);
        n.setProperty ("mute", (int) s.routing.mute, nullptr);
        n.setProperty ("solo", (int) s.routing.solo, nullptr);
        for (int t = 0; t < kNumTracks; ++t)
        {
            n.setProperty ("ch" + juce::String (t), s.routing.channels[(size_t) t], nullptr);
            n.setProperty ("pr" + juce::String (t), s.routing.programs[(size_t) t], nullptr);
        }
        n.setProperty ("sendPC", s.routing.sendProgram, nullptr);
        if (s.populated)
            n.setProperty ("pattern", patternToBase64 (s.pattern), nullptr);
        tree.addChild (n, -1, nullptr);
    }
    return tree;
}

void SceneBank::fromValueTree (const juce::ValueTree& tree)
{
    clear();
    for (const auto& n : tree)
    {
        const int i = n.getProperty ("index", -1);
        if (i < 0 || i >= kNumScenes || ! n.hasType ("Scene")) continue;

        auto& s = scenes[(size_t) i];
        Pattern p;
        if (! (bool) n.getProperty ("populated", false) || ! patternFromBase64 (n.getProperty ("pattern").toString(), p))
            continue;

        paramsFromTree (n, s.params);
        s.bpm                     = juce::jlimit (20.0, 400.0, (double) n.getProperty ("bpm", 120.0));
        s.routing.mute            = (uint32_t) (int) n.getProperty ("mute", 0) & ((1u << kNumTracks) - 1u);
        s.routing.solo            = (uint32_t) (int) n.getProperty ("solo", 0) & ((1u << kNumTracks) - 1u);
        for (int t = 0; t < kNumTracks; ++t)
        {
            s.routing.channels[(size_t) t] = juce::jlimit (1, 16, (int) n.getProperty ("ch" + juce::String (t), kDefaultChannels[(size_t) t]));
            s.routing.programs[(size_t) t] = juce::jlimit (0, 127, (int) n.getProperty ("pr" + juce::String (t), kDefaultPrograms[(size_t) t]));
        }
        s.routing.sendProgram = (bool) n.getProperty ("sendPC", false);

        clampPattern (p, s.params.bars);
        s.pattern   = std::move (p);
        s.populated = true;
    }
}

// ------------------------------------------------------------------ export
static const juce::String kTempPrefix = "icc_";

juce::File writeTempMidiFile (const std::vector<uint8_t>& bytes, const juce::String& extension)
{
    cleanupOldTempMidiFiles();
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile (kTempPrefix + juce::Uuid().toString() + extension);
    if (! file.replaceWithData (bytes.data(), bytes.size()))
        return {};
    return file;
}

void cleanupOldTempMidiFiles (int maxAgeHours)
{
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::hours ((double) maxAgeHours);
    for (const char* ext : { "*.mid", "*.fsc" })
        for (const auto& f : juce::File::getSpecialLocation (juce::File::tempDirectory).findChildFiles (juce::File::findFiles, false, kTempPrefix + ext))
            if (f.getLastModificationTime() < cutoff)
                f.deleteFile();   // age check keeps files another running instance may just have written
}

} // namespace icc
