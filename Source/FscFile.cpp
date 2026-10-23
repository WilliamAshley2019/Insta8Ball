#include "FscFile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace icc
{
namespace
{
    constexpr size_t kHeader = 22;
    constexpr uint8_t kVersionId = 0xC7, kNotesId = 0xE0, kAutoId = 0xDF;

    uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }
    int rd16 (const uint8_t* p) { return p[0] | p[1] << 8; }
    void wr16 (std::vector<uint8_t>& b, int v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); }
    void wr32 (std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
    void putVarint (std::vector<uint8_t>& b, uint32_t v) { do { uint8_t c = v & 0x7F; v >>= 7; if (v) c |= 0x80; b.push_back (c); } while (v); }

    bool plausibleLen (size_t len) { return len > 0 && (len % 20 == 0 || len % 24 == 0); }
    bool validRecSize (size_t s) { return s == 20 || s == 24; }

    // best guesses I don't know the actual size or if it is dynamic across different versions of fl studio
    size_t detectRecordSize (const uint8_t* d, size_t b, size_t e, int versionMajor)
    {
        const size_t len = e - b;
        const bool f20 = len % 20 == 0, f24 = len % 24 == 0;
        if (f20 != f24) return f20 ? 20 : 24;
        if (! f20) return 0;
        int score20 = 0, score24 = 0;
        for (size_t o = b; o + 20 <= e; o += 20) { const uint8_t* r = d + o; if ((rd16 (r + 4) & 0x4000) && r[12] <= 127 && rd32 (r + 8) > 0 && rd32 (r + 8) < 1000000) ++score20; }
        for (size_t o = b; o + 24 <= e; o += 24) { const uint8_t* r = d + o; if ((rd16 (r + 4) & 0x4000) && r[12] <= 127 && r[13] == 0 && rd32 (r + 8) > 0 && rd32 (r + 8) < 1000000) ++score24; }
        const double p20 = (double) score20 / (double) (len / 20), p24 = (double) score24 / (double) (len / 24);
        if (p20 != p24) return p20 > p24 ? 20 : 24;
        return versionMajor > 0 && versionMajor < 5 ? 20 : 24;
    }

    int parseMajor (const uint8_t* d, size_t b, size_t e)
    {
        int v = 0; bool any = false;
        for (size_t i = b; i < e && d[i] >= '0' && d[i] <= '9'; ++i) { v = v * 10 + (d[i] - '0'); any = true; if (v > 1000) break; }
        return any ? v : 0;
    }

    struct Event { uint8_t id = 0; size_t begin = 0, dataBegin = 0, end = 0; };

    // Parses one event at `pos` (must stay inside [pos, limit)). False on any structural problem.
    bool nextEvent (const uint8_t* d, size_t limit, size_t pos, Event& e)
    {
        if (pos >= limit) return false;
        e.id = d[pos]; e.begin = pos;
        size_t p = pos + 1, len;
        if (e.id < 64) len = 1;
        else if (e.id < 128) len = 2;
        else if (e.id < 192) len = 4;
        else
        {
            uint64_t v = 0; int shift = 0;
            for (;;)
            {
                if (p >= limit || shift > 28) return false;
                const uint8_t c = d[p++];
                v |= (uint64_t) (c & 0x7F) << shift; shift += 7;
                if (! (c & 0x80)) break;
            }
            len = (size_t) v;
        }
        if (len > limit - p) return false;
        e.dataBegin = p; e.end = p + len;
        return true;
    }

    struct Parsed
    {
        bool ok = false;
        int format = 0, channels = 1, ppq = 96;
        size_t eventsBegin = kHeader, eventsEnd = 0;
        size_t recSize = 0;
        int versionMajor = 0;
        size_t notesBegin = 0, notesEnd = 0;           // raw extent of the first notes event (id .. end)
        std::vector<std::pair<size_t, size_t>> noteData;   // data ranges of every notes event
        int automation = 0;
        bool suffixClean = false;
    };

    Parsed parse (const uint8_t* d, size_t n)
    {
        Parsed r;
        if (d == nullptr || n < kHeader || std::memcmp (d, "FLhd", 4) != 0 || rd32 (d + 4) != 6 || std::memcmp (d + 0x0E, "FLdt", 4) != 0) return r;
        r.format = rd16 (d + 8); r.channels = rd16 (d + 10); r.ppq = rd16 (d + 12);
        if (r.ppq < 1) return r;
        const size_t declared = rd32 (d + 0x12);
        r.eventsEnd = std::min (n, kHeader + declared);

        // generic walk; if the preamble does not parse as events, locate the notes event by scanning
        size_t pos = kHeader;
        bool structural = true;
        Event e;
        while (pos < r.eventsEnd)
        {
            if (! nextEvent (d, r.eventsEnd, pos, e)) { structural = false; break; }
            if (e.id == kVersionId && r.versionMajor == 0) r.versionMajor = parseMajor (d, e.dataBegin, e.end);
            if (e.id == kNotesId && plausibleLen (e.end - e.dataBegin)) { if (r.noteData.empty()) r.notesBegin = e.begin; r.notesEnd = e.end; r.noteData.emplace_back (e.dataBegin, e.end); }
            else if (e.id == kAutoId) ++r.automation;
            pos = e.end;
        }
        if (structural && ! r.noteData.empty())
        {
            r.recSize = detectRecordSize (d, r.noteData[0].first, r.noteData[0].second, r.versionMajor);
            r.suffixClean = true; r.ok = validRecSize (r.recSize); return r;
        }

        r.noteData.clear(); r.automation = 0;
        for (size_t p = kHeader; p < r.eventsEnd; ++p)
            if (d[p] == kNotesId && nextEvent (d, r.eventsEnd, p, e) && plausibleLen (e.end - e.dataBegin))
            {
                r.notesBegin = e.begin; r.notesEnd = e.end; r.noteData.emplace_back (e.dataBegin, e.end);
                r.recSize = detectRecordSize (d, e.dataBegin, e.end, r.versionMajor);
                r.ok = validRecSize (r.recSize); r.suffixClean = false;
                break;
            }
        return r;
    }
}

FscScore readFsc (const uint8_t* d, size_t n, size_t maxNotes)
{
    FscScore s;
    const auto p = parse (d, n);
    if (! p.ok) { s.error = "not an FL Studio score (.fsc) or no note data found"; return s; }
    s.recordSize = (int) p.recSize; s.format = p.format; s.channels = p.channels; s.ppq = p.ppq; s.automationEvents = p.automation;
    for (const auto& [b, e] : p.noteData)
        for (size_t o = b; o + p.recSize <= e; o += p.recSize)
        {
            if (s.notes.size() >= maxNotes) { s.error = "too many notes"; return s; }
            const uint8_t* r = d + o;
            FscNote note;
            note.start = (int) std::min<uint64_t> ((uint64_t) rd32 (r) * kPPQ / (uint64_t) p.ppq, 1u << 28);
            note.length = std::max (1, (int) std::min<uint64_t> ((uint64_t) rd32 (r + 8) * kPPQ / (uint64_t) p.ppq, 1u << 28));
            if (p.recSize == 20)
            {
                note.key = std::min<int> (r[12], 127);
                note.velocity = std::clamp (r[17], (uint8_t) 1, (uint8_t) 127);
                note.fine = r[13]; note.pan = r[16]; note.modX = r[18]; note.modY = r[19];
            }
            else
            {
                note.key = std::min (rd16 (r + 12), 127);
                note.velocity = std::clamp (r[0x15], (uint8_t) 1, (uint8_t) 127);
                note.fine = r[0x10]; note.release = r[0x12]; note.pan = r[0x14]; note.modX = r[0x16]; note.modY = r[0x17];
            }
            s.notes.push_back (note);
        }
    std::stable_sort (s.notes.begin(), s.notes.end(), [] (const FscNote& a, const FscNote& b) { return a.start != b.start ? a.start < b.start : a.key < b.key; });
    s.ok = ! s.notes.empty();
    if (! s.ok) s.error = "the score contains no notes";
    return s;
}

std::vector<Note> fscToNotes (const FscScore& s)
{
    std::vector<Note> out;
    for (const auto& n : s.notes) out.push_back ({ n.start, n.length, n.key, n.velocity });
    return out;
}

std::string diagnoseFsc (const uint8_t* d, size_t n)
{
    char b[200];
    std::string out;
    if (d == nullptr || n < kHeader || std::memcmp (d, "FLhd", 4) != 0 || std::memcmp (d + 0x0E, "FLdt", 4) != 0)
    {
        std::snprintf (b, sizeof b, "%zu bytes, no FLhd / FLdt header", n);
        return b;
    }
    std::snprintf (b, sizeof b, "%zu bytes, header len %u, format %d, channels %d, ppq %d, data %u", n, rd32 (d + 4), rd16 (d + 8), rd16 (d + 10), rd16 (d + 12), rd32 (d + 0x12));
    out = b;
    const size_t end = std::min (n, kHeader + (size_t) rd32 (d + 0x12));
    size_t pos = kHeader; Event e; int count = 0;
    while (pos < end && count++ < 24)
    {
        if (! nextEvent (d, end, pos, e)) { std::snprintf (b, sizeof b, "\n  offset %zu: cannot parse an event (id 0x%02X)", pos, d[pos]); out += b; break; }
        std::snprintf (b, sizeof b, "\n  event 0x%02X, %zu data bytes%s", e.id, e.end - e.dataBegin,
                       e.id == kNotesId ? (plausibleLen (e.end - e.dataBegin) ? "" : "  <- not a multiple of 20 or 24") : "");
        out += b;
        pos = e.end;
    }
    return out;
}

bool learnFscTemplate (const uint8_t* d, size_t n, FscTemplate& out)
{
    const auto p = parse (d, n);
    if (! p.ok || p.noteData.empty() || p.recSize == 0 || p.noteData[0].second - p.noteData[0].first < p.recSize) return false;
    FscTemplate t;
    t.learned = true; t.format = p.format; t.channels = p.channels; t.ppq = p.ppq;
    t.prefix.assign (d + kHeader, d + p.notesBegin);
    t.record.assign (d + p.noteData[0].first, d + p.noteData[0].first + p.recSize);
    t.record[4] = (uint8_t) (t.record[4] & ~0x08);     // never inherit the slide-note flag
    if (p.suffixClean)
    {
        Event e; size_t pos = p.notesEnd;
        while (pos < p.eventsEnd && nextEvent (d, p.eventsEnd, pos, e))
        {
            if (e.id != kAutoId && e.id != kNotesId) t.suffix.insert (t.suffix.end(), d + e.begin, d + e.end);
            pos = e.end;
        }
    }
    out = std::move (t);
    return true;
}

FscTemplate defaultFscTemplate()
{
    FscTemplate t;
    t.format = 16; t.channels = 1; t.ppq = 96;
    const char* ver = "11.5.0";
    t.prefix.push_back (kVersionId); t.prefix.push_back ((uint8_t) (std::strlen (ver) + 1));
    t.prefix.insert (t.prefix.end(), ver, ver + std::strlen (ver) + 1);
    t.prefix.insert (t.prefix.end(), { 0x1C, 0x03, 0x41, 0x00, 0x00 });
    t.record.assign (24, 0);
    t.record[5] = 0x40;                                
    t.record[0x10] = 0x78; t.record[0x12] = 0x40; t.record[0x14] = 0x40; t.record[0x15] = 0x64; t.record[0x16] = 0x80; t.record[0x17] = 0x80;
    return t;
}

std::vector<uint8_t> writeFsc (const std::vector<Note>& in, const FscTemplate& t)
{
    std::vector<Note> notes = in;
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    const FscTemplate& tp = (validRecSize (t.record.size()) && t.ppq >= 1) ? t : defaultFscTemplate();

    std::vector<uint8_t> ev = tp.prefix;
    ev.push_back (kNotesId);
    putVarint (ev, (uint32_t) (notes.size() * tp.record.size()));
    for (const auto& n : notes)
    {
        auto r = tp.record;
        const uint64_t s = (uint64_t) std::max (0, n.start) * (uint64_t) tp.ppq / kPPQ;
        const uint64_t l = std::max<uint64_t> (1, (uint64_t) std::max (1, n.length) * (uint64_t) tp.ppq / kPPQ);
        for (int i = 0; i < 4; ++i) { r[(size_t) i] = (uint8_t) (s >> (8 * i)); r[8 + (size_t) i] = (uint8_t) (l >> (8 * i)); }
        const int key = std::clamp (n.pitch, 0, 127);
        r[12] = (uint8_t) key;
        if (r.size() == 20) r[17] = (uint8_t) std::clamp (n.velocity, 1, 127);
        else { r[13] = 0; r[0x15] = (uint8_t) std::clamp (n.velocity, 1, 127); }
        ev.insert (ev.end(), r.begin(), r.end());
    }
    ev.insert (ev.end(), tp.suffix.begin(), tp.suffix.end());

    std::vector<uint8_t> f { 'F', 'L', 'h', 'd' };
    wr32 (f, 6); wr16 (f, tp.format); wr16 (f, tp.channels); wr16 (f, tp.ppq);
    f.insert (f.end(), { 'F', 'L', 'd', 't' });
    wr32 (f, (uint32_t) ev.size());
    f.insert (f.end(), ev.begin(), ev.end());
    return f;
}

std::vector<uint8_t> templateToBytes (const FscTemplate& t)
{
    std::vector<uint8_t> b { 'I', 'C', 'F', '1' };
    wr16 (b, t.format); wr16 (b, t.channels); wr16 (b, t.ppq);
    wr32 (b, (uint32_t) t.prefix.size()); b.insert (b.end(), t.prefix.begin(), t.prefix.end());
    wr32 (b, (uint32_t) t.suffix.size()); b.insert (b.end(), t.suffix.begin(), t.suffix.end());
    wr16 (b, (int) t.record.size());
    b.insert (b.end(), t.record.begin(), t.record.end());
    return b;
}

bool templateFromBytes (const uint8_t* d, size_t n, FscTemplate& out)
{
    if (d == nullptr || n < 10 + 4 + 4 + 2 + 20 || std::memcmp (d, "ICF1", 4) != 0) return false;
    FscTemplate t; t.learned = true;
    t.format = rd16 (d + 4); t.channels = rd16 (d + 6); t.ppq = rd16 (d + 8);
    if (t.ppq < 1) return false;
    size_t o = 10;
    const uint32_t pl = rd32 (d + o); o += 4;
    if (pl > n - o || pl > (1u << 20)) return false;
    t.prefix.assign (d + o, d + o + pl); o += pl;
    if (o + 4 > n) return false;
    const uint32_t sl = rd32 (d + o); o += 4;
    if (sl > n - o || sl > (1u << 20) || n - o - sl < 2) return false;
    t.suffix.assign (d + o, d + o + sl); o += sl;
    const size_t rs = (size_t) rd16 (d + o); o += 2;
    if (! validRecSize (rs) || n - o != rs) return false;
    t.record.assign (d + o, d + n);
    out = std::move (t);
    return true;
}

} // namespace icc
