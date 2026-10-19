#include "Score.h"
#include "Harmony.h"
#include "Phrase.h"

#include <algorithm>
#include <cmath>

namespace icc
{

namespace
{
    float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

    std::vector<Note> sortedByStart (std::vector<Note> v)
    {
        std::stable_sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
        return v;
    }

    bool chordHasPc (const ChordInfo& c, int pitch)
    {
        for (int n : c.notes()) if (pcOf (n) == pcOf (pitch)) return true;
        return false;
    }

    float scoreMelody (const Pattern& p, const std::vector<ChordInfo>& chords, std::vector<std::string>& notes)
    {
        const auto mel = sortedByStart (p[Melody]);
        if (mel.size() < 3) { notes.push_back ("Melody is very sparse"); return 0.3f; }

        int steps = 0, leaps = 0, repeats = 0, lo = 127, hi = 0, strong = 0, strongHit = 0;
        for (size_t i = 0; i < mel.size(); ++i)
        {
            lo = std::min (lo, mel[i].pitch); hi = std::max (hi, mel[i].pitch);
            if (i > 0)
            {
                const int d = std::abs (mel[i].pitch - mel[i - 1].pitch);
                if (d <= 2) ++steps;
                if (d > 12) ++leaps;
                if (d == 0) ++repeats;
            }
            if (mel[i].start % kPPQ == 0)
            {
                ++strong;
                if (chordHasPc (chords[(size_t) (mel[i].start / kBarTicks) % chords.size()], mel[i].pitch)) ++strongHit;
            }
        }
        const float n = (float) (mel.size() - 1);
        const float stepFrac = (float) steps / n, repFrac = (float) repeats / n;
        const float strongFrac = strong > 0 ? (float) strongHit / (float) strong : 0.5f;
        const int   range = hi - lo;
        const float rangeScore = range < 5 ? 0.3f : range <= 19 ? 1.0f : clamp01 (1.0f - (float) (range - 19) / 12.0f);

        if (leaps > 0)         notes.push_back ("Melody leaps more than an octave " + std::to_string (leaps) + (leaps == 1 ? " time" : " times"));
        if (strongFrac < 0.45f) notes.push_back ("Few chord tones on the strong beats of the melody");
        if (repFrac > 0.4f)    notes.push_back ("Melody repeats the same note a lot");
        if (range < 5)         notes.push_back ("Melody range is very narrow");

        return clamp01 (0.30f * clamp01 (stepFrac / 0.5f) + 0.30f * clamp01 (strongFrac / 0.7f) + 0.15f * (1.0f - clamp01 ((float) leaps / 3.0f))
                        + 0.10f * (1.0f - clamp01 (repFrac / 0.5f)) + 0.15f * rangeScore);
    }

    float scoreVoicing (const Pattern& p, int bars, std::vector<std::string>& notes)
    {
        std::vector<std::vector<int>> voices ((size_t) bars);
        for (const auto& n : p[Chord])
        {
            const int bar = n.start / kBarTicks;
            if (bar >= 0 && bar < bars && n.start % kBarTicks == 0) voices[(size_t) bar].push_back (n.pitch);
        }
        for (auto& v : voices) std::sort (v.begin(), v.end());

        float movement = 0.0f; int pairsCounted = 0, parallels = 0;
        for (int b = 1; b < bars; ++b)
        {
            const auto &a = voices[(size_t) b - 1], &c = voices[(size_t) b];
            if (a.empty() || c.empty()) continue;
            const size_t m = std::min (a.size(), c.size());
            for (size_t i = 0; i < m; ++i) { movement += (float) std::abs (c[i] - a[i]); ++pairsCounted; }

            bool found = false;
            for (size_t i = 0; i < m && ! found; ++i)
                for (size_t j = i + 1; j < m && ! found; ++j)
                {
                    const int i1 = (a[j] - a[i]) % 12, i2 = (c[j] - c[i]) % 12;
                    const int mi = c[i] - a[i], mj = c[j] - a[j];
                    if (i1 == i2 && (i1 == 7 || i1 == 0) && mi != 0 && mj != 0 && (mi > 0) == (mj > 0))
                    {
                        ++parallels; found = true;
                        notes.push_back (std::string ("Parallel ") + (i1 == 7 ? "fifths" : "octaves") + " in the chords, bar "
                                         + std::to_string (b) + " to " + std::to_string (b + 1));
                    }
                }
        }
        const float avg = pairsCounted > 0 ? movement / (float) pairsCounted : 0.0f;
        return clamp01 (1.0f - clamp01 (avg / 8.0f) * 0.6f - (float) parallels * 0.15f);
    }

    float scoreRhythm (const Pattern& p, int bars, std::vector<std::string>& notes)
    {
        int kickOnOne = 0, backbeat = 0, busy = 0;
        for (int b = 0; b < bars; ++b)
        {
            bool k = false, s = false; int count = 0;
            for (const auto& n : p[Rhythm])
            {
                if (n.start / kBarTicks != b) continue;
                ++count;
                if ((n.pitch == gm::kick) && n.start % kBarTicks < 40) k = true;
                if ((n.pitch == 38 || n.pitch == 39 || n.pitch == 37 || n.pitch == 40) && n.velocity >= 60) s = true;
            }
            kickOnOne += k; backbeat += s; busy += (count >= 6 && count <= 64);
        }
        const float kf = (float) kickOnOne / (float) bars, sf = (float) backbeat / (float) bars, bf = (float) busy / (float) bars;
        if (kf < 0.5f) notes.push_back ("Drums often skip the downbeat kick");
        if (sf < 0.5f) notes.push_back ("Drums have no clear snare / clap backbeat");

        // bass should line up with the kick (or deliberately avoid it): reward a clear choice either way
        int aligned = 0;
        for (const auto& bn : p[Bass])
            for (const auto& kn : p[Rhythm])
                if (kn.pitch == gm::kick && std::abs (kn.start - bn.start) <= 30) { ++aligned; break; }
        float bassPart = 0.7f;
        if (! p[Bass].empty())
        {
            const float a = (float) aligned / (float) p[Bass].size();
            bassPart = a > 0.45f ? 1.0f : (a < 0.1f ? 0.85f : 0.6f);
        }
        return clamp01 (0.3f * kf + 0.3f * sf + 0.2f * bf + 0.2f * bassPart);
    }
}

PatternScore scorePattern (const Pattern& p, const GenParams& gp)
{
    const int bars = std::clamp (gp.bars, 1, kMaxBars);
    const int rootPc = pcOf (gp.rootPc);
    const auto chords = buildProgression (gp.progressionIndex, rootPc, gp.scaleIndex, bars);
    const auto labels = labelProgression (chords, rootPc, gp.scaleIndex);

    PatternScore s;
    s.melody  = scoreMelody (p, chords, s.notes);
    s.voicing = scoreVoicing (p, bars, s.notes);
    s.rhythm  = scoreRhythm (p, bars, s.notes);

    const auto cad = detectCadence (labels);
    s.cadence = cad == CadenceType::PerfectAuthentic ? 1.0f : cad == CadenceType::Authentic ? 0.9f : cad == CadenceType::Plagal ? 0.85f
              : cad == CadenceType::Backdoor ? 0.8f : cad == CadenceType::Deceptive ? 0.7f : cad == CadenceType::Half ? 0.6f : 0.35f;
    if (cad == CadenceType::None) s.notes.push_back ("No clear cadence into the loop point");

    Rng r (gp.seed);                       // the phrase plan is the first thing a seed draws, so this matches the generator
    const auto plan = makePhrasePlan (bars, r);
    float diff = 0.0f;
    for (int b = 0; b < bars; ++b)
        diff += std::abs (chordTension (chords[(size_t) b], rootPc, gp.scaleIndex) - planAtBar (plan, b).tension);
    s.tension = clamp01 (1.0f - diff / (float) bars * 1.5f);

    s.total = 100.0f * (0.34f * s.melody + 0.14f * s.voicing + 0.22f * s.rhythm + 0.14f * s.cadence + 0.16f * s.tension);
    return s;
}

GenerationResult generateBest (const GenParams& in, const Pattern* locked, uint32_t lockMask)
{
    const int n = std::clamp (in.candidates, 1, 16);
    GenerationResult best;
    best.variants = n;
    float bestScore = -1.0f;

    for (int i = 0; i < n; ++i)
    {
        GenParams g = in;
        g.seed = (uint32_t) (((uint64_t) in.seed + (uint64_t) i * 7919ull) % (uint64_t) (kMaxSeed + 1));
        if (i == 0) g.seed = in.seed;
        auto pat = generatePattern (g, locked, lockMask);
        auto sc  = scorePattern (pat, g);
        if (sc.total > bestScore)
        {
            bestScore = sc.total;
            best.pattern = std::move (pat);
            best.score   = std::move (sc);
            best.seed    = g.seed;
            best.variant = i;
        }
    }
    return best;
}

} // namespace icc
