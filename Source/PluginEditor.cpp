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
    g.setFont (13.0f);
    g.drawText ("Drag MIDI into your DAW", getLocalBounds(), juce::Justification::centred);
}

void DragExport::mouseDown (const juce::MouseEvent&)
{
    started = false;
    file = provider ? writeTempMidiFile (provider()) : juce::File();   // written up-front so the drag starts instantly
}

void DragExport::mouseDrag (const juce::MouseEvent& e)
{
    if (started || e.getDistanceFromDragStart() < 6 || ! file.existsAsFile()) return;
    started = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

// ------------------------------------------------------------------ Editor
Editor::Editor (Processor& p) : AudioProcessorEditor (&p), proc (p)
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
    bpmAtt     = std::make_unique<SA> (apvts, ids::bpm, bpmSlider);
    barsAtt    = std::make_unique<SA> (apvts, ids::bars, barsSlider);
    seedAtt    = std::make_unique<SA> (apvts, ids::seed, seedSlider);
    foliageAtt = std::make_unique<SA> (apvts, ids::foliage, foliageSlider);
    snapAtt    = std::make_unique<BA> (apvts, ids::snapScale, snapBtn);

    const char* capText[] = { "Root", "Scale", "Progression", "BPM", "Bars", "Seed", "Foliage", "" };
    juce::Component* capTarget[] = { &rootBox, &scaleBox, &progBox, &bpmSlider, &barsSlider, &seedSlider, &foliageSlider, nullptr };
    for (size_t i = 0; i < 7; ++i)
    {
        captions[i].setText (capText[i], juce::dontSendNotification);
        captions[i].setFont (juce::FontOptions (11.0f));
        captions[i].setColour (juce::Label::textColourId, juce::Colour (0xff9aa3b5));
        captions[i].attachToComponent (capTarget[i], false);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &rootBox, &scaleBox, &progBox, &bpmSlider, &barsSlider, &seedSlider,
                                                              &foliageSlider, &generateBtn, &diceBtn, &previewBtn, &snapBtn, &drawBtn,
                                                              &undoBtn, &redoBtn, &gridBox, &noteTrackBox, &saveBtn, &status,
                                                              &roll, &dragExport })
        addAndMakeVisible (c);

    generateBtn.onClick = [this] { proc.generate(); syncPattern(); };
    diceBtn.onClick     = [this] { proc.randomizeSeed(); proc.generate(); syncPattern(); };
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
    noteTrackBox.onChange = [this] { roll.setNewNoteTrack (noteTrackBox.getSelectedId() - 1); };

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
        r.count.setColour (juce::Label::textColourId, juce::Colour (0xff9aa3b5));
        r.count.setJustificationType (juce::Justification::centredRight);
        for (int c = 1; c <= 16; ++c) r.channel.addItem (juce::String (c), c);
        r.chAtt = std::make_unique<CA> (apvts, ids::channel (t), r.channel);
        r.mAtt  = std::make_unique<BA> (apvts, ids::mute (t), r.mute);
        r.sAtt  = std::make_unique<BA> (apvts, ids::solo (t), r.solo);
        r.mute.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffe57373));
        r.solo.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xfffff176));
        for (auto* c : std::initializer_list<juce::Component*> { &r.name, &r.count, &r.channel, &r.mute, &r.solo })
            addAndMakeVisible (c);
    }

    roll.onEdited = [this] (const Pattern& pat) { proc.setPattern (pat); updateCounts (pat); };
    roll.onHistoryChanged = [this] { undoBtn.setEnabled (roll.canUndo()); redoBtn.setEnabled (roll.canRedo()); };
    undoBtn.setEnabled (false);
    redoBtn.setEnabled (false);

    dragExport.provider = [this] { return proc.renderMidiFile (true); };

    setResizable (true, true);
    setResizeLimits (900, 620, 2000, 1200);
    setSize (1100, 760);
    syncPattern();
    startTimerHz (30);
}

Editor::~Editor()
{
    stopTimer();
    setLookAndFeel (nullptr);
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
    size_t total = 0;
    for (int t = 0; t < kNumTracks; ++t)
    {
        rows[(size_t) t].count.setText (juce::String (p[(size_t) t].size()), juce::dontSendNotification);
        total += p[(size_t) t].size();
    }
    status.setText (juce::String ((int) total) + " notes", juce::dontSendNotification);
}

void Editor::saveMidiAs()
{
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

    auto row2 = r.removeFromTop (46); row2.removeFromTop (16);
    seedSlider.setBounds (takeLeft (row2, 170));
    diceBtn.setBounds (takeLeft (row2, 50));
    foliageSlider.setBounds (takeLeft (row2, 220));
    generateBtn.setBounds (takeLeft (row2, 110));
    previewBtn.setBounds (takeLeft (row2, 90));
    snapBtn.setBounds (takeLeft (row2, 130));

    r.removeFromTop (4);
    auto scenes = r.removeFromTop (28);
    const int sw = (scenes.getWidth() - 7 * 6) / kNumScenes;
    for (auto& b : sceneBtns) { b.setBounds (scenes.removeFromLeft (sw)); scenes.removeFromLeft (6); }
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (34);
    r.removeFromBottom (6);
    dragExport.setBounds (takeLeft (bottom, 240));
    saveBtn.setBounds (takeLeft (bottom, 110));
    status.setBounds (bottom);

    auto left = r.removeFromLeft (270);
    r.removeFromLeft (8);
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto row = left.removeFromTop (30);
        auto& tr = rows[(size_t) t];
        tr.name.setBounds (takeLeft (row, 62));
        tr.channel.setBounds (takeLeft (row, 54));
        tr.mute.setBounds (takeLeft (row, 40));
        tr.solo.setBounds (takeLeft (row, 40));
        tr.count.setBounds (row);
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
