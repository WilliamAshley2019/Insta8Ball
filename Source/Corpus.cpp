#include "Corpus.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace icc
{

std::string TrainReport::summary() const
{
    char b[320];
    std::snprintf (b, sizeof b, "%d file%s scanned, %d used%s. Skipped: %d without drums, %d not 4/4, %d unreadable, %d too big, %d duplicates.",
                   scanned, scanned == 1 ? "" : "s", used, cancelled ? " (cancelled)" : "", noDrums, oddMeter, unreadable, tooBig, duplicates);
    return b;
}

std::vector<fs::path> findMidiFiles (const fs::path& root, bool recursive, size_t maxFiles)
{
    std::vector<fs::path> out;
    std::error_code ec;
    auto isMidi = [] (const fs::path& p)
    {
        std::string e = p.extension().string();
        std::transform (e.begin(), e.end(), e.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return e == ".mid" || e == ".midi";
    };
    if (! fs::is_directory (root, ec)) return out;
    if (recursive)
    {
        for (fs::recursive_directory_iterator it (root, fs::directory_options::skip_permission_denied, ec), end; it != end && out.size() < maxFiles; it.increment (ec))
        {
            if (ec) { ec.clear(); continue; }
            std::error_code e2;
            if (it->is_regular_file (e2) && isMidi (it->path())) out.push_back (it->path());
        }
    }
    else
    {
        for (fs::directory_iterator it (root, fs::directory_options::skip_permission_denied, ec), end; it != end && out.size() < maxFiles; it.increment (ec))
        {
            if (ec) { ec.clear(); continue; }
            std::error_code e2;
            if (it->is_regular_file (e2) && isMidi (it->path())) out.push_back (it->path());
        }
    }
    std::sort (out.begin(), out.end());
    return out;
}

bool extractDrums (const ImportedMidi& m, std::vector<Note>& out)
{
    out.clear();
    for (const auto& t : m.tracks)
        if (t.channel == 10) out.insert (out.end(), t.notes.begin(), t.notes.end());
    if (out.empty())
        for (const auto& t : m.tracks)
        {
            std::string n = t.name;
            std::transform (n.begin(), n.end(), n.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            if (n.find ("drum") != std::string::npos || n.find ("perc") != std::string::npos || n.find ("kit") != std::string::npos)
                out.insert (out.end(), t.notes.begin(), t.notes.end());
        }
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    return ! out.empty();
}

namespace
{
    uint64_t hashNotes (const std::vector<Note>& v)
    {
        uint64_t h = 1469598103934665603ull;
        for (const auto& n : v)
            for (uint64_t x : { (uint64_t) n.start, (uint64_t) n.pitch, (uint64_t) n.velocity }) { h ^= x; h *= 1099511628211ull; }
        return h;
    }
}

TrainReport trainFromFiles (const std::vector<fs::path>& files, const std::string& name, const TrainOptions& opt,
                            const std::function<bool (int, int)>& progress)
{
    TrainReport rep;
    ProfileTrainer trainer;
    std::set<uint64_t> seen, seenFiles;
    const int total = (int) files.size();

    for (int i = 0; i < total; ++i)
    {
        if (progress && ! progress (i, total)) { rep.cancelled = true; break; }
        ++rep.scanned;
        try
        {
            std::error_code ec;
            const auto bytesOnDisk = fs::file_size (files[(size_t) i], ec);
            if (ec) { ++rep.unreadable; continue; }
            if (bytesOnDisk > opt.maxFileBytes) { ++rep.tooBig; continue; }

            std::ifstream in (files[(size_t) i], std::ios::binary);
            if (! in) { ++rep.unreadable; continue; }
            std::vector<uint8_t> data ((size_t) bytesOnDisk);
            if (! data.empty()) in.read (reinterpret_cast<char*> (data.data()), (std::streamsize) data.size());
            if (in.gcount() != (std::streamsize) data.size()) { ++rep.unreadable; continue; }

            const auto m = importMidi (data.data(), data.size());
            if (! m.ok) { ++rep.unreadable; continue; }
            if (opt.collectInstruments)
            {
                uint64_t fh = 1469598103934665603ull;
                for (uint8_t b : data) { fh ^= b; fh *= 1099511628211ull; }
                if (! seenFiles.insert (fh).second) ++rep.instrumentDuplicates;
                else
                {
                    rep.instruments.beginFile (m.systemMode);
                    for (const auto& t : m.tracks) rep.instruments.addTrack (t, classifyTrack (t, m.systemMode), m.tsNum, m.tsDen);
                    rep.instruments.endFile();
                    ++rep.instrumentFiles;
                }
            }
            if (opt.skipOddMeter && (m.tsNum != 4 || m.tsDen != 4)) { ++rep.oddMeter; continue; }

            std::vector<Note> drums;
            if (! extractDrums (m, drums) || (int) drums.size() < opt.minDrumNotes) { ++rep.noDrums; continue; }
            if (opt.dropDuplicates && ! seen.insert (hashNotes (drums)).second) { ++rep.duplicates; continue; }

            int end = 0;
            for (const auto& n : drums) end = std::max (end, n.start + 1);
            const int bars = std::clamp ((end + kBarTicks - 1) / kBarTicks, 1, opt.maxBarsPerFile);
            const auto single = profileOfDrums (drums, bars);
            if (single.files <= 0) { ++rep.noDrums; continue; }
            trainer.add (single, m.bpm);
            ++rep.used;
        }
        catch (...) { ++rep.unreadable; }
    }
    if (progress && ! rep.cancelled) progress (total, total);
    rep.profile = trainer.finish (name);
    return rep;
}

} // namespace icc
