#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "MoogLook.h"
#include "P6Presets.h"
#include "EuclidPage.h"

namespace ui
{
using APVTS = juce::AudioProcessorValueTreeState;

struct Knob : public juce::Component
{
    Knob (APVTS& s, const juce::String& id, const juce::String& text, const juce::String& suffix = {});
    void resized() override;
    juce::Slider slider; juce::Label label;
    std::unique_ptr<APVTS::SliderAttachment> att;
};
struct Toggle : public juce::Component
{
    Toggle (APVTS& s, const juce::String& id, const juce::String& text);
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::ToggleButton button;
    std::unique_ptr<APVTS::ButtonAttachment> att;
};
struct Chooser : public juce::Component
{
    Chooser (APVTS& s, const juce::String& id, const juce::String& text);
    void resized() override;
    juce::ComboBox box; juce::Label label;
    std::unique_ptr<APVTS::ComboBoxAttachment> att;
};
struct Section : public juce::Component
{
    explicit Section (juce::String t) : title (std::move (t)) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
    juce::Rectangle<int> inner() const { return getLocalBounds().reduced (8).withTrimmedTop (18); }
    juce::String title;
};
/** Piano roll of the program's sequence: 64 steps across, pitch up/down, the six note slots of a step drawn as blocks in that
    column. Click an empty spot to add a note, drag a block up/down to change its pitch, double-click to remove it; the lane
    underneath holds the step velocity (drag). Mouse wheel scrolls the pitch range. */
struct SeqGrid : public juce::Component
{
    explicit SeqGrid (P6PanelProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int  colOf (int x) const;  int noteOf (int y) const;
    juce::Rectangle<int> rollArea() const  { return getLocalBounds().withTrimmedLeft (kKeyW).withTrimmedTop (kHead).withTrimmedBottom (kVelH + 8); }
    juce::Rectangle<int> velArea() const   { return getLocalBounds().withTrimmedLeft (kKeyW).removeFromBottom (kVelH); }
    juce::Rectangle<int> noteRect (int step, int note) const;
    int  trackAt (int step, int note) const;   // which of the six slots sounds this note at this step, or -1
    int  startOf (int track, int step) const; int lengthOf (int track, int step) const;
    int  freeTrack (int step) const;
    P6PanelProcessor& proc;
    int topNote = 84, dragTrack = -1, dragStep = -1, dragStartY = 0, dragValue = 0, dragLen = 1; bool dragVel = false, dragResize = false;
    static constexpr int kKeyW = 44, kHead = 18, kVelH = 70, kRowH = 11;
};
/** Preset browser: the factory list filtered by category and search text; a click sends the program to the synth. */
/** Preset browser, Arturia style: a category column on the left, the matching programs on the right. Sources are the
    synth's factory banks, its user banks, or the panel's own file library (unlimited). A click sends the sound to the synth. */
struct PresetBrowser : public juce::Component, private juce::ListBoxModel
{
    explicit PresetBrowser (P6PanelProcessor& p);
    void resized() override; void paint (juce::Graphics&) override;
    void rebuild();                                   // apply source / category / search / stars
    void goToCurrent();                               // on opening: select the synth's program and its category
    void syncCurrent();                               // show + highlight the program the synth is on
    void refreshCategories();                         // the left column (with counts) for the current source
    void step (int delta);                            // previous / next in the list
    void select (int row, bool send);
    void ensureLib();                                 // make sure the list shows the library row the panel is on
    int  libIndex() const;                            // that row (-1 = not in the list)
    std::function<void()> onBack;
    P6PanelProcessor& proc;
    juce::TextEditor search, eNote; juce::Label lNote { {}, "MY NOTE" };
    void showSelected();                              // the line under the list: the selected sound, and its note ready to edit
    void storeNote();                                 // what was typed becomes the selected sound's note
    static constexpr int kNameW = 230;                // the name column; the note follows it
    juce::ComboBox cBanks, cStars; juce::Label lStars { {}, "STARS" }, lSource { {}, "SOURCE" };
    juce::TextButton bBack { "PANEL" }, bPrev { "<" }, bNext { ">" }, bScan { "SCAN NAMES" }, bPlay { "PLAY" }; juce::Label lCount, lDesc, lNow, lSearch { {}, "SEARCH" };
    struct Row { juce::String key, category, name, description; int slot = -1; juce::File file; bool p5 = false, user = false; int stars = 0; juce::String note; };
    std::vector<Row> rows, allRows;                   // allRows: the current source before category / search / stars filtering
    juce::String currentCategory { "All" };           // "All", "My sounds", "Prophet-5 classics", or a category name
    // the category column is a second list box with its own tiny model
    struct CatModel : public juce::ListBoxModel
    {
        PresetBrowser& b; explicit CatModel (PresetBrowser& o) : b (o) {}
        int getNumRows() override { return (int) b.cats.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override { if (row >= 0 && row < (int) b.cats.size()) { b.currentCategory = b.cats[(size_t) row].first; b.proc.browserCategory = b.currentCategory; b.rebuild(); } }
    } catModel { *this };
    std::vector<std::pair<juce::String, int>> cats;   // name, count
    juce::ListBox catList { "categories", &catModel }, list { "presets", this };
    juce::Rectangle<int> starsRect (int h) const { return { 8, 0, 86, h }; }
    int selectedEntry = -1;
private:
    void loadSource();                                // allRows from the chosen source
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override;
    void selectedRowsChanged (int) override {}
};
/** Five clickable stars. */
struct Stars : public juce::Component
{
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { const int n = juce::jlimit (1, 5, 1 + e.x * 5 / juce::jmax (1, getWidth())); if (onChange) onChange (n == value ? 0 : n); }
    int value = 0; std::function<void (int)> onChange;
    static void draw (juce::Graphics&, juce::Rectangle<int>, int value, bool dim);
};
struct Lamp : public juce::Component
{
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
    bool on = false; juce::String text; std::function<void()> onClick;
};
/** The X/Y pad of the MOD page: drag a finger (the mouse) around, two parameters follow. */
struct XYPad : public juce::Component
{
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { update (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { update (e, true); }
    void mouseUp (const juce::MouseEvent&) override { if (latch) return; down = false; if (onChange) onChange (x, y, false); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { down = false; if (onChange) onChange (x, y, false); repaint(); }   // latched: a double-click lets go
    bool latch = false;
    void update (const juce::MouseEvent& e, bool d) { x = juce::jlimit (-1.f, 1.f, (float) e.x / (float) getWidth() * 2.f - 1.f); y = juce::jlimit (-1.f, 1.f, 1.f - (float) e.y / (float) getHeight() * 2.f); down = d; if (onChange) onChange (x, y, d); repaint(); }
    float x = 0.f, y = 0.f; bool down = false; std::function<void (float, float, bool)> onChange;
};
/** A drawable curve (Serum style): click to add a point, drag a point to move it, drag the line between two points to bend it,
    double-click a point to remove it, right-click for ready-made shapes. Edits proc.mod.shape[idx] directly. */
struct ShapeEditor : public juce::Component
{
    ShapeEditor (P6PanelProcessor& p, int index) : proc (p), idx (index) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override; void mouseDrag (const juce::MouseEvent&) override; void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override; void mouseMove (const juce::MouseEvent&) override { repaint(); }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10.f, 10.f).withTrimmedBottom (14.f); }
    juce::Point<float> toScreen (float x, float y) const; int pointAt (juce::Point<float>) const; int segmentAt (float x) const; void showMenu();
    Mod::Shape& data() const { return proc.mod.shape[idx]; }
    P6PanelProcessor& proc; int idx; int dragPoint = -1, dragSeg = -1; float dragStartBend = 0.f, dragStartY = 0.f;
};
/** The MOD page: two LFOs, a 16-step sequencer, an ADSR envelope and the X/Y pad, all sent to the synth as NRPNs on top of the program;
    SHAPES switches the same page to the two drawable modulators. */
struct ModPage : public juce::Component
{
    explicit ModPage (P6PanelProcessor& p);
    void resized() override; void paint (juce::Graphics&) override;
    void refresh();                                   // widgets from proc.mod
    P6PanelProcessor& proc;
    std::function<void()> onBack;
    juce::TextButton bBack { "PANEL" }, bPrev { "<" }, bNext { ">" }; juce::Slider sBpm; juce::Label lBpm { {}, "BPM" }, lClock, lNow;
    struct LfoRow { juce::ToggleButton on { "ON" }; juce::ComboBox shape, div, dest[2]; juce::ToggleButton sync { "SYNC" }; juce::Slider hz, depth, amt[2]; juce::Label lRate { {}, "RATE" }, lDepth { {}, "DEPTH" }, lDest[2]; };
    LfoRow lfo[2];
    juce::ToggleButton seqOn { "ON" }; juce::Slider seqStep[16], seqLen, seqAmt; juce::ComboBox seqDiv, seqDest; juce::Label lSeqLen { {}, "LENGTH" }, lSeqDiv { {}, "RATE" }, lSeqDest { {}, "DEST" }, lSeqAmt { {}, "AMOUNT" };
    juce::ToggleButton envOn { "ON" }; juce::Slider envA, envD, envS, envR, envAmt[3], envCurve[3]; juce::ComboBox envDest[3]; juce::Label lEnvA { {}, "ATTACK" }, lEnvD { {}, "DECAY" }, lEnvS { {}, "SUSTAIN" }, lEnvR { {}, "RELEASE" }, lEnvDest[3], lEnvCurve[3];
    XYPad pad; juce::ToggleButton xyLatch { "LATCH" }; juce::ComboBox xyDest[3], xyGate; juce::Slider xyAmt[3], xyA; juce::ToggleButton xyInv[3], xyGateInv { "INV" }; juce::Label lXy[3], lGate { {}, "GATE" }, lHint, lA { {}, "A" };
    Section secLfo[2] { Section ("LFO 1"), Section ("LFO 2") }, secSeq { "STEP SEQUENCER" }, secEnv { "ADSR ENVELOPE" }, secXY { "X / Y PAD" };
    // the SHAPES view
    juce::TextButton bShapes { "SHAPES" }; bool showShapes = false;
    struct ShapeRow { ShapeRow (P6PanelProcessor& p, int i) : editor (p, i) {} ShapeEditor editor; juce::ToggleButton on { "ON" }, sync { "SYNC" }, bipolar { "BIPOLAR" }; juce::ComboBox mode, div, dest[3]; juce::Slider seconds, amt[3]; juce::Label lMode { {}, "MODE" }, lLen { {}, "LENGTH" }, lDest[3]; juce::TextButton presets { "PRESETS" }; };
    static juce::File shapeDir() { auto d = P6PanelProcessor::libraryDir().getChildFile ("Shapes"); d.createDirectory(); return d; }   // the saved shapes: Prophet 6 Patches/Shapes/<name>.p6shape
    void shapePresetMenu (int i); void saveShapeAs (int i); void loadShape (int i, const juce::File&);
    ShapeRow shapeRow[2] { ShapeRow (proc, 0), ShapeRow (proc, 1) };
    Section secShape[2] { Section ("SHAPE 1"), Section ("SHAPE 2") };
    std::vector<juce::Component*> shapeComps;
    void setShapesVisible (bool);
    void fillDest (juce::ComboBox&, bool switches = false);
    static int destFromBox (const juce::ComboBox& b) { return b.getSelectedId() - 2; }
    bool updating = false;
};
} // namespace ui

class P6PanelEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit P6PanelEditor (P6PanelProcessor&);
    ~P6PanelEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPorts();
    void portsChanged (bool inBox, bool outBox);
    void refreshProgramRow();
    void writeClicked();
    void loadClicked(); void saveClicked();

    P6PanelProcessor& proc; ui::APVTS& s;
    MoogLook look;
    static constexpr int kBaseW = 1600, kBaseH = 800;
    juce::Component canvas;

    // ---- header
    juce::ComboBox cIn, cOut, cKeys, cChan, cTuning; juce::Label lIn { {}, "MIDI IN" }, lOut { {}, "MIDI OUT" }, lKeys { {}, "KEYS" }, lChan { {}, "CH" }, lTuning { {}, "TUNING" };
    ui::Lamp lamp;
    // ---- program strip
    juce::ComboBox cBank; juce::Slider sProg; juce::Label lBank { {}, "BANK" }, lProg { {}, "PROGRAM" }, lName { {}, "NAME" };
    juce::TextEditor eName;
    juce::TextButton bFetch { "FETCH" }, bSend { "SEND" }, bWrite { "WRITE" }, bLoad { "LOAD" }, bSave { "SAVE" }, bLearn { "LEARN" };
    juce::Label lStatus; ui::Stars progStars;
    juce::ToggleButton tVintage { "VINTAGE" };   // the synth's Vintage global (Slop then sets its depth)
    std::unique_ptr<juce::FileChooser> chooser;
    bool lfoSyncShown = false;
    int lastUiVersion = -1; bool updatingProgramRow = false; int portPollCounter = -1;

    // ---- sections
    // sections laid out like the Prophet-6 desktop module's front panel (row by row, left to right)
    ui::Section secDist { "DISTORT" }, secFx { "EFFECTS" }, secClock { "CLOCK" }, secArp { "ARPEGGIATOR" }, secSeq { "SEQUENCER" },
                secLfo { "LOW FREQUENCY OSCILLATOR" }, secPM { "POLY MOD" }, secAT { "AFTERTOUCH" }, secMisc { "MISC PARAMETERS" },
                secOsc1 { "OSCILLATOR 1" }, secSlop { "SLOP" }, secMix { "MIXER" }, secHP { "HIGH-PASS FILTER" }, secFEnv { "FILTER ENVELOPE" },
                secOsc2 { "OSCILLATOR 2" }, secMix2 { "MIXER" }, secLP { "LOW-PASS FILTER" }, secAEnv { "AMPLIFIER ENVELOPE" },
                secGlide { "GLIDE" }, secUnison { "UNISON" }, secProg { "PROGRAM" };
    juce::TextButton bFxAB { "EDIT A" }; bool fxShowB = false;
    // ---- sequencer page (replaces the panel while shown)
    juce::TextButton bSeqView { "SEQ" }, bSeqBack { "PANEL" }, bSeqClear { "CLEAR" }, bSeqPlay2 { "PLAY" }, bTrDown { "-1" }, bTrUp { "+1" }, bOctDown { "-12" }, bOctUp { "+12" }, bHumanize { "HUMANIZE" }, bReverse { "REVERSE" }, bDouble { "DOUBLE" }; juce::Label lTr { {}, "TRANSPOSE" };
    juce::Component seqPage; ui::SeqGrid seqGrid { proc };
    juce::MidiKeyboardState kbState; juce::MidiKeyboardComponent keyboard { kbState, juce::MidiKeyboardComponent::horizontalKeyboard };   // plays the synth (for recording sequences)
    struct KbListener : public juce::MidiKeyboardState::Listener
    {
        explicit KbListener (P6PanelProcessor& p) : proc (p) {}
        void handleNoteOn  (juce::MidiKeyboardState*, int ch, int note, float vel) override { proc.link.sendRaw (juce::MidiMessage::noteOn  (juce::jlimit (1, 16, proc.link.channel), note, vel)); proc.triggerEnv(); juce::ignoreUnused (ch); }
        void handleNoteOff (juce::MidiKeyboardState*, int ch, int note, float vel) override { proc.link.sendRaw (juce::MidiMessage::noteOff (juce::jlimit (1, 16, proc.link.channel), note, vel)); proc.releaseEnv(); juce::ignoreUnused (ch); }
        P6PanelProcessor& proc;
    } kbListener { proc }; juce::Slider sSeqLen; juce::Label lSeqLen { {}, "LENGTH" }, lSeqHint;
    void showSeqPage (bool);
    void showPage (juce::Component* page);   // nullptr = the panel
    // ---- save-as dialog (an overlay; the plugin runs without modal loops)
    struct SaveAsDialog : public juce::Component
    {
        explicit SaveAsDialog (P6PanelEditor&);
        void paint (juce::Graphics&) override; void resized() override;
        void open();
        P6PanelEditor& ed; juce::TextEditor eName; juce::ComboBox cCategory, cBank; juce::Slider sProg; juce::TextButton bOk { "SAVE" }, bCancel { "CANCEL" }; juce::ToggleButton tSlot { "also write into this synth slot" };
        juce::Label lName { {}, "NAME" }, lCategory { {}, "CATEGORY" }, lSlot { {}, "USER SLOT" }, lTitle { {}, "SAVE AS" };
    } saveAs { *this };
    juce::TextButton bSaveAs { "SAVE AS" }, bRandom { "RANDOM" }, bMutate { "MUTATE" }, bMem[P6PanelProcessor::kMemories];   // A B C D: four sounds side by side
    void refreshMem();
    // ---- preset browser page
    juce::TextButton bPresets { "PRESETS" }; ui::PresetBrowser presetPage { proc };
    juce::MidiKeyboardComponent keyboard2 { kbState, juce::MidiKeyboardComponent::horizontalKeyboard };
    // ---- MOD page
    juce::TextButton bMod { "MOD" }, bShapes { "SHAPES" }; ui::ModPage modPage { proc }; juce::MidiKeyboardComponent keyboard3 { kbState, juce::MidiKeyboardComponent::horizontalKeyboard };
    // ---- EUCLID page: four Euclidean tracks playing the synth in time with Logic
    juce::TextButton bEuclid { "EUCLID" }; ui::EuclidPage euclidPage { proc }; juce::MidiKeyboardComponent keyboard4 { kbState, juce::MidiKeyboardComponent::horizontalKeyboard };
    // osc 1
    ui::Knob kO1Freq { s, "osc1Freq", "Frequency", " st" }, kO1Shape { s, "osc1Shape", "Shape" }, kO1PW { s, "osc1PW", "Pulse Width" }; ui::Toggle tO1Sync { s, "osc1Sync", "Sync" };
    // osc 2
    ui::Knob kO2Freq { s, "osc2Freq", "Frequency", " st" }, kO2Fine { s, "osc2Fine", "Fine" }, kO2Shape { s, "osc2Shape", "Shape" }, kO2PW { s, "osc2PW", "Pulse Width" };
    ui::Toggle tO2Low { s, "osc2Low", "Lo Freq" }, tO2Key { s, "osc2Key", "Keybd" };
    // mixer
    ui::Knob kMO1 { s, "osc1Level", "Osc 1" }, kMSub { s, "subLevel", "Sub Octave" }, kMO2 { s, "osc2Level", "Osc 2" }, kMNoise { s, "noise", "Noise" };
    // glide & pitch
    ui::Toggle tGlide { s, "glideOn", "Glide" }; ui::Knob kGlide { s, "glideRate", "Glide Rate" }, kPB { s, "pbRange", "P Whl Range", " st" }; ui::Chooser cGlideMode { s, "glideMode", "Glide mode" };
    // voice
    ui::Knob kVol { s, "voiceVol", "Prgm Vol" }, kPan { s, "panSpread", "Pan Spread" }, kDist { s, "distortion", "Amount" }, kSlop { s, "slop", "Amount" };
    ui::Toggle tUnison { s, "unisonOn", "Unison" }; ui::Chooser cUnison { s, "unisonMode", "Voices" }, cKeyMode { s, "keyMode", "Key mode" };
    // filters
    ui::Knob kLPF { s, "lpFreq", "Cutoff" }, kLPR { s, "lpRes", "Resonance" }, kLPE { s, "lpEnvAmt", "Env Amount" }; ui::Chooser cLPKey { s, "lpKey", "Keybd" }; ui::Toggle tLPVel { s, "lpVel", "Velo" };
    ui::Knob kHPF { s, "hpFreq", "Cutoff" }, kHPR { s, "hpRes", "Resonance" }, kHPE { s, "hpEnvAmt", "Env Amount" }; ui::Chooser cHPKey { s, "hpKey", "Keybd" }; ui::Toggle tHPVel { s, "hpVel", "Velo" };
    // envelopes
    ui::Knob kFA { s, "fAtk", "Attack" }, kFD { s, "fDec", "Decay" }, kFS { s, "fSus", "Sustain" }, kFR { s, "fRel", "Release" };
    ui::Knob kAAmt { s, "vcaEnvAmt", "Env Amount" }, kAA { s, "aAtk", "Attack" }, kAD { s, "aDec", "Decay" }, kAS { s, "aSus", "Sustain" }, kAR { s, "aRel", "Release" }; ui::Toggle tAVel { s, "vcaVel", "Velo" };
    // lfo
    ui::Knob kLfoF { s, "lfoFreq", "Frequency" }, kLfoA { s, "lfoAmt", "Initial Amt" }; ui::Chooser cLfoShape { s, "lfoShape", "Shape" }; ui::Toggle tLfoSync { s, "lfoSync", "Sync" };
    ui::Toggle tLfo1 { s, "lfoFreq1", "Freq 1" }, tLfo2 { s, "lfoFreq2", "Freq 2" }, tLfoPW { s, "lfoPW", "PW 1+2" }, tLfoAmp { s, "lfoAmp", "Amp" }, tLfoLP { s, "lfoLP", "LP" }, tLfoHP { s, "lfoHP", "HP" };
    // poly mod
    ui::Knob kPMEnv { s, "pmFiltEnv", "Filter Env" }, kPMO2 { s, "pmOsc2", "Osc 2" };
    ui::Toggle tPM1 { s, "pmFreq1", "Freq 1" }, tPMShape { s, "pmShape1", "Shape 1" }, tPMPW { s, "pmPW1", "PW 1" }, tPMLP { s, "pmLP", "LP" }, tPMHP { s, "pmHP", "HP" };
    // aftertouch
    ui::Knob kAT { s, "atAmt", "Amount" };
    ui::Toggle tAT1 { s, "atFreq1", "Freq 1" }, tAT2 { s, "atFreq2", "Freq 2" }, tATLP { s, "atLP", "LP" }, tATHP { s, "atHP", "HP" }, tATVCA { s, "atVCA", "Amp" }, tATLFO { s, "atLFO", "LFO" };
    // effects
    ui::Toggle tFx { s, "fxOn", "On/Off" };
    ui::Chooser cFxA { s, "fx1Type", "Effect A" }; ui::Knob kFxAMix { s, "fx1Mix", "Mix" }, kFxA1 { s, "fx1P1", "Param 1" }, kFxA2 { s, "fx1P2", "Param 2" }; ui::Toggle tFxASync { s, "fx1Sync", "Sync" }; ui::Chooser cFxATime { s, "fx1Time", "Time" };
    ui::Chooser cFxB { s, "fx2Type", "Effect B" }; ui::Knob kFxBMix { s, "fx2Mix", "Mix" }, kFxB1 { s, "fx2P1", "Param 1" }, kFxB2 { s, "fx2P2", "Param 2" }; ui::Toggle tFxBSync { s, "fx2Sync", "Sync" }; ui::Chooser cFxBTime { s, "fx2Time", "Time" };
    // clock & arp
    ui::Knob kBPM { s, "bpm", "BPM" }; ui::Toggle tArp { s, "arpOn", "On/Off" }; ui::Chooser cArpMode { s, "arpMode", "Mode" }, cArpOct { s, "arpRange", "Octave" }, cArpVal { s, "arpTime", "Value" };
    // sequencer
    ui::Toggle tSeq { s, "seqOn", "On/Off" }; juce::TextButton bPlay { "PLAY" }; /* live Play/Stop (NRPN 1088), not a program parameter */ ui::Knob kSeqLen { s, "seqLength", "Length" }; ui::Chooser cSeqMode { s, "seqMode", "Mode" }, cSeqPlay { s, "seqPlay", "Play" };
};
