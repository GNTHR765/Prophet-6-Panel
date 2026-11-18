#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <vector>
#include <array>
#include <atomic>

/** The plugin's own MIDI connection to the Prophet-6 (independent of the host).
    Opens one input and one output device, speaks NRPN and DSI SysEx, and reports what the synth sends.
    All callbacks to the owner happen on the message thread. */
class MidiLink : private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer, private juce::Thread
{
public:
    MidiLink();
    ~MidiLink() override;

    // ---- devices
    static juce::Array<juce::MidiDeviceInfo> inputs()  { return juce::MidiInput::getAvailableDevices(); }
    /** The outputs to choose from - without the panel's own port: sending into it would come straight back in and round again. */
    static juce::Array<juce::MidiDeviceInfo> outputs() { auto d = juce::MidiOutput::getAvailableDevices(); for (int i = d.size(); --i >= 0;) if (d.getReference (i).name == "Prophet 6 Panel" || d.getReference (i).name == "Prophet 6 Panel (App)") d.remove (i); return d; }
    bool openInput (const juce::String& identifier);
    bool openOutput (const juce::String& identifier);
    bool openKeys (const juce::String& identifier);          // a controller keyboard: its notes / wheels are forwarded to the synth
    /** The panel's own MIDI port ("Prophet 6 Panel"), created on the first timer tick: point Logic's External Instrument at it and the track's
        notes reach the synth through the panel - on its channel, and the MOD page's envelope sees them. Mac only. */
    juce::String ownPortName() const;
    bool hasOwnPort() const { return own != nullptr; }
    juce::String keysId() const { return kId; }
    /** Bumped (on the message thread) whenever the Mac's list of MIDI devices changes: the editor refreshes its port boxes
        when it sees a new number, instead of asking the system for the list again and again. */
    int deviceListVersion() const { return listVersion; }
    /** The ports as chosen, whether or not they could be opened: what the state saves, and what the timer keeps trying
        to reopen - so a synth or keyboard that was off at launch, or unplugged and plugged back in, comes back by itself. */
    juce::String wantedIn() const { return wIn; }
    juce::String wantedOut() const { return wOut; }
    juce::String wantedKeys() const { return wKeys; }
    std::function<void()> onKeysNote, onKeysNoteOff;        // notes from the controller (for the MOD page's envelope)
    /** The chord being played into the panel (from Logic's track through the panel's port, or the controller): the notes
        held right now, and the last chord played, kept after the keys are released ("latched") so the EUCLID page can
        keep taking its pitches from it. Written on the MIDI thread, read from the audio thread. */
    std::array<std::atomic<bool>, 128> held {}, latched {}; std::atomic<int> heldCount { 0 };
    std::atomic<bool> muteKeys { false };                   // the chord drives the page but is not passed on to the synth
    /** The chord: the latched one (default), or only what is held right now (LATCH off). Sorted low to high. */
    int chord (int* notesOut, int maxNotes, bool useLatch = true) const { int n = 0; for (int i = 0; i < 128 && n < maxNotes; ++i) if ((useLatch ? latched : held)[(size_t) i].load()) notesOut[n++] = i; return n; }
    /** A note on / off from the keys or Logic's region: keeps held and latched up to date. Notes that start within a moment of each
        other are one chord; letting go of a chord leaves it latched whole, and the notes of an older chord that are still sounding
        when the next one starts drop out of it as they end (legato or overlapping regions). Public for the tests. */
    void chordNote (int note, bool on, juce::uint32 nowMs);
    void allKeysOff();                                      // end the notes that were passed on to the synth and still sound (when muting them)
    /** A message from the controller keyboard or the panel's own port: on to the synth on its channel. A note-off goes on exactly
        when its note-on did (and on that note-on's channel) - so MUTE CHORD coming on can never swallow the off of a note that
        sounds, and going off never sends an off for a note that was held back. Public for the tests. */
    void keysIn (const juce::MidiMessage&);
    std::array<std::atomic<int>, 128> passed {}, passedCh {};   // per key: note-ons passed on and not yet ended, and the channel they went out on
    /** For the tests: sees every message the link sends (also with no device open); setOnlineForTest stands in for the synth's answer. */
    std::function<void (const juce::MidiMessage&)> testTap; void setOnlineForTest (bool b) { online = b; }
    void closeAll();
    juce::String inputId() const  { return inId; }
    juce::String outputId() const { return outId; }
    bool hasOutput() const { return haveOut.load(); }
    bool isOnline() const { return online; }
    juce::String synthVersion() const { return version; }
    int channel = 1;   // 1-16; the P6 ignores it when its MIDI channel is set to All

    // ---- sending
    void sendNrpn (int nrpn, int value);
    void sendBankProgram (int bank, int program);           // bank 0-9, program 0-99
    void sendRaw (const juce::MidiMessage&);
    void sendFromAudioAt (const juce::MidiMessage& m, double dueMs) { if (testTap) testTap (m); if (! haveOut.load()) return; Item it { m, nullptr, false }; it.dueMs = dueMs; push (std::move (it)); }   // a note with its time (Time::getMillisecondCounterHiRes() scale): it leaves when that moment has come; what is sent after it waits behind it
    std::function<void (const juce::MidiMessage&)> sentTap; void openForTest() { haveOut = true; }   // for the test harness: each message at the moment it leaves the queue
    void sendFromAudio (const juce::MidiMessage&);          // from the audio thread (the EUCLID page's notes): guarded against the port closing meanwhile
    void sendNrpnFromAudio (int nrpn, int value);           // likewise for the page's parameter locks
    void requestEditBuffer();
    void requestProgram (int bank, int program);
    void requestInquiry();
    void retryConnect() { inquiriesLeft = 3; silentPolls = 0; }   // ask the synth again (lamp click)
    void sendEditBuffer (const juce::MemoryBlock& program1024);
    void sendProgram (int bank, int program, const juce::MemoryBlock& program1024);

    // ---- receiving (message thread)
    std::function<void (int nrpn, int value)> onNrpn;
    std::function<void (int note, bool on)> onNote;    // notes the synth sends (its sequencer / arpeggiator, with Param Xmit = nas)
    std::function<void (const juce::MemoryBlock& program1024)> onEditBuffer;
    std::function<void (int bank, int program, const juce::MemoryBlock& program1024)> onProgram;
    std::function<void (bool online)> onOnlineChanged;
    std::function<void (int bank, int program)> onProgramChange;   // the synth's own program select (bank from CC 32, then the program change)

    // ---- DSI 7-bit packing (public for the test harness)
    static juce::MemoryBlock pack (const juce::MemoryBlock& data);
    static juce::MemoryBlock unpack (const uint8_t* packed, int numBytes, int expectedBytes);

    static constexpr uint8_t kDsiId = 0x01, kP6Id = 0x2D;

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void handleSysEx (const uint8_t* d, int n);
    void handleCC (int cc, int value);

    std::unique_ptr<juce::MidiInput> in, keys, own; bool ownPending = true;
    juce::String inId, outId, kId, version;
    juce::String wIn, wOut, wKeys;   // the chosen identifiers, kept even while the device is missing
    // ---- the sender: everything that goes to the synth is queued and sent by this thread, so neither the audio thread nor the
    //      message thread (the knobs, the timers - and with them the whole plugin window) can be held up by a slow or stalled MIDI port.
    //      The output device itself is owned and used by that thread alone; the message thread hands it a new one through the queue.
    void run() override;
    struct Item { juce::MidiMessage msg; std::unique_ptr<juce::MidiOutput> newOut; bool replace = false; double dueMs = 0.0; };   // dueMs 0: at once
    void push (Item&&);
    void pushGroup (Item* items, int n);   // together, with nothing from another thread between them (an NRPN's four controllers)
    void push (const juce::MidiMessage& m) { push (Item { m, nullptr, false }); }
    bool canSend() const { return haveOut.load() || testTap != nullptr; }
    void emit (const juce::MidiMessage& m) { if (testTap) testTap (m); if (haveOut.load()) push (m); }
    std::vector<Item> ring; juce::AbstractFifo fifo { 4096 }; juce::SpinLock pushLock; juce::WaitableEvent wake;
    std::unique_ptr<juce::MidiOutput> out;   // the sender thread's
    std::atomic<bool> haveOut { false };     // the message thread's view: an output has been opened and handed over
    juce::MidiDeviceListConnection devicesChanged; bool watching = false, rescan = true; int ticks = 0, listVersion = 0;
    std::vector<juce::MidiMessage> pendingKeys;
    bool online = false; int silentPolls = 0, inquiriesLeft = 0;
    std::array<int, 128> noteGroup {}; int chordGroup = 0; juce::uint32 lastNoteOnMs = 0;   // chordNote(): which chord each held note belongs to
    juce::CriticalSection lock; std::vector<juce::MidiMessage> pending;
    int rxParamMsb = 0, rxParamLsb = 0, rxValMsb = 0;   // NRPN receive state
    int rxBank = 0;
};
