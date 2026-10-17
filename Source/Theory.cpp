#include "Theory.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace icc
{

const char* trackName (int track) noexcept
{
    static const char* names[kNumTracks] = { "Melody", "Chord", "Bass", "Rhythm", "Pad", "Arp", "Foliage" };
    return (track >= 0 && track < kNumTracks) ? names[track] : "?";
}

int pcOf (int midiNote) noexcept { return ((midiNote % 12) + 12) % 12; }

const char* noteName (int pc) noexcept
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[pcOf (pc)];
}

int wrapIntoRange (int note, int lo, int hi) noexcept
{
    if (hi < lo) std::swap (lo, hi);
    while (note < lo) note += 12;
    while (note > hi) note -= 12;
    return std::clamp (note, lo, hi);
}

// ------------------------------------------------------------------ scales
const std::vector<Scale>& scales()
{
    static const std::vector<Scale> list = []
    {
        std::vector<Scale> s;
        auto add = [&s] (const char* n, const char* c, std::vector<int> iv) { s.push_back ({ n, c, std::move (iv) }); };

        add ("Major (Ionian)",           "Modes",      { 0, 2, 4, 5, 7, 9, 11 });
        add ("Dorian",                   "Modes",      { 0, 2, 3, 5, 7, 9, 10 });
        add ("Phrygian",                 "Modes",      { 0, 1, 3, 5, 7, 8, 10 });
        add ("Lydian",                   "Modes",      { 0, 2, 4, 6, 7, 9, 11 });
        add ("Mixolydian",               "Modes",      { 0, 2, 4, 5, 7, 9, 10 });
        add ("Aeolian (Natural Minor)",  "Modes",      { 0, 2, 3, 5, 7, 8, 10 });
        add ("Locrian",                  "Modes",      { 0, 1, 3, 5, 6, 8, 10 });

        add ("Harmonic Minor",           "Minor",      { 0, 2, 3, 5, 7, 8, 11 });
        add ("Melodic Minor",            "Minor",      { 0, 2, 3, 5, 7, 9, 11 });
        add ("Harmonic Major",           "Major",      { 0, 2, 4, 5, 7, 8, 11 });
        add ("Dorian b2",                "Minor",      { 0, 1, 3, 5, 7, 9, 10 });
        add ("Lydian Augmented",         "Minor",      { 0, 2, 4, 6, 8, 9, 11 });
        add ("Lydian Dominant",          "Minor",      { 0, 2, 4, 6, 7, 9, 10 });
        add ("Mixolydian b6",            "Minor",      { 0, 2, 4, 5, 7, 8, 10 });
        add ("Locrian #2",               "Minor",      { 0, 2, 3, 5, 6, 8, 10 });
        add ("Altered (Super Locrian)",  "Minor",      { 0, 1, 3, 4, 6, 8, 10 });

        add ("Major Pentatonic",         "Pentatonic", { 0, 2, 4, 7, 9 });
        add ("Minor Pentatonic",         "Pentatonic", { 0, 3, 5, 7, 10 });
        add ("Major Blues",              "Blues",      { 0, 2, 3, 4, 7, 9 });
        add ("Minor Blues",              "Blues",      { 0, 3, 5, 6, 7, 10 });

        add ("Whole Tone",               "Symmetric",  { 0, 2, 4, 6, 8, 10 });
        add ("Diminished (W-H)",         "Symmetric",  { 0, 2, 3, 5, 6, 8, 9, 11 });
        add ("Diminished (H-W)",         "Symmetric",  { 0, 1, 3, 4, 6, 7, 9, 10 });
        add ("Chromatic",                "Symmetric",  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });

        add ("Phrygian Dominant",        "Exotic",     { 0, 1, 4, 5, 7, 8, 10 });
        add ("Hungarian Minor",          "Exotic",     { 0, 2, 3, 6, 7, 8, 11 });
        add ("Double Harmonic",          "Exotic",     { 0, 1, 4, 5, 7, 8, 11 });
        add ("Hirajoshi",                "Exotic",     { 0, 2, 3, 7, 8 });
        add ("In Sen",                   "Exotic",     { 0, 1, 5, 7, 10 });
        add ("Yo",                       "Exotic",     { 0, 2, 5, 7, 9 });
        add ("Iwato",                    "Exotic",     { 0, 1, 5, 6, 10 });
        add ("Kumoi",                    "Exotic",     { 0, 2, 3, 7, 9 });
        return s;
    }();
    return list;
}

const Scale& scaleAt (int index) noexcept
{
    const auto& l = scales();
    return l[(size_t) std::clamp (index, 0, (int) l.size() - 1)];
}

bool inScale (const Scale& s, int rootPc, int midiNote) noexcept
{
    const int rel = pcOf (midiNote - rootPc);
    return std::find (s.intervals.begin(), s.intervals.end(), rel) != s.intervals.end();
}

int snapToScale (const Scale& s, int rootPc, int midiNote) noexcept
{
    if (s.intervals.empty()) return midiNote;
    for (int d = 0; d <= 6; ++d)
    {
        if (inScale (s, rootPc, midiNote - d)) return midiNote - d;   // lower neighbour first: ties go down
        if (inScale (s, rootPc, midiNote + d)) return midiNote + d;
    }
    return midiNote;
}

// ------------------------------------------------------------------ chords
const std::vector<ChordShape>& chordShapes()
{
    static const std::vector<ChordShape> list = []
    {
        std::vector<ChordShape> s;
        auto add = [&s] (const char* n, const char* sym, std::vector<int> iv) { s.push_back ({ n, sym, std::move (iv) }); };

        add ("maj",     "",      { 0, 4, 7 });
        add ("min",     "m",     { 0, 3, 7 });
        add ("dim",     "dim",   { 0, 3, 6 });
        add ("aug",     "+",     { 0, 4, 8 });
        add ("sus2",    "sus2",  { 0, 2, 7 });
        add ("sus4",    "sus4",  { 0, 5, 7 });
        add ("maj6",    "6",     { 0, 4, 7, 9 });
        add ("min6",    "m6",    { 0, 3, 7, 9 });
        add ("maj7",    "maj7",  { 0, 4, 7, 11 });
        add ("min7",    "m7",    { 0, 3, 7, 10 });
        add ("dom7",    "7",     { 0, 4, 7, 10 });
        add ("min7b5",  "m7b5",  { 0, 3, 6, 10 });
        add ("dim7",    "dim7",  { 0, 3, 6, 9 });
        add ("minMaj7", "mMaj7", { 0, 3, 7, 11 });
        add ("aug7",    "+7",    { 0, 4, 8, 10 });
        add ("augMaj7", "+maj7", { 0, 4, 8, 11 });
        add ("add9",    "add9",  { 0, 4, 7, 14 });
        add ("madd9",   "madd9", { 0, 3, 7, 14 });
        add ("maj9",    "maj9",  { 0, 4, 7, 11, 14 });
        add ("min9",    "m9",    { 0, 3, 7, 10, 14 });
        add ("dom9",    "9",     { 0, 4, 7, 10, 14 });
        add ("dom7b9",  "7b9",   { 0, 4, 7, 10, 13 });
        add ("dom7#9",  "7#9",   { 0, 4, 7, 10, 15 });
        add ("dom7#11", "7#11",  { 0, 4, 7, 10, 18 });
        add ("dom13",   "13",    { 0, 4, 7, 10, 21 });
        add ("maj7#11", "maj7#11", { 0, 4, 7, 11, 18 });
        add ("min11",   "m11",   { 0, 3, 7, 10, 17 });
        add ("maj13",   "maj13", { 0, 4, 7, 11, 21 });
        return s;
    }();
    return list;
}

const ChordShape& chordShapeAt (int index) noexcept
{
    const auto& l = chordShapes();
    return l[(size_t) std::clamp (index, 0, (int) l.size() - 1)];
}

int chordShapeIndex (const std::string& name) noexcept
{
    const auto& l = chordShapes();
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i].name == name) return (int) i;
    return -1;
}

std::vector<int> ChordInfo::notes() const
{
    std::vector<int> n;
    for (int iv : chordShapeAt (shape).intervals) n.push_back (rootMidi + iv);
    return n;
}

// ------------------------------------------------------------ progressions
const std::vector<ProgressionDef>& progressions()
{
    static const std::vector<ProgressionDef> list = {
        { "I - V - vi - IV",                 { 0, 4, 5, 3 },                false },
        { "ii - V - I (7ths)",               { 1, 4, 0, 0 },                true  },
        { "I - vi - IV - V",                 { 0, 5, 3, 4 },                false },
        { "vi - IV - I - V",                 { 5, 3, 0, 4 },                false },
        { "I - IV - V - I",                  { 0, 3, 4, 0 },                false },
        { "I - vi - ii - V (7ths)",          { 0, 5, 1, 4 },                true  },
        { "i - VI - III - VII (minor)",      { 0, 5, 2, 6 },                false },
        { "i - iv - v - i (minor)",          { 0, 3, 4, 0 },                false },
        { "i - VII - VI - VII (minor)",      { 0, 6, 5, 6 },                false },
        { "I - iii - IV - V",                { 0, 2, 3, 4 },                false },
        { "Canon: I V vi iii IV I IV V",     { 0, 4, 5, 2, 3, 0, 3, 4 },    false },
        { "i - iv - VII - III (minor)",      { 0, 3, 6, 2 },                false },
    };
    return list;
}

static int findShape (const std::vector<int>& iv)
{
    const auto& l = chordShapes();
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i].intervals == iv) return (int) i;
    return -1;
}

std::vector<ChordInfo> buildProgression (int progressionIndex, int rootPc, int scaleIndex, int bars)
{
    std::vector<ChordInfo> out;
    const auto& defs = progressions();
    const auto& def  = defs[(size_t) std::clamp (progressionIndex, 0, (int) defs.size() - 1)];

    const Scale* sc = &scaleAt (scaleIndex);
    if (sc->intervals.size() != 7)
    {
        const bool minorish = inScale (*sc, 0, 3) && ! inScale (*sc, 0, 4);
        sc = &scaleAt (minorish ? 5 : 0);   // Aeolian / Ionian
    }
    const auto& iv = sc->intervals;

    for (int bar = 0; bar < std::max (1, bars); ++bar)
    {
        const int d = def.degrees[(size_t) bar % def.degrees.size()] % 7;
        auto off = [&] (int step) { return pcOf (iv[(size_t) (d + step) % 7] - iv[(size_t) d]); };

        std::vector<int> tri { 0, off (2), off (4) };
        std::vector<int> sev { 0, off (2), off (4), off (6) };
        std::sort (tri.begin(), tri.end());
        std::sort (sev.begin(), sev.end());

        int shape = findShape (def.sevenths ? sev : tri);
        if (shape < 0) shape = findShape (tri);
        if (shape < 0) shape = chordShapeIndex (tri[1] == 3 ? "min" : "maj");

        ChordInfo c;
        c.rootMidi = 60 + pcOf (rootPc + iv[(size_t) d]);
        c.shape    = shape;
        out.push_back (c);
    }
    return out;
}

// ------------------------------------------------------------ voice leading
std::vector<int> voiceLead (const std::vector<int>& notes, const std::vector<int>& previous, int lo, int hi)
{
    if (notes.empty()) return notes;
    std::vector<int> base = notes;
    std::sort (base.begin(), base.end());

    std::vector<int> best = base;
    long bestCost = std::numeric_limits<long>::max();

    for (size_t inv = 0; inv < base.size(); ++inv)
    {
        std::vector<int> cand = base;
        for (size_t i = 0; i < inv; ++i) cand[i] += 12;
        std::sort (cand.begin(), cand.end());

        for (int shift = -36; shift <= 36; shift += 12)
        {
            long cost = 0;
            std::vector<int> c = cand;
            for (auto& n : c)
            {
                n += shift;
                if (n < lo) cost += 12 + (lo - n) * 4;
                if (n > hi) cost += 12 + (n - hi) * 4;
            }

            if (previous.empty())
            {
                double mean = 0; for (int n : c) mean += n; mean /= (double) c.size();
                cost += (long) std::abs (mean - 0.5 * (lo + hi)) * 2 + (long) inv * 3;
            }
            else
            {
                const size_t m = std::min (c.size(), previous.size());
                for (size_t i = 0; i < m; ++i) cost += std::abs (c[i] - previous[i]) * 2;
                cost += (long) std::abs ((int) c.size() - (int) previous.size()) * 3;
                cost += (long) inv;
            }

            if (cost < bestCost) { bestCost = cost; best = std::move (c); }
        }
    }
    return best;
}

} // namespace icc
