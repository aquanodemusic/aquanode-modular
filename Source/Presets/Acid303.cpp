#include "Acid303.h"
#include "../Engine/PluginProcessor.h"
#include "../Modules/SoundGeneration/Oscillators/Acid303Module.h"

namespace aquanode::presets
{

// the 303 row as it was before the 303 module: a Warp Osc into a Ladder Filter
static juce::String buildWarpAcid (AquanodeModularAudioProcessor& p, juce::uint32 seed)
{
    PatchSketch s (p);
    Dice d (seed ^ 0x303303u);
    const bool square = d.chance (0.3f);

    // ---- modulators ---------------------------------------------------------------
    const int ampEnv = s.add ("util.adsr", 0);
    s.set (ampEnv, "attack", d.range (1.0f, 3.0f));
    s.set (ampEnv, "decay", d.range (300.0f, 1400.0f));
    s.set (ampEnv, "sustain", d.range (0.55f, 0.9f));
    s.set (ampEnv, "release", d.range (15.0f, 60.0f));

    // the filter's snap: a short decay to nothing
    const int filtEnv = s.add ("util.adsr", 0);
    s.set (filtEnv, "attack", 1.0f);
    s.set (filtEnv, "decay", d.range (110.0f, 520.0f));
    s.set (filtEnv, "sustain", d.range (0.0f, 0.12f));
    s.set (filtEnv, "release", d.range (30.0f, 120.0f));

    const int vel = s.add ("util.velocity", 0);

    // ---- the voice ----------------------------------------------------------------
    const int osc = s.add ("osc.warp", 1);
    s.set (osc, "shape", square ? 75.0f : 50.0f);
    s.set (osc, "unison", 1.0f);
    s.set (osc, "voices", 1.0f);                      // mono: one line, sliding
    s.set (osc, "glide", d.range (35.0f, 90.0f));
    s.set (osc, "volume", 0.8f);
    s.set (osc, "transpose", d.chance (0.5f) ? -12.0f : 0.0f);
    s.setChoice (osc, "morphType", "None");
    s.wire (ampEnv, "modOut", osc, "envIn");

    const int ladder = s.add ("filter.ladder", 2);
    s.setChoice (ladder, "mode", "24dB LP");
    s.set (ladder, "cutoff", d.range (180.0f, 650.0f));
    s.set (ladder, "resonance", d.range (0.72f, 0.93f));
    s.set (ladder, "drive", d.range (1.3f, 2.6f));
    s.set (ladder, "modDepth", d.range (50.0f, 85.0f));
    s.set (ladder, "outputVolume", 1.0f);
    s.wire (osc, "audioOut", ladder, "audioIn");
    s.wire (filtEnv, "modOut", ladder, "cutoffMod");
    // accent: harder notes open the filter further
    s.mod (vel, "modOut", ladder, "cutoff", d.range (0.015f, 0.035f));

    const int sum = s.add ("util.polysum", 3);
    s.set (sum, "amount", 0.0f);
    s.wire (ladder, "audioOut", sum, "audioIn");
    int chain = sum;
    auto insert = [&] (int id)
    {
        s.wire (chain, "audioOut", id, "audioIn");
        chain = id;
    };

    // ---- after the voice ------------------------------------------------------------
    if (d.chance (0.75f))
    {
        const int shaper = s.add ("fx.waveshaper", 4);
        s.set (shaper, "drive", d.range (2.0f, 9.0f));
        s.set (shaper, "out", 0.7f);
        s.set (shaper, "dryWet", d.range (45.0f, 100.0f));
        insert (shaper);
    }
    if (d.chance (0.6f))
    {
        const int dl = s.add ("fx.delay", 4);
        s.setChoice (dl, "timeL", d.chance (0.5f) ? "1/8" : "1/4");
        s.setChoice (dl, "timeR", d.chance (0.5f) ? "1/8" : "1/4");
        s.set (dl, "feedback", d.range (0.2f, 0.5f));
        s.set (dl, "highPass", d.range (250.0f, 700.0f));
        s.set (dl, "dryWet", d.range (12.0f, 30.0f));
        insert (dl);
    }
    if (d.chance (0.35f))
    {
        const int rv = s.add ("fx.reverb", 4);
        s.set (rv, "roomSize", d.range (25.0f, 55.0f));
        s.set (rv, "decay", d.range (0.6f, 1.6f));
        s.set (rv, "dryWet", d.range (8.0f, 18.0f));
        insert (rv);
    }

    // half of them play themselves: an Arp in front
    const bool arpeggiated = d.chance (0.5f);
    if (arpeggiated)
    {
        const int arp = s.add ("util.arp", 0);
        s.setChoice (arp, "division", d.chance (0.85f) ? "1/16" : "1/8");
        s.setChoice (arp, "mode", juce::StringArray { "Up", "Up/Down", "Random", "Random", "As Played" }[d.index (5)]);
        s.set (arp, "octaves", d.chance (0.75f) ? 2.0f : 3.0f);
        s.set (arp, "gate", d.range (45.0f, 95.0f));
        s.set (arp, "swing", d.chance (0.4f) ? d.range (10.0f, 30.0f) : 0.0f);
        s.wire (arp, "midiOut", osc, "addMidiIn");
    }

    const int out = s.add ("io.audioOut", 5);
    s.set (out, "level", arpeggiated ? 1.0f : 0.75f);   // short arp notes sit lower
    s.wire (chain, "audioOut", out, "audioIn");

    s.layout();
    return arpeggiated ? "303 Acid Line" : "303 Acid Bass";
}


juce::String buildAcid303 (AquanodeModularAudioProcessor& p, juce::uint32 seed)
{
    // a third each: the Warp Osc + Ladder voice, the 303 played from the keys, the 303 playing a pattern
    Dice pick (seed ^ 0x3c3c3cu);
    const int kind = pick.index (3);
    if (kind == 0)
        return buildWarpAcid (p, seed);

    PatchSketch s (p);
    Dice d (seed ^ 0x303303u);
    const bool square = d.chance (0.3f);
    const bool grid = kind == 2;

    // ---- the voice ----------------------------------------------------------------
    const int acid = s.add ("osc.acid303", 1);
    s.setChoice (acid, "wave", square ? "Square" : "Saw");
    s.setChoice (acid, "voiceMode", "Mono");
    s.set (acid, "cutoff", d.range (12.0f, 55.0f));
    s.set (acid, "resonance", d.range (55.0f, 95.0f));
    s.set (acid, "envMod", d.range (35.0f, 90.0f));
    s.set (acid, "decay", d.range (15.0f, 70.0f));
    s.set (acid, "accent", d.range (45.0f, 100.0f));
    s.set (acid, "slide", d.range (45.0f, 90.0f));
    s.set (acid, "volume", 0.0f);

    bool arpeggiated = false;
    if (grid)
    {
        s.setChoice (acid, "play", "Grid");
        const int bars = d.chance (0.55f) ? 1 : d.chance (0.6f) ? 2 : 4;
        s.set (acid, "bars", (float) bars);
        s.set (acid, "tempo", (float) juce::roundToInt (d.range (118.0f, 140.0f)));
        s.set (acid, "transpose", d.chance (0.6f) ? 0.0f : (float) juce::Array<int> { -5, -3, 2, 5, 7 }[d.index (5)]);
        if (auto* m = dynamic_cast<Acid303Module*> (s.dsp (acid)))
        {
            juce::Random rng ((juce::int64) scrambleSeed (seed ^ 0xac1dbeefu));
            m->setPattern (Acid303Pattern::hook (rng, bars, 0));
        }
        if (d.chance (0.35f)) s.set (acid, "swing", d.range (15.0f, 45.0f));
        s.set (acid, "running", 1.0f);
    }
    else
    {
        s.setChoice (acid, "play", "Free");
        s.set (acid, "accVel", d.range (90.0f, 112.0f));
        arpeggiated = d.chance (0.5f);
        if (arpeggiated)
        {
            const int arp = s.add ("util.arp", 0);
            s.setChoice (arp, "division", d.chance (0.85f) ? "1/16" : "1/8");
            s.setChoice (arp, "mode", juce::StringArray { "Up", "Up/Down", "Random", "Random", "As Played" }[d.index (5)]);
            s.set (arp, "octaves", d.chance (0.75f) ? 2.0f : 3.0f);
            s.set (arp, "gate", d.range (45.0f, 95.0f));
            s.set (arp, "swing", d.chance (0.4f) ? d.range (10.0f, 30.0f) : 0.0f);
            s.wire (arp, "midiOut", acid, "midiIn");
        }
    }

    int chain = acid;
    auto insert = [&] (int id)
    {
        s.wire (chain, "audioOut", id, "audioIn");
        chain = id;
    };

    // ---- after the voice ------------------------------------------------------------
    if (d.chance (0.7f))
    {
        const int shaper = s.add ("fx.waveshaper", 2);
        s.set (shaper, "drive", d.range (1.5f, 7.0f));
        s.set (shaper, "out", 0.7f);
        s.set (shaper, "dryWet", d.range (35.0f, 90.0f));
        insert (shaper);
    }
    if (d.chance (0.6f))
    {
        const int dl = s.add ("fx.delay", 3);
        s.setChoice (dl, "timeL", d.chance (0.5f) ? "1/8" : "1/16");
        s.setChoice (dl, "timeR", d.chance (0.5f) ? "1/8" : "1/4");
        s.set (dl, "feedback", d.range (0.2f, 0.5f));
        s.set (dl, "highPass", d.range (250.0f, 700.0f));
        s.set (dl, "dryWet", d.range (10.0f, 26.0f));
        insert (dl);
    }
    if (d.chance (0.35f))
    {
        const int rv = s.add ("fx.reverb", 3);
        s.set (rv, "roomSize", d.range (25.0f, 55.0f));
        s.set (rv, "decay", d.range (0.6f, 1.6f));
        s.set (rv, "dryWet", d.range (8.0f, 18.0f));
        insert (rv);
    }

    const int out = s.add ("io.audioOut", 4);
    s.set (out, "level", 0.8f);
    s.wire (chain, "audioOut", out, "audioIn");

    s.layout();
    return grid ? "303 Acid Pattern" : arpeggiated ? "303 Acid Line" : "303 Acid Bass";
}

} // namespace aquanode::presets
