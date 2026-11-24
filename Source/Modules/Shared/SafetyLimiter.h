#pragma once
//==============================================================================
// (6.9.19) A safety limiter for the filters that can run away (Center Comb, Comb
// Filter): never over the ceiling - the gain drops at once when a peak would pass
// it, and comes back over 80 ms. Stereo-linked. Not a sound of its own: below the
// ceiling it does nothing at all.
//==============================================================================
#include <JuceHeader.h>

namespace aquanode
{
    struct SafetyLimiter
    {
        float ceiling { 0.95f };
        float env { 0.0f }, release { 0.9997f };

        void prepare (double sr) { release = std::exp (-1.0f / (0.08f * (float) juce::jmax (1000.0, sr))); env = 0.0f; }
        void reset() { env = 0.0f; }

        void process (float& l, float& r)
        {
            const float peak = juce::jmax (std::abs (l), std::abs (r));
            env = peak > env ? peak : peak + release * (env - peak);
            if (! std::isfinite (env)) { env = 0.0f; l = r = 0.0f; return; }
            if (env > ceiling)
            {
                const float g = ceiling / env;
                l *= g;
                r *= g;
            }
        }
    };
}
