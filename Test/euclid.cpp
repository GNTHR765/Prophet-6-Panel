// Tests for the EUCLID engine: the pattern maths, the scale helpers and the scheduler across audio blocks.
#include "Euclid.h"
#include <cstdio>
#include <map>

static int fails = 0;
#define CHECK(cond) do { if (! (cond)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static juce::String pattern (int n, int k, int rot = 0) { juce::String s; for (int i = 0; i < n; ++i) s << (Euclid::euclidHit (n, k, rot, i) ? 'x' : '.'); return s; }

int main()
{
    using namespace Euclid;
    // ---- the Euclidean rhythms from Toussaint's paper
    CHECK (pattern (8, 3) == "x..x..x.");          // tresillo
    CHECK (pattern (8, 5) == "x.x.xx.x");          // the cinquillo necklace (Bresenham's rotation of it)
    CHECK (pattern (16, 4) == "x...x...x...x...");  // four on the floor
    CHECK (pattern (4, 4) == "xxxx"); CHECK (pattern (4, 0) == "...."); CHECK (pattern (7, 7) == "xxxxxxx");
    for (int n = 1; n <= 32; ++n) for (int k = 0; k <= n; ++k) { int c = 0; for (int i = 0; i < n; ++i) c += euclidHit (n, k, 0, i); CHECK (c == k); }   // always exactly k hits
    CHECK (pattern (8, 3, 1) == ".x..x..x");        // rotation moves the hits later
    CHECK (pattern (8, 3, -1) == "..x..x.x");
    { // rotating by n is the identity
        for (int i = 0; i < 12; ++i) CHECK (euclidHit (12, 5, 12, i) == euclidHit (12, 5, 0, i));
    }
    // ---- scales
    CHECK (quantise (60, 1, 0) == 60);   // C in C major
    CHECK (quantise (61, 1, 0) == 62);   // C# -> D
    CHECK (quantise (61, 0, 0) == 61);   // chromatic leaves it
    CHECK (quantise (63, 6, 0) == 63);   // Eb in C minor
    CHECK (quantise (64, 6, 0) == 65);   // E -> F in C minor
    CHECK (quantise (60, 6, 2) == 60);   // C in D minor (the 7th degree)
    CHECK (stepScale (60, 2, 1, 0) == 64);   // a third up in C major: C -> E
    CHECK (stepScale (60, 4, 1, 0) == 67);   // C -> G
    CHECK (stepScale (60, 7, 1, 0) == 72);   // an octave of degrees
    CHECK (stepScale (60, -1, 1, 0) == 59);  // down to B
    CHECK (stepScale (62, 2, 6, 2) == 65);   // D minor: D -> F
    CHECK (stepScale (60, 2, 0, 0) == 62);   // chromatic: a degree is a semitone
    CHECK (noteText (60) == "C3"); CHECK (noteText (36) == "C1"); CHECK (noteText (61) == "C#3"); CHECK (noteText (59) == "B2");
    // ---- track overrides and the patch round trip
    { Track t; t.steps = 8; t.beats = 3; CHECK (t.hits() == 3); t.over[1] = 1; CHECK (t.hit (1)); CHECK (t.hits() == 4); t.over[0] = -1; CHECK (! t.hit (0)); CHECK (t.hits() == 3);
      Patch p; p.tr[2] = t; p.tr[2].accent[5] = true; p.scale = 3; p.root = 7; auto tree = p.toTree(); Patch q; q.fromTree (tree);
      CHECK (q.scale == 3 && q.root == 7); CHECK (q.tr[2].steps == 8 && q.tr[2].beats == 3); CHECK (q.tr[2].over[1] == 1 && q.tr[2].over[0] == -1); CHECK (q.tr[2].accent[5] && ! q.tr[2].accent[4]); }
    // ---- the scheduler: one track, 16 steps at 1/16, 4 beats, played through a bar in blocks of 64 samples at 120 bpm / 48 kHz
    {
        Engine e; e.patch = Patch(); for (auto& t : e.patch.tr) t.mute = true;
        auto& t = e.patch.tr[0]; t.mute = false; t.steps = 16; t.beats = 4; t.rotate = 0; t.chance = 100; t.length = 50; t.clock = 2; t.pitch = 60; t.chord = 1; t.ratchet = 1; t.swing = 0;
        e.patch.scaleOn = false;
        const double sr = 48000.0, bpm = 120.0, spb = sr * 60.0 / bpm;  // 24000 samples per beat, a 1/16 = 6000 samples
        std::vector<Event> out; int ons = 0, offs = 0; std::vector<juce::int64> onTimes, offTimes; juce::int64 clock = 0;
        for (int b = 0; b < 1500; ++b)   // exactly four beats
        {
            e.process (clock / spb, bpm, sr, 64, true, out);
            for (auto& ev : out) { CHECK (ev.sampleOffset >= 0 && ev.sampleOffset < 64); if (ev.msg.isNoteOn()) { ++ons; onTimes.push_back (clock + ev.sampleOffset); CHECK (ev.msg.getNoteNumber() == 60); CHECK (ev.msg.getVelocity() == 100); } else if (ev.msg.isNoteOff()) { ++offs; offTimes.push_back (clock + ev.sampleOffset); } }
            clock += 64;
        }
        CHECK (ons == 4); CHECK (offs == 4);
        for (int i = 0; i < 4 && i < (int) onTimes.size(); ++i) CHECK (std::abs (onTimes[(size_t) i] - i * 24000) <= 64);          // on the beat
        for (int i = 0; i < 4 && i < (int) offTimes.size(); ++i) CHECK (std::abs (offTimes[(size_t) i] - (i * 24000 + 3000)) <= 64);  // half a step later
        // stopping flushes nothing (all notes ended), and a stop with a note sounding sends its off
        e.process (4.51, bpm, sr, 64, true, out); CHECK (out.size() == 0); e.process (4.51, bpm, sr, 64, false, out); CHECK (out.size() == 0);   // between steps: nothing to play, nothing to end
        t.length = 400;   // notes longer than a step
        e.process (8.0, bpm, sr, 64, true, out); int n = 0; for (auto& ev : out) n += ev.msg.isNoteOn(); CHECK (n == 1); CHECK (e.pendingNotes() == 1);
        e.process (8.0 + 64.0 / spb, bpm, sr, 64, false, out); CHECK (out.size() == 1 && out[0].msg.isNoteOff()); CHECK (e.pendingNotes() == 0);
    }
    // ---- ratchets, chords and accents
    {
        Engine e; for (auto& t : e.patch.tr) t.mute = true;
        auto& t = e.patch.tr[1]; t.mute = false; t.steps = 4; t.beats = 1; t.rotate = 0; t.chance = 100; t.clock = 6; t.pitch = 60; t.chord = 3; t.ratchet = 4; t.length = 50; t.velocity = 90; t.accent[0] = true;
        e.patch.scaleOn = true; e.patch.scale = 1; e.patch.root = 0;
        const double sr = 48000.0, bpm = 120.0, spb = 24000.0;
        std::vector<Event> out; std::map<int, int> onsPerNote; int ons = 0; std::vector<juce::int64> onTimes; juce::int64 clock = 0;
        for (int b = 0; b < 375; ++b)   // four beats
        {
            e.process (clock / spb, bpm, sr, 256, true, out);
            for (auto& ev : out) if (ev.msg.isNoteOn()) { ++ons; ++onsPerNote[ev.msg.getNoteNumber()]; onTimes.push_back (clock + ev.sampleOffset); CHECK (ev.msg.getVelocity() == 120); }
            clock += 256;
        }
        CHECK (ons == 12);                                   // one hit x 4 ratchets x 3 chord notes
        CHECK (onsPerNote[60] == 4 && onsPerNote[64] == 4 && onsPerNote[67] == 4);   // C E G
        std::sort (onTimes.begin(), onTimes.end());
        for (int k = 0; k < 4; ++k) CHECK (std::abs (onTimes[(size_t) k * 3] - k * 6000) <= 256);   // sub-hits a quarter of the 1/4 step apart
    }
    // ---- swing, chance and solo
    {
        Engine e; for (auto& t : e.patch.tr) t.mute = true;
        auto& t = e.patch.tr[2]; t.mute = false; t.steps = 8; t.beats = 8; t.rotate = 0; t.chance = 100; t.clock = 4; t.swing = 50; t.pitch = 48; t.chord = 1; t.ratchet = 1; t.length = 10;
        e.patch.scaleOn = false;
        const double sr = 48000.0, bpm = 120.0, spb = 24000.0; std::vector<Event> out; std::vector<juce::int64> onTimes; juce::int64 clock = 0;
        for (int b = 0; b < 750; ++b) { e.process (clock / spb, bpm, sr, 128, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) onTimes.push_back (clock + ev.sampleOffset); clock += 128; }
        CHECK (onTimes.size() == 8);
        if (onTimes.size() == 8) { CHECK (std::abs (onTimes[0] - 0) <= 128); CHECK (std::abs (onTimes[1] - 16000) <= 128); CHECK (std::abs (onTimes[2] - 24000) <= 128); }   // 1/8 = 12000 samples; 50 % swing delays the second by a third of a step: 2/3 of the way through the pair, the triplet position
        t.swing = 0; t.chance = 0; onTimes.clear();
        for (int b = 0; b < 750; ++b) { e.process (clock / spb, bpm, sr, 128, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) onTimes.push_back (clock + ev.sampleOffset); clock += 128; }
        CHECK (onTimes.empty());
        t.chance = 100; e.patch.tr[3].solo = true; onTimes.clear();   // another track soloed: this one is silent
        for (int b = 0; b < 750; ++b) { e.process (clock / spb, bpm, sr, 128, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) onTimes.push_back (clock + ev.sampleOffset); clock += 128; }
        CHECK (onTimes.empty());
    }
    // ---- lanes: position rules, offsets on the track's controls, parameter locks, round trip
    {
        juce::Random rng; Lane l; l.target = laneVelocity; l.lock = false; l.length = 4;
        int fwd[8], back[8], pp[8]; for (int n = 0; n < 8; ++n) { l.dir = 0; fwd[n] = l.posAt (n, 16, rng); l.dir = 1; back[n] = l.posAt (n, 16, rng); l.dir = 2; pp[n] = l.posAt (n, 16, rng); }
        CHECK (fwd[0] == 0 && fwd[3] == 3 && fwd[4] == 0 && fwd[7] == 3);
        CHECK (back[0] == 3 && back[3] == 0 && back[4] == 3);
        CHECK (pp[0] == 0 && pp[1] == 1 && pp[2] == 2 && pp[3] == 3 && pp[4] == 2 && pp[5] == 1 && pp[6] == 0 && pp[7] == 1);
        l.lock = true; l.dir = 0; CHECK (l.posAt (17, 12, rng) == 5);   // locked: the track's length
        Track t; t.steps = 8; CHECK (t.laneCount() == 0); auto* a = t.ensureLane (laneChance); a->v[2] = -0.5f; CHECK (t.laneCount() == 1 && t.lane (laneChance) == a); t.ensureLane (kSynth + 7); CHECK (t.laneCount() == 2);
        t.removeLane (laneChance); CHECK (t.laneCount() == 1 && t.lane (laneChance) == nullptr && t.lane (kSynth + 7) != nullptr);
        Patch p; p.tr[1] = t; auto tree = p.toTree(); Patch q; q.fromTree (tree); CHECK (q.tr[1].laneCount() == 1 && q.tr[1].lane (kSynth + 7) != nullptr);
    }
    {
        Engine e; for (auto& t : e.patch.tr) t.mute = true;
        auto& t = e.patch.tr[0]; t.mute = false; t.steps = 16; t.beats = 4; t.rotate = 0; t.chance = 100; t.length = 50; t.clock = 2; t.pitch = 60; t.chord = 1; t.ratchet = 1; t.swing = 0; t.velocity = 100;
        e.patch.scaleOn = true; e.patch.scale = 1; e.patch.root = 0;
        auto* lv = t.ensureLane (laneVelocity); lv->v[4] = 0.25f;                        // step 4: +16
        auto* lp = t.ensureLane (lanePitch); lp->v[8] = 2.f / 12.f;                      // step 8: two scale degrees up, C3 -> E3
        auto* ls = t.ensureLane (kSynth + 5); ls->v[0] = 0.5f; ls->v[1] = 0.5f; ls->v[2] = -0.25f;   // a lock on parameter 5 for steps 0-2, back to the program from step 3
        std::vector<std::pair<int, float>> locks; e.onParamLock = [&] (int pidx, float off) { locks.push_back ({ pidx, off }); };
        const double sr = 48000.0, bpm = 120.0, spb = 24000.0; std::vector<Event> out; std::vector<std::pair<int, int>> ons; juce::int64 clock = 0;
        for (int b = 0; b < 1480; ++b) { e.process (clock / spb, bpm, sr, 64, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) ons.push_back ({ ev.msg.getNoteNumber(), (int) ev.msg.getVelocity() }); clock += 64; }
        CHECK (ons.size() == 4);
        if (ons.size() == 4) { CHECK (ons[0] == std::make_pair (60, 100)); CHECK (ons[1] == std::make_pair (60, 116)); CHECK (ons[2] == std::make_pair (64, 100)); CHECK (ons[3] == std::make_pair (60, 100)); }
        CHECK (locks.size() == 16);                                                      // the lock lane is reported at every step of the track
        if (locks.size() == 16) { CHECK (locks[0].first == 5 && std::abs (locks[0].second - 0.5f) < 0.001f); CHECK (std::abs (locks[2].second + 0.25f) < 0.001f); CHECK (locks[3].second == 0.f); CHECK (locks[15].second == 0.f); }
        locks.clear(); e.process (clock / spb, bpm, sr, 64, false, out);              // stop: the lock is released
        CHECK (locks.size() == 1 && locks[0].first == 5 && locks[0].second == 0.f);
        // a BEATS lane changes which steps hit: +4 beats on every step makes it eight-to-the-bar
        t.removeLane (laneVelocity); t.removeLane (lanePitch); t.removeLane (kSynth + 5); auto* lb = t.ensureLane (laneBeats); for (auto& x : lb->v) x = 0.5f;
        ons.clear(); for (int b = 0; b < 1480; ++b) { e.process (clock / spb, bpm, sr, 64, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) ons.push_back ({ ev.msg.getNoteNumber(), (int) ev.msg.getVelocity() }); clock += 64; }
        CHECK (ons.size() == 8);
    }
    // ---- pitch from the held chord: note k of D F A, wrapping an octave up past the top
    {
        Engine e; for (auto& t : e.patch.tr) t.mute = true; e.patch.scaleOn = false;
        e.getChord = [] (int* out, int) { out[0] = 62; out[1] = 65; out[2] = 69; return 3; };
        auto& t = e.patch.tr[0]; t.mute = false; t.steps = 4; t.beats = 1; t.rotate = 0; t.chance = 100; t.clock = 6; t.pitch = 40; t.chord = 1; t.ratchet = 1; t.length = 20;
        auto& u = e.patch.tr[1]; u = t; u.rotate = 1; u.src = 4;   // the 4th note: D an octave up
        t.src = 2;
        const double sr = 48000.0, bpm = 120.0, spb = 24000.0; std::vector<Event> out; std::vector<int> notes; juce::int64 clock = 0;
        for (int b = 0; b < 370; ++b) { e.process (clock / spb, bpm, sr, 256, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) notes.push_back (ev.msg.getNoteNumber()); clock += 256; }
        CHECK (notes.size() == 2); if (notes.size() == 2) { CHECK (notes[0] == 65); CHECK (notes[1] == 74); }
        e.getChord = [] (int*, int) { return 0; }; notes.clear();   // nothing played yet: the own PITCH
        for (int b = 0; b < 370; ++b) { e.process (clock / spb, bpm, sr, 256, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) notes.push_back (ev.msg.getNoteNumber()); clock += 256; }
        CHECK (notes.size() == 2 && notes[0] == 40 && notes[1] == 40);
        Patch p; p.tr[2].src = 3; Patch q; q.fromTree (p.toTree()); CHECK (q.tr[2].src == 3);
        // walking: up through B D F# A C#, one note per hit, starting again from the bottom when the chord changes
        Engine w; for (auto& x : w.patch.tr) x.mute = true; w.patch.scaleOn = false;
        auto& a = w.patch.tr[0]; a.mute = false; a.steps = 8; a.beats = 8; a.rotate = 0; a.chance = 100; a.clock = 4; a.pitch = 40; a.chord = 1; a.ratchet = 1; a.length = 20; a.src = 7;
        w.getChord = [] (int* out, int) { out[0] = 59; out[1] = 62; out[2] = 66; out[3] = 69; out[4] = 73; return 5; };
        notes.clear(); clock = 0; for (int b = 0; b < 370; ++b) { w.process (clock / spb, bpm, sr, 256, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) notes.push_back (ev.msg.getNoteNumber()); clock += 256; }
        CHECK (notes.size() == 8); if (notes.size() == 8) { const int want[8] = { 59, 62, 66, 69, 73, 59, 62, 66 }; for (int k = 0; k < 8; ++k) CHECK (notes[(size_t) k] == want[k]); }
        a.src = 9; w.getChord = [] (int* out, int) { out[0] = 60; out[1] = 64; out[2] = 67; return 3; }; notes.clear();   // up and down over C E G: C E G E C E G E
        for (int b = 0; b < 370; ++b) { w.process (clock / spb, bpm, sr, 256, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) notes.push_back (ev.msg.getNoteNumber()); clock += 256; }
        CHECK (notes.size() == 8); if (notes.size() == 8) { const int want[8] = { 60, 64, 67, 64, 60, 64, 67, 64 }; for (int k = 0; k < 8; ++k) CHECK (notes[(size_t) k] == want[k]); }
        // random steps over B D F# A C#: every hit a neighbour of the one before (one chord note up or down), never the same note twice, never off the ends
        w.seed (7);   // a known start for the dice: the walk below must reach both ends, which a free-running one misses once in sixty runs
        a.src = 11; w.getChord = [] (int* out, int) { out[0] = 59; out[1] = 62; out[2] = 66; out[3] = 69; out[4] = 73; return 5; }; notes.clear();
        for (int b = 0; b < 370 * 8; ++b) { w.process (clock / spb, bpm, sr, 256, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) notes.push_back (ev.msg.getNoteNumber()); clock += 256; }
        {
            static const int chordNotes[5] = { 59, 62, 66, 69, 73 }; auto indexOf = [] (int n) { for (int k = 0; k < 5; ++k) if (chordNotes[k] == n) return k; return -1; };
            bool neighbours = notes.size() >= 60, inChord = true, bothWays = false, reachedTop = false, reachedBottom = false;
            if (! neighbours) printf ("  (random steps: %d notes)\n", (int) notes.size());
            for (size_t k = 0; k < notes.size(); ++k) { const int i = indexOf (notes[k]); if (i < 0) inChord = false; if (i == 4) reachedTop = true; if (i == 0) reachedBottom = true;
                                                        if (k > 0) { const int d = i - indexOf (notes[k - 1]); if (d != 1 && d != -1) { neighbours = false; printf ("  (random steps: %d then %d)\n", notes[k - 1], notes[k]); } if (d == -1) bothWays = true; } }
            CHECK (inChord); CHECK (neighbours); CHECK (bothWays); CHECK (reachedTop && reachedBottom);
        }
    }
    // ---- INIT: an empty page, the key and the switches kept
    {
        Patch p; p.scale = 2; p.root = 4; p.chordMute = true; p.latch = false; p.on = false; p.tr[1].ensureLane (kSynth + 3)->v[0] = 0.5f; p.tr[0].over[3] = 1; p.tr[2].accent[1] = true; p.tr[3].glide[2] = true;
        p.setInit();
        CHECK (p.scale == 2 && p.root == 4 && p.chordMute && ! p.latch && ! p.on);
        for (int i = 0; i < kTracks; ++i) { auto& t = p.tr[(size_t) i]; CHECK (t.beats == 0 && t.hits() == 0 && t.laneCount() == 0 && t.steps == 16 && ! t.anyGlide()); CHECK (t.pitch == quantise (36 + 12 * i + 4, 2, 4)); }
        CHECK (p.tr[0].over[3] == 0 && ! p.tr[2].accent[1]);
        Engine e; e.patch = p; e.patch.on = true; std::vector<Event> out; int n = 0;
        for (int b = 0; b < 1500; ++b) { e.process (b * 64 / 24000.0, 120.0, 48000.0, 64, true, out); n += (int) out.size(); }
        CHECK (n == 0);   // silent until a track gets beats
    }
    // ---- glide steps survive a save
    { Patch p; p.tr[1].glide[5] = true; Patch q; q.fromTree (p.toTree()); CHECK (q.tr[1].glide[5] && ! q.tr[1].glide[4]); Patch r; r.fromTree (Patch().toTree()); CHECK (! r.tr[0].anyGlide()); }
    // ---- mono (the Voyager): one voice, the upper track wins a shared step, glide steps play legato with the glide switch on
    {
        auto run = [] (Engine& e, int blocks, std::vector<juce::MidiMessage>& msgs, std::vector<juce::int64>& when)
        {
            std::vector<Event> out; juce::int64 clock = 0; const double spb = 24000.0;
            for (int b = 0; b < blocks; ++b) { e.process (clock / spb, 120.0, 48000.0, 64, true, out); for (auto& ev : out) { msgs.push_back (ev.msg); when.push_back (clock + ev.sampleOffset); } clock += 64; }
            e.process (clock / spb, 120.0, 48000.0, 64, false, out); for (auto& ev : out) { msgs.push_back (ev.msg); when.push_back (clock); }
        };
        auto voicesOk = [] (const std::vector<juce::MidiMessage>& msgs, int maxVoices)
        {   // never more than maxVoices notes at once, every note ends, no off without its on
            std::map<int, int> on; int sounding = 0, worst = 0; bool ok = true;
            for (auto& m : msgs) { if (m.isNoteOn()) { if (on[m.getNoteNumber()]++ == 0) ++sounding; worst = std::max (worst, sounding); } else if (m.isNoteOff()) { if (on[m.getNoteNumber()] <= 0) ok = false; else if (--on[m.getNoteNumber()] == 0) --sounding; } }
            return ok && sounding == 0 && worst <= maxVoices;
        };
        // two tracks on every quarter (track 1 C3, track 2 G3) and a chord track: mono plays only track 1's C
        Engine e; e.mono = true; e.glideCC = 65; e.patch = Patch(); e.patch.scaleOn = false;
        for (auto& t : e.patch.tr) { t.mute = true; t.chance = 100; t.ratchet = 1; t.swing = 0; t.rotate = 0; t.velocity = 100; t.clock = 2; t.steps = 16; t.length = 50; }
        e.patch.tr[0] = e.patch.tr[3]; e.patch.tr[1] = e.patch.tr[3];
        e.patch.tr[0].mute = false; e.patch.tr[0].beats = 4; e.patch.tr[0].pitch = 60;
        e.patch.tr[1].mute = false; e.patch.tr[1].beats = 4; e.patch.tr[1].pitch = 67; e.patch.tr[1].chord = 3;
        std::vector<juce::MidiMessage> msgs; std::vector<juce::int64> when; run (e, 1500, msgs, when);
        int ons = 0; bool onlyC = true, anyCC = false; for (auto& m : msgs) { if (m.isNoteOn()) { ++ons; onlyC &= m.getNoteNumber() == 60; } anyCC |= m.isController(); }
        CHECK (ons == 4); CHECK (onlyC); CHECK (voicesOk (msgs, 1)); CHECK (! anyCC);   // no glide steps: the glide switch is left alone
        // track 2 alone on the off-beats as well: track 1 on 0,4,8,12 (C), track 2 on 2,6,10,14 (G): eight notes, alternating, still one voice
        Engine f; f.mono = true; f.glideCC = 65; f.patch = e.patch; f.patch.tr[1].rotate = 2; f.patch.tr[0].length = 300;   // track 1's notes would run into track 2's
        msgs.clear(); when.clear(); run (f, 1500, msgs, when);
        ons = 0; for (auto& m : msgs) ons += m.isNoteOn(); CHECK (ons == 8); CHECK (voicesOk (msgs, 1));
        // glide into track 1's second hit: CC65 127 before it, its note-on comes before the old note's off (legato), then CC65 0 on the next plain note
        Engine g; g.mono = true; g.glideCC = 65; g.patch = e.patch; g.patch.tr[1].mute = true; g.patch.tr[0].length = 400; g.patch.tr[0].glide[4] = true;
        g.patch.tr[0].lanes[0].target = -1; g.patch.tr[0].over[4] = 0; g.patch.tr[0].pitch = 60;
        auto* pl = g.patch.tr[0].ensureLane (lanePitch); pl->v[4] = 7.f / 24.f; pl->lock = true;   // step 4 a fifth higher (chromatic: semitones)
        msgs.clear(); when.clear(); run (g, 1500, msgs, when);
        int iCC = -1, iOn2 = -1, iOff1 = -1, iCC0 = -1; int k = 0, nOn = 0;
        for (auto& m : msgs) { if (m.isController() && m.getControllerNumber() == 65) { if (m.getControllerValue() == 127 && iCC < 0) iCC = k; else if (m.getControllerValue() == 0 && iCC >= 0 && iCC0 < 0) iCC0 = k; }
                               if (m.isNoteOn()) { if (++nOn == 2) iOn2 = k; } if (m.isNoteOff() && m.getNoteNumber() == 60 && iOn2 >= 0 && iOff1 < 0) iOff1 = k; ++k; }
        CHECK (iCC >= 0 && iCC < iOn2); CHECK (iOn2 >= 0 && msgs[(size_t) iOn2].getNoteNumber() == 67); CHECK (iOff1 > iOn2); CHECK (iOff1 >= 0 && when[(size_t) iOff1] == when[(size_t) iOn2]);
        CHECK (iCC0 > iOn2); CHECK (voicesOk (msgs, 2));   // legato: two notes for an instant, never more
        // a short note before a glide step is held on into it (legato even at LENGTH 50 %), the other notes keep their length
        Engine sl; sl.mono = true; sl.glideCC = 65; sl.patch = g.patch; sl.patch.tr[0].length = 50; msgs.clear(); when.clear(); run (sl, 1500, msgs, when);
        { juce::int64 on1 = -1, off1 = -1, on2 = -1, on3 = -1, off3 = -1; int c = 0;
          for (size_t q = 0; q < msgs.size(); ++q) { auto& m = msgs[q]; if (m.isNoteOn()) { ++c; if (c == 1) on1 = when[q]; if (c == 2) on2 = when[q]; if (c == 3) on3 = when[q]; }
                                                     if (m.isNoteOff() && c == 2 && off1 < 0 && m.getNoteNumber() == 60) off1 = when[q]; if (m.isNoteOff() && c == 3 && off3 < 0) off3 = when[q]; }
          CHECK (on1 == 0 && on2 > 0 && off1 == on2); CHECK (on3 > on2 && off3 > on3 && off3 - on3 <= 24000 / 2 + 64); CHECK (voicesOk (msgs, 2)); }
        // the panel's own glide on: plain notes leave it on, and stopping does not switch it off
        Engine h; h.mono = true; h.glideCC = 65; h.glideBase = true; h.patch = g.patch; msgs.clear(); when.clear(); run (h, 1500, msgs, when);
        bool sentOff = false; for (auto& m : msgs) sentOff |= m.isController() && m.getControllerValue() == 0; CHECK (! sentOff);
        // stopping after a glide step puts the switch back
        Engine s; s.mono = true; s.glideCC = 65; s.patch = g.patch; s.patch.tr[0].glide.fill (true); msgs.clear(); when.clear(); run (s, 900, msgs, when);
        CHECK (! msgs.empty() && msgs.back().isController() && msgs.back().getControllerValue() == 0);
        // poly panels (mono off) ignore glide steps
        Engine pz; pz.patch = g.patch; msgs.clear(); when.clear(); run (pz, 1500, msgs, when); bool cc = false; for (auto& m : msgs) cc |= m.isController(); CHECK (! cc);
    }
    // ---- a pattern file: XML text in and out, synth lanes named by parameter id
    {
        Euclid::paramNames().idOf = [] (int i) { return i == 19 ? juce::String ("lpFreq") : juce::String ("p" + juce::String (i)); };
        Euclid::paramNames().indexOf = [] (const juce::String& id) { return id == "lpFreq" ? 19 : id.startsWith ("p") ? id.substring (1).getIntValue() : -1; };
        Patch p; p.scale = 11; p.root = 2; auto* l = p.tr[1].ensureLane (kSynth + 19); l->v[0] = 0.5f; l->lock = false; l->length = 7; l->dir = 2; p.tr[3].mute = true; p.tr[0].accent[2] = true;
        const juce::String xml = p.toTree().toXmlString();
        CHECK (xml.contains ("param=\"lpFreq\"")); CHECK (xml.contains ("scale=\"11\""));
        auto doc = juce::XmlDocument::parse (xml); CHECK (doc != nullptr);
        Patch q; if (doc != nullptr) q.fromTree (juce::ValueTree::fromXml (*doc));
        CHECK (q.scale == 11 && q.root == 2 && q.tr[3].mute && ! q.tr[2].mute && q.tr[0].accent[2]);
        const auto* ql = q.tr[1].lane (kSynth + 19); CHECK (ql != nullptr); if (ql) { CHECK (std::abs (ql->v[0] - 0.5f) < 0.001f && ! ql->lock && ql->length == 7 && ql->dir == 2); }
        if (std::getenv ("EUCLID_PRINT_XML")) std::printf ("%s\n", xml.toRawUTF8());
    }
    if (auto* dir = std::getenv ("EUCLID_LOAD_DIR"))
    {   // load every pattern file under a folder and print what came out (a check of the factory bank)
        juce::Array<juce::File> fs = juce::File (dir).findChildFiles (juce::File::findFiles, true, "*.p6euclid"); fs.sort();
        for (auto& f : fs)
        {
            auto doc = juce::XmlDocument::parse (f); Patch p; if (doc == nullptr) { std::printf ("%s: NOT XML\n", f.getFileName().toRawUTF8()); ++fails; continue; }
            p.fromTree (juce::ValueTree::fromXml (*doc));
            std::printf ("%-22s %s %-9s |", f.getFileNameWithoutExtension().toRawUTF8(), noteName (p.root), scales()[(size_t) p.scale].name);
            for (int t = 0; t < kTracks; ++t) { auto& tr = p.tr[(size_t) t]; std::printf (" %2d/%-2d %s %s c%d l%d%s", tr.beats, tr.steps, noteText (tr.pitch).toRawUTF8(), clockNames()[tr.clock].toRawUTF8(), tr.chord, tr.laneCount(), tr.mute ? " M" : ""); }
            std::printf ("\n");
        }
    }
    // ==== November 2026: the engine's block edges, note pairing, swing, lanes, mono ratchets and lock release
    {   // ---- block edges: the host's beat position is worked out afresh for every block and does not meet the block before
        //      to the last digit; a step on a block edge must still play exactly once (48 kHz, 512 samples)
        struct Case { double bpm; int clock; }; const Case cases[2] = { { 120.0, 1 }, { 125.0, 2 } };   // 1/16T at 120, 1/16 at 125: steps that land on block edges
        for (int mono = 0; mono < 2; ++mono) for (auto& c : cases)
        {
            Engine e; e.mono = mono != 0; for (auto& t : e.patch.tr) { t = Track(); t.beats = 0; } e.patch.scaleOn = false;
            auto& t = e.patch.tr[0]; t.steps = 16; t.beats = 16; t.clock = c.clock; t.length = 20; t.pitch = 60;
            const double sr = 48000.0, spb = sr * 60.0 / c.bpm, stepSamples = clockBeats (c.clock) * spb; const int N = 512; std::vector<Event> out; std::vector<double> at; int on = 0, off = 0;
            for (long k = 0; k < 11250; ++k)   // two minutes
            {
                e.process (((double) (k * N) / sr) * (c.bpm / 60.0), c.bpm, sr, N, true, out);
                for (auto& ev : out) { if (ev.msg.isNoteOn()) { ++on; at.push_back ((double) (k * N + ev.sampleOffset)); } else if (ev.msg.isNoteOff()) ++off; CHECK (ev.sampleOffset >= 0 && ev.sampleOffset < N); }
            }
            e.process (0, c.bpm, sr, N, false, out); for (auto& ev : out) if (ev.msg.isNoteOff()) ++off;
            int twice = 0, missing = 0; for (size_t i = 1; i < at.size(); ++i) { const double d = (at[i] - at[i - 1]) / stepSamples; if (d < 0.5) ++twice; else if (d > 1.5) ++missing; }
            CHECK (twice == 0); CHECK (missing == 0); CHECK (on == off); CHECK (! at.empty() && at[0] == 0.0);
            CHECK (std::abs ((double) on - 11250.0 * N / stepSamples) <= 1.0);
        }
    }
    {   // ---- two tracks on one note: every key's stream is on, off, on, off ... and ends closed (no off before its on, no two ons in a row)
        auto alternates = [] (Engine& e, int blocks, int N)
        {
            std::vector<Event> out; std::map<int, bool> sounding; bool ok = true; const double sr = 48000.0, bpm = 120.0, spb = sr * 60.0 / bpm; double ppq = 0.0; int ons = 0;
            auto take = [&] { int last = 0; for (auto& ev : out) { ok &= ev.sampleOffset >= last; last = ev.sampleOffset; if (! ev.msg.isNoteOnOrOff()) continue; bool& s = sounding[ev.msg.getNoteNumber()]; ok &= s != ev.msg.isNoteOn(); s = ev.msg.isNoteOn(); ons += ev.msg.isNoteOn(); } };
            for (int k = 0; k < blocks; ++k) { e.process (ppq, bpm, sr, N, true, out); ppq += N / spb; take(); }
            e.process (0, bpm, sr, N, false, out); take();
            for (auto& s : sounding) ok &= ! s.second;
            return ok && ons > 0;
        };
        { Engine e; for (auto& t : e.patch.tr) { t = Track(); t.beats = 0; } e.patch.scaleOn = false;   // the same step, the same note
          for (int i = 0; i < 2; ++i) { auto& t = e.patch.tr[(size_t) i]; t.steps = 16; t.beats = 4; t.pitch = 60; t.length = 50 + 40 * i; } CHECK (alternates (e, 3000, 512)); }
        { Engine e; for (auto& t : e.patch.tr) { t = Track(); t.beats = 0; } e.patch.scaleOn = false;   // a later track's hits lying earlier in the block (1/16 against 1/16T), long gates
          auto& a = e.patch.tr[0]; a.steps = 16; a.beats = 16; a.pitch = 60; a.clock = 1; a.length = 180; auto& b = e.patch.tr[1]; b.steps = 16; b.beats = 16; b.pitch = 60; b.clock = 2; b.length = 180; CHECK (alternates (e, 3000, 1024)); }
        { Engine e; for (auto& t : e.patch.tr) { t = Track(); t.beats = 0; } e.patch.scaleOn = false;   // chords that share notes, ratchets that cross the blocks
          auto& a = e.patch.tr[0]; a.steps = 8; a.beats = 5; a.pitch = 60; a.chord = 4; a.ratchet = 3; a.length = 150; auto& b = e.patch.tr[1]; b.steps = 7; b.beats = 4; b.pitch = 64; b.chord = 3; b.ratchet = 4; b.clock = 4; b.length = 90;
          auto& c = e.patch.tr[2]; c.steps = 5; c.beats = 5; c.pitch = 67; c.swing = 50; c.length = 200; CHECK (alternates (e, 6000, 256)); }
        {   // the two tracks' one note lasts as long as the longer of the two gates, and sounds once
            Engine e; for (auto& t : e.patch.tr) { t = Track(); t.beats = 0; } e.patch.scaleOn = false;
            for (int i = 0; i < 2; ++i) { auto& t = e.patch.tr[(size_t) i]; t.steps = 16; t.beats = 1; t.pitch = 60; t.length = i == 0 ? 50 : 100; }
            std::vector<Event> out; int ons = 0, offAt = -1; for (int k = 0; k < 20; ++k) { e.process (k * 512 / 24000.0, 120.0, 48000.0, 512, true, out); for (auto& ev : out) { if (ev.msg.isNoteOn()) ++ons; else if (ev.msg.isNoteOff()) offAt = k * 512 + ev.sampleOffset; } }
            CHECK (ons == 1); CHECK (offAt == 6000);   // a whole sixteenth at 120 bpm
        }
    }
    {   // ---- swing goes by the place on the grid: a track of seven steps swings the same grid places on every pass
        Engine e; for (auto& t : e.patch.tr) { t = Track(); t.beats = 0; } e.patch.scaleOn = false;
        auto& t = e.patch.tr[0]; t.steps = 7; t.beats = 7; t.swing = 50; t.pitch = 60; t.length = 10;
        std::vector<Event> out; std::vector<long> at; const double spb = 24000.0;
        for (long k = 0; k < 1400; ++k) { e.process ((double) (k * 64) / spb, 120.0, 48000.0, 64, true, out); for (auto& ev : out) if (ev.msg.isNoteOn()) at.push_back (k * 64 + ev.sampleOffset); }
        CHECK (at.size() >= 14);
        for (size_t n = 0; n < at.size() && n < 14; ++n) CHECK (std::abs ((double) at[n] - ((double) n * 6000.0 + ((n & 1) ? 2000.0 : 0.0))) <= 1.0);   // odd grid places a triplet late, the even ones on the grid
    }
    {   // ---- a drawn lane plays the same after it went through a project or a pattern file
        juce::Random rnd (7); Lane l; l.target = laneVelocity; for (auto& x : l.v) x = laneSnap (rnd.nextFloat() * 2.f - 1.f);
        l.v[0] = laneSnap (0.3359f); l.v[1] = laneSnap (-1.7f); l.v[2] = laneSnap (0.004f);
        CHECK (l.v[1] == -1.f); CHECK (l.v[2] == 0.f); CHECK (std::abs (l.v[0] - 0.34f) < 1.0e-6f);
        Lane m; m.fromTree (l.toTree());
        for (int i = 0; i < kLaneSteps; ++i) { CHECK (m.v[(size_t) i] == l.v[(size_t) i]); for (int range : { 3, 8, 12, 24, 64, 100, 127, 164, 254, 255 }) CHECK (juce::roundToInt (m.v[(size_t) i] * (float) range) == juce::roundToInt (l.v[(size_t) i] * (float) range)); }
    }
    {   // ---- mono: a ratchet's repeats that start in later blocks are played (x4 gives four notes), one note at a time
        Engine e; e.mono = true; for (auto& t : e.patch.tr) { t = Track(); t.beats = 0; } e.patch.scaleOn = false;
        auto& t = e.patch.tr[0]; t.steps = 4; t.beats = 4; t.clock = 6; t.ratchet = 4; t.pitch = 60; t.length = 50;
        std::vector<Event> out; int on = 0, off = 0, depth = 0, maxDepth = 0; double ppq = 0.0;
        for (int k = 0; k < 750; ++k) { e.process (ppq, 120.0, 48000.0, 512, true, out); ppq += 512 / 24000.0; for (auto& ev : out) { if (ev.msg.isNoteOn()) { ++on; maxDepth = juce::jmax (maxDepth, ++depth); } else if (ev.msg.isNoteOff()) { ++off; --depth; } } }   // 16 beats: 16 hits
        e.process (0, 120.0, 48000.0, 512, false, out); for (auto& ev : out) if (ev.msg.isNoteOff()) { ++off; --depth; }
        CHECK (on == 64); CHECK (off == 64); CHECK (maxDepth == 1); CHECK (depth == 0);
    }
    {   // ---- a lock lane on a track that is muted lets go of its parameter - once - and takes it again when the track is heard
        Engine e; for (auto& t : e.patch.tr) { t = Track(); t.beats = 0; } e.patch.scaleOn = false;
        auto& t = e.patch.tr[0]; t.steps = 16; t.beats = 4; t.pitch = 60; for (auto& x : t.ensureLane (kSynth + 3)->v) x = 0.5f;
        auto& u = e.patch.tr[1]; u.steps = 16; u.beats = 4; u.pitch = 64; for (auto& x : u.ensureLane (kSynth + 9)->v) x = -0.25f;
        std::vector<std::pair<int, float>> calls; e.onParamLock = [&] (int p, float x) { calls.push_back ({ p, x }); };
        std::vector<Event> out; double ppq = 0.0; auto bar = [&] { for (int k = 0; k < 188; ++k) { e.process (ppq, 120.0, 48000.0, 512, true, out); ppq += 512 / 24000.0; } };
        bar(); int pushed3 = 0; for (auto& c : calls) pushed3 += c.first == 3 && c.second == 0.5f; CHECK (pushed3 >= 16); CHECK (e.lockedParams() == 2);
        calls.clear(); t.mute = true; bar();
        int zero3 = 0, any3 = 0, push9 = 0; for (auto& c : calls) { any3 += c.first == 3; zero3 += c.first == 3 && c.second == 0.f; push9 += c.first == 9 && c.second == -0.25f; }
        CHECK (zero3 == 1); CHECK (any3 == 1); CHECK (push9 >= 16); CHECK (e.lockedParams() == 1);   // released once; the other track's lock goes on
        calls.clear(); t.mute = false; bar(); pushed3 = 0; for (auto& c : calls) pushed3 += c.first == 3 && c.second == 0.5f; CHECK (pushed3 >= 16);
        calls.clear(); u.removeLane (kSynth + 9); bar(); int zero9 = 0, any9 = 0; for (auto& c : calls) { any9 += c.first == 9; zero9 += c.first == 9 && c.second == 0.f; }
        CHECK (zero9 == 1); CHECK (any9 == 1);                                                         // a lane that is taken away lets go as well
        calls.clear(); e.process (0, 120.0, 48000.0, 512, false, out); CHECK (calls.size() == 1 && calls[0].first == 3 && calls[0].second == 0.f); CHECK (e.lockedParams() == 0);
    }
    std::printf (fails == 0 ? "EUCLID TESTS PASSED\n" : "EUCLID TESTS: %d FAILURES\n", fails);
    return fails == 0 ? 0 : 1;
}
