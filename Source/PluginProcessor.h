#pragma once
#include <map>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "P6Params.h"
#include "MidiLink.h"
#include "Modulation.h"
#include "Euclid.h"

/** Prophet 6 Panel — an editor/librarian for the Sequential Prophet-6 with its own MIDI connection.
    Audio and host MIDI pass straight through; the plugin's parameters mirror the synth's program. */
class P6PanelProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::Timer
{
public:
    P6PanelProcessor();
    ~P6PanelProcessor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override;                                                  // the audio stops: the EUCLID notes still sounding end
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;   // bypassed: likewise, and nothing else is sent
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainInputChannelSet() == l.getMainOutputChannelSet(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Prophet 6 Panel"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    MidiLink link;

    // ---- program image: the 1024 bytes the synth would hold, kept in step with the parameters
    uint8_t program[P6::kProgramBytes] {};
    juce::String patchName() const;
    void setPatchName (const juce::String&);
    juce::MemoryBlock programBlock() const { return { program, P6::kProgramBytes }; }
    void applyProgram (const juce::MemoryBlock&, bool sendToSynth);   // dump → parameters (+ optionally edit buffer)

    // ---- actions for the editor
    void fetch()                             { link.requestEditBuffer(); }

    // ---- the program's sequence: 6 note tracks x 64 steps, stored in the dump from byte 128 (track t: notes at 128+128t,
    //      velocities at +64). Velocity byte: bit 7 = the step is part of the sequence, low bits = velocity (0 = rest / tie
    //      continuation). Note byte: bit 7 = this step continues (ties) the previous step's note on that track.
    //      The length is simply the number of flagged steps. Edits are pushed as NRPNs (256 + 128t (+64) + step) for notes
    //      and velocities, and as an edit-buffer dump for everything the NRPNs cannot express (rests, ties, length).
    static constexpr int kSeqTracks = 6, kSeqSteps = 64, kSeqBase = 128;
    uint8_t& seqNoteByte (int t, int s)      { return program[kSeqBase + t * 128 + s]; }
    uint8_t& seqVelByte  (int t, int s)      { return program[kSeqBase + t * 128 + 64 + s]; }
    int  seqNote (int t, int s) const        { return program[kSeqBase + t * 128 + s] & 0x7f; }
    int  seqVel  (int t, int s) const        { return program[kSeqBase + t * 128 + 64 + s] & 0x7f; }
    bool seqUsed (int t, int s) const        { return (program[kSeqBase + t * 128 + 64 + s] & 0x80) != 0; }
    bool seqTie  (int t, int s) const        { return (program[kSeqBase + t * 128 + s] & 0x80) != 0; }
    bool seqOn   (int t, int s) const        { return seqUsed (t, s) && seqVel (t, s) > 0 && ! seqTie (t, s); }   // a sounding note starts here
    int  seqLength() const                   { int n = 0; for (int s = 0; s < kSeqSteps; ++s) for (int t = 0; t < kSeqTracks; ++t) if (seqUsed (t, s)) { n = s + 1; break; } return n; }
    void setSeqNote (int t, int s, int note);
    void setSeqVel  (int t, int s, int vel);
    void addSeqNote (int t, int s, int note, int vel);
    void removeSeqNote (int t, int s);        // clears the note and any tie continuing it; the step stays part of the sequence
    void setSeqTieLength (int t, int s, int steps);   // make the note at (t, s) last this many steps
    void setSeqLength (int n);
    void clearSequence();
    void seqChanged();                        // schedules an edit-buffer dump (rests / ties / length are not reachable by NRPN)
    juce::int64 seqDumpAt = 0;
    // what the synth is playing right now (needs the synth's Param Xmit global set to "nas": NRPN + notes out)
    bool noteHeld[128] {}; int playStep = -1; juce::int64 lastNoteAt = 0;
    int burstBase = -1; std::vector<int> burstNotes;   // note-ons arriving together are one chord step, matched as a set
    void onNoteIn (int note, bool on);
    uint8_t seqShadow[kSeqTracks][2][kSeqSteps] {};   // what shortening the length hid, so lengthening brings it back (cleared on fetch/load/clear)
    void sendAll();                          // whole program → edit buffer
    void writeTo (int bank, int program);    // whole program → user bank slot
    void selectProgram (int bank, int program);
    /** A program change is on its way (from the panel or the synth): what MOD last sent belongs to the old program and is forgotten -
        nothing of it is put back into the new one - and MOD sends nothing until the new program's dump has shown its values (or 3 s pass). */
    void expectFreshProgram(); juce::int64 freshProgramUntil = 0;
    /** SAVE of a library sound: the file it goes to, and whether that is another sound's file (then SAVE asks before replacing it). */
    struct LibSave { juce::File file; juce::String category, oldName, name; bool same = false, replacesOther = false; };
    LibSave librarySave() const;
    void setPorts (const juce::String& inId, const juce::String& outId);
    int currentBank = 0, currentProgram = 0;
    std::atomic<int> uiVersion { 0 };        // bumped when the whole program changed (fetch/load) so the editor refreshes

    void flush() { timerCallback(); }        // for the test harness: send pending changes now
    void savePatchFile (const juce::File&);
    bool loadPatchFile (const juce::File&);

    // ---- dump layout: byte offset inside the 1024-byte dump for each parameter (-1 = not yet known)
    std::vector<int> dumpOffset;
    // Seq Length has no byte of its own: the synth ignores its NRPN, the length is where the recorded steps end (auto-learn, Oct 2026)
    static bool notStored (int i) { return P6::params()[(size_t) i].nrpn == 169; }
    int knownOffsets() const { int n = 0; for (int i = 0; i < (int) dumpOffset.size(); ++i) n += (dumpOffset[(size_t) i] >= 0 || notStored (i)); return n; }
    juce::String unknownList() const;
    static juce::File layoutFile();
    // ---- the panel's own catalogue of user-bank slots (name + category); the synth itself knows no categories
    struct UserEntry { juce::String name, category; };
    std::map<int, UserEntry> catalogue;                 // key: bank * 100 + program (user banks 0-4 only)
    static juce::File catalogueFile();
    void loadCatalogue(); void saveCatalogue() const;
    // ---- name scan: read every program's name from the synth (program dumps 0xx-9xx) into synth_names.txt, without touching the panel
    void startNameScan(); void stopNameScan();
    bool scanning = false; int scanSlot = 0; juce::int64 scanSentAt = 0; std::map<int, juce::String> synthNames;   // slot (bank*100+prog) -> name as the synth has it
    static juce::File synthNamesFile();
    // ---- ratings: 0-5 stars per slot (0-999), the panel's own file
    //      keys: "123" for a synth slot, "lib:<Category>/<name>" for a library file
    std::map<juce::String, int> ratings; static juce::File ratingsFile(); void loadRatings(); void saveRatings() const;
    int  rating (const juce::String& key) const { auto it = ratings.find (key); return it == ratings.end() ? 0 : it->second; }
    int  rating (int slot) const { return rating (juce::String (slot)); }
    void setRating (const juce::String& key, int stars) { stars = juce::jlimit (0, 5, stars); if (stars == 0) ratings.erase (key); else ratings[key] = stars; saveRatings(); ++uiVersion; }
    void setRating (int slot, int stars) { setRating (juce::String (slot), stars); }
    // ---- notes: the user's own words about a sound (one line each), kept like the ratings in the panel's own file notes.txt
    std::map<juce::String, juce::String> notes; static juce::File notesFile(); void loadNotes(); void saveNotes() const;
    juce::String note (const juce::String& key) const { auto it = notes.find (key); return it == notes.end() ? juce::String() : it->second; }
    void setNote (const juce::String& key, const juce::String& text);
    juce::String euclidFresh;   // the EUCLID page as a new panel has it (switched on): a project that saved exactly this never used the page
    // ---- the file library: Documents/Prophet 6 Patches/<Category>/<name>.p6patch - unlimited sounds beyond the synth's 500 user slots
    struct LibEntry { juce::File file; juce::String category, name, key; };
    std::vector<LibEntry> library(); static juce::File libraryDir();
    void loadSynthNames(); void saveSynthNames() const;
    // ---- sound design helpers
    void randomize();                          // a fresh, playable sound (voice / arp / seq settings untouched)
    void mutate (float amount);                // nudge the current sound (0..1)
    // ---- A B C D: four sounds side by side, to compare. One letter is in use - its sound is the one on the panel and in the
    //      synth's edit buffer. Another letter: the sound in use is put away under its own letter and that one comes up; an empty
    //      letter starts as a copy of the sound in use. Each keeps where its sound came from (slot or library file, and the name it
    //      had then). Nothing is written to the synth's stored programs; the letters are saved with the project.
    static constexpr int kMemories = 4;
    struct Memory { bool used = false; juce::MemoryBlock data; juce::String origin, libKey; int bank = 0, prog = 0; };
    Memory mem[kMemories]; int memActive = 0; bool originPending = true, memSwitching = false;
    void switchMemory (int m);
    void clearMemory (int m) { if (m >= 0 && m < kMemories && m != memActive) { mem[m] = Memory(); ++uiVersion; } }   // a letter that is not in use becomes empty again
    juce::String originNow() const;            // "501  Name" or "Library  Category / Name": what the sound in use is called now
    juce::String memoryText (int m) const;     // what a letter holds, in words (for its tooltip)
    juce::MemoryBlock patchBytes();            // the sound in use, whole: the program and what rides with it (MOD settings, Vintage)
    bool applyPatchBytes (const void* data, int size);   // ... and back: onto the panel and into the synth's edit buffer
    void setParam (int i, int v);              // through the parameter so the UI and the synth both follow
    int  valueOf (int index) const;            // parameter → synth value
    // ---- sequencer helpers
    void seqTranspose (int semitones); void seqHumanize (int amount); void seqReverse(); void seqDouble();
    // ---- remembered browser state (saved with the project)
    juce::String browserCategory, browserSearch; int browserBanks = 1;
    juce::String libKey;                       // "lib:<Category>/<name>" while the sound came from the file library (empty = a synth slot)
    void saveAs (const juce::String& name, const juce::String& category, int bank, int program, bool writeSlot);   // name it, catalogue it, keep a .p6patch copy; optionally WRITE it into a user slot
    void loadLayout(); void saveLayout();
    // ---- extra modulation over MIDI (the MOD page): LFOs, step sequencer, ADSR, X/Y pad - sent as NRPNs on top of the program
    Mod::State mod;
    void triggerEnv() { envTrig = true; ++envHeld; }
    void releaseEnv() { envHeld = juce::jmax (0, envHeld - 1); }
    void xyTouch (float x, float y, bool down);                     // x, y in -1..1
    void modSettingsChanged();                                      // dests may have changed: restore what is no longer modulated
    // MOD settings travel with the program: Documents/Prophet 6 Patches/mod/<slot>.xml for synth slots, appended to .p6patch files
    static juce::File modFile (int slot) { auto d = libraryDir().getChildFile ("mod"); d.createDirectory(); return d.getChildFile (juce::String (slot) + ".xml"); }
    void loadModFor (int slot); void saveModFor (int slot);
    bool modRunning() const { return mod.lfo[0].on || mod.lfo[1].on || mod.seq.on || mod.env.on || xyDown; }
    float lfoPhase[2] {}, lfoHold[2] {}; int seqPos = -1; float envLevel = 0.f, envPos = 0.f, envFrom = 0.f; int envStage = 0; int envHeld = 0; float shapePos[2] {}; bool shapeRun[2] {}; double shapeStartBeat[2] {}; bool xyDown = false; float xyX = 0.f, xyY = 0.f;   // envStage: 0 idle, 1 attack, 2 decay/sustain, 3 release
    std::atomic<double> hostBpm { 0.0 }, hostPpq { 0.0 }; std::atomic<bool> hostPlaying { false }; std::atomic<int> hostNoteOns { 0 }, hostNoteOffs { 0 };
    double beatClock = 0.0; juce::int64 lastModTick = 0; int seenNoteOns = 0, seenNoteOffs = 0; bool envTrig = false;
    std::vector<int> modSent;                                       // last value sent for a modulated parameter (-1 = at its base value)
    bool xyGateOn = false; int xyGateParam = -1;
    bool padDown = false; float padX = 0.f, padY = 0.f;   // the on-screen pad
    std::atomic<bool> modParamsDirty { false }; bool syncingMod = false;
    // ---- the EUCLID page: four Euclidean tracks, played from processBlock through the panel's own port, in step with the host
    Euclid::Engine euclid; std::vector<Euclid::Event> euclidEvents;
    std::atomic<bool> euclidRun { false };     // the page's RUN: play without the host's transport (the standalone app, or Logic stopped)
    std::atomic<bool> euclidPanic { false };   // end every note the page started, now
    double euclidFreePpq = 0.0; bool euclidWasFree = false; Euclid::BlockClock euclidClock;
    std::vector<int> euclidLockSent;           // per parameter: the lock value last sent (-1 = the program's own value)
    std::atomic<int> euclidLocks { 0 };        // how many parameters are locked right now (for the display)
    bool euclidHostClock() const { return hostPlaying.load() && hostBpm.load() > 0; }
    // ---- learn mode: turn one control on the synth, the plugin fetches and finds the byte that moved
    std::atomic<bool> learning { false };
    juce::String learnStatus;
    juce::String lastUnknown;
    bool seqPlaying = false;
    int altTuning = -1;           // the synth's Alt Tuning global as last reported by it (-1 = unknown)      // the synth's sequencer transport, as last reported (NRPN 1088)   // last NRPN the synth sent that the panel has no parameter for (shown in the status line)
    uint8_t lastDump[P6::kProgramBytes] {}; bool haveLastDump = false;
    // ---- auto-learn: the panel finds unknown dump offsets by itself - it sends a parameter (NRPN) with two test values,
    //      reads the edit buffer after each and takes the byte that followed; afterwards the synth gets its sound back.
    //      It also tries the NRPN numbers the manual's table does not name, and writes what it found to probe.txt.
    void startAutoLearn(); void stopAutoLearn(); bool autoLearning() const { return autoStage != 0; }
    // ---- the synth's Vintage global (OS 1.6.x; with it on, Slop sets the vintage depth). Its NRPN is not in the manual,
    //      so the panel learns it once from the synth (switch Vintage on the synth) and keeps it in globals.txt
    int vintageNrpn = -1, vintageState = -1; bool vintageLearn = false;
    void setVintage (bool on); void loadGlobals(); void saveGlobals();
    // the synth keeps Vintage as a global, so the panel stores it with each sound itself (behind the SysEx in .p6patch files and in the
    // per-slot MOD files) and switches the synth to match when the sound is loaded through the panel
    juce::ValueTree soundExtras() const;
    void applySoundExtras (const juce::ValueTree&);

private:
    struct Probe { int nrpn, param, v1, v2; };
    std::vector<Probe> autoQueue; size_t autoPos = 0; int autoStage = 0, autoRetries = 0; juce::int64 autoAt = 0; bool autoWait = false;
    bool haveAutoRef = false;   // the reference dump of this run has arrived: only then is there a sound to give back
    uint8_t autoRef[P6::kProgramBytes] {}, autoA[P6::kProgramBytes] {}; juce::String probeReport; int autoFound = 0, autoHidden = 0;
    void autoNext(); void autoDump (const uint8_t* d); void autoFinish (bool aborted);
    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;
    void setParamFromSynth (int index, int value);
    void onNrpnIn (int nrpn, int value);
    void onDumpIn (const juce::MemoryBlock&);
    void modTick();
    int learnNrpn = -1, learnValue = 0; juce::int64 learnTime = 0; bool learnFetchPending = false;

    std::vector<std::atomic<bool>> dirty;
    std::vector<int> lastSent;
    bool restoring = false;
    bool resendPending = false;   // project reopened: push the stored program once the synth is on-line ...
    bool fetchPending = false;    // ... the standalone instead reads what the synth is on, so a launch never overwrites the edit buffer
    bool haveStoredProgram (const juce::ValueTree& state) const;
    JUCE_DECLARE_WEAK_REFERENCEABLE (P6PanelProcessor)
};
