#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "P6Params.h"

/** Extra modulation the panel adds over MIDI: two LFOs, a step sequencer, an ADSR envelope and an X/Y pad.
    Everything is applied on top of the program's knob values and sent as ordinary NRPNs, so the synth itself
    never changes - stop the modulation and the program is back as it was. */
namespace Mod
{
inline const juce::StringArray& divisions() { static const juce::StringArray d { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T", "8 bars", "16 bars", "32 bars" }; return d; }   // the long ones came later and sit at the end, so saved files keep their numbers
inline double divisionBeats (int i) { static const double b[] = { 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 32, 64, 128 }; return b[juce::jlimit (0, 13, i)]; }
inline void fillDivisions (juce::ComboBox& b) { for (int i : { 13, 12, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }) b.addItem (divisions()[i], i + 1); }   // the menu from long to short; the item ids stay index + 1
inline const juce::StringArray& shapes() { static const juce::StringArray s { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Sample & Hold" }; return s; }

struct Lfo  { bool on = false; int shape = 1; bool sync = true; float hz = 0.5f; int div = 4; float depth = 1.f; int dest[2] { -1, -1 }; float amt[2] { 0.3f, 0.3f }; };
struct Seq  { bool on = false; float step[16] {}; int length = 16; int div = 6; int dest = -1; float amt = 0.5f; };
struct Env  { bool on = false; float attackMs = 5.f, decayMs = 300.f, sustain = 0.f, releaseMs = 200.f; int dest[3] { -1, -1, -1 }; float amt[3] { 0.5f, 0.5f, 0.5f }; float curve[3] {}; };   // curve: attack, decay, release; -1 = fast at first, 0 = straight, +1 = slow at first
/** A segment's bend (Serum style): t 0..1 in, 0..1 out. c < 0 moves fast at first and slows down, c > 0 starts slowly and speeds up. The time the segment takes never changes. */
inline float envCurve (float t, float c) { t = juce::jlimit (0.f, 1.f, t); if (std::abs (c) < 0.01f) return t; const float k = 1.f + 4.f * std::abs (c); return c < 0.f ? std::pow (t, 1.f / k) : std::pow (t, k); }
/** A drawable modulator (Serum / Vital style): up to kMax breakpoints (x ascending 0..1, y 0..1), each segment bent by -1..1
    (positive bows it up / early, negative down / late). Played once per note like an envelope, looped like an LFO, or looped and restarted by each note. */
struct Shape
{
    static constexpr int kMax = 48;
    bool on = false; int mode = 0;                            // 0 one shot per note, 1 loop, 2 loop restarting on each note
    bool sync = false; float seconds = 2.f; int div = 4;      // one pass: in seconds, or a note value at the tempo
    bool bipolar = false;                                     // swing both ways around the knob's value, or push upwards only
    int dest[3] { -1, -1, -1 }; float amt[3] { 0.f, 0.f, 0.f };          // a fresh destination starts at 0 %
    int count = 2; float x[kMax] {}, y[kMax] {}, bend[kMax] {};
    Shape() { setDefault(); }
    void setDefault() { count = 2; x[0] = 0.f; y[0] = 1.f; bend[0] = 0.6f; x[1] = 1.f; y[1] = 0.f; bend[1] = 0.f; }    // a decaying ramp, falling fast at first like a natural envelope
    static float curve (float t, float b) { return std::pow (juce::jlimit (0.f, 1.f, t), std::pow (4.f, -b)); }        // b > 0 rises fast then flattens, b < 0 stays low then jumps
    float evaluate (float p) const
    {
        const int n = juce::jlimit (1, kMax, count); p = juce::jlimit (0.f, 1.f, p);
        if (n == 1 || p <= x[0]) return y[0];
        for (int i = 0; i < n - 1; ++i) if (p <= x[i + 1]) { const float w = juce::jmax (1.0e-5f, x[i + 1] - x[i]); return y[i] + (y[i + 1] - y[i]) * curve ((p - x[i]) / w, bend[i]); }
        return y[n - 1];
    }
    juce::ValueTree toTree (bool withDest = true) const
    {
        juce::ValueTree s ("shape"); s.setProperty ("on", on, nullptr); s.setProperty ("mode", mode, nullptr); s.setProperty ("sync", sync, nullptr); s.setProperty ("seconds", seconds, nullptr); s.setProperty ("div", div, nullptr); s.setProperty ("bipolar", bipolar, nullptr);
        if (withDest) for (int d = 0; d < 3; ++d) { s.setProperty ("dest" + juce::String (d), dest[d], nullptr); s.setProperty ("amt" + juce::String (d), amt[d], nullptr); }
        s.setProperty ("points", pointsToString(), nullptr); return s;
    }
    void fromTree (const juce::ValueTree& s, bool withDest = true)
    {
        if (withDest) on = s.getProperty ("on", false);
        mode = s.getProperty ("mode", 0); sync = s.getProperty ("sync", false); seconds = (float) (double) s.getProperty ("seconds", 2.0); div = s.getProperty ("div", 4); bipolar = s.getProperty ("bipolar", false);
        if (withDest) for (int d = 0; d < 3; ++d) { dest[d] = s.getProperty ("dest" + juce::String (d), -1); amt[d] = (float) (double) s.getProperty ("amt" + juce::String (d), 0.0); }
        pointsFromString (s.getProperty ("points").toString());
    }
    juce::String pointsToString() const { juce::String s; for (int i = 0; i < juce::jlimit (1, kMax, count); ++i) s << juce::String (x[i], 4) << "," << juce::String (y[i], 4) << "," << juce::String (bend[i], 3) << ";"; return s; }
    void pointsFromString (const juce::String& s)
    {
        juce::StringArray pts; pts.addTokens (s, ";", ""); pts.removeEmptyStrings(); const int n = juce::jlimit (0, kMax, pts.size()); if (n < 1) { setDefault(); return; }
        for (int i = 0; i < n; ++i) { juce::StringArray v; v.addTokens (pts[i], ",", ""); x[i] = v.size() > 0 ? v[0].getFloatValue() : 0.f; y[i] = v.size() > 1 ? v[1].getFloatValue() : 0.f; bend[i] = v.size() > 2 ? v[2].getFloatValue() : 0.f; }
        count = n;
    }
};
struct XY   { int dest[3] { -1, -1, -1 }; float amt[3] { 0.5f, 0.5f, 0.5f }; bool inv[3] {}; int gateDest = -1; bool gateInv = false; };   // X, Y (the pad) and A (a fader)

struct State
{
    Lfo lfo[2]; Seq seq; Env env; Shape shape[2]; XY xy; float bpm = 120.f;
    bool any() const { return lfo[0].on || lfo[1].on || seq.on || env.on || shape[0].on || shape[1].on || xy.dest[0] >= 0 || xy.dest[1] >= 0 || xy.dest[2] >= 0 || xy.gateDest >= 0; }
    float aLevel = 0.f;                                             // the A fader: a third, independent axis you set and leave (0..1)
    bool autoXY = false; float autoX = 0.f, autoY = 0.f;            // the X/Y pad driven by automation (not saved with the sound)
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);
    void toParams (juce::AudioProcessorValueTreeState&) const;     // keep the automatable plugin parameters in step
    void fromParams (const juce::AudioProcessorValueTreeState&);
};

/** The MOD settings Logic can automate: they live as ordinary plugin parameters as well, kept in step with State. */
struct AutoParam { const char* id; const char* name; float min, max, def; bool isBool; };
inline const std::vector<AutoParam>& autoParams()
{
    static const std::vector<AutoParam> a {
        { "modLfo1On", "MOD LFO 1 On", 0, 1, 0, true }, { "modLfo1Rate", "MOD LFO 1 Rate (Hz)", 0.02f, 20.f, 0.5f, false }, { "modLfo1Depth", "MOD LFO 1 Depth", 0, 1, 1, false },
        { "modLfo2On", "MOD LFO 2 On", 0, 1, 0, true }, { "modLfo2Rate", "MOD LFO 2 Rate (Hz)", 0.02f, 20.f, 0.5f, false }, { "modLfo2Depth", "MOD LFO 2 Depth", 0, 1, 1, false },
        { "modSeqOn", "MOD Sequencer On", 0, 1, 0, true }, { "modSeqAmt", "MOD Sequencer Amount", -1, 1, 0.5f, false },
        { "modEnvOn", "MOD Envelope On", 0, 1, 0, true }, { "modEnvAmt", "MOD Envelope Amount", -1, 1, 0.5f, false },
        { "modShape1On", "MOD Shape 1 On", 0, 1, 0, true }, { "modShape1Amt", "MOD Shape 1 Amount", -1, 1, 0.f, false },
        { "modShape2On", "MOD Shape 2 On", 0, 1, 0, true }, { "modShape2Amt", "MOD Shape 2 Amount", -1, 1, 0.f, false },
        { "modXYOn", "MOD X/Y Touch", 0, 1, 0, true }, { "modX", "MOD X", -1, 1, 0, false }, { "modY", "MOD Y", -1, 1, 0, false }, { "modA", "MOD A", 0, 1, 0, false } };
    return a;
}
inline void addAutoParams (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& p)
{
    for (auto& a : autoParams())
    {
        juce::ParameterID pid (a.id, 1);
        if (a.isBool) p.push_back (std::make_unique<juce::AudioParameterBool> (pid, a.name, a.def > 0.5f));
        else { juce::NormalisableRange<float> r (a.min, a.max, 0.001f); if (a.max > 10.f) r.setSkewForCentre (1.f); p.push_back (std::make_unique<juce::AudioParameterFloat> (pid, a.name, r, a.def)); }
    }
}
inline float autoGet (const juce::AudioProcessorValueTreeState& s, const char* id) { auto* p = s.getParameter (id); return p == nullptr ? 0.f : p->convertFrom0to1 (p->getValue()); }
inline void  autoSet (juce::AudioProcessorValueTreeState& s, const char* id, float v) { if (auto* p = s.getParameter (id)) { const float n = p->convertTo0to1 (v); if (std::abs (n - p->getValue()) > 1.0e-4f) p->setValueNotifyingHost (n); } }

} // namespace Mod
