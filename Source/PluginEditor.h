#pragma once
// PluginEditor.h - the whole UI: parameter controls, scenes, track routing, piano roll, MIDI export.

#include "CorpusPanel.h"
#include "PianoRoll.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace icc
{

/** Drag this onto a DAW track / piano roll to drop the pattern (or one track of it) as a .mid file. */
class DragExport final : public juce::Component
{
public:
    std::function<std::vector<uint8_t>()> provider;
    juce::String label { "Drag MIDI into your DAW" };
    std::function<juce::String()> extension;   // ".mid" when unset, ".fsc" for FL scores
    float fontSize = 13.0f;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { file = juce::File(); started = false; }
private:
    juce::File file;
    bool started = false;
};

class Editor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showCorpusTab() { corpusTab.setToggleState (true, juce::dontSendNotification); showTab (true); }
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void selectEditTrack (int track);   // which track new notes go to; Rhythm also switches the keys to GM drum names

private:
    void timerCallback() override;
    void syncPattern();
    void updateCounts (const Pattern&);
    void refreshStatus();
    void saveMidiAs();
    void showMap();

    Processor& proc;
    juce::LookAndFeel_V4 laf;
    uint32_t seenVersion = 0xFFFFFFFFu;
    size_t   lastNotes = 0;

    // parameter controls
    juce::ComboBox rootBox, scaleBox, progBox, drumStyleBox;
    juce::Slider   bpmSlider, barsSlider, seedSlider, foliageSlider, complexitySlider, swingSlider, humanizeSlider, fillsSlider, variantsSlider;
    juce::TextButton generateBtn { "Generate" }, diceBtn { "Dice" }, previewBtn { "Preview" }, drawBtn { "Draw" },
                     undoBtn { "Undo" }, redoBtn { "Redo" }, saveBtn { "Save .mid" }, mapBtn { "Map" };
    juce::ToggleButton snapBtn { "Snap to scale" }, sendPcBtn { "Send PC" }, fscBtn { "FL score (.fsc)" };
    void applyFormat();
    juce::ComboBox gridBox, noteTrackBox;
    std::array<juce::Label, 14> captions;
    juce::Label status, progStrip;

    std::array<juce::TextButton, kNumScenes> sceneBtns;

    struct TrackRow
    {
        juce::Label        name;
        juce::ComboBox     channel, program;
        juce::ToggleButton mute { "M" }, solo { "S" }, lock { "L" };
        DragExport         drag;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chAtt, prAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   mAtt, sAtt, lAtt;
    };
    std::array<TrackRow, kNumTracks> rows;

    PianoRoll  roll;
    CorpusPanel corpusPanel;
    juce::TextButton composeTab { "Compose" }, corpusTab { "Corpus" };
    void showTab (bool corpus);
    DragExport dragExport;
    std::unique_ptr<juce::FileChooser> chooser;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> rootAtt, scaleAtt, progAtt, styleAtt;
    std::unique_ptr<SA> bpmAtt, barsAtt, seedAtt, foliageAtt, cxAtt, swingAtt, humAtt, fillsAtt, varAtt;
    std::unique_ptr<BA> snapAtt, pcAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace icc
