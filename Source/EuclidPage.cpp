#include "EuclidPage.h"

using namespace juce;
namespace ui
{
// ---------------------------------------------------------------- ValueCell
void ValueCell::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.f);
    g.setColour (MoogLook::panelLight); g.fillRoundedRectangle (r, 5.f);
    g.setColour (highlight ? colour : MoogLook::rule); g.drawRoundedRectangle (r, 5.f, highlight ? 1.6f : 1.f);   // the cell whose lane the strip shows wears its colour
    const String t = text ? text (value) : String (value);
    g.setColour (colour); g.setFont (MoogLook::silk (t.length() > 5 ? 15.f : 20.f, true));
    g.drawText (t, r.withTrimmedBottom (16.f), Justification::centred);
    g.setColour (MoogLook::inkDim); g.setFont (MoogLook::silk (9.5f));
    g.drawText (title, r.removeFromBottom (16.f), Justification::centred);
    if (marked) { g.setColour (colour); g.fillEllipse (getWidth() - 11.f, 6.f, 5.f, 5.f); }
}
void ValueCell::mouseDrag (const MouseEvent& e)
{
    const float px = e.mods.isShiftDown() ? pixelsPerStep * 5.f : pixelsPerStep;
    const int steps = roundToInt ((float) (dragStart - e.y) / px);
    if (stepFn) { int v = dragValue; const int n = std::abs (steps); for (int i = 0; i < n; ++i) v = stepFn (v, steps > 0 ? 1 : -1); set (v, true); }
    else set (dragValue + steps, true);
}

// ---------------------------------------------------------------- Ring
int Ring::stepAt (Point<float> p) const
{
    auto& t = page.patch().tr[(size_t) index];
    const auto c = centre(); const float d = p.getDistanceFrom (c);
    if (d < radius() - 22.f || d > radius() + 12.f) return -1;
    float a = std::atan2 (p.x - c.x, c.y - p.y); if (a < 0) a += MathConstants<float>::twoPi;   // 0 at the top, clockwise
    return (int) std::floor (a / MathConstants<float>::twoPi * (float) t.steps + 0.5f) % jmax (1, t.steps);
}
void Ring::paint (Graphics& g)
{
    auto& t = page.patch().tr[(size_t) index]; const auto col = page.trackColour (index);
    const auto c = centre(); const float R = radius();
    const bool audible = page.patch().audible (index);
    g.setColour (MoogLook::panelLight); g.fillEllipse (c.x - R - 10.f, c.y - R - 10.f, 2.f * R + 20.f, 2.f * R + 20.f);
    g.setColour (MoogLook::rule); g.drawEllipse (c.x - R, c.y - R, 2.f * R, 2.f * R, 1.f);
    const int cur = page.proc.euclid.uiStep[(size_t) index].load();
    const float dotR = jlimit (3.5f, 7.f, R * 3.1f / (float) jmax (8, t.steps));
    for (int i = 0; i < t.steps; ++i)
    {
        const float a = (float) i / (float) t.steps * MathConstants<float>::twoPi;
        const Point<float> p (c.x + R * std::sin (a), c.y - R * std::cos (a));
        const bool hit = t.hit (i), forced = t.over[(size_t) i] != 0, acc = t.accent[(size_t) i];
        const float rr = acc && hit ? dotR * 1.45f : dotR;
        if (hit) { g.setColour (audible ? col : col.withAlpha (0.35f)); g.fillEllipse (p.x - rr, p.y - rr, 2.f * rr, 2.f * rr); }
        else { g.setColour (MoogLook::inkDim.withAlpha (0.5f)); g.drawEllipse (p.x - dotR * 0.6f, p.y - dotR * 0.6f, dotR * 1.2f, dotR * 1.2f, 1.f); }
        if (t.glide[(size_t) i] && page.proc.euclid.mono)
        {   // a glide step: a short arc along the ring from the step before, sliding into this one
            const float a0 = a - MathConstants<float>::twoPi / (float) t.steps * 0.8f; Path arc;
            arc.addCentredArc (c.x, c.y, R + dotR + 6.f, R + dotR + 6.f, 0.f, a0, a, true);
            g.setColour (hit ? col.brighter (0.4f) : MoogLook::inkDim); g.strokePath (arc, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (forced) { g.setColour (MoogLook::cream); g.drawEllipse (p.x - rr - 2.5f, p.y - rr - 2.5f, 2.f * rr + 5.f, 2.f * rr + 5.f, 1.2f); }   // a hand-set step wears a ring
        if (i == hover) { g.setColour (MoogLook::cream.withAlpha (0.5f)); g.drawEllipse (p.x - rr - 4.f, p.y - rr - 4.f, 2.f * rr + 8.f, 2.f * rr + 8.f, 1.f); }
        if (i == cur) { g.setColour (MoogLook::cream); g.fillEllipse (p.x - 2.f, p.y - 2.f, 4.f, 4.f); }
    }
    // the hand: from the centre to the current step
    if (cur >= 0 && cur < t.steps)
    {
        const float a = (float) cur / (float) t.steps * MathConstants<float>::twoPi;
        g.setColour (MoogLook::cream.withAlpha (0.35f)); g.drawLine (c.x, c.y, c.x + (R - dotR - 4.f) * std::sin (a), c.y - (R - dotR - 4.f) * std::cos (a), 1.f);
    }
    // the flash on a hit, fading
    if (flash > 0.01f) { g.setColour (col.withAlpha (flash * 0.45f)); g.fillEllipse (c.x - R * 0.42f, c.y - R * 0.42f, R * 0.84f, R * 0.84f); }
    g.setColour (audible ? col : MoogLook::inkDim); g.setFont (MoogLook::silk (13.f, true));
    g.drawText (String (t.hits()) + " / " + String (t.steps), Rectangle<float> (c.x - 40.f, c.y - 10.f, 80.f, 20.f), Justification::centred);
}
void Ring::mouseDown (const MouseEvent& e)
{
    auto& t = page.patch().tr[(size_t) index];
    if (e.mods.isPopupMenu()) { t.clearOverrides(); repaint(); return; }
    const int s = stepAt (e.position); if (s < 0) return;
    if (e.mods.isShiftDown()) { t.accent[(size_t) s] = ! t.accent[(size_t) s]; repaint(); return; }
    if (e.mods.isAltDown() && page.proc.euclid.mono) { t.glide[(size_t) s] = ! t.glide[(size_t) s]; repaint(); return; }   // option-click: glide into this step
    // click: a hit becomes a forced rest, a rest a forced hit; clicking a forced step hands it back to the maths
    const bool maths = Euclid::euclidHit (t.steps, t.beats, t.rotate, s);
    if (t.over[(size_t) s] != 0) t.over[(size_t) s] = 0;
    else t.over[(size_t) s] = (int8_t) (maths ? -1 : 1);
    repaint();
}

// ---------------------------------------------------------------- LaneStrip
int LaneStrip::stepAt (float x) const
{
    auto& t = page.patch().tr[(size_t) index]; const auto* l = t.lane (page.rows[(size_t) index]->selected);
    const int L = l != nullptr ? l->len (t.steps) : juce::jlimit (1, Euclid::kLaneSteps, t.steps);
    return juce::jlimit (0, L - 1, (int) std::floor ((x - bars().getX()) / bars().getWidth() * (float) L));
}
float LaneStrip::valueAt (float y) const
{
    const auto b = bars(); float v = 1.f - 2.f * (y - b.getY()) / b.getHeight(); v = juce::jlimit (-1.f, 1.f, v);
    return std::abs (v) < 0.04f ? 0.f : Euclid::laneSnap (v);   // a little dead zone round the centre line, so "nothing" is easy to draw; in hundredths, as a saved lane holds it
}
void LaneStrip::mouseDown (const MouseEvent& e)
{
    auto& t = page.patch().tr[(size_t) index];
    if (e.mods.isPopupMenu()) { if (auto* l = t.lane (page.rows[(size_t) index]->selected)) l->v.fill (0.f); page.refreshLaneRow (index); return; }
    lastStep = -1; drawAt (e);
}
void LaneStrip::drawAt (const MouseEvent& e)
{
    auto& t = page.patch().tr[(size_t) index]; const int target = page.rows[(size_t) index]->selected; if (target < 0) return;
    auto* l = page.laneFor (index); if (l == nullptr) return;   // no free slot: nothing is replaced, the page says so
    const int s = stepAt (e.position.x); const float v = valueAt (e.position.y);
    if (lastStep < 0 || lastStep == s) l->v[(size_t) s] = v;
    else
    {   // a fast drag skipped steps: fill them in a straight line from the last point
        const int a = juce::jmin (lastStep, s), b = juce::jmax (lastStep, s); const float va = lastStep < s ? lastVal : v, vb = lastStep < s ? v : lastVal;
        for (int i = a; i <= b; ++i) l->v[(size_t) i] = Euclid::laneSnap (va + (vb - va) * (float) (i - a) / (float) juce::jmax (1, b - a));
    }
    lastStep = s; lastVal = v;
    page.refreshLaneRow (index);
}
void LaneStrip::paint (Graphics& g)
{
    auto& t = page.patch().tr[(size_t) index]; auto& row = *page.rows[(size_t) index]; const auto col = page.trackColour (index);
    const auto* l = t.lane (row.selected);
    const auto b = bars(); const int L = l != nullptr ? l->len (t.steps) : juce::jlimit (1, Euclid::kLaneSteps, t.steps);
    g.setColour (MoogLook::panelLight.darker (0.25f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.f);
    g.setColour (MoogLook::rule); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.f, 1.f);
    const float w = b.getWidth() / (float) L, mid = b.getCentreY();
    // the running position (random lanes have none to show)
    int cur = -1;
    if (l != nullptr && page.proc.euclid.uiStep[(size_t) index].load() >= 0) { const int slot = (int) (l - &t.lanes[0]); if (slot >= 0 && slot < Euclid::kMaxLanes) cur = page.proc.euclid.uiLanePos[(size_t) index][(size_t) slot].load(); }
    for (int i = 0; i < L; ++i)
    {
        const float x = b.getX() + i * w; const float v = l != nullptr ? l->v[(size_t) i] : 0.f;
        if (i == cur) { g.setColour (MoogLook::cream.withAlpha (0.12f)); g.fillRect (x, b.getY(), w, b.getHeight()); }
        if (v != 0.f) { g.setColour (col.withAlpha (i == cur ? 1.f : 0.8f)); const float h = std::abs (v) * b.getHeight() * 0.5f; g.fillRect (x + 1.f, v > 0 ? mid - h : mid, juce::jmax (1.f, w - 2.f), h); }
        else { g.setColour (MoogLook::inkDim.withAlpha (0.35f)); g.fillRect (x + 1.f, mid - 0.5f, juce::jmax (1.f, w - 2.f), 1.f); }
        if (t.steps > 0 && (i % 4) == 0) { g.setColour (MoogLook::rule); g.drawVerticalLine ((int) x, b.getY(), b.getBottom()); }
    }
    g.setColour (MoogLook::inkDim); g.drawLine (b.getX(), mid, b.getRight(), mid, 0.6f);
    // the value under the mouse, in the control's units
    if (hoverX >= 0 && l != nullptr)
    {
        const int s = stepAt ((float) hoverX); const float v = l->v[(size_t) s]; String txt;
        if (l->target >= Euclid::kSynth) txt = (v >= 0 ? "+" : "") + String (roundToInt (v * 100)) + "%";
        else if (l->target == Euclid::lanePitch && page.patch().scaleOn) { const int d = roundToInt (v * 12.f); txt = (d >= 0 ? "+" : "") + String (d) + (std::abs (d) == 1 ? " degree" : " degrees"); }
        else { const int o = roundToInt (v * (float) Euclid::laneRange (l->target)); txt = (o >= 0 ? "+" : "") + String (o); }
        g.setColour (MoogLook::cream); g.setFont (MoogLook::silk (10.5f, true)); g.drawText ("step " + String (s + 1) + ": " + txt, getLocalBounds().reduced (8, 2), Justification::topRight);
    }
    if (l == nullptr) { g.setColour (MoogLook::inkDim); g.setFont (MoogLook::silk (10.5f)); g.drawText ("draw here to make a " + page.laneTitle (row.selected) + " lane", getLocalBounds(), Justification::centred); }
}

// ---------------------------------------------------------------- ScaleGrid
std::vector<ScaleGrid::Cell> ScaleGrid::cells() const
{
    std::vector<Cell> out; auto r = getLocalBounds().reduced (12);
    const int root = page.patch().root;
    // roots
    { auto row = r.removeFromTop (30); const int w = row.getWidth() / 12; for (int i = 0; i < 12; ++i) out.push_back ({ row.removeFromLeft (w).reduced (2), -1, i, Euclid::noteName (i) }); }
    r.removeFromTop (8);
    // the seven modes x three keys (fifth down, the key, fifth up)
    const int modes[7] = { 1, 2, 3, 4, 5, 6, 7 }; const int keys[3] = { (root + 5) % 12, root, (root + 7) % 12 };
    auto grid = r.removeFromTop (7 * 30); const int cw = grid.getWidth() / 3;
    for (int m = 0; m < 7; ++m) { auto row = grid.removeFromTop (30); for (int k = 0; k < 3; ++k) out.push_back ({ row.removeFromLeft (cw).reduced (2), modes[m], keys[k], String (Euclid::noteName (keys[k])) + " " + Euclid::scales()[(size_t) modes[m]].name }); }
    r.removeFromTop (8);
    // the others, in the key
    { auto row = r.removeFromTop (30); const int n = (int) Euclid::scales().size() - 8; const int w = row.getWidth() / n; for (int i = 8; i < (int) Euclid::scales().size(); ++i) out.push_back ({ row.removeFromLeft (w).reduced (2), i, root, Euclid::scales()[(size_t) i].name }); }
    return out;
}
void ScaleGrid::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (MoogLook::panel.withAlpha (0.97f)); g.fillRoundedRectangle (r, 8.f); g.setColour (MoogLook::rule); g.drawRoundedRectangle (r.reduced (0.5f), 8.f, 1.f);
    const int scale = page.patch().scale, root = page.patch().root;
    for (auto& c : cells())
    {
        const bool rootCell = c.scale < 0; const bool sel = rootCell ? c.root == root : (c.scale == scale && c.root == root); const bool hov = c.r.contains (hoverAt);
        g.setColour (sel ? MoogLook::accent.withAlpha (0.85f) : hov ? MoogLook::panelLight.brighter (0.3f) : MoogLook::panelLight); g.fillRoundedRectangle (c.r.toFloat(), 4.f);
        g.setColour (sel ? Colour (0xff202020) : rootCell ? MoogLook::cream : MoogLook::ink); g.setFont (MoogLook::silk (rootCell ? 12.f : 10.5f, sel || rootCell));
        g.drawText (c.text, c.r, Justification::centred);
    }
    g.setColour (MoogLook::inkDim); g.setFont (MoogLook::silk (9.5f));
    g.drawText ("ROOT", getLocalBounds().reduced (12).removeFromTop (30).translated (0, -11).withHeight (10), Justification::centredLeft);
}
void ScaleGrid::mouseDown (const MouseEvent& e)
{
    for (auto& c : cells()) if (c.r.contains (e.getPosition()))
    {
        if (c.scale < 0) page.setScale (page.patch().scale, c.root); else { page.setScale (c.scale, c.root); page.scaleOpen = false; setVisible (false); }
        repaint(); return;
    }
}

// ---------------------------------------------------------------- EuclidPage
const char* EuclidPage::cellNames[EuclidPage::kCells] = { "STEPS", "BEATS", "ROTATE", "CHANCE", "VELOCITY", "SWING", "LENGTH", "CLOCK", "PITCH", "CHORD", "RATCHET" };

EuclidPage::EuclidPage (P6PanelProcessor& p) : proc (p)
{
    addAndMakeVisible (bBack); bBack.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (bOn); bOn.setClickingTogglesState (true); bOn.setColour (TextButton::buttonOnColourId, MoogLook::accent);
    bOn.setTooltip ("The master switch: off, the page is silent (Logic keeps running) - and MUTE CHORD and LATCH go off with it, so the keys play the synth again; ON brings them back as they were");
    bOn.onClick = [this]
    {   auto& pt = patch(); pt.on = bOn.getToggleState();
        if (! pt.on)
        {   proc.euclidPanic = true;
            muteBefore = pt.chordMute; latchBefore = pt.latch; offSaved = true;   // remembered for ON
            pt.chordMute = false; pt.latch = false; proc.link.muteKeys = false; }
        else if (offSaved)
        {   pt.chordMute = muteBefore; pt.latch = latchBefore; proc.link.muteKeys = pt.chordMute; if (pt.chordMute) proc.link.allKeysOff(); offSaved = false; }
        bChordMute.setToggleState (pt.chordMute, dontSendNotification); bLatch.setToggleState (pt.latch, dontSendNotification);
    };
    addAndMakeVisible (bRun); bRun.setClickingTogglesState (true); bRun.setColour (TextButton::buttonOnColourId, MoogLook::modColour[3]);
    bRun.setTooltip ("Play on the page's own clock while Logic stands still (or in the standalone app). When Logic plays, the tracks follow Logic's beat instead."); bRun.onClick = [this] { proc.euclidRun = bRun.getToggleState(); };
    addAndMakeVisible (lClock); lClock.setFont (MoogLook::silk (10.5f)); lClock.setColour (Label::textColourId, MoogLook::inkDim);
    addAndMakeVisible (bScale); bScale.setTooltip ("The key: the root and the scale every pitch snaps to (the seven modes with their neighbours on the circle of fifths, and a few more)");
    bScale.onClick = [this] { scaleOpen = ! scaleOpen; scaleGrid.setVisible (scaleOpen); if (scaleOpen) scaleGrid.toFront (false); };
    addAndMakeVisible (bScaleOn); bScaleOn.setClickingTogglesState (true); bScaleOn.setColour (TextButton::buttonOnColourId, MoogLook::accent);
    bScaleOn.setTooltip ("On: PITCH counts scale notes and CHORD stacks scale thirds, so everything stays in key. Off: chromatic, every semitone"); bScaleOn.onClick = [this] { patch().scaleOn = bScaleOn.getToggleState(); scaleChanged(); };
    addAndMakeVisible (bClear); bClear.setTooltip ("Forget every hand-set step and accent on all four tracks"); bClear.onClick = [this] { for (auto& t : patch().tr) t.clearOverrides(); repaint(); };
    addAndMakeVisible (bReset); bReset.setTooltip ("Back to the starting patch: four tracks in a rotated canon"); bReset.onClick = [this] { proc.euclidPanic = true; patch().setDefault(); refresh(); };
    addAndMakeVisible (bInit); bInit.setTooltip ("An empty page to build from: four silent tracks (no beats yet, an octave apart), default settings, no hand-set steps, accents or lanes. The key, ON, MUTE CHORD and LATCH stay as they are");
    bInit.onClick = [this] { proc.euclidPanic = true; patch().setInit(); currentPattern = File(); lPattern.setText ("Init", dontSendNotification); lPattern.setTooltip ({}); refresh(); };
    addAndMakeVisible (bArp); bArp.setTooltip ("Set the four tracks up as an arpeggio: track 1's rhythm on every track, each one a few steps later (a canon), and optionally the pitches from the chord you play"); bArp.onClick = [this] { arpMenu(); };
    addAndMakeVisible (bChordMute); bChordMute.setClickingTogglesState (true); bChordMute.setColour (TextButton::buttonOnColourId, MoogLook::modColour[4]);
    bChordMute.setTooltip ("The chord played into the panel (Logic's region, or the keys) only steers the tracks and is not passed on to the synth - so you hear the pattern, not the chord");
    bChordMute.onClick = [this] { patch().chordMute = bChordMute.getToggleState(); proc.link.muteKeys = patch().chordMute; if (patch().chordMute) proc.link.allKeysOff(); };
    addAndMakeVisible (bLatch); bLatch.setClickingTogglesState (true); bLatch.setColour (TextButton::buttonOnColourId, MoogLook::accent);
    bLatch.setTooltip ("On: the last chord keeps playing after you let go (handy across small gaps in Logic's region). Off: the tracks that take their notes from the chord rest until the next chord is played");
    bLatch.onClick = [this] { patch().latch = bLatch.getToggleState(); };
    addAndMakeVisible (lHint); lHint.setFont (MoogLook::silk (10.5f)); lHint.setColour (Label::textColourId, MoogLook::inkDim);
    lHint.setText (String ("ring: click = force a step on / off (again: back to the maths)\n   ") + (proc.euclid.mono ? "shift = accent, option = glide, right-click = clear all" : "shift-click = accent, right-click = clear all") + "\ncells: drag (shift = fine), double-click = default, click = lane\nlane strip: draw with the mouse, right-click = clear", dontSendNotification); hintText = lHint.getText();
    addChildComponent (scaleGrid);
    addAndMakeVisible (bPrev); addAndMakeVisible (bNext); addAndMakeVisible (bPatterns); addAndMakeVisible (lPattern);
    bPrev.onClick = [this] { stepPattern (-1); }; bNext.onClick = [this] { stepPattern (1); }; bPatterns.onClick = [this] { patternMenu(); };
    bPatterns.setTooltip ("Saved patterns: the whole page - four tracks, their lanes and the key. Load one, save this one under a name, or delete one");
    lPattern.setFont (MoogLook::silk (12.f, true)); lPattern.setColour (Label::textColourId, MoogLook::cream); lPattern.setJustificationType (Justification::centred); lPattern.setText ("(unsaved pattern)", dontSendNotification);
    for (int i = 0; i < Euclid::kTracks; ++i)
    {
        auto row = std::make_unique<Row> (*this, i);
        addAndMakeVisible (row->ring); addAndMakeVisible (row->bMute); addAndMakeVisible (row->bSolo); addAndMakeVisible (row->lNum);
        row->lNum.setText (String (i + 1), dontSendNotification); row->lNum.setFont (MoogLook::silk (22.f, true)); row->lNum.setColour (Label::textColourId, trackColour (i)); row->lNum.setJustificationType (Justification::centred);
        row->bMute.setClickingTogglesState (true); row->bMute.setColour (TextButton::buttonOnColourId, MoogLook::modColour[4]); row->bMute.setTooltip ("Mute this track");
        row->bSolo.setClickingTogglesState (true); row->bSolo.setColour (TextButton::buttonOnColourId, MoogLook::modColour[6]); row->bSolo.setTooltip ("Solo this track (several can be soloed)");
        row->bMute.onClick = [this, i] { patch().tr[(size_t) i].mute = rows[(size_t) i]->bMute.getToggleState(); repaint(); };
        row->bSolo.onClick = [this, i] { patch().tr[(size_t) i].solo = rows[(size_t) i]->bSolo.getToggleState(); repaint(); };
        for (int c = 0; c < kCells; ++c)
        {
            auto cell = std::make_unique<ValueCell> (cellNames[c]); cell->colour = trackColour (i);
            switch (c)
            {
                case 0: cell->min = 1; cell->max = Euclid::kMaxSteps; cell->def = 16; break;
                case 1: cell->min = 0; cell->max = Euclid::kMaxSteps; cell->def = 4; break;
                case 2: cell->min = -Euclid::kMaxSteps; cell->max = Euclid::kMaxSteps; cell->def = 0; break;
                case 3: cell->min = 0; cell->max = 100; cell->def = 100; cell->text = [] (int v) { return String (v) + "%"; }; cell->pixelsPerStep = 2.f; break;
                case 4: cell->min = 1; cell->max = 127; cell->def = 100; cell->pixelsPerStep = 2.f; break;
                case 5: cell->min = 0; cell->max = 50; cell->def = 0; cell->text = [] (int v) { return String (v) + "%"; }; cell->pixelsPerStep = 3.f; break;
                case 6: cell->min = 5; cell->max = 200; cell->def = 50; cell->text = [] (int v) { return String (v) + "%"; }; cell->pixelsPerStep = 2.f; break;
                case 7: cell->min = 0; cell->max = 11; cell->def = Euclid::kClockDefault; cell->text = [] (int v) { return Euclid::clockNames()[v]; }; cell->pixelsPerStep = 12.f; break;
                case 8: cell->min = 0; cell->max = 127; cell->def = 48; cell->pixelsPerStep = 4.f;
                        cell->text = [this, i] (int v) { const int src = patch().tr[(size_t) i].src; static const char* walk[5] = { "walk up", "walk down", "up-down", "random", "rnd steps" };
                                                         return src >= 7 ? String (walk[jlimit (0, 4, src - 7)]) : src > 0 ? "chord " + String (src) : Euclid::noteText (patch().scaleOn ? Euclid::quantise (v, patch().scale, patch().root) : v); };   // in key: the note the engine plays (it snaps at play time)
                        cell->onPopup = [this, i] { pitchMenu (i); };
                        cell->stepFn = [this] (int v, int d) { auto& pt = patch(); if (! pt.scaleOn) return jlimit (0, 127, v + d); return Euclid::stepScale (v, d, pt.scale, pt.root); }; break;
                case 9: cell->min = 1; cell->max = Euclid::kMaxChord; cell->def = 1; cell->text = [] (int v) { return v == 1 ? String ("single") : String (v) + " notes"; }; cell->pixelsPerStep = 12.f; break;
                default: cell->min = 1; cell->max = Euclid::kMaxRatchet; cell->def = 1; cell->text = [] (int v) { return v == 1 ? String ("off") : "x" + String (v); }; cell->pixelsPerStep = 12.f; break;
            }
            cell->onChange = [this, i, c] (int v) { applyCell (i, c, v); };
            if (laneOfCell (c) >= 0) cell->onSelect = [this, i, c] { selectLane (i, laneOfCell (c)); };
            addAndMakeVisible (*cell); row->cells.push_back (std::move (cell));
        }
        // the lane strip under the cells
        addAndMakeVisible (row->strip); addAndMakeVisible (row->bLane); addAndMakeVisible (row->bDir); addAndMakeVisible (row->bLock); addAndMakeVisible (row->cLen);
        row->bLane.setTooltip ("Which lane the strip shows: one of this track's controls, or a Prophet-6 parameter to lock per step"); row->bLane.onClick = [this, i] { laneMenu (i); };
        row->bDir.setTooltip ("How the lane runs: forwards, backwards, ping-pong, or a random step each time"); row->bDir.onClick = [this, i] { if (auto* l = laneFor (i)) l->dir = (l->dir + 1) % 4; refreshLaneRow (i); };
        row->bLock.setClickingTogglesState (true); row->bLock.setColour (TextButton::buttonOnColourId, MoogLook::accent); row->bLock.setTooltip ("The lane is as long as the track (off: its own length, so it drifts against the pattern)");
        row->bLock.onClick = [this, i] { if (auto* l = laneFor (i)) l->lock = rows[(size_t) i]->bLock.getToggleState(); refreshLaneRow (i); };
        row->cLen.min = 2; row->cLen.max = Euclid::kLaneSteps; row->cLen.def = 16; row->cLen.colour = trackColour (i); row->cLen.pixelsPerStep = 4.f;
        row->cLen.onChange = [this, i] (int v) { if (updating) return; if (auto* l = laneFor (i)) { l->length = v; l->lock = false; } refreshLaneRow (i); };
        rows.push_back (std::move (row));
    }
    refresh();
}
void EuclidPage::pitchMenu (int i)
{
    auto& t = patch().tr[(size_t) i]; PopupMenu m;
    m.addItem (1, "Own note (the PITCH value)", true, t.src == 0);
    m.addSeparator(); m.addSectionHeader ("FROM THE CHORD PLAYED INTO THE PANEL");
    for (int k = 1; k <= 6; ++k) m.addItem (1 + k, "Chord note " + String (k) + (k == 1 ? "  (the lowest)" : ""), true, t.src == k);
    m.addSeparator(); m.addSectionHeader ("WALK THROUGH THE CHORD, ONE NOTE PER HIT");
    m.addItem (8, "Up", true, t.src == 7); m.addItem (9, "Down", true, t.src == 8); m.addItem (10, "Up and down", true, t.src == 9);
    m.addItem (11, "Random  (any note, dice)", true, t.src == 10); m.addItem (12, "Random steps  (a neighbour each hit, turning at the ends)", true, t.src == 11);
    m.showMenuAsync (PopupMenu::Options().withDeletionCheck (*this).withTargetComponent (rows[(size_t) i]->cells[8].get()), [this, i] (int r) { if (r <= 0) return; patch().tr[(size_t) i].src = r - 1; rows[(size_t) i]->cells[8]->repaint(); });
}
void EuclidPage::arpMenu()
{
    PopupMenu m;
    m.addSectionHeader ("TRACK 1'S RHYTHM ON ALL FOUR TRACKS, EACH ONE LATER");
    m.addItem (1, "Canon, 1 step apart"); m.addItem (2, "Canon, 2 steps apart"); m.addItem (4, "Canon, 4 steps apart"); m.addItem (10, "Unison (same rhythm, same place)");
    m.addSeparator(); m.addSectionHeader ("PITCHES");
    m.addItem (20, "Tracks take chord notes 1 - 4 (from the chord you play)"); m.addItem (21, "Tracks play their own PITCH");
    m.showMenuAsync (PopupMenu::Options().withDeletionCheck (*this).withTargetComponent (&bArp), [this] (int r)
    {
        if (r <= 0) return; auto& pt = patch();
        if (r <= 10)
        {
            const int d = r == 10 ? 0 : r; auto& a = pt.tr[0];
            for (int i = 1; i < Euclid::kTracks; ++i) { auto& t = pt.tr[(size_t) i]; t.steps = a.steps; t.beats = a.beats; t.clock = a.clock; t.length = a.length; t.swing = a.swing; t.rotate = a.rotate + i * d; t.clearOverrides(); }
        }
        else if (r == 20) for (int i = 0; i < Euclid::kTracks; ++i) pt.tr[(size_t) i].src = i + 1;
        else if (r == 21) for (auto& t : pt.tr) t.src = 0;
        refresh();
    });
}
String EuclidPage::laneTitle (int target) const
{
    if (target >= Euclid::kSynth) { const int p = target - Euclid::kSynth; return p >= 0 && p < (int) P6::params().size() ? String (P6::params()[(size_t) p].name).toUpperCase() : String ("?"); }
    return Euclid::laneName (target);
}
Euclid::Lane* EuclidPage::laneFor (int i)
{   // the lane the strip shows, made if there is none - unless every slot is taken: then no other lane gives way for it, the page says so instead
    auto& t = patch().tr[(size_t) i]; const int target = rows[(size_t) i]->selected; if (target < 0) return nullptr;
    if (t.lane (target) == nullptr && t.laneCount() >= Euclid::kMaxLanes)
    { warn ("Track " + String (i + 1) + ": all " + String (Euclid::kMaxLanes) + " lanes are in use.\nRemove one (the lane button's menu) before making another."); return nullptr; }
    return t.ensureLane (target);
}
void EuclidPage::warn (const String& m) { lHint.setColour (Label::textColourId, MoogLook::accent); lHint.setText (m, dontSendNotification); warnUntil = Time::currentTimeMillis() + 6000; }
void EuclidPage::selectLane (int i, int target) { rows[(size_t) i]->selected = target; refreshLaneRow (i); }
void EuclidPage::refreshLaneRow (int i)
{
    auto& r = *rows[(size_t) i]; auto& t = patch().tr[(size_t) i]; const auto* l = t.lane (r.selected);
    const bool was = updating; updating = true;
    r.bLane.setButtonText (laneTitle (r.selected) + (l != nullptr && l->any() ? String (CharPointer_UTF8 ("  \xe2\x97\x8f")) : ""));
    static const char* dirs[4] = { "FWD", "BACK", "PING", "RAND" }; r.bDir.setButtonText (dirs[l != nullptr ? l->dir : 0]);
    r.bLock.setToggleState (l == nullptr || l->lock, dontSendNotification);
    r.cLen.set (l != nullptr ? l->len (t.steps) : t.steps, false);
    for (int c = 0; c < kCells; ++c) { auto& cell = *r.cells[(size_t) c]; const int lt = laneOfCell (c); const auto* cl = lt >= 0 ? t.lane (lt) : nullptr; const bool m = cl != nullptr && cl->any(), hl = lt >= 0 && lt == r.selected; if (cell.marked != m || cell.highlight != hl) { cell.marked = m; cell.highlight = hl; cell.repaint(); } }
    updating = was; r.strip.repaint();
}
void EuclidPage::laneMenu (int i)
{
    auto& t = patch().tr[(size_t) i]; auto& r = *rows[(size_t) i];
    PopupMenu m, synth;
    m.addSectionHeader ("THIS TRACK");
    for (int k = 0; k < Euclid::kTrackLanes; ++k) { const auto* l = t.lane (k); m.addItem (1 + k, String (Euclid::laneName (k)) + (l != nullptr && l->any() ? String (CharPointer_UTF8 ("  \xe2\x97\x8f")) : ""), true, r.selected == k); }
    for (int p = 0; p < (int) P6::params().size(); ++p) { auto& q = P6::params()[(size_t) p]; if (q.kind != P6::Kind::Knob) continue; const auto* l = t.lane (Euclid::kSynth + p); synth.addItem (1000 + p, String (q.name) + (l != nullptr && l->any() ? String (CharPointer_UTF8 ("  \xe2\x97\x8f")) : ""), true, r.selected == Euclid::kSynth + p); }
    m.addSubMenu ("PROPHET-6 PARAMETER (a lock per step)", synth);
    if (const auto* l = t.lane (r.selected)) { m.addSeparator(); m.addItem (9998, "Clear this lane", l->any()); m.addItem (9999, "Remove this lane"); }
    m.showMenuAsync (PopupMenu::Options().withDeletionCheck (*this).withTargetComponent (r.bLane), [this, i] (int id)
    {
        if (id <= 0) return; auto& tr = patch().tr[(size_t) i]; auto& row = *rows[(size_t) i];
        if (id == 9998) { if (auto* l = tr.lane (row.selected)) l->v.fill (0.f); }
        else if (id == 9999) { tr.removeLane (row.selected); proc.euclidPanic = true; }   // a lock the lane held goes back to the program's value
        else if (id >= 1000) row.selected = Euclid::kSynth + (id - 1000);
        else row.selected = id - 1;
        refreshLaneRow (i);
    });
}
String EuclidPage::scaleText() { auto& pt = patch(); return String (Euclid::noteName (pt.root)) + "  " + String (Euclid::scales()[(size_t) pt.scale].name).toUpperCase(); }
void EuclidPage::setScale (int scale, int root) { patch().scale = jlimit (0, (int) Euclid::scales().size() - 1, scale); patch().root = jlimit (0, 11, root); scaleChanged(); }
void EuclidPage::scaleChanged()
{
    // the tracks' pitches stay as they were set: the engine snaps them to the key when it plays, and the PITCH cells show the snapped
    // note - so going through keys and back brings the melody back (rewriting them here pushed every pitch up, key after key)
    refresh();
}
void EuclidPage::applyCell (int i, int c, int v)
{
    if (updating) return;
    auto& t = patch().tr[(size_t) i];
    switch (c)
    {
        case 0: t.steps = v; if (t.beats > t.steps) { t.beats = t.steps; rows[(size_t) i]->cells[1]->set (t.beats, false); } break;
        case 1: t.beats = jmin (v, t.steps); if (t.beats != v) rows[(size_t) i]->cells[1]->set (t.beats, false); break;
        case 2: t.rotate = v; break;
        case 3: t.chance = v; break;
        case 4: t.velocity = v; break;
        case 5: t.swing = v; break;
        case 6: t.length = v; break;
        case 7: t.clock = v; break;
        case 8: t.pitch = patch().scaleOn ? Euclid::quantise (v, patch().scale, patch().root) : v; if (t.pitch != v) rows[(size_t) i]->cells[8]->set (t.pitch, false); break;
        case 9: t.chord = v; break;
        default: t.ratchet = v; break;
    }
    rows[(size_t) i]->ring.repaint();
}
// ---------------------------------------------------------------- the pattern library
Array<File> EuclidPage::patternFiles() const
{
    Array<File> out; Array<File> loose = patternDir().findChildFiles (File::findFiles, false, "*.p6euclid"); loose.sort(); out.addArray (loose);
    Array<File> dirs = patternDir().findChildFiles (File::findDirectories, false); dirs.sort();
    for (auto& d : dirs) { Array<File> fs = d.findChildFiles (File::findFiles, false, "*.p6euclid"); fs.sort(); out.addArray (fs); }
    return out;
}
void EuclidPage::stepPattern (int delta)
{
    auto files = patternFiles(); if (files.isEmpty()) return;
    int i = files.indexOf (currentPattern); i = i < 0 ? (delta > 0 ? 0 : files.size() - 1) : (i + delta + files.size()) % files.size();
    loadPattern (files[i]);
}
void EuclidPage::loadPattern (const File& f)
{
    auto xml = XmlDocument::parse (f); if (xml == nullptr) return;
    proc.euclidPanic = true;   // as INIT does: the old pattern's notes end and its parameter locks are let go
    const bool on = patch().on; patch().fromTree (ValueTree::fromXml (*xml)); patch().on = on;   // the master switch is the page's, not the pattern's
    currentPattern = f; lPattern.setText (f.getFileNameWithoutExtension(), dontSendNotification); lPattern.setTooltip (f.getParentDirectory().getFileName() + " / " + f.getFileNameWithoutExtension());
    for (int i = 0; i < Euclid::kTracks; ++i) { auto& t = patch().tr[(size_t) i]; rows[(size_t) i]->selected = Euclid::laneVelocity; for (auto& l : t.lanes) if (l.target >= 0 && l.any()) { rows[(size_t) i]->selected = l.target; break; } }   // show each track's first drawn lane
    refresh();
}
void EuclidPage::patternMenu()
{
    Array<File> files; PopupMenu m; int id = 100;
    auto addFiles = [&files, &id, this] (PopupMenu& menu, const File& dir) { Array<File> fs = dir.findChildFiles (File::findFiles, false, "*.p6euclid"); fs.sort(); for (auto& f : fs) { menu.addItem (id++, f.getFileNameWithoutExtension(), true, f == currentPattern); files.add (f); } return fs.size(); };
    addFiles (m, patternDir());
    Array<File> dirs = patternDir().findChildFiles (File::findDirectories, false); dirs.sort();
    for (auto& d : dirs) { PopupMenu sub; if (addFiles (sub, d) == 0) sub.addItem (1, "(nothing saved here yet)", false); m.addSubMenu (d.getFileName(), sub); }
    if (files.isEmpty() && dirs.isEmpty()) m.addItem (1, "(no saved patterns yet)", false);
    m.addSeparator(); m.addItem (2, "Save this pattern as...");
    if (currentPattern.existsAsFile()) m.addItem (3, "Save into \"" + currentPattern.getFileNameWithoutExtension() + "\"");
    if (! files.isEmpty()) { PopupMenu del; int did = 1000; for (auto& f : files) del.addItem (did++, (f.getParentDirectory() == patternDir() ? String() : f.getParentDirectory().getFileName() + " / ") + f.getFileNameWithoutExtension()); m.addSubMenu ("Delete a saved pattern", del); }
    m.showMenuAsync (PopupMenu::Options().withDeletionCheck (*this).withTargetComponent (&bPatterns), [this, files] (int r)
    {
        if (r == 2) savePatternAs();
        else if (r == 3 && currentPattern.existsAsFile()) currentPattern.replaceWithText (patch().toTree().toXmlString());
        else if (r >= 1000 && r - 1000 < files.size())
        {
            const File f = files[r - 1000];
            AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Delete pattern", "Delete the saved pattern \"" + f.getFileNameWithoutExtension() + "\"?", "Delete", "Cancel", this,
                                          ModalCallbackFunction::create ([this, safe = Component::SafePointer<EuclidPage> (this), f] (int ok) { if (safe == nullptr) return; if (ok == 1) { f.deleteFile(); if (f == currentPattern) { currentPattern = File(); lPattern.setText ("(unsaved pattern)", dontSendNotification); } } }));
        }
        else if (r >= 100 && r - 100 < files.size()) loadPattern (files[r - 100]);
    });
}
void EuclidPage::savePatternAs()
{
    auto* w = new AlertWindow ("Save pattern", "A name and a category for this pattern:", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", currentPattern.existsAsFile() ? currentPattern.getFileNameWithoutExtension() : String(), "Name");
    StringArray cats; Array<File> dirs = patternDir().findChildFiles (File::findDirectories, false); dirs.sort(); for (auto& d : dirs) cats.add (d.getFileName()); if (! cats.contains ("User")) cats.add ("User");
    w->addComboBox ("category", cats, "Category"); w->getComboBoxComponent ("category")->setSelectedId (cats.indexOf ("User") + 1, dontSendNotification);
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey)); w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, safe = Component::SafePointer<EuclidPage> (this), w] (int r)
    { if (safe == nullptr) return;
        const String name = File::createLegalFileName (w->getTextEditorContents ("name").trim());
        const String cat = w->getComboBoxComponent ("category")->getText().trim();
        if (r == 1 && name.isNotEmpty())
        {
            auto f = patternDir().getChildFile (cat.isEmpty() ? "User" : cat).getChildFile (name + ".p6euclid"); f.getParentDirectory().createDirectory();
            auto write = [this, safe, f] { if (safe == nullptr) return; f.replaceWithText (patch().toTree().toXmlString()); currentPattern = f; lPattern.setText (f.getFileNameWithoutExtension(), dontSendNotification); };
            if (f.existsAsFile() && f != currentPattern) AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Save pattern", "A pattern called \"" + f.getFileNameWithoutExtension() + "\" exists. Replace it?", "Replace", "Cancel", this, ModalCallbackFunction::create ([write] (int ok) { if (ok == 1) write(); }));
            else write();
        }
    }), true);
}
void EuclidPage::refresh()
{
    updating = true;
    auto& pt = patch();
    bOn.setToggleState (pt.on, dontSendNotification); bRun.setToggleState (proc.euclidRun.load(), dontSendNotification); bScaleOn.setToggleState (pt.scaleOn, dontSendNotification);
    bLatch.setToggleState (pt.latch, dontSendNotification);
    bChordMute.setToggleState (pt.chordMute, dontSendNotification);
    { const bool mute = pt.on && pt.chordMute, was = proc.link.muteKeys.exchange (mute); if (mute && ! was) proc.link.allKeysOff(); }   // a pattern or a project switched MUTE CHORD on: the keys that sound are let go, or their note-offs would never reach the synth
    bScale.setButtonText (scaleText());
    for (int i = 0; i < Euclid::kTracks; ++i)
    {
        auto& t = pt.tr[(size_t) i]; auto& r = *rows[(size_t) i];
        const int vals[kCells] = { t.steps, t.beats, t.rotate, t.chance, t.velocity, t.swing, t.length, t.clock, t.pitch, t.chord, t.ratchet };
        for (int c = 0; c < kCells; ++c) r.cells[(size_t) c]->set (vals[c], false);
        r.bMute.setToggleState (t.mute, dontSendNotification); r.bSolo.setToggleState (t.solo, dontSendNotification);
        refreshLaneRow (i);
    }
    updating = false; repaint();
}
void EuclidPage::timerCallback()
{
    if (warnUntil != 0 && Time::currentTimeMillis() > warnUntil) { warnUntil = 0; lHint.setColour (Label::textColourId, MoogLook::inkDim); lHint.setText (hintText, dontSendNotification); }
    const unsigned f = proc.euclid.uiFlash.exchange (0);
    for (int i = 0; i < Euclid::kTracks; ++i)
    {
        auto& ring = rows[(size_t) i]->ring;
        if (f & (1u << i)) ring.flash = 1.f; else ring.flash *= 0.8f;
        ring.repaint(); rows[(size_t) i]->strip.repaint();
    }
    const bool host = proc.euclidHostClock();
    const String t = host ? "clock: Logic, " + String (proc.hostBpm.load(), 1) + " bpm" : proc.euclidRun.load() ? "clock: own, " + String (proc.hostBpm.load() > 0 ? proc.hostBpm.load() : (double) proc.mod.bpm, 1) + " bpm (RUN)" : "clock: Logic's when it plays  -  RUN plays without it";
    { int notes[16]; const int n = proc.link.chord (notes, 16, patch().latch); String ch; for (int k = 0; k < n; ++k) ch << (k ? " " : "") << Euclid::noteText (notes[k]);
      const String full = t + (n > 0 ? "   chord: " + ch : String()); if (lClock.getText() != full) lClock.setText (full, dontSendNotification); }
    if (bRun.getToggleState() != proc.euclidRun.load()) bRun.setToggleState (proc.euclidRun.load(), dontSendNotification);
}
void EuclidPage::paint (Graphics& g)
{
    g.setColour (MoogLook::ink); g.setFont (MoogLook::silk (13.f, true));
    g.drawText ("EUCLID", getLocalBounds().reduced (8).removeFromTop (28).withX (getWidth() - 120).withWidth (100), Justification::centredRight);
}
void EuclidPage::resized()
{
    auto p = getLocalBounds().reduced (8); auto bar = p.removeFromTop (28);
    bBack.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (16);
    bOn.setBounds (bar.removeFromLeft (60)); bar.removeFromLeft (6); bRun.setBounds (bar.removeFromLeft (70)); bar.removeFromLeft (10); lClock.setBounds (bar.removeFromLeft (206)); bar.removeFromLeft (8);
    bScaleOn.setBounds (bar.removeFromLeft (70)); bar.removeFromLeft (6); bScale.setBounds (bar.removeFromLeft (170)); bar.removeFromLeft (12);
    bClear.setBounds (bar.removeFromLeft (60)); bar.removeFromLeft (4); bReset.setBounds (bar.removeFromLeft (60)); bar.removeFromLeft (4); bInit.setBounds (bar.removeFromLeft (54)); bar.removeFromLeft (10);
    bArp.setBounds (bar.removeFromLeft (60)); bar.removeFromLeft (6); bChordMute.setBounds (bar.removeFromLeft (100)); bar.removeFromLeft (6); bLatch.setBounds (bar.removeFromLeft (60)); bar.removeFromLeft (10);
    bPrev.setBounds (bar.removeFromLeft (30)); lPattern.setBounds (bar.removeFromLeft (150)); bNext.setBounds (bar.removeFromLeft (30)); bar.removeFromLeft (6); bPatterns.setBounds (bar.removeFromLeft (96));
    scaleGrid.setBounds (bScaleOn.getX(), bar.getBottom() + 4, 660, 30 + 8 + 7 * 30 + 8 + 30 + 24);
    p.removeFromTop (10);
    auto kb = p.removeFromBottom (76); p.removeFromBottom (8);   // the keyboard sits in the middle of that strip (placed by the editor); the hint takes the left
    lHint.setBounds (kb.removeFromLeft (330)); lHint.setJustificationType (Justification::topLeft);
    const int rowH = p.getHeight() / Euclid::kTracks;
    for (int i = 0; i < Euclid::kTracks; ++i)
    {
        auto& r = *rows[(size_t) i]; auto row = p.removeFromTop (rowH).reduced (0, 3);
        r.lNum.setBounds (row.removeFromLeft (34));
        r.ring.setBounds (row.removeFromLeft (row.getHeight() + 8).reduced (2));
        row.removeFromLeft (12);
        auto ms = row.removeFromRight (44); ms = ms.withSizeKeepingCentre (44, 60); r.bMute.setBounds (ms.removeFromTop (26).reduced (2, 0)); r.bSolo.setBounds (ms.removeFromBottom (26).reduced (2, 0));
        row.removeFromRight (10);
        const int cw = jmin (104, row.getWidth() / kCells); auto cells = row.removeFromTop (60).withSizeKeepingCentre (cw * kCells, 60);
        for (int c = 0; c < kCells; ++c) r.cells[(size_t) c]->setBounds (cells.removeFromLeft (cw));
        row.removeFromTop (4);
        auto lane = row.withSizeKeepingCentre (cw * kCells, row.getHeight());
        r.bLane.setBounds (lane.removeFromLeft (150).reduced (2, 6)); lane.removeFromLeft (4);
        r.bLock.setBounds (lane.removeFromRight (56).reduced (2, 6)); r.bDir.setBounds (lane.removeFromRight (60).reduced (2, 6)); r.cLen.setBounds (lane.removeFromRight (84)); lane.removeFromRight (4);
        r.strip.setBounds (lane);
    }
}
} // namespace ui
