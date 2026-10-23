#pragma once
// CorpusPanel.h - the "Corpus" tab: import a .mid, pick a target style (built-in or trained), restyle the drums,
// and train new genres from a folder of MIDI files. Optional: the Compose tab works without ever opening this.

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace icc
{

class CorpusPanel final : public juce::Component, private juce::Timer
{
public:
    explicit CorpusPanel (Processor&);
    ~CorpusPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void refresh();                         // rebuild the target list and the profile view

private:
    void timerCallback() override;
    void rebuildTargets();
    void showProfile();
    void importMidi();
    void chooseFolder();
    TrainOptions trainOptions() const;
    void startTraining();
    void finishTraining();
    void setMessage (const juce::String&);

    Processor& proc;
    juce::Label  importTitle, targetTitle, trainTitle, importMsg, matchLabel, folderLabel, trainStatus, trainReport, hint;
    juce::TextButton importBtn { "Import .mid..." }, restyleBtn { "Restyle drums" }, followBtn { "Rebuild bass + melody around drums" },
                     generateBtn { "Generate with target" }, folderBtn { "Choose folder..." }, trainBtn { "Train genre" },
                     cancelBtn { "Cancel" }, chartBtn { "Show instrument chart" }, csvBtn { "Export CSV..." }, fscBtn { "Learn FSC format..." }, deleteBtn { "Delete genre" }, openBtn { "Open genre folder" }, loadBtn { "Load .icgenre..." };
    juce::ToggleButton useTargetBtn { "Use target when generating" }, recursiveBtn { "Include subfolders" }, instBtn { "Collect instrument stats" };
    juce::Slider  startBarSlider, strengthSlider;
    juce::ComboBox targetBox;
    juce::TextEditor nameEdit, profileView;
    double progressValue = 0.0;
    juce::ProgressBar progressBar;
    juce::File trainFolder;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CorpusPanel)
};

} // namespace icc
