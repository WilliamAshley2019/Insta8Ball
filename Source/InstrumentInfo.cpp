#include "InstrumentInfo.h"

#include <algorithm>
#include <cctype>

namespace icc
{

namespace
{
    struct NameRule { const char* word; int program; int family; };   // program >= 0: specific; else family only

    // Longest / most specific words first within a family. Matching is on whole words of the normalised name,
    // or on a prefix of a word for the stems marked with '*' (e.g. "guit*" matches "guitar" and "guitars").
    const NameRule kRules[] = {
        { "drum*", -1, 16 }, { "kit", -1, 16 }, { "kick", -1, 16 }, { "snare", -1, 16 }, { "hihat", -1, 16 }, { "hat", -1, 16 }, { "hats", -1, 16 }, { "tom", -1, 16 }, { "toms", -1, 16 },
        { "cymbal*", -1, 16 }, { "perc*", -1, 16 }, { "conga*", -1, 16 }, { "bongo*", -1, 16 }, { "shaker", -1, 16 }, { "tamb*", -1, 16 }, { "clap*", -1, 16 },
        { "rhodes", 4, 0 }, { "wurli*", 5, 0 }, { "epiano", 4, 0 }, { "electric piano", 4, 0 }, { "ep", 4, 0 }, { "honky*", 3, 0 }, { "grand", 0, 0 },
        { "harpsichord", 6, 0 }, { "harpsi*", 6, 0 }, { "clav*", 7, 0 }, { "piano*", -1, 0 }, { "keys", -1, 0 },
        { "celesta", 8, 1 }, { "glock*", 9, 1 }, { "music box", 10, 1 }, { "vibes", 11, 1 }, { "vibraphone", 11, 1 }, { "marimba", 12, 1 }, { "xylo*", 13, 1 },
        { "tubular", 14, 1 }, { "bell*", -1, 1 }, { "mallet*", -1, 1 },
        { "hammond", 16, 2 }, { "b3", 16, 2 }, { "church organ", 19, 2 }, { "pipe organ", 19, 2 }, { "accordion", 21, 2 }, { "harmonica", 22, 2 }, { "organ*", -1, 2 },
        { "nylon", 24, 3 }, { "steel", 25, 3 }, { "jazz guitar", 26, 3 }, { "clean", 27, 3 }, { "muted", 28, 3 }, { "overdrive*", 29, 3 }, { "distort*", 30, 3 },
        { "guit*", -1, 3 }, { "gtr", -1, 3 }, { "acoustic", -1, 3 },
        { "fretless", 35, 4 }, { "slap", 36, 4 }, { "fingered", 33, 4 }, { "picked", 34, 4 }, { "acoustic bass", 32, 4 }, { "upright", 32, 4 }, { "synth bass", 38, 4 },
        { "sub", 38, 4 }, { "bass*", -1, 4 },
        { "violin", 40, 5 }, { "viola", 41, 5 }, { "cello", 42, 5 }, { "contrabass", 43, 5 }, { "pizzicato", 45, 5 }, { "tremolo", 44, 5 }, { "harp", 46, 5 },
        { "timpani", 47, 5 },
        { "string*", -1, 6 }, { "choir", 52, 6 }, { "aah*", 52, 6 }, { "ooh*", 53, 6 }, { "vox", 54, 6 }, { "voice*", -1, 6 }, { "vocal*", -1, 6 }, { "orch*", 55, 6 },
        { "trumpet", 56, 7 }, { "trombone", 57, 7 }, { "tuba", 58, 7 }, { "french horn", 60, 7 }, { "horn*", -1, 7 }, { "brass", 61, 7 },
        { "soprano", 64, 8 }, { "alto", 65, 8 }, { "tenor", 66, 8 }, { "baritone", 67, 8 }, { "sax*", -1, 8 }, { "oboe", 68, 8 }, { "clarinet", 71, 8 }, { "bassoon", 70, 8 },
        { "piccolo", 72, 9 }, { "flute", 73, 9 }, { "recorder", 74, 9 }, { "pan flute", 75, 9 }, { "whistle", 78, 9 }, { "ocarina", 79, 9 }, { "woodwind*", -1, 8 },
        { "saw", 81, 10 }, { "square", 80, 10 }, { "lead", -1, 10 }, { "leads", -1, 10 }, { "solo", -1, 10 }, { "melody", -1, 10 },
        { "pad", -1, 11 }, { "pads", -1, 11 }, { "warm", 89, 11 }, { "halo", 94, 11 }, { "sweep", 95, 11 }, { "atmos*", 99, 12 }, { "fx", -1, 12 }, { "sfx", -1, 15 }, { "riser", -1, 12 },
        { "sitar", 104, 13 }, { "banjo", 105, 13 }, { "koto", 107, 13 }, { "kalimba", 108, 13 }, { "bagpipe*", 109, 13 }, { "fiddle", 110, 13 },
        { "steel drum*", 114, 14 }, { "taiko", 116, 14 }, { "pluck*", -1, 13 }, { "arp*", -1, 10 }, { "synth*", -1, 10 },
    };

    bool wordMatches (const std::string& word, const std::string& rule)
    {
        if (! rule.empty() && rule.back() == '*') return word.compare (0, rule.size() - 1, rule, 0, rule.size() - 1) == 0;
        return word == rule;
    }

    // phrase rules contain a space; check them against the whole normalised name
    bool ruleHits (const std::vector<std::string>& words, const std::string& norm, const NameRule& r)
    {
        const std::string w = r.word;
        if (w.find (' ') != std::string::npos) return (" " + norm + " ").find (" " + w + " ") != std::string::npos;
        for (const auto& x : words) if (wordMatches (x, w)) return true;
        return false;
    }

    std::vector<std::string> splitWords (const std::string& norm)
    {
        std::vector<std::string> out; std::string cur;
        for (char c : norm) { if (c == ' ') { if (! cur.empty()) out.push_back (cur); cur.clear(); } else cur += c; }
        if (! cur.empty()) out.push_back (cur);
        return out;
    }

    const NameRule* bestRule (const std::string& rawName)
    {
        const auto norm = normaliseInstrumentName (rawName);
        if (norm.empty() || isGenericInstrumentName (rawName)) return nullptr;
        const auto words = splitWords (norm);
        const NameRule* best = nullptr;
        for (const auto& r : kRules)
            if (ruleHits (words, norm, r) && (best == nullptr || (r.program >= 0 && best->program < 0))) { best = &r; if (r.program >= 0) break; }
        return best;
    }
}

std::string normaliseInstrumentName (const std::string& s)
{
    std::string out;
    for (unsigned char c : s) out += std::isalpha (c) ? (char) std::tolower (c) : ' ';
    std::string squeezed; bool space = true;
    for (char c : out) { if (c == ' ') { if (! space) squeezed += ' '; space = true; } else { squeezed += c; space = false; } }
    while (! squeezed.empty() && squeezed.back() == ' ') squeezed.pop_back();
    return squeezed;
}

bool isGenericInstrumentName (const std::string& s)
{
    const auto n = normaliseInstrumentName (s);
    if (n.empty()) return true;
    static const char* generic[] = { "track", "untitled", "midi", "channel", "ch", "instrument", "part", "voice", "unnamed", "new track", "midi track", "default", "main", "mix", "master", "conductor", "tempo", "tempo map" };
    for (auto* g : generic) if (n == g) return true;
    return false;
}

int familyFromName (const std::string& name) { const auto* r = bestRule (name); return r ? r->family : -1; }
int programFromName (const std::string& name) { const auto* r = bestRule (name); return r ? r->program : -1; }

std::string InstrumentId::key() const
{
    char b[16];
    if (family == kDrumFamily) { std::snprintf (b, sizeof b, "D%03d", std::max (program, 0)); return b; }
    if (program >= 0) { std::snprintf (b, sizeof b, "P%03d", program); return b; }
    if (family >= 0) { std::snprintf (b, sizeof b, "F%02d", family); return b; }
    return "U";
}

std::string InstrumentId::label() const
{
    if (family == kDrumFamily) return program >= 0 ? "Drum kit: " + gmDrumKitName (program) : std::string ("Drum kit (unspecified)");
    if (program >= 0) return gmProgramName (program);
    if (family >= 0) return std::string (gmFamilyName (family * 8)) + " (family only)";
    return "Unassigned (no program change, generic name)";
}

InstrumentId classifyTrack (const ImportedTrack& t, SystemMode mode)
{
    InstrumentId id;
    id.bank = t.bankMsb;

    const bool bankDrums = (mode == SystemMode::GM2 && t.bankMsb == 120) || (mode == SystemMode::XG && t.bankMsb == 127);
    if (t.channel == 10 || bankDrums)
    {
        id.source = InstrumentSource::Drums; id.family = kDrumFamily; id.program = t.program >= 0 ? t.program : -1; id.confidence = 0.95f;
        return id;
    }

    const auto* byName = bestRule (t.instrumentName);
    if (byName == nullptr) byName = bestRule (t.name);

    if (t.program >= 0)
    {
        id.source = InstrumentSource::ProgramChange; id.program = t.program; id.family = t.program / 8;
        id.confidence = t.program == 0 ? 0.6f : 0.95f;
        if (t.program == 0 && byName != nullptr && byName->family != 0)      // exporters often write 0 for everything
        {
            id.nameOverrodeZero = true; id.source = InstrumentSource::Name; id.family = byName->family; id.program = byName->program; id.confidence = byName->program >= 0 ? 0.7f : 0.5f;
            if (byName->family == kDrumFamily) id.program = -1;
        }
        return id;
    }
    if (byName != nullptr)
    {
        id.source = InstrumentSource::Name; id.family = byName->family; id.program = byName->program; id.confidence = byName->program >= 0 ? 0.7f : 0.5f;
        return id;
    }
    return id;     // unassigned
}

const char* flexSearchHint (int program, bool drums) noexcept
{
    if (drums) return "drum kit";
    static const char* fam[16] = { "piano", "bells mallet", "organ", "guitar", "bass", "strings", "choir ensemble", "brass", "sax woodwind", "flute", "lead",
                                   "pad", "fx atmosphere", "world pluck", "percussion", "fx" };
    if (program == 4 || program == 5) return "e-piano";
    if (program == 6 || program == 7) return "keys";
    return fam[std::clamp (program, 0, 127) / 8];
}

} // namespace icc
