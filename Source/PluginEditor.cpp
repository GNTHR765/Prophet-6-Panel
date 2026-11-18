#include "ClaudeData.h"
#include <array>
#include <algorithm>
#include "PluginEditor.h"
#include <cstdlib>

using namespace juce;
using namespace ui;

static void styleLabel (Label& l, const String& text, float size = 10.5f)
{
    l.setText (text.toUpperCase(), dontSendNotification);
    l.setJustificationType (Justification::centred);
    l.setFont (MoogLook::silk (size));
}

// ---------------------------------------------------------------- widgets
Knob::Knob (APVTS& s, const String& id, const String& text, const String& suffix)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 17);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);
    styleLabel (label, text);
    addAndMakeVisible (label);
    att = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
    slider.textFromValueFunction = [suffix] (double v) { return String (roundToInt (v)) + suffix; };
    slider.valueFromTextFunction = [] (const String& t) { return t.trimCharactersAtStart (" ").getDoubleValue(); };
    slider.updateText();
}
void Knob::resized() { auto r = getLocalBounds(); label.setBounds (r.removeFromTop (17)); slider.setBounds (r); }

Toggle::Toggle (APVTS& s, const String& id, const String& text)
{
    button.setButtonText (text);
    addAndMakeVisible (button);
    att = std::make_unique<APVTS::ButtonAttachment> (s, id, button);
}
Chooser::Chooser (APVTS& s, const String& id, const String& text)
{
    if (auto* choice = dynamic_cast<AudioParameterChoice*> (s.getParameter (id))) box.addItemList (choice->choices, 1);
    addAndMakeVisible (box);
    styleLabel (label, text);
    addAndMakeVisible (label);
    att = std::make_unique<APVTS::ComboBoxAttachment> (s, id, box);
}
void Chooser::resized()
{
    auto r = getLocalBounds();
    if (label.getText().isNotEmpty()) label.setBounds (r.removeFromTop (17));
    box.setBounds (r.withHeight (jmin (24, r.getHeight())));
}
void Section::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.f);
    g.setColour (MoogLook::panelLight.withAlpha (0.35f)); g.fillRoundedRectangle (r, 4.f);
    g.setColour (MoogLook::rule.brighter (0.15f)); g.drawRoundedRectangle (r, 4.f, 1.2f);
    g.setColour (MoogLook::cream); g.setFont (MoogLook::silk (11.f, true));
    g.drawText (title, r.reduced (10.f, 3.f).toNearestInt().removeFromTop (16), Justification::centredLeft);
    g.setColour (MoogLook::rule); g.drawLine (r.getX() + 8.f, r.getY() + 20.f, r.getRight() - 8.f, r.getY() + 20.f, 0.8f);
}
void Lamp::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto dot = r.removeFromLeft (r.getHeight()).reduced (4.f);
    g.setColour (on ? Colour (0xff7fc96b) : Colour (0xff5a2a2a)); g.fillEllipse (dot);
    if (on) { g.setColour (Colour (0xff7fc96b).withAlpha (0.35f)); g.fillEllipse (dot.expanded (3.f)); }
    g.setColour (MoogLook::ink); g.setFont (MoogLook::silk (11.f, true));
    g.drawText (text, r.withTrimmedLeft (4.f), Justification::centredLeft);
}

// ---------------------------------------------------------------- sequencer piano roll
int SeqGrid::colOf (int x) const { auto a = rollArea(); if (x < a.getX()) return -1; const int c = (x - a.getX()) * P6PanelProcessor::kSeqSteps / jmax (1, a.getWidth()); return c < P6PanelProcessor::kSeqSteps ? c : -1; }
int SeqGrid::noteOf (int y) const { auto a = rollArea(); if (! a.getVerticalRange().contains (y)) return -1; return topNote - (y - a.getY()) / kRowH; }
Rectangle<int> SeqGrid::noteRect (int step, int note) const
{
    auto a = rollArea(); const float cw = (float) a.getWidth() / P6PanelProcessor::kSeqSteps;
    return { a.getX() + roundToInt (step * cw), a.getY() + (topNote - note) * kRowH, roundToInt (cw), kRowH };
}
int SeqGrid::trackAt (int step, int note) const
{   // the note that sounds at this step on this pitch: starts here, or tied from an earlier step
    for (int t = 0; t < P6PanelProcessor::kSeqTracks; ++t)
        if (proc.seqNote (t, step) == note && (proc.seqOn (t, step) || (proc.seqTie (t, step) && proc.seqUsed (t, step)))) return t;
    return -1;
}
int SeqGrid::startOf (int t, int step) const { while (step > 0 && proc.seqTie (t, step)) --step; return step; }
int SeqGrid::lengthOf (int t, int step) const { int n = 1; while (step + n < P6PanelProcessor::kSeqSteps && proc.seqTie (t, step + n) && proc.seqUsed (t, step + n)) ++n; return n; }
int SeqGrid::freeTrack (int step) const { for (int t = 0; t < P6PanelProcessor::kSeqTracks; ++t) if (! proc.seqOn (t, step) && ! proc.seqTie (t, step)) return t; return -1; }
void SeqGrid::paint (Graphics& g)
{
    const int len = proc.seqLength();
    auto a = rollArea(); const float cw = (float) a.getWidth() / P6PanelProcessor::kSeqSteps;
    const int rows = a.getHeight() / kRowH;
    g.setFont (MoogLook::silk (9.5f));
    // pitch rows and key column
    for (int r = 0; r < rows; ++r)
    {
        const int note = topNote - r; if (note < 0) break;
        auto row = Rectangle<int> (a.getX(), a.getY() + r * kRowH, a.getWidth(), kRowH);
        const bool black = MidiMessage::isMidiNoteBlack (note);
        g.setColour (black ? MoogLook::panelLight.withAlpha (0.25f) : MoogLook::panelLight.withAlpha (0.45f)); g.fillRect (row);
        g.setColour (black ? Colour (0xff202020) : MoogLook::cream.withAlpha (0.85f)); g.fillRect (Rectangle<int> (0, row.getY(), kKeyW - 4, kRowH).reduced (0, 1));
        if (note % 12 == 0) { g.setColour (black ? MoogLook::ink : Colour (0xff202020)); g.drawText (MidiMessage::getMidiNoteName (note, true, true, 3), 2, row.getY(), kKeyW - 8, kRowH, Justification::centredRight); }
    }
    // step columns
    for (int s = 0; s < P6PanelProcessor::kSeqSteps; ++s)
    {
        const int x = a.getX() + roundToInt (s * cw);
        g.setColour (s % 4 == 0 ? MoogLook::rule : MoogLook::rule.withAlpha (0.35f)); g.drawVerticalLine (x, (float) a.getY(), (float) a.getBottom());
        if (s >= len) { g.setColour (Colours::black.withAlpha (0.45f)); g.fillRect (Rectangle<int> (x, a.getY(), roundToInt (cw), a.getHeight())); }
        g.setColour (s < len ? MoogLook::ink : MoogLook::inkDim); g.drawText (String (s + 1), x, 0, roundToInt (cw), kHead, Justification::centred);
    }
    // playhead (where the synth's sequencer is, judged from the notes it sends) and the keys currently sounding
    if (proc.playStep >= 0 && proc.playStep < len)
    {
        const int x = a.getX() + roundToInt (proc.playStep * cw);
        g.setColour (MoogLook::cream.withAlpha (0.18f)); g.fillRect (Rectangle<int> (x, a.getY(), roundToInt (cw), a.getHeight()));
        g.setColour (MoogLook::cream); g.fillRect (Rectangle<int> (x, 0, roundToInt (cw), 3));
    }
    for (int n = 0; n < 128; ++n)
        if (proc.noteHeld[n] && n <= topNote && n > topNote - rows)
        { auto row = Rectangle<int> (0, a.getY() + (topNote - n) * kRowH, kKeyW - 4, kRowH).reduced (0, 1); g.setColour (MoogLook::accent); g.fillRect (row); }
    g.setColour (MoogLook::rule); g.drawRect (a);
    // notes (a tied note is one block spanning its steps)
    for (int s = 0; s < P6PanelProcessor::kSeqSteps; ++s)
        for (int t = 0; t < P6PanelProcessor::kSeqTracks; ++t)
        {
            if (! proc.seqOn (t, s)) continue;
            const int n = proc.seqNote (t, s); if (n > topNote || n <= topNote - rows) continue;
            const int span = lengthOf (t, s);
            auto r = noteRect (s, n).getUnion (noteRect (s + span - 1, n)).reduced (1);
            g.setColour (MoogLook::accent.withMultipliedBrightness (0.6f + 0.4f * proc.seqVel (t, s) / 127.f)); g.fillRoundedRectangle (r.toFloat(), 2.f);
            g.setColour (Colours::black.withAlpha (0.6f)); g.drawRoundedRectangle (r.toFloat(), 2.f, 1.f);
        }
    // velocity lane
    auto v = velArea();
    g.setColour (MoogLook::panelLight.withAlpha (0.35f)); g.fillRect (v);
    g.setColour (MoogLook::inkDim); g.drawText ("VEL", 0, v.getY(), kKeyW - 8, v.getHeight(), Justification::centredRight);
    for (int s = 0; s < P6PanelProcessor::kSeqSteps; ++s)
    {
        int vel = 0; for (int t = 0; t < P6PanelProcessor::kSeqTracks; ++t) if (proc.seqOn (t, s)) vel = jmax (vel, proc.seqVel (t, s));
        if (vel == 0) continue;
        auto col = Rectangle<int> (a.getX() + roundToInt (s * cw), v.getY(), roundToInt (cw), v.getHeight()).reduced (2, 2);
        auto bar = col.withHeight (jmax (2, col.getHeight() * vel / 127)).withBottomY (col.getBottom());
        g.setColour (MoogLook::accent.withAlpha (0.8f)); g.fillRect (bar);
    }
    g.setColour (MoogLook::rule); g.drawRect (v);
}
void SeqGrid::mouseDown (const MouseEvent& e)
{
    dragTrack = -1; dragStep = -1; dragVel = false; dragStartY = e.y;
    const int s = colOf (e.x); if (s < 0) return;
    if (velArea().contains (e.getPosition()))
    {
        dragVel = true; dragStep = s; int best = 0; for (int t = 0; t < P6PanelProcessor::kSeqTracks; ++t) if (proc.seqOn (t, s)) best = jmax (best, proc.seqVel (t, s));
        dragValue = best; mouseDrag (e); return;
    }
    const int n = noteOf (e.y); if (n < 0) return;
    int t = trackAt (s, n); dragResize = false;
    if (t >= 0)
    {   // existing note: grab near its right edge to change its length, otherwise move it
        const int st = startOf (t, s), span = lengthOf (t, st);
        auto r = noteRect (st, n).getUnion (noteRect (st + span - 1, n));
        if (e.x >= r.getRight() - 6) dragResize = true;
        dragTrack = t; dragStep = st; dragValue = n; dragLen = span; repaint(); return;
    }
    t = freeTrack (s); if (t < 0) return;
    proc.addSeqNote (t, s, n, 100);
    dragTrack = t; dragStep = s; dragValue = n; dragLen = 1; repaint();
}
void SeqGrid::mouseDrag (const MouseEvent& e)
{
    if (dragStep < 0) return;
    if (dragVel)
    {
        auto v = velArea(); const int vel = jlimit (1, 127, roundToInt (127.f * (float) (v.getBottom() - e.y) / (float) jmax (1, v.getHeight())));
        for (int t = 0; t < P6PanelProcessor::kSeqTracks; ++t) if (proc.seqOn (t, dragStep) && proc.seqVel (t, dragStep) != vel) proc.setSeqVel (t, dragStep, vel);
        repaint(); return;
    }
    if (dragTrack < 0) return;
    if (dragResize)
    {
        const int c = colOf (e.x); const int len = jlimit (1, P6PanelProcessor::kSeqSteps - dragStep, (c < 0 ? dragLen : c - dragStep + 1));
        if (len != lengthOf (dragTrack, dragStep)) { proc.setSeqTieLength (dragTrack, dragStep, len); repaint(); }
        return;
    }
    const int n = jlimit (12, 108, dragValue + (dragStartY - e.y) / kRowH);
    if (proc.seqNote (dragTrack, dragStep) != n) { proc.setSeqNote (dragTrack, dragStep, n); repaint(); }
}
void SeqGrid::mouseDoubleClick (const MouseEvent& e)
{
    const int s = colOf (e.x), n = noteOf (e.y); if (s < 0 || n < 0) return;
    const int t = trackAt (s, n); if (t < 0) return;
    proc.removeSeqNote (t, startOf (t, s)); dragTrack = -1; dragStep = -1; repaint();
}
void SeqGrid::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const int rows = rollArea().getHeight() / kRowH;
    topNote = jlimit (jmin (108, rows + 12), 108, topNote + (w.deltaY > 0 ? 3 : -3)); repaint();
}

// ---------------------------------------------------------------- stars
void Stars::draw (Graphics& g, Rectangle<int> r, int value, bool dim)
{
    const float sz = jmin ((float) r.getHeight(), r.getWidth() / 5.f) * 0.8f;
    for (int i = 0; i < 5; ++i)
    {
        Path p; p.addStar (Point<float> (r.getX() + (i + 0.5f) * r.getWidth() / 5.f, r.getCentreY()), 5, sz * 0.2f, sz * 0.5f, -MathConstants<float>::pi / 2.f);
        if (i < value) { g.setColour (dim ? MoogLook::accent.withAlpha (0.6f) : MoogLook::accent); g.fillPath (p); }
        else { g.setColour (MoogLook::inkDim.withAlpha (0.5f)); g.strokePath (p, PathStrokeType (1.f)); }
    }
}
void Stars::paint (Graphics& g) { draw (g, getLocalBounds(), value, false); }

// ---------------------------------------------------------------- preset browser
PresetBrowser::PresetBrowser (P6PanelProcessor& p) : proc (p)
{
    for (auto* c : std::initializer_list<Component*> { &search, &cBanks, &cStars, &catList, &list, &bBack, &bPrev, &bNext, &bScan, &bPlay, &lCount, &lDesc, &lNow, &lSearch, &lStars, &lSource, &eNote, &lNote }) addAndMakeVisible (c);
    bPlay.setClickingTogglesState (true); bPlay.setColour (TextButton::buttonOnColourId, MoogLook::accent);
    bPlay.onClick = [this] { const bool on = bPlay.getToggleState(); proc.link.sendNrpn (1088, on ? 1 : 0); proc.seqPlaying = on; proc.playStep = -1; ++proc.uiVersion; };
    for (auto* l : { &lSearch, &lStars, &lSource }) { l->setFont (MoogLook::silk (10.5f)); l->setJustificationType (Justification::centredLeft); }
    lCount.setFont (MoogLook::silk (10.5f)); lCount.setColour (Label::textColourId, MoogLook::inkDim);
    lNow.setFont (MoogLook::silk (12.f, true)); lNow.setColour (Label::textColourId, MoogLook::cream); lNow.setJustificationType (Justification::centredLeft);
    lDesc.setFont (MoogLook::silk (12.f)); lDesc.setColour (Label::textColourId, MoogLook::accent);
    search.setFont (MoogLook::silk (13.f)); search.setTextToShowWhenEmpty ("type part of a name", MoogLook::inkDim);
    search.setColour (TextEditor::backgroundColourId, MoogLook::panelLight); search.setColour (TextEditor::textColourId, MoogLook::cream); search.setColour (TextEditor::outlineColourId, MoogLook::rule);
    lNote.setFont (MoogLook::silk (10.5f)); lNote.setJustificationType (Justification::centredRight);
    eNote.setFont (MoogLook::silk (13.f)); eNote.setTextToShowWhenEmpty ("your own words about this sound - Return keeps them", MoogLook::inkDim); eNote.setInputRestrictions (200);
    eNote.setColour (TextEditor::backgroundColourId, MoogLook::panelLight); eNote.setColour (TextEditor::textColourId, MoogLook::cream); eNote.setColour (TextEditor::outlineColourId, MoogLook::rule);
    eNote.onReturnKey = [this] { storeNote(); list.grabKeyboardFocus(); }; eNote.onFocusLost = [this] { storeNote(); };
    eNote.onEscapeKey = [this] { showSelected(); list.grabKeyboardFocus(); };
    search.onTextChange = [this] { proc.browserSearch = search.getText(); rebuild(); };
    cBanks.addItem ("Factory banks  (5xx - 9xx)", 1); cBanks.addItem ("User banks  (0xx - 4xx)", 2); cBanks.addItem ("Library  (files)", 3); cBanks.setSelectedId (1, dontSendNotification);
    cBanks.onChange = [this] { proc.browserBanks = cBanks.getSelectedId(); loadSource(); refreshCategories(); rebuild(); };
    cStars.addItem ("any", 1); for (int i = 1; i <= 5; ++i) cStars.addItem (String::repeatedString (String::fromUTF8 ("\xe2\x98\x85"), i) + (i < 5 ? " and up" : ""), i + 1);
    cStars.addSeparator(); cStars.addItem ("best first (all)", 10); cStars.setSelectedId (1, dontSendNotification); cStars.onChange = [this] { refreshCategories(); rebuild(); };
    for (auto* lb : { &catList, &list }) { lb->setColour (ListBox::backgroundColourId, Colour (0xff0c0c0c)); lb->setColour (ListBox::outlineColourId, MoogLook::rule); lb->setOutlineThickness (1); }
    list.setRowHeight (24); catList.setRowHeight (24);
    bBack.onClick = [this] { if (onBack) onBack(); }; bPrev.onClick = [this] { step (-1); }; bNext.onClick = [this] { step (1); };
    bScan.setTooltip ("Read every program name from the synth (about seven minutes; the synth clicks on each request - turn the volume down).");
    bScan.onClick = [this] { if (proc.scanning) proc.stopNameScan(); else proc.startNameScan(); };
    loadSource(); refreshCategories(); rebuild();
}
void PresetBrowser::loadSource()
{
    allRows.clear();
    const int src = cBanks.getSelectedId();
    if (src == 3)
    {
        for (auto& e : proc.library()) { Row r; r.key = e.key; r.category = e.category; r.name = e.name; r.file = e.file; r.user = true; r.stars = proc.rating (e.key); r.note = proc.note (e.key); allRows.push_back (r); }
        return;
    }
    const bool userBanks = src == 2;
    for (auto& e : P6Presets::all())
    {
        Row r; r.slot = (userBanks ? 0 : 500) + e.number; r.key = String (r.slot); r.category = e.category; r.name = e.name; r.description = e.description; r.p5 = e.p5;
        if (userBanks)
        {   // what the synth really holds in that slot (the name scan), then what the panel filed there (its category); else the factory name the user banks ship with
            if (auto sn = proc.synthNames.find (e.number); sn != proc.synthNames.end() && sn->second.isNotEmpty() && sn->second != e.name) { r.name = sn->second; r.description = {}; r.p5 = false; r.user = true; }
            if (auto it = proc.catalogue.find (e.number); it != proc.catalogue.end()) { r.name = it->second.name; r.category = it->second.category; r.description = {}; r.p5 = false; r.user = true; }
        }
        r.stars = proc.rating (r.key); r.note = proc.note (r.key); allRows.push_back (r);
    }
}
void PresetBrowser::refreshCategories()
{
    const int starSel = cStars.getSelectedId(); const int minStars = starSel >= 2 && starSel <= 6 ? starSel - 1 : 0;
    std::map<String, int> counts; int all = 0, mine = 0, p5 = 0;
    for (auto& r : allRows) { if (r.stars < minStars) continue; ++all; ++counts[r.category]; if (r.user) ++mine; if (r.p5) ++p5; }
    cats.clear(); cats.push_back ({ "All", all });
    if (mine > 0) cats.push_back ({ "My sounds", mine });
    if (p5 > 0) cats.push_back ({ "Prophet-5 classics", p5 });
    for (auto& [c, n] : counts) cats.push_back ({ c, n });
    bool found = false; for (auto& c : cats) found |= c.first == currentCategory; if (! found) currentCategory = "All";
    catList.updateContent(); catList.repaint();
}
void PresetBrowser::rebuild()
{
    storeNote();   // (a note being typed is kept before the list is made anew)
    rows.clear();
    const auto q = search.getText().trim().toLowerCase(); const int starSel = cStars.getSelectedId(); const int minStars = starSel >= 2 && starSel <= 6 ? starSel - 1 : 0;
    for (auto& r : allRows)
    {
        if (r.stars < minStars) continue;
        if (currentCategory == "My sounds") { if (! r.user) continue; }
        else if (currentCategory == "Prophet-5 classics") { if (! r.p5) continue; }
        else if (currentCategory != "All" && r.category != currentCategory) continue;
        if (q.isNotEmpty() && ! (r.name.toLowerCase().contains (q) || r.description.toLowerCase().contains (q) || r.note.toLowerCase().contains (q) || r.category.toLowerCase().contains (q))) continue;
        rows.push_back (r);
    }
    if (starSel == 10) std::stable_sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.stars > b.stars; });
    selectedEntry = -1; showSelected(); list.updateContent(); catList.repaint();
    lCount.setText (String (rows.size()) + " of " + String (allRows.size()), dontSendNotification);
    syncCurrent();
}
void PresetBrowser::goToCurrent()
{   // open on the program the synth is playing: its bank source, its category selected, its row selected
    const int slot = proc.currentBank * 100 + proc.currentProgram;
    if (proc.libKey.isNotEmpty())
    {   // the sound came from the file library: open the library on its category (or the one the user browsed, if it holds it)
        cBanks.setSelectedId (3, dontSendNotification); proc.browserBanks = 3;
        search.setText ({}, false); proc.browserSearch = {};
        loadSource();
        String cat; for (auto& r : allRows) if (r.key == proc.libKey) { cat = r.category; break; }
        if (cat.isNotEmpty())
        {
            const auto& b = proc.browserCategory;
            currentCategory = (b == "All" || b == "My sounds" || b == cat) ? b : cat;
            proc.browserCategory = currentCategory;
            refreshCategories(); rebuild();
            for (int i = 0; i < (int) cats.size(); ++i) if (cats[(size_t) i].first == currentCategory) catList.scrollToEnsureRowIsOnscreen (i);
            return;
        }
        proc.libKey = {};   // the file is gone: fall back to the synth slot
    }
    cBanks.setSelectedId (slot >= 500 ? 1 : 2, dontSendNotification); proc.browserBanks = cBanks.getSelectedId();
    search.setText ({}, false); proc.browserSearch = {};
    loadSource();
    currentCategory = "All"; for (auto& r : allRows) if (r.slot == slot) { currentCategory = r.category; break; }
    proc.browserCategory = currentCategory;
    refreshCategories(); rebuild();
    for (int i = 0; i < (int) cats.size(); ++i) if (cats[(size_t) i].first == currentCategory) catList.scrollToEnsureRowIsOnscreen (i);
}
void PresetBrowser::syncCurrent()
{
    const int slot = proc.currentBank * 100 + proc.currentProgram;
    if (proc.libKey.isNotEmpty())
    {
        lNow.setText ("LIBRARY   " + proc.libKey.fromFirstOccurrenceOf ("lib:", false, false).replace ("/", "  /  "), dontSendNotification);
        const int row = libIndex();
        if (row >= 0 && selectedEntry != row) { storeNote(); selectedEntry = row; list.scrollToEnsureRowIsOnscreen (row); showSelected(); }
        list.repaint(); catList.repaint(); return;
    }
    lNow.setText ("SYNTH IS ON  " + String (slot).paddedLeft ('0', 3) + "   " + proc.patchName(), dontSendNotification);
    int row = -1; for (int i = 0; i < (int) rows.size(); ++i) if (rows[(size_t) i].slot == slot) { row = i; break; }
    // a file row stays selected after loading it; otherwise the selection follows the synth
    if (row >= 0 && (selectedEntry < 0 || rows[(size_t) selectedEntry].slot >= 0) && selectedEntry != row)
    { storeNote(); selectedEntry = row; list.scrollToEnsureRowIsOnscreen (row); showSelected(); }
    list.repaint(); catList.repaint();
}
void PresetBrowser::select (int row, bool send)
{
    if (row < 0 || row >= (int) rows.size()) return;
    storeNote();
    selectedEntry = row; list.scrollToEnsureRowIsOnscreen (row); list.repaint();
    auto& e = rows[(size_t) row];
    showSelected();
    if (! send) return;
    if (e.slot >= 0) proc.selectProgram (e.slot / 100, e.slot % 100);
    else if (e.file.existsAsFile()) { proc.libKey = e.key; proc.loadPatchFile (e.file); }
}
void PresetBrowser::showSelected()
{
    const bool has = selectedEntry >= 0 && selectedEntry < (int) rows.size();
    eNote.setEnabled (has);
    if (! has) { lDesc.setText ({}, dontSendNotification); eNote.setText ({}, false); return; }
    auto& e = rows[(size_t) selectedEntry];
    lDesc.setText (e.description.isNotEmpty() ? e.name + "  -  " + e.description : e.name, dontSendNotification);
    if (eNote.getText() != e.note) eNote.setText (e.note, false);
}
void PresetBrowser::storeNote()
{
    if (selectedEntry < 0 || selectedEntry >= (int) rows.size()) return;
    auto& r = rows[(size_t) selectedEntry];
    proc.setNote (r.key, eNote.getText()); const auto t = proc.note (r.key);
    if (t == r.note) return;
    r.note = t; for (auto& a : allRows) if (a.key == r.key) a.note = t;
    list.repaintRow (selectedEntry);
}
void PresetBrowser::ensureLib()
{
    if (proc.libKey.isEmpty() || isVisible()) return;   // while the browser is open the user's browsing wins
    if (cBanks.getSelectedId() != 3 || libIndex() < 0) goToCurrent();
}
int PresetBrowser::libIndex() const
{
    for (int i = 0; i < (int) rows.size(); ++i) if (rows[(size_t) i].key == proc.libKey) return i;
    return -1;
}
void PresetBrowser::step (int delta) { const int r = selectedEntry; select (jlimit (0, jmax (0, (int) rows.size() - 1), (r < 0 ? 0 : r + delta)), true); }
void PresetBrowser::CatModel::paintListBoxItem (int row, Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) b.cats.size()) return;
    auto& c = b.cats[(size_t) row]; const bool sel = c.first == b.currentCategory;
    const int slot = b.proc.currentBank * 100 + b.proc.currentProgram; bool current = false;   // the category of the program the synth is on
    for (auto& r : b.allRows) if (r.slot == slot) { current = r.category == c.first; break; }
    if (sel) { g.setColour (MoogLook::accent.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
    if (current) { Path p; p.addTriangle (3.f, h * 0.5f - 4.f, 3.f, h * 0.5f + 4.f, 10.f, h * 0.5f); g.setColour (MoogLook::accent); g.fillPath (p); }
    g.setFont (MoogLook::silk (12.f, sel || current)); g.setColour (sel || current ? MoogLook::cream : MoogLook::ink);
    g.drawText (c.first, 14, 0, w - 64, h, Justification::centredLeft);
    g.setColour (MoogLook::inkDim); g.drawText (String (c.second), w - 50, 0, 40, h, Justification::centredRight);
    if (row == 0 || (row < 3 && (c.first == "My sounds" || c.first == "Prophet-5 classics"))) { g.setColour (MoogLook::rule.withAlpha (0.6f)); g.drawHorizontalLine (h - 1, 6.f, (float) w - 6.f); }
}
void PresetBrowser::paintListBoxItem (int row, Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) rows.size()) return;
    auto& e = rows[(size_t) row]; const bool selected = row == selectedEntry;
    const bool current = e.slot >= 0 && e.slot == proc.currentBank * 100 + proc.currentProgram;   // what the synth is playing now
    if (selected) { g.setColour (MoogLook::accent.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
    g.setFont (MoogLook::silk (12.f, current));
    Stars::draw (g, starsRect (h), e.stars, ! selected);
    if (current) { Path p; p.addTriangle (94.f, h * 0.5f - 4.f, 94.f, h * 0.5f + 4.f, 101.f, h * 0.5f); g.setColour (MoogLook::accent); g.fillPath (p); }
    g.setColour (current ? MoogLook::cream : MoogLook::inkDim); g.drawText (e.slot >= 0 ? String (e.slot).paddedLeft ('0', 3) : "file", 104, 0, 40, h, Justification::centredLeft);
    g.setColour (MoogLook::accent.withAlpha (0.9f)); g.drawText (e.category, 150, 0, 190, h, Justification::centredLeft);   // wide enough for library folder names
    g.setColour (selected ? MoogLook::cream : MoogLook::ink); g.drawText (e.name, 350, 0, e.note.isEmpty() ? w - 364 : kNameW, h, Justification::centredLeft);
    if (e.note.isNotEmpty()) { g.setFont (MoogLook::silk (12.f)); g.setColour (selected ? MoogLook::cream.withAlpha (0.85f) : MoogLook::inkDim); g.drawText (e.note, 350 + kNameW + 10, 0, w - 364 - kNameW - 10, h, Justification::centredLeft); }
    if (e.user) { g.setColour (MoogLook::accent); g.fillEllipse (w - 8.f, h * 0.5f - 3.f, 6.f, 6.f); }
}
void PresetBrowser::listBoxItemClicked (int row, const MouseEvent& e)
{
    if (row < 0 || row >= (int) rows.size()) return;
    if (starsRect (list.getRowHeight()).contains (e.getPosition()))
    {   // a click on the stars rates without changing the program
        auto& r = rows[(size_t) row]; const int n = jlimit (1, 5, 1 + (e.x - starsRect (1).getX()) * 5 / jmax (1, starsRect (1).getWidth()));
        proc.setRating (r.key, n == r.stars ? 0 : n); r.stars = proc.rating (r.key);
        for (auto& a : allRows) if (a.key == r.key) a.stars = r.stars;
        list.repaintRow (row); return;
    }
    select (row, true);
}
void PresetBrowser::listBoxItemDoubleClicked (int row, const MouseEvent& e)
{
    if (starsRect (list.getRowHeight()).contains (e.getPosition())) return;
    select (row, true); if (onBack) onBack();
}
void PresetBrowser::paint (Graphics& g)
{
    auto side = catList.getBounds().expanded (6).withTop (cBanks.getY() - 8);
    g.setColour (MoogLook::panelLight.withAlpha (0.25f)); g.fillRoundedRectangle (side.toFloat(), 4.f);
    g.setColour (MoogLook::ink); g.setFont (MoogLook::silk (11.f, true)); g.drawText ("CATEGORIES", Rectangle<int> (catList.getX() + 4, catList.getY() - 24, 200, 22), Justification::centredLeft);
}
void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto bar = r.removeFromTop (28);
    bBack.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (24);
    lSearch.setBounds (bar.removeFromLeft (56)); search.setBounds (bar.removeFromLeft (300).reduced (0, 2)); bar.removeFromLeft (24);
    bPrev.setBounds (bar.removeFromLeft (40)); bar.removeFromLeft (4); bNext.setBounds (bar.removeFromLeft (40)); bar.removeFromLeft (24); bPlay.setBounds (bar.removeFromLeft (80)); bar.removeFromLeft (24);
    bScan.setBounds (bar.removeFromRight (110)); bar.removeFromRight (10); lCount.setBounds (bar.removeFromRight (230)); lNow.setBounds (bar);
    r.removeFromTop (12);
    r.removeFromBottom (84);   // the keyboard strip the editor places at the bottom
    auto foot = r.removeFromBottom (24); r.removeFromBottom (4);
    eNote.setBounds (foot.removeFromRight (jmin (560, foot.getWidth() / 2))); lNote.setBounds (foot.removeFromRight (70).withTrimmedRight (8)); lDesc.setBounds (foot);
    auto side = r.removeFromLeft (250); r.removeFromLeft (12);
    side.removeFromTop (8);
    auto src = side.removeFromTop (24); lSource.setBounds (src.removeFromLeft (60)); cBanks.setBounds (src.reduced (0, 1)); side.removeFromTop (6);
    auto st = side.removeFromTop (24); lStars.setBounds (st.removeFromLeft (60)); cStars.setBounds (st.reduced (0, 1)); side.removeFromTop (30);
    catList.setBounds (side.withTrimmedBottom (8));
    // the painted frame uses cStars.getBottom(): keep the combos above the list
    list.setBounds (r);
}

// ---------------------------------------------------------------- save-as dialog
P6PanelEditor::SaveAsDialog::SaveAsDialog (P6PanelEditor& e) : ed (e)
{
    for (auto* c : std::initializer_list<Component*> { &eName, &cCategory, &cBank, &sProg, &bOk, &bCancel, &lName, &lCategory, &lSlot, &lTitle, &tSlot }) addAndMakeVisible (c);
    tSlot.setToggleState (true, dontSendNotification); tSlot.onClick = [this] { cBank.setEnabled (tSlot.getToggleState()); sProg.setEnabled (tSlot.getToggleState()); };
    for (auto* l : { &lName, &lCategory, &lSlot }) { l->setFont (MoogLook::silk (10.5f)); l->setJustificationType (Justification::centredRight); }
    lTitle.setFont (MoogLook::silk (13.f, true)); lTitle.setJustificationType (Justification::centredLeft);
    eName.setFont (MoogLook::silk (13.f)); eName.setInputRestrictions (P6::kNameLength);
    eName.setColour (TextEditor::backgroundColourId, MoogLook::panelLight); eName.setColour (TextEditor::textColourId, MoogLook::cream); eName.setColour (TextEditor::outlineColourId, MoogLook::rule);
    cCategory.setEditableText (true); cCategory.setTextWhenNothingSelected ("pick or type a category");
    for (int b = 0; b < 5; ++b) cBank.addItem ("User " + String (b + 1) + "  (" + String (b) + "xx)", b + 1);
    sProg.setSliderStyle (Slider::IncDecButtons); sProg.setRange (0, 99, 1); sProg.setTextBoxStyle (Slider::TextBoxLeft, false, 40, 20);
    sProg.textFromValueFunction = [] (double v) { return String (roundToInt (v)).paddedLeft ('0', 2); };
    bCancel.onClick = [this] { setVisible (false); };
    bOk.onClick = [this]
    {
        const String name = eName.getText().trim(); if (name.isEmpty() || File::createLegalFileName (name).isEmpty()) return;
        const String cat = cCategory.getText().trim(); const int bank = jlimit (0, 4, cBank.getSelectedId() - 1), prog = jlimit (0, 99, roundToInt (sProg.getValue())); const bool slot = tSlot.getToggleState();
        // what this would replace: a library file of the same name under that category, and the synth's program in that slot
        const File f = P6PanelProcessor::libraryDir().getChildFile (File::createLegalFileName (cat.isEmpty() ? "Uncategorised" : cat)).getChildFile (File::createLegalFileName (name) + ".p6patch");
        String warn;
        if (f.existsAsFile() && ed.proc.libKey != "lib:" + f.getParentDirectory().getFileName() + "/" + f.getFileNameWithoutExtension()) warn << "the library sound \"" + f.getFileNameWithoutExtension() + "\" in " + f.getParentDirectory().getFileName();
        if (slot)
        {
            const int s = bank * 100 + prog; String there;
            if (auto it = ed.proc.catalogue.find (s); it != ed.proc.catalogue.end() && it->second.name.isNotEmpty()) there = it->second.name;
            else if (auto it2 = ed.proc.synthNames.find (s); it2 != ed.proc.synthNames.end()) there = it2->second;
            warn << (warn.isEmpty() ? "" : " and ") << "program " + String (s).paddedLeft ('0', 3) + (there.isNotEmpty() ? " (\"" + there + "\")" : String()) + " on the Prophet-6";
        }
        auto go = [safe = Component::SafePointer<SaveAsDialog> (this), name, cat, bank, prog, slot]
        {
            if (safe == nullptr) return;
            safe->ed.proc.saveAs (name, cat, bank, prog, slot);
            safe->ed.presetPage.cBanks.onChange(); safe->ed.refreshProgramRow();
            safe->setVisible (false);
        };
        if (warn.isEmpty()) { go(); return; }
        AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Save as", "This replaces " + warn + ". Go ahead?", "Replace", "Cancel", this, ModalCallbackFunction::create ([go] (int r) { if (r == 1) go(); }));
    };
    setVisible (false);
}
void P6PanelEditor::SaveAsDialog::open()
{
    eName.setText (ed.proc.patchName().trim(), dontSendNotification);
    StringArray cats; for (auto& e : P6Presets::all()) cats.addIfNotAlreadyThere (e.category);
    for (auto& [slot, e] : ed.proc.catalogue) if (e.category.isNotEmpty()) cats.addIfNotAlreadyThere (e.category);
    for (auto& e : ed.proc.library()) cats.addIfNotAlreadyThere (e.category);
    cats.sort (true); cCategory.clear (dontSendNotification); int id = 1; for (auto& c : cats) cCategory.addItem (c, id++);
    const int slot = ed.proc.currentBank * 100 + ed.proc.currentProgram;
    String cat; if (ed.proc.libKey.isNotEmpty()) cat = ed.proc.libKey.fromFirstOccurrenceOf ("lib:", false, false).upToFirstOccurrenceOf ("/", false, false);
    else if (auto it = ed.proc.catalogue.find (slot); it != ed.proc.catalogue.end()) cat = it->second.category;
    else if (ed.proc.currentProgram < 100) for (auto& e : P6Presets::all()) if (e.number == (ed.proc.currentBank % 5) * 100 + ed.proc.currentProgram) cat = e.category;
    cCategory.setText (cat, dontSendNotification);
    cBank.setSelectedId (jlimit (0, 4, ed.proc.currentBank % 5) + 1, dontSendNotification); sProg.setValue (ed.proc.currentProgram, dontSendNotification);
    // a program from a user slot goes back to its slot; a factory program or a library sound only goes to the library unless asked (so SAVE never overwrites a slot by surprise)
    tSlot.setToggleState (ed.proc.libKey.isEmpty() && ed.proc.currentBank < 5, dontSendNotification); tSlot.onClick();
    setVisible (true); toFront (true); eName.grabKeyboardFocus();
}
void P6PanelEditor::SaveAsDialog::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.55f));
    auto box = getLocalBounds().withSizeKeepingCentre (640, 230).toFloat();
    g.setColour (MoogLook::panel); g.fillRoundedRectangle (box, 6.f); g.setColour (MoogLook::accent); g.drawRoundedRectangle (box, 6.f, 1.5f);
}
void P6PanelEditor::SaveAsDialog::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (640, 230).reduced (16);
    lTitle.setBounds (box.removeFromTop (24)); box.removeFromTop (10);
    auto row = box.removeFromTop (28); lName.setBounds (row.removeFromLeft (90)); eName.setBounds (row.reduced (0, 2)); box.removeFromTop (10);
    row = box.removeFromTop (28); lCategory.setBounds (row.removeFromLeft (90)); cCategory.setBounds (row.reduced (0, 3)); box.removeFromTop (10);
    row = box.removeFromTop (28); lSlot.setBounds (row.removeFromLeft (90)); cBank.setBounds (row.removeFromLeft (170).reduced (0, 3)); row.removeFromLeft (10); sProg.setBounds (row.removeFromLeft (110).reduced (0, 2)); row.removeFromLeft (10); tSlot.setBounds (row);
    box.removeFromTop (16); row = box.removeFromTop (30); bOk.setBounds (row.removeFromRight (100)); row.removeFromRight (10); bCancel.setBounds (row.removeFromRight (100));
}

// ---------------------------------------------------------------- editor
// ---------------------------------------------------------------- MOD page
void XYPad::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0c0c0c)); g.fillRoundedRectangle (r, 6.f);
    g.setColour (MoogLook::rule); for (int i = 1; i < 4; ++i) { g.drawVerticalLine (roundToInt (r.getWidth() * i / 4.f), r.getY(), r.getBottom()); g.drawHorizontalLine (roundToInt (r.getHeight() * i / 4.f), r.getX(), r.getRight()); }
    g.setColour (MoogLook::rule.brighter (0.3f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.f, 1.2f);
    const float px = r.getX() + (x + 1.f) * 0.5f * r.getWidth(), py = r.getY() + (1.f - y) * 0.5f * r.getHeight();
    g.setColour (down ? MoogLook::accent : MoogLook::inkDim); g.fillEllipse (px - 9.f, py - 9.f, 18.f, 18.f);
    if (down) { g.setColour (MoogLook::accent.withAlpha (0.3f)); g.fillEllipse (px - 18.f, py - 18.f, 36.f, 36.f); }
    g.setColour (MoogLook::inkDim); g.setFont (MoogLook::silk (10.f)); g.drawText ("X", r.reduced (6.f).removeFromBottom (14.f), Justification::centredRight); g.drawText ("Y", r.reduced (6.f).removeFromTop (14.f), Justification::centredLeft);
}
// ---------------------------------------------------------------- the drawable shape
Point<float> ShapeEditor::toScreen (float x, float y) const { auto a = area(); return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() }; }
int ShapeEditor::pointAt (Point<float> p) const
{
    auto& d = data(); int best = -1; float bd = 9.f;
    for (int i = 0; i < d.count; ++i) { const float dist = p.getDistanceFrom (toScreen (d.x[i], d.y[i])); if (dist < bd) { bd = dist; best = i; } }
    return best;
}
int ShapeEditor::segmentAt (float x) const { auto& d = data(); for (int i = 0; i < d.count - 1; ++i) if (x >= d.x[i] && x <= d.x[i + 1]) return i; return -1; }
void ShapeEditor::paint (Graphics& g)
{
    auto a = area(); auto& d = data(); const Colour col = MoogLook::modColour[4 + idx];
    g.setColour (Colour (0xff0e0e0e)); g.fillRoundedRectangle (a.expanded (4.f), 4.f);
    g.setColour (MoogLook::rule.withAlpha (0.35f));
    for (int i = 1; i < 4; ++i) { const float x = a.getX() + a.getWidth() * (float) i / 4.f; g.drawLine (x, a.getY(), x, a.getBottom(), 0.6f); const float y = a.getY() + a.getHeight() * (float) i / 4.f; g.drawLine (a.getX(), y, a.getRight(), y, i == 2 ? 1.2f : 0.6f); }
    Path p; const int steps = jmax (2, (int) a.getWidth());
    for (int i = 0; i <= steps; ++i) { const float x = (float) i / (float) steps; auto pt = toScreen (x, d.evaluate (x)); if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt); }
    Path fill (p); fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
    g.setColour (col.withAlpha (d.on ? 0.14f : 0.06f)); g.fillPath (fill);
    g.setColour (d.on ? col : col.withAlpha (0.5f)); g.strokePath (p, PathStrokeType (2.f));
    if (d.on)
    {   // where it is right now
        const float ph = d.mode == 0 && ! proc.shapeRun[idx] ? 1.f : proc.shapePos[idx]; const float px = a.getX() + ph * a.getWidth();
        g.setColour (MoogLook::cream.withAlpha (0.7f)); g.drawLine (px, a.getY(), px, a.getBottom(), 1.f); g.fillEllipse (px - 3.f, toScreen (ph, d.evaluate (ph)).y - 3.f, 6.f, 6.f);
    }
    const int hot = pointAt (getMouseXYRelative().toFloat());
    for (int i = 0; i < d.count; ++i)
    {
        auto pt = toScreen (d.x[i], d.y[i]);
        g.setColour (i == hot || i == dragPoint ? Colours::white : MoogLook::cream); g.fillEllipse (pt.x - 4.f, pt.y - 4.f, 8.f, 8.f);
        g.setColour (Colours::black); g.drawEllipse (pt.x - 4.f, pt.y - 4.f, 8.f, 8.f, 1.f);
    }
    g.setColour (MoogLook::inkDim); g.setFont (MoogLook::silk (9.5f));
    g.drawText ("click: add a point  -  drag a point: move  -  drag the line: bend  -  double-click a point: remove  -  right-click: ready-made shapes", getLocalBounds().removeFromBottom (14).reduced (10, 0), Justification::centredLeft);
}
void ShapeEditor::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    auto& d = data(); dragPoint = pointAt (e.position); dragSeg = -1;
    if (dragPoint >= 0) return;
    auto a = area(); const float x = jlimit (0.f, 1.f, (e.position.x - a.getX()) / a.getWidth()); const float y = jlimit (0.f, 1.f, (a.getBottom() - e.position.y) / a.getHeight());
    const int seg = segmentAt (x); if (seg < 0) return;
    if (std::abs (toScreen (x, d.evaluate (x)).y - e.position.y) < 7.f) { dragSeg = seg; dragStartBend = d.bend[seg]; dragStartY = e.position.y; return; }   // on the line: bend this segment
    if (d.count >= Mod::Shape::kMax) return;   // elsewhere: a new point
    for (int i = d.count; i > seg + 1; --i) { d.x[i] = d.x[i - 1]; d.y[i] = d.y[i - 1]; d.bend[i] = d.bend[i - 1]; }
    d.x[seg + 1] = x; d.y[seg + 1] = y; d.bend[seg + 1] = 0.f; ++d.count; dragPoint = seg + 1;
    proc.modSettingsChanged(); repaint();
}
void ShapeEditor::mouseDrag (const MouseEvent& e)
{
    auto& d = data(); auto a = area();
    if (dragSeg >= 0) { d.bend[dragSeg] = jlimit (-1.f, 1.f, dragStartBend + (dragStartY - e.position.y) / 60.f); proc.modSettingsChanged(); repaint(); return; }
    if (dragPoint < 0) return;
    float x = jlimit (0.f, 1.f, (e.position.x - a.getX()) / a.getWidth()); const float y = jlimit (0.f, 1.f, (a.getBottom() - e.position.y) / a.getHeight());
    if (dragPoint == 0) x = 0.f; else if (dragPoint == d.count - 1) x = 1.f; else x = jlimit (d.x[dragPoint - 1] + 0.005f, d.x[dragPoint + 1] - 0.005f, x);
    d.x[dragPoint] = x; d.y[dragPoint] = y; proc.modSettingsChanged(); repaint();
}
void ShapeEditor::mouseUp (const MouseEvent&) { dragPoint = -1; dragSeg = -1; repaint(); }
void ShapeEditor::mouseDoubleClick (const MouseEvent& e)
{
    auto& d = data(); const int i = pointAt (e.position);
    if (i <= 0 || i >= d.count - 1) return;   // the two ends stay
    for (int k = i; k < d.count - 1; ++k) { d.x[k] = d.x[k + 1]; d.y[k] = d.y[k + 1]; d.bend[k] = d.bend[k + 1]; }
    --d.count; dragPoint = -1; proc.modSettingsChanged(); repaint();
}
void ShapeEditor::showMenu()
{
    PopupMenu m;
    m.addItem (1, "Ramp down (envelope)"); m.addItem (2, "Ramp up"); m.addItem (3, "Triangle"); m.addItem (4, "Sine"); m.addItem (5, "Pulse"); m.addItem (6, "Eight random steps");
    m.addSeparator(); m.addItem (7, "Flip vertically"); m.addItem (8, "Straighten all segments");
    m.showMenuAsync (PopupMenu::Options().withDeletionCheck (*this).withTargetComponent (this), [this] (int r)
    {
        if (r <= 0) return;   // cancelled - or the editor was closed while the menu was open (then `this` is gone)
        auto& d = data();
        auto set = [&d] (std::initializer_list<std::array<float, 3>> pts) { int i = 0; for (auto& p : pts) { d.x[i] = p[0]; d.y[i] = p[1]; d.bend[i] = p[2]; ++i; } d.count = i; };
        switch (r)
        {
            case 1: d.setDefault(); break;
            case 2: set ({ { 0.f, 0.f, 0.f }, { 1.f, 1.f, 0.f } }); break;
            case 3: set ({ { 0.f, 0.f, 0.f }, { 0.5f, 1.f, 0.f }, { 1.f, 0.f, 0.f } }); break;
            case 4: set ({ { 0.f, 0.5f, 0.f }, { 0.25f, 1.f, 0.55f }, { 0.5f, 0.5f, -0.55f }, { 0.75f, 0.f, 0.55f }, { 1.f, 0.5f, -0.55f } }); break;
            case 5: set ({ { 0.f, 1.f, 0.f }, { 0.49f, 1.f, 0.f }, { 0.5f, 0.f, 0.f }, { 1.f, 0.f, 0.f } }); break;
            case 6: { Random rnd; int i = 0; for (int s = 0; s < 8; ++s) { const float v = rnd.nextFloat(); d.x[i] = (float) s / 8.f + (s == 0 ? 0.f : 0.001f); d.y[i] = v; d.bend[i] = 0.f; ++i; d.x[i] = (float) (s + 1) / 8.f; d.y[i] = v; d.bend[i] = 0.f; ++i; } d.count = i; break; }
            case 7: for (int i = 0; i < d.count; ++i) d.y[i] = 1.f - d.y[i]; break;
            case 8: for (int i = 0; i < d.count; ++i) d.bend[i] = 0.f; break;
            default: return;
        }
        proc.modSettingsChanged(); repaint();
    });
}
ModPage::ModPage (P6PanelProcessor& p) : proc (p)
{
    auto style = [] (Label& l) { l.setFont (MoogLook::silk (10.5f)); l.setJustificationType (Justification::centredLeft); };
    auto amount = [] (Slider& s) { s.setSliderStyle (Slider::LinearHorizontal); s.setRange (-1.0, 1.0, 0.01); s.setDoubleClickReturnValue (true, 0.0); s.setTextBoxStyle (Slider::TextBoxRight, false, 44, 18); s.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100)) + "%"; }; s.valueFromTextFunction = [] (const String& t) { return t.getDoubleValue() / 100.0; }; };
    addAndMakeVisible (bBack); bBack.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (sBpm); addAndMakeVisible (lBpm); addAndMakeVisible (lClock); style (lBpm); style (lClock); lClock.setColour (Label::textColourId, MoogLook::inkDim);
    addAndMakeVisible (lNow); addAndMakeVisible (bPrev); addAndMakeVisible (bNext); lNow.setFont (MoogLook::silk (12.f, true)); lNow.setColour (Label::textColourId, MoogLook::cream); lNow.setJustificationType (Justification::centredLeft);
    auto stepPreset = [this] (int d) { int slot = jlimit (0, 999, proc.currentBank * 100 + proc.currentProgram + d); proc.selectProgram (slot / 100, slot % 100); };
    bPrev.onClick = [stepPreset] { stepPreset (-1); }; bNext.onClick = [stepPreset] { stepPreset (1); };
    sBpm.setSliderStyle (Slider::IncDecButtons); sBpm.setRange (40, 240, 1); sBpm.setTextBoxStyle (Slider::TextBoxLeft, false, 44, 20); sBpm.onValueChange = [this] { if (! updating) proc.mod.bpm = (float) sBpm.getValue(); };
    for (int i = 0; i < 2; ++i)
    {
        auto& L = lfo[i]; addAndMakeVisible (secLfo[i]);
        for (auto* comp : std::initializer_list<Component*> { &L.on, &L.shape, &L.sync, &L.hz, &L.div, &L.depth, &L.dest[0], &L.dest[1], &L.amt[0], &L.amt[1], &L.lRate, &L.lDepth, &L.lDest[0], &L.lDest[1] }) addAndMakeVisible (comp);
        style (L.lRate); style (L.lDepth); style (L.lDest[0]); style (L.lDest[1]); L.lDest[0].setText ("DEST 1", dontSendNotification); L.lDest[1].setText ("DEST 2", dontSendNotification);
        L.shape.addItemList (Mod::shapes(), 1); Mod::fillDivisions (L.div); fillDest (L.dest[0]); fillDest (L.dest[1]);
        L.hz.setSliderStyle (Slider::LinearHorizontal); L.hz.setRange (0.02, 20.0, 0.01); L.hz.setSkewFactorFromMidPoint (1.0); L.hz.setTextBoxStyle (Slider::TextBoxRight, false, 56, 18); L.hz.textFromValueFunction = [] (double v) { return String (v, 2) + " Hz"; };
        L.depth.setSliderStyle (Slider::LinearHorizontal); L.depth.setRange (0.0, 1.0, 0.01); L.depth.setTextBoxStyle (Slider::TextBoxRight, false, 44, 18); L.depth.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100)) + "%"; };
        amount (L.amt[0]); amount (L.amt[1]);
        auto apply = [this, i] { if (updating) return; auto& s = proc.mod.lfo[i]; auto& W = lfo[i]; s.on = W.on.getToggleState(); s.shape = jmax (0, W.shape.getSelectedId() - 1); s.sync = W.sync.getToggleState(); s.hz = (float) W.hz.getValue(); s.div = jmax (0, W.div.getSelectedId() - 1); s.depth = (float) W.depth.getValue();
                                 for (int d = 0; d < 2; ++d) { s.dest[d] = destFromBox (W.dest[d]); s.amt[d] = (float) W.amt[d].getValue(); } W.hz.setVisible (! showShapes && ! s.sync); W.div.setVisible (! showShapes && s.sync); proc.modSettingsChanged(); };
        L.on.onClick = apply; L.shape.onChange = apply; L.sync.onClick = apply; L.hz.onValueChange = apply; L.div.onChange = apply; L.depth.onValueChange = apply; L.dest[0].onChange = apply; L.dest[1].onChange = apply; L.amt[0].onValueChange = apply; L.amt[1].onValueChange = apply;
    }
    addAndMakeVisible (secSeq);
    for (auto* comp : std::initializer_list<Component*> { &seqOn, &seqLen, &seqAmt, &seqDiv, &seqDest, &lSeqLen, &lSeqDiv, &lSeqDest, &lSeqAmt }) addAndMakeVisible (comp);
    style (lSeqLen); style (lSeqDiv); style (lSeqDest); style (lSeqAmt);
    for (int s = 0; s < 16; ++s) { addAndMakeVisible (seqStep[s]); seqStep[s].setSliderStyle (Slider::LinearVertical); seqStep[s].setRange (0.0, 1.0, 0.01); seqStep[s].setTextBoxStyle (Slider::NoTextBox, false, 0, 0); seqStep[s].onValueChange = [this, s] { if (! updating) proc.mod.seq.step[s] = (float) seqStep[s].getValue(); }; }
    seqLen.setSliderStyle (Slider::IncDecButtons); seqLen.setRange (1, 16, 1); seqLen.setTextBoxStyle (Slider::TextBoxLeft, false, 36, 20);
    Mod::fillDivisions (seqDiv); fillDest (seqDest); amount (seqAmt);
    auto applySeq = [this] { if (updating) return; auto& s = proc.mod.seq; s.on = seqOn.getToggleState(); s.length = roundToInt (seqLen.getValue()); s.div = jmax (0, seqDiv.getSelectedId() - 1); s.dest = destFromBox (seqDest); s.amt = (float) seqAmt.getValue(); proc.modSettingsChanged(); repaint(); };
    seqOn.onClick = applySeq; seqLen.onValueChange = applySeq; seqDiv.onChange = applySeq; seqDest.onChange = applySeq; seqAmt.onValueChange = applySeq;
    addAndMakeVisible (secEnv);
    for (auto* comp : std::initializer_list<Component*> { &envOn, &envA, &envD, &envS, &envR, &lEnvA, &lEnvD, &lEnvS, &lEnvR }) addAndMakeVisible (comp);
    style (lEnvA); style (lEnvD); style (lEnvS); style (lEnvR);
    for (int d = 0; d < 3; ++d) { addAndMakeVisible (envDest[d]); addAndMakeVisible (envAmt[d]); addAndMakeVisible (lEnvDest[d]); style (lEnvDest[d]); lEnvDest[d].setText ("DEST " + String (d + 1), dontSendNotification); fillDest (envDest[d]); amount (envAmt[d]); }
    for (int c = 0; c < 3; ++c)
    {   // the bend of a segment: left of the middle it moves fast at first, right of it slowly at first; the segment's time stays as set
        addAndMakeVisible (envCurve[c]); addAndMakeVisible (lEnvCurve[c]); style (lEnvCurve[c]); lEnvCurve[c].setText ("CURVE", dontSendNotification);
        auto& s = envCurve[c]; s.setSliderStyle (Slider::LinearHorizontal); s.setRange (-1.0, 1.0, 0.01); s.setDoubleClickReturnValue (true, 0.0); s.setTextBoxStyle (Slider::TextBoxRight, false, 66, 18);
        s.textFromValueFunction = [] (double v) { return std::abs (v) < 0.005 ? String ("straight") : (v < 0 ? "fast " : "slow ") + String (roundToInt (std::abs (v) * 100)) + "%"; };
        s.valueFromTextFunction = [] (const String& t) { const double v = t.retainCharacters ("0123456789.").getDoubleValue() / 100.0; return t.containsIgnoreCase ("slow") ? v : t.containsIgnoreCase ("fast") ? -v : 0.0; };
        s.setTooltip ("How the segment bends: fast = moves quickly at first and slows down, slow = starts gently and speeds up; double-click for straight");
    }
    envS.setSliderStyle (Slider::LinearHorizontal); envS.setRange (0.0, 1.0, 0.01); envS.setTextBoxStyle (Slider::TextBoxRight, false, 60, 18); envS.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100)) + "%"; };
    for (auto* sl : { &envA, &envD, &envR }) { sl->setSliderStyle (Slider::LinearHorizontal); sl->setRange (1.0, 20000.0, 1.0); sl->setSkewFactorFromMidPoint (500.0); sl->setTextBoxStyle (Slider::TextBoxRight, false, 60, 18); sl->textFromValueFunction = [] (double v) { return v < 1000 ? String (roundToInt (v)) + " ms" : String (v / 1000.0, 2) + " s"; }; }
    envS.updateText();
    auto applyEnv = [this] { if (updating) return; auto& e = proc.mod.env; e.on = envOn.getToggleState(); e.attackMs = (float) envA.getValue(); e.decayMs = (float) envD.getValue(); e.sustain = (float) envS.getValue(); e.releaseMs = (float) envR.getValue();
                             for (int d = 0; d < 3; ++d) { e.dest[d] = destFromBox (envDest[d]); e.amt[d] = (float) envAmt[d].getValue(); e.curve[d] = (float) envCurve[d].getValue(); } proc.modSettingsChanged(); };
    envOn.onClick = applyEnv; envA.onValueChange = applyEnv; envD.onValueChange = applyEnv; envS.onValueChange = applyEnv; envR.onValueChange = applyEnv;
    for (int d = 0; d < 3; ++d) { envDest[d].onChange = applyEnv; envAmt[d].onValueChange = applyEnv; envCurve[d].onValueChange = applyEnv; }
    addAndMakeVisible (secXY); addAndMakeVisible (pad); addAndMakeVisible (xyLatch); addAndMakeVisible (lHint);
    xyLatch.setTooltip ("Keep the pad's position (and the gate) after releasing the mouse; double-click the pad to let go"); xyLatch.onClick = [this] { pad.latch = xyLatch.getToggleState(); if (! pad.latch && pad.down) { pad.down = false; proc.xyTouch (pad.x, pad.y, false); pad.repaint(); } };
    addAndMakeVisible (xyGate); addAndMakeVisible (xyGateInv); addAndMakeVisible (lGate); style (lGate); addAndMakeVisible (xyA); addAndMakeVisible (lA); style (lA);
    xyA.setSliderStyle (Slider::LinearVertical); xyA.setRange (0.0, 1.0, 0.01); xyA.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    xyA.setTooltip ("A: a third axis as a fader you set and leave - it works whether or not the pad is touched (automatable as MOD A)");
    xyA.onValueChange = [this] { if (! updating) { proc.mod.aLevel = (float) xyA.getValue(); proc.modSettingsChanged(); } };
    lHint.setFont (MoogLook::silk (10.5f)); lHint.setColour (Label::textColourId, MoogLook::inkDim); lHint.setJustificationType (Justification::topLeft);
    lHint.setText ("Drag on the pad: X and Y each push a parameter away from its program value\n(AMOUNT = how far, INV = the other way). GATE: a switch that is on while you touch the pad.\nRelease the pad and X, Y and the gate are back where the program has them.\nA is a fader on its own: set it, leave it, it stays.", dontSendNotification);
    fillDest (xyGate, true);
    for (int a = 0; a < 3; ++a) { addAndMakeVisible (xyDest[a]); addAndMakeVisible (xyAmt[a]); addAndMakeVisible (xyInv[a]); addAndMakeVisible (lXy[a]); style (lXy[a]); lXy[a].setText (String::charToString ((juce_wchar) ("XYA"[a])), dontSendNotification); xyInv[a].setButtonText ("INV"); fillDest (xyDest[a]); amount (xyAmt[a]); xyAmt[a].setRange (0.0, 1.0, 0.01); }
    auto applyXY = [this] { if (updating) return; auto& x = proc.mod.xy; x.gateDest = destFromBox (xyGate); x.gateInv = xyGateInv.getToggleState();
                            for (int a = 0; a < 3; ++a) { x.dest[a] = destFromBox (xyDest[a]); x.amt[a] = (float) xyAmt[a].getValue(); x.inv[a] = xyInv[a].getToggleState(); } proc.modSettingsChanged(); };
    xyGate.onChange = applyXY; xyGateInv.onClick = applyXY; for (int a = 0; a < 3; ++a) { xyDest[a].onChange = applyXY; xyAmt[a].onValueChange = applyXY; xyInv[a].onClick = applyXY; }
    pad.onChange = [this] (float x, float y, bool down) { proc.xyTouch (x, y, down); };
    addAndMakeVisible (bShapes); bShapes.setTooltip ("Two drawable modulators: a curve you draw, played once per note, looped, or looped and restarted by each note - and back to the MOD view");
    bShapes.onClick = [this] { setShapesVisible (! showShapes); };
    for (int i = 0; i < 2; ++i)
    {
        auto& R = shapeRow[i]; addChildComponent (secShape[i]); shapeComps.push_back (&secShape[i]);
        for (auto* comp : std::initializer_list<Component*> { &R.editor, &R.on, &R.sync, &R.bipolar, &R.mode, &R.div, &R.seconds, &R.lMode, &R.lLen, &R.dest[0], &R.dest[1], &R.dest[2], &R.amt[0], &R.amt[1], &R.amt[2], &R.lDest[0], &R.lDest[1], &R.lDest[2], &R.presets }) { addChildComponent (comp); shapeComps.push_back (comp); }
        R.presets.setTooltip ("Saved shapes: load one, save this one under a name, or delete one (the curve with its mode and length; the destinations stay as they are)"); R.presets.onClick = [this, i] { shapePresetMenu (i); };
        style (R.lMode); style (R.lLen); for (int d = 0; d < 3; ++d) { style (R.lDest[d]); R.lDest[d].setText ("DEST " + String (d + 1), dontSendNotification); fillDest (R.dest[d]); amount (R.amt[d]); }
        R.mode.addItemList (StringArray { "One shot per note", "Loop", "Loop, restart on note" }, 1); Mod::fillDivisions (R.div);
        R.seconds.setSliderStyle (Slider::LinearHorizontal); R.seconds.setRange (0.05, 120.0, 0.01); R.seconds.setSkewFactorFromMidPoint (4.0); R.seconds.setTextBoxStyle (Slider::TextBoxRight, false, 60, 18);
        R.seconds.textFromValueFunction = [] (double v) { return v < 1.0 ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; };
        R.mode.setTooltip ("One shot: the curve runs once from each note and then holds its end value. Loop: it runs round and round at the LENGTH. Loop, restart: the same, but every note starts it from the left");
        R.sync.setTooltip ("LENGTH as a note value at the tempo instead of seconds: a one shot then takes that long, a loop repeats in step with the song"); R.bipolar.setTooltip ("On: the curve swings the knob both ways around its value (the middle line = no change). Off: it only pushes the knob upwards from its value");
        auto apply = [this, i] { if (updating) return; auto& s = proc.mod.shape[i]; auto& W = shapeRow[i]; s.on = W.on.getToggleState(); s.mode = jmax (0, W.mode.getSelectedId() - 1); s.sync = W.sync.getToggleState(); s.seconds = (float) W.seconds.getValue(); s.div = jmax (0, W.div.getSelectedId() - 1); s.bipolar = W.bipolar.getToggleState();
                                 for (int d = 0; d < 3; ++d) { s.dest[d] = destFromBox (W.dest[d]); s.amt[d] = (float) W.amt[d].getValue(); } W.seconds.setVisible (showShapes && ! s.sync); W.div.setVisible (showShapes && s.sync); proc.modSettingsChanged(); };
        R.on.onClick = apply; R.mode.onChange = apply; R.sync.onClick = apply; R.seconds.onValueChange = apply; R.div.onChange = apply; R.bipolar.onClick = apply; for (int d = 0; d < 3; ++d) { R.dest[d].onChange = apply; R.amt[d].onValueChange = apply; }
    }
    refresh();
}
void ModPage::setShapesVisible (bool b)
{
    showShapes = b; bShapes.setButtonText (b ? "MOD" : "SHAPES");
    const Component* keep[] = { &bBack, &bPrev, &bNext, &lNow, &lBpm, &sBpm, &lClock, &bShapes };
    for (auto* c : getChildren())
    {
        if (std::find (std::begin (keep), std::end (keep), c) != std::end (keep) || dynamic_cast<MidiKeyboardComponent*> (c) != nullptr) continue;
        const bool isShape = std::find (shapeComps.begin(), shapeComps.end(), c) != shapeComps.end();
        c->setVisible (isShape ? b : ! b);
    }
    refresh(); resized(); repaint();
}
void ModPage::shapePresetMenu (int i)
{
    // the library: Prophet 6 Patches/Shapes/<Category>/<name>.p6shape - every folder is a submenu, loose files in Shapes/ are listed at the top
    Array<File> files; PopupMenu m; int id = 100;
    auto addFiles = [&files, &id] (PopupMenu& menu, const File& dir) { Array<File> fs = dir.findChildFiles (File::findFiles, false, "*.p6shape"); fs.sort(); for (auto& f : fs) { menu.addItem (id++, f.getFileNameWithoutExtension()); files.add (f); } return fs.size(); };
    addFiles (m, shapeDir());
    Array<File> dirs = shapeDir().findChildFiles (File::findDirectories, false); dirs.sort();
    for (auto& d : dirs) { PopupMenu sub; if (addFiles (sub, d) == 0) sub.addItem (1, "(nothing saved here yet)", false); m.addSubMenu (d.getFileName(), sub); }
    if (files.isEmpty() && dirs.isEmpty()) m.addItem (1, "(no saved shapes yet)", false);
    m.addSeparator(); m.addItem (2, "Save this shape as...  (into User)");
    if (! files.isEmpty()) { PopupMenu del; int did = 1000; for (auto& f : files) del.addItem (did++, (f.getParentDirectory() == shapeDir() ? String() : f.getParentDirectory().getFileName() + " / ") + f.getFileNameWithoutExtension()); m.addSubMenu ("Delete a saved shape", del); }
    m.showMenuAsync (PopupMenu::Options().withDeletionCheck (*this).withTargetComponent (&shapeRow[i].presets), [this, i, files] (int r)
    {
        if (r == 2) saveShapeAs (i);
        else if (r >= 1000 && r - 1000 < files.size())
        {
            const File f = files[r - 1000];
            AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Delete shape", "Delete the saved shape \"" + f.getFileNameWithoutExtension() + "\"?", "Delete", "Cancel", this,
                                          ModalCallbackFunction::create ([f] (int ok) { if (ok == 1) f.deleteFile(); }));
        }
        else if (r >= 100 && r - 100 < files.size()) loadShape (i, files[r - 100]);
    });
}
void ModPage::saveShapeAs (int i)
{
    auto* w = new AlertWindow ("Save shape", "A name for this shape:", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "", "Name"); w->addButton ("Save", 1, KeyPress (KeyPress::returnKey)); w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, safe = Component::SafePointer<ModPage> (this), i, w] (int r)
    { if (safe == nullptr) return;
        const String name = File::createLegalFileName (w->getTextEditorContents ("name").trim());
        if (r == 1 && name.isNotEmpty())
        {
            auto f = shapeDir().getChildFile ("User").getChildFile (name + ".p6shape"); f.getParentDirectory().createDirectory();
            auto write = [this, i, f] { f.replaceWithText (proc.mod.shape[i].toTree (false).toXmlString()); };
            if (f.existsAsFile()) AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Save shape", "A shape called \"" + f.getFileNameWithoutExtension() + "\" exists. Replace it?", "Replace", "Cancel", this, ModalCallbackFunction::create ([write] (int ok) { if (ok == 1) write(); }));
            else write();
        }
    }), true);
}
void ModPage::loadShape (int i, const File& f)
{
    if (auto xml = XmlDocument::parse (f)) { proc.mod.shape[i].fromTree (ValueTree::fromXml (*xml), false); proc.modSettingsChanged(); refresh(); }
}
void ModPage::fillDest (ComboBox& b, bool switches)
{
    b.clear (dontSendNotification); b.addItem ("none", 1);
    for (int i = 0; i < (int) P6::params().size(); ++i) { auto& q = P6::params()[(size_t) i]; if (switches ? q.kind != P6::Kind::Toggle : q.kind != P6::Kind::Knob) continue; b.addItem (q.name, i + 2); }
    b.setSelectedId (1, dontSendNotification);
}
void ModPage::refresh()
{
    updating = true;
    sBpm.setValue (proc.mod.bpm, dontSendNotification);
    for (int i = 0; i < 2; ++i) { auto& s = proc.mod.lfo[i]; auto& W = lfo[i]; W.on.setToggleState (s.on, dontSendNotification); W.shape.setSelectedId (s.shape + 1, dontSendNotification); W.sync.setToggleState (s.sync, dontSendNotification); W.hz.setValue (s.hz, dontSendNotification); W.div.setSelectedId (s.div + 1, dontSendNotification); W.depth.setValue (s.depth, dontSendNotification);
        for (int d = 0; d < 2; ++d) { W.dest[d].setSelectedId (s.dest[d] + 2, dontSendNotification); W.amt[d].setValue (s.amt[d], dontSendNotification); } W.hz.setVisible (! showShapes && ! s.sync); W.div.setVisible (! showShapes && s.sync); }
    auto& q = proc.mod.seq; seqOn.setToggleState (q.on, dontSendNotification); seqLen.setValue (q.length, dontSendNotification); seqDiv.setSelectedId (q.div + 1, dontSendNotification); seqDest.setSelectedId (q.dest + 2, dontSendNotification); seqAmt.setValue (q.amt, dontSendNotification);
    for (int s = 0; s < 16; ++s) seqStep[s].setValue (q.step[s], dontSendNotification);
    auto& e = proc.mod.env; envOn.setToggleState (e.on, dontSendNotification); envA.setValue (e.attackMs, dontSendNotification); envD.setValue (e.decayMs, dontSendNotification); envS.setValue (e.sustain, dontSendNotification); envR.setValue (e.releaseMs, dontSendNotification);
    for (int d = 0; d < 3; ++d) { envDest[d].setSelectedId (e.dest[d] + 2, dontSendNotification); envAmt[d].setValue (e.amt[d], dontSendNotification); envCurve[d].setValue (e.curve[d], dontSendNotification); }
    auto& x = proc.mod.xy; xyGate.setSelectedId (x.gateDest + 2, dontSendNotification); xyGateInv.setToggleState (x.gateInv, dontSendNotification);
    for (int a = 0; a < 3; ++a) { xyDest[a].setSelectedId (x.dest[a] + 2, dontSendNotification); xyAmt[a].setValue (x.amt[a], dontSendNotification); xyInv[a].setToggleState (x.inv[a], dontSendNotification); }
    xyA.setValue (proc.mod.aLevel, dontSendNotification);
    for (int i = 0; i < 2; ++i) { auto& s = proc.mod.shape[i]; auto& W = shapeRow[i]; W.on.setToggleState (s.on, dontSendNotification); W.mode.setSelectedId (s.mode + 1, dontSendNotification); W.sync.setToggleState (s.sync, dontSendNotification); W.seconds.setValue (s.seconds, dontSendNotification); W.div.setSelectedId (s.div + 1, dontSendNotification); W.bipolar.setToggleState (s.bipolar, dontSendNotification);
        for (int d = 0; d < 3; ++d) { W.dest[d].setSelectedId (s.dest[d] + 2, dontSendNotification); W.amt[d].setValue (s.amt[d], dontSendNotification); } W.seconds.setVisible (showShapes && ! s.sync); W.div.setVisible (showShapes && s.sync); W.editor.repaint(); }
    updating = false;
    const int slot = proc.currentBank * 100 + proc.currentProgram;
    lNow.setText ("SYNTH IS ON  " + String (slot).paddedLeft ('0', 3) + "   " + proc.patchName(), dontSendNotification);
}
void ModPage::paint (Graphics& g)
{
    if (showShapes) return;
    // step sequencer playhead
    if (proc.mod.seq.on && proc.seqPos >= 0 && proc.seqPos < 16) { auto r = seqStep[proc.seqPos].getBounds().expanded (2); g.setColour (MoogLook::accent.withAlpha (0.25f)); g.fillRoundedRectangle (r.toFloat(), 3.f); }
    g.setColour (MoogLook::inkDim); g.setFont (MoogLook::silk (9.5f));
    for (int s = 0; s < 16; ++s) g.drawText (String (s + 1), seqStep[s].getX(), seqStep[s].getBottom() + 2, seqStep[s].getWidth(), 12, Justification::centred);
    if (proc.mod.seq.on) for (int s = proc.mod.seq.length; s < 16; ++s) { g.setColour (Colours::black.withAlpha (0.5f)); g.fillRect (seqStep[s].getBounds().expanded (2)); }
}
void ModPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto bar = r.removeFromTop (28);
    bBack.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (24); bPrev.setBounds (bar.removeFromLeft (40)); bar.removeFromLeft (4); bNext.setBounds (bar.removeFromLeft (40)); bar.removeFromLeft (16); lNow.setBounds (bar.removeFromLeft (560)); bar.removeFromLeft (16); lBpm.setBounds (bar.removeFromLeft (40)); sBpm.setBounds (bar.removeFromLeft (110).reduced (0, 2)); bar.removeFromLeft (16); bShapes.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (16); lClock.setBounds (bar);
    r.removeFromTop (10); r.removeFromBottom (84);
    if (showShapes)
    {   // two drawable modulators side by side: the drawing takes the room above the destinations
        const int w = (r.getWidth() - 8) / 2;
        for (int i = 0; i < 2; ++i)
        {
            auto box = r.removeFromLeft (w); r.removeFromLeft (8); secShape[i].setBounds (box); auto a = secShape[i].inner() + box.getPosition(); auto& R = shapeRow[i];
            auto row = a.removeFromTop (26); R.on.setBounds (row.removeFromLeft (60)); row.removeFromLeft (8); R.lMode.setBounds (row.removeFromLeft (44)); R.mode.setBounds (row.removeFromLeft (170).reduced (0, 1)); row.removeFromLeft (12);
            R.sync.setBounds (row.removeFromLeft (66)); row.removeFromLeft (6); R.lLen.setBounds (row.removeFromLeft (56)); R.seconds.setBounds (row.removeFromLeft (200)); R.div.setBounds (R.seconds.getBounds().withWidth (120).reduced (0, 1)); row.removeFromLeft (12); R.bipolar.setBounds (row.removeFromLeft (90));
            auto ctl = a.removeFromBottom (3 * 26 + 2 * 6);
            for (int d = 0; d < 3; ++d) { if (d > 0) ctl.removeFromTop (6); auto rr = ctl.removeFromTop (26); R.lDest[d].setBounds (rr.removeFromLeft (52)); R.dest[d].setBounds (rr.removeFromLeft (190).reduced (0, 1)); rr.removeFromLeft (8); R.amt[d].setBounds (rr.removeFromLeft (230)); }
            a.removeFromTop (8); a.removeFromBottom (8); R.editor.setBounds (a);
            R.presets.setBounds (R.dest[0].getBounds().withX (R.amt[0].getRight() + 16).withWidth (100));
        }
        return;
    }
    auto left = r.removeFromLeft (560); r.removeFromLeft (8);
    auto rowH = (left.getHeight() - 8) / 2;
    for (int i = 0; i < 2; ++i)
    {
        auto box = left.removeFromTop (rowH); left.removeFromTop (8); secLfo[i].setBounds (box); auto a = secLfo[i].inner() + box.getPosition(); auto& L = lfo[i];
        auto row = a.removeFromTop (26); L.on.setBounds (row.removeFromLeft (60)); row.removeFromLeft (8); L.shape.setBounds (row.removeFromLeft (150).reduced (0, 1)); row.removeFromLeft (12); L.sync.setBounds (row.removeFromLeft (70)); row.removeFromLeft (8); L.lRate.setBounds (row.removeFromLeft (44)); L.hz.setBounds (row.removeFromLeft (190)); L.div.setBounds (L.hz.getBounds().withWidth (120).reduced (0, 1));
        a.removeFromTop (8); row = a.removeFromTop (26); L.lDepth.setBounds (row.removeFromLeft (52)); L.depth.setBounds (row.removeFromLeft (190));
        a.removeFromTop (8); row = a.removeFromTop (26); L.lDest[0].setBounds (row.removeFromLeft (52)); L.dest[0].setBounds (row.removeFromLeft (190).reduced (0, 1)); row.removeFromLeft (8); L.amt[0].setBounds (row.removeFromLeft (230));
        a.removeFromTop (6); row = a.removeFromTop (26); L.lDest[1].setBounds (row.removeFromLeft (52)); L.dest[1].setBounds (row.removeFromLeft (190).reduced (0, 1)); row.removeFromLeft (8); L.amt[1].setBounds (row.removeFromLeft (230));
    }
    auto mid = r.removeFromLeft (520); r.removeFromLeft (8);
    { auto box = mid.removeFromTop (rowH); secSeq.setBounds (box); auto a = secSeq.inner() + box.getPosition();
      auto row = a.removeFromTop (26); seqOn.setBounds (row.removeFromLeft (60)); row.removeFromLeft (8); lSeqLen.setBounds (row.removeFromLeft (56)); seqLen.setBounds (row.removeFromLeft (90).reduced (0, 2)); row.removeFromLeft (10); lSeqDiv.setBounds (row.removeFromLeft (40)); seqDiv.setBounds (row.removeFromLeft (100).reduced (0, 1));
      a.removeFromTop (6); row = a.removeFromTop (26); lSeqDest.setBounds (row.removeFromLeft (40)); seqDest.setBounds (row.removeFromLeft (170).reduced (0, 1)); row.removeFromLeft (8); lSeqAmt.setBounds (row.removeFromLeft (56)); seqAmt.setBounds (row.removeFromLeft (200));
      a.removeFromTop (10); auto steps = a.withTrimmedBottom (16); const int w = steps.getWidth() / 16; for (int s = 0; s < 16; ++s) seqStep[s].setBounds (steps.getX() + s * w + 2, steps.getY(), w - 4, steps.getHeight()); }
    mid.removeFromTop (8);
    { auto box = mid; secEnv.setBounds (box); auto a = secEnv.inner() + box.getPosition();
      for (int d = 0; d < 3; ++d)
      {   // DEST 1 shares its row with ON; 2 and 3 line up beneath it
          if (d > 0) a.removeFromTop (5);
          auto row = a.removeFromTop (26); if (d == 0) envOn.setBounds (row.removeFromLeft (60)); else row.removeFromLeft (60); row.removeFromLeft (8);
          lEnvDest[d].setBounds (row.removeFromLeft (52)); envDest[d].setBounds (row.removeFromLeft (170).reduced (0, 1)); row.removeFromLeft (8); envAmt[d].setBounds (row.removeFromLeft (200));
      }
      auto stage = [&a] (Label& l, Slider& s, Label* lc, Slider* sc) { a.removeFromTop (5); auto row = a.removeFromTop (26); l.setBounds (row.removeFromLeft (56)); s.setBounds (row.removeFromLeft (196));
                                                                       if (sc != nullptr) { row.removeFromLeft (8); lc->setBounds (row.removeFromLeft (50)); sc->setBounds (row.removeFromLeft (186)); } };
      a.removeFromTop (3); stage (lEnvA, envA, &lEnvCurve[0], &envCurve[0]); stage (lEnvD, envD, &lEnvCurve[1], &envCurve[1]); stage (lEnvS, envS, nullptr, nullptr); stage (lEnvR, envR, &lEnvCurve[2], &envCurve[2]); }
    { secXY.setBounds (r); auto a = secXY.inner() + r.getPosition();
      auto row = a.removeFromTop (26); xyLatch.setBounds (row.removeFromLeft (90)); a.removeFromTop (8);
      auto padArea = a.removeFromTop (280); pad.setBounds (padArea.removeFromLeft (280)); padArea.removeFromLeft (10); lA.setBounds (padArea.removeFromLeft (26).withHeight (60)); xyA.setBounds (padArea.removeFromLeft (30));
      a.removeFromTop (10);
      for (int i = 0; i < 3; ++i) { row = a.removeFromTop (26); lXy[i].setBounds (row.removeFromLeft (24)); xyDest[i].setBounds (row.removeFromLeft (170).reduced (0, 1)); row.removeFromLeft (6); xyAmt[i].setBounds (row.removeFromLeft (150)); row.removeFromLeft (6); xyInv[i].setBounds (row.removeFromLeft (60)); a.removeFromTop (4); }
      row = a.removeFromTop (26); lGate.setBounds (row.removeFromLeft (44)); xyGate.setBounds (row.removeFromLeft (150).reduced (0, 1)); row.removeFromLeft (6); xyGateInv.setBounds (row.removeFromLeft (60));
      a.removeFromTop (12); lHint.setBounds (a.removeFromTop (90)); }
}


P6PanelEditor::P6PanelEditor (P6PanelProcessor& pr) : AudioProcessorEditor (pr), proc (pr), s (pr.apvts)
{
    setLookAndFeel (&look);
    addAndMakeVisible (canvas);
    auto add = [this] (auto&... c) { (canvas.addAndMakeVisible (c), ...); };

    // header
    add (cIn, cOut, cKeys, cChan, lIn, lOut, lKeys, lChan, lamp, cTuning, lTuning);
    cKeys.setTooltip ("A controller keyboard: its notes and wheels are passed on to the Prophet-6 (and trigger the MOD page's envelope)");
    cKeys.onChange = [this] { auto ins = MidiLink::inputs(); const int i = cKeys.getSelectedId() - 2; proc.link.openKeys (i >= 0 && i < ins.size() ? ins[i].identifier : String()); };
    // the synth's Alt Tuning global (GLOBALS > 4 on the module), reachable over MIDI as NRPN 1044
    { const char* names[] = { "Normal (12-tone equal)", "1  Harmonic Series", "2  Carlos Harmonic 12", "3  Meantone", "4  Quarter-tone (24-TET)", "5  19-tone equal", "6  31-tone equal",
                              "7  Pythagorean C", "8  Just A, 7-limit tritone", "9  3-5 Lattice A", "10  3-7 Lattice A", "11  Other Music 7-limit C", "12  Pelog / Slendro", "13  Yamaha Just Major C",
                              "14  Yamaha Just Minor C", "15  Partch 43-note", "16  Arabic 12-tone" };
      for (int i = 0; i < 17; ++i) cTuning.addItem (names[i], i + 1);
      cTuning.setSelectedId (1, dontSendNotification); cTuning.setTooltip ("The synth's Alt Tuning global. The synth does not report it, so the box shows the last setting made here.");
      cTuning.onChange = [this] { proc.link.sendNrpn (1044, cTuning.getSelectedId() - 1); }; }
    for (auto* l : { &lIn, &lOut, &lChan, &lBank, &lProg, &lName, &lTuning }) { l->setFont (MoogLook::silk (10.5f)); l->setJustificationType (Justification::centredRight); }
    for (int c = 1; c <= 16; ++c) cChan.addItem (String (c), c);
    cChan.setSelectedId (jlimit (1, 16, proc.link.channel), dontSendNotification);
    cChan.onChange = [this] { proc.link.channel = cChan.getSelectedId(); };
    cIn.onChange = [this] { portsChanged (true, false); }; cOut.onChange = [this] { portsChanged (false, true); };
    refreshPorts(); portPollCounter = proc.link.deviceListVersion();
    lamp.text = "OFF-LINE  (click to retry)";
    lamp.setMouseCursor (MouseCursor::PointingHandCursor);
    lamp.onClick = [this] { proc.link.retryConnect(); };

    // program strip
    add (cBank, sProg, lBank, lProg, lName, eName, bFetch, bSend, bWrite, bLoad, bSave, bLearn, lStatus, progStars);
    progStars.onChange = [this] (int n) { if (proc.libKey.isNotEmpty()) proc.setRating (proc.libKey, n); else proc.setRating (proc.currentBank * 100 + proc.currentProgram, n); };
    bLearn.setClickingTogglesState (true); bLearn.onClick = [this]
    {
        proc.learning = bLearn.getToggleState();
        if (proc.learning)
        {   // first the panel learns what it can by itself; afterwards LEARN stays on for moving controls on the synth
            proc.learnStatus = "LEARN: press FETCH once, then move one control at a time. Still to learn: " + proc.unknownList(); proc.haveLastDump = false;
            proc.startAutoLearn();
        }
        else { proc.stopAutoLearn(); proc.learnStatus = {}; }
        lStatus.setText (proc.learnStatus, dontSendNotification);
    };
    tVintage.setToggleState (proc.vintageState > 0, dontSendNotification);
    tVintage.setTooltip ("The synth's Vintage mode (a global setting, OS 1.6 and later): with it on, the SLOP knob sets how much the voices drift apart like an old Prophet. Shift-click to teach the panel again.");
    tVintage.onClick = [this]
    {
        if (proc.vintageNrpn < 0 || ModifierKeys::currentModifiers.isShiftDown())
        {   // the panel does not know the switch's MIDI number yet: learn it from the synth
            tVintage.setToggleState (proc.vintageState > 0, dontSendNotification);
            proc.vintageLearn = true; proc.vintageNrpn = -1;
            proc.lastUnknown = "VINTAGE: now switch Vintage once on the synth (hold BANK, press GLOBALS, item 4) - the panel learns it"; ++proc.uiVersion;
            return;
        }
        proc.setVintage (tVintage.getToggleState());
    };
    bLearn.setTooltip ("The panel tests the parameters it does not know yet by itself (the synth clicks a little while it reads), then you can move controls on the synth to teach it more. Click again to stop.");
    lStatus.setFont (MoogLook::silk (10.5f)); lStatus.setColour (Label::textColourId, MoogLook::accent); lStatus.setJustificationType (Justification::centredLeft);
    lStatus.setMinimumHorizontalScale (0.55f);   // squeeze long messages instead of cutting them off
    for (int b = 0; b < 10; ++b) cBank.addItem ((b < 5 ? "User " : "Factory ") + String (b % 5 + 1) + "  (" + String (b) + "xx)", b + 1);
    cBank.addItem ("Library", 11);   // shown while the sound came from the file library; - / + then step through that library list
    sProg.setSliderStyle (Slider::IncDecButtons); sProg.setRange (0, 99, 1); sProg.setTextBoxStyle (Slider::TextBoxLeft, false, 40, 20);
    sProg.textFromValueFunction = [] (double v) { return String (roundToInt (v)).paddedLeft ('0', 2); };
    cBank.onChange = [this] { if (updatingProgramRow) return; if (cBank.getSelectedId() == 11) { refreshProgramRow(); return; }
                              proc.selectProgram (cBank.getSelectedId() - 1, proc.libKey.isNotEmpty() ? 0 : roundToInt (sProg.getValue())); };
    sProg.onValueChange = [this]
    {
        if (updatingProgramRow) return;
        if (proc.libKey.isNotEmpty())
        {   // library mode: the number is the position in the library list the browser shows
            presetPage.ensureLib(); const int r = roundToInt (sProg.getValue()) - 1;
            if (r != presetPage.libIndex()) presetPage.select (r, true);
            return;
        }
        proc.selectProgram (cBank.getSelectedId() - 1, roundToInt (sProg.getValue()));
    };
    eName.setFont (MoogLook::silk (13.f)); eName.setInputRestrictions (P6::kNameLength);
    eName.setColour (TextEditor::backgroundColourId, MoogLook::panelLight); eName.setColour (TextEditor::textColourId, MoogLook::cream);
    eName.setColour (TextEditor::outlineColourId, MoogLook::rule);
    eName.onReturnKey = [this] { proc.setPatchName (eName.getText()); eName.giveAwayKeyboardFocus(); };
    eName.onFocusLost = [this] { proc.setPatchName (eName.getText()); };
    bFetch.onClick = [this] { proc.fetch(); };
    bSend.onClick = [this] { proc.sendAll(); };
    bWrite.onClick = [this] { writeClicked(); };
    bLoad.onClick = [this] { loadClicked(); };
    bSave.onClick = [this] { saveClicked(); };
    bFetch.setTooltip ("Read the synth's current edit buffer into the panel");
    bSend.setTooltip ("Push the panel to the synth's edit buffer (not stored)");
    bWrite.setTooltip ("Store the panel into the selected user bank slot");
    bSave.setTooltip ("Save into the library: a library sound goes back into its own file, any other sound asks for a name and category");

    // sections and controls
    add (secDist, secFx, secClock, secArp, secSeq, secLfo, secPM, secAT, secMisc, secOsc1, secSlop, secMix, secHP, secFEnv, secOsc2, secMix2, secLP, secAEnv, secGlide, secUnison, secProg, bFxAB);
    for (auto* c : std::initializer_list<Component*> { &cBank, &sProg, &lBank, &lProg, &lName, &eName, &bFetch, &bSend, &bWrite, &bLoad, &bSave, &bLearn, &lStatus, &progStars }) c->toFront (false);   // the program strip now sits inside a section added after it
    // sequencer page
    add (bSeqView, seqPage);
    seqPage.addAndMakeVisible (bSeqBack); seqPage.addAndMakeVisible (bSeqClear); seqPage.addAndMakeVisible (bSeqPlay2); seqPage.addAndMakeVisible (seqGrid);
    seqPage.addAndMakeVisible (sSeqLen); seqPage.addAndMakeVisible (lSeqLen); seqPage.addAndMakeVisible (lSeqHint);
    for (auto* c : std::initializer_list<Component*> { &bTrDown, &bTrUp, &bOctDown, &bOctUp, &bHumanize, &bReverse, &bDouble, &lTr }) seqPage.addAndMakeVisible (c);
    lTr.setFont (MoogLook::silk (10.5f)); lTr.setJustificationType (Justification::centredRight);
    bTrDown.onClick = [this] { proc.seqTranspose (-1); seqGrid.repaint(); }; bTrUp.onClick = [this] { proc.seqTranspose (1); seqGrid.repaint(); };
    bOctDown.onClick = [this] { proc.seqTranspose (-12); seqGrid.repaint(); }; bOctUp.onClick = [this] { proc.seqTranspose (12); seqGrid.repaint(); };
    bHumanize.onClick = [this] { proc.seqHumanize (12); seqGrid.repaint(); }; bHumanize.setTooltip ("Vary every note's velocity a little");
    bReverse.onClick = [this] { proc.seqReverse(); seqGrid.repaint(); }; bDouble.onClick = [this] { proc.seqDouble(); sSeqLen.setValue (proc.seqLength(), dontSendNotification); seqGrid.repaint(); };
    bDouble.setTooltip ("Repeat the sequence once more after itself (twice the length)");
    seqPage.addAndMakeVisible (keyboard); kbState.addListener (&kbListener);
    seqPage.setVisible (false); seqPage.setOpaque (false);
    bSeqView.onClick = [this] { showSeqPage (true); }; bSeqBack.onClick = [this] { showSeqPage (false); };
    bSeqClear.onClick = [this] { proc.clearSequence(); seqGrid.repaint(); };
    bSeqPlay2.setClickingTogglesState (true); bSeqPlay2.setColour (TextButton::buttonOnColourId, MoogLook::accent);
    bSeqPlay2.onClick = [this] { const bool on = bSeqPlay2.getToggleState(); proc.link.sendNrpn (1088, on ? 1 : 0); proc.seqPlaying = on; proc.playStep = -1; ++proc.uiVersion; };
    sSeqLen.setSliderStyle (Slider::IncDecButtons); sSeqLen.setRange (1, 64, 1); sSeqLen.setTextBoxStyle (Slider::TextBoxLeft, false, 40, 20);
    sSeqLen.onValueChange = [this] { if (roundToInt (sSeqLen.getValue()) != proc.seqLength()) proc.setSeqLength (roundToInt (sSeqLen.getValue())); seqGrid.repaint(); };
    lSeqLen.setFont (MoogLook::silk (10.5f)); lSeqLen.setJustificationType (Justification::centredRight);
    lSeqHint.setFont (MoogLook::silk (10.5f)); lSeqHint.setColour (Label::textColourId, MoogLook::inkDim);
    lSeqHint.setText ("click: add note  -  drag: move / right edge: tie  -  double-click: remove  -  wheel: scroll pitch  -  edits apply on restart, WRITE stores them", dontSendNotification);
    // preset browser page
    add (bPresets, presetPage); presetPage.setVisible (false);
    presetPage.addAndMakeVisible (keyboard2);
    for (auto* k : { &keyboard, &keyboard2, &keyboard3, &keyboard4 })
    {
        k->setAvailableRange (24, 96); k->setKeyWidth (22.f); k->setOctaveForMiddleC (3);
        k->setColour (MidiKeyboardComponent::whiteNoteColourId, MoogLook::cream); k->setColour (MidiKeyboardComponent::blackNoteColourId, Colour (0xff202020));
        k->setColour (MidiKeyboardComponent::keyDownOverlayColourId, MoogLook::accent.withAlpha (0.7f)); k->setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, MoogLook::accent.withAlpha (0.3f));
        k->setColour (MidiKeyboardComponent::textLabelColourId, Colour (0xff202020));
    }
    add (bMod, bShapes, modPage); modPage.setVisible (false); modPage.addAndMakeVisible (keyboard3);
    add (bEuclid, euclidPage); euclidPage.setVisible (false); euclidPage.addAndMakeVisible (keyboard4);
    bEuclid.onClick = [this] { showPage (&euclidPage); }; euclidPage.onBack = [this] { showPage (nullptr); };
    bEuclid.setColour (TextButton::buttonOnColourId, MoogLook::accent);
    bEuclid.setTooltip ("Lit: EUCLID is switched on and plays the synth whenever Logic runs (its ON button is on the page). Four Euclidean tracks that play the synth through the panel, in time with Logic: steps, beats, rotation, chance, swing, scale, chords, ratchets");
    bMod.onClick = [this] { modPage.setShapesVisible (false); modPage.refresh(); showPage (&modPage); };
    bShapes.onClick = [this] { modPage.setShapesVisible (true); modPage.refresh(); showPage (&modPage); }; bShapes.setTooltip ("The two drawable modulators (the SHAPES view of the MOD page)"); modPage.onBack = [this] { showPage (nullptr); }; bMod.setTooltip ("Extra modulation over MIDI: two LFOs, a step sequencer, an ADSR envelope and an X/Y pad, on top of the program");
    bPresets.onClick = [this]
    {   // restore the browser the way the project left it
        if (proc.browserBanks >= 1 && proc.browserBanks <= 3) presetPage.cBanks.setSelectedId (proc.browserBanks, dontSendNotification);
        if (proc.browserSearch != presetPage.search.getText()) presetPage.search.setText (proc.browserSearch, false);
        presetPage.goToCurrent(); showPage (&presetPage);
    }; presetPage.onBack = [this] { showPage (nullptr); };
    add (bSaveAs, bRandom, bMutate); for (auto& b : bMem) addAndMakeVisible (b); bSaveAs.onClick = [this] { saveAs.open(); };
    bRandom.onClick = [this] { proc.randomize(); }; bRandom.setTooltip ("A new random sound (playing settings such as volume, unison, arp, glide stay as they are)");
    bMutate.onClick = [this] { proc.mutate (0.35f); }; bMutate.setTooltip ("Nudge the current sound a little");
    for (int i = 0; i < P6PanelProcessor::kMemories; ++i)
    {   // A B C D: lit = the sound in use; bright letter = holds a sound; dim letter = empty (a click copies the sound in use there)
        auto& b = bMem[i]; b.setButtonText (String::charToString ((juce_wchar) ('A' + i))); b.setColour (TextButton::buttonOnColourId, MoogLook::accent);
        b.setConnectedEdges ((i > 0 ? Button::ConnectedOnLeft : 0) | (i < P6PanelProcessor::kMemories - 1 ? Button::ConnectedOnRight : 0));
        b.onClick = [this, i] { if (ModifierKeys::getCurrentModifiers().isAltDown()) proc.clearMemory (i); else proc.switchMemory (i); refreshMem(); };
    }
    refreshMem();
    // LFO FREQUENCY shows the note value while SYNC is on (and the plain number while it is off); a number typed in still sets the knob
    kLfoF.slider.textFromValueFunction = [this] (double v) { const int n = roundToInt (v); return proc.apvts.getRawParameterValue ("lfoSync")->load() > 0.5f ? String (P6::lfoSyncName (n)) + "  " + String (n) : String (n); };
    kLfoF.slider.valueFromTextFunction = [] (const String& t) { const auto x = t.trim(); return (x.containsChar (' ') ? x.fromLastOccurrenceOf (" ", false, false) : x).getDoubleValue(); };
    kLfoF.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 86, 17); kLfoF.slider.updateText();
    kLfoF.slider.setTooltip ("With SYNC on the knob picks a note value - one LFO cycle - in 18 steps (measured on the synth), shown with the knob's number");
    // LFO FREQUENCY shows the note value while SYNC is on (and the plain number while it is off); a number typed in still sets the knob
    kLfoF.slider.textFromValueFunction = [this] (double v) { const int n = roundToInt (v); return proc.apvts.getRawParameterValue ("lfoSync")->load() > 0.5f ? String (P6::lfoSyncName (n)) + "  " + String (n) : String (n); };
    kLfoF.slider.valueFromTextFunction = [] (const String& t) { return t.fromLastOccurrenceOf (" ", false, false).getDoubleValue(); };
    kLfoF.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 86, 17); kLfoF.slider.updateText();
    kLfoF.slider.setTooltip ("With SYNC on the knob picks a note value - one LFO cycle - in 18 steps (measured on the synth), shown with the knob's number");
    bSaveAs.setTooltip ("Name the sound, give it a category and store it into a user slot (WRITE + catalogue + .p6patch copy)");
    canvas.addChildComponent (saveAs);
    // the oscillator shape knobs morph triangle -> sawtooth -> pulse: say so in the value box instead of a bare number
    for (auto* k : { &kO1Shape, &kO2Shape })
    {
        k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 82, 17);
        k->slider.textFromValueFunction = [] (double v)
        {
            const int i = roundToInt (v);
            if (i <= 2) return String ("Triangle"); if (i >= 125 && i <= 129) return String ("Sawtooth"); if (i >= 252) return String ("Pulse");
            return i < 127 ? "Tri>Saw " + String (roundToInt (i * 100.0 / 127.0)) + "%" : "Saw>Pulse " + String (roundToInt ((i - 127) * 100.0 / 127.0)) + "%";
        };
        k->slider.updateText();
    }
    // osc 2 fine: a quarter-tone each way (manual), 127 = centre - show cents, not the raw 0-254
    kO2Fine.slider.textFromValueFunction = [] (double v) { const double c = (v - 127.0) * 50.0 / 127.0; return (c >= 0 ? "+" : "") + String (c, 1) + " ct"; };
    kO2Fine.slider.valueFromTextFunction = [] (const String& t) { return jlimit (0.0, 254.0, 127.0 + t.retainCharacters ("0123456789.-+").getDoubleValue() * 127.0 / 50.0); };
    kO2Fine.slider.updateText();
    // likewise pulse width: centre is a square wave, either end a narrow pulse
    for (auto* k : { &kO1PW, &kO2PW })
    {
        k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 82, 17);
        k->slider.textFromValueFunction = [] (double v) { const int i = roundToInt (v); return i >= 125 && i <= 130 ? String ("Square") : String (i) + (i < 127 ? "  narrow<" : "  >narrow"); };
        k->slider.updateText();
    }
    bFxAB.setClickingTogglesState (true); bFxAB.onClick = [this] { fxShowB = bFxAB.getToggleState(); bFxAB.setButtonText (fxShowB ? "EDIT B" : "EDIT A"); };
    bFxAB.setTooltip ("Which effect the Mix / Param / Sync controls edit (the synth's EFFECT button)");
    add (kO1Freq, kO1Shape, kO1PW, tO1Sync,
         kO2Freq, kO2Fine, kO2Shape, kO2PW, tO2Low, tO2Key,
         kMO1, kMSub, kMO2, kMNoise,
         tGlide, kGlide, kPB, cGlideMode,
         kVol, kPan, kDist, kSlop, tVintage, tUnison, cUnison, cKeyMode,
         kLPF, kLPR, kLPE, cLPKey, tLPVel, kHPF, kHPR, kHPE, cHPKey, tHPVel,
         kFA, kFD, kFS, kFR, kAAmt, kAA, kAD, kAS, kAR, tAVel,
         kLfoF, kLfoA, cLfoShape, tLfoSync, tLfo1, tLfo2, tLfoPW, tLfoAmp, tLfoLP, tLfoHP,
         kPMEnv, kPMO2, tPM1, tPMShape, tPMPW, tPMLP, tPMHP,
         kAT, tAT1, tAT2, tATLP, tATHP, tATVCA, tATLFO,
         tFx, cFxA, kFxAMix, kFxA1, kFxA2, tFxASync, cFxATime, cFxB, kFxBMix, kFxB1, kFxB2, tFxBSync, cFxBTime,
         kBPM, tArp, cArpMode, cArpOct, cArpVal,
         tSeq, kSeqLen, cSeqMode, cSeqPlay, bPlay);
    bPlay.setClickingTogglesState (true); bPlay.setColour (TextButton::buttonOnColourId, MoogLook::accent);
    bPlay.onClick = [this] { const bool on = bPlay.getToggleState(); proc.link.sendNrpn (1088, on ? 1 : 0); proc.seqPlaying = on; proc.playStep = -1; ++proc.uiVersion; };
    bPlay.setTooltip ("Start / stop the synth's sequencer (the module's PLAY button)");

    getConstrainer()->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
    setResizable (true, true);
    canvas.setSize (kBaseW, kBaseH);
    setSize (kBaseW, kBaseH);
    refreshProgramRow();
    startTimerHz (10);
    if (auto* f = std::getenv ("P6_LOAD")) proc.loadPatchFile (File (f));   // debugging aids: preload a patch, open on the sequencer page
    if (std::getenv ("P6_SEQ") != nullptr) showSeqPage (true);
    if (std::getenv ("P6_PRESETS") != nullptr) { proc.currentBank = 5; proc.currentProgram = 13; bPresets.onClick(); }
    if (std::getenv ("P6_SAVEAS") != nullptr) saveAs.open();
    if (std::getenv ("P6_MOD") != nullptr) { proc.mod.lfo[0].on = true; proc.mod.seq.on = true; bMod.onClick(); }
    if (std::getenv ("P6_EUCLID") != nullptr) bEuclid.onClick();
}
P6PanelEditor::~P6PanelEditor() { kbState.removeListener (&kbListener); setLookAndFeel (nullptr); }

void P6PanelEditor::refreshPorts()
{
    const auto curIn = proc.link.wantedIn(), curOut = proc.link.wantedOut(), curKeys = proc.link.wantedKeys();   // the chosen ports, so a device that comes back is shown selected again
    cIn.clear (dontSendNotification); cOut.clear (dontSendNotification); cKeys.clear (dontSendNotification);
    cIn.addItem ("(none)", 1); cOut.addItem ("(none)", 1); cKeys.addItem ("(none)", 1);
    int id = 2, selIn = 1, selOut = 1, selKeys = 1;
    for (auto& d : MidiLink::inputs()) { cIn.addItem (d.name, id); cKeys.addItem (d.name, id); if (d.identifier == curIn) selIn = id; if (d.identifier == curKeys) selKeys = id; ++id; }
    id = 2;
    for (auto& d : MidiLink::outputs()) { cOut.addItem (d.name, id); if (d.identifier == curOut) selOut = id; ++id; }
    cIn.setSelectedId (selIn, dontSendNotification); cOut.setSelectedId (selOut, dontSendNotification); cKeys.setSelectedId (selKeys, dontSendNotification);
}
void P6PanelEditor::portsChanged (bool inBox, bool outBox)
{   // only the box that was changed speaks: the other one may show "(none)" merely because its device is away for the moment,
    // and reading that as a choice made the panel forget the port (so the synth did not come back by itself)
    auto ins = MidiLink::inputs(), outs = MidiLink::outputs();
    const int i = cIn.getSelectedId() - 2, o = cOut.getSelectedId() - 2;
    proc.setPorts (inBox ? (i >= 0 && i < ins.size() ? ins[i].identifier : String()) : proc.link.wantedIn(), outBox ? (o >= 0 && o < outs.size() ? outs[o].identifier : String()) : proc.link.wantedOut());
}
void P6PanelEditor::showSeqPage (bool show) { showPage (show ? &seqPage : nullptr); }
void P6PanelEditor::showPage (Component* page)
{
    for (auto* c : canvas.getChildren())
        if (c != &cIn && c != &cOut && c != &cKeys && c != &cChan && c != &lIn && c != &lOut && c != &lKeys && c != &lChan && c != &lamp && c != &saveAs) c->setVisible (page == nullptr);
    seqPage.setVisible (page == &seqPage); presetPage.setVisible (page == &presetPage); modPage.setVisible (page == &modPage); euclidPage.setVisible (page == &euclidPage);
    if (page != nullptr) page->toFront (false);
    if (page == &seqPage) { sSeqLen.setValue (proc.seqLength(), dontSendNotification); seqGrid.repaint(); }
    if (page == nullptr) { tSeq.setVisible (false); kSeqLen.setVisible (false); cSeqMode.setVisible (false); cSeqPlay.setVisible (false); timerCallback(); }   // restore the visibility rules
}
void P6PanelEditor::refreshProgramRow()
{
    updatingProgramRow = true;
    if (proc.libKey.isNotEmpty()) presetPage.ensureLib();
    if (proc.libKey.isNotEmpty())
    {   // a library sound: BANK names the library list, PROGRAM is the position in it
        const auto& c = presetPage.currentCategory;
        cBank.changeItemText (11, "Library: " + (c == "All" ? String ("all files") : c));
        cBank.setSelectedId (11, dontSendNotification);
        const int n = jmax (1, (int) presetPage.rows.size());
        sProg.setRange (1, n, 1); sProg.setValue (jmax (1, presetPage.libIndex() + 1), dontSendNotification);
        progStars.value = proc.rating (proc.libKey);
    }
    else
    {
        sProg.setRange (0, 99, 1);
        cBank.setSelectedId (proc.currentBank + 1, dontSendNotification);
        sProg.setValue (proc.currentProgram, dontSendNotification);
        progStars.value = proc.rating (proc.currentBank * 100 + proc.currentProgram);
    }
    if (! eName.hasKeyboardFocus (true)) eName.setText (proc.patchName(), dontSendNotification);
    progStars.repaint();
    updatingProgramRow = false;
}
void P6PanelEditor::refreshMem()
{   // A B C D: lit = the sound in use; bright letter = holds a sound; dim letter = empty
    for (int i = 0; i < P6PanelProcessor::kMemories; ++i)
    {
        auto& b = bMem[i]; const bool act = i == proc.memActive, used = act || proc.mem[i].used;
        if (b.getToggleState() != act) b.setToggleState (act, dontSendNotification);
        const auto col = used ? MoogLook::cream : MoogLook::inkDim.withAlpha (0.6f); if (b.findColour (TextButton::textColourOffId) != col) { b.setColour (TextButton::textColourOffId, col); b.repaint(); }
        if (const auto t = proc.memoryText (i); b.getTooltip() != t) b.setTooltip (t);
    }
}
void P6PanelEditor::timerCallback()
{
    if (const bool sy = proc.apvts.getRawParameterValue ("lfoSync")->load() > 0.5f; sy != lfoSyncShown) { lfoSyncShown = sy; kLfoF.slider.updateText(); }   // SYNC switched: the knob shows note values / numbers
    if (const bool on = proc.euclid.patch.on; bEuclid.getToggleState() != on) bEuclid.setToggleState (on, dontSendNotification);   // lit while the EUCLID page is switched on
    const bool on = proc.link.isOnline();
    const String t = on ? "ON-LINE  Prophet-6 v" + proc.link.synthVersion() : "OFF-LINE  (click to retry)";
    if (lamp.on != on || lamp.text != t) { lamp.on = on; lamp.text = t; lamp.repaint(); }
    // with Sync on, a delay's Param 1 is a note division: show the Time chooser instead of the knob
    if (modPage.isVisible()) { modPage.repaint(); if (! modPage.pad.down && proc.mod.autoXY && ! modPage.pad.latch) { modPage.pad.x = proc.mod.autoX; modPage.pad.y = proc.mod.autoY; }
        const bool h = proc.hostPlaying.load() && proc.hostBpm.load() > 0; modPage.lClock.setText (h ? "clock: Logic, " + String (proc.hostBpm.load(), 1) + " bpm" : "clock: internal (Logic's when it plays)", dontSendNotification); modPage.sBpm.setEnabled (! h); }
    if (! seqPage.isVisible() && ! presetPage.isVisible() && ! modPage.isVisible() && ! euclidPage.isVisible())
    { const bool sa = proc.apvts.getRawParameterValue ("fx1Sync")->load() > 0.5f, sb = proc.apvts.getRawParameterValue ("fx2Sync")->load() > 0.5f;
      const bool a = ! fxShowB, b = fxShowB;   // the knob set follows the EDIT A / EDIT B button, as on the synth
      kFxAMix.setVisible (a); kFxA2.setVisible (a); tFxASync.setVisible (a); kFxA1.setVisible (a && ! sa); cFxATime.setVisible (a && sa);
      kFxBMix.setVisible (b); kFxB2.setVisible (b); tFxBSync.setVisible (b); kFxB1.setVisible (b && ! sb); cFxBTime.setVisible (b && sb);
      // name the two parameter knobs after what they do in the chosen effect (Prophet-6 manual, Effects table)
      auto names = [] (int type) -> std::pair<const char*, const char*>
      {
          switch (type)
          {
              case 1: case 2:            return { "Time", "Feedback" };      // BBD / digital delay
              case 3: case 4: case 5: case 6: return { "Rate", "Depth" };   // chorus, phasers 1-3
              case 7:                    return { "Frequency", "Tracking" }; // ring mod: mod frequency, low-note pitch tracking on/off
              case 8: case 9:            return { "Rate", "Depth" };         // flangers (the manual's table leaves them out)
              case 10: case 11: case 12: return { "Time", "Reflections" };   // hall, room, plate: reverb time, early reflections
              case 13:                   return { "Decay", "Tone" };         // spring
              default:                   return { "Param 1", "Param 2" };
          }
      };
      const int ta = roundToInt (proc.apvts.getRawParameterValue ("fx1Type")->load()), tb = roundToInt (proc.apvts.getRawParameterValue ("fx2Type")->load());
      auto setName = [] (ui::Knob& k, const char* n) { const auto t = String (n).toUpperCase(); if (k.label.getText() != t) k.label.setText (t, dontSendNotification); };
      setName (kFxA1, names (ta).first); setName (kFxA2, names (ta).second); setName (kFxB1, names (tb).first); setName (kFxB2, names (tb).second); }
    refreshMem();
    if (const int v = proc.uiVersion.load(); v != lastUiVersion) { lastUiVersion = v; refreshProgramRow(); tVintage.setToggleState (proc.vintageState > 0, dontSendNotification); if (presetPage.isVisible() && ! proc.scanning) presetPage.syncCurrent(); if (modPage.isVisible()) modPage.refresh();
        if (proc.altTuning >= 0 && cTuning.getSelectedId() != proc.altTuning + 1) cTuning.setSelectedId (proc.altTuning + 1, dontSendNotification);
        for (auto* b : { &bPlay, &bSeqPlay2, &presetPage.bPlay }) { b->setToggleState (proc.seqPlaying, dontSendNotification); b->setButtonText (proc.seqPlaying ? "STOP" : "PLAY"); }
        if (seqPage.isVisible()) { sSeqLen.setValue (proc.seqLength(), dontSendNotification); seqGrid.repaint(); }
        if (proc.scanning) { presetPage.lCount.setText ("scanning program " + String (proc.scanSlot).paddedLeft ('0', 3) + " of 999 ...", dontSendNotification); presetPage.bScan.setButtonText ("STOP SCAN"); }
        else if (presetPage.bScan.getButtonText() != "SCAN NAMES") { presetPage.bScan.setButtonText ("SCAN NAMES"); presetPage.cBanks.onChange(); /* the scan is over: the rows take the names it found */ } lStatus.setText (proc.learning ? proc.learnStatus : (proc.lastUnknown.isNotEmpty() ? proc.lastUnknown : proc.knownOffsets() < (int) P6::params().size() ? "still to learn: " + proc.unknownList() : String()), dontSendNotification); lStatus.setTooltip (lStatus.getText()); }
    if (proc.link.deviceListVersion() != portPollCounter && ! cIn.isPopupActive() && ! cOut.isPopupActive() && ! cKeys.isPopupActive())
    {   // the Mac's MIDI device list changed (the link is told by the system): the port boxes follow
        portPollCounter = proc.link.deviceListVersion(); refreshPorts();
    }
}
void P6PanelEditor::writeClicked()
{
    if (proc.libKey.isNotEmpty()) { saveAs.open(); return; }   // a library sound: SAVE AS lets you pick the user slot
    const int bank = cBank.getSelectedId() - 1, prog = roundToInt (sProg.getValue());
    if (bank > 4) { AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Write", "Factory banks are read-only. Choose a User bank (0xx - 4xx)."); return; }
    AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Write to the Prophet-6",
        "Overwrite program " + String (bank) + String (prog).paddedLeft ('0', 2) + " on the synth with \"" + proc.patchName() + "\"?",
        "Write", "Cancel", this, ModalCallbackFunction::create ([this, safe = Component::SafePointer<P6PanelEditor> (this), bank, prog] (int r) { if (safe == nullptr) return; if (r == 1) proc.writeTo (bank, prog); }));
}
void P6PanelEditor::loadClicked()
{
    chooser = std::make_unique<FileChooser> ("Load Prophet-6 patch", File::getSpecialLocation (File::userDocumentsDirectory), "*.syx;*.p6patch");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, safe = Component::SafePointer<P6PanelEditor> (this)] (const FileChooser& fc)
    { if (safe == nullptr) return;
        auto f = fc.getResult(); if (f.existsAsFile() && ! proc.loadPatchFile (f))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Load", "No Prophet-6 program found in that file.");
    });
}
void P6PanelEditor::saveClicked()
{
    proc.setPatchName (eName.getText());
    if (proc.libKey.isEmpty()) { saveAs.open(); return; }   // a synth sound: SAVE AS names it and files it under a category, so the library list shows it
    // a library sound: SAVE puts the changes back into its own file (or next to it under a new name) - nothing lands anywhere else
    const auto ls = proc.librarySave(); const String cat = ls.category, oldName = ls.oldName, name = ls.name; const File f = ls.file;
    const String q = ls.same ? "Save your changes into \"" + oldName + "\" (" + cat + ")?"
                   : ls.replacesOther ? "A sound called \"" + f.getFileNameWithoutExtension() + "\" already exists in " + cat + ". Replace it with this one? (\"" + oldName + "\" stays as it is)"   // never over another sound without saying so
                                      : "Save as a new sound \"" + name + "\" in " + cat + "? (\"" + oldName + "\" stays as it is)";
    AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Save", q, ls.replacesOther ? "Replace" : "Save", "Cancel", this,
        ModalCallbackFunction::create ([this, safe = Component::SafePointer<P6PanelEditor> (this), f, cat] (int r)
        { if (safe == nullptr) return;
            if (r != 1) return;
            proc.savePatchFile (f);
            proc.libKey = "lib:" + cat + "/" + f.getFileNameWithoutExtension();
            presetPage.cBanks.onChange(); refreshProgramRow();
            lStatus.setText ("saved: " + cat + " / " + f.getFileNameWithoutExtension(), dontSendNotification);
        }));
}

void P6PanelEditor::paint (Graphics& g)
{
    g.fillAll (MoogLook::panel);
    g.addTransform (AffineTransform::scale ((float) getWidth() / (float) kBaseW));
    auto top = Rectangle<int> (0, 0, kBaseW, 34);
    g.setColour (MoogLook::ink); g.setFont (MoogLook::silk (17.f, true));
    g.drawText ("PROPHET 6 PANEL", top.withTrimmedLeft (16), Justification::centredLeft);
    g.setColour (MoogLook::rule); g.drawLine (12.f, 33.5f, (float) kBaseW - 12.f, 33.5f, 1.f);
}
void P6PanelEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    canvas.setTransform (AffineTransform::scale (scale));
    canvas.setBounds (0, 0, kBaseW, kBaseH);

    // header right side: lamp, ports, channel
    { auto h = Rectangle<int> (590, 6, kBaseW - 602, 22);
      cChan.setBounds (h.removeFromRight (56)); lChan.setBounds (h.removeFromRight (30));
      h.removeFromRight (10); cKeys.setBounds (h.removeFromRight (150)); lKeys.setBounds (h.removeFromRight (44));
      h.removeFromRight (10); cOut.setBounds (h.removeFromRight (180)); lOut.setBounds (h.removeFromRight (64));
      h.removeFromRight (10); cIn.setBounds (h.removeFromRight (180)); lIn.setBounds (h.removeFromRight (56));
      h.removeFromRight (16); lamp.setBounds (h.removeFromRight (180)); }
    { auto h = Rectangle<int> (250, 6, 330, 22); lTuning.setBounds (h.removeFromLeft (64)); cTuning.setBounds (h.removeFromLeft (250)); }

    // helpers: knobs are 80 x 88, toggles 22 high, choosers 14 (label) + 22 (box)
    const int kw = 80, kh = 88, th = 22;
    auto knobs  = [&] (Rectangle<int>& a, std::initializer_list<Knob*> ks) { for (auto* k : ks) k->setBounds (a.removeFromLeft (kw).withHeight (kh)); };
    auto toggle = [&] (Rectangle<int>& a, Toggle& t, int w) { t.setBounds (a.removeFromLeft (w).withHeight (kh).withTrimmedTop (14).withHeight (th).translated (0, 20)); };
    auto choose = [&] (Rectangle<int>& a, Chooser& c, int w) { c.setBounds (a.removeFromLeft (w).reduced (4, 0).withHeight (40).translated (0, 14)); };
    auto grid23 = [&] (Rectangle<int>& a, int w, std::initializer_list<Toggle*> ts) {   // two rows of up to three toggles, like the synth's destination buttons
        auto d = a.removeFromLeft (w).withHeight (kh).withTrimmedTop (12); const int cw = w / 3; int i = 0;
        for (auto* t : ts) { auto row = (i < 3 ? d : d.translated (0, 34)); t->setBounds (row.withX (d.getX() + (i % 3) * cw).withWidth (cw).withHeight (th)); ++i; } };
    auto inner = [] (Section& s) { return s.inner() + s.getPosition(); };

    auto r = Rectangle<int> (0, 0, kBaseW, kBaseH).withTrimmedTop (38).reduced (6);

    // ---- row 1: distort, effects, clock, arpeggiator, sequencer
    auto row = r.removeFromTop (140);
    secDist.setBounds (row.removeFromLeft (110));
    { auto a = inner (secDist); knobs (a, { &kDist }); }
    secFx.setBounds (row.removeFromLeft (600));
    { auto a = inner (secFx);
      auto c = a.removeFromLeft (100); tFx.setBounds (c.removeFromTop (th).translated (0, 8)); c.removeFromTop (22); bFxAB.setBounds (c.removeFromTop (24).reduced (4, 0));
      auto t = a.removeFromLeft (150); cFxA.setBounds (t.removeFromTop (46).reduced (4, 0).withHeight (36)); cFxB.setBounds (t.removeFromTop (46).reduced (4, 0).withHeight (36));
      a.removeFromLeft (10);
      auto k = a; knobs (k, { &kFxAMix }); { auto p = k.removeFromLeft (kw); kFxA1.setBounds (p.withHeight (kh)); cFxATime.setBounds (p.reduced (2, 0).withHeight (40).translated (0, 14)); } knobs (k, { &kFxA2 }); toggle (k, tFxASync, 70);
      k = a; knobs (k, { &kFxBMix }); { auto p = k.removeFromLeft (kw); kFxB1.setBounds (p.withHeight (kh)); cFxBTime.setBounds (p.reduced (2, 0).withHeight (40).translated (0, 14)); } knobs (k, { &kFxB2 }); toggle (k, tFxBSync, 70); }
    secClock.setBounds (row.removeFromLeft (230));
    { auto a = inner (secClock); knobs (a, { &kBPM }); choose (a, cArpVal, 110); }
    secArp.setBounds (row.removeFromLeft (320));
    { auto a = inner (secArp); toggle (a, tArp, 90); choose (a, cArpOct, 110); choose (a, cArpMode, 120); }
    secSeq.setBounds (row);
    seqPage.setBounds (r.withTop (secDist.getY()).withBottom (kBaseH - 6));
    presetPage.setBounds (seqPage.getBounds()); modPage.setBounds (seqPage.getBounds()); euclidPage.setBounds (seqPage.getBounds());
    { auto kb = euclidPage.getLocalBounds().reduced (8).removeFromBottom (76); keyboard4.setBounds (kb.withSizeKeepingCentre (jmin (kb.getWidth(), 42 * 22), kb.getHeight())); }
    { auto kb = modPage.getLocalBounds().reduced (8).removeFromBottom (76); keyboard3.setBounds (kb.withSizeKeepingCentre (jmin (kb.getWidth(), 42 * 22), kb.getHeight())); }
    saveAs.setBounds (0, 0, kBaseW, kBaseH);
    { auto kb = presetPage.getLocalBounds().reduced (8).removeFromBottom (76); keyboard2.setBounds (kb.withSizeKeepingCentre (jmin (kb.getWidth(), 42 * 22), kb.getHeight())); }
    { auto p = seqPage.getLocalBounds().reduced (8); auto bar = p.removeFromTop (28);
      bSeqBack.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (16); bSeqPlay2.setBounds (bar.removeFromLeft (80)); bar.removeFromLeft (16);
      lSeqLen.setBounds (bar.removeFromLeft (60)); sSeqLen.setBounds (bar.removeFromLeft (100).reduced (0, 2)); bar.removeFromLeft (16); bSeqClear.setBounds (bar.removeFromLeft (80));
      bar.removeFromLeft (20); lTr.setBounds (bar.removeFromLeft (80)); for (auto* b : { &bOctDown, &bTrDown, &bTrUp, &bOctUp }) { b->setBounds (bar.removeFromLeft (44)); bar.removeFromLeft (4); }
      bar.removeFromLeft (12); bHumanize.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (6); bReverse.setBounds (bar.removeFromLeft (80)); bar.removeFromLeft (6); bDouble.setBounds (bar.removeFromLeft (80));
      bar.removeFromLeft (20); lSeqHint.setBounds (bar);
      p.removeFromTop (12); auto kb = p.removeFromBottom (76); p.removeFromBottom (8); seqGrid.setBounds (p);
      keyboard.setBounds (kb.withSizeKeepingCentre (jmin (kb.getWidth(), 42 * 22), kb.getHeight())); }
    { auto a = inner (secSeq); a.removeFromLeft (10); bPlay.setBounds (a.removeFromLeft (90).withHeight (kh).withTrimmedTop (14).withHeight (34).translated (0, 14).reduced (4, 0)); a.removeFromLeft (10);
      bSeqView.setBounds (a.removeFromLeft (90).withHeight (kh).withTrimmedTop (14).withHeight (34).translated (0, 14).reduced (4, 0)); a.removeFromLeft (10);
      bEuclid.setBounds (a.removeFromLeft (90).withHeight (kh).withTrimmedTop (14).withHeight (34).translated (0, 14).reduced (4, 0));
      tSeq.setVisible (false); kSeqLen.setVisible (false); cSeqMode.setVisible (false); cSeqPlay.setVisible (false); }   // the synth has no such controls; they came from the MIDI table only

    // ---- row 2: lfo, poly mod, aftertouch, misc parameters
    row = r.removeFromTop (160);
    secLfo.setBounds (row.removeFromLeft (562));
    { auto a = inner (secLfo); knobs (a, { &kLfoF }); toggle (a, tLfoSync, 70); choose (a, cLfoShape, 100); knobs (a, { &kLfoA }); grid23 (a, 216, { &tLfo1, &tLfo2, &tLfoPW, &tLfoAmp, &tLfoLP, &tLfoHP }); }
    secPM.setBounds (row.removeFromLeft (392));
    { auto a = inner (secPM); knobs (a, { &kPMEnv, &kPMO2 }); grid23 (a, 216, { &tPM1, &tPMShape, &tPMPW, &tPMLP, &tPMHP }); }
    secAT.setBounds (row.removeFromLeft (312));
    { auto a = inner (secAT); knobs (a, { &kAT }); grid23 (a, 216, { &tAT1, &tAT2, &tATLFO, &tATVCA, &tATLP, &tATHP }); }
    secMisc.setBounds (row);
    { auto a = inner (secMisc); knobs (a, { &kPan }); choose (a, cKeyMode, 66); knobs (a, { &kPB, &kVol }); }

    // ---- row 3: osc 1, slop, mixer (osc 1 / osc 2), high-pass, filter envelope
    row = r.removeFromTop (160);
    secOsc1.setBounds (row.removeFromLeft (360));
    { auto a = inner (secOsc1); knobs (a, { &kO1Freq }); toggle (a, tO1Sync, 80); knobs (a, { &kO1Shape, &kO1PW }); }
    secSlop.setBounds (row.removeFromLeft (100));
    { auto a = inner (secSlop); knobs (a, { &kSlop }); auto v = inner (secSlop); tVintage.setBounds (v.getX(), v.getY() + kh + 6, v.getWidth(), th); }
    secMix.setBounds (row.removeFromLeft (180));
    { auto a = inner (secMix); knobs (a, { &kMO1, &kMO2 }); }
    secHP.setBounds (row.removeFromLeft (450));
    { auto a = inner (secHP); knobs (a, { &kHPF, &kHPR, &kHPE }); toggle (a, tHPVel, 70); choose (a, cHPKey, 100); }
    secFEnv.setBounds (row);
    { auto a = inner (secFEnv); a.removeFromLeft (150); knobs (a, { &kFA, &kFD, &kFS, &kFR }); }

    // ---- row 4: osc 2, mixer (sub / noise), low-pass, amplifier envelope
    row = r.removeFromTop (160);
    secOsc2.setBounds (row.removeFromLeft (460));
    { auto a = inner (secOsc2); knobs (a, { &kO2Freq, &kO2Fine, &kO2Shape, &kO2PW }); auto c = a.removeFromLeft (100).withHeight (kh).withTrimmedTop (14);
      tO2Low.setBounds (c.removeFromTop (th).translated (0, 8)); tO2Key.setBounds (c.removeFromTop (th).translated (0, 22)); }
    secMix2.setBounds (row.removeFromLeft (180));
    { auto a = inner (secMix2); knobs (a, { &kMSub, &kMNoise }); }
    secLP.setBounds (row.removeFromLeft (450));
    { auto a = inner (secLP); knobs (a, { &kLPF, &kLPR, &kLPE }); toggle (a, tLPVel, 70); choose (a, cLPKey, 100); }
    secAEnv.setBounds (row);
    { auto a = inner (secAEnv); knobs (a, { &kAAmt }); toggle (a, tAVel, 70); knobs (a, { &kAA, &kAD, &kAS, &kAR }); }

    // ---- row 5: glide, unison, program (the synth's bottom row: display, bank/program, write)
    row = r;
    secGlide.setBounds (row.removeFromLeft (300));
    { auto a = inner (secGlide); toggle (a, tGlide, 80); knobs (a, { &kGlide }); choose (a, cGlideMode, 130); }
    secUnison.setBounds (row.removeFromLeft (220));
    { auto a = inner (secUnison); toggle (a, tUnison, 90); choose (a, cUnison, 110); }
    secProg.setBounds (row);
    { auto a = inner (secProg); auto p = a.removeFromTop (30); a.removeFromTop (6);
      lBank.setBounds (p.removeFromLeft (44)); cBank.setBounds (p.removeFromLeft (170).reduced (0, 3)); p.removeFromLeft (10);
      lProg.setBounds (p.removeFromLeft (70)); sProg.setBounds (p.removeFromLeft (110).reduced (0, 2)); p.removeFromLeft (10);
      lName.setBounds (p.removeFromLeft (48)); eName.setBounds (p.removeFromLeft (240).reduced (0, 3)); p.removeFromLeft (12); progStars.setBounds (p.removeFromLeft (110));
      bWrite.setBounds (p.removeFromRight (80).reduced (2, 3)); bSend.setBounds (p.removeFromRight (80).reduced (2, 3)); bFetch.setBounds (p.removeFromRight (80).reduced (2, 3));
      auto q = a.removeFromTop (26); bPresets.setBounds (q.removeFromLeft (90).reduced (2, 2)); q.removeFromLeft (6); bSaveAs.setBounds (q.removeFromLeft (90).reduced (2, 2)); q.removeFromLeft (6); bMod.setBounds (q.removeFromLeft (60).reduced (2, 2)); q.removeFromLeft (6); bShapes.setBounds (q.removeFromLeft (76).reduced (2, 2)); q.removeFromLeft (16);
      bRandom.setBounds (q.removeFromLeft (80).reduced (2, 2)); q.removeFromLeft (6); bMutate.setBounds (q.removeFromLeft (80).reduced (2, 2)); q.removeFromLeft (16);
      for (auto& b : bMem) b.setBounds (q.removeFromLeft (45).reduced (0, 2)); q.removeFromLeft (10); bLearn.setBounds (q.removeFromRight (80).reduced (2, 2)); q.removeFromRight (16);
      bSave.setBounds (q.removeFromRight (70).reduced (2, 2)); bLoad.setBounds (q.removeFromRight (70).reduced (2, 2)); q.removeFromRight (10); lStatus.setBounds (q); }
}
