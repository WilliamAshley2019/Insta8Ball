#include "InstrumentStats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace icc
{

void InstrumentTable::beginFile (SystemMode m)
{
    seenInFile.clear();
    ++files;
    ++modes[(size_t) m];
}

void InstrumentTable::addTrack (const ImportedTrack& t, const InstrumentId& id, int tsNum, int tsDen)
{
    if (t.notes.empty()) return;
    auto& a = accs[id.key()];
    a.family = id.family; a.program = id.program;
    ++a.tracks;
    if (id.nameOverrodeZero) ++a.nameOverrides;
    if (std::find (seenInFile.begin(), seenInFile.end(), id.key()) == seenInFile.end()) { seenInFile.push_back (id.key()); ++a.files; }

    const int barTicks = std::max (1, kPPQ * 4 * std::max (1, tsNum) / std::max (1, tsDen));
    int end = 0;
    for (const auto& n : t.notes) end = std::max (end, n.start + n.length);
    a.bars += std::max (1.0, std::ceil ((double) end / barTicks));
    a.confSum += id.confidence;

    long together = 0;
    for (size_t i = 0; i < t.notes.size(); ++i)
    {
        const auto& n = t.notes[i];
        a.notes += 1; a.velSum += n.velocity; a.velSq += (double) n.velocity * n.velocity;
        a.lenSum += (double) n.length / kPPQ;
        a.pitch[(size_t) std::clamp (n.pitch, 0, 127)] += 1;
        for (size_t j = i + 1; j < t.notes.size() && t.notes[j].start - n.start <= 60; ++j) ++together;
    }
    a.polySum += 1.0 + 2.0 * (double) together / (double) t.notes.size();      // per track, weighted below by track count
}

void InstrumentTable::endFile() { seenInFile.clear(); }

std::vector<InstrumentRow> InstrumentTable::rows() const
{
    std::vector<InstrumentRow> out;
    for (const auto& [key, a] : accs)
    {
        if (a.notes <= 0) continue;
        InstrumentRow r;
        r.key = key; r.family = a.family; r.program = a.program;
        InstrumentId id; id.family = a.family; id.program = a.program;
        r.label = id.label();
        r.files = a.files; r.tracks = a.tracks; r.notes = a.notes;
        r.notesPerBar = (float) (a.notes / std::max (1.0, a.bars));
        const double m = a.velSum / a.notes;
        r.velMean = (float) m; r.velStd = (float) std::sqrt (std::max (0.0, a.velSq / a.notes - m * m));
        r.lengthBeats = (float) (a.lenSum / a.notes);
        r.polyphony = (float) (a.polySum / std::max (1, a.tracks));
        r.confidence = (float) (a.confSum / std::max (1, a.tracks));
        r.nameOverrides = a.nameOverrides;
        double cum = 0, mean = 0; bool got05 = false, got95 = false;
        for (int p = 0; p < 128; ++p)
        {
            mean += p * a.pitch[(size_t) p]; cum += a.pitch[(size_t) p];
            if (! got05 && cum >= 0.05 * a.notes) { r.pitchP05 = (float) p; got05 = true; }
            if (! got95 && cum >= 0.95 * a.notes) { r.pitchP95 = (float) p; got95 = true; }
        }
        r.pitchMean = (float) (mean / a.notes);
        out.push_back (std::move (r));
    }
    std::sort (out.begin(), out.end(), [] (const InstrumentRow& x, const InstrumentRow& y) { return x.notes > y.notes; });
    return out;
}

static std::string csvEscape (const std::string& s)
{
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}

std::string InstrumentTable::toCsv() const
{
    std::string s = "key,instrument,family,program,files,tracks,notes,notes_per_bar,pitch_mean,pitch_p05,pitch_p95,velocity_mean,velocity_std,length_beats,polyphony,confidence,name_overrides\n";
    char line[512];
    for (const auto& r : rows())
    {
        std::snprintf (line, sizeof line, ",%d,%d,%d,%d,%.0f,%.3f,%.2f,%.0f,%.0f,%.1f,%.1f,%.3f,%.2f,%.2f,%d\n", r.family, r.program, r.files, r.tracks, r.notes,
                       r.notesPerBar, r.pitchMean, r.pitchP05, r.pitchP95, r.velMean, r.velStd, r.lengthBeats, r.polyphony, r.confidence, r.nameOverrides);
        s += r.key + "," + csvEscape (r.label) + line;
    }
    return s;
}

std::string InstrumentTable::describe (int maxRows) const
{
    const auto all = rows();
    double total = 0;
    for (const auto& r : all) total += r.notes;
    char line[400];
    std::string s;
    std::snprintf (line, sizeof line, "%d files   sound sets: GM %d, GM2 %d, GS %d, XG %d, none declared %d\n\n", files, modes[(size_t) SystemMode::GM1],
                   modes[(size_t) SystemMode::GM2], modes[(size_t) SystemMode::GS], modes[(size_t) SystemMode::XG], modes[(size_t) SystemMode::Unknown]);
    s += line;
    s += "share of notes                      files  notes/bar  pitch (p05-p95)  vel  len  poly  conf\n";
    int shown = 0;
    for (const auto& r : all)
    {
        if (shown++ >= maxRows) break;
        const double share = total > 0 ? r.notes / total : 0.0;
        std::string bar (std::min<size_t> (20, (size_t) std::lround (share * 60.0)), '#');
        bar.resize (20, ' ');
        const auto lo = std::string (noteName ((int) r.pitchP05 % 12)) + std::to_string ((int) r.pitchP05 / 12 - 1);
        const auto hi = std::string (noteName ((int) r.pitchP95 % 12)) + std::to_string ((int) r.pitchP95 / 12 - 1);
        std::snprintf (line, sizeof line, "%s %4.1f%%  %-34.34s %5d  %8.2f  %4s - %-4s  %4.0f  %3.1f  %4.1f  %3.0f%%\n", bar.c_str(), share * 100.0, r.label.c_str(),
                       r.files, r.notesPerBar, lo.c_str(), hi.c_str(), r.velMean, r.lengthBeats, r.polyphony, r.confidence * 100.0f);
        s += line;
    }
    return s;
}

namespace
{
    void putU32 (std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
    void putF64 (std::vector<uint8_t>& b, double v) { uint64_t u; std::memcpy (&u, &v, 8); for (int i = 0; i < 8; ++i) b.push_back ((uint8_t) (u >> (8 * i))); }
    struct Rd
    {
        const uint8_t* d; size_t n, i = 0; bool bad = false;
        uint32_t u32() { if (i + 4 > n) { bad = true; return 0; } uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= (uint32_t) d[i + (size_t) k] << (8 * k); i += 4; return v; }
        double f64() { if (i + 8 > n) { bad = true; return 0; } uint64_t u = 0; for (int k = 0; k < 8; ++k) u |= (uint64_t) d[i + (size_t) k] << (8 * k); i += 8; double v; std::memcpy (&v, &u, 8); if (! std::isfinite (v) || v < 0 || v > 1e15) { bad = true; return 0; } return v; }
    };
}

std::vector<uint8_t> InstrumentTable::toBytes() const
{
    std::vector<uint8_t> b { 'I', 'C', 'I', '1' };
    putU32 (b, (uint32_t) files);
    for (int m : modes) putU32 (b, (uint32_t) m);
    putU32 (b, (uint32_t) accs.size());
    for (const auto& [key, a] : accs)
    {
        putU32 (b, (uint32_t) key.size());
        b.insert (b.end(), key.begin(), key.end());
        putU32 (b, (uint32_t) (a.family + 1)); putU32 (b, (uint32_t) (a.program + 1));
        putU32 (b, (uint32_t) a.files); putU32 (b, (uint32_t) a.tracks); putU32 (b, (uint32_t) a.nameOverrides);
        for (double v : { a.notes, a.bars, a.velSum, a.velSq, a.lenSum, a.polySum, a.confSum }) putF64 (b, v);
        for (double v : a.pitch) putF64 (b, v);
    }
    return b;
}

bool InstrumentTable::fromBytes (const uint8_t* d, size_t n)
{
    if (d == nullptr || n < 8 || std::memcmp (d, "ICI1", 4) != 0) return false;
    Rd r { d, n, 4 };
    InstrumentTable t;
    t.files = (int) std::min<uint32_t> (r.u32(), 2000000000u);
    for (int& m : t.modes) m = (int) std::min<uint32_t> (r.u32(), 2000000000u);
    const uint32_t count = r.u32();
    if (count > 100000) return false;
    for (uint32_t k = 0; k < count && ! r.bad; ++k)
    {
        const uint32_t kl = r.u32();
        if (kl == 0 || kl > 16 || r.i + kl > n) return false;
        std::string key (reinterpret_cast<const char*> (d + r.i), kl); r.i += kl;
        Acc a;
        a.family = (int) std::min<uint32_t> (r.u32(), 100u) - 1; a.program = (int) std::min<uint32_t> (r.u32(), 200u) - 1;
        a.files = (int) std::min<uint32_t> (r.u32(), 2000000000u); a.tracks = (int) std::min<uint32_t> (r.u32(), 2000000000u); a.nameOverrides = (int) std::min<uint32_t> (r.u32(), 2000000000u);
        a.notes = r.f64(); a.bars = r.f64(); a.velSum = r.f64(); a.velSq = r.f64(); a.lenSum = r.f64(); a.polySum = r.f64(); a.confSum = r.f64();
        for (double& v : a.pitch) v = r.f64();
        t.accs[key] = a;
    }
    if (r.bad || r.i != n) return false;
    *this = std::move (t);
    return true;
}

} // namespace icc
