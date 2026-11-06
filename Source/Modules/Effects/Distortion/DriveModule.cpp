//==============================================================================
// Drive - distortion with character (DriveCore.h): Tube (asymmetric, even
// harmonics from Bias), Tape (soft, losing a little top as it is pushed), Fuzz
// (hard, asymmetric clipping) and Crush (sample-rate and bit reduction). Tone
// tilts what is driven harder - the top (fizz, bite) or the low end (thick) -
// and tilts back after the curve. Twice oversampled (Crush aside), with Auto
// Gain keeping the loudness where it was so Drive changes the colour.
// Global effect. Input: 0 = Audio In. Output: 0 = Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/DriveCore.h"

using namespace aquanode;

class DriveModule final : public SynthModule
{
public:
    enum P { pMode, pDrive, pTone, pBias, pMix, pOutput, pAuto };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.drive";
        d.displayName = "Drive";
        d.description = "Distortion with character: Tube (Bias adds even harmonics), Tape (soft, darker as it is pushed), Fuzz "
                        "(hard, asymmetric) or Crush (sample-rate and bit reduction). Tone drives the top or the low end harder "
                        "and tilts back after the curve. Twice oversampled; Auto Gain keeps the loudness, so Drive changes the "
                        "colour, Output trims.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 13;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo ("mode", "Mode", { "Tube", "Tape", "Fuzz", "Crush" }, 0, 0, 2),
            makeRotary ("drive", "Drive", 0.0f, 100.0f, 35.0f, 0, "%", false, 0.1f),
            makeRotary ("tone", "Tone", -100.0f, 100.0f, 0.0f, 0, {}, false, 0.1f),
            makeRotary ("bias", "Bias", 0.0f, 100.0f, 25.0f, 0, "%", false, 0.1f),
            makeRotary ("mix", "Mix", 0.0f, 100.0f, 100.0f, 1, "%", false, 0.1f),
            makeRotary ("output", "Output", -24.0f, 12.0f, 0.0f, 1, "dB", false, 0.1f),
            makeCombo ("autogain", "Auto Gain", { "Off", "On" }, 1, 1, 1).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); drive.prepare (sr); }
    void reset() override { drive.reset(); }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (--ctl < 0)
        {
            ctl = 15;
            s.mode = juce::jlimit (0, drive::NumModes - 1, (int) std::lround (param (pMode)));
            s.drive = param (pDrive) * 0.01f;
            s.tone = param (pTone) * 0.01f;
            s.bias = param (pBias) * 0.01f;
            s.mix = param (pMix) * 0.01f;
            s.outputDb = param (pOutput);
            s.autoGain = getParameterBase (pAuto) >= 0.5f;
        }
        float l = in[0][0], r = in[0][1];
        drive.process (l, r, s);
        if (! std::isfinite (l) || ! std::isfinite (r)) { drive.reset(); l = r = 0.0f; }
        out[0] = { l, r };
    }

private:
    drive::Stereo drive;
    drive::Settings s;
    int ctl { 0 };
};

AQUANODE_REGISTER_MODULE (DriveModule, DriveModule::descriptor)
