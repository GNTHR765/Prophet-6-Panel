#include "ClaudeData.h"
#include "MidiLink.h"

using namespace juce;

MidiLink::MidiLink() : Thread ("Prophet 6 Panel MIDI out") { ring.resize ((size_t) fifo.getTotalSize()); startThread (Priority::high); startTimer (2000); }
MidiLink::~MidiLink() { stopTimer(); cancelPendingUpdate(); closeAll(); signalThreadShouldExit(); wake.signal(); stopThread (3000); }

// ---------------------------------------------------------------- the sender thread
void MidiLink::push (Item&& item)
{
    for (int attempt = 0;; ++attempt)
    {
        {
            const SpinLock::ScopedLockType sl (pushLock);
            int s1, n1, s2, n2; fifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 + n2 >= 1) { ring[(size_t) s1] = std::move (item); fifo.finishedWrite (1); break; }
        }
        // the queue is full (4096 messages the synth has not taken): a note or a knob value is dropped rather than waited for;
        // a new output device is worth a short wait, so the port boxes never end up pointing at nothing
        if (! item.replace || attempt > 200 || Thread::getCurrentThread() == this) return;
        Thread::sleep (2);
    }
    wake.signal();
}
void MidiLink::pushGroup (Item* items, int n)
{
    {
        const SpinLock::ScopedLockType sl (pushLock);
        int s1, n1, s2, n2; fifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 + n2 < n) return;   // not room for the whole group: none of it (half an NRPN would set the wrong parameter)
        for (int i = 0; i < n; ++i) ring[(size_t) (i < n1 ? s1 + i : s2 + i - n1)] = std::move (items[i]);
        fifo.finishedWrite (n);
    }
    wake.signal();
}
void MidiLink::run()
{
    // sends what is ready; a timed message that is not yet due stops it, and the wait until then is returned (0: nothing waits)
    auto drain = [this] (bool untilExit) -> double
    {
        while (fifo.getNumReady() > 0 && ! (untilExit && threadShouldExit()))
        {
            int s1, n1, s2, n2; fifo.prepareToRead (1, s1, n1, s2, n2);
            if (n1 + n2 < 1) break;
            auto& it = ring[(size_t) s1];
            if (untilExit && it.dueMs > 0.0) { const double wait = it.dueMs - Time::getMillisecondCounterHiRes(); if (wait > 0.05) return wait; }
            if (it.replace) { out = std::move (it.newOut); it.replace = false; }
            else { if (sentTap) sentTap (it.msg); if (out != nullptr) out->sendMessageNow (it.msg); }
            it.msg = MidiMessage(); it.newOut.reset(); it.dueMs = 0.0;
            fifo.finishedRead (1);
        }
        return 0.0;
    };
    while (! threadShouldExit())
    {   // a timed message: sleep through the whole milliseconds, then wait the last fraction out here
        const double wait = drain (true);
        if (wait <= 0.0) wake.wait (250);
        else if (wait >= 1.0) wake.wait (jmin (250, (int) wait));
        else Thread::yield();
    }
    drain (false);   // the plugin is going: what it queued on its way out (note-offs, restored values) still reaches the synth
}

bool MidiLink::openInput (const String& identifier)
{
    if (in != nullptr) { in->stop(); in.reset(); }
    inId = {}; online = false; wIn = identifier;
    if (identifier.isEmpty()) return false;
    in = MidiInput::openDevice (identifier, this);
    if (in == nullptr) return false;
    in->start(); inId = identifier;
    retryConnect();
    return true;
}
bool MidiLink::openOutput (const String& identifier)
{
    haveOut = false; push (Item { {}, nullptr, true }); outId = {}; online = false; wOut = identifier;
    if (identifier.isEmpty()) return false;
    auto o = MidiOutput::openDevice (identifier);
    if (o == nullptr) return false;
    push (Item { {}, std::move (o), true }); haveOut = true;
    outId = identifier;
    retryConnect();
    return true;
}
bool MidiLink::openKeys (const String& identifier)
{
    if (keys != nullptr) { keys->stop(); keys.reset(); }
    kId = {}; wKeys = identifier;
    if (identifier.isEmpty()) return false;
    keys = MidiInput::openDevice (identifier, this);
    if (keys == nullptr) return false;
    keys->start(); kId = identifier;
    return true;
}
void MidiLink::closeAll() { if (in != nullptr) in->stop(); if (keys != nullptr) keys->stop(); if (own != nullptr) own->stop(); in.reset(); keys.reset(); own.reset(); haveOut = false; push (Item { {}, nullptr, true }); inId = {}; outId = {}; kId = {}; wIn = {}; wOut = {}; wKeys = {}; online = false; }
String MidiLink::ownPortName() const { return JUCEApplicationBase::isStandaloneApp() ? "Prophet 6 Panel (App)" : "Prophet 6 Panel"; }

// ---------------------------------------------------------------- sending
static void debugDump (const char* name, const uint8_t* d, int n)
{
    auto dir = claudeDataDir ("Prophet 6 Patches"); dir.createDirectory();
    MemoryBlock m; m.append ("\xf0", 1); m.append (d, (size_t) n); m.append ("\xf7", 1);
    dir.getChildFile (name).replaceWithData (m.getData(), m.getSize());
}
void MidiLink::sendRaw (const MidiMessage& m) { emit (m); }
void MidiLink::sendFromAudio (const MidiMessage& m) { emit (m); }
void MidiLink::allKeysOff() { for (int i = 0; i < 128; ++i) if (passed[(size_t) i].exchange (0) > 0) sendRaw (MidiMessage::noteOff (jlimit (1, 16, passedCh[(size_t) i].load()), i)); }   // only what was passed on, on the channel it went out on
void MidiLink::sendNrpnFromAudio (int nrpn, int value) { sendNrpn (nrpn, value); }
void MidiLink::sendNrpn (int nrpn, int value)
{
    if (! canSend()) return;
    const int ch = jlimit (1, 16, channel);
    if (testTap) { testTap (MidiMessage::controllerEvent (ch, 99, (nrpn >> 7) & 0x7f)); testTap (MidiMessage::controllerEvent (ch, 98, nrpn & 0x7f)); testTap (MidiMessage::controllerEvent (ch, 6, (value >> 7) & 0x7f)); testTap (MidiMessage::controllerEvent (ch, 38, value & 0x7f)); }
    if (! haveOut.load()) return;
    Item g[4] = { { MidiMessage::controllerEvent (ch, 99, (nrpn >> 7) & 0x7f), nullptr, false }, { MidiMessage::controllerEvent (ch, 98, nrpn & 0x7f), nullptr, false },
                  { MidiMessage::controllerEvent (ch, 6,  (value >> 7) & 0x7f), nullptr, false }, { MidiMessage::controllerEvent (ch, 38, value & 0x7f), nullptr, false } };
    pushGroup (g, 4);   // as one piece: an EUCLID lock from the audio thread and a knob from the message thread never interleave
}
void MidiLink::sendBankProgram (int bank, int program)
{
    if (! canSend()) return;
    const int ch = jlimit (1, 16, channel);
    emit (MidiMessage::controllerEvent (ch, 32, jlimit (0, 9, bank)));
    emit (MidiMessage::programChange (ch, jlimit (0, 99, program)));
}
void MidiLink::requestInquiry()
{
    const uint8_t d[] = { 0x7e, 0x7f, 0x06, 0x01 };
    sendRaw (MidiMessage::createSysExMessage (d, 4));
}
void MidiLink::requestEditBuffer()            { const uint8_t d[] = { kDsiId, kP6Id, 0x06 }; sendRaw (MidiMessage::createSysExMessage (d, 3)); }
void MidiLink::requestProgram (int bank, int program) { const uint8_t d[] = { kDsiId, kP6Id, 0x05, (uint8_t) jlimit (0, 9, bank), (uint8_t) jlimit (0, 99, program) }; sendRaw (MidiMessage::createSysExMessage (d, 5)); }
void MidiLink::sendEditBuffer (const MemoryBlock& prog)
{
    auto packed = pack (prog);
    MemoryBlock m; m.append ("\x01\x2d\x03", 3); m.append (packed.getData(), packed.getSize());
    debugDump ("last_sent.syx", (const uint8_t*) m.getData(), (int) m.getSize());
    sendRaw (MidiMessage::createSysExMessage (m.getData(), (int) m.getSize()));
}
void MidiLink::sendProgram (int bank, int program, const MemoryBlock& prog)
{
    auto packed = pack (prog);
    const uint8_t head[] = { kDsiId, kP6Id, 0x02, (uint8_t) jlimit (0, 9, bank), (uint8_t) jlimit (0, 99, program) };
    MemoryBlock m; m.append (head, 5); m.append (packed.getData(), packed.getSize());
    sendRaw (MidiMessage::createSysExMessage (m.getData(), (int) m.getSize()));
}

// ---------------------------------------------------------------- packing
MemoryBlock MidiLink::pack (const MemoryBlock& data)
{
    MemoryBlock out;
    const auto* s = (const uint8_t*) data.getData(); const int n = (int) data.getSize();
    for (int i = 0; i < n; i += 7)
    {
        const int cnt = jmin (7, n - i);
        uint8_t ms = 0; for (int k = 0; k < cnt; ++k) ms |= (uint8_t) ((s[i + k] >> 7) << k);
        out.append (&ms, 1);
        for (int k = 0; k < cnt; ++k) { const uint8_t b = s[i + k] & 0x7f; out.append (&b, 1); }
    }
    return out;
}
MemoryBlock MidiLink::unpack (const uint8_t* p, int n, int expected)
{
    MemoryBlock out;
    for (int i = 0; i + 1 <= n; i += 8)
    {
        const uint8_t ms = p[i];
        for (int k = 1; k < 8 && i + k < n; ++k) { const uint8_t b = (uint8_t) (p[i + k] | (((ms >> (k - 1)) & 1) << 7)); out.append (&b, 1); }
    }
    if ((int) out.getSize() > expected) out.setSize ((size_t) expected);
    return out;
}

// ---------------------------------------------------------------- receiving
void MidiLink::chordNote (int n, bool on, juce::uint32 now)
{
    n = jlimit (0, 127, n);
    if (on)
    {
        const bool wasEmpty = heldCount.load() == 0, newChord = wasEmpty || now - lastNoteOnMs > 80;   // within 80 ms: the same chord
        if (newChord) ++chordGroup;
        lastNoteOnMs = now; noteGroup[(size_t) n] = chordGroup;
        if (! held[(size_t) n].exchange (true)) ++heldCount;
        if (wasEmpty) { for (auto& l : latched) l = false; latched[(size_t) n] = true; }          // after a gap: a fresh chord
        else if (newChord) { for (int i = 0; i < 128; ++i) latched[(size_t) i] = held[(size_t) i].load(); }   // legato: the old notes go as they end
        else latched[(size_t) n] = true;                                                          // one more note of this chord
        return;
    }
    if (! held[(size_t) n].exchange (false)) return;
    if (heldCount.load() > 0) --heldCount;
    if (noteGroup[(size_t) n] != chordGroup)
    {   // a note of an older chord ends: it leaves the latch if the newer chord is still sounding
        bool newerHeld = false; for (int i = 0; i < 128; ++i) newerHeld |= held[(size_t) i].load() && noteGroup[(size_t) i] == chordGroup;
        if (newerHeld) latched[(size_t) n] = false;
    }
    // a note of the current chord ends: the latch keeps the chord whole, whatever order the keys come up in
}
void MidiLink::handleIncomingMidiMessage (MidiInput* src, const MidiMessage& m)
{
    if ((src == keys.get() && keys != nullptr) || (src == own.get() && own != nullptr)) { keysIn (m); return; }
    { const ScopedLock sl (lock); pending.push_back (m); }
    triggerAsyncUpdate();
}
void MidiLink::keysIn (const MidiMessage& m)
{   // the controller keyboard, or Logic playing into the panel's own port: straight through to the synth on its channel (no SysEx / clock), the notes also to the owner
    if (m.isSysEx() || m.isMidiClock() || m.isActiveSense()) return;
    if (m.isNoteOn() || m.isNoteOff()) chordNote (m.getNoteNumber(), m.isNoteOn(), Time::getMillisecondCounter());
    int ch = jlimit (1, 16, channel);
    bool pass = ! (muteKeys.load() && (m.isNoteOn() || m.isAftertouch() || m.isChannelPressure()));   // MUTE CHORD: the chord drives the page only
    if (m.isNoteOn()) { if (pass) { auto& n = passed[(size_t) m.getNoteNumber()]; ++n; passedCh[(size_t) m.getNoteNumber()] = ch; } }
    else if (m.isNoteOff())
    {   // an off goes on exactly when its on did, whatever MUTE CHORD (or the channel box) says by now
        auto& n = passed[(size_t) m.getNoteNumber()]; pass = n.load() > 0;
        if (pass) { --n; ch = jlimit (1, 16, passedCh[(size_t) m.getNoteNumber()].load()); }
    }
    MidiMessage f = m; f.setChannel (ch);
    if (pass) emit (f);
    if (m.isNoteOn() || m.isNoteOff()) { { const ScopedLock sl (lock); pendingKeys.push_back (m); } triggerAsyncUpdate(); }
}
void MidiLink::handleAsyncUpdate()
{
    std::vector<MidiMessage> batch, kbatch;
    { const ScopedLock sl (lock); batch.swap (pending); kbatch.swap (pendingKeys); }
    for (auto& m : kbatch) { if (m.isNoteOn() && onKeysNote) onKeysNote(); else if (m.isNoteOff() && onKeysNoteOff) onKeysNoteOff(); }
    for (auto& m : batch)
    {
        if (m.isSysEx()) handleSysEx (m.getSysExData(), m.getSysExDataSize());
        else if (m.isController()) handleCC (m.getControllerNumber(), m.getControllerValue());
        else if (m.isNoteOn()) { if (onNote) onNote (m.getNoteNumber(), true); }
        else if (m.isNoteOff()) { if (onNote) onNote (m.getNoteNumber(), false); }
        else if (m.isProgramChange()) { if (onProgramChange) onProgramChange (juce::jlimit (0, 9, rxBank), juce::jlimit (0, 99, m.getProgramChangeNumber())); }
    }
}
void MidiLink::handleCC (int cc, int v)
{
    switch (cc)
    {
        case 99: rxParamMsb = v; break;
        case 98: rxParamLsb = v; break;
        case 6:  rxValMsb = v; break;
        case 38: if (onNrpn) onNrpn ((rxParamMsb << 7) | rxParamLsb, (rxValMsb << 7) | v); break;
        case 32: rxBank = v; break;
        default: break;
    }
}
void MidiLink::handleSysEx (const uint8_t* d, int n)
{
    if (n >= 4 && d[0] == 0x7e) debugDump ("inquiry.syx", d, n);
    else if (n >= 3 && d[0] == kDsiId) debugDump ("last_received.syx", d, n);
    if (n >= 10 && d[0] == 0x7e && d[2] == 0x06 && d[3] == 0x02 && d[4] == kDsiId && d[5] == kP6Id)
    {
        version = String (d[9]); for (int i = 10; i < n && i < 12; ++i) version += "." + String (d[i]);   // the P6 sends main.minor.rev as three bytes
        silentPolls = 0;
        if (! online) { online = true; if (onOnlineChanged) onOnlineChanged (true); }
        return;
    }
    if (n < 4 || d[0] != kDsiId || d[1] != kP6Id) return;
    silentPolls = 0;
    if (! online) { online = true; if (onOnlineChanged) onOnlineChanged (true); }
    if (d[2] == 0x03 && n >= 3 + 1171) { if (onEditBuffer) onEditBuffer (unpack (d + 3, n - 3, 1024)); }
    else if (d[2] == 0x02 && n >= 5 + 1171) { if (onProgram) onProgram (jlimit (0, 9, (int) d[3]), jlimit (0, 99, (int) d[4]), unpack (d + 5, n - 5, 1024)); }
}
void MidiLink::timerCallback()
{
    if (ownPending)
    {   // the port Logic's External Instrument can send to (MIDI Destination: Prophet 6 Panel)
        ownPending = false;
       #if JUCE_MAC || JUCE_LINUX
        own = MidiInput::createNewDevice (ownPortName(), this);
        if (own != nullptr) own->start();
       #endif
    }
    // the chosen devices: one that went away (USB unplugged, the Mac slept) is let go of, and one that is there again (or was off
    // when the panel started) is reopened - so the synth and the keyboard come back without a visit to the port boxes.
    // The list is asked for only when the system says it changed (and, as a safety net, every 16 seconds): asking the MIDI server
    // for it is what can hold the message thread up for seconds when a USB device is misbehaving
    if (! watching) { watching = true; devicesChanged = MidiDeviceListConnection::make ([this] { rescan = true; ++listVersion; }); }
    if (rescan || ++ticks % 8 == 0)
    {
        rescan = false;
        const auto ins = inputs(); const auto outs = outputs();
        auto present = [] (const Array<MidiDeviceInfo>& list, const String& id) { for (auto& d : list) if (d.identifier == id) return true; return false; };
        if (in != nullptr && ! present (ins, inId))    { in->stop(); in.reset(); inId = {}; }
        if (keys != nullptr && ! present (ins, kId))   { keys->stop(); keys.reset(); kId = {}; }
        if (haveOut.load() && ! present (outs, outId)) { haveOut = false; push (Item { {}, nullptr, true }); outId = {}; }
        if (in == nullptr && wIn.isNotEmpty() && present (ins, wIn))       { in = MidiInput::openDevice (wIn, this); if (in != nullptr) { in->start(); inId = wIn; retryConnect(); } }
        if (keys == nullptr && wKeys.isNotEmpty() && present (ins, wKeys)) { keys = MidiInput::openDevice (wKeys, this); if (keys != nullptr) { keys->start(); kId = wKeys; } }
        if (! haveOut.load() && wOut.isNotEmpty() && present (outs, wOut)) { auto o = MidiOutput::openDevice (wOut); if (o != nullptr) { push (Item { {}, std::move (o), true }); haveOut = true; outId = wOut; retryConnect(); } }
    }
    // The Prophet-6 clicks audibly on every SysEx it receives, so we never poll a synth that has answered:
    // a few inquiries after the ports are chosen (or the lamp is clicked), then silence.
    if (! haveOut.load() || in == nullptr) { if (online) { online = false; if (onOnlineChanged) onOnlineChanged (false); } return; }
    if (online) return;
    if (inquiriesLeft > 0) { --inquiriesLeft; requestInquiry(); }
}
