//==============================================================================
// Phizmo - random Presets by style (the logo's menu in the editor)
//
// Every roll starts from the default Preset, so nothing of the previous sound
// leaks in, then builds one to four Sounds. Each Sound gets two fresh random
// Transwaves from the archetypes that suit the style, and its envelopes,
// filter, scan and evolution curves are rolled inside ranges that make that
// kind of sound; the arpeggiator, the insert effect and the reverb follow the
// style too. The tempo, the GUI size, the cycle sizes, the engine and the wave
// generator's own settings are left as they are.
//==============================================================================

namespace
{
    struct Roll
    {
        juce::Random r;

        float uni (float a, float b) { return a + (b - a) * r.nextFloat(); }
        // log-uniform: times, frequencies
        float lg (float a, float b) { return a * std::pow (b / a, r.nextFloat()); }
        bool chance (float p) { return r.nextFloat() < p; }
        int between (int a, int b) { return a + r.nextInt (b - a + 1); }
        float sign() { return r.nextBool() ? 1.0f : -1.0f; }
        template <typename T> T pick (std::initializer_list<T> list)
        {
            return *(list.begin() + r.nextInt ((int) list.size()));
        }
    };

    // the archetype indices of the wave generator (its override argument)
    enum Arch { W_ResonantSweep, W_Formant, W_BellStack, W_AnalogSmooth, W_Unrelated, W_Hollow, W_CombGroove,
                W_OrganDrawbar, W_MetallicSweep, W_InharmonicBell, W_SpectralGrit, W_VocalMorph };

    // the insert effect algorithms
    enum Algo { FX_ParamEQ = 0, FX_HallReverb = 1, FX_LargePlate = 4, FX_StereoChorus = 9, FX_EightVoiceChorus = 10,
                FX_ReverbChorus = 11, FX_ReverbFlanger = 12, FX_ReverbPhaser = 13, FX_ChorusReverb = 14,
                FX_FlangerReverb = 15, FX_PhaserReverb = 16, FX_SpinnerReverb = 18, FX_DDLChorus = 19, FX_DDLFlanger = 20,
                FX_DDLPhaser = 21, FX_MultiTapDDL = 23, FX_DistChorus = 24, FX_DistAutowah = 27, FX_ResVCFDDL = 28,
                FX_FormantMorphFx = 32, FX_RotarySpeaker = 33, FX_DistDDLTrem = 36, FX_EQChoDDL = 39 };

    // a 32-point evolution curve (scan time -> table position), kept inside lo..hi
    std::array<float, EVO_POINTS> makeCurve (Roll& g, int kind, float lo, float hi)
    {
        std::array<float, EVO_POINTS> c {};
        const float bend = g.uni (0.5f, 2.0f);
        const float rise = g.uni (1.5f, 3.5f);
        const int steps = g.between (4, 8);
        float walk = g.uni (0.0f, 1.0f), wmin = 1.0e9f, wmax = -1.0e9f;
        std::array<float, EVO_POINTS> raw {};
        for (int i = 0; i < EVO_POINTS; ++i)
        {
            const float t = (float) i / (float) (EVO_POINTS - 1);
            float v = t;
            switch (kind)
            {
                case 0:  v = t; break;                                          // ramp
                case 1:  v = std::pow (t, bend); break;                         // bent ramp
                case 2:  v = 1.0f - std::abs (2.0f * t - 1.0f); break;          // up and back
                case 3:  v = juce::jmin (1.0f, t * rise); break;                // rise, then hold
                case 4:  v = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi); break;   // S-curve
                case 5:  v = std::floor (t * (float) steps) / (float) steps; break;                 // stairs
                default:                                                        // smooth wander
                    walk += g.uni (-0.18f, 0.18f);
                    v = walk;
                    break;
            }
            raw[(size_t) i] = v;
            wmin = juce::jmin (wmin, v);
            wmax = juce::jmax (wmax, v);
        }
        for (int i = 0; i < EVO_POINTS; ++i)
        {
            float v = raw[(size_t) i];
            if (kind >= 6)   // the wander: smoothed, then stretched to 0..1
            {
                const float a = raw[(size_t) juce::jmax (0, i - 1)], b = raw[(size_t) juce::jmin (EVO_POINTS - 1, i + 1)];
                v = ((a + 2.0f * v + b) * 0.25f - wmin) / juce::jmax (1.0e-3f, wmax - wmin);
            }
            c[(size_t) i] = juce::jlimit (0.0f, 1.0f, lo + (hi - lo) * v);
        }
        return c;
    }
}

const char* PhizmoAudioProcessor::randomStyleName (int style)
{
    static const char* n[NumRandomStyles] = { "Pad", "Bass", "Lead", "Pluck", "Keys", "Bell",
                                              "Evolving Texture", "Arpeggio", "Surprise Me" };
    return n[juce::jlimit (0, (int) NumRandomStyles - 1, style)];
}

bool PhizmoAudioProcessor::randomPreset (int style, juce::int64 seed, juce::String& statusOut)
{
    if (seed == 0)
        seed = juce::Random::getSystemRandom().nextInt64();
    Roll g { juce::Random (seed) };
    if (style < 0 || style >= RandSurprise)
        style = g.between (0, RandSurprise - 1);

    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };

    // --- everything back to the default Preset ------------------------------
    static const juce::StringArray keep { "guiWidth", "cycleSizeA", "cycleSizeB", "genFrames", "genUnpitched",
                                          "genArchetype", "tempo", "oscEngine", "gritVintage", "bendRange",
                                          "velCurve", "soundSelect" };
    auto resetAll = [&]
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! keep.contains (rp->getParameterID()))
                    rp->setValueNotifyingHost (rp->getDefaultValue());
    };
    for (int k = NUM_SOUNDS - 1; k >= 0; --k)
    {
        setEditSound (k);
        resetAll();
    }
    for (int k = 0; k < NUM_SOUNDS; ++k)
        set ("soundOn" + juce::String (k), 0.0f);

    // --- how many Sounds, and how they relate -------------------------------
    int sounds = 1;
    switch (style)
    {
        case RandPad:     sounds = g.chance (0.55f) ? 2 : 1; break;
        case RandBell:    sounds = g.chance (0.4f) ? 2 : 1; break;
        case RandTexture: sounds = g.between (2, 3); break;
        case RandKeys:    sounds = g.chance (0.25f) ? 2 : 1; break;
        default:          sounds = 1; break;
    }

    auto archetypeFor = [&] () -> int
    {
        switch (style)
        {
            case RandPad:     return g.pick ({ W_AnalogSmooth, W_Formant, W_VocalMorph, W_Hollow, W_OrganDrawbar, W_ResonantSweep });
            case RandBass:    return g.pick ({ W_ResonantSweep, W_AnalogSmooth, W_CombGroove, W_SpectralGrit, W_Hollow });
            case RandLead:    return g.pick ({ W_ResonantSweep, W_VocalMorph, W_MetallicSweep, W_Formant, W_SpectralGrit, W_Hollow });
            case RandPluck:   return g.pick ({ W_BellStack, W_Hollow, W_OrganDrawbar, W_ResonantSweep, W_CombGroove });
            case RandKeys:    return g.pick ({ W_OrganDrawbar, W_Hollow, W_AnalogSmooth, W_BellStack, W_Formant });
            case RandBell:    return g.pick ({ W_InharmonicBell, W_BellStack, W_MetallicSweep, W_InharmonicBell });
            case RandTexture: return g.pick ({ W_Unrelated, W_SpectralGrit, W_MetallicSweep, W_VocalMorph, W_Formant, W_CombGroove });
            default:          return g.pick ({ W_BellStack, W_Hollow, W_ResonantSweep, W_OrganDrawbar, W_CombGroove });   // arpeggio
        }
    };

    int wavesOk = 0;
    for (int k = 0; k < sounds; ++k)
    {
        setEditSound (k);
        const juce::String ks (k);
        set ("soundOn" + ks, 1.0f);
        set ("soundGain" + ks, (k == 0 ? 1.0f : g.uni (0.55f, 0.85f)) / std::sqrt ((float) sounds));

        // two fresh Transwaves; a layer may share one archetype across both oscillators
        const int frames = g.pick ({ 32, 64, 64 });
        const int archA = archetypeFor();
        const int archB = g.chance (0.5f) ? archA : archetypeFor();
        for (int o = 0; o < 2; ++o)
        {
            juce::String st;
            if (generateRandomWavetable (k * 2 + o, g.r.nextInt64(), frames, getCycleSizeParam (o), st,
                                         o == 0 ? archA : archB))
                ++wavesOk;
        }

        // the pitch relation of the two oscillators (and of the layers)
        set ("oscMix", g.uni (0.35f, 0.65f));
        set ("levelB", g.uni (0.6f, 1.0f));
        const float layerTune = k == 0 ? 0.0f : (float) g.pick ({ 12, -12, 7, 0, 19 });

        // the defaults most styles share
        auto ampEnv = [&] (float a, float d, float s, float r)
        {
            set ("attack", a);  set ("decay", d);  set ("sustain", s);  set ("release", r);
            set ("attack2", a * g.uni (0.8f, 1.4f));
            set ("decay2", d * g.uni (0.7f, 1.5f));
            set ("sustain2", juce::jlimit (0.0f, 1.0f, s * g.uni (0.8f, 1.15f)));
            set ("release2", r * g.uni (0.8f, 1.3f));
        };
        auto filter = [&] (float freq, float q, float envAmt, float fa, float fd, float fs, float fr)
        {
            set ("filterType", 0.0f);
            set ("filterFreq", freq);  set ("filterQ", q);  set ("filterEnvAmt", envAmt);
            set ("filterAtt", fa);  set ("filterDec", fd);  set ("filterSus", fs);  set ("filterRel", fr);
            set ("filterKeytrack", g.uni (0.1f, 0.5f));
        };
        auto scan = [&] (int styleA, float time, bool restart)
        {
            set ("scanStyle", (float) styleA);
            set ("scanStyleB", (float) (g.chance (0.7f) ? styleA : g.pick ({ 0, 1, 2 })));
            set ("evoTime", time);
            set ("evoTimeB", time * g.uni (0.7f, 1.4f));
            set ("evoRestart", restart ? 1.0f : 0.0f);
        };
        auto curves = [&] (std::initializer_list<int> kinds, float lo, float hi)
        {
            const int kindA = g.pick (kinds), kindB = g.chance (0.5f) ? kindA : g.pick (kinds);
            const auto a = makeCurve (g, kindA, lo, hi);
            const auto b = makeCurve (g, kindB, lo, hi);
            for (int i = 0; i < EVO_POINTS; ++i)
            {
                const auto idx = juce::String::formatted ("%02d", i);
                set ("evoPoint_" + idx, a[(size_t) i]);
                set ("evoPointB_" + idx, b[(size_t) i]);
            }
        };
        auto tune = [&] (float b)
        {
            set ("tuneA", layerTune);
            set ("tuneB", layerTune + b);
            const float cents = g.uni (2.0f, 9.0f);
            set ("fineA", -cents * 0.5f);
            set ("fineB", cents * 0.5f);
        };

        switch (style)
        {
            case RandPad:
                ampEnv (g.lg (0.3f, 2.5f), g.lg (1.0f, 4.0f), g.uni (0.6f, 0.9f), g.lg (1.2f, 4.0f));
                filter (g.lg (1500.0f, 9000.0f), g.uni (0.5f, 1.5f), g.uni (0.0f, 0.3f), g.lg (0.5f, 3.0f), 2.0f, 0.5f, 2.0f);
                scan (g.pick ({ 2, 2, 0 }), g.lg (3.0f, 14.0f), false);
                curves ({ 2, 4, 6, 1 }, g.uni (0.0f, 0.25f), g.uni (0.65f, 1.0f));
                tune ((float) g.pick ({ 0, 0, 12, 7, -12 }));
                set ("spread", g.uni (4.0f, 15.0f));
                set ("stereoWidth", g.uni (0.6f, 1.0f));
                set ("panA", -g.uni (0.0f, 0.4f));  set ("panB", g.uni (0.0f, 0.4f));
                set ("posLFODepth", g.uni (0.0f, 0.25f));  set ("posLFORate", g.lg (0.05f, 0.4f));
                break;

            case RandBass:
            {
                ampEnv (g.lg (0.001f, 0.005f), g.lg (0.15f, 0.8f), g.uni (0.2f, 0.8f), g.lg (0.05f, 0.25f));
                filter (g.lg (250.0f, 1400.0f), g.uni (1.0f, 4.5f), g.uni (0.3f, 0.75f), 0.001f, g.lg (0.1f, 0.5f),
                        g.uni (0.0f, 0.3f), 0.2f);
                scan (1, g.lg (0.15f, 1.2f), true);
                curves ({ 0, 1, 3 }, 0.0f, g.uni (0.5f, 1.0f));
                set ("tuneA", 0.0f);
                set ("tuneB", (float) g.pick ({ 0, 0, 12, 7, -12 }));
                set ("fineB", g.uni (-6.0f, 6.0f));
                set ("octaveA", g.chance (0.3f) ? -2.0f : -1.0f);
                set ("octaveB", (float) g.pick ({ -1, -1, -2, 0 }));
                if (g.chance (0.6f)) { set ("mono", 1.0f); set ("glide", g.lg (0.02f, 0.12f)); }
                if (g.chance (0.25f)) set ("grit", g.uni (0.1f, 0.4f));
                set ("ampVelAmt", 0.5f);
                break;
            }

            case RandLead:
                ampEnv (g.lg (0.003f, 0.05f), g.lg (0.3f, 1.0f), g.uni (0.7f, 1.0f), g.lg (0.1f, 0.5f));
                filter (g.lg (1500.0f, 7000.0f), g.uni (0.8f, 3.0f), g.uni (0.1f, 0.5f), 0.005f, g.lg (0.2f, 0.8f), 0.6f, 0.3f);
                scan (g.pick ({ 2, 0 }), g.lg (1.0f, 6.0f), g.chance (0.5f));
                curves ({ 0, 1, 2, 4, 6 }, g.uni (0.0f, 0.3f), g.uni (0.6f, 1.0f));
                tune ((float) g.pick ({ 0, 7, 12, -12, 0 }));
                if (g.chance (0.7f)) { set ("mono", 1.0f); set ("glide", g.lg (0.03f, 0.2f)); }
                set ("pitchLFO", g.uni (0.08f, 0.25f));
                set ("pitchLFORate", g.uni (4.5f, 6.5f));
                break;

            case RandPluck:
            case RandArp:
                ampEnv (g.lg (0.001f, 0.003f), g.lg (0.2f, 0.9f), g.uni (0.0f, 0.12f), g.lg (0.2f, 0.7f));
                filter (g.lg (600.0f, 3000.0f), g.uni (0.7f, 3.0f), g.uni (0.4f, 0.85f), 0.001f, g.lg (0.1f, 0.6f), 0.0f, 0.3f);
                scan (1, g.lg (0.2f, 1.5f), true);
                curves ({ 0, 1, 3, 5 }, 0.0f, g.uni (0.5f, 1.0f));
                tune ((float) g.pick ({ 0, 12, 7, 0, 19 }));
                set ("stereoWidth", g.uni (0.4f, 0.8f));
                break;

            case RandKeys:
                ampEnv (g.lg (0.002f, 0.01f), g.lg (0.8f, 2.5f), g.uni (0.25f, 0.6f), g.lg (0.3f, 1.0f));
                filter (g.lg (2000.0f, 8000.0f), g.uni (0.5f, 1.2f), g.uni (0.2f, 0.5f), 0.002f, g.lg (0.4f, 1.5f), 0.3f, 0.5f);
                scan (1, g.lg (0.8f, 3.0f), true);
                curves ({ 0, 1, 3, 4 }, 0.0f, g.uni (0.4f, 0.9f));
                tune ((float) g.pick ({ 0, 12, 0, 7 }));
                break;

            case RandBell:
                ampEnv (0.001f, g.lg (1.5f, 5.0f), g.uni (0.0f, 0.05f), g.lg (1.5f, 4.0f));
                filter (g.lg (5000.0f, 16000.0f), g.uni (0.5f, 1.0f), g.uni (0.0f, 0.2f), 0.001f, 1.5f, 0.3f, 2.0f);
                scan (g.pick ({ 2, 0, 1 }), g.lg (2.0f, 8.0f), true);
                curves ({ 0, 1, 2, 4 }, 0.0f, g.uni (0.5f, 1.0f));
                tune ((float) g.pick ({ 12, 19, 24, 7, 0 }));
                set ("stereoWidth", g.uni (0.5f, 0.9f));
                break;

            default:   // RandTexture
                ampEnv (g.lg (0.8f, 4.0f), g.lg (2.0f, 6.0f), g.uni (0.6f, 0.9f), g.lg (2.0f, 6.0f));
                filter (g.lg (800.0f, 6000.0f), g.uni (0.6f, 2.5f), g.uni (0.0f, 0.3f), g.lg (1.0f, 4.0f), 3.0f, 0.6f, 3.0f);
                scan (g.pick ({ 2, 0, 4 }), g.lg (8.0f, 40.0f), false);
                curves ({ 2, 4, 6, 6, 5 }, 0.0f, 1.0f);
                tune ((float) g.pick ({ 0, 7, 12, -12, 5 }));
                if (g.chance (0.3f)) { set ("evoMode", 1.0f); set ("evoModeB", g.chance (0.5f) ? 1.0f : 0.0f); }
                if (g.chance (0.4f)) set ("jumpProb", g.uni (0.02f, 0.2f));
                set ("posLFODepth", g.uni (0.1f, 0.4f));  set ("posLFORate", g.lg (0.03f, 0.3f));
                set ("filterLFODep", g.uni (0.1f, 0.4f));  set ("lfoSpeed", g.lg (0.05f, 0.5f));
                set ("panA", -g.uni (0.2f, 0.7f));  set ("panB", g.uni (0.2f, 0.7f));
                set ("spread", g.uni (5.0f, 20.0f));
                set ("stereoWidth", g.uni (0.7f, 1.0f));
                break;
        }
        // a layer above or below keeps clear of the first in the stereo field
        if (k > 0)
        {
            set ("panA", g.uni (-0.6f, 0.6f));
            set ("panB", g.uni (-0.6f, 0.6f));
        }
    }

    // --- the shared part: arpeggiator, effects, reverb ------------------------
    int algo = FX_StereoChorus;
    float fxMix = 0.3f, rev = 0.25f;
    switch (style)
    {
        case RandPad:     algo = g.pick ({ FX_StereoChorus, FX_EightVoiceChorus, FX_ChorusReverb, FX_SpinnerReverb, FX_EQChoDDL });
                          fxMix = g.uni (0.25f, 0.5f);  rev = g.uni (0.3f, 0.55f);  break;
        case RandBass:    algo = g.pick ({ FX_DistChorus, FX_DistAutowah, FX_ResVCFDDL, FX_StereoChorus, FX_ParamEQ });
                          fxMix = g.uni (0.0f, 0.3f);   rev = g.uni (0.0f, 0.12f);  break;
        case RandLead:    algo = g.pick ({ FX_DDLChorus, FX_MultiTapDDL, FX_DDLFlanger, FX_DistDDLTrem, FX_DDLPhaser });
                          fxMix = g.uni (0.2f, 0.45f);  rev = g.uni (0.1f, 0.3f);   break;
        case RandPluck:   algo = g.pick ({ FX_DDLChorus, FX_MultiTapDDL, FX_StereoChorus, FX_LargePlate });
                          fxMix = g.uni (0.2f, 0.4f);   rev = g.uni (0.15f, 0.35f); break;
        case RandKeys:    algo = g.pick ({ FX_RotarySpeaker, FX_StereoChorus, FX_PhaserReverb, FX_EightVoiceChorus });
                          fxMix = g.uni (0.25f, 0.5f);  rev = g.uni (0.15f, 0.3f);  break;
        case RandBell:    algo = g.pick ({ FX_HallReverb, FX_LargePlate, FX_ChorusReverb, FX_ReverbChorus });
                          fxMix = g.uni (0.3f, 0.5f);   rev = g.uni (0.3f, 0.5f);   break;
        case RandTexture: algo = g.pick ({ FX_ReverbFlanger, FX_ReverbPhaser, FX_FormantMorphFx, FX_SpinnerReverb, FX_FlangerReverb });
                          fxMix = g.uni (0.3f, 0.6f);   rev = g.uni (0.4f, 0.7f);   break;
        default:          algo = g.pick ({ FX_MultiTapDDL, FX_DDLChorus, FX_DDLFlanger });
                          fxMix = g.uni (0.25f, 0.45f); rev = g.uni (0.15f, 0.3f);  break;
    }
    // short or sparse sounds (plucks, bells, arpeggios) a little louder, to sit
    // level with the sustained styles
    if (style == RandPluck || style == RandArp || style == RandBell)
        set ("gain", g.uni (0.85f, 0.95f));
    set ("fxAlgo", (float) algo);
    set ("fxVariation", g.uni (0.2f, 0.7f));
    set ("fxMix", fxMix);
    set ("revAmount", rev);
    set ("revVariation", (float) (style == RandTexture || style == RandPad ? g.pick ({ 1, 7, 0 }) : g.between (0, 7)));

    if (style == RandArp)
    {
        set ("arpOn", 1.0f);
        set ("arpMode", (float) g.pick ({ 0, 0, 2, 2, 3, 4 }));
        set ("arpRange", (float) g.pick ({ 1, 1, 2, 0 }));
        set ("arpRate", (float) g.pick ({ 6, 8, 8, 7 }));   // 1/8, 1/16, 1/8 T
        set ("arpGate", g.uni (0.3f, 0.7f));
    }

    setEditSound (0);
    const auto name = juce::String ("Random ") + randomStyleName (style) + " " + juce::String ((int) (seed & 0xffff));
    setCurrentPresetName (name);
    statusOut = name + (wavesOk == sounds * 2 ? juce::String() : " (some waves failed)");
    return wavesOk > 0;
}
