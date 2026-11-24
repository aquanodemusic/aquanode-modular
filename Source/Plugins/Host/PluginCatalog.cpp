#include "PluginCatalog.h"
#include "RandomPresetSources.h"
#include "../../Engine/PluginProcessor.h"

namespace aquanode::plugins
{

namespace
{
    // everything the GUI module of a plugin needs except its create function
    struct BaseInfo
    {
        const char* id;
        const char* name;
        const char* description;
        juce::uint32 accent;
        ModuleSection section;
        bool midiIn, midiOut, audioIn, sideIn;
        int w, h;
        const char* presetPatterns;
        bool thirdParty { false };      // listed in the sidebar's Third Party section
        const char* sideInLabel { nullptr };   // the second input's socket name (default "Mod In")
    };

    // sidebar order: the synths, then the effects
    const std::vector<BaseInfo>& baseInfos()
    {
        static const std::vector<BaseInfo> infos {
            { "aquanova", "Aquanova",
              "A loose Supernova-style synth with its whole panel on the card: three oscillators, the "
              "oscillator and filter matrices, the 42-band vocoder and the effects. Audio In feeds its "
              "audio-in oscillators and the vocoder.",
              0xff5a9fe6, ModuleSection::Oscillator, true, false, true, false, 1200, 700, "*.aquanova" },

            { "aquavibrio", "Aqua Vibrio",
              "A close Virus/Viper-style synth with both of its engines (Native and Original), the whole "
              "effects rack and the note layers. Audio In is its Input oscillator and vocoder carrier. To hear "
              "its banks as they were made: Engine on Original; Waves: the folder that holds the Waveforms and "
              "Wavetables folders (their User folders have the hardware's sampled waves and wavetables, their "
              "Factory folders the .fxb banks' own waves); then Load a .mid / .syx bank or an .fxb bank.",
              0xff2f7a4a, ModuleSection::Oscillator, true, false, true, false, 1200, 700, "*.json;*.syx;*.SYX;*.mid;*.midi;*.fxb" },

            { "aquamassif", "Massif",
              "A close model of a classic 2000s big-wavetable synth, made from measurements: three wavetable oscillators with "
              "their eight modes, the modulation oscillator, noise and feedback, two filters with serial / parallel "
              "routing, two inserts, two master effects, EQ, the bypass, four envelopes, four LFO / stepper / "
              "performer pages and eight macros, with drag and drop modulation. Wavetables: the folder with your "
              "wavetables (.wav, any sub-folders) - tables without a file are generated, and the WT GEN page writes "
              "new ones from recipes. Comes with its own built-in sounds (tap the display) and a patch generator "
              "(a genre - Dub Techno among them -, a sound type, Generate); loads original .nmsv sounds with their modulation pages and its own .massif presets. Tap its "
              "name for the colours.",
              0xff3fd0e8, ModuleSection::Oscillator, true, false, false, false, 1260, 740, "*.nmsv;*.massif" },

            { "reflex", "Reflex",
              "A close remake of a classic 2005-era three-oscillator VA synth, measured: 32 waveforms with fat "
              "copies and drift, a ladder filter in thirteen types, two envelopes, three LFOs, the arpeggiator, "
              "the trance gate, delay and reverb, with its randomizer and mutator. Loads the original's .fxb "
              "banks and .fxp programs and its own .xml presets.",
              0xff6fd36a, ModuleSection::Oscillator, true, false, false, false, 1112, 850, "*.fxb;*.fxp;*.xml" },

            { "am20", "AM-20",
              "An MS-20-style synth, polyphonic, modelled from measurements of the original: both VCOs with the ring "
              "modulator, the screaming 12 dB high- and low-pass filters, EG1 (delay / attack / release), EG2 (hold / ADSR), "
              "the MG, and the whole 35-jack patch panel with the modulation VCA, sample & hold, noise and the external "
              "signal processor - every voice with a patch panel of its own. Poly, unison and mono with portamento. Its own "
              ".am20 presets, and the original's .fxp programs and .fxb banks in and out.",
              0xffff8a3d, ModuleSection::Oscillator, true, false, false, false, 1100, 720, "*.am20;*.fxp;*.fxb" },

            { "alphabeta", "AlphaBeta",
              "The Alpha 3-style synth with its liquid filter and chorus, both engines (Native and "
              "Original), the CRT matrix and its randomizer. Loads .abpreset and the original's .fxp / .fxb.",
              0xff4fb3d9, ModuleSection::Oscillator, true, false, false, false, 1140, 586, "*.abpreset;*.fxp;*.fxb" },

            { "phizmo", "Phizmo",
              "The four-sound transwave synth: two transwave oscillators per sound, the arpeggiator, the "
              "insert effects, global reverb and the performance macros.",
              0xffa77bdc, ModuleSection::Oscillator, true, false, false, false, 1300, 714, "*.phizmo" },

            { "phizmosc", "PhizmOsc",
              "The two-oscillator transwave synth with its evolution envelopes, filter, chorus and reverb. "
              "Presets carry their wavetables with them.",
              0xffc07ad0, ModuleSection::Oscillator, true, false, false, false, 1100, 640, "*.phizm" },

            { "tidebreeze", "Tidebreeze",
              "The generative wave synth: presets of one or two layers playing multisample waves it makes "
              "itself (every factory wave is a recipe) or audio files you load, each with its filter, amp, envelopes "
              "and LFOs, the arpeggio, three insert and two send effects. Rolls new waves and presets, up to Super Random.",
              0xff35a8c9, ModuleSection::Oscillator, true, false, false, false, 1100, 620, "*.tidebreeze" },

            { "covegroove", "Cove Groove",
              "The hardgroove loop generator: eight drum lanes (kick, offbeat bass, clap, hats, cymbal, conga / tom, FX) "
              "in patterns of up to four bars that GENERATE writes in a style - or you by hand - with swing, a dotted "
              "delay and a master drive. TOP on a lane sends it through the top bus - a subtle phaser and a band-pass "
              "moving with the bars - before the drive; lanes without TOP go to the drive dry. Every lane has synthesized "
              "sounds, the classic boxes, physically modelled kits (808, 909, Classic, Electro, Large Drums, Metal, "
              "Orchestral, World) and your own samples (+ beside the lane's name loads one). The row above the steps "
              "shows a lane's sound up close with its loudness over time; on a modelled kit it picks any of the kit's "
              "sounds and moves the model's own knobs. Plays with the DAW's transport, or on RUN at its own tempo; keys "
              "from C2 play the lanes.",
              0xff1ad6ec, ModuleSection::Oscillator, true, false, false, false, 1100, 700, "*.covegroove" },

            { "virtualdx7", "VirtualDX7",
              "The six-operator FM synth with both of its engines: the native engine, and the bit-accurate "
              "VDX7 emulation once you load your own DX7 firmware ROM. Loads any DX7 .syx bank.",
              0xffb8864a, ModuleSection::Oscillator, true, false, false, false, 1180, 594, "*.syx;*.SYX" },

            { "vita", "Vita",
              "The wavetable synth Vital (GPL source by Matt Tytel), without its name, logo and sign-in: "
              "three wavetable oscillators with spectral morphs and warps, the sampler, two filters, "
              "envelopes, LFOs, randoms, the modulation matrix and the effects rack, with its whole "
              "interface on the card. Loads .vital presets.",
              0xff7b8cde, ModuleSection::Oscillator, true, false, false, false, 1260, 738, "*.vital" },

            { "aliassynth", "AliasSynth",
              "The synth with no anti-aliasing at all: a saw (one folded fundamental, or 64 harmonics each "
              "folded on its own), a sine or a loaded sample, folded around a Nyquist limit of your choosing "
              "(or held at that rate, S&H), with a sine FM modulator, a wavefolder, bit reduction, transpose "
              "with glide and an ADSR. Load Sample plays a sound file rooted at C0, C5 or C10.",
              0xff20f2f7, ModuleSection::Oscillator, true, false, false, false, 700, 300, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" },

            { "noteadder", "NoteAdder",
              "Adds notes around the ones you play - manual, scale and custom modes, animation, humanize "
              "and decay. Its MIDI Out plays whatever is patched to it; Audio Out is its Audit sine.",
              0xff9ccc65, ModuleSection::Utility, true, true, false, false, 560, 592, "*.nastate" },

            { "soundscape", "Soundscape Ecology",
              "The procedural nature synth: three slots of Biophony, Geophony and Anthrophony models with "
              "weather, time of day and stereo placement. MIDI In plays it in its MIDI modes.",
              0xff6abf69, ModuleSection::Oscillator, true, false, false, false, 1200, 700, "*.xml" },

            { "slicer", "Slicer",
              "The drum slicer: load a loop (or anything), it finds the hits and lays one slice on each key "
              "from C1 up, with reverse, loop, mono and B0's random stream. Its 'presets' are audio files.",
              0xffe58a4e, ModuleSection::Oscillator, true, false, false, false, 800, 450,
              "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" },

            { "granulate", "Granulate",
              "The granulator on a sample: up to many grains at once, each with its own envelope, from a "
              "position you set (or click on the waveform), sprayed around it within a window, some of them "
              "reversed, with a note envelope, amplitude modulation, pitch dispersion and stereo spread. Its "
              "'presets' are audio files - any file can be read as raw audio too.",
              0xff7fb2ff, ModuleSection::Oscillator, true, false, false, false, 770, 420,
              "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" },

            { "combweave", "CombWeave",
              "Two banks of up to 128 sine partials on every note - spread linearly, exponentially or as "
              "harmonics, folded back (Wrap / Mirror) or note-locked - each with its own envelope and "
              "high-pass, summed into a hard limit.",
              0xff00c4aa, ModuleSection::Oscillator, true, false, false, false, 700, 787, "" },

            { "grainfreeze", "GrainFreeze",
              "The phase-vocoder player: load a sound, stretch it in time or freeze it on one spot (Spectral "
              "or Cepstral, with glide), scrub its waveform. Keys play it pitched from C4. Its 'presets' are "
              "audio files.",
              0xffa8d8ff, ModuleSection::Oscillator, true, false, false, false, 900, 650,
              "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" },

            { "audiostretcher", "AudioStretcher",
              "Offline pitch and speed with the Rubber Band Library: load a sound, choose a region, set "
              "Pitch, Fine and Speed (and fades) independently, press Play - it renders the result in the "
              "background (the card says 'Loading result') and plays it from Audio Out. Export writes the "
              "file. The loaded sound is saved in the patch.",
              0xfff4a7c0, ModuleSection::Oscillator, false, false, false, false, 400, 690, "" },

            { "tabla", "Tabla",
              "A physically modelled tabla: the bayan (bass) and dayan (treble) as modal skins with syahi loading, "
              "tension chirp, body and air resonances, sympathetic ringing. The bols sit on the keys from C3 up "
              "(Na, Ta, Tin, Tun, Te, Ti, Ge, Ghe, Dha, Dhin, Dhage, Tirakita); click or drag the drums to play them, "
              "Slide Mode bends the pitch like a palm sliding over the skin.",
              0xffcc785c, ModuleSection::Oscillator, true, false, false, false, 560, 620, "" },

            { "samplefield", "SampleField",
              "A field of samples: every held key plays them one after another at random, with random pan, "
              "rate and level, a time limit and an echo per voice. Its 'presets' are audio files (each adds "
              "a sample to the field).",
              0xff00c8e0, ModuleSection::Oscillator, true, false, false, false, 480, 560,
              "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" },

            { "fm12", "FM12",
              "The 12-operator PM/FM synth: a 12 x 12 modulation matrix with self-feedback on its diagonal, "
              "phase modulation, linear and exponential FM (with through-zero), an ADSR, delay and cancel per "
              "operator, an LFO and pitch envelope, user waves and a stereo chorus. DX7 Mode gives DX7-style envelope curves and feedback; "
              "its DX7 menu makes new sounds from the app's DX7 voices or imports a voice from a .syx.",
              0xff3ec6c0, ModuleSection::Oscillator, true, false, false, false, 976, 745, "*.fm12preset;*.syx;*.SYX" },

            { "additivekeys", "Additive Keys",
              "An additive synth laid out like a K5: two sources of 63 harmonics each, drawn by finger and "
              "animated by four envelopes, a dynamic filter that scales the harmonics, a formant shape, pitch "
              "and amplitude envelopes and an LFO. Its preset maker rolls new sounds in twelve kinds.",
              0xffe0a95a, ModuleSection::Oscillator, true, false, false, false, 1100, 780, "*.akpreset" },

            { "frequencydrawer", "Frequency Drawer",
              "Draw sound on a canvas and it plays from left to right: time across, pitch up the side. Two "
              "engines (the first with an interpolated drawing mode), a frequency blur reverb, harmonics, a "
              "draggable playhead and FLAC export. The drawing is saved with the patch.",
              0xff8f7ae6, ModuleSection::Oscillator, false, false, false, false, 1020, 660, "" },

            { "fruitgui", "Fruit DAW",
              "An FL Studio-style song workspace on one card, in four windows: 99 synth lanes (any sound generation module "
              "or synth plugin, bound to its lane), a piano roll with 99 patterns (each playing a lane of its choosing; the "
              "keys play the one shown), a playlist of 99 tracks to place and trim the patterns on, with Knob Record clips "
              "that move a knob along a drawn curve, and a mixer of 99 inserts and the master with ten effects each, a volume "
              "and up to nine sends with their own levels (00 the master; a send at 0 % is a sidechain only). Render plays "
              "its song. Its patch version is the four windows as four cards.",
              0xff1fd5c9, ModuleSection::Oscillator, true, false, false, false, 1600, 900, "" },

            { "aquanode", "Aquanode Modular",
              "Aquanode Modular itself on one card: a whole patch of its own, with its sidebar, keys, Mutator and Render, "
              "saved inside the card. Audio In feeds its Audio In modules and MIDI In (or the keys) plays it - a patch "
              "inside the patch. One can hold another, one level deep.",
              0xff38b6d8, ModuleSection::Oscillator, true, false, true, false, 1200, 760, "" },

            { "83chorusverb", "83ChorusVerb",
              "VirtualDX7's effects as a rack: Chorus, Delay, Phaser and Reverb (with Freeze), each with "
              "its own switch.",
              0xff7fd1c9, ModuleSection::Effect, false, false, true, false, 760, 400, "" },

            { "alphabetafx", "AlphaBetaFX",
              "AlphaBeta's drive, liquid filter (with its envelope, on the input level or on MIDI In) and "
              "chorus as an effect, in both models: Native and Original.",
              0xff8ec9e8, ModuleSection::Effect, true, false, true, false, 900, 240, "" },

            { "automorpheq", "AutoMorphEQ",
              "Seven bell filters, each morphing on its own between a start and an end frequency and level "
              "in one of seven shapes, with Wet Only and a hard limit.",
              0xff00b3b3, ModuleSection::Effect, false, false, true, false, 1100, 700, "" },

            { "centercomb", "CenterComb",
              "Up to 129 bell filters mirrored around a centre frequency, with its response display, "
              "Mirror / Wrap modes and smoothing.",
              0xffc77dd9, ModuleSection::Effect, false, false, true, false, 600, 550, "" },

            { "floweq", "FlowEQ",
              "Three bell filters whose frequency, gain and Q follow hand-drawn curves (or wavetables), "
              "with gesture recording straight from the graph.",
              0xff3fc1c9, ModuleSection::Effect, false, false, true, false, 900, 600, "" },

            { "fullfilter", "FullFilter",
              "Up to 128 bell filters on the harmonics of the played note - an EQ in the shape of a "
              "sawtooth - up to twelve notes at once, with bar editors for every bell, acoustic shapes and "
              "wavetable import. MIDI In plays it.",
              0xffff8a00, ModuleSection::Effect, true, false, true, false, 900, 500, "" },

            { "precisionutility", "PrecisionUtility",
              "The channel utility: up to 10 s of artificial latency (a plain delay of the signal, no echo), "
              "a polarity invert you can blend (50 % cancels, 100 % flips), a volume per channel and where "
              "each input channel goes in the stereo field. Bit for bit transparent at its defaults.",
              0xff00a3a3, ModuleSection::Effect, false, false, true, false, 560, 500, "" },

            { "resonate", "Resonate",
              "The seven-voice tuned resonator with its four engines (Modelled, Native, Crisp, Dark): "
              "resonator I sets the pitch, the others are tuned around it in semitones, each with its own "
              "decay, colour, gain and pan; filter, width, Full / Odd shape, Const decay and dry / wet. "
              "Notes on MIDI In retune it. Loads its .xml presets.",
              0xff3fc9a8, ModuleSection::Effect, true, false, true, false, 900, 560, "*.xml" },

            { "springer", "Springer",
              "The spring reverb of up to seven coupled coils: each a delay with dispersion all-passes (up to 256 "
              "stages), density smearing, damping and feedback, from dub-style springiness to phasing laser sounds. "
              "Coil Scale sets each coil's length, Pitch makes the coils thicker and deeper.",
              0xff00c3cd, ModuleSection::Effect, false, false, true, false, 650, 480, "" },

            { "granulatefx", "Granulate FX",
              "Granulate on live audio: the incoming sound runs through a ring buffer of the length you set, "
              "and grains are taken from it - number, size, position, spray, window, reversed grains, each "
              "grain's envelope, amplitude modulation, pitch dispersion and stereo spread.",
              0xff7fb2ff, ModuleSection::Effect, false, false, true, false, 770, 420, "" },

            { "phorest", "Phorest",
              "The deep phaser: up to 48 allpass stages swept by seven LFO shapes (or by hand), with feedback, "
              "stereo offset, detune and Dry Cancel.",
              0xff00c957, ModuleSection::Effect, false, false, true, false, 550, 550, "" },

            { "tapelectric", "TapElectric",
              "Tape deck character: electric hum with six harmonics, shaped tape noise, drift, wobble and "
              "saturation on the input.",
              0xffd98c4a, ModuleSection::Effect, false, false, true, false, 580, 620, "" },

            { "vocode", "Vocode",
              "The filter-bank vocoder with up to 256 bands and drawn band curves. Carrier on Audio In, "
              "the voice (modulator) on Mod In - or its auto-vocode mode.",
              0xffb07cf0, ModuleSection::Effect, false, false, true, true, 980, 520, "*.vpreset" },

            { "bandpassmod", "Bandpass Modulator",
              "AutoMorph Bandpass Modulator: a band-pass that jumps around a frequency range in time with the song - "
              "to random places by default, from a fixed seeded generator, so the same bar always moves the same way - "
              "or along sine, triangle, saw and step shapes; Skew pushes the jumps back and forth in time. Note Lock keeps "
              "it on chosen notes or on the notes held on MIDI In; distortion, a tempo echo and autopan on the band.",
              0xff2fd1c0, ModuleSection::Effect, true, false, true, false, 990, 700, "" },

            { "spectralcompress", "SpectralCompress",
              "A compressor on every FFT bin: each bin is pulled down (or pushed up) toward a target curve you "
              "draw across the spectrum, with offsets, ratio and knee per direction, attack, release, FFT size "
              "and overlap. Side In can drive the bins instead of the input.",
              0xff4fc3a1, ModuleSection::Effect, false, false, true, true, 1240, 780, "", false, "Side In" },

            { "vitafx", "Vita FX",
              "Vita's effects rack on its own, for any sound: Audio In runs through the chorus, compressor, delay, "
              "distortion, equalizer, filter, flanger, phaser and reverb in the order you drag them to, with Vita's own "
              "panel for them (only the effects page). Loads .vital presets (their effects); MIDI In still plays "
              "Vita's voices into the same chain.",
              0xff7b8cde, ModuleSection::Effect, true, false, true, false, 1260, 738, "*.vital", true },

            { "spectralfilter", "SpectralFilter",
              "A spectral processor you draw: the input's spectrum shown live, and per FFT bin (20 Hz - 20 kHz) a gain "
              "curve (+-24 dB), a phase curve, a frequency shift and a pan, each with its own Strength, Shift and Auto "
              "scrolling (bins per second), a global shift with Wrap, a Frequency Window that limits where it all acts, "
              "Wet Only to hear the change alone, FFT Size 1024-8192, and Export IR to save the curves as an impulse response.",
              0xff5fd0a8, ModuleSection::Effect, false, false, true, false, 860, 700, "", false },

            { "spectralstereoize", "SpectralStereoize",
              "Mid/side width per FFT bin: draw how wide each frequency should be, from mono to extra wide, on a "
              "butterfly plot of the output (green) and of Side In (pink) as a reference. Bit for bit transparent "
              "with the curve flat.",
              0xff6fdc8c, ModuleSection::Effect, false, false, true, true, 960, 540, "", false, "Side In" },

            { "spectralcompare", "SpectralCompare",
              "Two spectra over each other - the sound on Audio In and the one on Side In - to see where they clash, "
              "and a morph between them: Morph blends the input's spectrum toward the side's, Clarity how cleanly, "
              "each with its own smoothing speed; Hear Delta Only plays only their difference, and a gate and an "
              "enhancer work on chosen bins.",
              0xffd96fb8, ModuleSection::Effect, false, false, true, true, 1220, 660, "", false, "Side In" },

            { "ecosee", "EcoSee",
              "The ecological visualizer: spread / entropy, tone map, FM / AM cube, cepstrogram, vocal signature "
              "and a custom 3D grid of what passes through, made with bird calls and field recordings in mind. "
              "Audio passes through unchanged.",
              0xff7fc96b, ModuleSection::Effect, false, false, true, false, 1200, 700, "" },
        };
        return infos;
    }

    const BaseInfo* findInfo (const juce::String& id)
    {
        for (const auto& i : baseInfos())
            if (id == i.id)
                return &i;
        return nullptr;
    }

    std::map<juce::String, std::function<juce::AudioProcessor*()>>& creators()
    {
        static std::map<juce::String, std::function<juce::AudioProcessor*()>> m;
        return m;
    }

    struct PendingPatch { PatchBuildFn build; PresetLoadFn load; };

    // (6.9.17) the further kinds of a patch version
    struct PatchKinds { juce::String firstName; std::vector<std::pair<juce::String, PatchBuildFn>> more; };
    std::map<juce::String, PatchKinds>& patchKindsById()
    {
        static std::map<juce::String, PatchKinds> m;
        return m;
    }
    // the builder of the kind asked for (patchKind()), else the plugin's own
    const PatchBuildFn& builderFor (const CatalogEntry& e)
    {
        const int k = patchKind();
        if (k > 0)
            if (auto it = patchKindsById().find (e.id); it != patchKindsById().end() && k - 1 < (int) it->second.more.size())
                return it->second.more[(size_t) (k - 1)].second;
        return e.buildPatch;
    }
    std::map<juce::String, PendingPatch>& pendingPatchVersions()
    {
        static std::map<juce::String, PendingPatch> m;
        return m;
    }
}

//==============================================================================
std::vector<CatalogEntry>& catalog()
{
    static std::vector<CatalogEntry> entries = []
    {
        std::vector<CatalogEntry> v;
        for (const auto& i : baseInfos())
        {
            CatalogEntry e;
            e.id = i.id;
            e.name = i.name;
            e.guiTypeId = juce::String ("plugin.") + i.id;
            e.presetPatterns = i.presetPatterns;
            e.thirdParty = i.thirdParty;
            v.push_back (e);
        }
        return v;
    }();

    // patch versions registered during static initialisation, whichever ran first
    auto& pending = pendingPatchVersions();
    if (! pending.empty())
    {
        for (auto& e : entries)
            if (auto it = pending.find (e.id); it != pending.end())
            {
                e.buildPatch = it->second.build;
                e.loadPreset = it->second.load;
            }
        pending.clear();
    }
    return entries;
}

CatalogEntry* findEntry (const juce::String& id)
{
    for (auto& e : catalog())
        if (e.id == id)
            return &e;
    return nullptr;
}

HostedSpec baseSpec (const juce::String& id)
{
    HostedSpec s;
    if (const auto* i = findInfo (id))
    {
        s.pluginId = i->id;
        s.typeId = juce::String ("plugin.") + i->id;
        s.displayName = i->name;
        s.description = i->description;
        s.accent = juce::Colour (i->accent);
        s.section = i->section;
        s.midiIn = i->midiIn;
        s.midiOut = i->midiOut;
        s.audioIn = i->audioIn;
        s.sideIn = i->sideIn;
        s.defaultWidth = i->w;
        s.defaultHeight = i->h;
        if (i->sideInLabel != nullptr)
            s.sideInLabel = i->sideInLabel;
    }
    jassert (s.pluginId.isNotEmpty());
    return s;
}

bool registerCreate (const juce::String& id, std::function<juce::AudioProcessor*()> create)
{
    creators()[id] = std::move (create);
    return true;
}

namespace
{
    std::map<juce::String, AudioAccess>& audioAccessors()
    {
        static std::map<juce::String, AudioAccess> m;
        return m;
    }
}

bool registerAudioAccess (const juce::String& id, AudioAccess access)
{
    audioAccessors()[id] = std::move (access);
    return true;
}

const AudioAccess* audioAccessFor (const juce::String& id)
{
    auto it = audioAccessors().find (id);
    return it != audioAccessors().end() ? &it->second : nullptr;
}

bool registerPatchKind (const juce::String& id, const juce::String& firstName, const juce::String& name, PatchBuildFn build)
{
    auto& k = patchKindsById()[id];
    k.firstName = firstName;
    k.more.push_back ({ name, std::move (build) });
    return true;
}

juce::StringArray patchKinds (const juce::String& id)
{
    juce::StringArray names;
    if (auto it = patchKindsById().find (id); it != patchKindsById().end())
    {
        names.add (it->second.firstName);
        for (const auto& [name, fn] : it->second.more) names.add (name);
    }
    return names;
}

int& patchKind()
{
    static int kind = 0;
    return kind;
}

bool registerPatchVersion (const juce::String& id, PatchBuildFn build, PresetLoadFn load)
{
    // catalog() may not exist yet during static initialisation: park it
    pendingPatchVersions()[id] = { std::move (build), std::move (load) };
    return true;
}

std::unique_ptr<juce::AudioProcessor> createOriginal (const juce::String& id, double sampleRate)
{
    auto it = creators().find (id);
    if (it == creators().end() || it->second == nullptr)
        return nullptr;

    std::unique_ptr<juce::AudioProcessor> p (it->second());
    if (p != nullptr)
    {
        p->setRateAndBufferSizeDetails (sampleRate, 512);
        p->prepareToPlay (sampleRate, 512);
    }
    return p;
}

//==============================================================================
namespace
{
    int findFirst (AquanodeModularAudioProcessor& patch, const juce::String& typeId)
    {
        for (int id : patch.getModuleIds())
            if (auto* inst = patch.getInstance (id))
                if (inst->descriptor().typeId == typeId)
                    return id;
        return -1;
    }

    // right of everything already in the patch
    juce::Point<int> freeSpot (AquanodeModularAudioProcessor& patch)
    {
        int right = 40, top = 60;
        bool any = false;
        for (int id : patch.getModuleIds())
            if (auto* inst = patch.getInstance (id))
            {
                // right of everything already there (cards wider than the default
                // say so; rows of many knobs widen a card too)
                std::map<int, int> units;
                for (const auto& spec : inst->descriptor().params)
                    if (! spec.hidden)
                        units[spec.row] += juce::jlimit (1, 5, spec.widthUnits);
                int most = 5;
                for (const auto& u : units) most = juce::jmax (most, u.second);
                const int w = juce::jmax (inst->dsp->preferredModuleWidth(), 250, 20 + most * 46);
                right = juce::jmax (right, inst->position.x + w + 80);
                top = any ? juce::jmin (top, inst->position.y) : inst->position.y;
                any = true;
            }
        return { right, top };
    }
}

namespace
{
    // An effect plugin added to the instrument build is not given an Audio In
    // of its own (it plays what you patch into it, or drop it onto a cable);
    // only the FX build, where Audio In is the track, makes one. An Audio In
    // the patch already has is still wired up.
    bool makesAudioIn (const AquanodeModularAudioProcessor& patch)
    {
        juce::ignoreUnused (patch);
       #if AQUANODE_FX_BUILD
        return true;
       #else
        return false;
       #endif
    }
}

int addGuiVersion (AquanodeModularAudioProcessor& patch, const juce::String& id, juce::Point<int> where)
{
    auto* e = findEntry (id);
    if (e == nullptr)
        return -1;

    const int card = patch.addModule (e->guiTypeId, where);
    if (card < 0)
        return -1;

    auto* inst = patch.getInstance (card);
    const auto& d = inst->descriptor();

    // the card plays straight into Audio Out (made if the patch has none);
    // an effect card takes its input from Audio In
    int out = findFirst (patch, "io.audioOut");
    if (out < 0)
        out = patch.addModule ("io.audioOut", where + juce::Point<int> (inst->dsp->preferredModuleWidth() + 60, 0));
    if (out >= 0)
        patch.addCable (card, "audioOut", out, "audioIn");

    if (d.inputIndexOf ("audioIn") >= 0 && d.section == ModuleSection::Effect)
    {
        int in = findFirst (patch, "io.audioIn");
        if (in < 0 && makesAudioIn (patch))
            in = patch.addModule ("io.audioIn", where - juce::Point<int> (300, 0));
        if (in >= 0)
            patch.addCable (in, "audioOut", card, "audioIn");
    }
    return card;
}

namespace
{
    // The ports of a freshly built patch version: its output into Audio Out
    // (made if missing) and, for an effect, Audio In into its input.
    void wireToPatchIO (AquanodeModularAudioProcessor& patch, const PatchPorts& ports, bool isEffect)
    {
        if (! ports.audioOut.empty())
        {
            int out = findFirst (patch, "io.audioOut");
            if (out < 0)
            {
                auto pos = juce::Point<int> (60, 60);
                if (auto* src = patch.getInstance (ports.audioOut.front().module))
                    pos = src->position + juce::Point<int> (320, 0);
                out = patch.addModule ("io.audioOut", pos);
            }
            for (const auto& o : ports.audioOut)
                patch.addCable (o.module, o.socket, out, "audioIn");
        }

        if (isEffect && ! ports.audioIn.empty())
        {
            int in = findFirst (patch, "io.audioIn");
            if (in < 0 && ! makesAudioIn (patch))
                return;
            if (in < 0)
            {
                auto pos = juce::Point<int> (60, 60);
                if (auto* dst = patch.getInstance (ports.audioIn.front().module))
                    pos = dst->position - juce::Point<int> (320, 0);
                in = patch.addModule ("io.audioIn", pos);
            }
            for (const auto& i : ports.audioIn)
                patch.addCable (in, "audioOut", i.module, i.socket);
        }
    }

    bool isEffectPlugin (const juce::String& id)
    {
        const auto* i = findInfo (id);
        return i != nullptr && i->section == ModuleSection::Effect;
    }
}

bool isEffectPluginId (const juce::String& id) { return isEffectPlugin (id); }

bool isThirdPartyTypeId (const juce::String& typeId)
{
    if (! typeId.startsWith ("plugin."))
        return false;
    const auto id = typeId.fromFirstOccurrenceOf ("plugin.", false, false).upToFirstOccurrenceOf (".", false, false);
    const auto* i = findInfo (id);
    return i != nullptr && i->thirdParty;
}

juce::String addPatchVersion (AquanodeModularAudioProcessor& patch, const juce::String& id,
                              const juce::File& presetFile, bool replace)
{
    auto* e = findEntry (id);
    if (e == nullptr || ! e->hasPatchVersion())
        return {};

    auto original = createOriginal (id, 48000.0);
    if (original == nullptr)
        return {};

    juce::String presetName;
    if (presetFile != juce::File())
    {
        if (e->loadPreset == nullptr || ! e->loadPreset (*original, presetFile))
            return {};
        presetName = presetFile.getFileNameWithoutExtension();
    }

    const bool effect = isEffectPlugin (id);
    juce::String name;

    if (replace)
    {
        patch.generatePatch ([&] (AquanodeModularAudioProcessor& p)
        {
            const auto ports = builderFor (*e) (p, *original, { 60, 60 });
            wireToPatchIO (p, ports, effect);
            name = ports.name;
            return name;
        });
    }
    else
    {
        const auto ports = builderFor (*e) (patch, *original, freeSpot (patch));
        wireToPatchIO (patch, ports, effect);
        name = ports.name;
    }

    if (presetName.isNotEmpty())
        name = e->name + " - " + presetName;
    if (replace)
        patch.patchName = name;
    return name;
}

juce::String convertGuiToPatch (AquanodeModularAudioProcessor& patch, int guiInstanceId)
{
    auto* inst = patch.getInstance (guiInstanceId);
    if (inst == nullptr)
        return {};

    auto* hosted = dynamic_cast<HostedPluginModule*> (inst->dsp.get());
    if (hosted == nullptr)
        return {};

    auto* e = findEntry (hosted->getSpec().pluginId);
    if (e == nullptr || ! e->hasPatchVersion())
        return {};

    // a copy of the card's sound in a fresh instance, so the card itself is
    // never touched by the builder
    auto original = createOriginal (e->id, 48000.0);
    if (original == nullptr)
        return {};
    {
        juce::MemoryBlock state;
        hosted->getPlugin()->getStateInformation (state);
        original->setStateInformation (state.getData(), (int) state.getSize());
    }

    // audio the plugin keeps outside its state (Slicer's sample) goes along too
    if (auto* access = audioAccessFor (e->id); access != nullptr && access->get && access->set)
    {
        double rate = 44100.0;
        if (auto audio = access->get (*hosted->getPlugin(), rate))
            access->set (*original, std::make_shared<juce::AudioBuffer<float>> (*audio), rate);
    }

    const auto where = inst->position;

    // the card's cables, to rewire its replacement the same way
    std::vector<CableInfo> touching;
    for (const auto& c : patch.getCables())
        if (c.fromModule == guiInstanceId || c.toModule == guiInstanceId)
            touching.push_back (c);

    patch.removeModule (guiInstanceId);
    const auto ports = builderFor (*e) (patch, *original, where);

    auto reconnect = [&patch] (const std::vector<SocketRef>& targets, int fromModule, const juce::String& fromSocket,
                               bool targetsAreInputs)
    {
        for (const auto& t : targets)
        {
            if (targetsAreInputs) patch.addCable (fromModule, fromSocket, t.module, t.socket);
            else                  patch.addCable (t.module, t.socket, fromModule, fromSocket);
        }
    };

    for (const auto& c : touching)
    {
        if (c.toModule == guiInstanceId)   // something fed the card
        {
            if (c.toSocket == "audioIn") reconnect (ports.audioIn, c.fromModule, c.fromSocket, true);
            if (c.toSocket == "sideIn")  reconnect (ports.sideIn,  c.fromModule, c.fromSocket, true);
            if (c.toSocket == "midiIn")  reconnect (ports.midiIn,  c.fromModule, c.fromSocket, true);
        }
        else                               // the card fed something
        {
            if (c.fromSocket == "audioOut") reconnect (ports.audioOut, c.toModule, c.toSocket, false);
            if (c.fromSocket == "midiOut")  reconnect (ports.midiOut,  c.toModule, c.toSocket, false);
        }
    }
    return ports.name;
}

int openHeldPresetInPlugin (AquanodeModularAudioProcessor& patch, int instanceId)
{
    auto* inst = patch.getInstance (instanceId);
    if (inst == nullptr)
        return -1;
    auto* holder = dynamic_cast<randompreset::PluginPresetHolder*> (inst->dsp.get());
    auto* plugin = holder != nullptr ? holder->heldPlugin() : nullptr;
    auto* e = holder != nullptr ? findEntry (holder->heldPluginId()) : nullptr;
    if (plugin == nullptr || e == nullptr)
        return -1;

    // the sound, the place and the cables, before the module goes
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    const auto where = inst->position;
    std::vector<CableInfo> touching;
    for (const auto& c : patch.getCables())
        if (c.fromModule == instanceId || c.toModule == instanceId)
            touching.push_back (c);

    patch.removeModule (instanceId);
    const int card = patch.addModule (e->guiTypeId, where);
    if (card < 0)
        return -1;
    if (auto* h = dynamic_cast<HostedPluginModule*> (patch.getInstance (card)->dsp.get()))
        if (auto* p = h->getPlugin(); p != nullptr && state.getSize() > 0)
            p->setStateInformation (state.getData(), (int) state.getSize());

    // the module's sockets are the card's: Midi In and Audio Out
    for (const auto& c : touching)
    {
        if (c.toModule == instanceId)
            patch.addCable (c.fromModule, c.fromSocket, card, c.toSocket);
        else
            patch.addCable (card, c.fromSocket, c.toModule, c.toSocket);
    }
    return card;
}

std::vector<int> openPluginsBeside (AquanodeModularAudioProcessor& patch, int besideInstance,
                                    const std::vector<std::pair<juce::String, juce::MemoryBlock>>& pluginsAndStates)
{
    std::vector<int> made;
    auto* src = patch.getInstance (besideInstance);
    if (src == nullptr)
        return made;
    auto where = src->position + juce::Point<int> (src->dsp->preferredModuleWidth() + 40, 0);
    std::vector<CableInfo> touching;
    for (const auto& c : patch.getCables())
        if (c.fromModule == besideInstance || c.toModule == besideInstance)
            touching.push_back (c);

    for (const auto& [id, state] : pluginsAndStates)
    {
        auto* e = findEntry (id);
        if (e == nullptr)
            continue;
        const int card = patch.addModule (e->guiTypeId, where);
        if (card < 0)
            continue;
        auto* inst = patch.getInstance (card);
        if (auto* h = dynamic_cast<HostedPluginModule*> (inst->dsp.get()))
            if (auto* p = h->getPlugin(); p != nullptr && state.getSize() > 0)
                p->setStateInformation (state.getData(), (int) state.getSize());
        bool plays = false;
        for (const auto& c : touching)
        {
            if (c.fromModule == besideInstance && c.fromSocket == "audioOut")
            {
                patch.addCable (card, "audioOut", c.toModule, c.toSocket);
                plays = true;
            }
            else if (c.toModule == besideInstance && c.toSocket == "midiIn" && inst->descriptor().inputIndexOf ("midiIn") >= 0)
                patch.addCable (c.fromModule, c.fromSocket, card, "midiIn");
        }
        if (! plays)
        {
            int out = findFirst (patch, "io.audioOut");
            if (out < 0)
                out = patch.addModule ("io.audioOut", where + juce::Point<int> (inst->dsp->preferredModuleWidth() + 60, 0));
            if (out >= 0)
                patch.addCable (card, "audioOut", out, "audioIn");
        }
        made.push_back (card);
        where += juce::Point<int> (inst->dsp->preferredModuleWidth() + 40, 0);   // (a second one beside the first)
    }
    return made;
}

} // namespace aquanode::plugins
