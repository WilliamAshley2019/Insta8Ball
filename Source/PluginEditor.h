#pragma once
// PluginEditor.h - the whole UI: parameter controls, scenes, track routing, piano roll, MIDI export.

#include "PianoRoll.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace icc
{

/** Drag this onto a DAW track / piano roll to drop the current pattern as a .mid file. */
class DragExport final : public juce::Component
{
public:
    std::function<std::vector<uint8_t>()> provider;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { file = juce::File(); started = false; }
private:
    juce::File file;
    bool started = false;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void syncPattern();
    void updateCounts (const Pattern&);
    void saveMidiAs();

    Processor& proc;
    juce::LookAndFeel_V4 laf;
    uint32_t seenVersion = 0xFFFFFFFFu;

    // parameter controls
    juce::ComboBox rootBox, scaleBox, progBox;
    juce::Slider   bpmSlider, barsSlider, seedSlider, foliageSlider;
    juce::TextButton generateBtn { "Generate" }, diceBtn { "Dice" }, previewBtn { "Preview" }, drawBtn { "Draw" },
                     undoBtn { "Undo" }, redoBtn { "Redo" }, saveBtn { "Save .mid" };
    juce::ToggleButton snapBtn { "Snap to scale" };
    juce::ComboBox gridBox, noteTrackBox;
    std::array<juce::Label, 8> captions;
    juce::Label status;

    std::array<juce::TextButton, kNumScenes> sceneBtns;

    struct TrackRow
    {
        juce::Label        name, count;
        juce::ComboBox     channel;
        juce::ToggleButton mute { "M" }, solo { "S" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   mAtt, sAtt;
    };
    std::array<TrackRow, kNumTracks> rows;

    PianoRoll  roll;
    DragExport dragExport;
    std::unique_ptr<juce::FileChooser> chooser;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> rootAtt, scaleAtt, progAtt;
    std::unique_ptr<SA> bpmAtt, barsAtt, seedAtt, foliageAtt;
    std::unique_ptr<BA> snapAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace icc
