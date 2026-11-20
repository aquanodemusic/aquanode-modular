//==============================================================================
// Tape Stop (6.7.11) - the tape slows to a halt.
//
// While Stop is on (above 50 %), the sound plays from a tape that slows down
// over Stop Time - the pitch and speed falling together, Curve from an even
// slowing (0 %) to a heavy reel that hangs on and then drops (100 %) - and
// rests silent once it stands. When Stop goes off, it is back at once, or,
// with Start Time, it spins up again first (still behind) and then joins the
// sound as it is, crossfaded. Mix, Level (for an Automation Lane to open and
// close).
// Global. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class TapeStopModule final : public SynthModule
{
public:
    enum P { pStop = 0, pStopTime, pStartTime, pCurve, pMix, pLevel };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const int need = juce::nextPowerOfTwo ((int) (sr * 10.0));
        for (auto& b : buf) b.assign ((size_t) need, 0.0f);
        mask = need - 1;
        reset();
    }
    void reset() override
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0; readPos = -2.0; speed = 1.0; t = 0.0; state = Running; join = 1.0f;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const float inL = inputs[0][0], inR = inputs[0][1];
        buf[0][(size_t) (writePos & mask)] = inL;
        buf[1][(size_t) (writePos & mask)] = inR;
        const double sr = sampleRate;
        const bool on = param (pStop) >= 50.0f;

        if (on && (state == Running || state == Joining || state == Starting))
        {
            state = Stopping; t = 0.0; startSpeed = speed;
            if (join < 1.0f) { readPos = (double) writePos - 2.0; join = 1.0f; }
        }
        else if (! on && (state == Stopping || state == Stopped))
        {
            const double startT = param (pStartTime) * 0.001;
            if (startT < 0.005) { state = Joining; join = 0.0f; }   // (back at once: crossfade to the sound as it is)
            else { state = Starting; t = 0.0; }
        }

        if (state == Stopping)
        {
            const double T = juce::jmax (0.02, (double) param (pStopTime) * 0.001);
            t += 1.0 / sr;
            const double u = juce::jlimit (0.0, 1.0, t / T);
            // Curve: 0 = speed falls evenly; 1 = a heavy reel (hangs on, then drops)
            const double c = param (pCurve) * 0.01;
            speed = juce::jmax (0.0, startSpeed * ((1.0 - c) * (1.0 - u) + c * (1.0 - u * u * u)));
            if (u >= 1.0) { speed = 0.0; state = Stopped; }
        }
        else if (state == Starting)
        {
            const double T = juce::jmax (0.005, (double) param (pStartTime) * 0.001);
            t += 1.0 / sr;
            const double u = juce::jlimit (0.0, 1.0, t / T);
            speed = u * u * (3.0 - 2.0 * u);
            if (u >= 1.0) { speed = 1.0; state = Joining; join = 0.0f; }
        }
        else if (state == Running) speed = 1.0;

        // the tape: read where it has got to
        readPos += speed;
        const double lag = (double) writePos - readPos;
        if (lag > (double) mask - 4.0) readPos = (double) writePos - ((double) mask - 4.0);
        if (lag < 2.0) readPos = (double) writePos - 2.0;
        const juce::int64 i0 = (juce::int64) std::floor (readPos);
        const float f = (float) (readPos - (double) i0);
        auto at = [this] (int c, juce::int64 i) { return buf[(size_t) c][(size_t) (i & mask)]; };
        // (quieter as it slows: no thump of a standing reel)
        const float amp = (float) std::sqrt (juce::jlimit (0.0, 1.0, speed * 1.6));
        float tl = (at (0, i0) + (at (0, i0 + 1) - at (0, i0)) * f) * amp;
        float tr = (at (1, i0) + (at (1, i0 + 1) - at (1, i0)) * f) * amp;

        if (state == Joining)
        {
            join = juce::jmin (1.0f, join + (float) (1.0 / (0.03 * sr)));
            tl = tl * (1.0f - join) + inL * join;
            tr = tr * (1.0f - join) + inR * join;
            if (join >= 1.0f) { state = Running; readPos = (double) writePos - 2.0; speed = 1.0; }
        }
        const float mix = param (pMix) * 0.01f, level = param (pLevel);
        outputs[0] = { (inL * (1.0f - mix) + tl * mix) * level, (inR * (1.0f - mix) + tr * mix) * level };
        ++writePos;
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.tapestop";
        d.displayName = "Tape Stop";
        d.searchKeywords = "tape stop slow down brake vinyl spin glitch";
        d.description =
            "The tape slows to a halt while Stop is on (above 50 %): speed and pitch fall together over Stop Time - Curve "
            "from an even slowing to a heavy reel that hangs on and then drops - and it rests silent once it stands. When "
            "Stop goes off it is back at once, or spins up over Start Time first and then joins the sound as it is. Mix, "
            "Level (for an Automation Lane to open and close).";
        d.section = ModuleSection::Effect;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("stop", "Stop", 0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f),
            makeRotary ("stopTime", "Stop Time", 30.0f, 4000.0f, 500.0f, 0, "ms", true),
            makeRotary ("startTime", "Start Time", 0.0f, 3000.0f, 0.0f, 0, "ms"),
            makeRotary ("curve", "Curve", 0.0f, 100.0f, 40.0f, 1, "%", false, 1.0f),
            makeRotary ("mix", "Mix", 0.0f, 100.0f, 100.0f, 1, "%", false, 1.0f),
            makeRotary ("level", "Level", 0.0f, 1.0f, 1.0f, 1)
        };
        return d;
    }

private:
    enum State { Running, Stopping, Stopped, Starting, Joining };
    std::vector<float> buf[2];
    juce::int64 mask { 0 }, writePos { 0 };
    double readPos { -2.0 }, speed { 1.0 }, startSpeed { 1.0 }, t { 0.0 };
    float join { 1.0f };
    State state { Running };
};

AQUANODE_REGISTER_MODULE (TapeStopModule, TapeStopModule::descriptor)
