#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <functional>
#include <vector>

/** The EUCLID page's engine: four Euclidean tracks that play the synth through the panel's own port, in step with the host.
    A track spreads BEATS hits as evenly as possible over STEPS steps (Bjorklund / Toussaint), turned by ROTATE; every hit
    passes a CHANCE roll, gets the track's VELOCITY (accented steps more), lasts LENGTH percent of a step, can repeat as
    RATCHET sub-hits, and plays PITCH (snapped to the scale) stacked into a CHORD of scale thirds. Each track runs at its
    own CLOCK, so tracks of different lengths drift apart and meet again. Steps can be switched on or off by hand
    (overriding the maths) and accented. Everything here is plain data plus a scheduler; no JUCE GUI. */
namespace Euclid
{
constexpr int kTracks = 4, kMaxSteps = 32, kMaxRatchet = 4, kMaxChord = 6, kLaneSteps = 64, kMaxLanes = 12;

// ---- clock divisions (one step = this many beats)
inline const juce::StringArray& clockNames() { static const juce::StringArray n { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" }; return n; }
inline double clockBeats (int i) { static const double b[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 }; return b[juce::jlimit (0, 11, i)]; }
constexpr int kClockDefault = 2;   // 1/16

// ---- scales: the seven modes of the major scale first (Orbit's grid), then a few others
struct Scale { const char* name; int count; int degree[12]; };
inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s {
        { "Chromatic",      12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "Major",           7, { 0, 2, 4, 5, 7, 9, 11 } },
        { "Dorian",          7, { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian",        7, { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian",          7, { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",      7, { 0, 2, 4, 5, 7, 9, 10 } },
        { "Minor",           7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "Locrian",         7, { 0, 1, 3, 5, 6, 8, 10 } },
        { "Harmonic minor",  7, { 0, 2, 3, 5, 7, 8, 11 } },
        { "Melodic minor",   7, { 0, 2, 3, 5, 7, 9, 11 } },
        { "Major pentatonic", 5, { 0, 2, 4, 7, 9 } },
        { "Minor pentatonic", 5, { 0, 3, 5, 7, 10 } },
        { "Blues",           6, { 0, 3, 5, 6, 7, 10 } },
        { "Whole tone",      6, { 0, 2, 4, 6, 8, 10 } } };
    return s;
}
inline const char* noteName (int pc) { static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n[((pc % 12) + 12) % 12]; }
/** A note's name with Logic's octave numbering (middle C, note 60, is C3). */
inline juce::String noteText (int note) { return juce::String (noteName (note)) + juce::String (note / 12 - 2); }

/** Is step i of a Euclidean pattern with k hits in n steps a hit? Bresenham's form of Bjorklund's algorithm: the hits land
    where the running sum i*k crosses a multiple of n, which spreads them as evenly as the numbers allow. rotate turns the
    pattern so that a positive rotation moves the hits later. */
inline bool euclidHit (int n, int k, int rotate, int i)
{
    if (n <= 0 || k <= 0) return false; if (k >= n) return true;
    const int j = (((i - rotate) % n) + n) % n;
    return (j * k) % n < k;
}
/** The nearest note at or above `note` that belongs to the scale (root 0-11). The chromatic scale leaves notes alone. */
inline int quantise (int note, int scaleIndex, int root)
{
    const auto& sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scaleIndex)];
    if (sc.count >= 12) return juce::jlimit (0, 127, note);
    for (int up = 0; up < 12; ++up)
    {
        const int n = note + up; const int pc = (((n - root) % 12) + 12) % 12;
        for (int d = 0; d < sc.count; ++d) if (sc.degree[d] == pc) return juce::jlimit (0, 127, n);
    }
    return juce::jlimit (0, 127, note);
}
/** The scale note `degrees` scale steps above `note` (which should itself be in the scale); negative goes down.
    In the chromatic scale a degree is a semitone. */
inline int stepScale (int note, int degrees, int scaleIndex, int root)
{
    const auto& sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scaleIndex)];
    if (sc.count >= 12) return juce::jlimit (0, 127, note + degrees);
    // find the degree of `note` (snap up if it is not in the scale)
    note = quantise (note, scaleIndex, root);
    const int pc = (((note - root) % 12) + 12) % 12; int d = 0; for (int i = 0; i < sc.count; ++i) if (sc.degree[i] == pc) d = i;
    const int octave = (note - root - pc) / 12;   // whole octaves above the root below the note
    const int total = d + degrees; const int oct = (int) std::floor ((double) total / sc.count); const int dd = ((total % sc.count) + sc.count) % sc.count;
    return juce::jlimit (0, 127, root + (octave + oct) * 12 + sc.degree[dd]);
}

/** A modulation lane: one value per step, -1..1, drawn as bars around a centre line. 0 leaves the target alone. Its target is
    one of the track's own controls (then the value is an offset in that control's units - a BEATS lane at +0.25 adds two
    beats) or, from kSynth up, a parameter of the synth (then the value is a fraction of that parameter's range, pushed
    from the program's own value: the "parameter lock" of a step). The lane has its own length and direction, so it can
    run against the track's pattern; LOCK keeps it the track's length. */
/** How synth parameters are named in saved files (set by the plugin: the parameter table's ids), so a pattern file stays
    readable if the table is ever reordered. */
struct ParamNames { std::function<juce::String (int)> idOf; std::function<int (const juce::String&)> indexOf; };
inline ParamNames& paramNames() { static ParamNames p; return p; }
enum LaneTarget { laneBeats = 0, laneRotate, laneChance, laneVelocity, laneLength, lanePitch, laneChord, laneRatchet, kTrackLanes, kSynth = 100 };
inline const char* laneName (int target) { static const char* n[kTrackLanes] = { "BEATS", "ROTATE", "CHANCE", "VELOCITY", "LENGTH", "PITCH", "CHORD", "RATCHET" }; return target >= 0 && target < kTrackLanes ? n[target] : "?"; }
inline int laneRange (int target) { static const int r[kTrackLanes] = { 8, 8, 100, 64, 100, 24, 3, 3 }; return target >= 0 && target < kTrackLanes ? r[target] : 0; }   // what +1 means, in the control's units (PITCH: semitones, or 12 scale degrees)
/** A lane value as a saved lane holds it: in hundredths. Everything that writes a lane value snaps it with this, so the
    pattern that comes back from a project or a file plays exactly what was drawn (a value between two hundredths could
    round to another offset after loading). A synth lock lane therefore moves its parameter in steps of 1 % of its range. */
inline float laneSnap (float v) { return (float) juce::roundToInt (juce::jlimit (-1.f, 1.f, v) * 100.f) / 100.f; }
struct Lane
{
    int target = -1; int length = 16; int dir = 0; bool lock = true;   // target -1: an empty slot. dir: 0 forwards, 1 backwards, 2 ping-pong, 3 random
    std::array<float, kLaneSteps> v {};
    bool any() const { for (auto x : v) if (x != 0.f) return true; return false; }
    int len (int trackSteps) const { return juce::jlimit (1, kLaneSteps, lock ? trackSteps : length); }
    /** The lane step for the track's absolute step count n (random: a fresh one each time). */
    int posAt (juce::int64 n, int trackSteps, juce::Random& rng) const
    {
        const int L = len (trackSteps); if (L <= 1) return 0;
        switch (dir)
        {
            case 1:  return L - 1 - (int) (n % L);
            case 2:  { const int p = (int) (n % (2 * L - 2)); return p < L ? p : 2 * L - 2 - p; }
            case 3:  return rng.nextInt (L);
            default: return (int) (n % L);
        }
    }
    float at (juce::int64 n, int trackSteps, juce::Random& rng) const { return v[(size_t) juce::jlimit (0, kLaneSteps - 1, posAt (n, trackSteps, rng))]; }
    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("lane"); t.setProperty ("target", target, nullptr); t.setProperty ("length", length, nullptr); t.setProperty ("dir", dir, nullptr); t.setProperty ("lock", lock, nullptr);
        if (target >= kSynth && paramNames().idOf) t.setProperty ("param", paramNames().idOf (target - kSynth), nullptr);
        juce::String vals; for (int i = 0; i < kLaneSteps; ++i) { if (i) vals << ","; vals << juce::roundToInt (v[(size_t) i] * 100.f); } t.setProperty ("v", vals, nullptr); return t;
    }
    void fromTree (const juce::ValueTree& t)
    {
        target = t.getProperty ("target", -1);
        if (t.hasProperty ("param") && paramNames().indexOf) { const int i = paramNames().indexOf (t.getProperty ("param").toString()); target = i >= 0 ? kSynth + i : -1; }
        length = juce::jlimit (2, kLaneSteps, (int) t.getProperty ("length", 16)); dir = juce::jlimit (0, 3, (int) t.getProperty ("dir", 0)); lock = t.getProperty ("lock", true);
        juce::StringArray a; a.addTokens (t.getProperty ("v").toString(), ",", ""); for (int i = 0; i < kLaneSteps; ++i) v[(size_t) i] = i < a.size() ? juce::jlimit (-1.f, 1.f, a[i].getIntValue() / 100.f) : 0.f;
    }
};

/** One track. The step overrides: 0 = as the maths say, 1 = always a hit, -1 = never. */
struct Track
{
    int steps = 16, beats = 4, rotate = 0, chance = 100, velocity = 100, swing = 0, length = 50, clock = kClockDefault, pitch = 48, chord = 1, ratchet = 1;
    int src = 0;   // where the pitch comes from: 0 = PITCH, 1-6 = that note of the chord held in Logic / on the keys (counted from the bottom; past the last note it goes round an octave up),
                   // 7-11 = walk through the chord one note per hit: up, down, up and down, random notes (dice), random steps (a neighbour each hit, bouncing at the ends)
    bool mute = false, solo = false;
    std::array<int8_t, kMaxSteps> over {}; std::array<bool, kMaxSteps> accent {};
    std::array<bool, kMaxSteps> glide {};   // mono synths (the Voyager): the step glides into its note - portamento on, and legato when the note before still sounds
    std::array<Lane, kMaxLanes> lanes {};   // fixed slots (the audio thread reads them while the page edits): an empty slot has target -1
    bool hit (int i) const { return hit (i, beats, rotate); }
    bool hit (int i, int b, int r) const { if (i < 0 || i >= steps) return false; if (over[(size_t) i] != 0) return over[(size_t) i] > 0; return euclidHit (steps, b, r, i); }
    const Lane* lane (int target) const { for (auto& l : lanes) if (l.target == target && target >= 0) return &l; return nullptr; }
    Lane* lane (int target) { for (auto& l : lanes) if (l.target == target && target >= 0) return &l; return nullptr; }
    int laneCount() const { int n = 0; for (auto& l : lanes) n += l.target >= 0; return n; }
    /** The lane for a target, made if there is none (in a free slot; with every slot taken, the first one gives way). */
    Lane* ensureLane (int target)
    {
        if (auto* l = lane (target)) return l;
        Lane* slot = nullptr; for (auto& l : lanes) if (l.target < 0) { slot = &l; break; } if (slot == nullptr) slot = &lanes[0];
        Lane fresh; fresh.target = -1; *slot = fresh; slot->target = target; return slot;   // the target last, so the audio thread never sees a half-made lane
    }
    void removeLane (int target) { if (auto* l = lane (target)) l->target = -1; }
    int  hits() const { int n = 0; for (int i = 0; i < steps; ++i) n += hit (i); return n; }
    void clearOverrides() { over.fill (0); accent.fill (false); glide.fill (false); }
    bool anyGlide() const { for (int i = 0; i < steps; ++i) if (glide[(size_t) i]) return true; return false; }
    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("track");
        t.setProperty ("steps", steps, nullptr); t.setProperty ("beats", beats, nullptr); t.setProperty ("rotate", rotate, nullptr); t.setProperty ("chance", chance, nullptr); t.setProperty ("velocity", velocity, nullptr);
        t.setProperty ("swing", swing, nullptr); t.setProperty ("length", length, nullptr); t.setProperty ("clock", clock, nullptr); t.setProperty ("pitch", pitch, nullptr); t.setProperty ("chord", chord, nullptr); t.setProperty ("ratchet", ratchet, nullptr); t.setProperty ("src", src, nullptr);
        t.setProperty ("mute", mute, nullptr); t.setProperty ("solo", solo, nullptr);
        juce::String o, a, gl; for (int i = 0; i < kMaxSteps; ++i) { o << (over[(size_t) i] > 0 ? "+" : over[(size_t) i] < 0 ? "-" : "."); a << (accent[(size_t) i] ? "A" : "."); gl << (glide[(size_t) i] ? "G" : "."); }
        t.setProperty ("over", o, nullptr); t.setProperty ("accent", a, nullptr); if (gl.containsChar ('G')) t.setProperty ("glide", gl, nullptr);
        for (auto& l : lanes) if (l.target >= 0) t.appendChild (l.toTree(), nullptr);
        return t;
    }
    void fromTree (const juce::ValueTree& t)
    {
        steps = juce::jlimit (1, kMaxSteps, (int) t.getProperty ("steps", 16)); beats = juce::jlimit (0, kMaxSteps, (int) t.getProperty ("beats", 4)); rotate = (int) t.getProperty ("rotate", 0);
        chance = juce::jlimit (0, 100, (int) t.getProperty ("chance", 100)); velocity = juce::jlimit (1, 127, (int) t.getProperty ("velocity", 100)); swing = juce::jlimit (0, 50, (int) t.getProperty ("swing", 0));
        length = juce::jlimit (5, 200, (int) t.getProperty ("length", 50)); clock = juce::jlimit (0, 11, (int) t.getProperty ("clock", kClockDefault)); pitch = juce::jlimit (0, 127, (int) t.getProperty ("pitch", 48));
        chord = juce::jlimit (1, kMaxChord, (int) t.getProperty ("chord", 1)); ratchet = juce::jlimit (1, kMaxRatchet, (int) t.getProperty ("ratchet", 1)); src = juce::jlimit (0, 11, (int) t.getProperty ("src", 0));
        mute = t.getProperty ("mute", false); solo = t.getProperty ("solo", false);
        const juce::String o = t.getProperty ("over").toString(), a = t.getProperty ("accent").toString(), gl = t.getProperty ("glide").toString();
        for (int i = 0; i < kMaxSteps; ++i) { over[(size_t) i] = (int8_t) (i < o.length() ? (o[i] == '+' ? 1 : o[i] == '-' ? -1 : 0) : 0); accent[(size_t) i] = i < a.length() && a[i] == 'A'; glide[(size_t) i] = i < gl.length() && gl[i] == 'G'; }
        for (auto& l : lanes) l.target = -1; int li = 0; for (auto c : t) if (c.hasType ("lane") && li < kMaxLanes) { Lane l; l.fromTree (c); lanes[(size_t) li++] = l; }
    }
};

/** The whole page: the tracks plus the key. */
struct Patch
{
    std::array<Track, kTracks> tr;
    int scale = 6, root = 0;        // Minor, C
    bool scaleOn = true, on = true; // on: the master switch (off = silence, the host keeps running)
    bool chordMute = false;         // the chord played into the panel drives the tracks but is not passed on to the synth
    bool latch = true;              // the last chord keeps playing after the keys / the region let go (off: the chord tracks fall silent until the next chord)
    Patch() { setDefault(); }
    void setDefault()
    {   // four tracks that already make sense together: a bass, a stab, a melody and an accent, in rotated canon like Orbit's opening patch
        for (auto& t : tr) t = Track();
        tr[0].pitch = 36; tr[0].beats = 4;  tr[0].rotate = 0; tr[0].length = 60;                 // C1  four on the floor
        tr[1].pitch = 55; tr[1].beats = 3;  tr[1].rotate = 2; tr[1].chord = 3; tr[1].length = 40; // G2  a triad on the tresillo
        tr[2].pitch = 67; tr[2].beats = 5;  tr[2].rotate = 1; tr[2].steps = 12; tr[2].chance = 80; tr[2].velocity = 90;   // G3  a 12-step melody that drifts against the rest
        tr[3].pitch = 72; tr[3].beats = 2;  tr[3].rotate = 3; tr[3].steps = 7;  tr[3].clock = 4; tr[3].chance = 70; tr[3].velocity = 80;   // C4  sparse accents at 1/8
        for (int i = 0; i < kTracks; ++i) tr[(size_t) i].pitch = quantise (tr[(size_t) i].pitch, scale, root);
    }
    /** INIT: an empty page to start from - four silent tracks (no beats yet), an octave apart from the key's root, at the
        default settings, with no hand-set steps, accents, glides or lanes. The key and the switches (ON, MUTE CHORD, LATCH) stay. */
    void setInit()
    {
        for (int i = 0; i < kTracks; ++i) { auto& t = tr[(size_t) i]; t = Track(); t.beats = 0; t.pitch = quantise (36 + 12 * i + root, scale, root); }
    }
    bool anySolo() const { for (auto& t : tr) if (t.solo) return true; return false; }
    bool audible (int i) const { auto& t = tr[(size_t) i]; return ! t.mute && (! anySolo() || t.solo); }
    juce::ValueTree toTree() const { juce::ValueTree p ("euclid"); p.setProperty ("scale", scale, nullptr); p.setProperty ("root", root, nullptr); p.setProperty ("scaleOn", scaleOn, nullptr); p.setProperty ("on", on, nullptr); p.setProperty ("chordMute", chordMute, nullptr); p.setProperty ("latch", latch, nullptr); for (auto& t : tr) p.appendChild (t.toTree(), nullptr); return p; }
    void fromTree (const juce::ValueTree& p)
    {
        if (! p.isValid()) return;
        scale = juce::jlimit (0, (int) scales().size() - 1, (int) p.getProperty ("scale", 6)); root = juce::jlimit (0, 11, (int) p.getProperty ("root", 0)); scaleOn = p.getProperty ("scaleOn", true); on = p.getProperty ("on", true); chordMute = p.getProperty ("chordMute", false); latch = p.getProperty ("latch", true);
        int i = 0; for (auto c : p) if (c.hasType ("track") && i < kTracks) tr[(size_t) i++].fromTree (c);
    }
};

/** The clock time of each audio block's first sample, for sending the block's events at their own moments
    (start + offset) instead of all at once. The host does not call at even intervals - blocks come early, late, two at a
    time - so the time is not simply 'now': it runs on by the block's length from the last one, is pushed later at once by a
    block that comes late, and falls back only slowly. So the events keep their true distances, at the cost of a delay no
    longer than the host's unevenness. Times are milliseconds (Time::getMillisecondCounterHiRes()). */
struct BlockClock
{
    double start (double nowMs, int numSamples, double sampleRate)
    {
        const double len = 1000.0 * numSamples / juce::jmax (1.0, sampleRate);
        double base = nowMs;
        if (valid) { const double ahead = next - nowMs; if (ahead > 0.0 && ahead < juce::jmax (50.0, 3.0 * len)) base = nowMs + ahead * 0.98; }
        next = base + len; valid = true; return base;
    }
    void reset() { valid = false; }
    double next = 0.0; bool valid = false;
};

/** One scheduled MIDI event, with its sample offset inside the block. */
struct Event { int sampleOffset; juce::MidiMessage msg; };

/** The scheduler. Call process() once per audio block with the host's position; it returns the note-ons and note-offs that
    fall inside the block (sorted by offset). Tracks keep their place from the song position, so loops, jumps and a stopped
    transport all behave: the pattern is a pure function of the beat. Note-offs (and ratchet sub-hits that start in a later
    block) are kept across blocks and flushed - sent at once - when the transport stops or the master switch goes off. */
class Engine
{
public:
    Patch patch;
    int channel = 1;
    Engine() { offs.reserve (512); ons.reserve (256); hits.reserve (512); touched.reserve (128); }   // room up front: the audio thread then never grows them
    /** Mono synths (the Voyager): one note at a time. Tracks that hit together: the upper one (track 1 first) plays, the rest
        give way; a new note ends the one before (no stuck or doubled notes), chords play their lowest note. A step marked GLIDE
        turns the synth's glide on for its note (glideCC, 127), and the track's note before it holds on into it, so the two
        play legato (the pitch slides, the envelope carries on - a 303's slide); the other notes switch glide back to the
        panel's own setting. */
    bool mono = false; int glideCC = -1; bool glideBase = false;
    static constexpr int kOutRoom = 1024;   // events a block's list has room for from the start

    /** @param ppqStart   the host's beat position at the start of the block (quarter notes)
        @param bpm        the tempo; @param sampleRate, numSamples the block
        @param playing    the host transport (or the page's own RUN when there is no host) */
    void process (double ppqStart, double bpm, double sampleRate, int numSamples, bool playing, std::vector<Event>& out)
    {
        if (out.capacity() < (size_t) kOutRoom) out.reserve ((size_t) kOutRoom);   // once, on the very first block (or never, when the owner made room): after that nothing here touches the heap
        out.clear();
        const bool run = playing && patch.on && bpm > 0.0 && sampleRate > 0.0;
        if (! run)
        {   // stopped: every sounding note ends now, nothing queued survives, the positions reset
            for (auto& o : offs) out.push_back ({ 0, juce::MidiMessage::noteOff (o.channel, o.note) });
            offs.clear(); ons.clear(); hits.clear(); wasRunning = false; for (auto& s : uiStep) s = -1; sampleClock += numSamples; walkPos.fill (0); lastEndValid = false;
            releaseGlide (out); releaseLocks(); return;
        }
        wasRunning = true;
        const double spb = sampleRate * 60.0 / bpm;                  // samples per beat
        const double blockBeats = numSamples / spb, ppqEnd = ppqStart + blockBeats;
        // The block's steps are those in [from, ppqEnd). from is where the block before ended, not the host's ppqStart: the two
        // differ in their last digits (more on a tempo ramp), and a step on a block edge then fell into both blocks or into
        // neither - played twice, or not at all. A real jump (cycle, locate) is further off than half a block: it starts at ppqStart.
        const double from = lastEndValid && std::abs (ppqStart - lastEnd) < 0.5 * blockBeats ? lastEnd : ppqStart;
        const juce::int64 blockEnd = sampleClock + numSamples;
        // the block's hits are collected first and played in time order afterwards (the tracks come one after the other, not in time)
        hits.clear();
        for (size_t i = 0; i < ons.size();)
        {   // what was queued earlier and is due now (mono too: a ratchet's repeats that waited for this block)
            if (ons[i].at < blockEnd) { if (hits.size() < hits.capacity()) hits.push_back (ons[i]); ons.erase (ons.begin() + (long) i); }
            else ++i;
        }
        for (int ti = 0; ti < kTracks; ++ti)
        {
            auto& t = patch.tr[(size_t) ti];
            const double L = clockBeats (t.clock);                     // one step in beats
            const double swingDelay = L * (t.swing / 100.0) * (2.0 / 3.0);   // 50 % swing puts every second step on the triplet
            const int steps = juce::jmax (1, t.steps);
            // every step boundary n*L (+ swing on the odd ones) that falls inside [from, ppqEnd)
            const juce::int64 nFirst = (juce::int64) std::floor ((from - swingDelay) / L) - 1, nLast = (juce::int64) std::ceil (ppqEnd / L) + 1;
            for (juce::int64 n = juce::jmax ((juce::int64) 0, nFirst); n <= nLast; ++n)
            {
                const int step = (int) (n % steps);
                const double at = n * L + ((n & 1) ? swingDelay : 0.0);   // swing goes by the place on the grid, not in the pattern: a track of 7 steps swings with the others on every pass
                if (at < from || at >= ppqEnd) continue;
                uiStep[(size_t) ti] = step; uiN[(size_t) ti] = n;
                const bool audible = patch.audible (ti);
                // the lanes for this step: offsets to the track's own controls, and the synth's parameter locks
                int beats = t.beats, rotate = t.rotate, chance = t.chance, velocity = t.velocity, length = t.length, chord = t.chord, ratchet = t.ratchet, pitchOff = 0;
                for (size_t li = 0; li < t.lanes.size(); ++li)
                {
                    auto& l = t.lanes[li]; const int target = l.target; if (target < 0) continue;   // read once: the page may change it meanwhile
                    const int pos = juce::jlimit (0, kLaneSteps - 1, l.posAt (n, steps, rng)); uiLanePos[(size_t) ti][li] = pos;   // the step the lane is on (a random lane's choice included)
                    const float x = l.v[(size_t) pos];
                    if (target >= kSynth)
                    {   // a lock: pushed while the track is heard; a track that is muted (or not soloed) lets go of it - once, not on every step
                        if (onParamLock) { const int p = target - kSynth; if (! audible) unlock (p, ti); else { onParamLock (p, x); touch (p, ti); } }
                        continue;
                    }
                    const int off = juce::roundToInt (x * (float) laneRange (target));
                    switch (target)
                    {
                        case laneBeats: beats += off; break;           case laneRotate: rotate += off; break;
                        case laneChance: chance += off; break;         case laneVelocity: velocity += off; break;
                        case laneLength: length += off; break;         case laneChord: chord += off; break;
                        case laneRatchet: ratchet += off; break;
                        case lanePitch: pitchOff = patch.scaleOn ? juce::roundToInt (x * 12.f) : off; break;   // in key: scale degrees, else semitones
                        default: break;
                    }
                }
                if (onParamLock) unlockOrphans (t, ti);   // a lock whose lane is gone (removed, retargeted, another pattern loaded): back to the program's value
                if (mono) chord = 1;   // one voice: the chord's lowest note
                beats = juce::jlimit (0, steps, beats); chance = juce::jlimit (0, 100, chance); velocity = juce::jlimit (1, 127, velocity); length = juce::jlimit (5, 400, length); chord = juce::jlimit (1, kMaxChord, chord); ratchet = juce::jlimit (1, kMaxRatchet, ratchet);
                if (! t.hit (step, beats, rotate) || ! audible) continue;
                if (chance < 100 && rng.nextInt (100) >= chance) continue;
                uiFlash |= (1u << ti);
                const int vel = juce::jlimit (1, 127, t.accent[(size_t) step] ? juce::jmin (velocity + 30, 127) : velocity);
                const int r = ratchet;
                const double sub = L / r; juce::int64 gate = (juce::int64) juce::jmax (1.0, sub * (length / 100.0) * spb);
                if (mono && r == 1)
                {   // the track's next hit glides: this note holds on into it, so the two play legato (a 303's slide)
                    for (int d = 1; d <= steps; ++d) { const int s2 = (step + d) % steps; if (! t.hit (s2, beats, rotate)) continue;
                                                       if (t.glide[(size_t) s2]) gate = juce::jmax (gate, (juce::int64) ((d + 0.4) * L * spb)); break; }
                }
                // the notes: the pitch (snapped to the scale, moved by the PITCH lane) and the chord stacked on it in scale thirds
                const int src = t.src;   // read once: the page may change it while this hit is worked out
                int notes[kMaxChord]; int nn = 0;
                int own = t.pitch; bool silent = false;
                if (src > 0 && getChord)
                {
                    int notesHeld[16]; const int nh = getChord (notesHeld, 16);
                    if (nh == 0 && ! patch.latch) silent = true;   // LATCH off and nothing held: the chord tracks rest
                    if (nh > 0)
                    {
                        if (src <= 6) { const int k = src - 1; own = juce::jlimit (0, 127, notesHeld[k % nh] + 12 * (k / nh)); }   // the n-th note; past the top it goes round an octave up
                        else
                        {   // walking: one chord note per hit; a new chord starts the walk again from its first note
                            unsigned sig = (unsigned) nh; for (int k = 0; k < nh; ++k) sig = sig * 131u + (unsigned) notesHeld[k];
                            if (sig != walkChord[(size_t) ti]) { walkChord[(size_t) ti] = sig; walkPos[(size_t) ti] = 0; }
                            const int p = walkPos[(size_t) ti]; int idx = 0;
                            switch (src) { case 7: idx = p % nh; break; case 8: idx = nh - 1 - (p % nh); break; case 10: idx = rng.nextInt (nh); break;
                                           case 11: { // random steps: to a neighbour of the note before (never the same note twice), turning round at the ends
                                                      const int cur = juce::jlimit (0, nh - 1, p);
                                                      idx = nh < 2 ? 0 : cur <= 0 ? 1 : cur >= nh - 1 ? nh - 2 : (rng.nextBool() ? cur + 1 : cur - 1); break; }
                                           default: { const int period = juce::jmax (1, 2 * nh - 2); const int q = p % period; idx = q < nh ? q : period - q; } }
                            own = notesHeld[juce::jlimit (0, nh - 1, idx)];
                            walkPos[(size_t) ti] = src == 11 ? idx : p + 1;   // the steps walk remembers where it is, the others how far they have gone
                        }
                    }
                }
                const int base = patch.scaleOn ? stepScale (quantise (own, patch.scale, patch.root), pitchOff, patch.scale, patch.root) : juce::jlimit (0, 127, own + pitchOff);
                for (int c = 0; c < (silent ? 0 : chord); ++c)
                {
                    static const int chromatic[kMaxChord] = { 0, 4, 7, 10, 14, 17 };
                        const int note = patch.scaleOn ? stepScale (base, 2 * c, patch.scale, patch.root) : juce::jlimit (0, 127, base + chromatic[c]);
                    bool dup = false; for (int k = 0; k < nn; ++k) dup |= notes[k] == note; if (! dup) notes[nn++] = note;
                }
                for (int k = 0; k < r; ++k)
                {   // the hit and its ratchet repeats; a repeat that starts after this block waits in the queue
                    const juce::int64 onAbs = sampleClock + juce::jmax ((juce::int64) 0, (juce::int64) std::floor ((at + k * sub - ppqStart) * spb));   // a step a hair before ppqStart (see `from`) plays on the block's first sample
                    const bool gl = mono && k == 0 && t.glide[(size_t) step];   // a ratchet's repeats start their notes afresh
                    auto& list = onAbs >= blockEnd ? ons : hits;
                    for (int m = 0; m < nn; ++m) if (list.size() < list.capacity()) list.push_back ({ onAbs, notes[m], vel, gate, ti, gl });
                }
            }
        }
        if (mono) playMono (sampleRate, out);
        else { sortInPlace (hits, [] (const On& a, const On& b) { return a.at < b.at; }); for (auto& h : hits) noteOn (h.note, h.vel, h.at, h.gate, out); }
        hits.clear();
        // the notes that end inside this block - a short gate or a ratchet that began in it too: each ends at its own time, not at the next block
        for (size_t i = 0; i < offs.size();)
        {
            if (offs[i].at < blockEnd) { out.push_back ({ offsetOf (offs[i].at), juce::MidiMessage::noteOff (offs[i].channel, offs[i].note) }); offs.erase (offs.begin() + (long) i); }
            else ++i;
        }
        sortInPlace (out, [] (const Event& a, const Event& b) { return a.sampleOffset < b.sampleOffset; });
        sampleClock = blockEnd; lastEnd = ppqEnd; lastEndValid = true;
    }
    /** Everything off, now (the page's panic, or the plugin going away). */
    void allOff (std::vector<Event>& out) { out.clear(); for (auto& o : offs) out.push_back ({ 0, juce::MidiMessage::noteOff (o.channel, o.note) }); offs.clear(); ons.clear(); hits.clear(); for (auto& s : uiStep) s = -1; lastEndValid = false; releaseGlide (out); releaseLocks(); }
    /** The synth's parameter locks: called at every step of an audible track for each synth lane with (parameter index, -1..1);
        0 means "back to the program's value". Called from the audio thread. */
    std::function<void (int param, float offset)> onParamLock;
    /** The chord played into the panel, sorted low to high: fills `out`, returns how many. Called from the audio thread. */
    std::function<int (int* out, int maxNotes)> getChord;
    void releaseLocks() { if (onParamLock) for (auto& q : touched) onParamLock (q.param, 0.f); touched.clear(); }
    void seed (juce::int64 s) { rng.setSeed (s); }   // the dice (CHANCE, random lanes, random walks) from a known start: for the tests

    // ---- for the display
    std::array<std::atomic<int>, kTracks> uiStep { { -1, -1, -1, -1 } };   // the step each track is on (-1 = stopped)
    std::array<std::atomic<juce::int64>, kTracks> uiN { { 0, 0, 0, 0 } };   // the absolute step count (the lanes derive their position from it)
    std::array<std::array<std::atomic<int>, kMaxLanes>, kTracks> uiLanePos {};   // per track and lane slot: the lane step last used
    std::atomic<unsigned> uiFlash { 0 };                                     // a bit per track that played since the display last looked
    int pendingNotes() const { return (int) offs.size(); }
    int lockedParams() const { return (int) touched.size(); }               // parameters a lane holds away from the program's value right now

private:
    struct Off { juce::int64 at; int channel, note; juce::int64 on; };   // a sounding note: when it ends, and when it began
    struct On  { juce::int64 at; int note, vel; juce::int64 gate; int track = 0; bool glide = false; };
    struct Lock { int param, track; };                                   // a pushed parameter and the track whose lane pushed it last
    std::vector<Off> offs; std::vector<On> ons, hits;   // offs: one entry per sounding key (poly). ons: repeats waiting for a later block. hits: this block's, before they are played
    int glideState = -1; bool lastGlideBase = false;     // the glide switch as the page last set it (-1: not touched)
    juce::int64 sampleClock = 0; bool wasRunning = false;
    double lastEnd = 0.0; bool lastEndValid = false;   // where the block before ended, in beats (only while running on)
    juce::Random rng; std::vector<Lock> touched;   // synth parameters a lane has moved (to put back on stop, or when the lane's track falls silent)
    std::array<int, kTracks> walkPos {}; std::array<unsigned, kTracks> walkChord {};   // the chord walk: where each track is, and which chord it was walking
    void touch (int p, int track) { for (auto& q : touched) if (q.param == p) { q.track = track; return; } touched.push_back ({ p, track }); }
    /** The track's own lock on p is let go: the parameter goes back to the program's value (a lock another track holds stays). */
    void unlock (int p, int track) { for (size_t i = 0; i < touched.size(); ++i) if (touched[i].param == p && touched[i].track == track) { touched.erase (touched.begin() + (long) i); onParamLock (p, 0.f); return; } }
    void unlockOrphans (const Track& t, int track)
    {
        for (size_t i = 0; i < touched.size();)
        {
            bool has = touched[i].track != track; if (! has) for (auto& l : t.lanes) has |= l.target == kSynth + touched[i].param;
            if (has) ++i; else { const int p = touched[i].param; touched.erase (touched.begin() + (long) i); onParamLock (p, 0.f); }
        }
    }
    int offsetOf (juce::int64 abs) const { return (int) juce::jlimit ((juce::int64) 0, (juce::int64) 0x7fffffff, abs - sampleClock); }
    /** A stable sort in place - an insertion sort: the lists are short and nearly in order, and it takes no memory
        (std::stable_sort asks for a buffer, which the audio thread must not do). */
    template <typename T, typename Less> static void sortInPlace (std::vector<T>& v, Less less)
    {
        for (size_t i = 1; i < v.size(); ++i)
        {
            if (! less (v[i], v[i - 1])) continue;
            T x = std::move (v[i]); size_t j = i; while (j > 0 && less (x, v[j - 1])) { v[j] = std::move (v[j - 1]); --j; } v[j] = std::move (x);
        }
    }
    /** The glide switch back to the panel's setting (on stop / panic), if the page changed it. */
    void releaseGlide (std::vector<Event>& out)
    {
        if (glideCC >= 0 && glideState >= 0 && glideState != (glideBase ? 1 : 0)) out.push_back ({ 0, juce::MidiMessage::controllerEvent (channel, glideCC, glideBase ? 127 : 0) });
        glideState = -1;
    }
    /** Mono: of the notes that start together (within 3 ms) the upper track's wins; then they play in time order, one voice. */
    void playMono (double sampleRate, std::vector<Event>& out)
    {
        if (hits.empty()) return;
        sortInPlace (hits, [] (const On& a, const On& b) { return a.at != b.at ? a.at < b.at : a.track < b.track; });
        const juce::int64 tol = (juce::int64) (sampleRate * 0.003);
        for (size_t i = 0; i < hits.size();)
        {
            size_t best = i, j = i + 1;
            while (j < hits.size() && hits[j].at - hits[i].at <= tol) { if (hits[j].track < hits[best].track) best = j; ++j; }
            monoNoteOn (hits[best], out); i = j;
        }
    }
    void monoNoteOn (const On& o, std::vector<Event>& out)
    {
        const int onOffset = offsetOf (o.at);
        if (glideCC >= 0)
        {   // the glide switch for this note: on for a GLIDE step, else the panel's own setting (sent only when it changes)
            if (glideBase != lastGlideBase) { lastGlideBase = glideBase; glideState = -1; }   // the panel's switch moved: it sent its own value
            const int want = (o.glide || glideBase) ? 1 : 0;
            const bool send = glideState < 0 ? (o.glide && ! glideBase) : want != glideState;   // untouched: only a glide step needs it
            if (send) { out.push_back ({ onOffset, juce::MidiMessage::controllerEvent (channel, glideCC, want ? 127 : 0) }); glideState = want; }
        }
        // what still sounds at this moment: the notes whose off lies at or after it (a fixed list: nothing allocates here); a note that
        // ended before it keeps its own off, which goes out with the block's other offs
        int sounding[kMaxChord * 2]; int ns = 0;
        auto remember = [&] (int n) { for (int k = 0; k < ns; ++k) if (sounding[k] == n) return; if (ns < kMaxChord * 2) sounding[ns++] = n; };
        for (size_t i = 0; i < offs.size();) { if (offs[i].at >= o.at) { remember (offs[i].note); offs.erase (offs.begin() + (long) i); } else ++i; }
        if (o.glide && ns > 0)
        {   // legato: the new note first, then the old one lets go (the same note again: it is simply held on)
            bool tie = false; for (int k = 0; k < ns; ++k) tie |= sounding[k] == o.note;
            if (! tie) out.push_back ({ onOffset, juce::MidiMessage::noteOn (channel, o.note, (juce::uint8) o.vel) });
            for (int k = 0; k < ns; ++k) if (sounding[k] != o.note) out.push_back ({ onOffset, juce::MidiMessage::noteOff (channel, sounding[k]) });
        }
        else
        {   // a fresh note: the old one ends just before it
            for (int k = 0; k < ns; ++k) out.push_back ({ juce::jmax (0, onOffset - 1), juce::MidiMessage::noteOff (channel, sounding[k]) });
            out.push_back ({ onOffset, juce::MidiMessage::noteOn (channel, o.note, (juce::uint8) o.vel) });
        }
        offs.push_back ({ o.at + o.gate, channel, o.note, o.at });
    }
    /** One note-on, called in time order (poly). Every key has at most one entry in offs, so every note-on is followed by exactly one note-off. */
    void noteOn (int note, int vel, juce::int64 onAbs, juce::int64 gate, std::vector<Event>& out)
    {
        const int onOffset = offsetOf (onAbs);
        for (size_t i = 0; i < offs.size(); ++i)
        {
            auto& o = offs[i]; if (o.note != note || o.channel != channel) continue;
            if (o.on >= onAbs) { o.at = juce::jmax (o.at, onAbs + gate); return; }   // the same key at the same moment (two tracks on one note): one note, as long as the longer of the two
            // the key is still sounding (a long gate, or another track) or ends just now: its off first, so the synth retriggers
            out.push_back ({ o.at <= onAbs ? offsetOf (o.at) : juce::jmax (0, onOffset - 1), juce::MidiMessage::noteOff (channel, note) }); offs.erase (offs.begin() + (long) i); break;
        }
        out.push_back ({ onOffset, juce::MidiMessage::noteOn (channel, note, (juce::uint8) vel) });
        offs.push_back ({ onAbs + gate, channel, note, onAbs });
    }
};
} // namespace Euclid
