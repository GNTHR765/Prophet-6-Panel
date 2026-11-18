#include "ClaudeData.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{   // the panel's parameters plus the MOD settings Logic can automate
    auto l = P6::createLayout(); std::vector<std::unique_ptr<RangedAudioParameter>> p; Mod::addAutoParams (p);
    l.add (p.begin(), p.end()); return l;
}
P6PanelProcessor::P6PanelProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "P6Panel", makeLayout()),
      dirty (P6::params().size()), lastSent (P6::params().size(), -1), modSent (P6::params().size(), -1)
{
    for (auto& d : dirty) d = false;
    for (auto& q : P6::params()) apvts.addParameterListener (q.id, this);
    for (auto& a : Mod::autoParams()) apvts.addParameterListener (a.id, this);
    memset (program, 0, sizeof (program));
    for (int i = 0; i < P6::kNameLength; ++i) program[P6::kNameOffset + i] = ' ';
    setPatchName ("Init");
    link.onNrpn = [this] (int n, int v) { onNrpnIn (n, v); };
    link.onKeysNote = [this] { triggerEnv(); }; link.onKeysNoteOff = [this] { releaseEnv(); };
    link.onNote = [this] (int n, bool on) { onNoteIn (n, on); if (on) triggerEnv(); else releaseEnv(); };   // the synth's own notes (Param Xmit = nas) drive the MOD envelope too
    link.onProgramChange = [this] (int bank, int prog) { currentBank = bank; currentProgram = prog; originPending = true; expectFreshProgram(); link.requestEditBuffer(); if (! scanning) loadModFor (bank * 100 + prog); ++uiVersion; };
    dumpOffset.resize (P6::params().size());
    for (int i = 0; i < (int) P6::params().size(); ++i) dumpOffset[(size_t) i] = P6::params()[(size_t) i].dump;
    loadLayout();   // a learned layout file overrides the baked-in offsets
    loadGlobals();
    loadCatalogue();
    link.onEditBuffer = [this] (const MemoryBlock& b) { onDumpIn (b); };
    link.onProgram = [this] (int bank, int prog, const MemoryBlock& b)
    {
        if (scanning)
        {   // a scan reply: record the name, move on; the panel's own program stays untouched
            const auto* d = (const uint8_t*) b.getData(); String name; for (int i = 0; i < P6::kNameLength; ++i) name << (char) jlimit (32, 125, (int) d[P6::kNameOffset + i]);
            synthNames[bank * 100 + prog] = name.trimEnd();
            if (bank * 100 + prog >= scanSlot) { scanSlot = bank * 100 + prog + 1; scanSentAt = 0; }
            if (scanSlot > 999) { stopNameScan(); }
            ++uiVersion; return;
        }
        currentBank = bank; currentProgram = prog; originPending = true; expectFreshProgram(); onDumpIn (b); loadModFor (bank * 100 + prog);
    };
    loadSynthNames(); loadRatings(); loadNotes();
    // EUCLID is off in a new panel: nothing plays the synth until its ON is pressed (the front panel's EUCLID button is lit while it is on)
    euclidFresh = euclid.patch.toTree().toXmlString(); euclid.patch.on = false;
    // the EUCLID page's parameter locks: a lane value pushes the parameter away from the program's value for that step, 0 puts it back
    euclidLockSent.assign (P6::params().size(), -1); euclidEvents.reserve ((size_t) Euclid::Engine::kOutRoom);   // room for a block's events before the audio starts
    euclid.getChord = [this] (int* out, int maxNotes) { return link.chord (out, maxNotes, euclid.patch.latch); };
    Euclid::paramNames().idOf = [] (int i) { return i >= 0 && i < (int) P6::params().size() ? juce::String (P6::params()[(size_t) i].id) : juce::String(); };
    Euclid::paramNames().indexOf = [] (const juce::String& id) { return P6::indexOfId (id); };
    euclid.onParamLock = [this] (int i, float off)
    {
        if (i < 0 || i >= (int) P6::params().size()) return;
        auto& q = P6::params()[(size_t) i]; if (q.kind != P6::Kind::Knob) return;
        int& last = euclidLockSent[(size_t) i]; const int base = valueOf (i);
        if (off == 0.f) { if (last >= 0) { if (last != base) link.sendNrpnFromAudio (q.nrpn, base); last = -1; --euclidLocks; } return; }
        const int v = jlimit (q.min, q.max, roundToInt (base + off * (float) (q.max - q.min)));
        if (last < 0) ++euclidLocks;
        if (v != last) { link.sendNrpnFromAudio (q.nrpn, v); last = v; }
    };
    startTimerHz (60);
}
P6PanelProcessor::~P6PanelProcessor() { stopTimer(); { std::vector<Euclid::Event> ev; euclid.allOff (ev); for (auto& e : ev) link.sendRaw (e.msg); } for (auto& q : P6::params()) apvts.removeParameterListener (q.id, this); for (auto& a : Mod::autoParams()) apvts.removeParameterListener (a.id, this); }

void P6PanelProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{   // audio and MIDI pass through untouched; the MOD page only reads the host's clock and counts its notes (envelope trigger)
    juce::ScopedNoDenormals nd;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) { if (pos->getBpm()) hostBpm = *pos->getBpm(); if (pos->getPpqPosition()) hostPpq = *pos->getPpqPosition(); hostPlaying = pos->getIsPlaying(); }
    for (const auto m : midi) { if (m.getMessage().isNoteOn()) ++hostNoteOns; else if (m.getMessage().isNoteOff()) ++hostNoteOffs; }
    {   // the EUCLID page: its notes go straight out of the panel's port, on the synth's channel, timed from the host's beat
        //  (or from a clock of its own while RUN is on and the host stands still); they count as notes for the MOD envelope too
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0; const int numSamples = jmax (1, buffer.getNumSamples());
        const bool hostOn = euclidHostClock(), free = ! hostOn && euclidRun.load();
        const double bpm = hostBpm.load() > 0 ? hostBpm.load() : (double) mod.bpm;
        double ppq = 0.0;
        if (hostOn) { ppq = hostPpq.load(); euclidWasFree = false; }
        else if (free) { if (! euclidWasFree) { euclidFreePpq = 0.0; euclidWasFree = true; } ppq = euclidFreePpq; euclidFreePpq += numSamples / (sr * 60.0 / jmax (20.0, bpm)); }
        else euclidWasFree = false;
        euclid.channel = jlimit (1, 16, link.channel);
        if (euclidPanic.exchange (false)) { euclid.allOff (euclidEvents); for (auto& e : euclidEvents) link.sendFromAudio (e.msg); }
        euclid.process (ppq, bpm, sr, numSamples, hostOn || free, euclidEvents);
        // each event at its own moment inside the block (the block's clock time + its offset), not all at the block's start
        const double blockMs = euclidClock.start (Time::getMillisecondCounterHiRes(), numSamples, sr);
        for (auto& e : euclidEvents) { link.sendFromAudioAt (e.msg, blockMs + 1000.0 * e.sampleOffset / sr); if (e.msg.isNoteOn()) ++hostNoteOns; else if (e.msg.isNoteOff()) ++hostNoteOffs; }
    }
}
void P6PanelProcessor::processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer& midi)
{   // bypassed: the page plays nothing - but what it had sounding must end (its note-offs waited in the engine and would never go
    // out), and its parameter locks go back. Nothing else is sent
    AudioProcessor::processBlockBypassed (buffer, midi);
    euclidPanic = false; euclidWasFree = false; euclid.allOff (euclidEvents);
    for (auto& e : euclidEvents) { link.sendFromAudio (e.msg); if (e.msg.isNoteOff()) ++hostNoteOffs; }
}
void P6PanelProcessor::releaseResources()
{   // the host stops the audio (no more blocks): the same, from here
    std::vector<Euclid::Event> ev; euclid.allOff (ev); for (auto& e : ev) link.sendRaw (e.msg);
}

// ---------------------------------------------------------------- values
int P6PanelProcessor::valueOf (int i) const
{
    auto& q = P6::params()[(size_t) i];
    auto* p = apvts.getParameter (q.id);
    if (q.kind == P6::Kind::Knob) return jlimit (q.min, q.max, roundToInt (p->convertFrom0to1 (p->getValue())));
    if (q.kind == P6::Kind::Toggle) return p->getValue() > 0.5f ? 1 : 0;
    return jlimit (q.min, q.max, roundToInt (p->convertFrom0to1 (p->getValue())));
}
void P6PanelProcessor::setParamFromSynth (int i, int value)
{
    auto& q = P6::params()[(size_t) i];
    auto* p = apvts.getParameter (q.id);
    value = jlimit (q.min, q.max, value);
    lastSent[(size_t) i] = value;                                  // so the change listener does not echo it back
    if (dumpOffset[(size_t) i] >= 0) program[dumpOffset[(size_t) i]] = (uint8_t) value;
    p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
}
void P6PanelProcessor::parameterChanged (const String& id, float)
{
    if (restoring) return;
    const int i = P6::indexOfId (id);
    if (i >= 0) dirty[(size_t) i] = true;
    else if (id.startsWith ("mod") && ! syncingMod) modParamsDirty = true;   // automation from the host: picked up by the timer
}
void P6PanelProcessor::timerCallback()
{
    if (resendPending && link.isOnline()) { resendPending = false; sendAll(); }
    if (fetchPending && link.isOnline()) { fetchPending = false; link.requestEditBuffer(); }
    if (scanning && scanSlot <= 999 && Time::currentTimeMillis() - scanSentAt > 400) { scanSentAt = Time::currentTimeMillis(); link.requestProgram (scanSlot / 100, scanSlot % 100); }   // (re)ask until the reply moves scanSlot on
    if (seqDumpAt > 0 && Time::currentTimeMillis() >= seqDumpAt) { seqDumpAt = 0; link.sendEditBuffer (programBlock()); }
    if (learning && learnNrpn >= 0 && ! learnFetchPending && Time::currentTimeMillis() - learnTime > 800) { learnFetchPending = true; link.requestEditBuffer(); }
    if (autoStage != 0)
    {   // auto-learn: after each test value has settled, read the edit buffer (and ask again if the answer does not come)
        const auto now = Time::currentTimeMillis();
        if (! autoWait && now >= autoAt) { link.requestEditBuffer(); autoWait = true; autoAt = now + 1500; if (autoStage == 2 || autoStage == 4) ++autoStage; }
        else if (autoWait && now >= autoAt) { if (++autoRetries > 3 && ! haveAutoRef) autoFinish (true);   // the reference never came: stop here, nothing was changed and nothing is sent
                                               else if (autoRetries > 3) { probeReport << "NRPN " << (autoPos < autoQueue.size() ? autoQueue[autoPos].nrpn : -1) << ": no answer from the synth\n"; ++autoPos; autoRetries = 0; autoNext(); } else { link.requestEditBuffer(); autoAt = now + 1500; } }
    }
    for (int i = 0; i < (int) dirty.size(); ++i)
    {
        if (! dirty[(size_t) i].exchange (false)) continue;
        const int v = valueOf (i);
        auto& q = P6::params()[(size_t) i];
        if (dumpOffset[(size_t) i] >= 0) program[dumpOffset[(size_t) i]] = (uint8_t) v;
        if (v == lastSent[(size_t) i]) continue;
        lastSent[(size_t) i] = v;
        link.sendNrpn (q.nrpn, v);
    }
    if (modParamsDirty.exchange (false)) { mod.fromParams (apvts); ++uiVersion; }
    modTick();
}
void P6PanelProcessor::onNrpnIn (int nrpn, int value)
{
    if (vintageNrpn >= 0 && nrpn == vintageNrpn) { vintageState = value > 0 ? 1 : 0; saveGlobals(); ++uiVersion; return; }
    if (vintageLearn && nrpn >= 1024 && nrpn != 1044 && nrpn != 1088)
    {   // the first global the synth reports after VINTAGE was clicked: that is the Vintage switch
        vintageNrpn = nrpn; vintageState = value > 0 ? 1 : 0; vintageLearn = false; saveGlobals();
        lastUnknown = "VINTAGE learned (NRPN " + String (nrpn) + ") - the panel's switch now works"; ++uiVersion; return;
    }
    if (autoStage != 0) return;   // auto-learn is moving things on purpose
    if (nrpn >= 256 && nrpn < 1024)
    {   // a sequencer step the synth changed (note at 256 + track*128 + step, velocity 64 further on): keep the SEQ page in step
        program[kSeqBase + nrpn - 256] = (uint8_t) jlimit (0, 255, value); ++uiVersion; return;
    }
    if (nrpn >= 236 && nrpn < 256) { program[P6::kNameOffset + nrpn - 236] = (uint8_t) jlimit (32, 125, value); ++uiVersion; return; }
    const int i = P6::indexOfNrpn (nrpn);
    if (i >= 0) setParamFromSynth (i, value);
    else if (nrpn == 1088) { seqPlaying = value > 0; if (! seqPlaying) playStep = -1; ++uiVersion; }
    else if (nrpn == 1044) { altTuning = jlimit (0, 16, value); ++uiVersion; }
    else { lastUnknown = "synth sent NRPN " + String (nrpn) + " = " + String (value) + " (not a panel parameter)"; ++uiVersion; }
    if (learning) { learnNrpn = nrpn; learnValue = value; learnTime = Time::currentTimeMillis(); }
}
void P6PanelProcessor::onNoteIn (int note, bool on)
{
    noteHeld[note & 127] = on;
    if (on)
    {   // note-ons from the synth's sequencer: those within ~50 ms are one step (a chord); find, from the previous step on,
        // the first step that starts all of them - that is the playhead
        const auto now = Time::currentTimeMillis();
        if (now - lastNoteAt > 50) { burstNotes.clear(); burstBase = playStep; }
        lastNoteAt = now; burstNotes.push_back (note);
        const int len = seqLength();
        if (len > 0 && seqPlaying)
        {
            int best = -1, bestScore = 0;
            for (int k = 1; k <= len; ++k)
            {
                const int s = (burstBase + k) % len; int score = 0;
                for (int n : burstNotes) for (int t = 0; t < kSeqTracks; ++t) if (seqOn (t, s) && seqNote (t, s) == n) { ++score; break; }
                if (score == (int) burstNotes.size()) { best = s; break; }
                if (score > bestScore) { bestScore = score; best = s; }
            }
            if (best >= 0) playStep = best;
        }
    }
    ++uiVersion;
}
void P6PanelProcessor::onDumpIn (const MemoryBlock& b)
{
    if (b.getSize() < (size_t) P6::kProgramBytes) return;
    const auto* d = (const uint8_t*) b.getData();
    if (autoStage != 0) { autoDump (d); return; }
    const bool fresh = freshProgramUntil != 0; freshProgramUntil = 0;   // the dump of a program that was just selected: every value is the program's own
    if (learning && haveLastDump && learnNrpn >= 0)
    {
        std::vector<int> changed; for (int o = 0; o < P6::kProgramBytes; ++o) if (d[o] != lastDump[o]) changed.push_back (o);
        const int i = P6::indexOfNrpn (learnNrpn);
        int hit = -1;
        if (changed.size() == 1) hit = changed[0];
        else
        {   // several bytes moved (a knob sweep, or the synth touched something else too): the one that now holds
            // the value the synth reported, and that no other parameter already owns
            for (int o : changed)
            {
                if (d[o] != (uint8_t) (learnValue & 0xff)) continue;
                bool taken = false; for (int k = 0; k < (int) dumpOffset.size(); ++k) if (k != i && dumpOffset[(size_t) k] == o) taken = true;
                if (! taken) { hit = o; break; }
            }
        }
        if (hit >= 0 && i >= 0)
        {
            for (int k = 0; k < (int) dumpOffset.size(); ++k) if (k != i && dumpOffset[(size_t) k] == hit) dumpOffset[(size_t) k] = -1;   // a byte belongs to one parameter
            dumpOffset[(size_t) i] = hit; saveLayout();
            learnStatus = String (P6::params()[(size_t) i].name) + "  ->  byte " + String (hit) + "   (" + String (knownOffsets()) + " of " + String (P6::params().size()) + " known)";
        }
        else if (hit >= 0) learnStatus = "NRPN " + String (learnNrpn) + " is not a panel parameter (byte " + String (hit) + ")";
        else
        {
            String bs; for (int o : changed) bs << o << " ";
            learnStatus = "ambiguous for NRPN " + String (learnNrpn) + " (bytes " + bs.trim() + " changed) - move only that one control, then again";
        }
        learnNrpn = -1; learnFetchPending = false; ++uiVersion;
    }
    else if (learning) { learnStatus = "reference dump taken - now move one control on the synth"; learnNrpn = -1; learnFetchPending = false; ++uiVersion; }
    memcpy (lastDump, d, P6::kProgramBytes); haveLastDump = true;
    if (! learning && fresh) applyProgram (b, false);
    else if (! learning)
    {   // the synth's edit buffer while MOD or an EUCLID lock holds a parameter away from its value: the dump shows where it was pushed
        // to at that moment. Taking that as the knob's value would bake the modulation in (and WRITE would store it): the panel's own value stays
        MemoryBlock own (b); auto* o = (uint8_t*) own.getData();
        for (int i = 0; i < (int) P6::params().size(); ++i) if ((modSent[(size_t) i] >= 0 || euclidLockSent[(size_t) i] >= 0) && dumpOffset[(size_t) i] >= 0) o[dumpOffset[(size_t) i]] = (uint8_t) valueOf (i);
        applyProgram (own, false);
    }
    else { memcpy (program, d, P6::kProgramBytes); ++uiVersion; }
}
// ---------------------------------------------------------------- auto-learn
void P6PanelProcessor::startAutoLearn()
{
    if (autoStage != 0) return;
    if (! link.isOnline()) { learnStatus = "AUTO-LEARN needs the synth on-line"; ++uiVersion; return; }
    autoQueue.clear(); autoPos = 0; autoRetries = 0; autoFound = 0; autoHidden = 0; haveAutoRef = false;
    probeReport = "Prophet 6 Panel - auto-learn " + Time::getCurrentTime().toString (true, true) + "\n\n";
    std::vector<bool> named (200, false); for (auto& q : P6::params()) if (q.nrpn < 200) named[(size_t) q.nrpn] = true;
    for (int i = 0; i < (int) P6::params().size(); ++i)
        if (dumpOffset[(size_t) i] < 0 && ! notStored (i))
        {   // two distinct test values inside the parameter's range
            auto& q = P6::params()[(size_t) i];
            const int v1 = q.max - q.min >= 8 ? q.min + 3 : q.min, v2 = q.max - q.min >= 8 ? q.min + 6 : q.min + 1;
            autoQueue.push_back ({ q.nrpn, i, v1, v2 });
        }
    for (int n = 0; n < 200; ++n) if (! named[(size_t) n]) autoQueue.push_back ({ n, -1, 0, 1 });   // numbers the manual does not name: anything hidden there?
    autoStage = 1; autoWait = false; autoAt = 0;
    learnStatus = "AUTO-LEARN: reading the synth ..."; ++uiVersion;
}
void P6PanelProcessor::stopAutoLearn() { if (autoStage != 0) autoFinish (true); }
void P6PanelProcessor::autoNext()
{
    if (autoPos >= autoQueue.size()) { autoFinish (false); return; }
    auto& p = autoQueue[autoPos];
    link.sendNrpn (p.nrpn, p.v1); autoStage = 2; autoWait = false; autoRetries = 0; autoAt = Time::currentTimeMillis() + 250;
    learnStatus = "AUTO-LEARN " + String ((int) autoPos + 1) + " of " + String ((int) autoQueue.size()) + (p.param >= 0 ? "  -  " + String (P6::params()[(size_t) p.param].name) : String ("  -  checking unnamed NRPNs"));
    ++uiVersion;
}
void P6PanelProcessor::autoDump (const uint8_t* d)
{
    autoWait = false; autoRetries = 0;
    if (autoStage == 1) { memcpy (autoRef, d, P6::kProgramBytes); haveAutoRef = true; autoNext(); return; }
    if (autoPos >= autoQueue.size()) { autoFinish (false); return; }
    auto& p = autoQueue[autoPos];
    if (autoStage == 3) { memcpy (autoA, d, P6::kProgramBytes); link.sendNrpn (p.nrpn, p.v2); autoStage = 4; autoAt = Time::currentTimeMillis() + 250; return; }
    if (autoStage != 5) return;
    std::vector<int> hits, changed;
    for (int o = 0; o < P6::kProgramBytes; ++o)
    {
        if (autoA[o] == d[o]) continue;
        changed.push_back (o);
        if (autoA[o] == (uint8_t) p.v1 && d[o] == (uint8_t) p.v2) hits.push_back (o);
    }
    if (p.param >= 0)
    {
        auto& q = P6::params()[(size_t) p.param];
        std::vector<int> free; for (int o : hits) { bool taken = false; for (int k = 0; k < (int) dumpOffset.size(); ++k) if (k != p.param && dumpOffset[(size_t) k] == o) taken = true; if (! taken) free.push_back (o); }
        if (free.size() == 1) { dumpOffset[(size_t) p.param] = free[0]; saveLayout(); ++autoFound; probeReport << q.name << " (NRPN " << q.nrpn << ")  ->  byte " << free[0] << "\n"; }
        else { String bs; for (int o : changed) bs << o << " "; probeReport << q.name << " (NRPN " << q.nrpn << "): not found - bytes that changed: " << (bs.isEmpty() ? String ("none") : bs.trim()) << "\n"; }
    }
    else if (! changed.empty())
    {
        String bs; for (int o : changed) bs << o << " ";
        ++autoHidden; probeReport << "unnamed NRPN " << p.nrpn << " moves byte(s) " << bs.trim() << (hits.empty() ? "" : "  (follows the value)") << "\n";
    }
    ++autoPos; autoNext();
}
void P6PanelProcessor::autoFinish (bool aborted)
{
    autoStage = 0; autoWait = false;
    const bool hadRef = haveAutoRef; haveAutoRef = false;
    if (hadRef)
    {   // only with the reference of this run: stopped before it arrived, nothing was changed on the synth - and an empty (or an
        // earlier run's) reference must never be sent to it or loaded into the panel
        MemoryBlock ref (autoRef, P6::kProgramBytes);
        link.sendEditBuffer (ref);                    // the synth gets its sound back exactly as it was
        applyProgram (ref, false);
    }
    probeReport << "\n" << (aborted ? "stopped early" : "done") << ": " << autoFound << " parameter(s) learned, " << autoHidden << " unnamed NRPN(s) that move a byte\n";
    libraryDir().getChildFile ("probe.txt").replaceWithText (probeReport);
    haveLastDump = false; learnNrpn = -1; learnFetchPending = false;
    learnStatus = String (aborted ? "AUTO-LEARN stopped" : "AUTO-LEARN done") + ": " + String (autoFound) + " learned, " + String (knownOffsets()) + " of " + String ((int) P6::params().size()) + " known"
                + (unknownList().isNotEmpty() ? " - still: " + unknownList() : String()) + "   (details in probe.txt)";
    if (! hadRef) learnStatus = "AUTO-LEARN stopped before the synth's edit buffer arrived: nothing was sent, nothing changed";
    ++uiVersion;
}
// ---------------------------------------------------------------- the Vintage global
void P6PanelProcessor::setVintage (bool on)
{
    if (vintageNrpn < 0) return;
    link.sendNrpn (vintageNrpn, on ? 1 : 0); vintageState = on ? 1 : 0; saveGlobals(); ++uiVersion;
}
void P6PanelProcessor::loadGlobals()
{
    StringArray lines; libraryDir().getChildFile ("globals.txt").readLines (lines);
    for (auto& l : lines)
    {
        if (l.startsWith ("vintageNrpn=")) vintageNrpn = l.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        if (l.startsWith ("vintage=")) vintageState = l.fromFirstOccurrenceOf ("=", false, false).getIntValue();
    }
    if (vintageNrpn < 1024) vintageNrpn = -1;
}
void P6PanelProcessor::saveGlobals()
{
    libraryDir().getChildFile ("globals.txt").replaceWithText ("# the synth's global settings the panel knows (written by the Prophet 6 Panel)\nvintageNrpn=" + String (vintageNrpn) + "\nvintage=" + String (vintageState) + "\n");
}
File P6PanelProcessor::ratingsFile() { return catalogueFile().getSiblingFile ("ratings.txt"); }
void P6PanelProcessor::loadRatings()
{
    ratings.clear(); StringArray lines; ratingsFile().readLines (lines);
    for (auto& l : lines) { const int bar = l.lastIndexOfChar ('|'); if (bar > 0 && ! l.startsWith ("#")) { const int st = l.substring (bar + 1).getIntValue(); if (st > 0) ratings[l.substring (0, bar)] = jlimit (1, 5, st); } }
}
void P6PanelProcessor::saveRatings() const
{
    String out ("# key|stars   key = slot number (0xx-4xx user, 5xx-9xx factory) or lib:<Category>/<name>\n");
    for (auto& [key, st] : ratings) out << key << "|" << st << "\n";
    ratingsFile().replaceWithText (out);
}
File P6PanelProcessor::notesFile() { return catalogueFile().getSiblingFile ("notes.txt"); }
void P6PanelProcessor::loadNotes()
{   // one line per sound: key|text. The text never holds a '|' (setNote sees to that), so the last one divides
    notes.clear(); StringArray lines; notesFile().readLines (lines);
    for (auto& l : lines) { const int bar = l.lastIndexOfChar ('|'); if (bar > 0 && ! l.startsWith ("#")) { const auto t = l.substring (bar + 1).trim(); if (t.isNotEmpty()) notes[l.substring (0, bar)] = t; } }
}
void P6PanelProcessor::saveNotes() const
{
    if (notes.empty()) { notesFile().deleteFile(); return; }   // no notes: no file left lying about
    String out ("# Prophet 6 panel - your own notes on the sounds: slot or library key|text\n");
    for (auto& [key, t] : notes) out << key << "|" << t << "\n";
    notesFile().replaceWithText (out);
}
void P6PanelProcessor::setNote (const String& key, const String& text)
{
    const auto t = text.replaceCharacters ("|\r\n", "/  ").trim().substring (0, 200);
    if (t == note (key)) return;
    if (t.isEmpty()) notes.erase (key); else notes[key] = t;
    saveNotes(); ++uiVersion;
}
File P6PanelProcessor::synthNamesFile() { return catalogueFile().getSiblingFile ("synth_names.txt"); }
void P6PanelProcessor::loadSynthNames()
{
    synthNames.clear(); StringArray lines; synthNamesFile().readLines (lines);
    for (auto& l : lines) { if (l.startsWith ("#")) continue; const int bar = l.indexOfChar ('|'); if (bar > 0) synthNames[l.substring (0, bar).getIntValue()] = l.substring (bar + 1).trimEnd(); }
}
void P6PanelProcessor::saveSynthNames() const
{
    String out ("# slot|name as stored in the synth (read by the Prophet 6 Panel's name scan)\n");
    for (auto& [slot, n] : synthNames) out << slot << "|" << n << "\n";
    synthNamesFile().replaceWithText (out);
}
void P6PanelProcessor::startNameScan() { if (! link.isOnline()) return; scanning = true; scanSlot = 0; scanSentAt = 0; ++uiVersion; }
void P6PanelProcessor::stopNameScan() { scanning = false; saveSynthNames(); ++uiVersion; }
File P6PanelProcessor::catalogueFile() { auto dir = claudeDataDir ("Prophet 6 Patches"); dir.createDirectory(); return dir.getChildFile ("catalogue.txt"); }
void P6PanelProcessor::loadCatalogue()
{
    catalogue.clear();
    StringArray lines; catalogueFile().readLines (lines);
    for (auto& l : lines) { if (l.startsWith ("#")) continue; auto parts = StringArray::fromTokens (l, "|", ""); if (parts.size() >= 3) catalogue[parts[0].getIntValue()] = { parts[2].trim(), parts[1].trim() }; }
}
void P6PanelProcessor::saveCatalogue() const
{
    String out ("# slot|category|name  (user banks 0-4; written by the Prophet 6 Panel)\n");
    for (auto& [slot, e] : catalogue) out << slot << "|" << e.category << "|" << e.name << "\n";
    catalogueFile().replaceWithText (out);
}
File P6PanelProcessor::libraryDir() { return catalogueFile().getParentDirectory(); }
std::vector<P6PanelProcessor::LibEntry> P6PanelProcessor::library()
{
    std::vector<LibEntry> out;
    for (auto& dirEntry : RangedDirectoryIterator (libraryDir(), false, "*", File::findDirectories))
    {
        const auto dir = dirEntry.getFile(); const String cat = dir.getFileName();
        for (auto& f : RangedDirectoryIterator (dir, false, "*.p6patch", File::findFiles))
            out.push_back ({ f.getFile(), cat, f.getFile().getFileNameWithoutExtension(), "lib:" + cat + "/" + f.getFile().getFileNameWithoutExtension() });
    }
    std::sort (out.begin(), out.end(), [] (const LibEntry& a, const LibEntry& b) { return a.category + a.name < b.category + b.name; });
    return out;
}
void P6PanelProcessor::saveAs (const String& name, const String& category, int bank, int program, bool writeSlot)
{
    setPatchName (name);
    if (writeSlot)
    {
        bank = jlimit (0, 4, bank); program = jlimit (0, 99, program);
        writeTo (bank, program);
        catalogue[bank * 100 + program] = { name.trim(), category.trim() }; saveCatalogue();
    }
    auto dir = libraryDir().getChildFile (File::createLegalFileName (category.trim().isEmpty() ? "Uncategorised" : category.trim())); dir.createDirectory();
    savePatchFile (dir.getChildFile (File::createLegalFileName (name.trim()) + ".p6patch"));
}
File P6PanelProcessor::layoutFile() { auto dir = claudeDataDir ("Prophet 6 Patches"); dir.createDirectory(); return dir.getChildFile ("layout.txt"); }
String P6PanelProcessor::unknownList() const
{
    String s; int n = 0;
    for (int i = 0; i < (int) P6::params().size(); ++i) if (dumpOffset[(size_t) i] < 0 && ! notStored (i)) { if (n++ < 6) s << (n > 1 ? ", " : "") << P6::params()[(size_t) i].name; }
    if (n > 6) s << " +" << (n - 6) << " more";
    return s;
}
void P6PanelProcessor::loadLayout()
{
    StringArray lines; layoutFile().readLines (lines);
    std::vector<std::pair<int, int>> entries;   // param index, offset
    for (auto& l : lines) { auto t = StringArray::fromTokens (l, ",", ""); if (t.size() >= 2) { const int i = P6::indexOfId (t[0].trim()); if (i >= 0) entries.push_back ({ i, t[1].trim().getIntValue() }); } }
    for (auto& e : entries)
    {   // two learned entries claiming the same byte cancel each other out (an earlier learner weakness with buttons)
        bool clash = false; for (auto& f : entries) if (f.first != e.first && f.second == e.second) clash = true;
        if (! clash && e.second >= 0 && e.second < P6::kProgramBytes) dumpOffset[(size_t) e.first] = e.second;   // inside the program image only (a damaged layout.txt must not write past it)
    }
}
void P6PanelProcessor::saveLayout()
{
    String s;
    for (int i = 0; i < (int) P6::params().size(); ++i) if (dumpOffset[(size_t) i] >= 0) s << P6::params()[(size_t) i].id << "," << dumpOffset[(size_t) i] << "," << P6::params()[(size_t) i].nrpn << "," << P6::params()[(size_t) i].name << "\n";
    layoutFile().replaceWithText (s);
}

// ---------------------------------------------------------------- program image
String P6PanelProcessor::patchName() const
{
    String s;
    for (int i = 0; i < P6::kNameLength; ++i) { const char c = (char) program[P6::kNameOffset + i]; s += (c >= 32 && c < 127) ? String::charToString ((juce_wchar) c) : " "; }
    return s.trimEnd();
}
void P6PanelProcessor::setPatchName (const String& n)
{
    auto a = n.toStdString();
    for (int i = 0; i < P6::kNameLength; ++i) program[P6::kNameOffset + i] = (uint8_t) (i < (int) a.size() ? jlimit (32, 125, (int) a[(size_t) i]) : 32);
}
void P6PanelProcessor::applyProgram (const MemoryBlock& b, bool sendToSynth)
{
    if (b.getSize() < (size_t) P6::kProgramBytes) return;
    memcpy (program, b.getData(), P6::kProgramBytes); memset (seqShadow, 0, sizeof (seqShadow));
    restoring = true;
    for (int i = 0; i < (int) P6::params().size(); ++i)
    {
        auto& q = P6::params()[(size_t) i];
        if (dumpOffset[(size_t) i] < 0) continue;                    // layout not learned for this one yet
        const int v = jlimit (q.min, q.max, (int) program[dumpOffset[(size_t) i]]);
        lastSent[(size_t) i] = v;
        auto* p = apvts.getParameter (q.id);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    }
    restoring = false;
    for (auto& d : dirty) d = false;
    if (originPending && ! memSwitching) { mem[memActive].origin = originNow(); originPending = false; }   // a newly chosen sound has arrived: the letter in use is that sound now
    ++uiVersion;
    if (sendToSynth) sendAll();
}
void P6PanelProcessor::seqChanged() { seqDumpAt = Time::currentTimeMillis() + 400; ++uiVersion; }
void P6PanelProcessor::setSeqNote (int t, int s, int note)
{
    note = jlimit (0, 127, note); seqNoteByte (t, s) = (uint8_t) ((seqNoteByte (t, s) & 0x80) | note);
        link.sendNrpn (256 + t * 128 + s, note); seqChanged();
}
void P6PanelProcessor::setSeqVel (int t, int s, int vel)
{
    vel = jlimit (1, 127, vel); seqVelByte (t, s) = (uint8_t) (0x80 | vel);
    link.sendNrpn (256 + t * 128 + 64 + s, vel); seqChanged();
}
void P6PanelProcessor::addSeqNote (int t, int s, int note, int vel)
{
    seqNoteByte (t, s) = (uint8_t) jlimit (0, 127, note); seqVelByte (t, s) = (uint8_t) (0x80 | jlimit (1, 127, vel));
    for (int k = 0; k <= s; ++k) for (int u = 0; u < kSeqTracks; ++u) if (! seqUsed (u, k)) seqVelByte (u, k) |= 0x80;   // steps before it belong to the sequence too
    link.sendNrpn (256 + t * 128 + s, note); link.sendNrpn (256 + t * 128 + 64 + s, vel); seqChanged();
}
void P6PanelProcessor::removeSeqNote (int t, int s)
{
    seqNoteByte (t, s) = 60; seqVelByte (t, s) = 0x80;
    for (int k = s + 1; k < kSeqSteps && seqTie (t, k); ++k) { seqNoteByte (t, k) = 60; seqVelByte (t, k) = 0x80; }
    link.sendNrpn (256 + t * 128 + 64 + s, 0); seqChanged();
}
void P6PanelProcessor::setSeqTieLength (int t, int s, int steps)
{
    const int last = jmin (kSeqSteps - 1, s + jmax (1, steps) - 1);
    for (int k = s + 1; k <= last; ++k) { seqNoteByte (t, k) = 0x80 | 60; seqVelByte (t, k) = 0x80; }   // the synth stores a tie as 0x80 with the default note
    for (int k = last + 1; k < kSeqSteps && seqTie (t, k); ++k) { seqNoteByte (t, k) = 60; seqVelByte (t, k) = (uint8_t) (seqUsed (t, k) ? 0x80 : 0); }
    for (int k = 0; k <= last; ++k) for (int u = 0; u < kSeqTracks; ++u) if (! seqUsed (u, k)) seqVelByte (u, k) |= 0x80;
    seqChanged();
}
void P6PanelProcessor::setSeqLength (int n)
{
    n = jlimit (1, kSeqSteps, n);
    for (int s = 0; s < kSeqSteps; ++s) for (int t = 0; t < kSeqTracks; ++t)
    {
        if (s < n)
        {
            if (! seqUsed (t, s))
            {   // back inside the sequence: restore what shortening hid, else an empty (rest) step
                if (seqShadow[t][1][s] & 0x80) { seqNoteByte (t, s) = seqShadow[t][0][s]; seqVelByte (t, s) = seqShadow[t][1][s]; }
                else seqVelByte (t, s) |= 0x80;
                seqShadow[t][1][s] = 0;
            }
        }
        else if (seqUsed (t, s))
        {
            seqShadow[t][0][s] = seqNoteByte (t, s); seqShadow[t][1][s] = seqVelByte (t, s);
            seqVelByte (t, s) = 0; seqNoteByte (t, s) = 60;
        }
    }
    seqChanged();
}
void P6PanelProcessor::clearSequence()
{
    for (int t = 0; t < kSeqTracks; ++t) for (int s = 0; s < kSeqSteps; ++s) { seqNoteByte (t, s) = 60; seqVelByte (t, s) = 0; seqShadow[t][1][s] = 0; }
    seqChanged();
}
// ---------------------------------------------------------------- sound design helpers
void P6PanelProcessor::setParam (int i, int v)
{
    auto& q = P6::params()[(size_t) i]; auto* p = apvts.getParameter (q.id);
    p->setValueNotifyingHost (p->convertTo0to1 ((float) jlimit (q.min, q.max, v)));
}
static bool isPerformanceParam (const String& id)
{   // left alone by randomize / mutate: they belong to the way the synth is played, not to the timbre
    static const StringArray keep { "voiceVol", "panSpread", "pbRange", "keyMode", "unisonOn", "unisonMode", "glideOn", "glideRate", "glideMode",
                                    "arpOn", "arpMode", "arpRange", "arpTime", "bpm", "seqOn", "seqLength", "seqMode", "seqPlay", "slop" };
    return keep.contains (id);
}
void P6PanelProcessor::randomize()
{
    Random& r = Random::getSystemRandom();
    auto pick = [&] (const char* id, int lo, int hi) { setParam (P6::indexOfId (id), lo + r.nextInt (hi - lo + 1)); };
    auto coin = [&] (const char* id, float pOn) { setParam (P6::indexOfId (id), r.nextFloat() < pOn ? 1 : 0); };
    // oscillators: mostly in-tune, a fifth or an octave apart now and then
    const int base = 12 + 12 * r.nextInt (3);                        // 12, 24 or 36 semitones (the knob's 0-60 range)
    pick ("osc1Freq", base, base); const int iv[] = { 0, 0, 0, 7, 12, -12, 5 }; setParam (P6::indexOfId ("osc2Freq"), jlimit (0, 60, base + iv[r.nextInt (7)]));
    pick ("osc2Fine", 117, 137); pick ("osc1Shape", 0, 254); pick ("osc2Shape", 0, 254); pick ("osc1PW", 60, 200); pick ("osc2PW", 60, 200);
    coin ("osc1Sync", 0.15f); coin ("osc2Low", 0.08f); coin ("osc2Key", 0.95f);
    pick ("osc1Level", 80, 127); pick ("osc2Level", 0, 127); pick ("subLevel", 0, 100); pick ("noise", 0, r.nextFloat() < 0.7f ? 15 : 90);
    // filters and envelopes
    pick ("lpFreq", 30, 150); pick ("lpRes", 0, 200); pick ("lpEnvAmt", 100, 230); pick ("lpKey", 0, 2); coin ("lpVel", 0.5f);
    pick ("hpFreq", 0, r.nextFloat() < 0.7f ? 20 : 90); pick ("hpRes", 0, 120); pick ("hpEnvAmt", 100, 160); pick ("hpKey", 0, 1); coin ("hpVel", 0.3f);
    pick ("fAtk", 0, 60); pick ("fDec", 20, 110); pick ("fSus", 0, 110); pick ("fRel", 10, 100);
    pick ("vcaEnvAmt", 90, 127); pick ("aAtk", 0, 50); pick ("aDec", 20, 110); pick ("aSus", 20, 127); pick ("aRel", 10, 100); coin ("vcaVel", 0.6f);
    // modulation: usually one or two destinations
    pick ("lfoFreq", 20, 200); pick ("lfoAmt", 0, 90); pick ("lfoShape", 0, 4); coin ("lfoSync", 0.3f);
    for (auto* d : { "lfoFreq1", "lfoFreq2", "lfoPW", "lfoAmp", "lfoLP", "lfoHP" }) coin (d, 0.2f);
    pick ("pmFiltEnv", 100, 160); pick ("pmOsc2", 110, 150); for (auto* d : { "pmFreq1", "pmShape1", "pmPW1", "pmLP", "pmHP" }) coin (d, 0.12f);
    pick ("atAmt", 100, 200); for (auto* d : { "atFreq1", "atFreq2", "atLP", "atHP", "atVCA", "atLFO" }) coin (d, 0.15f);
    // effects: tasteful amounts
    coin ("fxOn", 0.7f); pick ("fx1Type", 0, 9); pick ("fx2Type", 0, 13); pick ("fx1Mix", 20, 70); pick ("fx2Mix", 20, 70);
    pick ("fx1P1", 40, 200); pick ("fx2P1", 40, 200); pick ("fx1P2", 20, 100); pick ("fx2P2", 20, 100); coin ("fx1Sync", 0.4f); coin ("fx2Sync", 0.2f);
    pick ("fx1Time", 3, 10); pick ("fx2Time", 3, 10); pick ("distortion", 0, r.nextFloat() < 0.75f ? 0 : 60);
    setPatchName ("Random " + String (r.nextInt (900) + 100));
    ++uiVersion;
}
void P6PanelProcessor::mutate (float amount)
{
    Random& r = Random::getSystemRandom();
    for (int i = 0; i < (int) P6::params().size(); ++i)
    {
        auto& q = P6::params()[(size_t) i]; if (isPerformanceParam (q.id) || r.nextFloat() > 0.5f) continue;
        const int v = valueOf (i), span = q.max - q.min;
        if (q.kind == P6::Kind::Knob) setParam (i, v + roundToInt ((r.nextFloat() * 2.f - 1.f) * amount * (float) span * 0.25f));
        else if (r.nextFloat() < amount * 0.3f) setParam (i, q.min + r.nextInt (span + 1));   // switches and choices flip now and then
    }
    ++uiVersion;
}
String P6PanelProcessor::originNow() const
{
    if (libKey.isNotEmpty()) return "Library  " + libKey.fromFirstOccurrenceOf ("lib:", false, false).replace ("/", " / ");
    return String (currentBank * 100 + currentProgram).paddedLeft ('0', 3) + "  " + patchName();
}
String P6PanelProcessor::memoryText (int m) const
{
    m = jlimit (0, kMemories - 1, m); const String letter = String::charToString ((juce_wchar) ('A' + m));
    if (m == memActive) return letter + " (in use):  " + (mem[m].origin.isNotEmpty() ? mem[m].origin : originNow());
    if (! mem[m].used) return letter + ":  empty - a click copies the sound in use here";
    return letter + ":  " + mem[m].origin + "   (Alt-click empties it)";
}
void P6PanelProcessor::switchMemory (int m)
{
    m = jlimit (0, kMemories - 1, m); if (m == memActive) return;
    auto& cur = mem[memActive]; cur.used = true; cur.data = patchBytes(); cur.bank = currentBank; cur.prog = currentProgram; cur.libKey = libKey;
    if (cur.origin.isEmpty()) cur.origin = originNow();
    auto& t = mem[m];
    if (t.used)
    {   // the panel is on that sound again, as far as the panel goes: its stars, its note and SAVE belong to it. The synth is only sent
        // the sound (edit buffer) - no program change, which would bring up the stored program first
        memSwitching = true; currentBank = t.bank; currentProgram = t.prog; libKey = t.libKey;
        applyPatchBytes (t.data.getData(), (int) t.data.getSize());
        memSwitching = false;
    }
    else t = cur;
    memActive = m; originPending = false; ++uiVersion;
}
// ---------------------------------------------------------------- sequencer helpers
void P6PanelProcessor::seqTranspose (int semitones)
{
    for (int t = 0; t < kSeqTracks; ++t) for (int s = 0; s < kSeqSteps; ++s)
        if (seqUsed (t, s) && ! seqTie (t, s) && seqVel (t, s) > 0) seqNoteByte (t, s) = (uint8_t) jlimit (12, 108, seqNote (t, s) + semitones);
    seqChanged();
}
void P6PanelProcessor::seqHumanize (int amount)
{
    Random& r = Random::getSystemRandom();
    for (int t = 0; t < kSeqTracks; ++t) for (int s = 0; s < kSeqSteps; ++s)
        if (seqOn (t, s)) seqVelByte (t, s) = (uint8_t) (0x80 | jlimit (1, 127, seqVel (t, s) + r.nextInt (2 * amount + 1) - amount));
    seqChanged();
}
void P6PanelProcessor::seqReverse()
{
    const int len = seqLength(); if (len < 2) return;
    for (int t = 0; t < kSeqTracks; ++t) for (int s = 0; s < len / 2; ++s)
    {   const int o = len - 1 - s; std::swap (seqNoteByte (t, s), seqNoteByte (t, o)); std::swap (seqVelByte (t, s), seqVelByte (t, o)); }
    // a reversed tie points the wrong way: turn the runs back into note + continuations
    for (int t = 0; t < kSeqTracks; ++t) for (int s = len - 1; s >= 0; --s)
        if (seqTie (t, s) && (s == 0 || ! seqTie (t, s - 1)))
        {   int e = s; while (e + 1 < len && seqTie (t, e + 1)) ++e;       // run s..e of ties; the note that started it now sits at e+1
            if (e + 1 < len) { seqNoteByte (t, s) = seqNoteByte (t, e + 1); seqVelByte (t, s) = seqVelByte (t, e + 1); for (int k = s + 1; k <= e + 1; ++k) { seqNoteByte (t, k) = 0x80 | 60; seqVelByte (t, k) = 0x80; } }
            s = s; }
    seqChanged();
}
void P6PanelProcessor::seqDouble()
{
    const int len = seqLength(); if (len < 1 || len * 2 > kSeqSteps) return;
    for (int t = 0; t < kSeqTracks; ++t) for (int s = 0; s < len; ++s) { seqNoteByte (t, len + s) = seqNoteByte (t, s); seqVelByte (t, len + s) = seqVelByte (t, s); }
    seqChanged();
}
void P6PanelProcessor::sendAll()
{
    for (int i = 0; i < (int) P6::params().size(); ++i) { if (dumpOffset[(size_t) i] >= 0) program[dumpOffset[(size_t) i]] = (uint8_t) valueOf (i); lastSent[(size_t) i] = valueOf (i); }
    link.sendEditBuffer (programBlock());
}
void P6PanelProcessor::writeTo (int bank, int prog)
{
    for (int i = 0; i < (int) P6::params().size(); ++i) if (dumpOffset[(size_t) i] >= 0) program[dumpOffset[(size_t) i]] = (uint8_t) valueOf (i);
    link.sendProgram (jlimit (0, 4, bank), jlimit (0, 99, prog), programBlock());
    currentBank = bank; currentProgram = prog; saveModFor (bank * 100 + prog);
}
void P6PanelProcessor::selectProgram (int bank, int prog)
{
    currentBank = jlimit (0, 9, bank); currentProgram = jlimit (0, 99, prog); libKey = {}; originPending = true;
    expectFreshProgram();
    link.sendBankProgram (currentBank, currentProgram);
    link.requestEditBuffer(); loadModFor (currentBank * 100 + currentProgram);
}
void P6PanelProcessor::expectFreshProgram() { std::fill (modSent.begin(), modSent.end(), -1); freshProgramUntil = Time::currentTimeMillis() + 3000; }
P6PanelProcessor::LibSave P6PanelProcessor::librarySave() const
{
    LibSave r; const String path = libKey.fromFirstOccurrenceOf ("lib:", false, false);
    r.category = path.upToFirstOccurrenceOf ("/", false, false); r.oldName = path.fromFirstOccurrenceOf ("/", false, false);
    r.name = patchName().trim(); if (r.name.isEmpty()) r.name = r.oldName;
    r.same = File::createLegalFileName (r.name) == r.oldName;
    r.file = libraryDir().getChildFile (r.category).getChildFile (File::createLegalFileName (r.name) + ".p6patch");
    r.replacesOther = ! r.same && r.file.existsAsFile();   // a new name that another sound of the category already has
    return r;
}
void P6PanelProcessor::setPorts (const String& inId, const String& outId)
{
    if (inId != link.wantedIn()) link.openInput (inId);     // the wanted port, so a device that is away for the moment stays chosen
    if (outId != link.wantedOut()) link.openOutput (outId);
}

// ---------------------------------------------------------------- files
MemoryBlock P6PanelProcessor::patchBytes()
{
    for (int i = 0; i < (int) P6::params().size(); ++i) if (dumpOffset[(size_t) i] >= 0) program[dumpOffset[(size_t) i]] = (uint8_t) valueOf (i);
    MemoryBlock m; m.append ("\xf0\x01\x2d\x03", 4);
    auto packed = MidiLink::pack (programBlock()); m.append (packed.getData(), packed.getSize()); m.append ("\xf7", 1);
    if (mod.any() || vintageState >= 0) { auto xml = soundExtras().toXmlString(); m.append (xml.toRawUTF8(), xml.getNumBytesAsUTF8()); }   // MOD settings and Vintage ride behind the SysEx
    return m;
}
void P6PanelProcessor::savePatchFile (const File& f)
{
    const auto m = patchBytes();
    f.replaceWithData (m.getData(), m.getSize());
    if (libKey.isEmpty()) saveModFor (currentBank * 100 + currentProgram);   // a synth sound: the MOD settings stay with its slot too (a library sound has its own file)
}
bool P6PanelProcessor::loadPatchFile (const File& f)
{
    MemoryBlock m; if (! f.loadFileAsData (m)) return false;
    originPending = true;   // a sound chosen from a file: the letter in use is that sound now
    return applyPatchBytes (m.getData(), (int) m.getSize());
}
bool P6PanelProcessor::applyPatchBytes (const void* data, int size)
{
    const auto* d = (const uint8_t*) data; const int n = size;
    // accept an edit-buffer dump or a program dump, with or without F0/F7, and take the first one in a bank file
    for (int i = 0; i + 4 < n; ++i)
    {
        if (d[i] == 0x01 && d[i + 1] == 0x2d && (d[i + 2] == 0x03 || d[i + 2] == 0x02))
        {
            const int start = i + (d[i + 2] == 0x03 ? 3 : 5);
            if (start + P6::kPackedBytes > n) return false;
            applyProgram (MidiLink::unpack (d + start, P6::kPackedBytes, P6::kProgramBytes), true);
            { Mod::State fresh; fresh.autoXY = mod.autoXY; fresh.autoX = mod.autoX; fresh.autoY = mod.autoY; mod = fresh; } const int end = start + P6::kPackedBytes + 1;
            if (end < n) if (auto xml = XmlDocument::parse (String::fromUTF8 ((const char*) d + end, n - end))) applySoundExtras (ValueTree::fromXml (*xml));
            modSettingsChanged(); ++uiVersion;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- extra modulation (the MOD page)
void P6PanelProcessor::xyTouch (float x, float y, bool down)
{   // the on-screen pad; automation (modXYOn / modX / modY) takes over while the pad is not touched
    const bool was = padDown; padDown = down; padX = jlimit (-1.f, 1.f, x); padY = jlimit (-1.f, 1.f, y);
    if (was && ! down) { lastModTick = 0; modTick(); }   // release: put the destinations back at once
}
void P6PanelProcessor::modSettingsChanged() { syncingMod = true; mod.toParams (apvts); syncingMod = false; lastModTick = 0; modTick(); }
void P6PanelProcessor::loadModFor (int slot)
{
    Mod::State fresh; fresh.autoXY = mod.autoXY; fresh.autoX = mod.autoX; fresh.autoY = mod.autoY; mod = fresh;   // the sound's settings change, automation carries on
    auto f = modFile (slot);
    if (f.existsAsFile()) if (auto xml = XmlDocument::parse (f)) applySoundExtras (ValueTree::fromXml (*xml));
    modSettingsChanged(); ++uiVersion;
}
void P6PanelProcessor::saveModFor (int slot)
{
    auto f = modFile (slot);
    if (mod.any() || vintageState >= 0) f.replaceWithText (soundExtras().toXmlString()); else f.deleteFile();
}
ValueTree P6PanelProcessor::soundExtras() const
{
    auto t = mod.toTree();
    if (vintageState >= 0) t.setProperty ("vintage", vintageState, nullptr);
    return t;
}
void P6PanelProcessor::applySoundExtras (const ValueTree& t)
{
    mod.fromTree (t);
    if (t.hasProperty ("vintage") && vintageNrpn >= 0)
    {   // only when it differs: the synth's global switch follows the sound
        const int v = (int) t.getProperty ("vintage") > 0 ? 1 : 0;
        if (v != vintageState) setVintage (v == 1);
    }
}
void P6PanelProcessor::modTick()
{
    const auto now = Time::currentTimeMillis();
    if (lastModTick == 0) lastModTick = now;
    if (now - lastModTick < 25) return;                                      // ~30 updates a second is what a DIN cable carries comfortably
    const double dt = jlimit (0.0, 0.2, (now - lastModTick) / 1000.0); lastModTick = now;
    // the clock: the host's when it plays, otherwise our own at the page's BPM
    const bool hostOn = hostPlaying.load() && hostBpm.load() > 0;
    const double bpm = hostOn ? hostBpm.load() : (double) mod.bpm;
    if (hostOn) beatClock = hostPpq.load(); else beatClock += bpm / 60.0 * dt;
    // note-ons from the host (or the on-screen keyboard) restart the envelope
    { const int n = hostNoteOns.load(), f = hostNoteOffs.load(); if (n != seenNoteOns) { envHeld += n - seenNoteOns; seenNoteOns = n; envTrig = true; } if (f != seenNoteOffs) { envHeld = jmax (0, envHeld - (f - seenNoteOffs)); seenNoteOffs = f; } }
    const bool noteOn = envTrig;
    if (envTrig) { envTrig = false; envStage = 1; envPos = 0.f; envFrom = envLevel; }                                   // a new note: rise from wherever the envelope is
    else if (envHeld <= 0 && (envStage == 1 || envStage == 2)) { envStage = 3; envPos = 0.f; envFrom = envLevel; }
    std::vector<float> offset (P6::params().size(), 0.f); std::vector<bool> active (P6::params().size(), false);
    auto addTo = [&] (int dest, float v) { if (dest >= 0 && dest < (int) offset.size() && P6::params()[(size_t) dest].kind == P6::Kind::Knob) { offset[(size_t) dest] += v; active[(size_t) dest] = true; } };
    // LFOs: output -1..1 (S&H holds a random value per cycle)
    for (int i = 0; i < 2; ++i)
    {
        auto& l = mod.lfo[i]; if (! l.on) continue;
        const double cycles = l.sync ? beatClock / Mod::divisionBeats (l.div) : 0.0;
        float ph = l.sync ? (float) (cycles - std::floor (cycles)) : lfoPhase[i] + (float) (l.hz * dt);
        if (! l.sync) ph -= std::floor (ph);
        if (ph < lfoPhase[i]) lfoHold[i] = Random::getSystemRandom().nextFloat() * 2.f - 1.f;   // a new cycle
        lfoPhase[i] = ph;
        float v = 0.f;
        switch (l.shape) { case 0: v = std::sin (ph * MathConstants<float>::twoPi); break; case 1: v = ph < 0.5f ? ph * 4.f - 1.f : 3.f - ph * 4.f; break;
                           case 2: v = ph * 2.f - 1.f; break; case 3: v = 1.f - ph * 2.f; break; case 4: v = ph < 0.5f ? 1.f : -1.f; break; default: v = lfoHold[i]; }
        for (int d = 0; d < 2; ++d) addTo (l.dest[d], v * l.depth * l.amt[d]);
    }
    // step sequencer: unipolar steps 0..1, applied as amount x step
    if (mod.seq.on)
    {
        const double stepsF = beatClock / Mod::divisionBeats (mod.seq.div); const int pos = ((int) std::floor (stepsF)) % jmax (1, mod.seq.length);
        if (pos != seqPos) { seqPos = pos; ++uiVersion; }
        addTo (mod.seq.dest, mod.seq.step[(size_t) jlimit (0, 15, pos)] * mod.seq.amt);
    }
    else seqPos = -1;
    // ADSR envelope
    if (mod.env.on)
    {
        auto& e = mod.env; const float ms = (float) (dt * 1000.0);   // envPos runs 0..1 through the segment, the curve bends it, envFrom is where the segment started
        if (envStage == 1)      { envPos = jmin (1.f, envPos + ms / jmax (1.f, e.attackMs)); envLevel = envFrom + (1.f - envFrom) * Mod::envCurve (envPos, e.curve[0]); if (envPos >= 1.f) { envStage = 2; envPos = 0.f; envFrom = 1.f; } }
        else if (envStage == 2) { envPos = jmin (1.f, envPos + ms / jmax (1.f, e.decayMs)); envLevel = e.sustain + (envFrom - e.sustain) * (1.f - Mod::envCurve (envPos, e.curve[1])); }
        else if (envStage == 3) { envPos = jmin (1.f, envPos + ms / jmax (1.f, e.releaseMs)); envLevel = envFrom * (1.f - Mod::envCurve (envPos, e.curve[2])); if (envPos >= 1.f) { envLevel = 0.f; envStage = 0; } }
        for (int d = 0; d < 3; ++d) addTo (e.dest[d], envLevel * e.amt[d]);
    }
    else { envLevel = 0.f; envStage = 0; }
    // drawn shapes: one pass per note, a loop, or a loop that every note restarts
    for (int i = 0; i < 2; ++i)
    {
        auto& s = mod.shape[i]; if (! s.on) { shapeRun[i] = false; shapePos[i] = 0.f; continue; }
        const float secs = jmax (0.05f, s.sync ? (float) (Mod::divisionBeats (s.div) * 60.0 / jmax (20.0, bpm)) : s.seconds);   // synced: the note value at the tempo, also for a one shot
        if (noteOn && s.mode != 1) { shapePos[i] = 0.f; shapeRun[i] = true; shapeStartBeat[i] = beatClock; }
        if (s.mode == 0) { if (shapeRun[i]) { shapePos[i] += (float) dt / secs; if (shapePos[i] >= 1.f) { shapePos[i] = 1.f; shapeRun[i] = false; } } }
        else if (s.sync) { const double c = (beatClock - (s.mode == 2 ? shapeStartBeat[i] : 0.0)) / Mod::divisionBeats (s.div); shapePos[i] = (float) (c - std::floor (c)); shapeRun[i] = true; }
        else { shapePos[i] += (float) dt / secs; shapePos[i] -= std::floor (shapePos[i]); shapeRun[i] = true; }
        float v = s.evaluate (s.mode == 0 && ! shapeRun[i] ? 1.f : shapePos[i]);   // a finished one-shot holds its end value
        if (s.bipolar) v = v * 2.f - 1.f;
        for (int d = 0; d < 3; ++d) addTo (s.dest[d], v * s.amt[d]);
    }
    // X/Y pad: the on-screen pad, or automation while the pad is not touched; both axes bipolar
    const bool wantDown = padDown || mod.autoXY;
    if (padDown) { xyX = padX; xyY = padY; } else { xyX = mod.autoX; xyY = mod.autoY; }
    if (wantDown != xyDown)
    {   // the gate: a switch that follows the finger
        xyDown = wantDown; const int gateParam = mod.xy.gateDest; const bool inv = mod.xy.gateInv;
        if (xyDown && gateParam >= 0 && gateParam < (int) P6::params().size()) { auto& q = P6::params()[(size_t) gateParam]; link.sendNrpn (q.nrpn, inv ? q.min : q.max); xyGateOn = true; xyGateParam = gateParam; }
        else if (! xyDown && xyGateOn && xyGateParam >= 0) { auto& q = P6::params()[(size_t) xyGateParam]; link.sendNrpn (q.nrpn, valueOf (xyGateParam)); xyGateOn = false; }
    }
    // X and Y while the pad is touched; A (the fader) always - it is a third axis you set and leave
    { const float axis[3] = { xyX, xyY, mod.aLevel }; for (int a = 0; a < 3; ++a) { if (a < 2 ? ! xyDown : mod.aLevel <= 0.f) continue; addTo (mod.xy.dest[a], (mod.xy.inv[a] ? -1.f : 1.f) * axis[a] * mod.xy.amt[a]); } }
    if (freshProgramUntil != 0 && now < freshProgramUntil) return;   // a program change is under way: the panel still holds the old program's values, and none of them may land in the new one
    // send what changed, restore what stopped (offsets are fractions of the parameter's range)
    // An NRPN is 12 bytes and a DIN cable carries ~3 kB/s: when many knobs move at once, the ones that moved most
    // go first and the rest follow on the next ticks, so the stream never clogs
    struct Pending { int i, v, dist; }; std::vector<Pending> pending;
    for (int i = 0; i < (int) offset.size(); ++i)
    {
        auto& q = P6::params()[(size_t) i]; if (q.kind != P6::Kind::Knob) continue;
        if (active[(size_t) i])
        {
            const int v = jlimit (q.min, q.max, roundToInt (valueOf (i) + offset[(size_t) i] * (float) (q.max - q.min)));
            if (v != modSent[(size_t) i]) pending.push_back ({ i, v, modSent[(size_t) i] < 0 ? 1000 : std::abs (v - modSent[(size_t) i]) * 255 / jmax (1, q.max - q.min) });
        }
        else if (modSent[(size_t) i] >= 0) { modSent[(size_t) i] = -1; link.sendNrpn (q.nrpn, valueOf (i)); lastSent[(size_t) i] = valueOf (i); }
    }
    std::sort (pending.begin(), pending.end(), [] (const Pending& a, const Pending& b) { return a.dist > b.dist; });
    const int budget = 6;
    for (int k = 0; k < (int) pending.size() && k < budget; ++k) { auto& pd = pending[(size_t) k]; modSent[(size_t) pd.i] = pd.v; link.sendNrpn (P6::params()[(size_t) pd.i].nrpn, pd.v); }
}
void Mod::State::toParams (AudioProcessorValueTreeState& s) const
{
    for (int i = 0; i < 2; ++i) { const String n = "modLfo" + String (i + 1); Mod::autoSet (s, (n + "On").toRawUTF8(), lfo[i].on ? 1.f : 0.f); Mod::autoSet (s, (n + "Rate").toRawUTF8(), lfo[i].hz); Mod::autoSet (s, (n + "Depth").toRawUTF8(), lfo[i].depth); }
    Mod::autoSet (s, "modSeqOn", seq.on ? 1.f : 0.f); Mod::autoSet (s, "modSeqAmt", seq.amt); Mod::autoSet (s, "modEnvOn", env.on ? 1.f : 0.f); Mod::autoSet (s, "modEnvAmt", env.amt[0]);
    for (int i = 0; i < 2; ++i) { const String n = "modShape" + String (i + 1); Mod::autoSet (s, (n + "On").toRawUTF8(), shape[i].on ? 1.f : 0.f); Mod::autoSet (s, (n + "Amt").toRawUTF8(), shape[i].amt[0]); }
    Mod::autoSet (s, "modXYOn", autoXY ? 1.f : 0.f); Mod::autoSet (s, "modX", autoX); Mod::autoSet (s, "modY", autoY); Mod::autoSet (s, "modA", aLevel);
}
void Mod::State::fromParams (const AudioProcessorValueTreeState& s)
{
    for (int i = 0; i < 2; ++i) { const String n = "modLfo" + String (i + 1); lfo[i].on = Mod::autoGet (s, (n + "On").toRawUTF8()) > 0.5f; lfo[i].hz = Mod::autoGet (s, (n + "Rate").toRawUTF8()); lfo[i].depth = Mod::autoGet (s, (n + "Depth").toRawUTF8()); }
    seq.on = Mod::autoGet (s, "modSeqOn") > 0.5f; seq.amt = Mod::autoGet (s, "modSeqAmt"); env.on = Mod::autoGet (s, "modEnvOn") > 0.5f; env.amt[0] = Mod::autoGet (s, "modEnvAmt");
    for (int i = 0; i < 2; ++i) { const String n = "modShape" + String (i + 1); shape[i].on = Mod::autoGet (s, (n + "On").toRawUTF8()) > 0.5f; shape[i].amt[0] = Mod::autoGet (s, (n + "Amt").toRawUTF8()); }
    autoXY = Mod::autoGet (s, "modXYOn") > 0.5f; autoX = Mod::autoGet (s, "modX"); autoY = Mod::autoGet (s, "modY"); aLevel = Mod::autoGet (s, "modA");
}
// ---------------------------------------------------------------- settings as a ValueTree
ValueTree Mod::State::toTree() const
{
    ValueTree t ("mod"); t.setProperty ("bpm", bpm, nullptr); t.setProperty ("a", aLevel, nullptr);
    for (int i = 0; i < 2; ++i) { ValueTree l ("lfo"); auto& s = lfo[i]; l.setProperty ("on", s.on, nullptr); l.setProperty ("shape", s.shape, nullptr); l.setProperty ("sync", s.sync, nullptr); l.setProperty ("hz", s.hz, nullptr); l.setProperty ("div", s.div, nullptr); l.setProperty ("depth", s.depth, nullptr);
        for (int d = 0; d < 2; ++d) { l.setProperty ("dest" + String (d), s.dest[d], nullptr); l.setProperty ("amt" + String (d), s.amt[d], nullptr); } t.appendChild (l, nullptr); }
    { ValueTree s ("seq"); s.setProperty ("on", seq.on, nullptr); s.setProperty ("length", seq.length, nullptr); s.setProperty ("div", seq.div, nullptr); s.setProperty ("dest", seq.dest, nullptr); s.setProperty ("amt", seq.amt, nullptr);
      String steps; for (int i = 0; i < 16; ++i) steps << String (seq.step[i], 3) << " "; s.setProperty ("steps", steps.trim(), nullptr); t.appendChild (s, nullptr); }
    { ValueTree e ("env"); e.setProperty ("on", env.on, nullptr); e.setProperty ("attack", env.attackMs, nullptr); e.setProperty ("decay", env.decayMs, nullptr); e.setProperty ("sustain", env.sustain, nullptr); e.setProperty ("release", env.releaseMs, nullptr);
      for (int d = 0; d < 3; ++d) { e.setProperty ("dest" + String (d), env.dest[d], nullptr); e.setProperty ("amt" + String (d), env.amt[d], nullptr); }
      e.setProperty ("curveA", env.curve[0], nullptr); e.setProperty ("curveD", env.curve[1], nullptr); e.setProperty ("curveR", env.curve[2], nullptr);
      e.setProperty ("dest", env.dest[0], nullptr); e.setProperty ("amt", env.amt[0], nullptr);   // the first destination also under its old names, for older panels
      t.appendChild (e, nullptr); }
    for (int i = 0; i < 2; ++i) t.appendChild (shape[i].toTree(), nullptr);
    { ValueTree x ("xy"); x.setProperty ("gateDest", xy.gateDest, nullptr); x.setProperty ("gateInv", xy.gateInv, nullptr);
      for (int a = 0; a < 3; ++a) { x.setProperty ("dest" + String (a), xy.dest[a], nullptr); x.setProperty ("amt" + String (a), xy.amt[a], nullptr); x.setProperty ("inv" + String (a), xy.inv[a], nullptr); } t.appendChild (x, nullptr); }
    return t;
}
void Mod::State::fromTree (const ValueTree& t)
{
    bpm = (float) (double) t.getProperty ("bpm", 120.0); aLevel = (float) (double) t.getProperty ("a", 0.0); int li = 0;
    for (auto l : t) if (l.hasType ("lfo") && li < 2) { auto& s = lfo[li++]; s.on = l.getProperty ("on", false); s.shape = l.getProperty ("shape", 1); s.sync = l.getProperty ("sync", true); s.hz = (float) (double) l.getProperty ("hz", 0.5); s.div = l.getProperty ("div", 4); s.depth = (float) (double) l.getProperty ("depth", 1.0);
        for (int d = 0; d < 2; ++d) { s.dest[d] = l.getProperty ("dest" + String (d), -1); s.amt[d] = (float) (double) l.getProperty ("amt" + String (d), 0.3); } }
    if (auto s = t.getChildWithName ("seq"); s.isValid()) { seq.on = s.getProperty ("on", false); seq.length = juce::jlimit (1, 16, (int) s.getProperty ("length", 16)); seq.div = s.getProperty ("div", 6); seq.dest = s.getProperty ("dest", -1); seq.amt = (float) (double) s.getProperty ("amt", 0.5);
        auto parts = StringArray::fromTokens (s.getProperty ("steps").toString(), " ", ""); for (int i = 0; i < 16 && i < parts.size(); ++i) seq.step[i] = parts[i].getFloatValue(); }
    if (auto e = t.getChildWithName ("env"); e.isValid()) { env.on = e.getProperty ("on", false); env.attackMs = (float) (double) e.getProperty ("attack", 5.0); env.decayMs = (float) (double) e.getProperty ("decay", 300.0); env.sustain = (float) (double) e.getProperty ("sustain", 0.0); env.releaseMs = (float) (double) e.getProperty ("release", 200.0);
        env.dest[0] = e.getProperty ("dest0", e.getProperty ("dest", -1)); env.amt[0] = (float) (double) e.getProperty ("amt0", e.getProperty ("amt", 0.5));   // files from before: one destination
        for (int d = 1; d < 3; ++d) { env.dest[d] = e.getProperty ("dest" + String (d), -1); env.amt[d] = (float) (double) e.getProperty ("amt" + String (d), 0.5); }
        env.curve[0] = (float) (double) e.getProperty ("curveA", 0.0); env.curve[1] = (float) (double) e.getProperty ("curveD", 0.0); env.curve[2] = (float) (double) e.getProperty ("curveR", 0.0); }
    { int si = 0; for (auto s : t) if (s.hasType ("shape") && si < 2) shape[si++].fromTree (s); }
    if (auto x = t.getChildWithName ("xy"); x.isValid()) { xy.gateDest = x.getProperty ("gateDest", -1); xy.gateInv = x.getProperty ("gateInv", false);
        for (int a = 0; a < 3; ++a) { xy.dest[a] = x.getProperty ("dest" + String (a), -1); xy.amt[a] = (float) (double) x.getProperty ("amt" + String (a), 0.5); xy.inv[a] = x.getProperty ("inv" + String (a), false); } }
}

// ---------------------------------------------------------------- state
void P6PanelProcessor::getStateInformation (MemoryBlock& dest)
{
    for (int i = 0; i < (int) P6::params().size(); ++i) if (dumpOffset[(size_t) i] >= 0) program[dumpOffset[(size_t) i]] = (uint8_t) valueOf (i);
    auto state = apvts.copyState();
    state.setProperty ("program", Base64::toBase64 (program, P6::kProgramBytes), nullptr);
    state.setProperty ("midiIn", link.wantedIn(), nullptr);      // as chosen, even while the device is away: it comes back by itself
    state.setProperty ("midiOut", link.wantedOut(), nullptr);
    state.setProperty ("midiKeys", link.wantedKeys(), nullptr);
    state.setProperty ("channel", link.channel, nullptr);
    state.setProperty ("bank", currentBank, nullptr);
    state.setProperty ("prog", currentProgram, nullptr);
    state.setProperty ("browserCategory", browserCategory, nullptr); state.setProperty ("browserSearch", browserSearch, nullptr); state.setProperty ("browserBanks", browserBanks, nullptr); state.setProperty ("libKey", libKey, nullptr);
    state.removeChild (state.getChildWithName ("mod"), nullptr); state.appendChild (mod.toTree(), nullptr);
    state.removeChild (state.getChildWithName ("euclid"), nullptr); state.appendChild (euclid.patch.toTree(), nullptr);
    state.removeChild (state.getChildWithName ("memories"), nullptr);
    {   // A B C D: the letter in use is the state itself; the others carry their sound
        ValueTree ms ("memories"); ms.setProperty ("active", memActive, nullptr);
        for (int i = 0; i < kMemories; ++i)
        {
            ValueTree t ("m"); const auto& e = mem[i]; const bool act = i == memActive;
            t.setProperty ("used", e.used || act, nullptr); t.setProperty ("origin", act && e.origin.isEmpty() ? originNow() : e.origin, nullptr);
            if (! act && e.used) { t.setProperty ("bank", e.bank, nullptr); t.setProperty ("prog", e.prog, nullptr); t.setProperty ("libKey", e.libKey, nullptr); t.setProperty ("data", Base64::toBase64 (e.data.getData(), e.data.getSize()), nullptr); }
            ms.appendChild (t, nullptr);
        }
        state.appendChild (ms, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}
void P6PanelProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto state = ValueTree::fromXml (*xml);
    if (! state.isValid()) return;
    restoring = true;
    apvts.replaceState (state);
    restoring = false;
    MemoryOutputStream mo; if (Base64::convertFromBase64 (mo, state.getProperty ("program").toString()) && mo.getDataSize() == (size_t) P6::kProgramBytes)
        memcpy (program, mo.getData(), P6::kProgramBytes);
    for (int i = 0; i < (int) P6::params().size(); ++i) { if (dumpOffset[(size_t) i] >= 0) program[dumpOffset[(size_t) i]] = (uint8_t) valueOf (i); lastSent[(size_t) i] = valueOf (i); }
    for (auto& d : dirty) d = false;
    link.channel = (int) state.getProperty ("channel", 1);
    currentBank = (int) state.getProperty ("bank", 0); currentProgram = (int) state.getProperty ("prog", 0);
    browserCategory = state.getProperty ("browserCategory", "").toString(); browserSearch = state.getProperty ("browserSearch", "").toString(); browserBanks = (int) state.getProperty ("browserBanks", 1); libKey = state.getProperty ("libKey", "").toString();
    setPorts (state.getProperty ("midiIn").toString(), state.getProperty ("midiOut").toString());
    if (const auto k = state.getProperty ("midiKeys").toString(); k != link.wantedKeys()) link.openKeys (k);
    if (auto m = state.getChildWithName ("mod"); m.isValid()) mod.fromTree (m);
    if (auto e = state.getChildWithName ("euclid"); e.isValid()) { euclid.patch.fromTree (e); if (euclid.patch.on && euclid.patch.toTree().toXmlString() == euclidFresh) euclid.patch.on = false;   /* saved by an older panel, where the untouched page was on by default: that was never asked for */ link.muteKeys = euclid.patch.on && euclid.patch.chordMute; }
    syncingMod = true; mod.toParams (apvts); syncingMod = false;   // the saved MOD settings are the reference; the automatable parameters follow them
    for (auto& e : mem) e = Memory(); memActive = 0; originPending = false;
    if (auto ms = state.getChildWithName ("memories"); ms.isValid())
    {
        memActive = jlimit (0, kMemories - 1, (int) ms.getProperty ("active", 0));
        for (int i = 0; i < jmin (kMemories, ms.getNumChildren()); ++i)
        {
            auto t = ms.getChild (i); auto& e = mem[i]; e.origin = t.getProperty ("origin", "").toString();
            MemoryOutputStream ds; if (i != memActive && (bool) t.getProperty ("used", false) && Base64::convertFromBase64 (ds, t.getProperty ("data", "").toString()) && ds.getDataSize() > 0)
            { e.used = true; e.data = ds.getMemoryBlock(); e.bank = (int) t.getProperty ("bank", 0); e.prog = (int) t.getProperty ("prog", 0); e.libKey = t.getProperty ("libKey", "").toString(); }
        }
    }
    ++uiVersion;
    // A Logic project that reopens should sound as it did: push the stored program to the edit buffer once the synth answers. The standalone
    // is the other way round - the synth is the source of truth there, so the panel reads its edit buffer instead of overwriting it.
    if (JUCEApplicationBase::isStandaloneApp()) fetchPending = true;
    else resendPending = haveStoredProgram (state);
}
bool P6PanelProcessor::haveStoredProgram (const ValueTree& state) const { return state.hasProperty ("program"); }

AudioProcessorEditor* P6PanelProcessor::createEditor() { return new P6PanelEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new P6PanelProcessor(); }
