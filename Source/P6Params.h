#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

/** The Prophet-6 program parameters, straight from Appendix C of the operation manual (v2.1).
    NRPN number == byte offset inside the 1024-byte program data, which is what makes one table serve
    the knobs, the NRPN stream and the SysEx dumps alike. Sequencer steps (256-1023) are kept as raw
    bytes and not exposed as parameters. */
namespace P6
{
enum class Kind { Knob, Toggle, Choice };

struct Param
{
    int nrpn; const char* id; const char* name; int min; int max; Kind kind;
    std::vector<const char*> choices {};
    const char* suffix = "";
    int dump = -1;      // byte offset in the program dump (-1 = not yet known; learned from the synth, see LEARN)
};

/** LFO FREQUENCY while SYNC is on: the knob's 0-254 is 18 zones of 14 values, each a note value (one LFO cycle), measured on the
    synth at 120 BPM (Nov 2026: a MIDI sweep of the knob, one value per bar, random shape on the amp - Sequential's manual has no table).
    The lowest zone was too slow to measure in its 14 bars: longer than 4 bars. */
inline const char* lfoSyncName (int v)
{
    static const char* names[] = { "> 4 bars", "4 bars", "2 bars", "1.5 bars", "1 bar", "1/2 D", "1/2", "1/4 D", "1/4", "1/4 T", "1/8 D", "1/8", "1/8 T", "1/16 D", "1/16", "1/16 T", "1/32", "1/32 T" };
    return names[v < 0 ? 0 : v / 14 > 17 ? 17 : v / 14];
}
inline const std::vector<Param>& params()
{
    static const std::vector<Param> t {
        // ---- oscillator 1
        { 0,   "osc1Freq",   "Osc 1 Freq",        0, 60,  Kind::Knob, {}, " st", 0 },
        { 1,   "osc1Sync",   "Osc 1 Sync",        0, 1,   Kind::Toggle, {}, "", 11 },
        { 3,   "osc1Shape",  "Osc 1 Shape",       0, 254, Kind::Knob, {}, "", 3 },
        { 4,   "osc1PW",     "Osc 1 Pulse Width", 0, 255, Kind::Knob, {}, "", 5 },
        // ---- oscillator 2
        { 5,   "osc2Freq",   "Osc 2 Freq",        0, 60,  Kind::Knob, {}, " st", 1 },
        { 6,   "osc2Fine",   "Osc 2 Fine",        0, 254, Kind::Knob, {}, "", 2 },
        { 8,   "osc2Shape",  "Osc 2 Shape",       0, 254, Kind::Knob, {}, "", 4 },
        { 9,   "osc2PW",     "Osc 2 Pulse Width", 0, 255, Kind::Knob, {}, "", 6 },
        { 10,  "osc2Low",    "Osc 2 Low Freq",    0, 1,   Kind::Toggle, {}, "", 13 },
        { 11,  "osc2Key",    "Osc 2 Keyboard",    0, 1,   Kind::Toggle, {}, "", 12 },
        // ---- mixer
        { 2,   "osc1Level",  "Osc 1 Level",       0, 127, Kind::Knob, {}, "", 7 },
        { 27,  "subLevel",   "Sub Osc Level",     0, 127, Kind::Knob, {}, "", 9 },
        { 7,   "osc2Level",  "Osc 2 Level",       0, 127, Kind::Knob, {}, "", 8 },
        { 32,  "noise",      "Noise Level",       0, 127, Kind::Knob, {}, "", 10 },
        // ---- glide / pitch
        { 29,  "glideOn",    "Glide On",          0, 1,   Kind::Toggle, {}, "", 16 },
        { 30,  "glideRate",  "Glide Rate",        0, 127, Kind::Knob, {}, "", 14 },
        { 28,  "glideMode",  "Glide Mode",        0, 3,   Kind::Choice, { "Fixed Rate", "Fixed Rate A", "Fixed Time", "Fixed Time A" }, "", 15 },
        { 31,  "pbRange",    "Pitch Bend Range",  0, 24,  Kind::Knob, {}, " st", 17 },
        { 33,  "slop",       "Slop",              0, 127, Kind::Knob, {}, "", 18 },
        // ---- low-pass filter
        { 45,  "lpFreq",     "Low-pass Freq",     0, 164, Kind::Knob, {}, "", 19 },
        { 46,  "lpRes",      "Low-pass Res",      0, 255, Kind::Knob, {}, "", 20 },
        { 77,  "lpEnvAmt",   "Low-pass Env Amt",  0, 254, Kind::Knob, {}, "", 29 },
        { 47,  "lpKey",      "Low-pass Keyboard", 0, 2,   Kind::Choice, { "Off", "Half", "Full" }, "", 21 },
        { 48,  "lpVel",      "Low-pass Velocity", 0, 1,   Kind::Toggle, {}, "", 22 },
        // ---- high-pass filter
        { 49,  "hpFreq",     "High-pass Freq",    0, 164, Kind::Knob, {}, "", 23 },
        { 50,  "hpRes",      "High-pass Res",     0, 255, Kind::Knob, {}, "", 24 },
        { 82,  "hpEnvAmt",   "High-pass Env Amt", 0, 254, Kind::Knob, {}, "", 30 },
        { 51,  "hpKey",      "High-pass Keyboard",0, 2,   Kind::Choice, { "Off", "Half", "Full" }, "", 25 },
        { 52,  "hpVel",      "High-pass Velocity",0, 1,   Kind::Toggle, {}, "", 26 },
        // ---- filter envelope
        { 78,  "fAtk",       "Filter Attack",     0, 127, Kind::Knob, {}, "", 35 },
        { 79,  "fDec",       "Filter Decay",      0, 127, Kind::Knob, {}, "", 37 },
        { 80,  "fSus",       "Filter Sustain",    0, 127, Kind::Knob, {}, "", 39 },
        { 81,  "fRel",       "Filter Release",    0, 127, Kind::Knob, {}, "", 41 },
        // ---- amplifier envelope
        { 66,  "vcaEnvAmt",  "VCA Env Amt",       0, 127, Kind::Knob, {}, "", 31 },
        { 67,  "aAtk",       "Amp Attack",        0, 127, Kind::Knob, {}, "", 36 },
        { 68,  "aDec",       "Amp Decay",         0, 127, Kind::Knob, {}, "", 38 },
        { 69,  "aSus",       "Amp Sustain",       0, 127, Kind::Knob, {}, "", 40 },
        { 70,  "aRel",       "Amp Release",       0, 127, Kind::Knob, {}, "", 42 },
        { 71,  "vcaVel",     "VCA Velocity",      0, 1,   Kind::Toggle, {}, "", 43 },
        // ---- LFO
        { 88,  "lfoFreq",    "LFO Freq",          0, 254, Kind::Knob, {}, "", 59 },
        { 89,  "lfoAmt",     "LFO Initial Amt",   0, 255, Kind::Knob, {}, "", 63 },
        { 90,  "lfoShape",   "LFO Shape",         0, 4,   Kind::Choice, { "Triangle", "Sawtooth", "Rev Sawtooth", "Square", "Random" }, "", 60 },
        { 91,  "lfoSync",    "LFO Sync",          0, 1,   Kind::Toggle, {}, "", 61 },
        { 93,  "lfoFreq1",   "LFO > Freq 1",      0, 1,   Kind::Toggle, {}, "", 64 },
        { 94,  "lfoFreq2",   "LFO > Freq 2",      0, 1,   Kind::Toggle, {}, "", 65 },
        { 95,  "lfoPW",      "LFO > PW 1+2",      0, 1,   Kind::Toggle, {}, "", 66 },
        { 96,  "lfoAmp",     "LFO > Amp",         0, 1,   Kind::Toggle, {}, "", 67 },
        { 97,  "lfoLP",      "LFO > Low-pass",    0, 1,   Kind::Toggle, {}, "", 68 },
        { 98,  "lfoHP",      "LFO > High-pass",   0, 1,   Kind::Toggle, {}, "", 69 },
        // ---- poly mod
        { 143, "pmFiltEnv",  "PolyMod Filt Env",  0, 254, Kind::Knob, {}, "", 77 },
        { 144, "pmOsc2",     "PolyMod Osc 2",     0, 254, Kind::Knob, {}, "", 78 },
        { 145, "pmFreq1",    "PolyMod > Freq 1",  0, 1,   Kind::Toggle, {}, "", 79 },
        { 146, "pmShape1",   "PolyMod > Shape 1", 0, 1,   Kind::Toggle, {}, "", 80 },
        { 147, "pmPW1",      "PolyMod > PW 1",    0, 1,   Kind::Toggle, {}, "", 81 },
        { 148, "pmLP",       "PolyMod > Low-pass",0, 1,   Kind::Toggle, {}, "", 82 },
        { 149, "pmHP",       "PolyMod > High-pass",0, 1,  Kind::Toggle, {}, "", 83 },
        // ---- aftertouch
        { 109, "atAmt",      "Aftertouch Amt",    0, 254, Kind::Knob, {}, "", 70 },
        { 110, "atFreq1",    "Aftertouch > Freq 1",0, 1,  Kind::Toggle, {}, "", 71 },
        { 111, "atFreq2",    "Aftertouch > Freq 2",0, 1,  Kind::Toggle, {}, "", 72 },
        { 112, "atLP",       "Aftertouch > Low-pass",0, 1, Kind::Toggle, {}, "", 73 },
        { 113, "atHP",       "Aftertouch > High-pass",0, 1, Kind::Toggle, {}, "", 74 },
        { 114, "atVCA",      "Aftertouch > VCA",  0, 1,   Kind::Toggle, {}, "", 75 },
        { 115, "atLFO",      "Aftertouch > LFO Amt",0, 1, Kind::Toggle, {}, "", 76 },
        // ---- effects
        { 135, "fxOn",       "Effects On",        0, 1,   Kind::Toggle, {}, "", 46 },
        { 119, "fx1Type",    "Effect A Type",     0, 9,   Kind::Choice, { "Off", "BBD Delay", "Digital Delay", "Chorus", "Phaser 1", "Phaser 2", "Phaser 3", "Ring Mod", "Flanger 1", "Flanger 2" }, "", 44 },
        { 120, "fx1Mix",     "Effect A Mix",      0, 127, Kind::Knob, {}, "", 48 },
        { 121, "fx1P1",      "Effect A Param 1",  0, 255, Kind::Knob, {}, "", 50 },
        { 122, "fx1P2",      "Effect A Param 2",  0, 127, Kind::Knob, {}, "", 52 },
        { 123, "fx1Sync",    "Effect A Sync",     0, 1,   Kind::Toggle, {}, "", 54 },
        { 124, "fx1Time",    "Effect A Sync Time",0, 10,  Kind::Choice, { "16", "16d", "8t", "8", "8d", "4t", "4", "4d", "2", "2d", "1" }, "", 56 },   // undocumented: delay time when Sync is on
        { 127, "fx2Type",    "Effect B Type",     0, 13,  Kind::Choice, { "Off", "BBD Delay", "Digital Delay", "Chorus", "Phaser 1", "Phaser 2", "Phaser 3", "Ring Mod", "Flanger 1", "Flanger 2", "Hall", "Room", "Plate", "Spring" }, "", 45 },
        { 128, "fx2Mix",     "Effect B Mix",      0, 127, Kind::Knob, {}, "", 49 },
        { 129, "fx2P1",      "Effect B Param 1",  0, 255, Kind::Knob, {}, "", 51 },
        { 130, "fx2P2",      "Effect B Param 2",  0, 127, Kind::Knob, {}, "", 53 },
        { 131, "fx2Sync",    "Effect B Sync",     0, 1,   Kind::Toggle, {}, "", 55 },
        { 132, "fx2Time",    "Effect B Sync Time",0, 10,  Kind::Choice, { "16", "16d", "8t", "8", "8d", "4t", "4", "4d", "2", "2d", "1" }, "", 57 },
        // ---- voice / output
        { 62,  "voiceVol",   "Voice Volume",      0, 127, Kind::Knob, {}, "", 27 },
        { 63,  "panSpread",  "Pan Spread",        0, 127, Kind::Knob, {}, "", 28 },
        { 64,  "distortion", "Distortion",        0, 127, Kind::Knob, {}, "", 58 },
        { 156, "unisonOn",   "Unison",            0, 1,   Kind::Toggle, {}, "", 84 },
        { 157, "unisonMode", "Unison Voices",     0, 6,   Kind::Choice, { "1", "2", "3", "4", "5", "6", "Chord" }, "", 85 },
        { 158, "keyMode",    "Key Mode",          0, 5,   Kind::Choice, { "Low", "High", "Last", "Low Retrig", "High Retrig", "Last Retrig" }, "", 86 },
        // ---- arpeggiator / clock
        { 160, "arpOn",      "Arp On",            0, 1,   Kind::Toggle, {}, "", 91 },
        { 161, "arpMode",    "Arp Mode",          0, 4,   Kind::Choice, { "Up", "Down", "Up + Down", "Random", "Assign" }, "", 89 },
        { 162, "arpRange",   "Arp Octaves",       0, 2,   Kind::Choice, { "1 Octave", "2 Octaves", "3 Octaves" }, "", 90 },
        { 163, "arpTime",    "Arp Value",         0, 9,   Kind::Choice, { "Half", "Quarter", "8th", "8th Dot", "8th Swing", "8th Trip", "16th", "16th Swing", "16th Trip", "32nd" }, "", 92 },
        { 167, "bpm",        "BPM",               30, 250, Kind::Knob, {}, "", 87 },
        // ---- sequencer (transport only; the steps live on their own page later)
        { 168, "seqOn",      "Seq On",            0, 1,   Kind::Toggle, {}, "", -1 },
        { 169, "seqLength",  "Seq Length",        0, 63,  Kind::Knob, {}, "", -1 },
        { 170, "seqMode",    "Seq Mode",          0, 1,   Kind::Choice, { "Normal", "Gated" }, "", -1 },
        { 171, "seqPlay",    "Seq Play Mode",     0, 1,   Kind::Choice, { "Once", "Loop" }, "", -1 },
    };
    return t;
}

// The program dump is NOT laid out by NRPN number (the manual's table describes only the NRPN side).
// Measured on a real Prophet-6: the 20-character name sits at byte 107. The parameter offsets are learned
// from the synth (LEARN mode) and kept in Documents/Prophet 6 Patches/layout.txt until baked in here.
inline constexpr int kNameOffset = 107, kNameLength = 20, kProgramBytes = 1024, kPackedBytes = 1171;

inline int indexOfNrpn (int nrpn) { auto& t = params(); for (int i = 0; i < (int) t.size(); ++i) if (t[(size_t) i].nrpn == nrpn) return i; return -1; }
inline int indexOfId (const juce::String& id) { auto& t = params(); for (int i = 0; i < (int) t.size(); ++i) if (id == t[(size_t) i].id) return i; return -1; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    for (auto& q : params())
    {
        ParameterID pid (q.id, 1);
        if (q.kind == Kind::Choice)
        {
            StringArray names; for (auto* c : q.choices) names.add (c);
            while (names.size() < q.max - q.min + 1) names.add (String (names.size()));   // safety for ranges wider than the list
            p.push_back (std::make_unique<AudioParameterChoice> (pid, q.name, names, 0));
        }
        else if (q.kind == Kind::Toggle)
            p.push_back (std::make_unique<AudioParameterBool> (pid, q.name, false));
        else
            p.push_back (std::make_unique<AudioParameterInt> (pid, q.name, q.min, q.max, q.min));
    }
    return { p.begin(), p.end() };
}
} // namespace P6
