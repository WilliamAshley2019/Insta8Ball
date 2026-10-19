#include "Engine.h"
#include "Drums.h"
#include "Phrase.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace icc
{

// =============================================================== utilities
void Rng::reseed (uint32_t seed)
{
    uint64_t z = (uint64_t) seed + 0x9E3779B97F4A7C15ull;   // splitmix64 scramble so small seeds diverge
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    state = (z ^ (z >> 31)) | 1ull;
}

uint32_t Rng::next()
{
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return (uint32_t) ((state * 0x2545F4914F6CDD1Dull) >> 32);   // xorshift64*
}

float Rng::nextFloat() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }

int Rng::nextInt (int n)
{
    return n <= 1 ? 0 : (int) (((uint64_t) next() * (uint64_t) n) >> 32);
}

void sortPattern (Pattern& p)
{
    for (auto& t : p)
        std::stable_sort (t.begin(), t.end(), [] (const Note& a, const Note& b)
                          { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
}

size_t countNotes (const Pattern& p)
{
    size_t n = 0;
    for (auto& t : p) n += t.size();
    return n;
}

void clampPattern (Pattern& p, int bars)
{
    const int loop = std::max (1, bars) * kBarTicks;
    for (auto& t : p)
    {
        for (auto& n : t)
        {
            n.pitch    = std::clamp (n.pitch, 0, 127);
            n.velocity = std::clamp (n.velocity, 1, 127);
            n.length   = std::max (1, n.length);
            if (n.start >= 0 && n.start < loop) n.length = std::min (n.length, loop - n.start);
        }
        t.erase (std::remove_if (t.begin(), t.end(), [loop] (const Note& n) { return n.start < 0 || n.start >= loop; }),
                 t.end());
    }
    sortPattern (p);
}

// =============================================================== generators
namespace
{
    constexpr int kStep16 = kBarTicks / 16;   // 240
    constexpr int kEighth = kBarTicks / 8;    // 480

    struct Ctx
    {
        GenParams          p;
        const Scale*       scale = nullptr;
        Rng                rng;
        std::vector<ChordInfo> chords;
        Pattern            out;
        std::vector<PhrasePlan> plan;
        DrumResult         drums;                       // anchors + kicks for bass / melody, even when the drum track is locked
        std::vector<std::vector<int>> rhythmOnsets;     // per bar, tick positions the melody lands on

        const ChordInfo& chordForBar (int bar) const { return chords[(size_t) bar % chords.size()]; }
    };

    void addNote (Ctx& c, int track, int start, int length, int pitch, int velocity)
    {
        if (pitch < 0 || pitch > 127 || length <= 0 || start < 0) return;
        c.out[(size_t) track].push_back ({ start, length, pitch, std::clamp (velocity, 1, 127) });
    }

    template <typename T> T pick (const std::vector<T>& v, Rng& r) { return v[(size_t) r.nextInt ((int) v.size())]; }

    struct Weighted { int pitch; float weight; };

    int weightedPick (const std::vector<Weighted>& c, Rng& rng)
    {
        if (c.empty()) return -1;
        float total = 0.0f;
        for (auto& w : c) total += std::max (0.0f, w.weight);
        if (total <= 0.0f) return c.front().pitch;
        float r = rng.nextFloat() * total;
        for (auto& w : c)
        {
            r -= std::max (0.0f, w.weight);
            if (r <= 0.0f) return w.pitch;
        }
        return c.back().pitch;
    }

    float proximity (int pitch, int ref)
    {
        if (ref < 0) return 1.0f;
        return std::clamp (1.0f - (float) std::abs (pitch - ref) / 24.0f, 0.15f, 1.0f);
    }

    // ------------------------------------------------ Chord
    void genChord (Ctx& c)
    {
        std::vector<int> prev;
        for (int bar = 0; bar < c.p.bars; ++bar)
        {
            auto voiced = voiceLead (c.chordForBar (bar).notes(), prev, 55, 79);
            for (int n : voiced)
                addNote (c, Chord, bar * kBarTicks, kBarTicks * 95 / 100, n, 76 + c.rng.nextInt (9));
            prev = voiced;
        }
    }

    // ------------------------------------------------ Bass
    int fifthOf (const ChordShape& s)
    {
        for (int iv : s.intervals) if (iv == 6 || iv == 7 || iv == 8) return iv;
        return 7;
    }

    constexpr int kBassLo = 28, kBassHi = 48, kBassVel = 92;

    void genBassEighths (Ctx& c, int bar)
    {
        const auto& ch    = c.chordForBar (bar);
        const auto& next  = c.chordForBar (bar + 1);
        const int   root  = wrapIntoRange (ch.rootMidi, kBassLo, kBassHi);
        const int   fifth = wrapIntoRange (ch.rootMidi + fifthOf (chordShapeAt (ch.shape)), kBassLo, kBassHi);
        const int   base  = bar * kBarTicks;

        const bool variation = c.rng.nextFloat() < 0.25f;
        const bool pickup    = c.rng.nextFloat() < 0.35f;
        const int  gate      = kEighth * 70 / 100;

        const int pitches[4] = { root, fifth, root, fifth };
        for (int i = 0; i < 4; ++i)
        {
            int len = gate;
            if (variation && i == 3) len = kEighth / 2 - 10;
            addNote (c, Bass, base + i * 2 * kEighth, len, pitches[i], i % 2 == 0 ? kBassVel : kBassVel * 85 / 100);
        }
        if (variation)
            addNote (c, Bass, base + 6 * kEighth + kEighth / 2, kEighth / 2 - 10,
                     wrapIntoRange (fifth + 12, kBassLo, kBassHi), kBassVel * 75 / 100);
        if (pickup)
        {
            const int approach = snapToScale (*c.scale, c.p.rootPc, next.rootMidi - 1);
            addNote (c, Bass, base + 7 * kEighth, kEighth / 2, wrapIntoRange (approach, kBassLo, kBassHi), kBassVel * 90 / 100);
        }
    }

    // Bass notes sit on the kick hits, so kick and bass read as one instrument.
    void genBassKickLock (Ctx& c, int bar)
    {
        const auto& kicks = c.drums.kicks[(size_t) bar];
        if (kicks.empty()) { genBassEighths (c, bar); return; }

        const auto& ch    = c.chordForBar (bar);
        const auto& next  = c.chordForBar (bar + 1);
        const int   root  = wrapIntoRange (ch.rootMidi, kBassLo, kBassHi);
        const int   fifth = wrapIntoRange (ch.rootMidi + fifthOf (chordShapeAt (ch.shape)), kBassLo, kBassHi);
        const int   barEnd = (bar + 1) * kBarTicks;

        const bool approachNote = c.rng.nextFloat() < 0.3f && kicks.back() < barEnd - 2 * kStep16;
        const int  lastEnd      = approachNote ? barEnd - kStep16 : barEnd;   // keep the last kick note clear of the approach note

        for (size_t i = 0; i < kicks.size(); ++i)
        {
            const int start = kicks[i];
            const int gap   = (i + 1 < kicks.size() ? kicks[i + 1] : lastEnd) - start;
            int len = std::clamp ((int) ((float) gap * c.drums.sustain), 90, kPPQ * 2);
            len = std::min (len, std::max (60, gap - 12));

            int pitch = root;
            if (i > 0)
            {
                const float r = c.rng.nextFloat();
                if (r < 0.22f) pitch = fifth;
                else if (r < 0.34f && root + 12 <= kBassHi) pitch = root + 12;
            }
            addNote (c, Bass, start, len, pitch, kBassVel + (i == 0 ? 8 : -4) + c.rng.nextInt (6));
        }
        if (approachNote)   // approach note into the next chord
        {
            const int approach = snapToScale (*c.scale, c.p.rootPc, next.rootMidi - 1);
            addNote (c, Bass, barEnd - kStep16, kStep16 - 20, wrapIntoRange (approach, kBassLo, kBassHi), kBassVel * 85 / 100);
        }
    }

    // House / techno: bass on the off-beat eighths, leaving the kick alone.
    void genBassOffbeat (Ctx& c, int bar)
    {
        const auto& ch   = c.chordForBar (bar);
        const int   root = wrapIntoRange (ch.rootMidi, kBassLo, kBassHi);
        const int   fifth = wrapIntoRange (ch.rootMidi + fifthOf (chordShapeAt (ch.shape)), kBassLo, kBassHi);
        for (int beat = 0; beat < 4; ++beat)
        {
            const int pitch = (beat == 3 && c.rng.nextFloat() < 0.2f) ? fifth : root;
            addNote (c, Bass, bar * kBarTicks + beat * kPPQ + kEighth, kEighth * 75 / 100, pitch, kBassVel - 4 + c.rng.nextInt (8));
        }
    }

    // Disco / synthwave: running eighths jumping an octave.
    void genBassPulse (Ctx& c, int bar)
    {
        const auto& ch   = c.chordForBar (bar);
        const int   root = wrapIntoRange (ch.rootMidi, kBassLo, kBassHi - 12);
        const int   fifth = wrapIntoRange (ch.rootMidi + fifthOf (chordShapeAt (ch.shape)), kBassLo, kBassHi - 12);
        for (int i = 0; i < 8; ++i)
        {
            int pitch = (i % 2 == 0) ? root : root + 12;
            if (i == 5 && c.rng.nextFloat() < 0.3f) pitch = fifth + 12;
            addNote (c, Bass, bar * kBarTicks + i * kEighth, kEighth * (int) (c.drums.sustain * 100.0f) / 100 - 10, pitch,
                     i % 2 == 0 ? kBassVel : kBassVel * 80 / 100);
        }
    }

    void genBass (Ctx& c)
    {
        for (int bar = 0; bar < c.p.bars; ++bar)
            switch (c.drums.bassMode)
            {
                case BassMode::KickLock: genBassKickLock (c, bar); break;
                case BassMode::Offbeat:  genBassOffbeat (c, bar);  break;
                case BassMode::Pulse:    genBassPulse (c, bar);    break;
                default:                 genBassEighths (c, bar);  break;
            }
    }

    // ------------------------------------------------ Rhythm
    void genRhythm (Ctx& c, bool locked)
    {
        if (locked)
        {
            c.drums = analyseDrums (c.out[Rhythm], c.p.bars);
        }
        else
        {
            DrumSettings ds;
            ds.style = c.p.drumStyle; ds.complexity = c.p.drumComplexity; ds.swing = c.p.swing;
            ds.humanize = c.p.humanize; ds.fills = c.p.fills; ds.bars = c.p.bars;
            c.drums = generateDrums (ds, c.plan, c.rng);
            c.out[Rhythm] = c.drums.notes;
        }
        c.rhythmOnsets = c.drums.anchors;
    }

    // ------------------------------------------------ Melody
    float contourTarget (int contour, float t, float centre, int& walk, Rng& rng)
    {
        switch (contour)
        {
            case 0:  return centre - 4.0f + 10.0f * std::sin (3.14159265f * t);   // arch
            case 1:  return centre - 6.0f + 12.0f * t;                            // ascend
            case 2:  return centre + 6.0f - 12.0f * t;                            // descend
            default: walk = std::clamp (walk + rng.nextInt (5) - 2, -6, 6);       // wander
                     return centre + (float) walk;
        }
    }

    void genMelody (Ctx& c)
    {
        constexpr int   lo = 60, hi = 84;
        constexpr float phraseRest = 0.5f, chordBias = 0.75f;
        const float wRoot = 1.0f, wThird = 1.2f, wFifth = 0.9f, wSeventh = 0.7f, wColour = 0.5f;

        int prev = -1, contour = c.rng.nextInt (4), walk = 0;

        for (int bar = 0; bar < c.p.bars; ++bar)
        {
            const auto& pp = planAtBar (c.plan, bar);
            if (isFirstBarOfPhrase (pp, bar)) contour = c.rng.nextInt (4);

            const auto& onsets = c.rhythmOnsets[(size_t) bar];
            if (onsets.empty()) continue;

            const float usage = 0.42f + 0.4f * pp.activity;
            const bool  lastBarOfPhrase = isLastBarOfPhrase (pp, bar);
            std::vector<int> chosen;
            for (size_t i = 0; i < onsets.size(); ++i)
            {
                if (lastBarOfPhrase && ! pp.shouldResolve && i + 1 == onsets.size() && c.rng.nextFloat() < phraseRest) continue;
                if ((isFirstBarOfPhrase (pp, bar) && i == 0) || c.rng.nextFloat() < usage) chosen.push_back (onsets[i]);
            }
            if (chosen.empty()) chosen.push_back (onsets.front());

            const auto& chord = c.chordForBar (bar);
            const auto& shape = chordShapeAt (chord.shape);

            for (size_t i = 0; i < chosen.size(); ++i)
            {
                const int start = chosen[i];
                const int end   = i + 1 < chosen.size() ? chosen[i + 1] : (bar + 1) * kBarTicks;
                const int len   = std::clamp ((end - start) * 85 / 100, 60, kPPQ * 2);

                const float t      = ((float) (bar - pp.startBar) + (float) i / (float) chosen.size()) / (float) pp.lengthBars;
                const int   target = (int) std::lround (contourTarget (contour, t, (float) pp.targetPitch, walk, c.rng));
                const bool  resolve = pp.shouldResolve && lastBarOfPhrase && i + 1 == chosen.size();

                std::vector<Weighted> cands;
                for (int iv : shape.intervals)
                {
                    const int m = iv % 12;
                    float w = m == 0 ? wRoot : (m == 3 || m == 4) ? wThird : (m >= 6 && m <= 8) ? wFifth
                            : (m == 10 || m == 11) ? wSeventh : wColour;
                    if (resolve) w = (m == 0) ? 4.0f : (m == 3 || m == 4 || (m >= 6 && m <= 8)) ? 2.0f : 0.0f;   // land on a stable chord tone
                    for (int oct = -3; oct <= 3; ++oct)
                    {
                        const int p = chord.rootMidi + iv + 12 * oct;
                        if (p >= lo && p <= hi) cands.push_back ({ p, w * (resolve ? 1.0f : chordBias) });
                    }
                }
                if (! resolve)
                    for (int p = lo; p <= hi; ++p)
                        if (inScale (*c.scale, c.p.rootPc, p)) cands.push_back ({ p, (1.0f - chordBias) * 0.7f });

                for (auto& w : cands)
                {
                    w.weight *= proximity (w.pitch, prev);
                    w.weight *= std::clamp (1.0f + (12.0f - (float) std::abs (w.pitch - target)) / 12.0f, 0.3f, 3.0f);
                }

                const int pitch = weightedPick (cands, c.rng);
                if (pitch < 0) continue;

                const int vel = 72 + (int) (8.0f * pp.activity) + c.rng.nextInt (25) + (start % kPPQ == 0 ? 6 : 0);
                addNote (c, Melody, start, resolve ? std::max (len, kPPQ) : len, pitch, vel);
                prev = pitch;
            }
        }
    }

    // ------------------------------------------------ Pad
    void genPad (Ctx& c)
    {
        constexpr int lo = 58, hi = 82;
        std::vector<int> prev;
        for (int bar = 0; bar < c.p.bars; ++bar)
        {
            const auto& chord = c.chordForBar (bar);
            const auto& shape = chordShapeAt (chord.shape);

            int third = -1, fifth = -1, seventh = -1;
            for (int iv : shape.intervals)
            {
                if (third < 0 && (iv == 3 || iv == 4 || iv == 2 || iv == 5)) third = iv;
                else if (fifth < 0 && (iv == 6 || iv == 7 || iv == 8)) fifth = iv;
                else if (seventh < 0 && (iv == 9 || iv == 10 || iv == 11)) seventh = iv;
            }
            std::vector<int> tones { 0 };
            if (third >= 0) tones.push_back (third);
            const bool useSeventh = seventh >= 0 && c.rng.nextFloat() < 0.6f;
            if (useSeventh) tones.push_back (seventh);
            else if (fifth >= 0) tones.push_back (fifth);

            const bool dissonantBase = shape.name == "dim" || shape.name == "aug" || shape.name == "dim7";
            if (tones.size() >= 3 && ! dissonantBase && ! useSeventh && c.rng.nextFloat() < 0.3f)
                tones.back() = 14;   // add-nine colour instead of the fifth

            std::vector<int> notes;
            for (int iv : tones) notes.push_back (chord.rootMidi + iv);
            auto voiced = voiceLead (notes, prev, lo, hi);
            for (int n : voiced) addNote (c, Pad, bar * kBarTicks, kBarTicks * 98 / 100, n, 48);
            prev = voiced;
        }
    }

    // ------------------------------------------------ Arp
    void genArp (Ctx& c)
    {
        constexpr int lo = 60, hi = 96, steps = 16;
        int order = c.rng.nextInt (4);   // 0 up, 1 down, 2 up-down, 3 random

        for (int bar = 0; bar < c.p.bars; ++bar)
        {
            if (bar > 0 && c.rng.nextFloat() < 0.25f) order = c.rng.nextInt (4);

            const auto& chord = c.chordForBar (bar);
            std::vector<int> tones;
            for (int iv : chordShapeAt (chord.shape).intervals)
            {
                const int p = wrapIntoRange (chord.rootMidi + iv, lo, lo + 11);
                tones.push_back (p);
                if (p + 12 <= hi) tones.push_back (p + 12);
            }
            std::sort (tones.begin(), tones.end());
            tones.erase (std::unique (tones.begin(), tones.end()), tones.end());
            const int n = (int) tones.size();
            if (n == 0) continue;

            for (int s = 0; s < steps; ++s)
            {
                int idx = 0;
                switch (order)
                {
                    case 0:  idx = s % n; break;
                    case 1:  idx = n - 1 - (s % n); break;
                    case 2:  { const int period = std::max (1, 2 * n - 2); const int ph = s % period; idx = ph < n ? ph : period - ph; break; }
                    default: idx = c.rng.nextInt (n); break;
                }
                if (c.rng.nextFloat() < 0.10f) continue;   // rest
                const int vel = 70 + c.rng.nextInt (17) - 8;
                addNote (c, Arp, bar * kBarTicks + s * kStep16, kStep16 / 2, tones[(size_t) idx], vel);
            }
        }
    }

    // ------------------------------------------------ Foliage
    void genFoliage (Ctx& c)
    {
        constexpr int lo = 60, hi = 84;
        const float density = std::clamp (c.p.foliageDensity, 0.0f, 1.0f);
        auto melody = c.out[Melody];
        if (density <= 0.0f || melody.size() < 2) return;
        std::stable_sort (melody.begin(), melody.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

        for (size_t i = 0; i + 1 < melody.size(); ++i)
        {
            const Note& a = melody[i];
            const Note& b = melody[i + 1];
            const int gapStart = a.start + a.length;
            const int slots    = (b.start - gapStart) / kEighth;
            if (slots < 1) continue;

            for (int slot = 0; slot < slots; ++slot)
            {
                if (c.rng.nextFloat() >= density) continue;

                int at = (int) std::lround ((double) (gapStart + slot * kEighth) / kEighth) * kEighth;   // snap to the eighth grid
                if (at < gapStart || at >= b.start) continue;
                const int len = std::min (kEighth * 80 / 100, b.start - at);
                if (len < 60) continue;

                const auto& chord = c.chordForBar (at / kBarTicks);
                std::vector<Weighted> cands;
                for (int iv : chordShapeAt (chord.shape).intervals)
                    cands.push_back ({ wrapIntoRange (chord.rootMidi + iv, lo, hi), 1.0f });
                for (int d : { 1, -1 })
                {
                    const int p = wrapIntoRange (snapToScale (*c.scale, c.p.rootPc, a.pitch + d), lo, hi);
                    if (p != a.pitch) cands.push_back ({ p, 1.2f });
                }
                for (int d : { 12, -12 })
                    if (a.pitch + d >= lo && a.pitch + d <= hi) cands.push_back ({ a.pitch + d, 0.4f });

                const int pitch = weightedPick (cands, c.rng);
                const int vel   = std::clamp ((int) std::lround ((float) std::max (a.velocity, b.velocity) * 0.65f), 30, 80);
                if (pitch >= 0) addNote (c, Foliage, at, len, pitch, vel);
            }
        }
    }
}

Pattern generatePattern (const GenParams& in, const Pattern* locked, uint32_t lockMask)
{
    Ctx c;
    c.p        = in;
    c.p.bars   = std::clamp (in.bars, 1, kMaxBars);
    c.p.rootPc = pcOf (in.rootPc);
    c.scale    = &scaleAt (in.scaleIndex);
    c.rng.reseed (in.seed);
    c.chords   = buildProgression (in.progressionIndex, c.p.rootPc, in.scaleIndex, c.p.bars);
    c.plan     = makePhrasePlan (c.p.bars, c.rng);

    auto isLocked = [&] (int t) { return locked != nullptr && ((lockMask >> t) & 1u) != 0; };
    for (int t = 0; t < kNumTracks; ++t)
        if (isLocked (t)) c.out[(size_t) t] = (*locked)[(size_t) t];

    if (! isLocked (Chord)) genChord (c);
    genRhythm (c, isLocked (Rhythm));      // drums first: bass and melody lock to them
    if (! isLocked (Bass)) genBass (c);
    if (! isLocked (Melody)) genMelody (c);
    if (! isLocked (Pad)) genPad (c);
    if (! isLocked (Arp)) genArp (c);
    if (! isLocked (Foliage)) genFoliage (c);

    clampPattern (c.out, c.p.bars);
    return std::move (c.out);
}

// =============================================================== playback table
Playback buildPlayback (const Pattern& pattern, int bars, const Routing& routing)
{
    Playback pb;
    pb.routing   = routing;
    pb.loopTicks = std::clamp (bars, 1, kMaxBars) * kBarTicks;

    size_t total = 0;
    for (auto& t : pattern) total += t.size() * 2;
    pb.events.reserve (total);

    for (int t = 0; t < kNumTracks; ++t)
        for (const auto& n : pattern[(size_t) t])
        {
            if (n.start < 0 || n.start >= pb.loopTicks) continue;
            const int end = std::min (n.start + std::max (1, n.length), pb.loopTicks);
            pb.events.push_back ({ n.start, t, std::clamp (n.pitch, 0, 127), std::clamp (n.velocity, 1, 127) });
            pb.events.push_back ({ end >= pb.loopTicks ? pb.loopTicks - 1 : end, t, std::clamp (n.pitch, 0, 127), 0 });
        }

    std::stable_sort (pb.events.begin(), pb.events.end(), [] (const PlaybackEvent& a, const PlaybackEvent& b)
                      { return a.tick != b.tick ? a.tick < b.tick : (a.velocity == 0) > (b.velocity == 0); });
    return pb;
}

// =============================================================== serialisation
namespace
{
    void put32 (std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((uint8_t) (x >> (8 * i))); }
    uint32_t get32 (const uint8_t* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }
    constexpr size_t kNoteBytes = 10;
}

std::vector<uint8_t> encodePattern (const Pattern& p)
{
    std::vector<uint8_t> v { 'I', 'C', 'P', '1' };
    for (auto& t : p)
    {
        put32 (v, (uint32_t) t.size());
        for (auto& n : t)
        {
            put32 (v, (uint32_t) n.start);
            put32 (v, (uint32_t) n.length);
            v.push_back ((uint8_t) n.pitch);
            v.push_back ((uint8_t) n.velocity);
        }
    }
    return v;
}

bool decodePattern (const uint8_t* d, size_t size, Pattern& out)
{
    if (d == nullptr || size < 4 || std::memcmp (d, "ICP1", 4) != 0) return false;
    Pattern result;
    size_t pos = 4;
    for (auto& t : result)
    {
        if (pos + 4 > size) return false;
        const uint32_t count = get32 (d + pos); pos += 4;
        if (count > 200000 || pos + (size_t) count * kNoteBytes > size) return false;
        t.reserve (count);
        for (uint32_t i = 0; i < count; ++i, pos += kNoteBytes)
        {
            Note n;
            n.start    = (int) get32 (d + pos);
            n.length   = (int) get32 (d + pos + 4);
            n.pitch    = d[pos + 8];
            n.velocity = d[pos + 9];
            t.push_back (n);
        }
    }
    out = std::move (result);
    return true;
}

// =============================================================== MIDI file
namespace
{
    void putVlq (std::vector<uint8_t>& v, uint32_t x)
    {
        uint8_t buf[5]; int n = 0;
        buf[n++] = x & 0x7F;
        while ((x >>= 7) != 0) buf[n++] = (uint8_t) ((x & 0x7F) | 0x80);
        while (n > 0) v.push_back (buf[--n]);
    }
    void putBE32 (std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back ((uint8_t) (x >> (8 * i))); }
    void putBE16 (std::vector<uint8_t>& v, uint32_t x) { v.push_back ((uint8_t) (x >> 8)); v.push_back ((uint8_t) x); }

    void putMeta (std::vector<uint8_t>& v, uint32_t delta, uint8_t type, const std::vector<uint8_t>& data)
    {
        putVlq (v, delta); v.push_back (0xFF); v.push_back (type); putVlq (v, (uint32_t) data.size());
        v.insert (v.end(), data.begin(), data.end());
    }

    void endChunk (std::vector<uint8_t>& file, const std::vector<uint8_t>& body)
    {
        file.insert (file.end(), { 'M', 'T', 'r', 'k' });
        putBE32 (file, (uint32_t) body.size());
        file.insert (file.end(), body.begin(), body.end());
    }
}

std::vector<uint8_t> writeMidiFile (const Pattern& pattern, double bpm, const Routing& routing, int bars, bool respectMuteSolo)
{
    std::vector<uint8_t> f { 'M', 'T', 'h', 'd' };
    putBE32 (f, 6); putBE16 (f, 1); putBE16 (f, 1 + kNumTracks); putBE16 (f, (uint32_t) kPPQ);

    const uint32_t endTick = (uint32_t) (std::clamp (bars, 1, kMaxBars) * kBarTicks);
    auto text = [] (const char* s) { return std::vector<uint8_t> (s, s + std::strlen (s)); };

    {   // conductor
        std::vector<uint8_t> b;
        putMeta (b, 0, 0x03, text ("InstaComposerClone"));
        const uint32_t us = (uint32_t) std::lround (60000000.0 / std::clamp (bpm, 20.0, 400.0));
        putMeta (b, 0, 0x51, { (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us });
        putMeta (b, 0, 0x58, { 4, 2, 24, 8 });
        putMeta (b, endTick, 0x2F, {});
        endChunk (f, b);
    }

    for (int t = 0; t < kNumTracks; ++t)
    {
        struct Ev { uint32_t tick; int order; uint8_t status, d1, d2; };
        std::vector<Ev> evs;
        if (! respectMuteSolo || routing.audible (t))
        {
            const int ch = routing.channelOf (t) - 1;
            for (const auto& n : pattern[(size_t) t])
            {
                if (n.start < 0 || n.start >= (int) endTick) continue;
                const uint32_t off = (uint32_t) std::min (n.start + std::max (1, n.length), (int) endTick);
                evs.push_back ({ (uint32_t) n.start, 1, (uint8_t) (0x90 | ch), (uint8_t) std::clamp (n.pitch, 0, 127), (uint8_t) std::clamp (n.velocity, 1, 127) });
                evs.push_back ({ off, 0, (uint8_t) (0x80 | ch), (uint8_t) std::clamp (n.pitch, 0, 127), 0 });
            }
            std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b)
                              { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
        }

        std::vector<uint8_t> b;
        putMeta (b, 0, 0x03, text (trackName (t)));
        if (routing.sendProgram && (! respectMuteSolo || routing.audible (t)))   // GM program (drum kit on channel 10)
        {
            b.push_back (0x00); b.push_back ((uint8_t) (0xC0 | (routing.channelOf (t) - 1)));
            b.push_back ((uint8_t) std::clamp (routing.programs[(size_t) t], 0, 127));
        }
        uint32_t last = 0;
        for (const auto& e : evs)
        {
            putVlq (b, e.tick - last); last = e.tick;
            b.push_back (e.status); b.push_back (e.d1); b.push_back (e.d2);
        }
        putMeta (b, std::max (last, endTick) - last, 0x2F, {});
        endChunk (f, b);
    }
    return f;
}

} // namespace icc

