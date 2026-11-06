#pragma once
//==============================================================================
// Tidebreeze - waves (the "PCM" multisamples)
//
// A wave is what a workstation's multisample is: a mono sample with an attack
// and a sustain loop, recorded at one root note and played across the whole
// keyboard by transposing it - so whatever moves inside it (beating, sweeps,
// clicks, a vowel turning) speeds up as you play higher, which is a large part
// of how those instruments sound.
//
// Here no wave is a recording. A WaveSpec describes one, and render() makes
// the sample from it:
//
//   - harmonics: a spectrum (first 32 partials in dB plus a tilt), shaped by
//     vowel formants, up to two resonances sweeping across it, odd/even
//     balance and stretch, read as an evolving wavetable by 1-5 slightly
//     detuned copies (the beating of an ensemble, a chorused vox), with
//     vibrato, drift and a pitch scoop at the start;
//   - breath: noise shaped by its own band, tilt and (optionally) gathered
//     around the harmonics, through the same formants;
//   - events: click trains (sonar, clickers) or bubbles (rising Minnaert
//     chirps, the sound of moving water) on top;
//   - an attack envelope and amplitude motion, then a crossfaded sustain loop.
//
// The rendered sample keeps a mip pyramid (each level half the rate of the
// one above), so playing it far above its root stays clean.
//
// The factory waves are hand-made specs; rolled waves come from an archetype
// and a seed (roll()), so a program only needs to store those two numbers.
//==============================================================================
#include "TbCommon.h"

namespace tb
{
    struct WaveSpec
    {
        // --- timeline (seconds, at the root note) ---------------------------
        float rootHz = 261.63f;
        float attackSec = 0.4f;        // before the loop starts (>= xfadeSec)
        float loopSec = 2.0f;          // the sustain loop
        float xfadeSec = 0.3f;         // loop crossfade
        float attackRise = 0.05f;      // amplitude rise time at the start
        bool  oneShot = false;         // no loop: plays attack + loop once and stops (bells, hits)
        float oneShotDecay = 1.5f;     // seconds to -60 dB for one-shots

        // --- harmonics --------------------------------------------------------
        float harmDb = 0.0f;                 // level of the harmonic part (dB, before normalising)
        float harm[32] = {};                 // dB of partials 1..32, relative
        float tiltDbOct = -6.0f;             // beyond the 32: slope per octave from partial 32's level
        float evenDb = 0.0f;                 // added to even partials
        float stretch = 0.0f;                // inharmonicity: f_k = k f0 sqrt (1 + B k^2)
        int   maxPartials = 200;
        int   copies = 1;                    // detuned copies (1..5)
        float detuneCents = 0.0f;            // spread of the copies (outer to centre)
        float vibRateHz = 0.0f, vibCents = 0.0f;
        float driftCents = 0.0f, driftRateHz = 0.3f;   // slow random pitch wander
        float scoopCents = 0.0f, scoopSec = 0.1f;      // pitch at the very start, gliding to 0
        float harmAmRateHz = 0.0f, harmAmDb = 0.0f;    // tremolo of the harmonic part

        // formants (Hz at root): vowel A, morphing to vowel B and back over the loop
        int   numFormants = 0;
        float formHz[4] = {}, formBw[4] = {}, formDb[4] = {};
        float formBHz[4] = {}, formBBw[4] = {}, formBDb[4] = {};
        float formMorphCycles = 0.0f;        // A->B->A cycles per loop (0: stays on A)
        float formFloorDb = -30.0f;          // level away from the formants

        // sweeping resonances over the spectrum
        struct Sweep { float fromHz = 0, toHz = 0, widthOct = 1.0f, gainDb = 0, cycles = 1; int shape = 0; float phase = 0; };
        int   numSweeps = 0;
        Sweep sweep[2];                      // shape 0 triangle, 1 saw up, 2 saw down, 3 sine

        // --- breath / noise ---------------------------------------------------
        float noiseDb = -100.0f;             // noise level relative to the harmonic part (dB)
        float noiseLowHz = 100.0f, noiseHighHz = 12000.0f;
        float noiseTiltDbOct = 0.0f;
        float noiseFocus = 0.0f;             // 0 flat, 1 gathered around the harmonics
        float noiseFocusBw = 0.08f;          // width of that gathering (fraction of f0)
        bool  noiseFormants = true;          // through the same formants
        bool  noiseSweeps = true;            // through the same sweeps
        float noiseAmRateHz = 0.0f, noiseAmDb = 0.0f;
        float attackNoiseDb = -100.0f;       // a breath / chiff burst at the start
        float attackNoiseSec = 0.08f;

        // --- events -------------------------------------------------------------
        int   eventType = 0;                 // 0 none, 1 clicks, 2 bubbles, 3 click trains
        float eventRateHz = 3.0f;            // clicks per second / bubbles per second
        float eventDb = 0.0f;                // relative to the harmonic part
        float eventDecaySec = 0.03f;
        juce::uint32 eventAccents = 0xF;     // clicks: accent pattern over 8 steps (bit set = loud)
        float clickHarm[12] = { 0, -6, -12, -18, -24, -30, -36, -42, -48, -54, -60, -66 };   // partials 1..12 of a click
        float clickGateSec = 0.0f;           // >0: each click is cut off after this long (a burst, not a ping)
        float clickNoiseDb = -100.0f;        // a broadband tick at each click (relative to its partials)
        float clickThumpDb = -100.0f;        // a low thump under each click
        float clickThumpMul = 0.3f;          // its pitch relative to the click's
        float clickPitchMul = 1.0f;          // click pitch relative to the root
        float bubbleLowMul = 2.0f, bubbleHighMul = 8.0f;   // bubble pitch range (multiples of f0)
        float bubbleChirp = 0.15f;           // upward sweep (fraction)
        bool  bubbleHarmonic = true;         // bubbles land on partials of the note
        bool  eventsReplaceTone = false;     // clicks only (the harmonic part silent)
        // click trains (echolocation): bursts of rapid pulses, each a short tone
        // on the odd partials of clickHarm, gliding across the burst. The pulse
        // rate and the tone follow the zone's root only partly (track 0..1)
        int   trainCount = 2;                // bursts per loop
        float trainSec = 1.6f;               // a burst's length
        float trainToneHz = 1500.0f;         // the pulses' tone at a C4 root
        float trainToneTrack = 0.3f, trainRateTrack = 0.25f;
        float trainGlideOct = 0.25f;         // the glide across a burst (random direction)

        // --- motion of the whole -----------------------------------------------
        float levelSwellCycles = 0.0f, levelSwellDb = 0.0f;   // slow swell over the loop

        // --- zones -------------------------------------------------------------
        // 0: one recording transposed over the whole keyboard (its motion speeds
        // up as you play higher). >0: a zone every so many semitones from C1 to
        // C7, each rendered at its own root with the formants where they are in
        // Hz - the way voices and acoustic instruments are multisampled.
        int zoneStepSemis = 0;
        float zoneLowNote = 24.0f;           // the lowest zone's root; notes below play it transposed down

        juce::uint32 seed = 1;
        float gainDb = 0.0f;                 // extra gain after normalising
    };

    // one rendered sample: level 0 at the render rate, level L at rate / 2^L
    struct Zone
    {
        std::vector<std::vector<float>> levels;
        float rootHz = 261.63f;
        float topNote = 127.0f;              // the zone plays up to this note
        int loopStart = 0, loopEnd = 0;      // in level-0 samples
        int length() const { return levels.empty() ? 0 : (int) levels[0].size(); }
    };

    struct Wave
    {
        std::vector<Zone> zones;             // by rising root
        double renderRate = 44100.0;
        bool oneShot = false;
        const Zone& zoneFor (float note) const
        {
            for (const auto& z : zones) if (note <= z.topNote) return z;
            return zones.back();
        }
    };

    using WavePtr = std::shared_ptr<const Wave>;

    WavePtr render (const WaveSpec& spec, double rate = 44100.0);

    //==========================================================================
    // the factory waves and the archetypes waves are rolled from
    //==========================================================================
    struct FactoryWave
    {
        const char* name;
        const char* group;        // "Air", "Voice", "Motion", "Strings", "Synth", "Bell", "Water", "Basic"
        std::function<WaveSpec()> make;
    };
    const std::vector<FactoryWave>& factoryWaves();

    enum Archetype
    {
        ArchAirPad, ArchGlassVox, ArchVoice, ArchBreathFlute, ArchWaveSweep, ArchClicks,
        ArchStrings, ArchSuperSaw, ArchGhost, ArchNoiseSpectrum, ArchDigitalBell,
        ArchBubbles, ArchSurf, ArchWhale, NumArchetypes
    };
    const char* archetypeName (int a);
    WaveSpec roll (int archetype, juce::uint32 seed);

    //==========================================================================
    // user waves from audio files
    //
    // A loaded file is kept as it came (mono, at its own rate, and encoded
    // losslessly for presets and patches); fromSample() makes a wave of it the
    // way the recipes are made: one zone at its root key, a crossfaded
    // sustain loop (or one-shot), the loop (or the whole one-shot) at the
    // factory waves' loudness, and the mip pyramid.
    //==========================================================================
    struct SampleData
    {
        std::vector<float> audio;     // mono
        double rate = 44100.0;
        juce::MemoryBlock encoded;    // the same audio as FLAC (or WAV), for saving
        juce::String key;             // a hash of the audio: rendered waves are shared by it
        juce::String fileName;        // where it came from (shown only)
    };
    using SamplePtr = std::shared_ptr<const SampleData>;

    enum SampleLoopMode { LoopOneShot, LoopSustain, NumLoopModes };
    inline const juce::StringArray& loopModeNames() { static const juce::StringArray n { "One-Shot", "Loop" }; return n; }

    struct SampleSettings
    {
        int rootNote = 60;            // the key the file sounds at its own pitch
        int loopMode = LoopSustain;
        float loopStart = 0.5f;       // fraction of the length (LoopSustain)
        float xfadeMs = 80.0f;        // loop crossfade
        bool operator== (const SampleSettings& o) const
        {
            return rootNote == o.rootNote && loopMode == o.loopMode
                && std::abs (loopStart - o.loopStart) < 1.0e-6f && std::abs (xfadeMs - o.xfadeMs) < 1.0e-4f;
        }
    };

    WavePtr fromSample (const SampleData& data, const SampleSettings& settings);

    // reads an audio file (any format JUCE reads, by its content) into mono at
    // its own rate, up to maxSeconds; nullptr and a reason when it can't
    SamplePtr readSampleFile (const juce::File& file, double maxSeconds, juce::String& error);
    // the saved form back into audio (and its key)
    SamplePtr decodeSample (const juce::MemoryBlock& encoded, const juce::String& fileName);
    // first guesses for a freshly loaded file: the root from a note name in
    // the file name ("Pad C3.wav") or the pitch it plays, the loop from how it
    // ends (fading out: one-shot)
    SampleSettings guessSettings (const SampleData& data);
}
