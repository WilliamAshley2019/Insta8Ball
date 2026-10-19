#include "PianoRoll.h"
#include "Mapping.h"

#include <algorithm>
#include <cmath>

namespace icc
{

namespace
{
    const juce::Colour kTrackColours[kNumTracks] = {
        juce::Colour (0xff4fc3f7), juce::Colour (0xffffb74d), juce::Colour (0xff81c784), juce::Colour (0xffe57373),
        juce::Colour (0xffba68c8), juce::Colour (0xfffff176), juce::Colour (0xff4db6ac) };

    bool isBlackKey (int pitch) { const int pc = pcOf (pitch); return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    template <typename V>
    bool sameContent (const V& a, const V& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (! (a[i].n == b[i].n) || a[i].track != b[i].track) return false;
        return true;
    }
}

PianoRoll::PianoRoll()
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
}

// ------------------------------------------------------------------ data
void PianoRoll::setPattern (const Pattern& p)
{
    if (drag != Drag::None) return;
    notes.clear();
    for (int t = 0; t < kNumTracks; ++t)
        for (const auto& n : p[(size_t) t])
            notes.push_back ({ n, t, false });
    undoStack.clear();
    redoStack.clear();
    notifyHistory();
    repaint();
}

Pattern PianoRoll::getPattern() const
{
    Pattern p;
    for (const auto& r : notes) p[(size_t) r.track].push_back (r.n);
    sortPattern (p);
    return p;
}

void PianoRoll::setBars (int b)
{
    b = juce::jlimit (1, kMaxBars, b);
    if (b == bars) return;
    bars = b;
    clampView();
    repaint();
}

void PianoRoll::setRouting (const Routing& r)
{
    if (r.mute == routing.mute && r.solo == routing.solo) return;
    routing = r;
    repaint();
}

void PianoRoll::setScale (int root, int scale)
{
    if (root == rootPc && scale == scaleIndex) return;
    rootPc = root; scaleIndex = scale;
    repaint();
}

void PianoRoll::setSnapToScale (bool b) { snapScale = b; }
void PianoRoll::setGridDivision (int d) { grid = juce::jlimit (1, 64, d); repaint(); }
void PianoRoll::setDrawMode (bool b) { drawMode = b; }

void PianoRoll::setPlayhead (double tick, bool running)
{
    if (running == playing && std::abs (tick - playhead) < 1.0) return;
    playhead = tick; playing = running;
    repaint();
}

// ------------------------------------------------------------------ geometry
juce::Rectangle<int> PianoRoll::noteArea() const
{
    return getLocalBounds().withTrimmedLeft (keyW).withTrimmedTop (rulerH).withTrimmedBottom (velH);
}

juce::Rectangle<int> PianoRoll::velArea() const
{
    return getLocalBounds().withTrimmedLeft (keyW).removeFromBottom (velH);
}

float PianoRoll::tickToX (double t) const noexcept { return (float) keyW + (float) ((t - scrollTick) * pxPerTick); }
double PianoRoll::xToTick (float x) const noexcept { return scrollTick + (double) (x - (float) keyW) / pxPerTick; }
float PianoRoll::pitchToY (int p) const noexcept   { return (float) rulerH + (float) (topPitch - (double) p) * rowH; }
int   PianoRoll::yToPitch (float y) const noexcept { return (int) std::floor (topPitch - (double) (y - (float) rulerH) / (double) rowH); }

void PianoRoll::clampView()
{
    const auto a = noteArea();
    if (a.getWidth() <= 0 || a.getHeight() <= 0) return;
    pxPerTick  = juce::jlimit ((double) a.getWidth() / (double) loopTicks(), 0.5, std::max (pxPerTick, 0.005));
    pxPerTick  = std::max (pxPerTick, (double) a.getWidth() / (double) loopTicks());
    const double visTicks = (double) a.getWidth() / pxPerTick;
    scrollTick = juce::jlimit (0.0, std::max (0.0, (double) loopTicks() - visTicks), scrollTick);
    const double rows = (double) a.getHeight() / (double) rowH;
    topPitch = juce::jlimit (std::min (127.0, rows - 1.0), 127.0, topPitch);
}

juce::Rectangle<float> PianoRoll::noteRect (const RNote& r) const
{
    return { tickToX (r.n.start), pitchToY (r.n.pitch) + 1.0f,
             std::max (3.0f, (float) (r.n.length * pxPerTick)), rowH - 2.0f };
}

int PianoRoll::noteAtPoint (juce::Point<int> p) const
{
    for (int i = (int) notes.size() - 1; i >= 0; --i)
        if (noteRect (notes[(size_t) i]).contains (p.toFloat())) return i;
    return -1;
}

int PianoRoll::velocityHit (juce::Point<int> p) const
{
    int best = -1; float bestD = 5.0f;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const float d = std::abs (tickToX (notes[i].n.start) - (float) p.x);
        if (d < bestD || (juce::approximatelyEqual (d, bestD) && notes[i].selected)) { bestD = d; best = (int) i; }
    }
    return best;
}

int PianoRoll::snapPitch (int pitch) const
{
    pitch = juce::jlimit (0, 127, pitch);
    return snapScale ? juce::jlimit (0, 127, snapToScale (scaleAt (scaleIndex), rootPc, pitch)) : pitch;
}

// ------------------------------------------------------------------ painting
void PianoRoll::setDrumLabels (bool on)
{
    if (drumLabels == on) return;
    drumLabels = on;
    keyW = on ? 130 : 46;
    if (on)
    {
        const float visible = (float) juce::jmax (100, noteArea().getHeight());
        rowH = juce::jlimit (9.0f, 28.0f, visible / 51.0f);   // keys 34..84 cover everything the drum generator writes
        topPitch = 84.0;
    }
    clampView();
    repaint();
}

void PianoRoll::paint (juce::Graphics& g)
{
    clampView();
    g.fillAll (juce::Colour (0xff15171c));

    const auto area = noteArea();
    const auto& sc = scaleAt (scaleIndex);

    // rows
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (area);
        for (int p = 127; p >= 0; --p)
        {
            const float y = pitchToY (p);
            if (y + rowH < (float) area.getY() || y > (float) area.getBottom()) continue;
            juce::Colour c = isBlackKey (p) ? juce::Colour (0xff1b1e24) : juce::Colour (0xff20242b);
            if (inScale (sc, rootPc, p)) c = c.brighter (0.12f);
            g.setColour (c);
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth(), rowH));
            if (pcOf (p) == 0) { g.setColour (juce::Colour (0xff3a404c)); g.drawHorizontalLine ((int) (y + rowH), (float) area.getX(), (float) area.getRight()); }
        }

        // vertical grid
        const int gt = gridTicks();
        const double first = std::floor (scrollTick / gt) * gt;
        for (double t = first; t <= loopTicks() && tickToX (t) < (float) area.getRight(); t += gt)
        {
            const float x = tickToX (t);
            const long long ti = (long long) std::llround (t);
            if (ti % kBarTicks == 0)        g.setColour (juce::Colour (0xff6a7285));
            else if (ti % kPPQ == 0)        g.setColour (juce::Colour (0xff434a59));
            else if (gt * pxPerTick >= 6.0) g.setColour (juce::Colour (0xff2c313b));
            else continue;
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
        }

        // notes
        for (const auto& r : notes)
        {
            const auto rc = noteRect (r);
            if (rc.getRight() < (float) area.getX() || rc.getX() > (float) area.getRight()) continue;
            float a = routing.audible (r.track) ? 1.0f : 0.3f;
            auto col = kTrackColours[r.track].withMultipliedBrightness (0.55f + 0.45f * (float) r.n.velocity / 127.0f).withAlpha (a);
            g.setColour (col);
            g.fillRoundedRectangle (rc, 2.0f);
            g.setColour (r.selected ? juce::Colours::white : col.darker (0.5f));
            g.drawRoundedRectangle (rc, 2.0f, r.selected ? 1.6f : 1.0f);
        }

        // loop end
        const float endX = tickToX (loopTicks());
        g.setColour (juce::Colour (0x66000000));
        if (endX < (float) area.getRight())
            g.fillRect (juce::Rectangle<float> (endX, (float) area.getY(), (float) area.getRight() - endX, (float) area.getHeight()));

        if (drag == Drag::Marquee)
        {
            const auto m = juce::Rectangle<int> (marqueeStart, marqueeEnd);
            g.setColour (juce::Colour (0x33ffffff)); g.fillRect (m);
            g.setColour (juce::Colours::white);      g.drawRect (m, 1);
        }
    }

    // keys
    g.setFont (10.0f);
    for (int p = 127; p >= 0; --p)
    {
        const float y = pitchToY (p);
        if (y + rowH < (float) area.getY() || y > (float) area.getBottom()) continue;
        g.setColour (isBlackKey (p) ? juce::Colour (0xff111318) : juce::Colour (0xffd9dce3));
        g.fillRect (juce::Rectangle<float> (0.0f, y, (float) keyW - 1.0f, rowH - 1.0f));
        if (drumLabels && gmDrumName (p)[0] != '\0' && rowH >= 9.0f)
        {
            g.setColour (isBlackKey (p) ? juce::Colours::white : juce::Colours::black);
            g.drawText (juce::String (p) + " " + gmDrumName (p), 3, (int) y, keyW - 6, (int) rowH, juce::Justification::centredLeft, true);
        }
        else if (pcOf (p) == 0)
        {
            g.setColour (juce::Colours::black);
            g.drawText ("C" + juce::String (p / 12 - 1), 2, (int) y, keyW - 4, (int) rowH, juce::Justification::centredRight);
        }
    }
    g.setColour (juce::Colour (0xff15171c));
    g.fillRect (0, 0, getWidth(), rulerH);
    g.fillRect (0, area.getBottom(), keyW, getHeight() - area.getBottom());

    // ruler
    g.setColour (juce::Colour (0xffaab1c2));
    for (int b = 0; b < bars; ++b)
    {
        const float x = tickToX (b * kBarTicks);
        if (x < (float) keyW - 1.0f || x > (float) getWidth()) continue;
        g.drawText (juce::String (b + 1), (int) x + 3, 0, 40, rulerH, juce::Justification::centredLeft);
        g.drawVerticalLine ((int) x, 0.0f, (float) rulerH);
    }

    // velocity lane
    const auto va = velArea();
    g.setColour (juce::Colour (0xff101216));
    g.fillRect (va);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (va);
        const float h = (float) velH - 8.0f;
        for (const auto& r : notes)
        {
            const float x = tickToX (r.n.start);
            if (x < (float) va.getX() || x > (float) va.getRight()) continue;
            const float top = (float) va.getBottom() - 3.0f - h * (float) r.n.velocity / 127.0f;
            g.setColour (kTrackColours[r.track].withAlpha (routing.audible (r.track) ? (r.selected ? 1.0f : 0.7f) : 0.25f));
            g.drawLine (x, (float) va.getBottom() - 3.0f, x, top, r.selected ? 2.0f : 1.2f);
            g.fillEllipse (x - 2.5f, top - 2.5f, 5.0f, 5.0f);
        }
    }

    // playhead
    if (playing)
    {
        const float x = tickToX (playhead);
        if (x >= (float) keyW && x <= (float) getWidth())
        {
            g.setColour (juce::Colour (0xffffffff).withAlpha (0.85f));
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        }
    }
}

// ------------------------------------------------------------------ edit plumbing
void PianoRoll::beginEdit() { before = notes; undoArmed = true; }

void PianoRoll::endEdit (bool)
{
    if (! undoArmed) return;
    undoArmed = false;
    if (sameContent (before, notes)) return;

    undoStack.push_back (before);
    if (undoStack.size() > 50) undoStack.erase (undoStack.begin());
    redoStack.clear();
    notifyHistory();
    if (onEdited) onEdited (getPattern());
}

void PianoRoll::restore (std::vector<RNote> snapshot)
{
    notes = std::move (snapshot);
    notifyHistory();
    if (onEdited) onEdited (getPattern());
    repaint();
}

void PianoRoll::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (notes);
    auto s = std::move (undoStack.back());
    undoStack.pop_back();
    restore (std::move (s));
}

void PianoRoll::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (notes);
    auto s = std::move (redoStack.back());
    redoStack.pop_back();
    restore (std::move (s));
}

void PianoRoll::deleteSelected()
{
    beginEdit();
    notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const RNote& r) { return r.selected; }), notes.end());
    endEdit (true);
    repaint();
}

void PianoRoll::duplicateSelected (int shift)
{
    beginEdit();
    std::vector<RNote> copies;
    for (auto& r : notes)
        if (r.selected)
        {
            RNote c = r;
            c.n.start += shift;
            if (c.n.start < loopTicks()) { c.n.length = std::min (c.n.length, loopTicks() - c.n.start); copies.push_back (c); }
            r.selected = false;
        }
    notes.insert (notes.end(), copies.begin(), copies.end());
    endEdit (true);
    repaint();
}

void PianoRoll::nudgeSelected (int dT, int dP)
{
    int minStart = loopTicks(), maxEnd = 0, minP = 127, maxP = 0;
    for (auto& r : notes)
        if (r.selected)
        {
            minStart = std::min (minStart, r.n.start); maxEnd = std::max (maxEnd, r.n.start + r.n.length);
            minP = std::min (minP, r.n.pitch);         maxP = std::max (maxP, r.n.pitch);
        }
    if (maxEnd == 0) return;
    dT = juce::jlimit (-minStart, loopTicks() - maxEnd, dT);
    dP = juce::jlimit (-minP, 127 - maxP, dP);
    beginEdit();
    for (auto& r : notes)
        if (r.selected) { r.n.start += dT; r.n.pitch += dP; }
    endEdit (true);
    repaint();
}

void PianoRoll::snapSelectedToScale()
{
    beginEdit();
    for (auto& r : notes)
        if (r.selected) r.n.pitch = juce::jlimit (0, 127, snapToScale (scaleAt (scaleIndex), rootPc, r.n.pitch));
    endEdit (true);
    repaint();
}

void PianoRoll::selectWhere (const std::function<bool (const RNote&)>& pred)
{
    for (auto& r : notes) r.selected = pred (r);
    repaint();
}

void PianoRoll::copySelected()
{
    clipboard.clear();
    int minStart = loopTicks();
    for (const auto& r : notes) if (r.selected) minStart = std::min (minStart, r.n.start);
    for (auto r : notes)
        if (r.selected) { r.n.start -= minStart; r.selected = false; clipboard.push_back (r); }
}

void PianoRoll::paste()
{
    if (clipboard.empty()) return;
    beginEdit();
    for (auto& r : notes) r.selected = false;
    for (auto c : clipboard)
    {
        c.n.start += pasteTick;
        if (c.n.start >= loopTicks()) continue;
        c.n.length = std::min (c.n.length, loopTicks() - c.n.start);
        c.selected = true;
        notes.push_back (c);
    }
    endEdit (true);
    repaint();
}

void PianoRoll::showContextMenu (juce::Point<int> where)
{
    const bool any = std::any_of (notes.begin(), notes.end(), [] (const RNote& r) { return r.selected; });
    const int ref  = noteAtPoint (where);

    juce::PopupMenu m;
    m.addItem (1, "Delete", any);
    m.addItem (2, "Duplicate", any);
    m.addItem (3, "Snap to Scale", any);
    m.addSeparator();
    m.addItem (4, "Select Same Pitch", ref >= 0);
    m.addItem (5, "Select Same Track", ref >= 0);
    m.addItem (6, "Select All");

    const int refPitch = ref >= 0 ? notes[(size_t) ref].n.pitch : -1;
    const int refTrack = ref >= 0 ? notes[(size_t) ref].track : -1;

    m.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<PianoRoll> (this), refPitch, refTrack] (int id)
    {
        if (safe == nullptr) return;
        switch (id)
        {
            case 1: safe->deleteSelected(); break;
            case 2: safe->duplicateSelected (safe->gridTicks()); break;
            case 3: safe->snapSelectedToScale(); break;
            case 4: safe->selectWhere ([refPitch] (const RNote& r) { return r.n.pitch == refPitch; }); break;
            case 5: safe->selectWhere ([refTrack] (const RNote& r) { return r.track == refTrack; }); break;
            case 6: safe->selectWhere ([] (const RNote&) { return true; }); break;
            default: break;
        }
    });
}

// ------------------------------------------------------------------ mouse
void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto pos = e.getPosition();
    drag = Drag::None;

    if (velArea().contains (pos))
    {
        const int idx = velocityHit (pos);
        if (idx < 0) return;
        if (! notes[(size_t) idx].selected && ! e.mods.isShiftDown())
        {
            for (auto& r : notes) r.selected = false;
            notes[(size_t) idx].selected = true;
        }
        beginEdit();
        dragOrigin = notes;
        velTarget = idx;
        drag = Drag::Velocity;
        mouseDrag (e);
        return;
    }

    if (! noteArea().contains (pos)) return;

    pasteTick = (int) (std::floor (xToTick ((float) pos.x) / gridTicks()) * gridTicks());
    const int idx = noteAtPoint (pos);

    if (e.mods.isPopupMenu())
    {
        if (idx >= 0 && ! notes[(size_t) idx].selected)
        {
            for (auto& r : notes) r.selected = false;
            notes[(size_t) idx].selected = true;
        }
        repaint();
        showContextMenu (pos);
        return;
    }

    if (idx >= 0)
    {
        auto& hit = notes[(size_t) idx];
        if (e.mods.isShiftDown()) hit.selected = ! hit.selected;
        else if (! hit.selected) { for (auto& r : notes) r.selected = false; hit.selected = true; }

        beginEdit();
        dragOrigin = notes;
        anchorTick  = xToTick ((float) pos.x);
        anchorPitch = yToPitch ((float) pos.y);
        const auto rc = noteRect (hit);
        drag = ((float) pos.x > rc.getRight() - 6.0f && rc.getWidth() > 12.0f) ? Drag::Resize : Drag::Move;
    }
    else if (drawMode)
    {
        for (auto& r : notes) r.selected = false;
        beginEdit();
        RNote r;
        r.track = newNoteTrack;
        r.n.start = juce::jlimit (0, loopTicks() - gridTicks(), pasteTick);
        r.n.length = gridTicks();
        r.n.pitch = snapPitch (yToPitch ((float) pos.y));
        r.n.velocity = 100;
        r.selected = true;
        notes.push_back (r);
        endEdit (true);
    }
    else
    {
        if (! e.mods.isShiftDown()) for (auto& r : notes) r.selected = false;
        dragOrigin = notes;
        marqueeStart = marqueeEnd = pos;
        drag = Drag::Marquee;
    }
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    if (drag == Drag::Marquee)
    {
        marqueeEnd = pos;
        const juce::Rectangle<float> m (juce::Rectangle<int> (marqueeStart, marqueeEnd).toFloat());
        for (size_t i = 0; i < notes.size(); ++i)
            notes[i].selected = noteRect (notes[i]).intersects (m) || (e.mods.isShiftDown() && dragOrigin[i].selected);
        repaint();
        return;
    }

    if (drag == Drag::Velocity)
    {
        const auto va = velArea();
        const float h = (float) velH - 8.0f;
        const int target = juce::jlimit (1, 127, (int) std::lround (((float) va.getBottom() - 3.0f - (float) pos.y) / h * 127.0f));
        const int delta = target - dragOrigin[(size_t) velTarget].n.velocity;
        for (size_t i = 0; i < notes.size(); ++i)
            if ((int) i == velTarget || (notes[i].selected && notes[(size_t) velTarget].selected))
                notes[i].n.velocity = juce::jlimit (1, 127, dragOrigin[i].n.velocity + delta);
        repaint();
        return;
    }

    if (drag != Drag::Move && drag != Drag::Resize) return;

    const int g = gridTicks();
    int dT = (int) std::lround ((xToTick ((float) pos.x) - anchorTick) / g) * g;

    int minStart = loopTicks(), maxEnd = 0, minP = 127, maxP = 0;
    for (const auto& r : dragOrigin)
        if (r.selected)
        {
            minStart = std::min (minStart, r.n.start); maxEnd = std::max (maxEnd, r.n.start + r.n.length);
            minP = std::min (minP, r.n.pitch);         maxP = std::max (maxP, r.n.pitch);
        }

    if (drag == Drag::Move)
    {
        dT = juce::jlimit (-minStart, std::max (-minStart, loopTicks() - maxEnd), dT);
        const int dP = juce::jlimit (-minP, 127 - maxP, yToPitch ((float) pos.y) - anchorPitch);
        for (size_t i = 0; i < notes.size(); ++i)
            if (dragOrigin[i].selected)
            {
                notes[i].n.start = dragOrigin[i].n.start + dT;
                notes[i].n.pitch = snapPitch (dragOrigin[i].n.pitch + dP);
            }
    }
    else
    {
        for (size_t i = 0; i < notes.size(); ++i)
            if (dragOrigin[i].selected)
                notes[i].n.length = juce::jlimit (g, std::max (g, loopTicks() - dragOrigin[i].n.start), dragOrigin[i].n.length + dT);
    }
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    const auto d = drag;
    drag = Drag::None;
    if (d == Drag::Move || d == Drag::Resize || d == Drag::Velocity) endEdit (true);
    else undoArmed = false;
    repaint();
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (! noteArea().contains (pos) || e.mods.isPopupMenu()) return;

    const int idx = noteAtPoint (pos);
    if (idx >= 0)
    {
        for (auto& r : notes) r.selected = false;
        notes[(size_t) idx].selected = true;
        duplicateSelected (gridTicks());
    }
    else if (! drawMode)
    {
        for (auto& r : notes) r.selected = false;
        beginEdit();
        RNote r;
        r.track = newNoteTrack;
        r.n.start = juce::jlimit (0, loopTicks() - gridTicks(), pasteTick);
        r.n.length = gridTicks();
        r.n.pitch = snapPitch (yToPitch ((float) pos.y));
        r.n.velocity = 100;
        r.selected = true;
        notes.push_back (r);
        endEdit (true);
        repaint();
    }
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float dy = w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    if (e.mods.isCommandDown())          // zoom time around the cursor
    {
        const double anchor = xToTick (e.position.x);
        pxPerTick *= std::pow (1.5, (double) dy * 3.0);
        clampView();
        scrollTick += anchor - xToTick (e.position.x);
    }
    else if (e.mods.isShiftDown())
        scrollTick -= (double) dy * 600.0 / std::max (0.01, pxPerTick * 20.0) * 20.0;
    else if (e.mods.isAltDown())
        rowH = juce::jlimit (6.0f, 28.0f, rowH * (1.0f + dy * 0.5f));
    else
        topPitch += (double) dy * 8.0;
    clampView();
    repaint();
}

// ------------------------------------------------------------------ keyboard
bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    const bool cmd = key.getModifiers().isCommandDown(), shift = key.getModifiers().isShiftDown();
    const int code = key.getKeyCode();

    if (cmd && code == 'Z') { if (shift) redo(); else undo(); return true; }
    if (cmd && code == 'Y') { redo(); return true; }
    if (cmd && code == 'A') { selectWhere ([] (const RNote&) { return true; }); return true; }
    if (cmd && code == 'C') { copySelected(); return true; }
    if (cmd && code == 'V') { paste(); return true; }
    if (cmd && code == 'D')
    {
        int lo = loopTicks(), hi = 0;
        for (auto& r : notes) if (r.selected) { lo = std::min (lo, r.n.start); hi = std::max (hi, r.n.start + r.n.length); }
        if (hi > lo) duplicateSelected (((hi - lo + gridTicks() - 1) / gridTicks()) * gridTicks());
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }

    const int step = shift ? kBarTicks : gridTicks();
    if (code == juce::KeyPress::leftKey)  { nudgeSelected (-step, 0); return true; }
    if (code == juce::KeyPress::rightKey) { nudgeSelected (step, 0);  return true; }
    if (code == juce::KeyPress::upKey)    { nudgeSelected (0, shift ? 12 : 1);  return true; }
    if (code == juce::KeyPress::downKey)  { nudgeSelected (0, shift ? -12 : -1); return true; }
    return false;
}

} // namespace icc
