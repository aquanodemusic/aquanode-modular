#pragma once

#include "../../Core/ModuleCore.h"
#include "../../Shared/RotaryCabinet.h"

// Rotary - a rotating speaker cabinet, voiced after the classic free rotary
// plugins: the signal is split at X-Over into a bass drum and a treble horn.
// Each rotor turns at its own speed and has its own inertia (the horn spins
// up faster than the heavy drum), and each moves the sound three ways:
//   Throb  the level rises and falls as the rotor faces you and turns away
//          (the horn's lobe is narrow, so its throb is sharper)
//   Width  the sound swings between left and right
//   Depth  (horn) the Doppler shift: the horn's mouth moves toward you and
//          away, its sound bent up and down in pitch and summed with a
//          reflection from the cabinet - the moving comb of the swirl
// Speed switches Stop / Slow / Fast (cable it from a Gate or an LFO to switch
// it from a key or a rhythm); Rate scales both speeds, Inertia how long the
// rotors take to get there. Drive is the cabinet amp's tube grit.
// Global effect. Input: 0 = Audio In. Output: 0 = Audio Out.
class RotaryModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pSpeed = 0, pRate, pInertia, pXover, pHornDepth, pHornThrob, pHornWidth,
                      pDrumThrob, pDrumWidth, pDrive, pLevel, pMix };

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    aquanode::RotaryCabinet::Settings settings() const;
    aquanode::RotaryCabinet cabinet;   // (6.9.1: Shared/RotaryCabinet.h)
};
