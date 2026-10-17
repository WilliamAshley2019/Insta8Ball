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
        n.setProperty ("root", s.params.rootPc, nullptr);
        n.setProperty ("scale", s.params.scaleIndex, nullptr);
        n.setProperty ("prog", s.params.progressionIndex, nullptr);
        n.setProperty ("bars", s.params.bars, nullptr);
        n.setProperty ("seed", (juce::int64) s.params.seed, nullptr);
        n.setProperty ("foliage", (double) s.params.foliageDensity, nullptr);
        n.setProperty ("bpm", s.bpm, nullptr);
        n.setProperty ("mute", (int) s.routing.mute, nullptr);
        n.setProperty ("solo", (int) s.routing.solo, nullptr);
        for (int t = 0; t < kNumTracks; ++t)
            n.setProperty ("ch" + juce::String (t), s.routing.channels[(size_t) t], nullptr);
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

        s.params.rootPc           = juce::jlimit (0, 11, (int) n.getProperty ("root", 0));
        s.params.scaleIndex       = juce::jmax (0, (int) n.getProperty ("scale", 0));
        s.params.progressionIndex = juce::jmax (0, (int) n.getProperty ("prog", 0));
        s.params.bars             = juce::jlimit (1, kMaxBars, (int) n.getProperty ("bars", 4));
        s.params.seed             = (uint32_t) juce::jlimit ((juce::int64) 0, (juce::int64) kMaxSeed, (juce::int64) n.getProperty ("seed", 1));
        s.params.foliageDensity   = juce::jlimit (0.0f, 1.0f, (float) (double) n.getProperty ("foliage", 0.35));
        s.bpm                     = juce::jlimit (20.0, 400.0, (double) n.getProperty ("bpm", 120.0));
        s.routing.mute            = (uint32_t) (int) n.getProperty ("mute", 0) & ((1u << kNumTracks) - 1u);
        s.routing.solo            = (uint32_t) (int) n.getProperty ("solo", 0) & ((1u << kNumTracks) - 1u);
        for (int t = 0; t < kNumTracks; ++t)
            s.routing.channels[(size_t) t] = juce::jlimit (1, 16, (int) n.getProperty ("ch" + juce::String (t), kDefaultChannels[(size_t) t]));

        clampPattern (p, s.params.bars);
        s.pattern   = std::move (p);
        s.populated = true;
    }
}

// ------------------------------------------------------------------ export
static const juce::String kTempPrefix = "icc_";

juce::File writeTempMidiFile (const std::vector<uint8_t>& bytes)
{
    cleanupOldTempMidiFiles();
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile (kTempPrefix + juce::Uuid().toString() + ".mid");
    if (! file.replaceWithData (bytes.data(), bytes.size()))
        return {};
    return file;
}

void cleanupOldTempMidiFiles (int maxAgeHours)
{
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::hours ((double) maxAgeHours);
    for (const auto& f : juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .findChildFiles (juce::File::findFiles, false, kTempPrefix + "*.mid"))
        if (f.getLastModificationTime() < cutoff)
            f.deleteFile();   // age check keeps files another running instance may just have written
}

} // namespace icc
