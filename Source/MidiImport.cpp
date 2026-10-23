#include "MidiImport.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <map>

namespace icc
{

int ImportedMidi::totalNotes() const noexcept
{
    int n = 0;
    for (const auto& t : tracks) n += (int) t.notes.size();
    return n;
}

int ImportedMidi::barsNeeded() const noexcept
{
    return std::clamp ((lengthTicks + kBarTicks - 1) / kBarTicks, 1, kMaxBars);
}

namespace
{
    struct Reader
    {
        const uint8_t* p; size_t n; size_t i = 0;
        bool has (size_t k) const { return i + k <= n; }
        uint32_t be32() { const uint32_t v = ((uint32_t) p[i] << 24) | ((uint32_t) p[i + 1] << 16) | ((uint32_t) p[i + 2] << 8) | p[i + 3]; i += 4; return v; }
        uint32_t be16() { const uint32_t v = ((uint32_t) p[i] << 8) | p[i + 1]; i += 2; return v; }
    };

    bool readVlq (const uint8_t* d, size_t end, size_t& i, uint32_t& out)
    {
        out = 0;
        for (int k = 0; k < 4; ++k)
        {
            if (i >= end) return false;
            const uint8_t b = d[i++];
            out = (out << 7) | (b & 0x7F);
            if ((b & 0x80) == 0) return true;
        }
        return false;
    }

    struct Pending { int64_t start; int vel; };

    struct ProgState { int program = -1, msb = -1, lsb = -1; bool multi = false; };

    struct TrackParse
    {
        std::string name, instrument;
        std::array<ProgState, 17> prog;                   // by channel 1..16
        std::map<int, std::vector<Note>> byChannel;   // channel 1..16 -> notes (raw ticks, scaled later)
        int64_t endTick = 0;
    };
}

const char* systemModeName (SystemMode m) noexcept
{
    switch (m) { case SystemMode::GM1: return "GM"; case SystemMode::GM2: return "GM2"; case SystemMode::GS: return "GS"; case SystemMode::XG: return "XG"; default: return "unknown"; }
}

namespace
{
    // payload of an F0 message (after the length): the reset messages that declare the sound set
    SystemMode detectSystemMode (const uint8_t* d, size_t n)
    {
        if (n >= 4 && d[0] == 0x7E && d[2] == 0x09)
        {
            if (d[3] == 0x01) return SystemMode::GM1;
            if (d[3] == 0x03) return SystemMode::GM2;
        }
        if (n >= 9 && d[0] == 0x41 && d[2] == 0x42 && d[3] == 0x12 && d[4] == 0x40 && d[5] == 0x00 && d[6] == 0x7F) return SystemMode::GS;
        if (n >= 6 && d[0] == 0x43 && (d[1] & 0xF0) == 0x10 && d[2] == 0x4C && d[3] == 0x00 && d[4] == 0x00 && d[5] == 0x7E) return SystemMode::XG;
        return SystemMode::Unknown;
    }
}

ImportedMidi importMidi (const uint8_t* data, size_t size, size_t maxNotes)
{
    ImportedMidi out;
    if (data == nullptr || size < 14) { out.error = "File is too small to be MIDI"; return out; }
    Reader r { data, size };
    if (std::string (reinterpret_cast<const char*> (data), 4) != "MThd") { out.error = "Not a standard MIDI file"; return out; }
    r.i = 4;
    const uint32_t hdrLen = r.be32();
    if (hdrLen < 6 || ! r.has (hdrLen)) { out.error = "Bad MIDI header"; return out; }
    out.format = (int) r.be16();
    const int declaredTracks = (int) r.be16();
    const uint32_t division = r.be16();
    r.i = 8 + hdrLen;
    if (out.format > 2) { out.error = "Unknown MIDI file format"; return out; }

    double ticksPerQuarter = 480.0;
    if (division & 0x8000)
    {
        const int fps = 256 - (int) ((division >> 8) & 0xFF);   // two's complement negative frames-per-second
        const int sub = (int) (division & 0xFF);
        if (fps <= 0 || sub <= 0) { out.error = "Bad SMPTE division"; return out; }
        ticksPerQuarter = 0.5 * (double) fps * (double) sub;     // at 120 BPM a quarter note lasts half a second
    }
    else
    {
        if (division == 0) { out.error = "Zero ticks per quarter"; return out; }
        ticksPerQuarter = (double) division;
    }
    out.sourcePPQ = (int) std::lround (ticksPerQuarter);
    const double scale = (double) kPPQ / ticksPerQuarter;

    bool gotTempo = false, gotSig = false;
    std::array<ProgState, 17> fileProg;                    // first program seen anywhere in the file, by channel
    size_t noteBudget = maxNotes;
    std::vector<TrackParse> tracks;

    while (r.has (8) && (int) tracks.size() < std::max (declaredTracks, 1) + 64)
    {
        const std::string id (reinterpret_cast<const char*> (data + r.i), 4);
        r.i += 4;
        uint32_t len = r.be32();
        if (len > size - r.i) len = (uint32_t) (size - r.i);            // truncated file: parse what is there
        const size_t start = r.i, end = r.i + len;
        r.i = end;
        if (id != "MTrk") continue;

        TrackParse tp;
        size_t i = start;
        int64_t tick = 0;
        int running = 0;
        std::map<int, Pending> pending;                                  // key = channel * 128 + pitch
        bool broken = false;

        auto closeNote = [&] (int key, int64_t at)
        {
            auto it = pending.find (key);
            if (it == pending.end()) return false;
            Note n;
            n.start    = (int) std::min<int64_t> (INT32_MAX / 2, (int64_t) std::llround ((double) it->second.start * scale));
            const int64_t e = (int64_t) std::llround ((double) at * scale);
            n.length   = (int) std::clamp<int64_t> (e - n.start, 30, INT32_MAX / 4);
            n.pitch    = key % 128;
            n.velocity = it->second.vel;
            tp.byChannel[key / 128].push_back (n);
            pending.erase (it);
            return true;
        };

        while (i < end && ! broken)
        {
            uint32_t delta = 0;
            if (! readVlq (data, end, i, delta)) break;
            tick += delta;
            if (i >= end) break;
            int status = data[i];
            if (status < 0x80)                                          // running status
            {
                if (running == 0) { broken = true; break; }
                status = running;
            }
            else
            {
                ++i;
                if (status < 0xF0) running = status;
            }

            if (status == 0xFF)
            {
                if (i >= end) break;
                const int type = data[i++];
                uint32_t mlen = 0;
                if (! readVlq (data, end, i, mlen) || mlen > end - i) break;
                const uint8_t* md = data + i;
                if (type == 0x2F) { tp.endTick = std::max (tp.endTick, tick); i += mlen; break; }
                if (type == 0x03 && tp.name.empty()) tp.name.assign (reinterpret_cast<const char*> (md), mlen);
                if (type == 0x04 && tp.instrument.empty()) tp.instrument.assign (reinterpret_cast<const char*> (md), mlen);
                if (type == 0x51 && mlen == 3)
                {
                    const uint32_t us = ((uint32_t) md[0] << 16) | ((uint32_t) md[1] << 8) | md[2];
                    if (us > 0)
                    {
                        if (! gotTempo) { out.bpm = 60000000.0 / (double) us; gotTempo = true; }
                        else if (std::abs (60000000.0 / (double) us - out.bpm) > 0.5) out.multiTempo = true;
                    }
                }
                if (type == 0x58 && mlen >= 2 && ! gotSig)
                {
                    out.tsNum = std::max (1, (int) md[0]);
                    out.tsDen = 1 << std::min (6, (int) md[1]);
                    gotSig = true;
                }
                i += mlen;
            }
            else if (status == 0xF0 || status == 0xF7)
            {
                uint32_t slen = 0;
                if (! readVlq (data, end, i, slen) || slen > end - i) break;
                if (status == 0xF0 && out.systemMode == SystemMode::Unknown) out.systemMode = detectSystemMode (data + i, slen);
                i += slen;
                running = 0;
            }
            else if (status >= 0xF1)                                    // system common / realtime inside a file: no data we know
            {
                if (status == 0xF2) i += 2; else if (status == 0xF1 || status == 0xF3) i += 1;
            }
            else
            {
                const int kind = status & 0xF0, ch = (status & 0x0F) + 1;
                const int need = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
                if (i + (size_t) need > end) break;
                const int d1 = data[i], d2 = need == 2 ? data[i + 1] : 0;
                i += (size_t) need;
                if (d1 > 127 || d2 > 127) { broken = true; break; }
                const int key = ch * 128 + d1;
                if (kind == 0xB0 && (d1 == 0 || d1 == 32))
                {
                    auto& ps = tp.prog[(size_t) ch];
                    if (ps.program < 0) (d1 == 0 ? ps.msb : ps.lsb) = d2;        // bank select before the first program change
                    auto& fp = fileProg[(size_t) ch];
                    if (fp.program < 0) (d1 == 0 ? fp.msb : fp.lsb) = d2;
                }
                else if (kind == 0xC0)
                {
                    auto& ps = tp.prog[(size_t) ch];
                    if (ps.program < 0) ps.program = d1; else if (ps.program != d1) ps.multi = true;
                    auto& fp = fileProg[(size_t) ch];
                    if (fp.program < 0) fp.program = d1; else if (fp.program != d1) fp.multi = true;
                }
                if (kind == 0x90 && d2 > 0)
                {
                    if (noteBudget == 0) { out.error = "Too many notes"; return out; }
                    if (pending.count (key)) { closeNote (key, tick); ++out.retriggers; }
                    pending[key] = { tick, d2 };
                    --noteBudget;
                }
                else if (kind == 0x80 || kind == 0x90)
                {
                    if (! closeNote (key, tick)) ++out.orphanOffs;
                }
            }
        }

        const int64_t finish = std::max (tick, tp.endTick);
        for (auto it = pending.begin(); it != pending.end();)
        {
            const int key = it->first;
            ++it;
            closeNote (key, finish);
            ++out.hangingNotes;
        }
        tracks.push_back (std::move (tp));
    }

    if (tracks.empty()) { out.error = "No tracks found"; return out; }

    for (auto& tp : tracks)
        for (auto& [ch, notes] : tp.byChannel)
        {
            if (notes.empty()) continue;
            std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                              { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
            ImportedTrack t;
            t.name = tp.name; t.instrumentName = tp.instrument; t.channel = ch; t.notes = std::move (notes);
            const auto& own = tp.prog[(size_t) ch];
            const auto& fp = fileProg[(size_t) ch];
            const auto& use = own.program >= 0 ? own : fp;
            t.program = use.program; t.bankMsb = use.msb >= 0 ? use.msb : fp.msb; t.bankLsb = use.lsb >= 0 ? use.lsb : fp.lsb;
            t.multiProgram = own.multi || fp.multi;
            for (const auto& n : t.notes) out.lengthTicks = std::max (out.lengthTicks, n.start + n.length);
            out.tracks.push_back (std::move (t));
        }

    out.ok = true;
    return out;
}

namespace
{
    struct Stat { int idx; double meanPitch = 60, poly = 1, density = 0; int count = 0; };

    Stat statOf (const ImportedTrack& t, int idx)
    {
        Stat s; s.idx = idx; s.count = (int) t.notes.size();
        if (t.notes.empty()) return s;
        double sum = 0; long overlaps = 0;
        for (size_t i = 0; i < t.notes.size(); ++i)
        {
            sum += t.notes[i].pitch;
            // how many notes start within 60 ticks of this one (a chord counts its members)
            for (size_t j = i + 1; j < t.notes.size() && t.notes[j].start - t.notes[i].start <= 60; ++j) ++overlaps;
        }
        s.meanPitch = sum / (double) s.count;
        s.poly = 1.0 + 2.0 * (double) overlaps / (double) s.count;
        s.density = (double) s.count;
        return s;
    }
}

ImportMapping importToPattern (const ImportedMidi& m, int startBar, int maxBarsIn)
{
    ImportMapping out;
    const int maxBars = std::clamp (maxBarsIn, 1, kMaxBars);
    out.oddMeter = m.ok && (m.tsNum != 4 || m.tsDen != 4);
    if (! m.ok) return out;

    const int shift = std::max (0, startBar) * kBarTicks;
    std::vector<Stat> melodic;
    for (size_t i = 0; i < m.tracks.size(); ++i)
    {
        const auto& t = m.tracks[i];
        if (t.channel == 10)
        {
            if (out.source[Rhythm] < 0) out.source[Rhythm] = (int) i;
            for (auto n : t.notes) { n.start -= shift; if (n.start >= 0) { out.pattern[Rhythm].push_back (n); } }
        }
        else melodic.push_back (statOf (t, (int) i));
    }

    auto take = [&] (int slot, const Stat& s)
    {
        out.source[(size_t) slot] = s.idx;
        for (auto n : m.tracks[(size_t) s.idx].notes)
        {
            n.start -= shift;
            if (n.start >= 0) out.pattern[(size_t) slot].push_back (n);
        }
        std::stable_sort (out.pattern[(size_t) slot].begin(), out.pattern[(size_t) slot].end(),
                          [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    };

    std::vector<bool> used (melodic.size(), false);
    auto pick = [&] (auto&& better, auto&& eligible) -> int
    {
        int best = -1;
        for (size_t k = 0; k < melodic.size(); ++k)
            if (! used[k] && eligible (melodic[k]) && (best < 0 || better (melodic[k], melodic[(size_t) best]))) best = (int) k;
        return best;
    };
    auto assign = [&] (int slot, int k) { if (k >= 0) { used[(size_t) k] = true; take (slot, melodic[(size_t) k]); } };

    assign (Bass,   pick ([] (const Stat& a, const Stat& b) { return a.meanPitch < b.meanPitch; }, [] (const Stat& s) { return s.meanPitch < 55 && s.poly < 2.0; }));
    assign (Chord,  pick ([] (const Stat& a, const Stat& b) { return a.poly > b.poly; },           [] (const Stat& s) { return s.poly >= 1.8; }));
    assign (Melody, pick ([] (const Stat& a, const Stat& b) { return a.count > b.count; },         [] (const Stat& s) { return s.poly < 1.8; }));
    assign (Pad,    pick ([] (const Stat& a, const Stat& b) { return a.poly > b.poly; },           [] (const Stat&) { return true; }));
    assign (Arp,    pick ([] (const Stat& a, const Stat& b) { return a.count > b.count; },         [] (const Stat&) { return true; }));
    assign (Foliage, pick ([] (const Stat& a, const Stat& b) { return a.count > b.count; },        [] (const Stat&) { return true; }));
    for (int slot : { (int) Melody, (int) Chord, (int) Pad, (int) Arp, (int) Foliage, (int) Bass })   // anything left fills an empty slot
        if (out.pattern[(size_t) slot].empty())
            assign (slot, pick ([] (const Stat& a, const Stat& b) { return a.count > b.count; }, [] (const Stat&) { return true; }));
    for (size_t k = 0; k < melodic.size(); ++k) if (! used[k]) ++out.droppedTracks;

    int endTick = 0;
    for (const auto& t : out.pattern) for (const auto& n : t) endTick = std::max (endTick, n.start + 1);
    out.bars = std::clamp ((endTick + kBarTicks - 1) / kBarTicks, 1, maxBars);
    clampPattern (out.pattern, out.bars);
    return out;
}

} // namespace icc
