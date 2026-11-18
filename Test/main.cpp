#include <cmath>
#include "PluginProcessor.h"
#include "EuclidPage.h"
#include "PluginEditor.h"
#include <cstdio>
// Offline checks: 7-bit packing round trip, NRPN encoding, dump parsing into parameters, program image consistency.
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { printf ("%s  %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++fails; };

    // ---- packing: every byte value, 1024 bytes, round-trips and has the documented packed size
    juce::MemoryBlock prog ((size_t) P6::kProgramBytes);
    for (int i = 0; i < P6::kProgramBytes; ++i) ((uint8_t*) prog.getData())[i] = (uint8_t) ((i * 37 + 11) & 0xff);
    auto packed = MidiLink::pack (prog);
    check ((int) packed.getSize() == P6::kPackedBytes, "packed size is 1171");
    bool all7 = true; for (size_t i = 0; i < packed.getSize(); ++i) all7 &= (((uint8_t*) packed.getData())[i] & 0x80) == 0;
    check (all7, "packed bytes are all 7-bit");
    auto back = MidiLink::unpack ((const uint8_t*) packed.getData(), (int) packed.getSize(), P6::kProgramBytes);
    check (back == prog, "unpack(pack(x)) == x");
    // the manual's worked example: first packet MS byte holds bit 7 of A..G in bits 0..6
    { uint8_t a[7] = { 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80 }; auto p = MidiLink::pack (juce::MemoryBlock (a, 7));
      check (((uint8_t*) p.getData())[0] == 0b01010101 && ((uint8_t*) p.getData())[1] == 0, "MS-bit byte matches the manual's example"); }

    // ---- processor: a fake edit-buffer dump sets the parameters, and the program image follows the parameters
    P6PanelProcessor p;
    // pretend a learned layout: osc1Freq@0, lpFreq@30, lpRes@31, lfoShape@50, fx1Type@60, fx2Type@61, unisonMode@70, bpm@80
    auto setOff = [&] (const char* id, int o) { p.dumpOffset[(size_t) P6::indexOfId (id)] = o; };
    setOff ("osc1Freq", 0); setOff ("lpFreq", 30); setOff ("lpRes", 31); setOff ("lfoShape", 50); setOff ("fx1Type", 60); setOff ("fx2Type", 61); setOff ("unisonMode", 70); setOff ("bpm", 80); setOff ("hpFreq", 90);
    juce::MemoryBlock dump ((size_t) P6::kProgramBytes, true);
    auto* d = (uint8_t*) dump.getData();
    d[0] = 24; d[30] = 164; d[31] = 200; d[50] = 3; d[60] = 7; d[61] = 13; d[70] = 6; d[80] = 120; d[300] = 77;
    const char* nm = "Dub Chamber Test";
    for (int i = 0; i < 20; ++i) d[P6::kNameOffset + i] = (uint8_t) (i < (int) strlen (nm) ? nm[i] : ' ');
    p.applyProgram (dump, false);
    auto val = [&] (const char* id) { auto* q = p.apvts.getParameter (id); return juce::roundToInt (q->convertFrom0to1 (q->getValue())); };
    check (val ("osc1Freq") == 24, "osc1Freq from dump");
    check (val ("lpFreq") == 164 && val ("lpRes") == 200, "low-pass from dump (values above 127 survive)");
    check (val ("lfoShape") == 3 && val ("fx1Type") == 7 && val ("fx2Type") == 13 && val ("unisonMode") == 6, "choices from dump");
    check (val ("bpm") == 120, "bpm from dump");
    check (p.patchName() == "Dub Chamber Test", "name from dump");
    check (p.program[300] == 77, "untouched bytes (sequencer) are preserved in the image");
    // change a parameter, the image must follow after the flush
    { auto* q = p.apvts.getParameter ("lpRes"); q->setValueNotifyingHost (q->convertTo0to1 (99.f)); }
    p.flush();
    check (p.program[31] == 99, "program image follows a parameter change");
    p.setPatchName ("Hello");
    check (p.patchName() == "Hello" && p.program[P6::kNameOffset] == 'H' && p.program[P6::kNameOffset + 5] == ' ', "name writes into the image padded with spaces");
    // state round trip
    juce::MemoryBlock st; p.getStateInformation (st);
    P6PanelProcessor p2; p2.dumpOffset = p.dumpOffset; p2.setStateInformation (st.getData(), (int) st.getSize());
    p2.flush();
    check (memcmp (p.program, p2.program, P6::kProgramBytes) == 0, "state round trip keeps the whole image");
    // nrpn in → parameter, including the name area
    p.link.onNrpn (49, 150); p.link.onNrpn (236, 'Z');
    check (val ("hpFreq") == 150 && p.program[90] == 150 && p.program[P6::kNameOffset] == 'Z', "incoming NRPN updates parameter, image and name");
    // learn: reference dump, then a single changed byte after an NRPN -> mapping recorded
    p.learning = true; p.haveLastDump = false;
    p.link.onEditBuffer (dump);                       // reference
    juce::MemoryBlock dump2 = dump; ((uint8_t*) dump2.getData())[42] = 77;
    p.link.onNrpn (46, 77);                           // lpRes moved on the synth
    p.link.onEditBuffer (dump2);
    check (p.dumpOffset[(size_t) P6::indexOfId ("lpRes")] == 42, "learn maps NRPN 46 to the byte that changed");
    p.learning = false;
    // MOD settings: state round trip and the .p6patch file carries them behind the SysEx
    p.mod.lfo[0].on = true; p.mod.lfo[0].dest[0] = P6::indexOfId ("lpFreq"); p.mod.lfo[0].amt[0] = 0.4f; p.mod.seq.on = true; p.mod.seq.length = 7; p.mod.seq.step[3] = 0.75f; p.mod.xy.dest[1] = P6::indexOfId ("lpRes"); p.mod.env.sustain = 0.5f;
    { juce::MemoryBlock st2; p.getStateInformation (st2); P6PanelProcessor p3; p3.setStateInformation (st2.getData(), (int) st2.getSize());
      check (p3.mod.lfo[0].on && p3.mod.lfo[0].dest[0] == P6::indexOfId ("lpFreq") && p3.mod.seq.length == 7 && std::abs (p3.mod.seq.step[3] - 0.75f) < 0.01f && p3.mod.xy.dest[1] == P6::indexOfId ("lpRes") && std::abs (p3.mod.env.sustain - 0.5f) < 0.01f, "MOD settings survive the plugin state"); }
    { auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("p6modtest.p6patch"); p.savePatchFile (f);
      P6PanelProcessor p4; p4.dumpOffset = p.dumpOffset; const bool ok = p4.loadPatchFile (f); f.deleteFile();
      check (ok && p4.mod.lfo[0].on && p4.mod.seq.length == 7 && std::abs (p4.mod.lfo[0].amt[0] - 0.4f) < 0.01f, ".p6patch file carries the MOD settings"); }
    { p.mod = Mod::State(); juce::MemoryBlock st3; p.getStateInformation (st3); P6PanelProcessor p5; p5.setStateInformation (st3.getData(), (int) st3.getSize()); check (! p5.mod.any(), "empty MOD settings stay empty"); }
    // MOD automation parameters: the plugin parameter and the MOD setting stay in step both ways
    p.mod.lfo[0].on = true; p.mod.lfo[0].depth = 0.25f; p.mod.seq.amt = -0.5f; p.modSettingsChanged();
    check (Mod::autoGet (p.apvts, "modLfo1On") > 0.5f && std::abs (Mod::autoGet (p.apvts, "modLfo1Depth") - 0.25f) < 0.01f && std::abs (Mod::autoGet (p.apvts, "modSeqAmt") + 0.5f) < 0.01f, "MOD settings reach the automatable parameters");
    Mod::autoSet (p.apvts, "modLfo1Depth", 0.8f); Mod::autoSet (p.apvts, "modXYOn", 1.f); Mod::autoSet (p.apvts, "modX", -0.5f); p.flush();
    check (std::abs (p.mod.lfo[0].depth - 0.8f) < 0.01f && p.mod.autoXY && std::abs (p.mod.autoX + 0.5f) < 0.01f, "automation reaches the MOD settings");
    // ---- EUCLID: the engine runs from processBlock on the page's own clock, its notes count for the MOD envelope, the patch survives the state
    {
        P6PanelProcessor e; e.prepareToPlay (48000.0, 256); e.setPlayConfigDetails (2, 2, 48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        for (auto& t : e.euclid.patch.tr) t.mute = true; e.euclid.patch.tr[0].mute = false; e.euclid.patch.tr[0].steps = 16; e.euclid.patch.tr[0].beats = 4; e.euclid.patch.tr[0].rotate = 0; e.euclid.patch.tr[0].chance = 100; e.euclid.patch.tr[0].clock = 2;
        e.mod.bpm = 120.f; e.euclid.patch.on = true; e.euclidRun = true;
        const int before = e.hostNoteOns.load();
        for (int b = 0; b < 370; ++b) e.processBlock (buf, midi);   // just under four beats at 120 (375 blocks would touch beat 4)
        check (e.hostNoteOns.load() - before == 4, "EUCLID: four hits in a bar reach the note counter (free-running clock)");
        check (e.euclid.uiStep[0].load() >= 0, "EUCLID: the display knows the running step");
        e.euclidRun = false; e.processBlock (buf, midi); check (e.euclid.uiStep[0].load() == -1, "EUCLID: RUN off stops the tracks");
        // a parameter lock: the cutoff lane holds the parameter while it runs and lets go when it stops
        { auto* l = e.euclid.patch.tr[0].ensureLane (Euclid::kSynth + P6::indexOfId ("lpFreq")); for (auto& x : l->v) x = 0.3f; }
        e.euclid.patch.on = true; e.euclidRun = true; for (int b = 0; b < 100; ++b) e.processBlock (buf, midi);
        check (e.euclidLocks.load() == 1 && e.euclidLockSent[(size_t) P6::indexOfId ("lpFreq")] >= 0, "EUCLID: a cutoff lane locks the parameter while running");
        e.euclidRun = false; e.processBlock (buf, midi);
        check (e.euclidLocks.load() == 0 && e.euclidLockSent[(size_t) P6::indexOfId ("lpFreq")] < 0, "EUCLID: the lock is released when it stops");
        e.euclid.patch.tr[0].removeLane (Euclid::kSynth + P6::indexOfId ("lpFreq"));
        e.euclid.patch.tr[2].steps = 11; e.euclid.patch.tr[2].over[3] = 1; e.euclid.patch.scale = 4; e.euclid.patch.root = 9;
        juce::MemoryBlock st; e.getStateInformation (st); P6PanelProcessor e2; e2.setStateInformation (st.getData(), (int) st.getSize());
        check (e2.euclid.patch.tr[2].steps == 11 && e2.euclid.patch.tr[2].over[3] == 1 && e2.euclid.patch.scale == 4 && e2.euclid.patch.root == 9 && e2.euclid.patch.tr[0].mute == false && e2.euclid.patch.tr[1].mute, "EUCLID: the patch survives the plugin state");
    }
    // ---- the chord memory (LATCH): letting go in any order keeps the chord whole, legato changes drop the old notes, a gap starts afresh
    {
        MidiLink L; int nt[16]; auto chordIs = [&] (std::initializer_list<int> want, bool latch = true) { const int n = L.chord (nt, 16, latch); if (n != (int) want.size()) return false; int k = 0; for (int w : want) if (nt[k++] != w) return false; return true; };
        L.chordNote (60, true, 1000); L.chordNote (64, true, 1005); L.chordNote (67, true, 1010);
        L.chordNote (67, false, 2000); L.chordNote (64, false, 2010); L.chordNote (60, false, 2020);          // top first, bass last
        check (chordIs ({ 60, 64, 67 }), "LATCH: a chord let go top-down stays whole (it used to shrink to the bass note)");
        L.chordNote (60, true, 3000); L.chordNote (64, true, 3001); L.chordNote (67, true, 3002);
        L.chordNote (62, true, 3500); L.chordNote (65, true, 3501); L.chordNote (69, true, 3502);              // the next chord starts before the old one ends
        L.chordNote (60, false, 3510); L.chordNote (64, false, 3511); L.chordNote (67, false, 3512);
        check (chordIs ({ 62, 65, 69 }), "LATCH: legato - the old chord's notes drop out as they end");
        L.chordNote (65, false, 4000); L.chordNote (62, false, 4001); L.chordNote (69, false, 4002);
        L.chordNote (59, true, 5000);
        check (chordIs ({ 59 }), "LATCH: after a gap a new note starts a fresh chord");
        L.chordNote (59, false, 5100);
        check (chordIs ({}, false) && chordIs ({ 59 }), "LATCH off: nothing held means no chord");
        // the engine: with LATCH off a chord track rests while nothing is held, with LATCH on it plays the remembered chord
        Euclid::Engine e; for (auto& tr : e.patch.tr) tr.mute = true; auto& a = e.patch.tr[0]; a.mute = false; a.steps = 4; a.beats = 4; a.rotate = 0; a.chance = 100; a.clock = 6; a.ratchet = 1; a.chord = 1; a.src = 7;
        e.getChord = [&] (int* o, int m) { return L.chord (o, m, e.patch.latch); };
        std::vector<Euclid::Event> ev; int ons = 0; double ppq = 0.0;
        auto run = [&] { ons = 0; for (int b = 0; b < 360; ++b) { e.process (ppq, 120.0, 48000.0, 256, true, ev); for (auto& x : ev) if (x.msg.isNoteOn()) ++ons; ppq += 256.0 / 24000.0; } };
        e.patch.latch = true; run(); const int withLatch = ons;
        e.patch.latch = false; run(); const int without = ons;
        check (withLatch >= 3 && without == 0, "LATCH: on, the remembered chord plays; off, the chord track rests until keys are held");
    }
    // ==== November 2026: what the review found
    {   // ---- MUTE CHORD coming on while keys are held: their note-offs still reach the synth; a note that was held back gets no off
        MidiLink L; std::vector<juce::MidiMessage> sent; L.testTap = [&] (const juce::MidiMessage& m) { sent.push_back (m); }; L.channel = 3;
        auto count = [&] (bool on, int note) { int n = 0; for (auto& m : sent) n += (on ? m.isNoteOn() : m.isNoteOff()) && m.getNoteNumber() == note; return n; };
        L.keysIn (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)); L.keysIn (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100));
        L.muteKeys = true;                                                           // as a loaded pattern or a reopened project does
        L.keysIn (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100)); L.keysIn (juce::MidiMessage::noteOff (1, 60));
        check (count (true, 60) == 1 && count (false, 60) == 1 && count (true, 67) == 0, "MUTE CHORD on with keys held: the held key's note-off still goes to the synth, new notes are held back");
        L.channel = 5; L.keysIn (juce::MidiMessage::noteOff (1, 64));
        check (count (false, 64) == 1 && sent.back().isNoteOff() && sent.back().getChannel() == 3, "a note-off goes out on the channel its note-on went out on");
        L.muteKeys = false; L.keysIn (juce::MidiMessage::noteOff (1, 67));
        check (count (false, 67) == 0, "a note that was held back gets no note-off either");
        L.keysIn (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100)); L.muteKeys = true; L.allKeysOff(); L.keysIn (juce::MidiMessage::noteOff (1, 72));
        check (count (true, 72) == 1 && count (false, 72) == 1, "allKeysOff ends what was passed on, once");
    }
    {   // ---- the EUCLID page: keys are not rewritten by a key change, a pattern that switches MUTE CHORD on lets the sounding keys go, a 13th lane is refused
        P6PanelProcessor q; std::vector<juce::MidiMessage> sent; q.link.testTap = [&] (const juce::MidiMessage& m) { sent.push_back (m); };
        ui::EuclidPage page (q); auto& pt = q.euclid.patch; pt.scale = 6; pt.root = 0; pt.scaleOn = true; pt.on = true; pt.chordMute = false; pt.tr[0].pitch = 63; pt.tr[1].pitch = 61; page.refresh();
        page.setScale (6, 1); page.setScale (6, 0);
        check (pt.tr[0].pitch == 63 && pt.tr[1].pitch == 61, "EUCLID: going through keys and back leaves the tracks' pitches as they were set");
        check (page.rows[1]->cells[8]->text (61) == "D3" && page.rows[0]->cells[8]->text (63) == "D#3", "EUCLID: the PITCH cell shows the note the key snaps it to");
        q.link.keysIn (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)); sent.clear();
        pt.chordMute = true; page.refresh();
        int offs60 = 0; for (auto& m : sent) offs60 += m.isNoteOff() && m.getNoteNumber() == 60;
        check (q.link.muteKeys.load() && offs60 == 1, "EUCLID: MUTE CHORD switched on by a pattern releases the key that sounds");
        pt.on = false; page.refresh(); check (! q.link.muteKeys.load(), "EUCLID: with the page off MUTE CHORD does not hold the keys back");
        pt.on = true; pt.chordMute = false; page.refresh();
        auto& t = pt.tr[0]; for (auto& l : t.lanes) l.target = -1; for (int k = 0; k < Euclid::kMaxLanes; ++k) t.ensureLane (k < 8 ? k : Euclid::kSynth + k)->v[0] = 0.5f;
        const int first = t.lanes[0].target; page.rows[0]->selected = Euclid::kSynth + 40;
        check (page.laneFor (0) == nullptr && t.lanes[0].target == first && t.lanes[0].v[0] == 0.5f && t.laneCount() == Euclid::kMaxLanes, "EUCLID: a 13th lane is refused, the first one stays");
        page.rows[0]->selected = first; check (page.laneFor (0) == &t.lanes[0], "EUCLID: an existing lane is still found when all slots are taken");
        for (auto& l : t.lanes) l.target = -1;
    }
    {   // ---- LEARN / VERIFY stopped before the synth's edit buffer arrived: nothing is sent to it, the panel keeps its sound
        P6PanelProcessor q; std::vector<juce::MidiMessage> sent; q.link.testTap = [&] (const juce::MidiMessage& m) { sent.push_back (m); }; q.link.setOnlineForTest (true);
        auto dumps = [&] { int n = 0; for (auto& m : sent) n += m.isSysEx() && m.getSysExDataSize() > 100; return n; };
        q.setPatchName ("Keep me"); q.startAutoLearn(); q.flush(); q.stopAutoLearn();
        check (dumps() == 0 && q.patchName() == "Keep me" && ! q.autoLearning(), "stopped before the reference dump: no edit buffer is sent, the panel's sound stays");
        juce::MemoryBlock ref ((size_t) P6::kProgramBytes, true); ((uint8_t*) ref.getData())[P6::kNameOffset] = 'R'; for (int i = 1; i < 20; ++i) ((uint8_t*) ref.getData())[P6::kNameOffset + i] = ' ';
        sent.clear(); q.startAutoLearn(); q.flush(); q.link.onEditBuffer (ref); q.stopAutoLearn();
        check (dumps() == 1 && q.patchName() == "R", "stopped after the reference dump: the synth gets exactly that sound back");
        sent.clear(); q.startAutoLearn(); q.flush(); q.stopAutoLearn();
        check (dumps() == 0 && q.patchName() == "R", "a second run stopped early does not send the first run's reference");
    }
    {   // ---- a program change forgets what MOD last sent; a dump while MOD moves a knob keeps the panel's value for it
        P6PanelProcessor q; std::vector<juce::MidiMessage> sent; q.link.testTap = [&] (const juce::MidiMessage& m) { sent.push_back (m); };
        const int ci = P6::indexOfId ("lpFreq"), ri = P6::indexOfId ("lpRes"); auto val = [&] (int i) { return q.valueOf (i); };
        q.setParam (ci, 100); q.setParam (ri, 20); q.flush();
        q.mod.lfo[0].on = true; q.mod.lfo[0].sync = false; q.mod.lfo[0].shape = 4; q.mod.lfo[0].dest[0] = ci; q.mod.lfo[0].amt[0] = 0.2f; q.modSettingsChanged();
        juce::Thread::sleep (40); q.flush();
        check (q.modSent[(size_t) ci] >= 0, "MOD moves the cutoff");
        sent.clear(); q.selectProgram (0, 5); juce::Thread::sleep (40); q.flush(); juce::Thread::sleep (40); q.flush();
        int values = 0, pc = 0; for (auto& m : sent) { values += m.isController() && m.getControllerNumber() == 6; pc += m.isProgramChange(); }
        bool cleared = true; for (int v : q.modSent) cleared &= v < 0;
        check (pc == 1 && values == 0 && cleared, "program change: what MOD sent is forgotten, no value of the old program follows the program change");
        juce::MemoryBlock d1 = q.programBlock(); ((uint8_t*) d1.getData())[q.dumpOffset[(size_t) ci]] = 33; ((uint8_t*) d1.getData())[q.dumpOffset[(size_t) ri]] = 44;
        q.link.onEditBuffer (d1);
        check (val (ci) == 33 && val (ri) == 44, "the new program's dump is taken whole");
        q.mod.lfo[0].on = true; q.mod.lfo[0].sync = false; q.mod.lfo[0].shape = 4; q.mod.lfo[0].dest[0] = ci; q.mod.lfo[0].amt[0] = 0.2f; q.modSettingsChanged(); juce::Thread::sleep (40); q.flush();
        juce::MemoryBlock d2 = q.programBlock(); ((uint8_t*) d2.getData())[q.dumpOffset[(size_t) ci]] = 77; ((uint8_t*) d2.getData())[q.dumpOffset[(size_t) ri]] = 55;
        q.link.onEditBuffer (d2);
        check (q.modSent[(size_t) ci] >= 0 && val (ci) == 33 && val (ri) == 55, "FETCH while MOD moves the cutoff: the cutoff keeps the panel's value, everything else follows the synth");
    }
    {   // ---- bypass, or the audio stopping: the EUCLID notes that sound get their note-offs, and nothing else is sent
        P6PanelProcessor e; std::vector<juce::MidiMessage> sent; e.link.testTap = [&] (const juce::MidiMessage& m) { sent.push_back (m); };
        e.prepareToPlay (48000.0, 256); e.setPlayConfigDetails (2, 2, 48000.0, 256); juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        for (auto& t : e.euclid.patch.tr) t.mute = true; auto& t = e.euclid.patch.tr[0]; t.mute = false; t.steps = 16; t.beats = 16; t.chord = 3; t.length = 200; t.clock = 2; t.chance = 100;
        e.mod.bpm = 120.f; e.euclid.patch.on = true; e.euclidRun = true;
        auto open = [&] { int n = 0; for (auto& m : sent) n += m.isNoteOn() ? 1 : m.isNoteOff() ? -1 : 0; return n; };
        for (int b = 0; b < 40; ++b) e.processBlock (buf, midi);
        const int before = open(); const size_t n0 = sent.size(); e.processBlockBypassed (buf, midi);
        bool onlyOffs = true; for (size_t i = n0; i < sent.size(); ++i) onlyOffs &= sent[i].isNoteOff();
        check (before > 0 && open() == 0 && onlyOffs, "bypass: every sounding EUCLID note gets its note-off, nothing else goes out");
        const size_t n1 = sent.size(); e.processBlockBypassed (buf, midi); check (sent.size() == n1, "bypass: after that nothing is sent");
        for (int b = 0; b < 40; ++b) e.processBlock (buf, midi);
        const int again = open(); e.releaseResources(); check (again > 0 && open() == 0, "the audio stops (releaseResources): the sounding notes end");
    }
    {   // ---- SAVE of a library sound under a name another sound of the category has: the panel knows, and asks before replacing
        P6PanelProcessor q; auto dir = P6PanelProcessor::libraryDir().getChildFile ("zz panel test"); dir.createDirectory();
        dir.getChildFile (juce::String ("Aaa") + ".p6patch").replaceWithText ("x"); dir.getChildFile (juce::String ("Bbb") + ".p6patch").replaceWithText ("x");
        q.libKey = "lib:zz panel test/Aaa"; q.setPatchName ("Aaa"); auto s1 = q.librarySave();
        q.setPatchName ("Bbb"); auto s2 = q.librarySave(); q.setPatchName ("Ccc"); auto s3 = q.librarySave();
        check (s1.same && ! s1.replacesOther && ! s2.same && s2.replacesOther && s2.file == dir.getChildFile (juce::String ("Bbb") + ".p6patch") && ! s3.same && ! s3.replacesOther, "SAVE under another sound's name is recognised (the panel then asks before replacing)");
        dir.deleteRecursively();
    }
    {   // ---- EUCLID timing: the notes leave at their own moments, not at the starts of the audio blocks. The host calls unevenly
        //      here (two blocks at once, then a pause twice as long), as Logic does; sixteenths at 120 are 125 ms apart
        juce::CriticalSection cs; std::vector<double> ons;
        P6PanelProcessor e; e.prepareToPlay (48000.0, 512); e.setPlayConfigDetails (2, 2, 48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        e.mod.bpm = 120.f; for (auto& t : e.euclid.patch.tr) t.mute = true;
        auto& t0 = e.euclid.patch.tr[0]; t0.mute = false; t0.steps = 16; t0.beats = 16; t0.rotate = 0; t0.chance = 100; t0.clock = 2;
        e.link.sentTap = [&] (const juce::MidiMessage& m) { if (m.isNoteOn()) { const juce::ScopedLock sl (cs); ons.push_back (juce::Time::getMillisecondCounterHiRes()); } };
        e.link.openForTest(); e.euclid.patch.on = true; e.euclidRun = true;
        const double len = 512000.0 / 48000.0, startMs = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < 280; b += 2) { while (juce::Time::getMillisecondCounterHiRes() < startMs + b * len) juce::Thread::sleep (1); e.processBlock (buf, midi); e.processBlock (buf, midi); }
        e.euclidRun = false; e.processBlock (buf, midi); juce::Thread::sleep (80);
        double worst = 0.0; int n = 0; { const juce::ScopedLock sl (cs); n = (int) ons.size(); for (size_t i = 1; i < ons.size(); ++i) worst = juce::jmax (worst, std::abs (ons[i] - ons[i - 1] - 125.0)); }
        printf ("      (%d notes, the worst distance is %.2f ms off)\n", n, worst);
        check (n >= 20 && worst < 3.0, "EUCLID timing: notes leave at their own moments inside the block (sixteenths stay 125 ms apart while the host calls unevenly)");
    }
    {   // ---- A B C D: four sounds side by side. A letter keeps its sound and where it came from; nothing but the edit buffer is sent
        P6PanelProcessor q; const int ci = P6::indexOfId ("lpFreq"); auto val = [&] (int i) { return q.valueOf (i); };
        q.currentBank = 5; q.currentProgram = 1; q.setPatchName ("First Pad"); q.setParam (ci, 100); q.flush();
        q.switchMemory (1);   // B was empty: it starts as a copy
        check (q.memActive == 1 && val (ci) == 100 && q.patchName() == "First Pad" && q.mem[0].used && q.mem[0].origin == "501  First Pad", "A B C D: an empty letter starts as a copy of the sound in use, and A keeps its sound's name");
        q.currentBank = 5; q.currentProgram = 72; q.setPatchName ("Second One"); q.setParam (ci, 30); q.flush();
        q.switchMemory (0);
        check (val (ci) == 100 && q.patchName() == "First Pad" && q.currentBank == 5 && q.currentProgram == 1, "A B C D: back on A - its sound, its name and its slot are there again");
        q.setParam (ci, 64); q.flush(); q.switchMemory (1);
        check (val (ci) == 30 && q.patchName() == "Second One" && q.currentProgram == 72, "A B C D: B kept what was done to it");
        q.switchMemory (0); check (val (ci) == 64, "A B C D: ... and A what was done to it meanwhile");
        check (! q.mem[2].used && q.memoryText (2).contains ("empty") && q.memoryText (1).contains ("First Pad") && q.memoryText (0).contains ("in use"), "A B C D: each letter says what it holds (B started as a copy of First Pad)");
        q.switchMemory (2); q.switchMemory (0); q.clearMemory (2); q.clearMemory (0);
        check (! q.mem[2].used && q.memActive == 0 && q.valueOf (ci) == 64, "A B C D: a letter can be emptied again - but not the one in use");
        juce::MemoryBlock st; q.getStateInformation (st);
        P6PanelProcessor r; r.setStateInformation (st.getData(), (int) st.getSize());
        check (r.memActive == 0 && r.valueOf (ci) == 64 && r.mem[1].used && ! r.mem[2].used && r.mem[1].origin == q.mem[1].origin, "A B C D: the letters are saved with the project");
        r.switchMemory (1); check (r.valueOf (ci) == 30 && r.patchName() == "Second One", "A B C D: ... and their sounds come back from it");
    }
    {   // ---- EUCLID is off in a new panel, and a project saved by an older panel with the untouched page (on by default then) comes back off
        P6PanelProcessor a; check (! a.euclid.patch.on, "EUCLID: a new panel does not play the synth by itself");
        a.euclid.patch.on = true; juce::MemoryBlock st; a.getStateInformation (st);
        P6PanelProcessor b; b.setStateInformation (st.getData(), (int) st.getSize()); check (! b.euclid.patch.on, "EUCLID: an untouched page saved as on (older panels) is off after loading");
        a.euclid.patch.tr[0].beats = 5; juce::MemoryBlock st2; a.getStateInformation (st2);
        P6PanelProcessor c; c.setStateInformation (st2.getData(), (int) st2.getSize()); check (c.euclid.patch.on && c.euclid.patch.tr[0].beats == 5, "EUCLID: a page that was set up and switched on stays on");
    }
    {   // ---- LFO FREQUENCY with SYNC on: 18 zones of 14 values, as measured on the synth
        using juce::String;
        check (String (P6::lfoSyncName (0)) == "> 4 bars" && String (P6::lfoSyncName (13)) == "> 4 bars" && String (P6::lfoSyncName (14)) == "4 bars" && String (P6::lfoSyncName (56)) == "1 bar"
               && String (P6::lfoSyncName (112)) == "1/4" && String (P6::lfoSyncName (125)) == "1/4" && String (P6::lfoSyncName (126)) == "1/4 T" && String (P6::lfoSyncName (154)) == "1/8"
               && String (P6::lfoSyncName (196)) == "1/16" && String (P6::lfoSyncName (238)) == "1/32 T" && String (P6::lfoSyncName (254)) == "1/32 T", "LFO SYNC: the knob's zones carry the measured note values");
    }
    {   // ---- MY NOTE: the user's own words about a sound, kept in notes.txt beside the ratings and read again by the next instance
        auto f = P6PanelProcessor::notesFile(); const bool had = f.existsAsFile(); const auto before = had ? f.loadFileAsString() : juce::String();
        {
            P6PanelProcessor a; a.setNote ("501", "  warm pad | good for dub\n "); a.setNote ("lib:zz/Deep One", "bass, needs filter down");
            check (a.note ("501") == "warm pad / good for dub" && a.note ("777").isEmpty(), "MY NOTE: a note is kept as one clean line");
            P6PanelProcessor b; check (b.note ("501") == "warm pad / good for dub" && b.note ("lib:zz/Deep One") == "bass, needs filter down", "MY NOTE: the next instance reads the notes again");
            b.setNote ("501", ""); b.setNote ("lib:zz/Deep One", " ");
            P6PanelProcessor c; check (c.note ("501").isEmpty() && c.note ("lib:zz/Deep One").isEmpty(), "MY NOTE: emptied notes are gone");
        }
        if (had) f.replaceWithText (before); else check (! f.existsAsFile(), "MY NOTE: with no notes left, no file is left either");
    }
    if (argc > 2 && juce::String (argv[1]) == "--panel")
    {   // a picture of the front panel, with A and B holding sounds and B in use
        P6PanelProcessor a; a.setPatchName ("First Pad"); a.switchMemory (1); a.setPatchName ("Second One"); a.setParam (P6::indexOfId ("lfoSync"), 1); a.setParam (P6::indexOfId ("lfoFreq"), 118);
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        juce::PNGImageFormat png; juce::File out (argv[2]); out.deleteFile(); juce::FileOutputStream os (out);
        png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds()), os);
    }
    if (argc > 2 && juce::String (argv[1]) == "--shot")
    {   // a picture of the browser, with two notes
        auto f = P6PanelProcessor::notesFile();
        {
            P6PanelProcessor a; a.setNote ("501", "warm, slow attack - good dub pad with filter at 9 o'clock"); a.setNote ("507", "too bright");
            MoogLook look; juce::LookAndFeel::setDefaultLookAndFeel (&look);
            {
                ui::PresetBrowser pb (a); pb.setSize (1300, 620); pb.select (1, false);
                juce::Image img (juce::Image::RGB, 1300, 620, true); { juce::Graphics g (img); g.fillAll (MoogLook::panel); pb.paintEntireComponent (g, true); }
                juce::PNGImageFormat png; juce::File out (argv[2]); out.deleteFile(); juce::FileOutputStream os (out); png.writeImageToStream (img, os);
            }
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }
        f.deleteFile();
    }
    printf ("%d failures\n", fails);
    return fails;
}
