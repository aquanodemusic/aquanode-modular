#include "PolSweepPatch.h"
#include "../Engine/PluginProcessor.h"

namespace aquanode::presets
{
namespace
{
    constexpr float kBpm = 84.1f;

    const int kVoicings[2][9] = { { 54, 56, 59, 61, 68, 70, 71, 75, 78 },    // F#3 G#3 B3 C#4 | G#4 A#4 B4 D#5 F#5
                                  { 54, 56, 59, 61, 64, 68, 71, 73, 76 } };  // F#3 G#3 B3 C#4 E4 G#4 B4 C#5 E5

    // the stabs: start and length in beats, velocity (how far the stab opens
    // its filter), voicing (0: F#3 G#3 B3 C#4 | G#4 A#4 B4 D#5 F#5, 1: with E)
    struct Stab { double start, length; float velocity; int voicing; };
    const Stab kStabs[] = {
        { 0.2500, 1.6100, 0.595, 0 },
        { 2.0000, 0.6900, 0.627, 0 },
        { 2.7500, 0.6900, 0.000, 0 },
        { 3.5000, 0.4600, 0.000, 0 },
        { 4.0000, 0.9200, 0.364, 0 },
        { 5.0000, 0.6900, 0.442, 1 },
        { 5.7500, 0.6900, 1.000, 0 },
        { 6.5000, 0.6900, 0.472, 0 },
        { 7.2500, 0.6900, 0.000, 0 },
        { 8.0000, 0.4600, 0.000, 0 },
        { 8.5000, 1.3800, 0.172, 0 },
        { 10.0000, 1.3800, 0.577, 0 },
        { 11.5000, 1.8400, 0.009, 0 },
        { 13.5000, 0.6900, 0.482, 0 },
        { 14.2500, 0.9200, 0.846, 0 },
        { 15.2500, 0.6900, 0.576, 0 },
        { 16.0000, 0.4600, 0.034, 0 },
        { 16.5000, 1.8400, 0.396, 0 },
        { 18.5000, 0.6900, 0.000, 0 },
        { 19.2500, 0.4600, 0.000, 0 },
        { 19.7500, 2.3000, 0.000, 0 },
        { 22.2500, 1.3800, 0.155, 0 },
        { 23.7500, 1.3800, 0.383, 1 },
        { 25.2500, 1.1500, 0.216, 0 },
        { 26.5000, 0.6900, 0.000, 0 },
        { 27.2500, 0.4600, 0.000, 0 },
        { 27.7500, 1.6100, 0.510, 1 },
        { 29.5000, 0.6900, 0.448, 0 },
        { 30.2500, 0.6900, 0.248, 0 },
        { 31.0000, 0.6900, 0.170, 0 },
        { 31.7500, 0.4600, 0.000, 0 },
    };

    // the drums: start in beats, velocity
    struct Hit { double start; float velocity; };
    const Hit kKicks[] = {
        { 0.2500, 0.864 }, { 1.7500, 1.000 }, { 2.0000, 0.831 }, { 5.0000, 0.945 }, { 6.5000, 0.815 }, { 7.5000, 0.923 },
        { 8.2500, 0.980 }, { 9.7500, 0.985 }, { 13.0000, 0.951 }, { 15.0000, 0.788 }, { 15.5000, 0.980 }, { 16.2500, 0.971 },
        { 17.5000, 0.996 }, { 19.2500, 0.832 }, { 20.7500, 0.911 }, { 21.2500, 0.733 }, { 22.7500, 0.690 }, { 23.2500, 0.983 },
        { 24.0000, 0.895 }, { 25.5000, 0.990 }, { 27.7500, 0.748 }, { 28.7500, 0.932 }, { 29.2500, 0.711 }, { 31.2500, 0.961 },
    };
    const Hit kClicks[] = {
        { 0.7500, 0.630 }, { 1.0000, 0.938 }, { 2.7500, 0.921 }, { 4.8750, 0.619 }, { 5.5000, 0.905 }, { 6.3750, 0.657 },
        { 7.7500, 0.859 }, { 9.0000, 0.949 }, { 10.0000, 0.605 }, { 10.7500, 0.801 }, { 12.0000, 0.801 }, { 12.7500, 0.695 },
        { 13.5000, 0.949 }, { 13.8750, 0.607 }, { 14.3750, 0.611 }, { 14.7500, 0.623 }, { 15.5000, 0.748 }, { 15.7500, 0.874 },
        { 16.7500, 1.000 }, { 18.0000, 0.609 }, { 18.5000, 1.000 }, { 20.7500, 0.610 }, { 21.2500, 1.000 }, { 23.3750, 0.739 },
        { 23.5000, 1.000 }, { 24.7500, 0.972 }, { 26.5000, 0.878 }, { 26.7500, 0.618 }, { 29.2500, 0.986 }, { 30.5000, 0.640 },
        { 31.3750, 0.747 }, { 31.5000, 0.885 },
    };
    const Hit kHiss[] = {
        { 4.2500, 0.942 }, { 7.2500, 0.875 }, { 7.5000, 0.859 }, { 7.6250, 0.680 }, { 12.1250, 0.692 }, { 12.2500, 0.923 },
        { 15.2500, 0.999 }, { 15.3750, 0.931 }, { 20.1250, 0.862 }, { 23.0000, 1.000 }, { 28.0000, 0.941 }, { 31.0000, 0.914 },
        { 31.2500, 0.860 },
    };

    template <size_t N>
    juce::String hitsState (const Hit (&hits)[N], int pitch, double length)
    {
        juce::String s ("PR1");
        for (const auto& h : hits)
            s << ' ' << juce::String (h.start, 4) << ':' << juce::String (length, 4) << ':' << pitch << ':' << juce::String (h.velocity, 3);
        return s;
    }

    int roll (PatchSketch& s, int column, const juce::String& state)
    {
        const int pr = s.add ("util.pianoroll", column);
        s.setChoice (pr, "sync", "Own Tempo");
        s.set (pr, "tempo", kBpm);
        s.set (pr, "bars", 8.0f);
        s.setChoice (pr, "loop", "Loop");
        if (auto* d = s.dsp (pr))
            d->loadCustomState (state);
        s.set (pr, "playing", 1.0f);
        return pr;
    }
}

juce::StringArray polSweepPatterns()
{
    juce::String chords ("PR1");
    for (const auto& st : kStabs)
        for (int p : kVoicings[st.voicing])
            chords << ' ' << juce::String (st.start, 4) << ':' << juce::String (st.length, 4) << ':' << p
                   << ':' << juce::String (juce::jmax (0.03f, st.velocity), 3);
    return { chords, hitsState (kKicks, 36, 0.25), hitsState (kClicks, 84, 0.1), hitsState (kHiss, 84, 0.1) };
}

juce::String buildPolSweepPatchWith (AquanodeModularAudioProcessor& p, const juce::StringArray& patterns)
{
    PatchSketch s (p);

    // ---- the chord ----------------------------------------------------------------
    const int chords = roll (s, 0, patterns[0]);

    const int amp = s.add ("util.adsr", 0);
    s.set (amp, "attack", 4.0f);
    s.set (amp, "decay", 600.0f);
    s.set (amp, "sustain", 0.6f);
    s.set (amp, "release", 90.0f);

    const int osc = s.add ("osc.warp", 1);
    s.set (osc, "shape", 50.0f);        // saw
    s.set (osc, "unison", 1.0f);
    s.set (osc, "volume", 0.9f);
    s.setChoice (osc, "morphType", "None");
    s.wire (chords, "midiOut", osc, "addMidiIn");
    s.wire (amp, "modOut", osc, "envIn");

    // the chord's body: a soft triangle layer under the saws, through the same filter
    const int body = s.add ("osc.warp", 1);
    s.set (body, "shape", 25.0f);       // triangle
    s.set (body, "unison", 1.0f);
    s.set (body, "volume", 0.9f);
    s.setChoice (body, "morphType", "None");
    s.wire (chords, "midiOut", body, "addMidiIn");
    s.wire (amp, "modOut", body, "envIn");

    // each stab pushes its band-pass up as far as its velocity says, and holds
    // it there for the stab (the velocities follow how bright each stab of the
    // snippet is); a smooth Random LFO sweeps the centre all the while
    const int open = s.add ("util.curveenv", 0);
    s.set (open, "attack", 30.0f);
    s.set (open, "hold", 0.0f);
    s.set (open, "decay", 700.0f);
    s.set (open, "sustain", 65.0f);
    s.set (open, "release", 150.0f);
    s.set (open, "dCurve", -2.0f);
    s.set (open, "amount", 100.0f);
    s.set (open, "velocity", 100.0f);

    const int sweep = s.add ("util.randomlfo", 0);
    s.setChoice (sweep, "mode", "Smooth");
    s.set (sweep, "rate", 0.7f);
    s.setChoice (sweep, "retrig", "Free");
    s.setChoice (sweep, "polarity", "Bipolar");
    s.set (sweep, "amount", 45.0f);

    const int voiceFilter = s.add ("filter.svf", 2);
    s.setChoice (voiceFilter, "mode", "Bandpass");
    s.set (voiceFilter, "cutoff", 220.0f);
    s.set (voiceFilter, "resonance", 0.12f);
    s.set (voiceFilter, "modDepth", 70.0f);
    s.wire (osc, "audioOut", voiceFilter, "audioIn");
    s.wire (body, "audioOut", voiceFilter, "audioIn");
    s.wire (open, "envOut", voiceFilter, "cutoffMod");
    s.wire (sweep, "modOut", voiceFilter, "cutoffMod");

    const int sum = s.add ("util.polysum", 2);
    s.set (sum, "amount", 0.3f);
    s.wire (voiceFilter, "audioOut", sum, "audioIn");

    // one filter for the whole chord, swept by two free-running LFOs
    const int bar = s.add ("util.skewlfo", 2);
    s.setChoice (bar, "sync", "Free");
    s.set (bar, "rate", kBpm / 60.0f / 4.0f);   // a bar long
    s.set (bar, "waveform", 7.0f);              // a slow rise, a quicker fall
    s.set (bar, "level", 0.5f);

    const int drift = s.add ("util.randomlfo", 2);
    s.setChoice (drift, "mode", "Smooth");
    s.set (drift, "rate", 0.45f);
    s.setChoice (drift, "retrig", "Free");
    s.setChoice (drift, "polarity", "Bipolar");
    s.set (drift, "amount", 50.0f);

    const int ladder = s.add ("filter.ladder", 3);
    s.setChoice (ladder, "mode", "24dB LP");
    s.set (ladder, "cutoff", 2600.0f);
    s.set (ladder, "resonance", 0.45f);
    s.set (ladder, "drive", 1.2f);
    s.set (ladder, "modDepth", 45.0f);
    s.set (ladder, "outputVolume", 1.0f);
    s.wire (sum, "audioOut", ladder, "audioIn");
    s.wire (bar, "triOut", ladder, "cutoffMod");
    s.wire (drift, "modOut", ladder, "cutoffMod");

    const int chorus = s.add ("fx.chorus", 4);
    s.set (chorus, "rate", 0.35f);
    s.set (chorus, "depth", 30.0f);
    s.set (chorus, "dryWet", 22.0f);
    s.wire (ladder, "audioOut", chorus, "audioIn");

    const int hall = s.add ("fx.smoothhall", 4);
    s.set (hall, "time", 2.4f);
    s.set (hall, "preDelay", 18.0f);
    s.set (hall, "highDamp", 6500.0f);
    s.set (hall, "dryWet", 20.0f);
    s.wire (chorus, "audioOut", hall, "audioIn");

    // ---- the kick -----------------------------------------------------------------
    const int kicks = roll (s, 0, patterns[1]);
    const int kick = s.add ("osc.kick", 1);
    s.set (kick, "tune", 61.0f);         // ~B1
    s.set (kick, "drop", 14.0f);
    s.set (kick, "pitchDecay", 14.0f);
    s.set (kick, "decay", 50.0f);        // (a time constant: -8.7 dB every 50 ms)
    s.set (kick, "click", 35.0f);
    s.set (kick, "drive", 15.0f);
    s.set (kick, "voices", 1.0f);
    s.wire (kicks, "midiOut", kick, "midiIn");
    const int kickLevel = s.add ("util.volume", 2);
    s.set (kickLevel, "level", 0.6f);
    s.wire (kick, "audioOut", kickLevel, "audioIn");

    // ---- the ticks: clicks and hiss bursts -----------------------------------------
    const int clicks = roll (s, 0, patterns[2]);
    const int click = s.add ("osc.hats", 1);
    s.set (click, "tune", 500.0f);
    s.set (click, "decay", 10.0f);
    s.set (click, "tone", 100.0f);
    s.set (click, "metal", 40.0f);
    s.set (click, "voices", 2.0f);
    s.wire (clicks, "midiOut", click, "midiIn");

    const int hiss = roll (s, 0, patterns[3]);
    const int sizzle = s.add ("osc.hats", 1);
    s.set (sizzle, "tune", 470.0f);
    s.set (sizzle, "decay", 200.0f);
    s.set (sizzle, "tone", 100.0f);
    s.set (sizzle, "metal", 15.0f);
    s.set (sizzle, "voices", 2.0f);
    s.wire (hiss, "midiOut", sizzle, "midiIn");

    const int hp = s.add ("filter.highpass", 3);
    s.set (hp, "cutoff", 6500.0f);
    s.setChoice (hp, "slope", "24 dB/oct");
    s.wire (click, "audioOut", hp, "audioIn");
    s.wire (sizzle, "audioOut", hp, "audioIn");
    const int tickLevel = s.add ("util.volume", 4);
    s.set (tickLevel, "level", 1.5f);
    s.wire (hp, "audioOut", tickLevel, "audioIn");

    const int limiter = s.add ("fx.limiter", 5);
    s.set (limiter, "ceiling", -0.3f);
    s.set (limiter, "release", 60.0f);
    s.wire (hall, "audioOut", limiter, "audioIn");
    s.wire (kickLevel, "audioOut", limiter, "audioIn");
    s.wire (tickLevel, "audioOut", limiter, "audioIn");

    const int out = s.add ("io.audioOut", 5);
    s.set (out, "level", 1.0f);
    s.wire (limiter, "audioOut", out, "audioIn");

    s.layout (1200);
    return "Pol Sweep";
}

juce::String buildPolSweepPatch (AquanodeModularAudioProcessor& p, juce::uint32)
{
    return buildPolSweepPatchWith (p, polSweepPatterns());
}

} // namespace aquanode::presets
