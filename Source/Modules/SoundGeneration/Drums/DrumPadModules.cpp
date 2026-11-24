#include "DrumPadModules.h"
#include "../../../Platform/AndroidDocuments.h"

using namespace aquanode;
using namespace aquanode::drumpads;
namespace pd = aquanode::physdrums;

namespace
{
    //==========================================================================
    // The kits' sounds
    //==========================================================================
    struct Type808 { const char* name; const char* shortName; const char* prefix; int knobs; const char* knobA; const char* knobB; double defA, defB; };
    const Type808 k808[16] = {
        { "Bass Drum", "BD", "BD", 2, "Tone", "Decay", 2.5, 5.0 },  { "Snare Drum", "SD", "SD", 2, "Tone", "Snappy", 5.0, 5.0 },
        { "Low Tom", "LT", "LT", 1, "Tuning", "", 5.0, 0.0 },       { "Mid Tom", "MT", "MT", 1, "Tuning", "", 5.0, 0.0 },
        { "High Tom", "HT", "HT", 1, "Tuning", "", 5.0, 0.0 },      { "Low Conga", "LC", "LC", 1, "Tuning", "", 5.0, 0.0 },
        { "Mid Conga", "MC", "MC", 1, "Tuning", "", 5.0, 0.0 },     { "High Conga", "HC", "HC", 1, "Tuning", "", 5.0, 0.0 },
        { "Rim Shot", "RS", "RS", 0, "", "", 0, 0 },                { "Claves", "CL", "CL", 0, "", "", 0, 0 },
        { "Hand Clap", "CP", "CP", 0, "", "", 0, 0 },               { "Maracas", "MA", "MA", 0, "", "", 0, 0 },
        { "Cowbell", "CB", "CB", 0, "", "", 0, 0 },                 { "Cymbal", "CY", "CY", 2, "Tone", "Decay", 5.0, 5.0 },
        { "Open Hihat", "OH", "OH", 1, "Decay", "", 2.5, 0.0 },     { "Closed Hihat", "CH", "CH", 0, "", "", 0, 0 } };

    // which 808 type each pad starts with, and which 909 preset
    const int k808Default[kPads] = { 0, 1, 10, 15, 2, 3, 4, 14, 5, 6, 7, 13, 8, 9, 11, 12 };
    const char* k909Default[kPads] = { "909/bd01", "909/sd01", "909/cp01", "909/hh01", "909/lt01", "909/mt01", "909/ht01", "909/oh01",
                                       "909/rs01", "909/cr01", "909/rd01", "909/bd05", "909/sd08", "909/cp03", "909/hh03", "909/oh03" };

    const std::vector<const pd::Preset*>& presets909()
    {
        static const std::vector<const pd::Preset*> v = []
        {
            std::vector<const pd::Preset*> r;
            for (const auto& p : pd::allPresets())
                if (juce::String (p.key).startsWith ("909/")) r.push_back (&p);
            return r;
        }();
        return v;
    }

    int index909 (const char* key)
    {
        const auto& v = presets909();
        for (size_t i = 0; i < v.size(); ++i) if (juce::String (v[i]->key) == key) return (int) i;
        return 0;
    }

    juce::String short909 (const pd::Preset& p)
    {
        const juce::String k (p.key);
        return k.substring (4, 6).toUpperCase() + k.substring (6);
    }

    // the 808's knob grid: the fitted recordings at 0, 1, 2.5, 5 and 7.5 on the dial
    pd::Preset interpolate808 (int type, double a, double b)
    {
        static const double ax[5] = { 0.0, 1.0, 2.5, 5.0, 7.5 };
        static const char* axs[5] = { "00", "10", "25", "50", "75" };
        const auto& t = k808[juce::jlimit (0, 15, type)];
        auto idx = [] (double v, int& i, double& f)
        {
            v = juce::jlimit (0.0, 7.5, v);
            i = 0;
            while (i < 3 && v > ax[i + 1]) ++i;
            f = (v - ax[i]) / (ax[i + 1] - ax[i]);
        };
        std::vector<std::pair<const pd::Preset*, double>> corners;
        auto add = [&corners] (const juce::String& key, double w)
        {
            if (w <= 1.0e-9) return;
            if (const auto* p = pd::findPreset (key.toStdString())) corners.push_back ({ p, w });
        };
        if (t.knobs == 2)
        {
            int i, j; double fa, fb;
            idx (a, i, fa); idx (b, j, fb);
            const juce::String pre = juce::String ("808/") + t.prefix;
            add (pre + axs[i] + axs[j], (1 - fa) * (1 - fb));
            add (pre + axs[i + 1] + axs[j], fa * (1 - fb));
            add (pre + axs[i] + axs[j + 1], (1 - fa) * fb);
            add (pre + axs[i + 1] + axs[j + 1], fa * fb);
        }
        else if (t.knobs == 1)
        {
            int i; double fa;
            idx (a, i, fa);
            const juce::String pre = juce::String ("808/") + t.prefix;
            add (pre + axs[i], 1 - fa);
            add (pre + axs[i + 1], fa);
        }
        else
            add (juce::String ("808/") + t.prefix, 1.0);

        if (corners.empty())
            return *pd::findPreset ("808/BD5050");
        pd::Preset out = *corners.front().first;
        double tot = 0.0;
        for (auto& c : corners) tot += c.second;
        const auto& info = pd::paramInfo (out.voice);
        for (size_t k = 0; k < info.size(); ++k)
        {
            bool positive = true;
            for (auto& c : corners) positive = positive && c.first->values[k] > 0.0f;
            double v = 0.0;
            if (positive) { for (auto& c : corners) v += std::log ((double) c.first->values[k]) * c.second / tot; v = std::exp (v); }   // times and frequencies in log
            else          { for (auto& c : corners) v += (double) c.first->values[k] * c.second / tot; }
            out.values[k] = (float) v;
        }
        double nS = 0.0;
        for (auto& c : corners) nS += c.first->nS * c.second / tot;
        out.nS = (float) nS;
        return out;
    }

    //==========================================================================
    // Rendering (background thread)
    //==========================================================================
    std::shared_ptr<PadSound> renderPad (Kit kit, const PadSpec& s, double sr, std::shared_ptr<const PadSample> sample)
    {
        auto out = std::make_shared<PadSound>();
        if (kit == Kit::Samples)
        {
            if (sample == nullptr || sample->l.empty()) return out;
            // played at the pad's tune: read with 4-point interpolation at the new rate
            const double ratio = sample->rate / sr * std::pow (2.0, s.tune / 12.0);
            const int inLen = (int) sample->l.size();
            int outLen = (int) ((double) inLen / ratio);
            const double decay = juce::jlimit (5.0, 400.0, s.decay) / 100.0;
            if (decay < 1.0) outLen = juce::jmax (16, (int) (outLen * decay));
            const bool st = ! sample->r.empty();
            out->stereo = st;
            out->variants = 1;
            auto& L = out->l[0]; auto& R = out->r[0];
            L.resize ((size_t) outLen); if (st) R.resize ((size_t) outLen);
            auto read = [&] (const std::vector<float>& x, double p)
            {
                const int i = (int) p; const double f = p - i;
                auto at = [&] (int k) { return (double) x[(size_t) juce::jlimit (0, inLen - 1, k)]; };
                const double xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
                const double c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
                return ((c3 * f + c2) * f + c1) * f + x0;
            };
            // Tone: a gentle low-pass below the middle, a high-pass above it
            const double tone = juce::jlimit (0.0, 100.0, s.tone) / 100.0;
            const double lpHz = tone < 0.5 ? 400.0 * std::pow (50.0, tone * 2.0) : sr * 0.49;
            const double hpHz = tone > 0.5 ? 20.0 * std::pow (60.0, (tone - 0.5) * 2.0) : 0.0;
            const double aL = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * lpHz / sr);
            const double aH = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * std::max (1.0, hpHz) / sr);
            double zl[2] {}, zh[2] {};
            const int fadeLen = decay < 1.0 ? juce::jmax (8, outLen / 3) : juce::jmax (8, (int) (0.003 * sr));
            for (int i = 0; i < outLen; ++i)
            {
                const double pos = i * ratio;
                const double env = i > outLen - fadeLen ? std::pow ((double) (outLen - i) / fadeLen, decay < 1.0 ? 2.0 : 1.0) : 1.0;
                for (int c = 0; c < (st ? 2 : 1); ++c)
                {
                    double v = read (c == 0 ? sample->l : sample->r, pos);
                    if (tone < 0.5) { zl[c] += aL * (v - zl[c]); v = zl[c]; }
                    if (hpHz > 0.0) { zh[c] += aH * (v - zh[c]); v = v - zh[c]; }
                    (c == 0 ? L : R)[(size_t) i] = (float) (v * env);
                }
            }
            return out;
        }

        pd::Preset preset;
        if (kit == Kit::TR808)
            preset = interpolate808 (s.sound, s.knobA, s.knobB);
        else
        {
            const auto& v = presets909();
            preset = *v[(size_t) juce::jlimit (0, (int) v.size() - 1, s.sound)];
        }
        pd::Mods m;
        m.pitch = std::pow (2.0, s.tune / 12.0);
        m.decay = juce::jlimit (25.0, 400.0, s.decay) / 100.0;
        m.tone = juce::jlimit (0.0, 100.0, s.tone) / 100.0;
        for (int k = 0; k < 3; ++k) m.extra[k] = juce::jlimit (-1.0, 1.0, s.extra[k]);
        out->variants = kVariants;
        for (int v = 0; v < kVariants; ++v)
        {
            const auto r = pd::render (preset, m, sr, 1.0, (uint64_t) (v + 1), true);
            out->l[v].assign (r.l.begin(), r.l.end());
            if (r.stereo) { out->r[v].assign (r.r.begin(), r.r.end()); out->stereo = true; }
        }
        return out;
    }

    juce::ThreadPool& renderPool()
    {
        struct Pool { juce::ThreadPool pool { juce::ThreadPoolOptions().withThreadName ("Drum Renders").withNumberOfThreads (2) }; };
        return shutdownSafe<Pool>().pool;
    }
}

//==============================================================================
const std::vector<KitSound>& aquanode::drumpads::kitSounds (Kit k)
{
    static const std::vector<KitSound> s808 = []
    {
        std::vector<KitSound> v;
        for (const auto& t : k808) v.push_back ({ t.name, t.shortName });
        return v;
    }();
    static const std::vector<KitSound> s909 = []
    {
        std::vector<KitSound> v;
        for (const auto* p : presets909()) v.push_back ({ juce::String (p->name).fromFirstOccurrenceOf ("909 ", false, false), short909 (*p) });
        return v;
    }();
    static const std::vector<KitSound> none;
    return k == Kit::TR808 ? s808 : (k == Kit::TR909 ? s909 : none);
}

juce::String aquanode::drumpads::knobLabel (Kit k, int sound, int which)
{
    if (k != Kit::TR808) return {};
    const auto& t = k808[juce::jlimit (0, 15, sound)];
    return which == 0 ? juce::String (t.knobs >= 1 ? t.knobA : "") : juce::String (t.knobs >= 2 ? t.knobB : "");
}

std::array<juce::String, 3> aquanode::drumpads::physicalLabels (Kit k, int sound)
{
    std::array<juce::String, 3> out;
    if (k == Kit::Samples) return out;
    pd::Voice voice;
    if (k == Kit::TR808)
    {
        const auto p = interpolate808 (sound, 5.0, 5.0);
        voice = p.voice;
    }
    else
    {
        const auto& v = presets909();
        voice = v[(size_t) juce::jlimit (0, (int) v.size() - 1, sound)]->voice;
    }
    const auto& kn = pd::physicalKnobs (voice);
    for (int i = 0; i < 3; ++i)
        out[(size_t) i] = kn[(size_t) i].param[0] != 0 ? juce::String (kn[(size_t) i].label) : juce::String();
    return out;
}

//==============================================================================
ModuleDescriptor PadDrumModule::makeDescriptor (Kit k)
{
    ModuleDescriptor d;
    d.section = ModuleSection::Oscillator;
    if (k == Kit::TR808)
    {
        d.typeId = "osc.tr808";
        d.displayName = "808";
        d.description = "The 808's sixteen voices on 4 x 4 pads, physically modelled (bridged-T resonators, the six square waves, "
                        "the clap's sawtooth envelope) and fitted to recordings of the machine at every knob setting: its own Tone, "
                        "Decay, Snappy and Tuning move between them. Tap a pad to hear and edit it; each pad also has Level, Pan, "
                        "Tune, Decay, Tone and three knobs on the model itself. MIDI In plays the pads from Base Note up.";
    }
    else if (k == Kit::TR909)
    {
        d.typeId = "osc.tr909";
        d.displayName = "909";
        d.description = "The 909's voices on 4 x 4 pads, modelled and fitted to recordings of the machine - kicks, snares, toms, "
                        "clap and rim from their circuits, the hats and cymbals as the 6-bit cymbal recordings the machine plays. "
                        "Every pad can take any of the 71 fitted sounds. Tap a pad to hear and edit it: Level, Pan, Tune, Decay, Tone "
                        "and three knobs on the model. MIDI In plays the pads from Base Note up.";
    }
    else
    {
        d.typeId = "osc.drumpads";
        d.displayName = "Drum Pads";
        d.description = "Sixteen pads for drum sounds you load (wav, aiff, flac, ogg, mp3): LOAD PAD puts one on the selected pad "
                        "(replacing what is there), LOAD MANY up to 16 at once onto the empty pads, in order. "
                        "Each pad has Level, Pan, Tune, Decay (shortens and fades the sound), Tone (darker or thinner) and a choke "
                        "group: pads in the same group (1 - 4) cut each other off - hitting one stops the others still ringing, "
                        "as a closed hi-hat silences the open one; None rings on whatever else plays. The sounds are saved "
                        "with the patch. MIDI In plays the pads from Base Note up.";
    }
    d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
    d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 0));
    d.params.push_back (makeSteppedList ("base", "Base Note", midiNoteNameChoices(), 36, 0).noMod());
    for (int i = 0; i < kPads; ++i)
    {
        int sound = 0;
        double a = 0.0, b = 0.0;
        if (k == Kit::TR808) { sound = k808Default[i]; a = k808[sound].defA; b = k808[sound].defB; }
        if (k == Kit::TR909) sound = index909 (k909Default[i]);
        int choke = 0;
        if (k == Kit::TR808 && (sound == 14 || sound == 15)) choke = 1;
        if (k == Kit::TR909)
        {
            const juce::String key (presets909()[(size_t) sound]->key);
            if (key.contains ("/hh") || key.contains ("/oh")) choke = 1;
        }
        auto hidden = [] (ParamSpec p) { p.hidden = true; p.modulatable = false; return p; };
        const int nSounds = juce::jmax (1, (int) kitSounds (k).size());
        juce::StringArray soundNames;
        for (const auto& s : kitSounds (k)) soundNames.add (s.name);
        if (soundNames.isEmpty()) soundNames.add ("Sample");
        d.params.push_back (hidden (makeCombo (pid (i, "snd"), "Sound", soundNames, juce::jlimit (0, nSounds - 1, sound))));
        d.params.push_back (hidden (makeRotary (pid (i, "lvl"), "Level", 0.0f, 1.0f, 0.8f)));
        d.params.push_back (hidden (makeRotary (pid (i, "pan"), "Pan", -1.0f, 1.0f, 0.0f)));
        d.params.push_back (hidden (makeRotary (pid (i, "tun"), "Tune", -24.0f, 24.0f, 0.0f, 0, "st")));
        d.params.push_back (hidden (makeRotary (pid (i, "dec"), "Decay", 25.0f, 400.0f, 100.0f, 0, "%")));
        d.params.push_back (hidden (makeRotary (pid (i, "ton"), "Tone", 0.0f, 100.0f, 50.0f, 0, "%")));
        d.params.push_back (hidden (makeRotary (pid (i, "ka"), "Knob A", 0.0f, 7.5f, (float) a)));
        d.params.push_back (hidden (makeRotary (pid (i, "kb"), "Knob B", 0.0f, 7.5f, (float) b)));
        d.params.push_back (hidden (makeRotary (pid (i, "x1"), "Model 1", -1.0f, 1.0f, 0.0f)));
        d.params.push_back (hidden (makeRotary (pid (i, "x2"), "Model 2", -1.0f, 1.0f, 0.0f)));
        d.params.push_back (hidden (makeRotary (pid (i, "x3"), "Model 3", -1.0f, 1.0f, 0.0f)));
        d.params.push_back (hidden (makeCombo (pid (i, "chk"), "Choke", { "None", "1", "2", "3", "4" }, choke)));
    }
    d.params.push_back (makeOutputTrim (-3.0f));
    return d;
}

PadDrumModule::PadDrumModule (Kit k) : kitKind (k), slots (std::make_shared<RenderSlots>())
{
    for (auto& f : flash) f.store (0.0f);
    for (auto& r : retireFull) r.store (false);
    startTimerHz (12);
}

PadDrumModule::~PadDrumModule()
{
    stopTimer();
    slots->alive = false;
}

void PadDrumModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    pBase = paramIndex ("base");
    pLevel = paramIndex ("level");
    for (int i = 0; i < kPads; ++i)
    {
        pPadLevel[i] = paramIndex (pid (i, "lvl"));
        pPadPan[i] = paramIndex (pid (i, "pan"));
        pPadChoke[i] = paramIndex (pid (i, "chk"));
    }
    renderRate.store (sr);
    reset();
}

void PadDrumModule::reset()
{
    for (auto& v : voices)
        if (v.active) { v.active = false; retireSound (std::move (v.snd)); }
}

PadSpec PadDrumModule::padSpec (int pad) const
{
    PadSpec s;
    auto g = [this, pad] (const char* w) { return (double) getParameter (pid (pad, w)); };
    s.sound = (int) std::lround (g ("snd"));
    s.level = g ("lvl"); s.pan = g ("pan"); s.tune = g ("tun"); s.decay = g ("dec"); s.tone = g ("ton");
    s.knobA = g ("ka"); s.knobB = g ("kb");
    s.extra[0] = g ("x1"); s.extra[1] = g ("x2"); s.extra[2] = g ("x3");
    s.choke = (int) std::lround (g ("chk"));
    return s;
}

juce::uint64 PadDrumModule::specKey (int pad) const
{
    // what the render depends on (not Level, Pan or Choke, which the voices apply)
    const auto s = padSpec (pad);
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (double v) { juce::int64 q = (juce::int64) std::llround (v * 10000.0); h = (h ^ (juce::uint64) q) * 1099511628211ull; };
    mix (s.sound); mix (s.tune); mix (s.decay); mix (s.tone); mix (s.knobA); mix (s.knobB);
    mix (s.extra[0]); mix (s.extra[1]); mix (s.extra[2]); mix (renderRate.load());
    if (kitKind == Kit::Samples)
    {
        const juce::SpinLock::ScopedLockType sl (slots->lock);
        mix ((double) (juce::pointer_sized_int) slots->sample[pad].get());
    }
    return h;
}

void PadDrumModule::requestRenders (bool force)
{
    const double sr = renderRate.load();
    if (sr <= 0.0) return;
    for (int pad = 0; pad < kPads; ++pad)
    {
        const auto key = specKey (pad);
        if (! force && key == requested[pad]) continue;
        requested[pad] = key;
        slots->wanted[pad].store (key);
        const auto spec = padSpec (pad);
        std::shared_ptr<const PadSample> sample;
        {
            const juce::SpinLock::ScopedLockType sl (slots->lock);
            sample = slots->sample[pad];
        }
        auto target = slots;
        const auto kit = kitKind;
        renderPool().addJob ([target, pad, key, spec, sr, kit, sample]
        {
            if (! target->alive || target->wanted[pad].load() != key) return;   // superseded by a newer setting
            std::shared_ptr<const PadSound> fresh = renderPad (kit, spec, sr, sample);
            if (! target->alive || target->wanted[pad].load() != key) return;
            {
                const juce::SpinLock::ScopedLockType sl (target->lock);
                std::swap (target->sound[pad], fresh);
            }
            // (the old sound is let go here, on this thread)
        });
    }
}

void PadDrumModule::timerCallback()
{
    requestRenders (false);
    for (int i = 0; i < kRetire; ++i)
        if (retireFull[(size_t) i].load (std::memory_order_acquire))
        {
            retire[(size_t) i].reset();
            retireFull[(size_t) i].store (false, std::memory_order_release);
        }
}

void PadDrumModule::retireSound (std::shared_ptr<const PadSound>&& s)
{
    if (s == nullptr) return;
    for (int i = 0; i < kRetire; ++i)
        if (! retireFull[(size_t) i].load (std::memory_order_acquire))
        {
            retire[(size_t) i] = std::move (s);
            retireFull[(size_t) i].store (true, std::memory_order_release);
            return;
        }
    // the ring is full (the message thread is stalled): keep it alive a little longer in the voice slot
}

std::shared_ptr<const PadSound> PadDrumModule::currentSound (int pad) const
{
    const juce::SpinLock::ScopedLockType sl (slots->lock);
    return slots->sound[pad];
}

std::shared_ptr<const PadSample> PadDrumModule::currentSample (int pad) const
{
    const juce::SpinLock::ScopedLockType sl (slots->lock);
    return slots->sample[pad];
}

void PadDrumModule::loadPadSample (int pad, std::shared_ptr<PadSample> s)
{
    {
        const juce::SpinLock::ScopedLockType sl (slots->lock);
        slots->sample[pad] = std::move (s);
    }
    requestRenders (false);
}

void PadDrumModule::clearPadSample (int pad)
{
    {
        const juce::SpinLock::ScopedLockType sl (slots->lock);
        slots->sample[pad].reset();
        slots->sound[pad].reset();
    }
    requestRenders (false);
}

juce::String PadDrumModule::padName (int pad) const
{
    if (kitKind == Kit::Samples)
    {
        const auto s = currentSample (pad);
        return s != nullptr ? s->name : juce::String ("Empty");
    }
    const auto& ks = kitSounds (kitKind);
    const int i = (int) std::lround (getParameter (pid (pad, "snd")));
    return i >= 0 && i < (int) ks.size() ? ks[(size_t) i].shortName : juce::String();
}

//==============================================================================
// Audio thread
//==============================================================================
void PadDrumModule::rawMidiMessage (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int base = pBase >= 0 ? (int) std::lround (param (pBase)) : 36;
        const int pad = m.getNoteNumber() - base;
        if (pad >= 0 && pad < kPads)
            trigger (pad, m.getFloatVelocity());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        stopAll = true;
}

void PadDrumModule::triggerFromUi (int pad, float velocity)
{
    selectedPad = pad;
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0) uiQueue[(size_t) scope.startIndex1] = { pad, velocity };
    else if (scope.blockSize2 > 0) uiQueue[(size_t) scope.startIndex2] = { pad, velocity };
}

void PadDrumModule::blockStart()
{
    if (stopAll.exchange (false))
        for (auto& v : voices) if (v.active) { v.fadeStep = 1.0f / (float) (0.005 * sampleRate); }
    const auto scope = uiFifo.read (uiFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) trigger (uiQueue[(size_t) (scope.startIndex1 + i)].first, uiQueue[(size_t) (scope.startIndex1 + i)].second);
    for (int i = 0; i < scope.blockSize2; ++i) trigger (uiQueue[(size_t) (scope.startIndex2 + i)].first, uiQueue[(size_t) (scope.startIndex2 + i)].second);
}

void PadDrumModule::trigger (int pad, float velocity)
{
    if (pad < 0 || pad >= kPads) return;
    std::shared_ptr<const PadSound> snd;
    {
        const juce::SpinLock::ScopedTryLockType sl (slots->lock);
        if (! sl.isLocked()) return;
        snd = slots->sound[pad];
    }
    flash[(size_t) pad].store (1.0f, std::memory_order_relaxed);
    if (snd == nullptr || snd->variants <= 0) return;

    const float level = pPadLevel[pad] >= 0 ? param (pPadLevel[pad]) : 0.8f;
    const float pan = pPadPan[pad] >= 0 ? param (pPadPan[pad]) : 0.0f;
    const int choke = pPadChoke[pad] >= 0 ? (int) std::lround (param (pPadChoke[pad])) : 0;

    // the choke group: a hit cuts every sound of its group (the open hat by the closed one)
    if (choke > 0)
        for (auto& v : voices)
            if (v.active && v.choke == choke) v.fadeStep = juce::jmax (v.fadeStep, 1.0f / (float) (0.006 * sampleRate));

    // a free voice, else the oldest
    int pick = -1;
    for (int k = 0; k < kVoices && pick < 0; ++k)
        if (! voices[(size_t) ((nextVoice + (juce::uint32) k) % kVoices)].active) pick = (int) ((nextVoice + (juce::uint32) k) % kVoices);
    if (pick < 0) pick = (int) (nextVoice % kVoices);
    nextVoice = (juce::uint32) pick + 1;
    auto& v = voices[(size_t) pick];
    if (v.active) retireSound (std::move (v.snd));
    const float vel = juce::jlimit (0.0f, 1.0f, velocity);
    const float g = level * (0.12f + 0.88f * vel * vel * (2.0f - vel));   // a soft knee
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    v.snd = std::move (snd);
    v.pad = pad; v.pos = 0; v.choke = choke;
    v.variant = nextVariant[pad] % juce::jmax (1, v.snd->variants);
    nextVariant[pad] = (nextVariant[pad] + 1) % kVariants;
    v.gl = g * std::cos (a) * 1.41421356f;
    v.gr = g * std::sin (a) * 1.41421356f;
    v.fade = 1.0f; v.fadeStep = 0.0f;
    v.active = true;
}

void PadDrumModule::processSample (const StereoFrame*, StereoFrame* outputs)
{
    float l = 0.0f, r = 0.0f;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const auto& L = v.snd->l[v.variant];
        if (v.pos >= (int) L.size()) { v.active = false; retireSound (std::move (v.snd)); continue; }
        const float sl = L[(size_t) v.pos];
        const float sr = v.snd->stereo ? v.snd->r[v.variant][(size_t) v.pos] : sl;
        ++v.pos;
        float g = 1.0f;
        if (v.fadeStep > 0.0f)
        {
            v.fade -= v.fadeStep;
            if (v.fade <= 0.0f) { v.active = false; retireSound (std::move (v.snd)); continue; }
            g = v.fade;
        }
        l += sl * v.gl * g;
        r += sr * v.gr * g;
    }
    const float master = (pLevel >= 0 ? param (pLevel) : 0.7f) * outputTrim();
    outputs[0] = { l * master, r * master };
}

//==============================================================================
// Saving: the loaded samples (Drum Pads) as FLAC, and the pad being edited
//==============================================================================
juce::String PadDrumModule::saveCustomState() const
{
    juce::XmlElement x ("PADS");
    x.setAttribute ("sel", selectedPad);
    if (kitKind == Kit::Samples)
        for (int pad = 0; pad < kPads; ++pad)
        {
            const auto s = currentSample (pad);
            if (s == nullptr || s->l.empty()) continue;
            const int ch = s->r.empty() ? 1 : 2;
            juce::AudioBuffer<float> buf (ch, (int) s->l.size());
            buf.copyFrom (0, 0, s->l.data(), (int) s->l.size());
            if (ch == 2) buf.copyFrom (1, 0, s->r.data(), (int) s->r.size());
            juce::MemoryBlock mb;
            {
                juce::FlacAudioFormat flac;
                auto os = std::make_unique<juce::MemoryOutputStream> (mb, false);
                std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os.get(), s->rate, (unsigned int) ch, 24, {}, 0));
                if (w == nullptr) continue;
                os.release();
                w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
            }
            auto* e = x.createNewChildElement ("PAD");
            e->setAttribute ("i", pad);
            e->setAttribute ("name", s->name);
            e->setAttribute ("flac", mb.toBase64Encoding());
        }
    return x.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

void PadDrumModule::loadCustomState (const juce::String& state)
{
    auto x = juce::parseXML (state);
    if (x == nullptr || ! x->hasTagName ("PADS")) return;
    selectedPad = juce::jlimit (0, kPads - 1, x->getIntAttribute ("sel"));
    for (auto* e : x->getChildWithTagNameIterator ("PAD"))
    {
        const int pad = e->getIntAttribute ("i", -1);
        if (pad < 0 || pad >= kPads) continue;
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (e->getStringAttribute ("flac"))) continue;
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
        if (r == nullptr) continue;
        auto s = std::make_shared<PadSample>();
        const int n = (int) r->lengthInSamples;
        juce::AudioBuffer<float> buf ((int) juce::jmin (2u, r->numChannels), n);
        r->read (&buf, 0, n, 0, true, buf.getNumChannels() > 1);
        s->l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + n);
        if (buf.getNumChannels() > 1) s->r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + n);
        s->rate = r->sampleRate;
        s->name = e->getStringAttribute ("name");
        loadPadSample (pad, std::move (s));
    }
}

//==============================================================================
// The card: the tweaker above, the pads below
//==============================================================================
namespace
{
    std::shared_ptr<PadSample> readSampleData (juce::MemoryBlock mb, const juce::String& name)
    {
        if (mb.getSize() == 0) return nullptr;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (std::move (mb), false)));
        if (r == nullptr) return nullptr;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 30.0));
        if (n <= 0) return nullptr;
        juce::AudioBuffer<float> buf ((int) juce::jlimit (1u, 2u, r->numChannels), n);
        r->read (&buf, 0, n, 0, true, buf.getNumChannels() > 1);
        auto s = std::make_shared<PadSample>();
        s->l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + n);
        if (buf.getNumChannels() > 1) s->r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + n);
        s->rate = r->sampleRate;
        s->name = name;
        return s;
    }

    class PadDrumEditor : public juce::Component, public ExtraContentParamListener, private juce::Timer
    {
    public:
        PadDrumEditor (PadDrumModule& m, juce::Colour accentColour) : module (&m), accent (accentColour)
        {
            const auto kit = m.kit();
            addAndMakeVisible (padBox);
            for (int i = 0; i < kPads; ++i) padBox.addItem ("Pad " + juce::String (i + 1), i + 1);
            padBox.onChange = [this] { if (auto* mm = mod()) { mm->selectedPad = padBox.getSelectedId() - 1; refresh (true); } };

            if (kit != Kit::Samples)
            {
                addAndMakeVisible (soundBox);
                const auto& ks = kitSounds (kit);
                for (size_t i = 0; i < ks.size(); ++i) soundBox.addItem (ks[i].name, (int) i + 1);
                soundBox.onChange = [this] { set ("snd", (float) (soundBox.getSelectedId() - 1)); refresh (true); };
            }
            else
            {
                addAndMakeVisible (loadButton);
                addAndMakeVisible (loadManyButton);
                addAndMakeVisible (clearButton);
                loadButton.onClick = [this] { chooseSample(); };
                loadManyButton.onClick = [this] { chooseMany(); };   // (6.9.16)
                loadButton.setTooltip ("A sound for the selected pad (it replaces the one there)");
                loadManyButton.setTooltip ("Up to 16 sounds at once: they go onto the empty pads, in order");
                clearButton.onClick = [this] { if (auto* mm = mod()) { mm->clearPadSample (mm->selectedPad); refresh (true); } };
            }
            addAndMakeVisible (chokeBox);
            for (int i = 0; i < 5; ++i) chokeBox.addItem (i == 0 ? "No Choke" : "Choke " + juce::String (i), i + 1);
            chokeBox.onChange = [this] { set ("chk", (float) (chokeBox.getSelectedId() - 1)); };

            addKnob ("lvl", "Level", 0.0, 1.0);
            addKnob ("pan", "Pan", -1.0, 1.0);
            addKnob ("tun", "Tune", -24.0, 24.0, 0.5);
            addKnob ("dec", "Decay", 25.0, 400.0, 1.0);
            addKnob ("ton", "Tone", 0.0, 100.0, 1.0);
            if (kit == Kit::TR808) { addKnob ("ka", "", 0.0, 7.5); addKnob ("kb", "", 0.0, 7.5); }
            if (kit != Kit::Samples) { addKnob ("x1", "", -1.0, 1.0); addKnob ("x2", "", -1.0, 1.0); addKnob ("x3", "", -1.0, 1.0); }
            refresh (true);
            startTimerHz (20);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (24);
            padBox.setBounds (top.removeFromLeft (96).reduced (1));
            chokeBox.setBounds (top.removeFromRight (100).reduced (1));
            if (soundBox.isVisible()) soundBox.setBounds (top.reduced (1));
            else
            {
                loadButton.setBounds (top.removeFromLeft (70).reduced (1));
                loadManyButton.setBounds (top.removeFromLeft (78).reduced (1));
                clearButton.setBounds (top.removeFromLeft (58).reduced (1));
                nameArea = top.reduced (4, 0);
            }
            r.removeFromTop (4);
            const int perRow = 5;
            const int rowsNeeded = ((int) knobs.size() + perRow - 1) / perRow;
            auto knobArea = r.removeFromTop (rowsNeeded * 62);
            const int kw = knobArea.getWidth() / perRow;
            for (size_t i = 0; i < knobs.size(); ++i)
            {
                const int row = (int) i / perRow, col = (int) i % perRow;
                auto cell = juce::Rectangle<int> (knobArea.getX() + col * kw, knobArea.getY() + row * 62, kw, 62);
                knobs[i]->label.setBounds (cell.removeFromBottom (14));
                knobs[i]->slider.setBounds (cell.reduced (6, 0));
            }
            r.removeFromTop (4);
            waveArea = r.removeFromTop (52);
            r.removeFromTop (6);
            gridArea = r;
        }

        void paint (juce::Graphics& g) override
        {
            auto* mm = mod();
            if (mm == nullptr) return;
            // the selected pad's sound and its loudness over time (the envelope)
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (waveArea.toFloat(), 4.0f);
            if (auto snd = mm->currentSound (mm->selectedPad); snd != nullptr && snd->variants > 0 && ! snd->l[0].empty())
            {
                const auto& x = snd->l[0];
                const int w = waveArea.getWidth();
                const float mid = (float) waveArea.getCentreY(), h = (float) waveArea.getHeight() * 0.46f;
                float peak = 1e-6f;
                for (float v : x) peak = juce::jmax (peak, std::abs (v));
                juce::Path wave, env;
                const double per = (double) x.size() / w;
                for (int px = 0; px < w; ++px)
                {
                    float mx = 0.0f; double sq = 0.0;
                    const size_t a = (size_t) (px * per), b = juce::jmin (x.size(), (size_t) ((px + 1) * per) + 1);
                    for (size_t i = a; i < b; ++i) { mx = juce::jmax (mx, std::abs (x[i])); sq += (double) x[i] * x[i]; }
                    const float xx = (float) (waveArea.getX() + px);
                    wave.addLineSegment ({ xx, mid - mx / peak * h, xx, mid + mx / peak * h }, 1.0f);
                    const float rms = (float) std::sqrt (sq / juce::jmax<size_t> (1, b - a)) * 1.41f / peak;
                    if (px == 0) env.startNewSubPath (xx, mid - rms * h); else env.lineTo (xx, mid - rms * h);
                }
                g.setColour (accent.withAlpha (0.55f));
                g.fillPath (wave);
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.strokePath (env, juce::PathStrokeType (1.4f));
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.drawText (juce::String ((double) x.size() / mm->tapSampleRate(), 2) + " s", waveArea.reduced (4, 2), juce::Justification::topRight);
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.drawText (mm->kit() == Kit::Samples ? "Tap LOAD PAD (or LOAD MANY) to put sounds on the pads" : "rendering...", waveArea, juce::Justification::centred);
            }
            if (mm->kit() == Kit::Samples && ! nameArea.isEmpty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.setFont (juce::Font (juce::FontOptions (11.5f)));
                g.drawText (mm->padName (mm->selectedPad), nameArea, juce::Justification::centredLeft, true);
            }
            // the pads (pad 1 bottom left, as on the machines)
            for (int pad = 0; pad < kPads; ++pad)
            {
                const auto rc = padRect (pad).toFloat().reduced (3.0f);
                const float fl = mm->padFlash (pad);
                const bool sel = pad == mm->selectedPad;
                g.setColour (accent.withAlpha (0.22f + 0.6f * fl).interpolatedWith (juce::Colours::white, 0.4f * fl));
                g.fillRoundedRectangle (rc, 5.0f);
                g.setColour (sel ? juce::Colours::white : accent.withAlpha (0.7f));
                g.drawRoundedRectangle (rc, 5.0f, sel ? 2.0f : 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.setFont (juce::Font (juce::FontOptions (mm->kit() == Kit::Samples ? 11.0f : 14.0f, juce::Font::bold)));
                g.drawFittedText (mm->padName (pad), rc.toNearestInt().reduced (4), juce::Justification::centred, 2);
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.setFont (juce::Font (juce::FontOptions (9.0f)));
                g.drawText (juce::String (pad + 1), rc.toNearestInt().reduced (5, 3), juce::Justification::topLeft);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int pad = 0; pad < kPads; ++pad)
                if (padRect (pad).contains (e.getPosition()))
                {
                    if (auto* mm = mod())
                    {
                        // tapped higher on the pad: louder
                        const auto rc = padRect (pad);
                        const float vel = juce::jlimit (0.3f, 1.0f, 1.0f - (float) (e.y - rc.getY()) / (float) rc.getHeight() * 0.7f);
                        mm->triggerFromUi (pad, vel);
                        refresh (true);
                    }
                    return;
                }
        }

    private:
        struct KnobUi { juce::String id; juce::Slider slider; juce::Label label; };

        PadDrumModule* mod() const { return dynamic_cast<PadDrumModule*> (module.get()); }

        void addKnob (const juce::String& id, const juce::String& name, double lo, double hi, double step = 0.0)
        {
            auto k = std::make_unique<KnobUi>();
            k->id = id;
            k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k->slider.setRange (lo, hi, step);
            k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
            k->slider.setPopupDisplayEnabled (true, true, nullptr);
            auto* raw = k.get();
            k->slider.onValueChange = [this, raw] { set (raw->id, (float) raw->slider.getValue()); };
            k->slider.onDragEnd = [this] { repaint(); };
            k->label.setText (name, juce::dontSendNotification);
            k->label.setJustificationType (juce::Justification::centred);
            k->label.setFont (juce::Font (juce::FontOptions (10.5f)));
            addAndMakeVisible (k->slider);
            addAndMakeVisible (k->label);
            knobs.push_back (std::move (k));
        }

        void set (const juce::String& what, float v)
        {
            if (syncing) return;
            if (auto* mm = mod())
            {
                mm->setParameter (PadDrumModule::pid (mm->selectedPad, what.toRawUTF8()), v);
                if (paramValuesChangedByContent) paramValuesChangedByContent();
                repaint();
            }
        }

        // the controls follow the selected pad (and anything that changed its knobs)
        void refresh (bool labels)
        {
            auto* mm = mod();
            if (mm == nullptr) return;
            const int pad = mm->selectedPad;
            juce::ScopedValueSetter<bool> svs (syncing, true);
            padBox.setSelectedId (pad + 1, juce::dontSendNotification);
            const int snd = (int) std::lround (mm->getParameter (PadDrumModule::pid (pad, "snd")));
            if (soundBox.isVisible()) soundBox.setSelectedId (snd + 1, juce::dontSendNotification);
            chokeBox.setSelectedId ((int) std::lround (mm->getParameter (PadDrumModule::pid (pad, "chk"))) + 1, juce::dontSendNotification);
            const auto phys = physicalLabels (mm->kit(), snd);
            for (auto& k : knobs)
            {
                if (! k->slider.isMouseButtonDown())
                    k->slider.setValue (mm->getParameter (PadDrumModule::pid (pad, k->id.toRawUTF8())), juce::dontSendNotification);
                if (! labels) continue;
                juce::String text = k->label.getText();
                if (k->id == "ka") text = knobLabel (mm->kit(), snd, 0);
                if (k->id == "kb") text = knobLabel (mm->kit(), snd, 1);
                if (k->id == "x1") text = phys[0];
                if (k->id == "x2") text = phys[1];
                if (k->id == "x3") text = phys[2];
                const bool used = text.isNotEmpty();
                k->label.setText (used ? text : "-", juce::dontSendNotification);
                k->slider.setEnabled (used);
                k->slider.setAlpha (used ? 1.0f : 0.3f);
            }
            repaint();
        }

        void timerCallback() override
        {
            auto* mm = mod();
            if (mm == nullptr) return;
            mm->decayFlashes();
            const int pad = mm->selectedPad;
            const auto snd = mm->currentSound (pad);
            const bool changed = pad != shownPad || snd.get() != shownSound.get();
            shownPad = pad; shownSound = snd;
            refresh (changed);
        }

        juce::Rectangle<int> padRect (int pad) const
        {
            const int col = pad % 4, row = 3 - pad / 4;
            const int w = gridArea.getWidth() / 4, h = gridArea.getHeight() / 4;
            return { gridArea.getX() + col * w, gridArea.getY() + row * h, w, h };
        }

        void chooseSample()
        {
            auto* mm = mod();
            if (mm == nullptr) return;
            const int pad = mm->selectedPad;
            chooser = std::make_shared<juce::FileChooser> ("Load a drum sound", juce::File(),
                                                           aqp::android::pickerPatterns ("*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"));
            juce::Component::SafePointer<PadDrumEditor> safe (this);
            auto weak = module;
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe, weak, pad] (const juce::FileChooser& fc)
                {
                    auto* target = dynamic_cast<PadDrumModule*> (weak.get());
                    if (target == nullptr) return;
                    // (on Android a content:// document: read whole, its name from the provider)
                    const auto url = fc.getURLResult();
                    juce::MemoryBlock data;
                    if (aqp::files::readDocument (url, data).isNotEmpty()) return;
                    juce::String name = url.isLocalFile() ? url.getLocalFile().getFileNameWithoutExtension() : juce::String();
                   #if JUCE_ANDROID
                    if (name.isEmpty())
                        if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                            name = doc.getInfo().getName().upToLastOccurrenceOf (".", false, false);
                   #endif
                    if (name.isEmpty()) name = juce::URL::removeEscapeChars (url.getFileName()).upToLastOccurrenceOf (".", false, false);
                    auto s = readSampleData (std::move (data), name);
                    if (s == nullptr) return;
                    target->loadPadSample (pad, std::move (s));
                    if (safe != nullptr) safe->refresh (true);
                });
        }

        // (6.9.16) up to 16 sounds at once, onto the pads that have none (in order)
        void chooseMany()
        {
            auto* mm = mod();
            if (mm == nullptr) return;
            chooser = std::make_shared<juce::FileChooser> ("Load drum sounds (up to 16)", juce::File(),
                                                           aqp::android::pickerPatterns ("*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"));
            juce::Component::SafePointer<PadDrumEditor> safe (this);
            auto weak = module;
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::canSelectMultipleItems,
                [safe, weak] (const juce::FileChooser& fc)
                {
                    auto* target = dynamic_cast<PadDrumModule*> (weak.get());
                    if (target == nullptr) return;
                    int pad = 0;
                    auto nextEmpty = [&]
                    {
                        while (pad < kPads)
                        {
                            if (target->currentSample (pad) == nullptr) return pad;   // (the sound renders later)
                            ++pad;
                        }
                        return -1;
                    };
                    int loaded = 0;
                    for (const auto& url : fc.getURLResults())
                    {
                        if (loaded >= kPads) break;
                        const int p = nextEmpty();
                        if (p < 0) break;
                        juce::MemoryBlock data;
                        if (aqp::files::readDocument (url, data).isNotEmpty()) continue;
                        juce::String name = url.isLocalFile() ? url.getLocalFile().getFileNameWithoutExtension() : juce::String();
                       #if JUCE_ANDROID
                        if (name.isEmpty())
                            if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                                name = doc.getInfo().getName().upToLastOccurrenceOf (".", false, false);
                       #endif
                        if (name.isEmpty()) name = juce::URL::removeEscapeChars (url.getFileName()).upToLastOccurrenceOf (".", false, false);
                        auto s = readSampleData (std::move (data), name);
                        if (s == nullptr) continue;
                        target->loadPadSample (p, std::move (s));
                        ++pad;
                        ++loaded;
                    }
                    if (safe != nullptr) safe->refresh (true);
                });
        }

        juce::WeakReference<SynthModule> module;
        juce::Colour accent;
        juce::ComboBox padBox, soundBox, chokeBox;
        juce::TextButton loadButton { "LOAD PAD" }, loadManyButton { "LOAD MANY" }, clearButton { "CLEAR" };
        std::vector<std::unique_ptr<KnobUi>> knobs;
        juce::Rectangle<int> waveArea, gridArea, nameArea;
        std::shared_ptr<juce::FileChooser> chooser;
        std::shared_ptr<const PadSound> shownSound;
        int shownPad { -1 };
        bool syncing { false };
    };
}

std::unique_ptr<juce::Component> PadDrumModule::createExtraContentComponent()
{
    return std::make_unique<PadDrumEditor> (*this, getDescriptor().colour());
}

//==============================================================================
class TR808Module final : public PadDrumModule
{
public:
    TR808Module() : PadDrumModule (Kit::TR808) {}
    static ModuleDescriptor descriptor() { return makeDescriptor (Kit::TR808); }
};

class TR909Module final : public PadDrumModule
{
public:
    TR909Module() : PadDrumModule (Kit::TR909) {}
    static ModuleDescriptor descriptor() { return makeDescriptor (Kit::TR909); }
};

class DrumPadsModule final : public PadDrumModule
{
public:
    DrumPadsModule() : PadDrumModule (Kit::Samples) {}
    static ModuleDescriptor descriptor() { return makeDescriptor (Kit::Samples); }
};

AQUANODE_REGISTER_MODULE (TR808Module, TR808Module::descriptor)
AQUANODE_REGISTER_MODULE (TR909Module, TR909Module::descriptor)
AQUANODE_REGISTER_MODULE (DrumPadsModule, DrumPadsModule::descriptor)
