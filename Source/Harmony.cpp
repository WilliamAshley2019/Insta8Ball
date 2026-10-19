#include "Harmony.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace icc
{

const char* functionName (HarmonicFunction f) noexcept
{
    switch (f) { case HarmonicFunction::Tonic: return "Tonic"; case HarmonicFunction::Predominant: return "Predominant";
                 case HarmonicFunction::Dominant: return "Dominant"; default: return "Other"; }
}

const char* cadenceName (CadenceType c) noexcept
{
    switch (c)
    {
        case CadenceType::PerfectAuthentic: return "perfect authentic (V-I)";
        case CadenceType::Authentic:        return "authentic";
        case CadenceType::Plagal:           return "plagal (IV-I)";
        case CadenceType::Backdoor:         return "backdoor (bVII-I)";
        case CadenceType::Deceptive:        return "deceptive (V-vi)";
        case CadenceType::Half:             return "half cadence (ends on V)";
        default:                            return "none";
    }
}

namespace
{
    bool minorish (const Scale& s) noexcept { return inScale (s, 0, 3) && ! inScale (s, 0, 4); }

    bool hasInterval (const ChordInfo& c, int iv)
    {
        for (int x : chordShapeAt (c.shape).intervals) if (x % 12 == iv) return true;
        return false;
    }

    bool minorThird (const ChordInfo& c) { return hasInterval (c, 3) && ! hasInterval (c, 4); }

    std::string suffixFor (const std::string& shape)
    {
        static const std::pair<const char*, const char*> table[] = {
            { "maj", "" }, { "min", "" }, { "dim", "\xC2\xB0" }, { "aug", "+" }, { "sus2", "sus2" }, { "sus4", "sus4" },
            { "maj6", "6" }, { "min6", "6" }, { "maj7", "maj7" }, { "min7", "7" }, { "dom7", "7" },
            { "min7b5", "\xC3\xB8" "7" }, { "dim7", "\xC2\xB0" "7" }, { "minMaj7", "maj7" }, { "aug7", "+7" },
            { "augMaj7", "+maj7" }, { "add9", "add9" }, { "madd9", "add9" }, { "maj9", "maj9" }, { "min9", "9" },
            { "dom9", "9" }, { "dom7b9", "7b9" }, { "dom7#9", "7#9" }, { "dom7#11", "7#11" }, { "dom13", "13" },
            { "maj7#11", "maj7#11" }, { "min11", "11" }, { "maj13", "maj13" } };
        for (const auto& e : table) if (shape == e.first) return e.second;
        return shape;
    }
}

std::string chordName (const ChordInfo& c)
{
    return std::string (noteName (c.rootMidi)) + chordShapeAt (c.shape).symbol;
}

ChordLabel labelChord (const ChordInfo& c, int rootPc, int scaleIndex)
{
    const auto& sc = scaleAt (scaleIndex);
    const bool minor = minorish (sc);

    ChordLabel l;
    l.name     = chordName (c);
    l.offset   = pcOf (c.rootMidi - rootPc);
    l.diatonic = inScale (sc, rootPc, c.rootMidi);
    l.majorThird = hasInterval (c, 4);

    static const char* majorNames[12] = { "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII" };
    static const char* minorNames[12] = { "I", "bII", "II", "III", "#III", "IV", "#IV", "V", "VI", "#VI", "VII", "#VII" };
    std::string numeral = (minor ? minorNames : majorNames)[l.offset];
    if (minorThird (c))
        for (auto& ch : numeral) ch = (char) std::tolower ((unsigned char) ch);
    l.roman = numeral + suffixFor (chordShapeAt (c.shape).name);

    // functional grouping on the usual major / natural-minor reading
    switch (l.offset)
    {
        case 0:  l.function = HarmonicFunction::Tonic; break;
        case 2:  l.function = HarmonicFunction::Predominant; break;
        case 5:  l.function = HarmonicFunction::Predominant; break;
        case 7:  l.function = HarmonicFunction::Dominant; break;
        case 11: l.function = HarmonicFunction::Dominant; break;
        case 3:  l.function = minor ? HarmonicFunction::Tonic : HarmonicFunction::Other; break;
        case 4:  l.function = HarmonicFunction::Tonic; break;
        case 8:  l.function = minor ? HarmonicFunction::Tonic : HarmonicFunction::Other; break;
        case 9:  l.function = HarmonicFunction::Tonic; break;
        default: l.function = HarmonicFunction::Other; break;
    }
    return l;
}

std::vector<ChordLabel> labelProgression (const std::vector<ChordInfo>& chords, int rootPc, int scaleIndex)
{
    std::vector<ChordLabel> out;
    out.reserve (chords.size());
    for (const auto& c : chords) out.push_back (labelChord (c, rootPc, scaleIndex));
    return out;
}

namespace
{
    CadenceType classify (const ChordLabel& a, const ChordLabel& b)
    {
        if (a.offset == 7 && b.offset == 0)  return a.majorThird ? CadenceType::PerfectAuthentic : CadenceType::Authentic;
        if (a.offset == 11 && b.offset == 0) return CadenceType::Authentic;
        if (a.offset == 5 && b.offset == 0)  return CadenceType::Plagal;
        if (a.offset == 10 && b.offset == 0) return CadenceType::Backdoor;
        if (a.offset == 7 && (b.offset == 8 || b.offset == 9)) return CadenceType::Deceptive;
        return CadenceType::None;
    }
}

CadenceType detectCadence (const std::vector<ChordLabel>& labels)
{
    if (labels.size() < 2) return CadenceType::None;
    const auto& last = labels.back();
    auto c = classify (last, labels.front());                        // the loop repeats: last -> first
    if (c != CadenceType::None) return c;
    c = classify (labels[labels.size() - 2], last);
    if (c != CadenceType::None) return c;
    return last.offset == 7 ? CadenceType::Half : CadenceType::None;
}

float chordTension (const ChordInfo& c, int rootPc, int scaleIndex)
{
    // Krumhansl & Kessler (1982) probe-tone profiles
    static const std::array<double, 12> kMajor { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const std::array<double, 12> kMinor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    const auto& prof = minorish (scaleAt (scaleIndex)) ? kMinor : kMajor;

    std::array<double, 12> hist {};
    const auto notes = c.notes();
    for (int n : notes) hist[(size_t) pcOf (n - rootPc)] += 1.0;

    double mp = 0, mh = 0;
    for (size_t i = 0; i < 12; ++i) { mp += prof[i]; mh += hist[i]; }
    mp /= 12.0; mh /= 12.0;
    double num = 0, dp = 0, dh = 0;
    for (size_t i = 0; i < 12; ++i)
    {
        num += (prof[i] - mp) * (hist[i] - mh);
        dp  += (prof[i] - mp) * (prof[i] - mp);
        dh  += (hist[i] - mh) * (hist[i] - mh);
    }
    const double corr = (dp > 0 && dh > 0) ? num / std::sqrt (dp * dh) : 0.0;
    const double fromKey = (1.0 - corr) * 0.5;

    int rough = 0, pairs = 0;
    for (size_t i = 0; i < notes.size(); ++i)
        for (size_t j = i + 1; j < notes.size(); ++j)
        {
            const int d = std::abs (notes[j] - notes[i]) % 12;
            rough += (d == 6) ? 2 : ((d == 1 || d == 11) ? 2 : (d == 2 || d == 10 ? 1 : 0));
            ++pairs;
        }
    const double dissonance = pairs > 0 ? std::min (1.0, (double) rough / (double) (pairs * 2) * 2.0) : 0.0;

    const auto l = labelChord (c, rootPc, scaleIndex);
    const double fn = l.function == HarmonicFunction::Dominant ? 0.25 : l.function == HarmonicFunction::Predominant ? 0.10
                    : l.function == HarmonicFunction::Other ? 0.18 : 0.0;
    return (float) std::clamp (0.45 * fromKey + 0.30 * dissonance + fn + (l.diatonic ? 0.0 : 0.10), 0.0, 1.0);
}

std::string describeProgression (const std::vector<ChordInfo>& chords, int rootPc, int scaleIndex)
{
    std::string s = "|";
    for (const auto& c : chords)
    {
        const auto l = labelChord (c, rootPc, scaleIndex);
        s += " " + l.name + " (" + l.roman + ") |";
    }
    const auto labels = labelProgression (chords, rootPc, scaleIndex);
    s += "    loop cadence: " + std::string (cadenceName (detectCadence (labels)));
    return s;
}

} // namespace icc
