#include "PluginEditor.h"

namespace icc
{

static const juce::Colour kTrackColours[kNumTracks] = {
    juce::Colour (0xff4fc3f7), juce::Colour (0xffffb74d), juce::Colour (0xff81c784), juce::Colour (0xffe57373),
    juce::Colour (0xffba68c8), juce::Colour (0xfffff176), juce::Colour (0xff4db6ac) };

// ------------------------------------------------------------------ DragExport
void DragExport::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff2b3a55));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colour (0xff6c8cc8));
    g.drawRoundedRectangle (r, 5.0f, 1.2f);
    g.setColour (juce::Colours::white);
    g.setFont (fontSize);
    g.drawText (label, getLocalBounds(), juce::Justification::centred);
}

void DragExport::mouseDown (const juce::MouseEvent&)
{
    started = false;
    file = provider ? writeTempMidiFile (provider(), extension ? extension() : juce::String (".mid")) : juce::File();   // written up-front so the drag starts instantly
}

void DragExport::mouseDrag (const juce::MouseEvent& e)
{
    if (started || e.getDistanceFromDragStart() < 6 || ! file.existsAsFile()) return;
    started = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

// ------------------------------------------------------------------ Map popup
namespace
{
    class MapPanel final : public juce::Component
    {
    public:
        explicit MapPanel (const std::string& text)
        {
            box.setMultiLine (true, false);
            box.setReadOnly (true);
            box.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain)));
            box.setText (juce::String::fromUTF8 (text.c_str()), false);
            copy.onClick = [text] { juce::SystemClipboard::copyTextToClipboard (juce::String::fromUTF8 (text.c_str())); };
            addAndMakeVisible (box);
            addAndMakeVisible (copy);
            setSize (700, 600);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            copy.setBounds (r.removeFromBottom (28).removeFromLeft (170));
            r.removeFromBottom (6);
            box.setBounds (r);
        }
    private:
        juce::TextEditor box;
        juce::TextButton copy { "Copy to clipboard" };
    };
}

void Editor::showMap()
{
    auto* panel = new MapPanel (proc.getMappingText());
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (panel), mapBtn.getScreenBounds(), nullptr);
}

// ------------------------------------------------------------------ Editor
Editor::Editor (Processor& p) : AudioProcessorEditor (&p), proc (p), corpusPanel (p)
{
    laf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    setLookAndFeel (&laf);

    auto& apvts = proc.getAPVTS();

    auto fillChoice = [&apvts] (juce::ComboBox& box, const char* id)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
            box.addItemList (c->choices, 1);
    };
    fillChoice (rootBox, ids::root);
    fillChoice (scaleBox, ids::scale);
    fillChoice (progBox, ids::progression);
    fillChoice (drumStyleBox, ids::drumStyle);
    styleAtt = std::make_unique<CA> (apvts, ids::drumStyle, drumStyleBox);
    rootAtt  = std::make_unique<CA> (apvts, ids::root, rootBox);
    scaleAtt = std::make_unique<CA> (apvts, ids::scale, scaleBox);
    progAtt  = std::make_unique<CA> (apvts, ids::progression, progBox);

    auto setupSlider = [] (juce::Slider& s, juce::Slider::SliderStyle st, int tbw)
    {
        s.setSliderStyle (st);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, tbw, 22);
    };
    setupSlider (bpmSlider, juce::Slider::LinearHorizontal, 60);
    setupSlider (foliageSlider, juce::Slider::LinearHorizontal, 50);
    setupSlider (barsSlider, juce::Slider::IncDecButtons, 40);
    setupSlider (seedSlider, juce::Slider::IncDecButtons, 90);
    for (auto* sl : { &complexitySlider, &swingSlider, &humanizeSlider, &fillsSlider }) setupSlider (*sl, juce::Slider::LinearHorizontal, 40);
    setupSlider (variantsSlider, juce::Slider::IncDecButtons, 36);
    bpmAtt     = std::make_unique<SA> (apvts, ids::bpm, bpmSlider);
    barsAtt    = std::make_unique<SA> (apvts, ids::bars, barsSlider);
    seedAtt    = std::make_unique<SA> (apvts, ids::seed, seedSlider);
    foliageAtt = std::make_unique<SA> (apvts, ids::foliage, foliageSlider);
    snapAtt    = std::make_unique<BA> (apvts, ids::snapScale, snapBtn);
    cxAtt      = std::make_unique<SA> (apvts, ids::drumComplexity, complexitySlider);
    swingAtt   = std::make_unique<SA> (apvts, ids::swing, swingSlider);
    humAtt     = std::make_unique<SA> (apvts, ids::humanize, humanizeSlider);
    fillsAtt   = std::make_unique<SA> (apvts, ids::fills, fillsSlider);
    varAtt     = std::make_unique<SA> (apvts, ids::candidates, variantsSlider);
    pcAtt      = std::make_unique<BA> (apvts, ids::sendProgram, sendPcBtn);

    const char* capText[] = { "Root", "Scale", "Progression", "BPM", "Bars", "Seed", "Foliage", "Drum style", "Complexity",
                              "Swing", "Humanize", "Fills", "Variants (best of)", "" };
    juce::Component* capTarget[] = { &rootBox, &scaleBox, &progBox, &bpmSlider, &barsSlider, &seedSlider, &foliageSlider,
                                     &drumStyleBox, &complexitySlider, &swingSlider, &humanizeSlider, &fillsSlider, &variantsSlider, nullptr };
    for (size_t i = 0; i < 13; ++i)
    {
        captions[i].setText (capText[i], juce::dontSendNotification);
        captions[i].setFont (juce::FontOptions (11.0f));
        captions[i].setColour (juce::Label::textColourId, juce::Colour (0xff9aa3b5));
        captions[i].attachToComponent (capTarget[i], false);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &rootBox, &scaleBox, &progBox, &bpmSlider, &barsSlider, &seedSlider,
                                                              &foliageSlider, &generateBtn, &diceBtn, &previewBtn, &snapBtn, &drawBtn,
                                                              &undoBtn, &redoBtn, &gridBox, &noteTrackBox, &saveBtn, &status,
                                                              &roll, &dragExport, &drumStyleBox, &complexitySlider, &swingSlider,
                                                              &humanizeSlider, &fillsSlider, &variantsSlider, &sendPcBtn, &mapBtn, &progStrip, &fscBtn })
        addAndMakeVisible (c);

    generateBtn.onClick = [this] { proc.generate(); syncPattern(); };
    diceBtn.onClick     = [this] { proc.randomizeSeed(); proc.generate(); syncPattern(); };
    mapBtn.onClick      = [this] { showMap(); };
    progStrip.setFont (juce::FontOptions (13.0f));
    progStrip.setColour (juce::Label::textColourId, juce::Colour (0xffc9d3ea));
    progStrip.setJustificationType (juce::Justification::centredLeft);
    previewBtn.setClickingTogglesState (true);
    previewBtn.onClick  = [this] { proc.setPreviewPlaying (previewBtn.getToggleState()); };
    drawBtn.setClickingTogglesState (true);
    drawBtn.onClick     = [this] { roll.setDrawMode (drawBtn.getToggleState()); };
    undoBtn.onClick     = [this] { roll.undo(); };
    redoBtn.onClick     = [this] { roll.redo(); };
    saveBtn.onClick     = [this] { saveMidiAs(); };

    gridBox.addItemList ({ "1/4", "1/8", "1/16", "1/32" }, 1);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    gridBox.onChange = [this] { static const int div[] = { 4, 8, 16, 32 }; roll.setGridDivision (div[juce::jlimit (0, 3, gridBox.getSelectedId() - 1)]); };
    for (int t = 0; t < kNumTracks; ++t) noteTrackBox.addItem (trackName (t), t + 1);
    noteTrackBox.setSelectedId (1, juce::dontSendNotification);
    noteTrackBox.onChange = [this] { selectEditTrack (noteTrackBox.getSelectedId() - 1); };

    for (int i = 0; i < kNumScenes; ++i)
    {
        sceneBtns[(size_t) i].setButtonText ("Scene " + juce::String (i + 1));
        sceneBtns[(size_t) i].onClick = [this, i] { proc.selectScene (i); syncPattern(); };
        addAndMakeVisible (sceneBtns[(size_t) i]);
    }

    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& r = rows[(size_t) t];
        r.name.setText (trackName (t), juce::dontSendNotification);
        r.name.setColour (juce::Label::textColourId, kTrackColours[t]);
        for (int c = 1; c <= 16; ++c) r.channel.addItem (juce::String (c), c);
        if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ids::program (t))))
            r.program.addItemList (pc->choices, 1);
        r.chAtt = std::make_unique<CA> (apvts, ids::channel (t), r.channel);
        r.prAtt = std::make_unique<CA> (apvts, ids::program (t), r.program);
        r.mAtt  = std::make_unique<BA> (apvts, ids::mute (t), r.mute);
        r.sAtt  = std::make_unique<BA> (apvts, ids::solo (t), r.solo);
        r.lAtt  = std::make_unique<BA> (apvts, ids::lock (t), r.lock);
        r.mute.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffe57373));
        r.solo.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xfffff176));
        r.lock.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff81d4fa));
        r.lock.setTooltip ("Lock: Generate leaves this track alone (the others still line up with it)");
        r.program.setTooltip ("General MIDI program for this track (sent only when 'Send PC' is on)");
        r.drag.label = "MIDI";
        r.drag.fontSize = 11.0f;
        r.drag.provider = [this, t] { return fscBtn.getToggleState() ? proc.renderFsc (t) : proc.renderMidiFile (true, t); };
        r.drag.extension = [this] { return juce::String (fscBtn.getToggleState() ? ".fsc" : ".mid"); };
        for (auto* c : std::initializer_list<juce::Component*> { &r.name, &r.channel, &r.program, &r.mute, &r.solo, &r.lock, &r.drag })
            addAndMakeVisible (c);
    }

    roll.onEdited = [this] (const Pattern& pat) { proc.setPattern (pat); updateCounts (pat); };
    roll.onHistoryChanged = [this] { undoBtn.setEnabled (roll.canUndo()); redoBtn.setEnabled (roll.canRedo()); };
    undoBtn.setEnabled (false);
    redoBtn.setEnabled (false);

    dragExport.provider = [this] { return fscBtn.getToggleState() ? proc.renderFsc (noteTrackBox.getSelectedId() - 1) : proc.renderMidiFile (true); };
    dragExport.extension = [this] { return juce::String (fscBtn.getToggleState() ? ".fsc" : ".mid"); };
    fscBtn.onClick = [this] { applyFormat(); };
    fscBtn.setTooltip ("Save and drag FL Studio score files (.fsc) instead of .mid. A score holds one track: the one chosen in the note-track box (the per-track handles give each track).");

    addChildComponent (corpusPanel);
    for (auto* b : { &composeTab, &corpusTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (77);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3f7fd9));
        addAndMakeVisible (*b);
    }
    composeTab.setToggleState (true, juce::dontSendNotification);
    composeTab.onClick = [this] { showTab (false); };
    corpusTab.onClick  = [this] { showTab (true); };

    setResizable (true, true);
    setResizeLimits (1100, 700, 2400, 1300);
    setSize (1240, 830);
    syncPattern();
    startTimerHz (30);
}

Editor::~Editor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Editor::showTab (bool corpus)
{
    corpusPanel.setVisible (corpus);
    corpusPanel.toFront (false);
    composeTab.toFront (false);
    corpusTab.toFront (false);
}

void Editor::selectEditTrack (int t)
{
    t = juce::jlimit (0, kNumTracks - 1, t);
    noteTrackBox.setSelectedId (t + 1, juce::dontSendNotification);
    roll.setNewNoteTrack (t);
    roll.setDrumLabels (t == Rhythm);   // GM drum names on the keys while you work on the drums
}

void Editor::syncPattern()
{
    seenVersion = proc.getPatternVersion();
    const auto pat = proc.getPattern();
    roll.setBars (proc.barsFromState());
    roll.setPattern (pat);
    updateCounts (pat);
}

void Editor::updateCounts (const Pattern& p)
{
    lastNotes = countNotes (p);
    refreshStatus();
}

void Editor::refreshStatus()
{
    status.setText (juce::String ((int) lastNotes) + " notes    " + proc.getLastGenerationSummary(), juce::dontSendNotification);
}

void Editor::applyFormat()
{
    const bool fsc = fscBtn.getToggleState();
    saveBtn.setButtonText (fsc ? "Save .fsc" : "Save .mid");
    dragExport.label = fsc ? "Drag FL score (.fsc)" : "Drag MIDI into your DAW";
    for (auto& r : rows) { r.drag.label = fsc ? "FSC" : "MIDI"; r.drag.repaint(); }
    dragExport.repaint();
    if (fsc) status.setText (proc.hasFscTemplate() ? "FSC: the selected note track is written using the layout learned from your FL file."
                                                   : "FSC: no FL-saved score learned yet (Corpus tab > Learn FSC format). Files use a best-guess layout.", juce::dontSendNotification);
}

bool Editor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("mid;midi;fsc;icgenre")) return true;
    return false;
}

void Editor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        const juce::File f (path);
        juce::String msg;
        if (f.hasFileExtension ("mid;midi"))      msg = proc.importMidiFile (f, 0).message;
        else if (f.hasFileExtension ("fsc"))      msg = proc.importFscFile (f, noteTrackBox.getSelectedId() - 1).message;
        else if (f.hasFileExtension ("icgenre"))  { msg = proc.getGenres().importFile (f) ? "Loaded genre " + f.getFileName() : "Not a valid genre file."; corpusPanel.refresh(); }
        else continue;
        syncPattern();
        status.setText (msg, juce::dontSendNotification);
        return;
    }
}

void Editor::saveMidiAs()
{
    const bool fsc = fscBtn.getToggleState();
    const int track = juce::jlimit (0, kNumTracks - 1, noteTrackBox.getSelectedId() - 1);
    if (fsc)
    {
        chooser = std::make_unique<juce::FileChooser> ("Save FL Studio score",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::String ("ICC ") + trackName (track) + ".fsc"), "*.fsc");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, track] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            const auto bytes = proc.renderFsc (track);
            status.setText (f.replaceWithData (bytes.data(), bytes.size()) ? "Saved " + f.getFileName() + " (" + trackName (track) + ")" : "Could not write file", juce::dontSendNotification);
        });
        return;
    }
    chooser = std::make_unique<juce::FileChooser> ("Save MIDI file",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("InstaComposerClone.mid"), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto bytes = proc.renderMidiFile (true);
        status.setText (f.replaceWithData (bytes.data(), bytes.size()) ? "Saved " + f.getFileName() : "Could not write file",
                        juce::dontSendNotification);
    });
}

void Editor::timerCallback()
{
    if (proc.getPatternVersion() != seenVersion) syncPattern();
    else refreshStatus();
    progStrip.setText (juce::String::fromUTF8 (proc.getProgressionText().c_str()), juce::dontSendNotification);

    const auto gp = proc.paramsFromState();
    roll.setBars (gp.bars);
    roll.setScale (gp.rootPc, gp.scaleIndex);
    roll.setRouting (proc.routingFromState());
    roll.setSnapToScale (snapBtn.getToggleState());
    roll.setPlayhead (proc.getPlayheadTick(), proc.isTransportRunning());

    const int active = proc.getActiveScene(), pending = proc.getPendingScene();
    for (int i = 0; i < kNumScenes; ++i)
    {
        auto& b = sceneBtns[(size_t) i];
        const juce::Colour base = i == active ? juce::Colour (0xff3f7fd9) : (proc.isScenePopulated (i) ? juce::Colour (0xff3a4358) : juce::Colour (0xff23262e));
        b.setColour (juce::TextButton::buttonColourId, i == pending ? juce::Colour (0xffd98a2b) : base);
    }
    if (previewBtn.getToggleState() != proc.isPreviewPlaying())
        previewBtn.setToggleState (proc.isPreviewPlaying(), juce::dontSendNotification);
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d24));
}

void Editor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto takeLeft = [] (juce::Rectangle<int>& row, int w) { auto c = row.removeFromLeft (w); row.removeFromLeft (8); return c; };

    auto row1 = r.removeFromTop (46); row1.removeFromTop (16);
    rootBox.setBounds (takeLeft (row1, 80));
    scaleBox.setBounds (takeLeft (row1, 210));
    progBox.setBounds (takeLeft (row1, 230));
    bpmSlider.setBounds (takeLeft (row1, 240));
    barsSlider.setBounds (takeLeft (row1, 110));
    {
        auto tabs = row1.removeFromRight (190);
        composeTab.setBounds (tabs.removeFromLeft (92)); tabs.removeFromLeft (6);
        corpusTab.setBounds (tabs.removeFromLeft (92));
    }
    corpusPanel.setBounds (getLocalBounds().withTrimmedTop (57));

    auto row2 = r.removeFromTop (46); row2.removeFromTop (16);
    seedSlider.setBounds (takeLeft (row2, 170));
    diceBtn.setBounds (takeLeft (row2, 50));
    foliageSlider.setBounds (takeLeft (row2, 220));
    generateBtn.setBounds (takeLeft (row2, 110));
    previewBtn.setBounds (takeLeft (row2, 90));
    snapBtn.setBounds (takeLeft (row2, 130));
    sendPcBtn.setBounds (takeLeft (row2, 90));
    mapBtn.setBounds (takeLeft (row2, 60));

    auto row3 = r.removeFromTop (46); row3.removeFromTop (16);
    drumStyleBox.setBounds (takeLeft (row3, 190));
    complexitySlider.setBounds (takeLeft (row3, 190));
    swingSlider.setBounds (takeLeft (row3, 170));
    humanizeSlider.setBounds (takeLeft (row3, 170));
    fillsSlider.setBounds (takeLeft (row3, 170));
    variantsSlider.setBounds (takeLeft (row3, 110));

    r.removeFromTop (4);
    auto scenes = r.removeFromTop (28);
    const int sw = (scenes.getWidth() - 7 * 6) / kNumScenes;
    for (auto& b : sceneBtns) { b.setBounds (scenes.removeFromLeft (sw)); scenes.removeFromLeft (6); }
    r.removeFromTop (4);
    progStrip.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    auto bottom = r.removeFromBottom (34);
    r.removeFromBottom (6);
    dragExport.setBounds (takeLeft (bottom, 240));
    saveBtn.setBounds (takeLeft (bottom, 110));
    fscBtn.setBounds (takeLeft (bottom, 130));
    status.setBounds (bottom);

    auto left = r.removeFromLeft (470);
    r.removeFromLeft (8);
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto row = left.removeFromTop (30);
        auto& tr = rows[(size_t) t];
        auto take = [&row] (int w) { auto c = row.removeFromLeft (w); row.removeFromLeft (4); return c; };
        tr.name.setBounds (take (58));
        tr.channel.setBounds (take (54));
        tr.mute.setBounds (take (40));
        tr.solo.setBounds (take (40));
        tr.lock.setBounds (take (40));
        tr.drag.setBounds (take (44).reduced (0, 2));
        tr.program.setBounds (row);
    }

    auto tools = r.removeFromTop (28);
    r.removeFromTop (6);
    undoBtn.setBounds (takeLeft (tools, 60));
    redoBtn.setBounds (takeLeft (tools, 60));
    gridBox.setBounds (takeLeft (tools, 70));
    drawBtn.setBounds (takeLeft (tools, 60));
    noteTrackBox.setBounds (takeLeft (tools, 100));
    roll.setBounds (r);
}

} // namespace icc
