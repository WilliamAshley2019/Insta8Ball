#pragma once
// PianoRoll.h - editable multi-track piano roll with velocity lane, undo/redo and marquee selection.

#include "Engine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace icc
{

class PianoRoll final : public juce::Component
{
public:
    PianoRoll();

    // ---- data ----
    void    setPattern (const Pattern&);          // ignored while the user is mid-drag
    Pattern getPattern() const;
    std::function<void (const Pattern&)> onEdited;        // after every committed edit
    std::function<void()>                onHistoryChanged;

    // ---- context ----
    void setBars (int bars);
    void setRouting (const Routing&);             // notes of inaudible tracks are drawn dimmed
    void setScale (int rootPc, int scaleIndex);
    void setSnapToScale (bool);
    void setGridDivision (int division);          // notes per bar: 4, 8, 16, 32
    void setDrawMode (bool);                      // click on empty space adds a note
    void setNewNoteTrack (int track) noexcept { newNoteTrack = juce::jlimit (0, kNumTracks - 1, track); }
    void setPlayhead (double tick, bool running);
    void setDrumLabels (bool);                    // label keys with General MIDI drum names (wide key column) and frame the drum range

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    // ---- Component ----
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct RNote { Note n; int track = 0; bool selected = false; };
    enum class Drag { None, Move, Resize, Marquee, Velocity };

    // geometry
    int keyW = 46;
    static constexpr int rulerH = 18, velH = 64;
    juce::Rectangle<int> noteArea() const;
    juce::Rectangle<int> velArea() const;
    float tickToX (double tick) const noexcept;
    double xToTick (float x) const noexcept;
    float pitchToY (int pitch) const noexcept;
    int   yToPitch (float y) const noexcept;
    int   gridTicks() const noexcept { return kBarTicks / grid; }
    int   loopTicks() const noexcept { return bars * kBarTicks; }
    juce::Rectangle<float> noteRect (const RNote&) const;
    int   noteAtPoint (juce::Point<int>) const;
    int   velocityHit (juce::Point<int>) const;
    void  clampView();

    // editing
    void beginEdit();
    void endEdit (bool changed);
    void restore (std::vector<RNote>);
    void deleteSelected();
    void duplicateSelected (int shiftTicks);
    void nudgeSelected (int dTicks, int dPitch);
    void snapSelectedToScale();
    void selectWhere (const std::function<bool (const RNote&)>&);
    void copySelected();
    void paste();
    void showContextMenu (juce::Point<int>);
    void notifyHistory() { if (onHistoryChanged) onHistoryChanged(); }
    int  snapPitch (int pitch) const;

    std::vector<RNote> notes, before;
    std::vector<std::vector<RNote>> undoStack, redoStack;
    std::vector<RNote> clipboard;

    int  bars = 4, grid = 16, newNoteTrack = Melody;
    int  rootPc = 0, scaleIndex = 0;
    bool snapScale = false, drawMode = false, playing = false, drumLabels = false;
    double playhead = 0.0;
    Routing routing;

    // view
    double scrollTick = 0.0, pxPerTick = 0.06;
    double topPitch = 96.0;
    float  rowH = 12.0f;

    // drag state
    Drag   drag = Drag::None;
    bool   dragChanged = false, undoArmed = false;
    double anchorTick = 0.0;
    int    anchorPitch = 0, velTarget = -1, pasteTick = 0;
    juce::Point<int> marqueeStart, marqueeEnd;
    std::vector<RNote> dragOrigin;   // copy of `notes` at drag start

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace icc
