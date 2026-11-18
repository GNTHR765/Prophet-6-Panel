#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "MoogLook.h"

namespace ui
{
/** A number you drag, Orbit style: the value large, its name underneath. Drag up or down (Shift: fine), scroll, double-click
    to reset, and the text is whatever the cell's formatter says (a note name, a clock division, a percentage). */
struct ValueCell : public juce::Component
{
    explicit ValueCell (juce::String name) : title (std::move (name)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onPopup) onPopup(); return; } dragStart = e.y; dragValue = value; dragAcc = 0.f; if (onSelect) onSelect(); }
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { set (def, true); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { juce::ignoreUnused (e); wheelAcc += w.deltaY; while (wheelAcc >= 0.15f) { wheelAcc -= 0.15f; nudge (1); } while (wheelAcc <= -0.15f) { wheelAcc += 0.15f; nudge (-1); } }
    void set (int v, bool notify) { v = juce::jlimit (min, max, v); if (v == value) return; value = v; repaint(); if (notify && onChange) onChange (value); }
    void nudge (int steps) { set (stepFn ? stepFn (value, steps) : value + steps, true); }
    juce::String title; int value = 0, min = 0, max = 100, def = 0;
    juce::Colour colour { 0xffd9a441 };
    std::function<juce::String (int)> text;          // how the value reads (default: the number)
    std::function<int (int, int)> stepFn;            // a custom step (value, +-steps) -> value, e.g. through a scale
    std::function<void (int)> onChange;
    std::function<void()> onSelect;                 // a click (before any drag): the page shows this control's lane
    std::function<void()> onPopup;                  // a right-click: the cell's own menu (the PITCH cell: where the note comes from)
    bool marked = false;                            // a dot: this control has a lane with values
    float pixelsPerStep = 5.f;
    int dragStart = 0, dragValue = 0; float dragAcc = 0.f, wheelAcc = 0.f; bool highlight = false;
};

struct EuclidPage;
/** One track's circle: its steps around a ring, hits filled, the running step lit. Click a step to force it on or off (a third click
    hands it back to the maths), Shift-click to accent it, right-click the ring to clear every hand edit. */
struct Ring : public juce::Component
{
    Ring (EuclidPage& p, int i) : page (p), index (i) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { const int s = stepAt (e.position); if (s != hover) { hover = s; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int stepAt (juce::Point<float>) const;
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    float radius() const { return juce::jmin (getWidth(), getHeight()) * 0.5f - 12.f; }
    EuclidPage& page; int index; int hover = -1; float flash = 0.f;
};

/** A track's lane editor: the selected lane's steps as bars around a centre line, drawn with the mouse (drag across steps),
    right-click to clear. The running step is lit. */
struct LaneStrip : public juce::Component
{
    LaneStrip (EuclidPage& p, int i) : page (p), index (i) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override { drawAt (e); }
    void mouseMove (const juce::MouseEvent& e) override { hoverX = e.x; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hoverX = -1; repaint(); }
    juce::Rectangle<float> bars() const { return getLocalBounds().toFloat().reduced (4.f, 4.f); }
    int stepAt (float x) const; float valueAt (float y) const;
    void drawAt (const juce::MouseEvent&);
    EuclidPage& page; int index; int lastStep = -1; float lastVal = 0.f; int hoverX = -1;
};

/** The scale picker: the twelve roots across the top, then Orbit's grid - the seven modes as rows, the key and its two
    circle-of-fifths neighbours as columns - and the other scales in a row underneath. One click sets key and scale. */
struct ScaleGrid : public juce::Component
{
    explicit ScaleGrid (EuclidPage& p) : page (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hoverAt = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hoverAt = { -1, -1 }; repaint(); }
    struct Cell { juce::Rectangle<int> r; int scale, root; juce::String text; };
    std::vector<Cell> cells() const;
    EuclidPage& page; juce::Point<int> hoverAt { -1, -1 };
};

/** The EUCLID page: four Euclidean tracks playing the synth in time with Logic (or on their own). */
struct EuclidPage : public juce::Component, private juce::Timer
{
    explicit EuclidPage (P6PanelProcessor& p);
    void resized() override; void paint (juce::Graphics&) override;
    void visibilityChanged() override { if (isVisible()) { refresh(); startTimerHz (30); } else stopTimer(); }
    void refresh();                                   // the cells from the patch
    Euclid::Patch& patch() { return proc.euclid.patch; }
    P6PanelProcessor& proc;
    std::function<void()> onBack;
    juce::TextButton bBack { "PANEL" }, bOn { "ON" }, bRun { "RUN" }, bScale, bScaleOn { "SCALE" }, bClear { "CLEAR" }, bReset { "RESET" }, bInit { "INIT" }, bArp { "ARP" }, bChordMute { "MUTE CHORD" }, bLatch { "LATCH" };
    bool muteBefore = false, latchBefore = true, offSaved = false;   // MUTE CHORD / LATCH as they were when ON went off
    void arpMenu(); void pitchMenu (int track);
    juce::Label lClock, lHint;
    // ---- the pattern library: Prophet 6 Patches/Euclid/<Category>/<name>.p6euclid - the whole page (tracks, lanes, key) as one preset
    juce::TextButton bPrev { "<" }, bNext { ">" }, bPatterns { "PATTERNS" }; juce::Label lPattern;
    static juce::File patternDir() { auto d = P6PanelProcessor::libraryDir().getChildFile ("Euclid"); d.createDirectory(); return d; }
    juce::Array<juce::File> patternFiles() const;     // every pattern, by category then name
    juce::File currentPattern;
    void patternMenu(); void savePatternAs(); void loadPattern (const juce::File&); void stepPattern (int delta);
    static constexpr int kCells = 11;
    static const char* cellNames[kCells];
    struct Row
    {
        Row (EuclidPage& p, int i) : ring (p, i), strip (p, i) {}
        Ring ring; std::vector<std::unique_ptr<ValueCell>> cells; juce::TextButton bMute { "M" }, bSolo { "S" }; juce::Label lNum;
        LaneStrip strip; juce::TextButton bLane, bDir { "FWD" }, bLock { "LOCK" }; ValueCell cLen { "LANE STEPS" }; int selected = Euclid::laneVelocity;   // the lane shown
    };
    static int laneOfCell (int cell) { static const int m[kCells] = { -1, Euclid::laneBeats, Euclid::laneRotate, Euclid::laneChance, Euclid::laneVelocity, -1, Euclid::laneLength, -1, Euclid::lanePitch, Euclid::laneChord, Euclid::laneRatchet }; return m[juce::jlimit (0, kCells - 1, cell)]; }
    juce::String laneTitle (int target) const;
    void selectLane (int track, int target);
    Euclid::Lane* laneFor (int track);                // the shown lane, made if a slot is free (nullptr and a hint when all are taken)
    void warn (const juce::String&); juce::String hintText; juce::int64 warnUntil = 0;   // a message in the hint's place for a few seconds
    void laneMenu (int track);
    void refreshLaneRow (int track);
    std::vector<std::unique_ptr<Row>> rows;
    ScaleGrid scaleGrid { *this }; bool scaleOpen = false;
    void setScale (int scale, int root);
    void scaleChanged();                              // re-snap the pitches, relabel
    juce::Colour trackColour (int i) const { return MoogLook::modColour[i % 4]; }
    bool updating = false;
private:
    void timerCallback() override;
    void applyCell (int track, int cell, int value);
    juce::String scaleText();
};
} // namespace ui
