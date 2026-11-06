//==============================================================================
// Piano - the physical piano model with its three measured voicings on one
// card: Model picks Felt (a muted felt upright: dark, soft and close), Mid (a
// brighter felt piano: more hammer and air on top, still warm) or Hard (hard
// hammers: a short, stiff blow and bright, ringing strings). The physics is
// PianoModelCore's; the voicing tables are those of the earlier Felt / Mid /
// Hard Piano modules, which stay registered (unlisted) for the patches made
// with them.
//
// A new Model applies from the next block: notes struck after it use the new
// voicing; notes already ringing keep their strings.
// Per-voice generator. Inputs: 0 = Pitch In, 1 = Env In, 2 = Add Midi In.
// Output: 0 = Audio Out.
//==============================================================================
#include "PianoModelCore.h"

using namespace aquanode;

namespace
{
    constexpr int kNumModels = 3;

    const piano::Voicing& voicingOf (int model)
    {
        switch (juce::jlimit (0, kNumModels - 1, model))
        {
            case 0:  return piano::feltVoicing();
            case 2:  return piano::hardVoicing();
            default: return piano::midVoicing();
        }
    }
}

class PianoModule final : public PianoModel
{
public:
    static constexpr int pModel = pVoices + 1;
    static constexpr int pHardTrim = pModel + 1;

    static ModuleDescriptor descriptor()
    {
        auto d = PianoModel::makeDescriptor ("osc.piano", "Piano",
            juce::String ("A physical model of a piano - a felt hammer thrown at stiff, coupled unison strings, a radiating "
                          "soundboard, a case thump and felt dampers - in three voicings, each measured on a recording: "
                          "Model Felt is a muted felt upright (dark, soft, close), Mid a brighter felt piano, Hard a piano "
                          "with hard hammers (a short, stiff blow and bright, ringing strings).")
                + PianoModel::controlsDescription(), 15, 30.0f, 3);
        // the Model menu on a row of its own at the top
        for (auto& p : d.params)
            ++p.row;
        d.params.push_back (makeCombo ("model", "Model", { "Felt", "Mid", "Hard" }, 1, 0, 2).noMod());   // pModel
        // the Hard voicing's blow peaked above full scale on a single note: it
        // comes down to sit with the other two (patches made before keep it)
        auto hardTrim = makeOutputTrim (-6.5f);
        hardTrim.id = "hardTrim";
        hardTrim.label = "Hard Trim";
        d.params.push_back (hardTrim);   // pHardTrim
        return d;
    }

    PianoModule() : PianoModel (voicingOf (1)) {}

    void blockStart() override
    {
        PianoModel::blockStart();
        const int m = juce::jlimit (0, kNumModels - 1, (int) std::lround (getParameterBase (pModel)));
        if (m != model)
        {
            model = m;
            setVoicing (voicingOf (m));
        }
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        PianoModel::processVoiceSample (v, inputs, outputs);
        if (model == 2)
        {
            const float g = param (pHardTrim);
            outputs[0][0] *= g;
            outputs[0][1] *= g;
        }
    }

private:
    int model { 1 };
};

AQUANODE_REGISTER_MODULE (PianoModule, PianoModule::descriptor)
