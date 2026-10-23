#include "CorpusPanel.h"

namespace icc
{

namespace
{
    void styleTitle (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff8fb4ff));
    }
    void styleNote (juce::Label& l)
    {
        l.setFont (juce::FontOptions (13.0f));
        l.setColour (juce::Label::textColourId, juce::Colour (0xffc7ccd6));
        l.setMinimumHorizontalScale (1.0f);
        l.setJustificationType (juce::Justification::topLeft);
    }
}

CorpusPanel::CorpusPanel (Processor& p) : proc (p), progressBar (progressValue)
{
    styleTitle (importTitle, "1. Import MIDI");
    styleTitle (targetTitle, "2. Target style");
    styleTitle (trainTitle, "3. Train a genre from your MIDI library (optional)");
    for (auto* l : { &importMsg, &matchLabel, &folderLabel, &trainStatus, &trainReport, &hint }) styleNote (*l);

    hint.setText ("Everything on this tab is optional. Without training data the plugin works exactly like the Compose tab. "
                  "A trained genre is a statistical portrait of the drum parts in a folder: which steps each drum hits, how loud, how swung, how dense.",
                  juce::dontSendNotification);
    importMsg.setText ("Loads a .mid into the active scene (replaces its pattern). Channel 10 becomes the Rhythm track; other tracks are sorted by register.", juce::dontSendNotification);

    startBarSlider.setSliderStyle (juce::Slider::LinearBar);
    startBarSlider.setRange (1, 64, 1);
    startBarSlider.setValue (1, juce::dontSendNotification);
    startBarSlider.setTextValueSuffix ("  start bar");
    strengthSlider.setSliderStyle (juce::Slider::LinearBar);
    strengthSlider.setRange (0.0, 1.0, 0.01);
    strengthSlider.setValue (0.7, juce::dontSendNotification);
    strengthSlider.setTextValueSuffix ("  strength");

    nameEdit.setTextToShowWhenEmpty ("Genre name (default: folder name)", juce::Colours::grey);
    nameEdit.setMultiLine (false);
    profileView.setMultiLine (true, false);
    profileView.setReadOnly (true);
    profileView.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    profileView.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff14161c));
    recursiveBtn.setToggleState (true, juce::dontSendNotification);
    cancelBtn.setEnabled (false);
    folderLabel.setText ("No folder chosen", juce::dontSendNotification);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &importTitle, &targetTitle, &trainTitle, &importMsg, &matchLabel, &folderLabel,
            &trainStatus, &trainReport, &hint, &importBtn, &restyleBtn, &followBtn, &generateBtn, &folderBtn, &trainBtn, &cancelBtn, &deleteBtn,
            &openBtn, &loadBtn, &chartBtn, &csvBtn, &fscBtn, &instBtn, &useTargetBtn, &recursiveBtn, &startBarSlider, &strengthSlider, &targetBox, &nameEdit, &profileView, &progressBar })
        addAndMakeVisible (*c);

    instBtn.setToggleState (true, juce::dontSendNotification);
    chartBtn.setClickingTogglesState (true);
    chartBtn.onClick = [this]
    {
        chartBtn.setButtonText (chartBtn.getToggleState() ? "Show drum profile" : "Show instrument chart");
        showProfile();
    };
    csvBtn.onClick = [this]
    {
        const auto t = proc.getGenres().instruments();
        if (t == nullptr) { trainStatus.setText ("No instrument statistics yet. Train a folder with \"Collect instrument stats\" on.", juce::dontSendNotification); return; }
        chooser = std::make_unique<juce::FileChooser> ("Export instrument statistics", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("instruments.csv"), "*.csv");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, t] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            const auto s = t->toCsv();
            trainStatus.setText (f.replaceWithData (s.data(), s.size()) ? "Saved " + f.getFileName() : "Could not write file", juce::dontSendNotification);
        });
    };
    fscBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick a .fsc saved by FL Studio (any score with at least one note)", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fsc");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            if (proc.learnFscTemplate (f)) trainStatus.setText ("Learned the .fsc layout from " + f.getFileName() + ". Saved scores now copy it.", juce::dontSendNotification);
            else
            {
                trainStatus.setText ("Could not learn from " + f.getFileName() + ": see the panel on the right.", juce::dontSendNotification);
                profileView.setText ("Could not learn the .fsc layout.\n\n" + proc.getFscDiagnosis() + "\n\nSend this text (and the file) so the reader can be fixed.", false);
                chartBtn.setToggleState (false, juce::dontSendNotification);
            }
        });
    };
    importBtn.onClick = [this] { importMidi(); };
    folderBtn.onClick = [this] { chooseFolder(); };
    trainBtn.onClick = [this] { startTraining(); };
    cancelBtn.onClick = [this] { proc.getGenres().cancelTraining(); trainStatus.setText ("Cancelling...", juce::dontSendNotification); };
    openBtn.onClick = [this] { auto d = proc.getGenres().directory(); d.createDirectory(); d.revealToUser(); };
    deleteBtn.onClick = [this]
    {
        const int t = proc.getTargetProfile();
        if (! proc.isTrainedProfile (t)) { trainStatus.setText ("Select a trained genre (not a built-in style) to delete it.", juce::dontSendNotification); return; }
        const auto name = proc.getProfileName (t);
        proc.setTargetProfile (-1);
        proc.getGenres().remove (t - drumStyleCount());
        refresh();
        trainStatus.setText ("Deleted " + name, juce::dontSendNotification);
    };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a genre (.icgenre) or instrument statistics (.icinst) file", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.icgenre;*.icinst");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            if (f.hasFileExtension ("icinst"))
            {
                trainStatus.setText (proc.getGenres().loadInstrumentFile (f) ? "Loaded " + f.getFileName() : "That is not a valid instrument statistics file.", juce::dontSendNotification);
                chartBtn.setToggleState (true, juce::sendNotification);
                return;
            }
            trainStatus.setText (proc.getGenres().importFile (f) ? "Loaded " + f.getFileName() : "That is not a valid genre file.", juce::dontSendNotification);
            refresh();
        });
    };
    targetBox.onChange = [this]
    {
        proc.setTargetProfile (targetBox.getSelectedId() - 2);   // id 1 = none
        useTargetBtn.setToggleState (proc.getUseTargetForGeneration(), juce::dontSendNotification);
        showProfile();
    };
    useTargetBtn.onClick = [this]
    {
        if (useTargetBtn.getToggleState() && proc.getTargetProfile() < 0)
        {
            useTargetBtn.setToggleState (false, juce::dontSendNotification);
            setMessage ("Pick a target style first.");
            return;
        }
        proc.setUseTargetForGeneration (useTargetBtn.getToggleState());
    };
    restyleBtn.onClick = [this] { setMessage (proc.restyleCurrentDrums ((float) strengthSlider.getValue())); };
    followBtn.onClick = [this] { proc.generate (1u << Rhythm); setMessage ("Rebuilt the other tracks around the current drums."); };
    generateBtn.onClick = [this]
    {
        if (proc.getTargetProfile() >= 0) { proc.setUseTargetForGeneration (true); useTargetBtn.setToggleState (true, juce::dontSendNotification); }
        proc.generate();
        setMessage ("Generated" + (proc.getTargetProfile() >= 0 ? " drums from " + proc.getProfileName (proc.getTargetProfile()) : juce::String()) + ".");
    };

    rebuildTargets();
    showProfile();
    startTimerHz (5);
}

CorpusPanel::~CorpusPanel() { stopTimer(); }

void CorpusPanel::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff1a1d24)); }

void CorpusPanel::visibilityChanged() { if (isVisible()) refresh(); }

void CorpusPanel::setMessage (const juce::String& s) { importMsg.setText (s, juce::dontSendNotification); }

void CorpusPanel::refresh()
{
    rebuildTargets();
    useTargetBtn.setToggleState (proc.getUseTargetForGeneration(), juce::dontSendNotification);
    showProfile();
}

void CorpusPanel::rebuildTargets()
{
    targetBox.clear (juce::dontSendNotification);
    targetBox.addItem ("(none)", 1);
    for (int i = 0; i < proc.getNumProfiles(); ++i)
        targetBox.addItem ((proc.isTrainedProfile (i) ? "Trained: " : "") + proc.getProfileName (i), i + 2);
    targetBox.setSelectedId (proc.getTargetProfile() + 2, juce::dontSendNotification);
}

void CorpusPanel::showProfile()
{
    if (chartBtn.getToggleState())
    {
        const auto t = proc.getGenres().instruments();
        profileView.setText (t != nullptr ? juce::String::fromUTF8 (t->describe().c_str())
                                          : juce::String ("No instrument statistics yet.\n\nTick \"Collect instrument stats\" and train a folder: every file is read for program changes, "
                                                          "bank selects, GM / GM2 / GS / XG resets, drum kits and track names, and the result is charted per instrument."),
                             false);
        return;
    }
    const auto p = proc.getProfile (proc.getTargetProfile());
    profileView.setText (p != nullptr ? juce::String::fromUTF8 (describeProfile (*p).c_str())
                                      : juce::String ("Pick a target style to see its drum statistics.\n\nEach row is a drum, each column a 16th note.\n"
                                                      "The darker the mark, the more often the drum hits that step."),
                         false);
}

void CorpusPanel::importMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a MIDI file", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid;*.midi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        setMessage (proc.importMidiFile (f, (int) startBarSlider.getValue() - 1).message);
    });
}

void CorpusPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of MIDI files", trainFolder.isDirectory() ? trainFolder : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.isDirectory()) return;
        trainFolder = f;
        folderLabel.setText (f.getFullPathName(), juce::dontSendNotification);
    });
}

TrainOptions CorpusPanel::trainOptions() const
{
    TrainOptions o;
    o.collectInstruments = instBtn.getToggleState();
    return o;
}

void CorpusPanel::startTraining()
{
    if (! trainFolder.isDirectory()) { trainStatus.setText ("Choose a folder first.", juce::dontSendNotification); return; }
    if (proc.getGenres().startTraining (trainFolder, nameEdit.getText(), recursiveBtn.getToggleState(), trainOptions()))
    {
        trainBtn.setEnabled (false); cancelBtn.setEnabled (true);
        trainReport.setText ({}, juce::dontSendNotification);
        trainStatus.setText ("Starting...", juce::dontSendNotification);
    }
    else trainStatus.setText ("Training is already running, or the folder is not readable.", juce::dontSendNotification);
}

void CorpusPanel::finishTraining()
{
    TrainReport rep;
    juce::String saved;
    if (! proc.getGenres().takeFinished (rep, saved)) return;
    trainBtn.setEnabled (true); cancelBtn.setEnabled (false);
    progressValue = 0.0;
    trainReport.setText (juce::String (rep.summary()), juce::dontSendNotification);
    if (saved.isNotEmpty())
    {
        rebuildTargets();
        const int idx = proc.getGenres().indexOfName (saved);
        if (idx >= 0) { proc.setTargetProfile (drumStyleCount() + idx); rebuildTargets(); }
        showProfile();
        trainStatus.setText ("Saved genre \"" + saved + "\" and selected it as the target.", juce::dontSendNotification);
    }
    else if (rep.instrumentFiles > 0 && ! rep.cancelled)
    {
        trainStatus.setText ("No usable drum parts, but instrument statistics from " + juce::String (rep.instrumentFiles) + " files were saved.", juce::dontSendNotification);
        chartBtn.setToggleState (true, juce::sendNotification);
    }
    else trainStatus.setText (rep.cancelled ? "Cancelled. Nothing was saved." : "No usable drum parts found, nothing was saved.", juce::dontSendNotification);
    if (rep.instrumentFiles > 0 && ! rep.cancelled) { if (saved.isNotEmpty()) trainStatus.setText (trainStatus.getText() + " Instrument chart ready.", juce::dontSendNotification); showProfile(); }
}

void CorpusPanel::timerCallback()
{
    if (! isVisible()) return;
    auto& lib = proc.getGenres();
    if (lib.isTraining()) { progressValue = lib.progress(); trainStatus.setText (lib.statusText(), juce::dontSendNotification); }
    finishTraining();
    const float m = proc.getTargetMatch();
    matchLabel.setText (m < 0.0f ? "Current drums vs target: -" : "Current drums vs target: " + juce::String (juce::roundToInt (m)) + "% match", juce::dontSendNotification);
}

void CorpusPanel::resized()
{
    auto r = getLocalBounds().reduced (14);
    hint.setBounds (r.removeFromTop (38));
    r.removeFromTop (6);
    auto right = r.removeFromRight (juce::jmax (360, r.getWidth() * 42 / 100));
    r.removeFromRight (12);

    importTitle.setBounds (r.removeFromTop (24));
    auto row = r.removeFromTop (30);
    importBtn.setBounds (row.removeFromLeft (140)); row.removeFromLeft (8);
    startBarSlider.setBounds (row.removeFromLeft (150));
    importMsg.setBounds (r.removeFromTop (58));
    r.removeFromTop (8);

    targetTitle.setBounds (r.removeFromTop (24));
    targetBox.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    row = r.removeFromTop (30);
    strengthSlider.setBounds (row.removeFromLeft (190)); row.removeFromLeft (8);
    restyleBtn.setBounds (row.removeFromLeft (130));
    r.removeFromTop (6);
    row = r.removeFromTop (30);
    followBtn.setBounds (row.removeFromLeft (260)); row.removeFromLeft (8);
    generateBtn.setBounds (row.removeFromLeft (170));
    r.removeFromTop (6);
    useTargetBtn.setBounds (r.removeFromTop (26));
    matchLabel.setBounds (r.removeFromTop (22));
    r.removeFromTop (12);

    trainTitle.setBounds (r.removeFromTop (24));
    row = r.removeFromTop (30);
    folderBtn.setBounds (row.removeFromLeft (130)); row.removeFromLeft (8);
    recursiveBtn.setBounds (row.removeFromLeft (140)); row.removeFromLeft (8);
    instBtn.setBounds (row.removeFromLeft (190));
    folderLabel.setBounds (r.removeFromTop (22));
    nameEdit.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    row = r.removeFromTop (30);
    trainBtn.setBounds (row.removeFromLeft (120)); row.removeFromLeft (8);
    cancelBtn.setBounds (row.removeFromLeft (90));
    r.removeFromTop (6);
    progressBar.setBounds (r.removeFromTop (22));
    trainStatus.setBounds (r.removeFromTop (22));
    trainReport.setBounds (r.removeFromTop (44));
    row = r.removeFromTop (30);
    openBtn.setBounds (row.removeFromLeft (150)); row.removeFromLeft (8);
    loadBtn.setBounds (row.removeFromLeft (140)); row.removeFromLeft (8);
    deleteBtn.setBounds (row.removeFromLeft (120));
    r.removeFromTop (6);
    row = r.removeFromTop (30);
    chartBtn.setBounds (row.removeFromLeft (170)); row.removeFromLeft (8);
    csvBtn.setBounds (row.removeFromLeft (120)); row.removeFromLeft (8);
    fscBtn.setBounds (row.removeFromLeft (170));

    profileView.setBounds (right);
}

} // namespace icc
