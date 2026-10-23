#include "GenreLibrary.h"

#include <algorithm>

namespace icc
{

static std::filesystem::path toPath (const juce::File& f)
{
#if JUCE_WINDOWS
    return std::filesystem::path (f.getFullPathName().toWideCharPointer());
#else
    return std::filesystem::path (f.getFullPathName().toRawUTF8());
#endif
}

class GenreLibrary::Job final : public juce::Thread
{
public:
    Job (GenreLibrary& o, juce::File f, juce::String n, bool rec, TrainOptions opt)
        : juce::Thread ("ICC corpus training"), owner (o), folder (std::move (f)), name (std::move (n)), recursive (rec), options (opt) {}
    ~Job() override { stopThread (10000); }

    void run() override
    {
        {
            const juce::ScopedLock sl (owner.lock);
            owner.status = "Scanning for .mid files...";
        }
        const auto files = findMidiFiles (toPath (folder), recursive);
        owner.total.store ((int) files.size());
        owner.done.store (0);
        {
            const juce::ScopedLock sl (owner.lock);
            owner.status = juce::String ((int) files.size()) + " files found";
        }
        auto rep = trainFromFiles (files, name.toStdString(), options, [this] (int d, int t)
        {
            owner.done.store (d); owner.total.store (t);
            if ((d & 63) == 0)
            {
                const juce::ScopedLock sl (owner.lock);
                owner.status = juce::String (d) + " / " + juce::String (t) + " files";
            }
            return ! threadShouldExit();
        });
        {
            const juce::ScopedLock sl (owner.lock);
            owner.report = std::move (rep);
            owner.status = "Done";
        }
        owner.running.store (false);
        owner.finishedFlag.store (true);
    }

private:
    GenreLibrary& owner;
    juce::File folder; juce::String name; bool recursive; TrainOptions options;
};

GenreLibrary::GenreLibrary (juce::File d) : dir (std::move (d))
{
    refresh();
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.icinst");
    juce::File newest;
    for (const auto& f : files) if (newest == juce::File() || f.getLastModificationTime() > newest.getLastModificationTime()) newest = f;
    if (newest != juce::File()) loadInstrumentFile (newest);
}
GenreLibrary::~GenreLibrary() { if (job != nullptr) job->signalThreadShouldExit(); job.reset(); }

juce::File GenreLibrary::defaultDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("AlphaAudio").getChildFile ("InstaComposerClone").getChildFile ("Genres");
}

void GenreLibrary::setDirectory (const juce::File& d) { dir = d; refresh(); }

void GenreLibrary::refresh()
{
    entries.clear();
    if (! dir.isDirectory()) return;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.icgenre"))
    {
        juce::MemoryBlock mb;
        if (f.getSize() > 1024 * 1024 || ! f.loadFileAsData (mb)) continue;
        auto p = std::make_shared<StyleProfile>();
        if (! p->fromBytes (static_cast<const uint8_t*> (mb.getData()), mb.getSize())) continue;
        if (p->name.empty()) p->name = f.getFileNameWithoutExtension().toStdString();
        entries.push_back ({ juce::String (p->name), f, std::move (p) });
    }
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
}

juce::String GenreLibrary::nameAt (int i) const { return i >= 0 && i < count() ? entries[(size_t) i].name : juce::String(); }
std::shared_ptr<const StyleProfile> GenreLibrary::profileAt (int i) const { return i >= 0 && i < count() ? entries[(size_t) i].profile : nullptr; }

int GenreLibrary::indexOfName (const juce::String& n) const
{
    for (int i = 0; i < count(); ++i) if (entries[(size_t) i].name.equalsIgnoreCase (n)) return i;
    return -1;
}

bool GenreLibrary::remove (int i)
{
    if (i < 0 || i >= count()) return false;
    const bool ok = entries[(size_t) i].file.deleteFile();
    refresh();
    return ok;
}

bool GenreLibrary::importFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (f.getSize() > 1024 * 1024 || ! f.loadFileAsData (mb)) return false;
    StyleProfile p;
    if (! p.fromBytes (static_cast<const uint8_t*> (mb.getData()), mb.getSize())) return false;
    if (! dir.createDirectory().wasOk()) return false;
    const auto target = dir.getChildFile (juce::File::createLegalFileName (juce::String (p.name)) + ".icgenre");
    if (! target.replaceWithData (mb.getData(), mb.getSize())) return false;
    refresh();
    return true;
}

bool GenreLibrary::startTraining (const juce::File& folder, const juce::String& name, bool recursive, const TrainOptions& opt)
{
    if (running.load() || ! folder.isDirectory()) return false;
    job.reset();                                   // joins a previous, finished thread
    finishedFlag.store (false);
    running.store (true);
    done.store (0); total.store (0);
    job = std::make_unique<Job> (*this, folder, name.trim().isEmpty() ? folder.getFileName() : name.trim(), recursive, opt);
    job->startThread();
    return true;
}

void GenreLibrary::cancelTraining() { if (job != nullptr) job->signalThreadShouldExit(); }

double GenreLibrary::progress() const noexcept
{
    const int t = total.load();
    return t > 0 ? (double) done.load() / (double) t : 0.0;
}

juce::String GenreLibrary::statusText() const { const juce::ScopedLock sl (lock); return status; }

std::shared_ptr<const InstrumentTable> GenreLibrary::instruments() const { const juce::ScopedLock sl (lock); return lastInstruments; }

bool GenreLibrary::loadInstrumentFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (f.getSize() > 64 * 1024 * 1024 || ! f.loadFileAsData (mb)) return false;
    auto t = std::make_shared<InstrumentTable>();
    if (! t->fromBytes (static_cast<const uint8_t*> (mb.getData()), mb.getSize())) return false;
    const juce::ScopedLock sl (lock);
    lastInstruments = std::move (t);
    return true;
}

bool GenreLibrary::saveInstruments (const InstrumentTable& t, const juce::String& name)
{
    const auto bytes = t.toBytes();
    if (! dir.createDirectory().wasOk()) return false;
    const auto f = dir.getChildFile (juce::File::createLegalFileName (name.isEmpty() ? juce::String ("instruments") : name) + ".icinst");
    return f.replaceWithData (bytes.data(), bytes.size());
}

bool GenreLibrary::takeFinished (TrainReport& out, juce::String& savedName)
{
    if (! finishedFlag.exchange (false)) return false;
    {
        const juce::ScopedLock sl (lock);
        out = report;
    }
    savedName.clear();
    if (! out.cancelled && out.instrumentFiles > 0)
    {
        saveInstruments (out.instruments, juce::String (out.profile.name));
        const juce::ScopedLock sl (lock);
        lastInstruments = std::make_shared<InstrumentTable> (out.instruments);
    }
    if (! out.cancelled && out.used > 0 && out.profile.files > 0)
    {
        const auto bytes = out.profile.toBytes();
        if (dir.createDirectory().wasOk())
        {
            const auto f = dir.getChildFile (juce::File::createLegalFileName (juce::String (out.profile.name)) + ".icgenre");
            if (f.replaceWithData (bytes.data(), bytes.size())) savedName = juce::String (out.profile.name);
        }
        refresh();
    }
    return true;
}

} // namespace icc
