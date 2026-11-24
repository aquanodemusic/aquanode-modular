#pragma once
#include "../../Engine/Tuning.h"
//==============================================================================
// The physically modelled instruments as playable engines: notes in, stereo
// out. The Strings, Cello, Clarinet, Acoustic Guitar and Electric Guitar
// modules run these (each with its own polyphony, the instrument's body or
// amp once on the sum of its strings).
//
//   BowedEngine     a section (or a soloist) of bowed strings: every note is
//                   played by `players` independent players - their own
//                   tuning, vibrato, bow changes and onsets - spread across
//                   the stereo field, through the instruments' bodies
//   ClarinetEngine  the clarinet (Kernel v1 or the refitted v2), polyphonic or
//                   one voice slurring from note to note
//   GuitarEngine    six strings, picked (strummed when keys come together),
//                   through an amp and cabinet (electric) or a body (acoustic)
//
// Breath control: every engine plays from the keys (velocity and the note's
// own envelope) or from a breath (0..1, a breath controller, pressure or a
// wheel) - or a mix of the two, `breathAmount` 0..1. With breath the bow
// speed, the blowing pressure or (a guitar) a volume swell follow it live.
//
// Plain C++, no JUCE: the offline test renders the same code. Nothing
// allocates once prepare() has run.
//==============================================================================
#include "PhysInstruments.h"
#include "StudioVoicing.h"
#include "PizzModal.h"
#include <array>

namespace aquanode::physinst
{
    inline double midiHz (double note) { return aquanode::tuning::noteToHz (note); }   // (the patch tuning, 6.9.10)

    // the live controls, read once per sample
    struct LiveControls
    {
        double bendRatio { 1.0 };
        double breath { 0.0 };        // 0..1 (smoothed)
        double vibScale { 1.0 };      // the mod wheel's extra vibrato
        // MPE: each MIDI channel's own bend (a ratio) and, when the breath is
        // the pressure, each channel's own pressure (< 0: none, use breath)
        const double* chanBend { nullptr };       // [17], index = channel
        const double* chanBreath { nullptr };     // [17]
        double bendOf (int ch) const { return bendRatio * (chanBend != nullptr ? chanBend[std::clamp (ch, 1, 16)] : 1.0); }
        double breathOf (int ch) const
        {
            if (chanBreath != nullptr && ch >= 2 && ch <= 16 && chanBreath[ch] >= 0.0) return chanBreath[ch];
            return breath;
        }
    };

    // keys held and the sustain pedal, shared by the engines
    struct NoteStack
    {
        std::array<int, 16> held {};
        int count { 0 };
        void push (int n) { remove (n); if (count < 16) held[(size_t) count++] = n; }
        void remove (int n)
        {
            for (int i = 0; i < count; ++i)
                if (held[(size_t) i] == n) { for (int j = i; j < count - 1; ++j) held[(size_t) j] = held[(size_t) j + 1]; --count; return; }
        }
        int top() const { return count > 0 ? held[(size_t) count - 1] : -1; }
        void clear() { count = 0; }
    };

    //==========================================================================
    // BOWED STRINGS
    //==========================================================================
    // Decay / Sustain (Aquanode): after the instrument's own attack a note falls from
    // full level to the Sustain level, most of the way (-60 dB of the difference) in the
    // Decay time; a Sustain of 100 % leaves the note as the model plays it.
    struct DecayEnv
    {
        double g { 1.0 };
        void restart() { g = 1.0; }
        double next (double coef, double sustain)
        {
            if (sustain >= 0.9999) return 1.0;
            g = sustain + (g - sustain) * coef;
            return g;
        }
        static double coefFor (double decaySeconds, double sampleRate)
        {
            return std::exp (-6.907755 / std::max (1.0, std::max (0.005, decaySeconds) * sampleRate));   // ln (1000)
        }
    };

    class BowedEngine
    {
    public:
        static constexpr int kMaxStrings = 32;
        enum Section { Full = -1 };

        struct Settings
        {
            int instrument { 0 };          // Bowed (0 violin .. 3 bass), or Full: by the note's range
            int players { 1 };             // per note
            double pressure { 0.5 }, position { -1.0 }, vibCents { 11.0 }, vibRate { 5.3 }, attack { 0.08 }, release { 0.10 };
            double brightness { 1.0 }, spread { 0.8 }, glide { 0.06 }, detune { 5.0 }, stringQ { 650.0 };
            bool pizzicato { false }, legato { false };
            double breathAmount { 0.0 };
            double decay { 1.0 }, sustain { 1.0 };   // Decay (s) / Sustain (0..1), see DecayEnv
            int studio { 0 };                        // 0 classic, 1 Arco Studio, 2 Pizz Studio, 3 Pizz Mellow, 4 Pizz Modal
        };

        void prepare (double sampleRate, bool section)
        {
            sr = sampleRate;
            isSection = section;
            for (int i = 0; i < 4; ++i)
            {
                const auto info = instrumentInfo ((Bowed) i);
                // a soloist: the full body; a section: its strongest modes, one body each side
                const int maxModes = section ? 48 : 0;
                bodyL[(size_t) i].build (info.bf, info.bq, info.bg, info.bn, sr, 1.0, section ? 0.01 : 0.0, 7, maxModes);
                bodyR[(size_t) i].build (info.bf, info.bq, info.bg, info.bn, sr, 1.0, section ? 0.01 : 0.0, 11, maxModes);
            }
            for (auto& s : strings) { s.str.prepare (sr); s.mp.prepare (sr); }
            directHp.set (2000.0, sr);
            builtStudio = -1;
            buildStudio();
            reset();
        }

        void reset()
        {
            for (auto& s : strings) { s.resetVoice(); s.note = -1; s.sustained = false; }
            for (auto& b : bodyL) b.reset();
            for (auto& b : bodyR) b.reset();
            stack.clear();
            busL.fill (0.0); busR.fill (0.0);
            for (auto& q : quiet) q = 0;
            for (auto& e : studioL) e.reset();
            for (auto& e : studioR) e.reset();
        }

        void setSettings (const Settings& s) { set = s; decayCoef = DecayEnv::coefFor (set.decay, sr); buildStudio(); }

        void noteOn (int note, double velocity, int channel = 1)
        {
            curChannel = std::clamp (channel, 1, 16);
            const double dyn = 0.25 + 0.75 * std::clamp (velocity, 0.0, 1.0);
            const int inst = instrumentFor (note);
            const double f = midiHz (note);
            // legato (one line): the sounding string glides to the new note
            if (set.legato && ! set.pizzicato)
            {
                const int prev = stack.top();
                stack.push (note);
                if (prev >= 0)
                {
                    bool moved = false;
                    for (auto& s : strings)
                        if (s.note == prev && s.active())
                        {
                            startOne (s, note, f * s.det, dyn * s.dynScale, inst, s.pan, true, midiHz (prev) * s.det);
                            moved = true;
                        }
                    if (moved) return;
                }
            }
            else
                stack.push (note);
            // re-struck: the same key's strings let go first
            for (auto& s : strings) if (s.note == note && s.held()) s.release();
            const int players = std::clamp (set.players, 1, 8);
            for (int p = 0; p < players; ++p)
            {
                auto& s = freeString();
                rng.reseed ((uint64_t) (++counter) * 7919u + (uint64_t) p);
                s.det = players > 1 || set.detune > 0.0 ? std::pow (2.0, rng.normal() * (players > 1 ? set.detune : set.detune * 0.2) / 1200.0) : 1.0;
                s.dynScale = players > 1 ? 0.8 + 0.2 * rng.uni() : 1.0;
                s.pan = players > 1 ? set.spread * (2.0 * (p + 0.5) / players - 1.0) : 0.0;
                startOne (s, note, f * s.det, dyn * s.dynScale, inst, s.pan, false, 0.0, p, players);
            }
        }

        void noteOff (int note)
        {
            const bool wasTop = stack.top() == note;
            stack.remove (note);
            if (set.legato && ! set.pizzicato && wasTop && stack.top() >= 0)
            {
                // back to the key still held
                const int back = stack.top();
                for (auto& s : strings)
                    if (s.note == note && s.active())
                        startOne (s, back, midiHz (back) * s.det, s.lastDyn, instrumentFor (back), s.pan, true, midiHz (note) * s.det);
                return;
            }
            for (auto& s : strings)
                if (s.note == note && s.held())
                {
                    if (sustain) s.sustained = true;
                    else s.release();
                }
        }

        void setSustain (bool on)
        {
            sustain = on;
            if (! on)
                for (auto& s : strings)
                    if (s.sustained) { s.sustained = false; s.release(); }
        }

        void allNotesOff() { for (auto& s : strings) { s.sustained = false; s.release(); } stack.clear(); }

        bool isSounding() const { for (const auto& s : strings) if (s.active()) return true; for (auto q : quiet) if (q < kQuietMax) return true; return false; }

        void process (const LiveControls& c, double& outL, double& outR)
        {
            busL.fill (0.0); busR.fill (0.0);
            double modalL = 0.0, modalR = 0.0;
            const double a = std::clamp (set.breathAmount, 0.0, 1.0);
            std::array<bool, 4> any {};
            for (auto& s : strings)
            {
                if (! s.active()) continue;
                if (a > 0.0 && ! set.pizzicato) s.str.setDynamic ((1.0 - a) * s.lastDyn + a * c.breathOf (s.channel));
                const double x = (s.modal ? s.mp.process (c.bendOf (s.channel)) : s.str.process (c.bendOf (s.channel), c.vibScale))
                                 * s.gain * s.env.next (decayCoef, set.sustain);
                const double pl = std::cos ((s.pan + 1.0) * 0.25 * kPi) * 1.41421356, pr = std::sin ((s.pan + 1.0) * 0.25 * kPi) * 1.41421356;
                if (s.modal)
                {
                    // Pizz Modal: the measured partials carry the body and the room already
                    if (isSection) { modalL += x * pl; modalR += x * pr; } else { modalL += x; modalR += x; }
                    continue;
                }
                busL[(size_t) s.inst] += x * pl;
                busR[(size_t) s.inst] += x * pr;
                any[(size_t) s.inst] = true;
            }
            double l = 0.0, r = 0.0;
            for (int i = 0; i < 4; ++i)
            {
                // a body rings a little after its strings stop: run it until it is quiet
                if (any[(size_t) i]) quiet[(size_t) i] = 0;
                else if (quiet[(size_t) i] >= kQuietMax) continue;
                else ++quiet[(size_t) i];
                const double direct = set.pizzicato ? 0.12 : 0.05;
                const bool st = set.studio > 0 && builtStudio == set.studio;
                if (isSection)
                {
                    // each side its own body (slightly different modes): the section's width
                    const double bl = bodyL[(size_t) i].process ((float) busL[(size_t) i]) + direct * busL[(size_t) i];
                    const double br = bodyR[(size_t) i].process ((float) busR[(size_t) i]) + direct * busR[(size_t) i];
                    l += st ? studioL[(size_t) i].process (bl) : bl;
                    r += st ? studioR[(size_t) i].process (br) : br;
                }
                else
                {
                    const double m = 0.5 * (busL[(size_t) i] + busR[(size_t) i]);
                    double b = bodyL[(size_t) i].process ((float) m) + direct * m;
                    if (st) b = studioL[(size_t) i].process (b);
                    l += b; r += b;
                }
            }
            outL = l + modalL;
            outR = r + modalR;
        }

    private:
        struct Str
        {
            BowedString str;
            pizzmodal::Voice mp;     // Pizz Modal plays this instead of the string
            bool modal { false };
            bool active() const { return modal ? mp.isActive() : str.isActive(); }
            bool held() const { return modal ? mp.isHeld() : str.isHeld(); }
            void release() { if (modal) mp.release(); else str.release(); }
            void resetVoice() { str.reset(); mp.reset(); }
            int note { -1 }, inst { 0 }, channel { 1 };
            double det { 1.0 }, dynScale { 1.0 }, pan { 0.0 }, gain { 1.0 }, lastDyn { 0.7 };
            bool sustained { false };
            uint32_t age { 0 };
            DecayEnv env;
        };

        int instrumentFor (int note) const
        {
            if (set.instrument >= 0) return std::clamp (set.instrument, 0, 3);
            return note >= 60 ? 0 : (note >= 48 ? 1 : (note >= 36 ? 2 : 3));
        }

        Str& freeString()
        {
            Str* best = nullptr;
            for (auto& s : strings) if (! s.active()) { best = &s; break; }
            if (best == nullptr)
            {
                // the oldest released one, else the oldest of all
                for (auto& s : strings) if (! s.held() && (best == nullptr || s.age < best->age)) best = &s;
                if (best == nullptr) for (auto& s : strings) if (best == nullptr || s.age < best->age) best = &s;
                best->resetVoice();
            }
            return *best;
        }

        void startOne (Str& s, int note, double f, double dyn, int inst, double pan, bool glide, double fromF, int player = 0, int players = 1)
        {
            if (! glide) s.env.restart();   // (a legato step goes on with the note's decay)
            BowSettings b;
            b.pressure = set.pressure;
            b.position = set.position;
            b.attack = set.attack;
            b.release = set.release;
            b.vibCents = set.vibCents;
            b.vibRate = set.vibRate;
            b.brightness = set.brightness;
            b.glideTime = set.glide;
            b.stringQ = set.stringQ;
            double delay = 0.0;
            if (players > 1 && ! glide)
            {
                // each player their own: bow weight, attack, vibrato, entry
                b.pressure = std::clamp (set.pressure + rng.normal() * 0.08, 0.1, 1.0);
                b.attack = set.attack * (0.7 + 0.8 * rng.uni());
                b.vibCents = set.vibCents * (0.6 + 0.7 * rng.uni());
                b.vibRate = set.vibRate * (0.9 + 0.2 * rng.uni());
                b.vibDelay = 0.1 + 0.3 * rng.uni();
                b.release = set.release * (0.8 + 0.5 * rng.uni());
                delay = 0.035 * rng.uni();
            }
            (void) player;
            const auto info = instrumentInfo ((Bowed) inst);
            const double a = std::clamp (set.breathAmount, 0.0, 1.0);
            s.lastDyn = dyn;
            s.modal = set.pizzicato && set.studio == 4;
            if (s.modal)
            {
                // Pizz Modal: the partials of a measured cello pizzicato (a section: fewer each)
                s.str.reset();
                s.mp.start (note, f, dyn, set.brightness, (uint64_t) (++counter) * 31u + 5u, delay, players > 2 ? 96 : pizzmodal::Voice::kMaxPartials);
            }
            else
            {
                s.mp.reset();
                if (set.studio >= 2 && set.pizzicato) studioPluck (b, note);
                s.str.start (note, f, a > 0.0 && ! set.pizzicato ? (1.0 - a) * dyn : dyn, info, b, set.pizzicato, (uint64_t) (++counter) * 31u + 5u,
                             glide ? fromF : 0.0, delay, 0.0);
            }
            s.note = note; s.inst = inst; s.pan = pan; s.channel = curChannel;
            s.gain = (set.pizzicato ? 2.0 : 1.0) / std::sqrt ((double) std::max (1, players)) * kOutGain * levelFor (inst);
            s.sustained = false;
            s.age = ++ageCounter;
        }

        static double levelFor (int inst) { static const double g[4] = { 1.0, 1.0, 1.15, 1.3 }; return g[std::clamp (inst, 0, 3)]; }

        // the Studio voicings: the radiation toward the microphone, per instrument
        void buildStudio()
        {
            if (set.studio == builtStudio) return;
            builtStudio = set.studio;
            if (set.studio <= 0 || set.studio >= 4) return;
            for (int i = 0; i < 4; ++i)
            {
                const studio::Id id = set.studio == 1 ? (i < 2 ? studio::ViolinArco : studio::CelloArco)
                                                      : (set.studio == 2 ? studio::PizzBright : studio::PizzMellow);
                studioL[(size_t) i].build (studio::voicing (id), sr);
                studioR[(size_t) i].build (studio::voicing (id), sr);
            }
        }
        // the Studio pizzicati, fitted to a cello's plucked notes (decay per key, the finger, the point)
        void studioPluck (BowSettings& b, int note)
        {
            const bool mellow = set.studio == 3;
            const double k = (note - 50) / 12.0;
            b.pluckT60 = mellow ? std::exp (0.553 - 0.090 * k) : std::exp (0.698 - 0.171 * k);
            b.pluckBrightHz = mellow ? kPizzMellowBright : kPizzBright;
            b.pluckMs = (mellow ? kPizzMellowMs : kPizzMs) * std::pow (2.0, std::clamp (note - 42, -12, 30) / 12.0 * (mellow ? kPizzMellowMsSlope : kPizzMsSlope));
            b.pluckBeta = (mellow ? kPizzMellowBeta : kPizzBeta) + 0.08 * (rng.uni() - 0.5);
            b.pluckBuzz = mellow ? 0.0 : kPizzBuzz;
        }
        static constexpr double kPizzBright = 20000.0, kPizzMs = 1.3, kPizzMsSlope = 0.5, kPizzBeta = 0.18, kPizzBuzz = 0.22;
        static constexpr double kPizzMellowBright = 16000.0, kPizzMellowMs = 3.0, kPizzMellowMsSlope = 0.3, kPizzMellowBeta = 0.2;

        static constexpr double kOutGain = 1.0;
        static constexpr int kQuietMax = 48000 * 2;

        double sr { 48000.0 };
        double decayCoef { 0.0 };
        int curChannel { 1 };
        bool isSection { false }, sustain { false };
        Settings set;
        std::array<Str, kMaxStrings> strings;
        std::array<ModalBody, 4> bodyL, bodyR;
        std::array<double, 4> busL {}, busR {};
        std::array<int, 4> quiet {};
        OnePole directHp;
        NoteStack stack;
        Rng rng;
        uint32_t counter { 0 }, ageCounter { 0 };
        std::array<studio::EqChain, 4> studioL, studioR;
        int builtStudio { -1 };
    };

    //==========================================================================
    // CLARINET
    //==========================================================================
    class ClarinetEngine
    {
    public:
        static constexpr int kMaxVoices = 4;
        struct Settings
        {
            int version { 2 };
            double attack { 0.035 }, release { 0.07 }, breathNoise { 0.07 }, vibCents { 0.0 }, vibRate { 5.0 }, glide { 0.04 };
            bool mono { true };
            double breathAmount { 0.0 };
            int polyphony { 4 };
            double decay { 1.0 }, sustain { 1.0 };   // Decay (s) / Sustain (0..1), see DecayEnv
            bool studio { false };                   // the Studio voicing (on the refined bore)
        };

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            for (auto& v : voices) v.v.prepare (sr, set.version);
            preparedVersion = set.version;
            if (set.studio) studioEq.build (studio::voicing (studio::Clarinet), sr);
            builtStudio = set.studio;
            reset();
        }

        void reset() { for (auto& v : voices) { v.v.reset(); v.sustained = false; v.note = -1; } stack.clear(); studioEq.reset(); }

        void setSettings (const Settings& s)
        {
            set = s;
            decayCoef = DecayEnv::coefFor (set.decay, sr);
            if (set.studio != builtStudio)
            {
                builtStudio = set.studio;
                if (set.studio) { studioEq.build (studio::voicing (studio::Clarinet), sr); studioEq.reset(); }
            }
            if (set.version != preparedVersion)
            {
                for (auto& v : voices) v.v.prepare (sr, set.version);   // (tables only: no allocation)
                preparedVersion = set.version;
            }
        }

        void noteOn (int note, double velocity, int channel = 1)
        {
            channel = std::clamp (channel, 1, 16);
            const double dyn = 0.2 + 0.8 * std::clamp (velocity, 0.0, 1.0);
            const double f = midiHz (note);
            ClarinetSettings cs;
            cs.attack = set.attack; cs.release = set.release; cs.breath = set.breathNoise; cs.vibCents = set.vibCents; cs.vibRate = set.vibRate; cs.glideTime = set.glide;
            if (set.mono)
            {
                const int prev = stack.top();
                stack.push (note);
                auto& v = voices[0];
                if (prev >= 0 && v.v.isActive() && v.v.isHeld()) { v.v.slurTo (note, f, dyn, ++counter); v.note = note; v.channel = channel; return; }
                v.v.start (note, f, dyn, cs, ++counter, v.v.isActive() ? midiHz (v.note) : 0.0);
                v.note = note; v.dyn = dyn; v.sustained = false; v.channel = channel; v.env.restart();
                return;
            }
            stack.push (note);
            auto& v = pick (note);
            v.v.start (note, f, dyn, cs, ++counter, 0.0);
            v.note = note; v.dyn = dyn; v.sustained = false; v.age = ++ageCounter; v.channel = channel; v.env.restart();
        }

        void noteOff (int note)
        {
            const bool wasTop = stack.top() == note;
            stack.remove (note);
            if (set.mono)
            {
                auto& v = voices[0];
                if (wasTop && stack.top() >= 0 && v.v.isActive()) { const int back = stack.top(); v.v.slurTo (back, midiHz (back), v.dyn, ++counter); v.note = back; return; }
                if (v.note == note || stack.count == 0) { if (sustain) v.sustained = true; else v.v.release(); }
                return;
            }
            for (int i = 0; i < std::clamp (set.polyphony, 1, kMaxVoices); ++i)
            {
                auto& v = voices[(size_t) i];
                if (v.note == note && v.v.isHeld()) { if (sustain) v.sustained = true; else v.v.release(); }
            }
        }

        void setSustain (bool on)
        {
            sustain = on;
            if (! on) for (auto& v : voices) if (v.sustained) { v.sustained = false; v.v.release(); }
        }

        void allNotesOff() { for (auto& v : voices) { v.sustained = false; v.v.release(); } stack.clear(); }

        void process (const LiveControls& c, double& outL, double& outR)
        {
            const double a = std::clamp (set.breathAmount, 0.0, 1.0);
            double y = 0.0;
            const int n = set.mono ? 1 : std::clamp (set.polyphony, 1, kMaxVoices);
            for (int i = 0; i < n; ++i)
            {
                auto& v = voices[(size_t) i];
                if (! v.v.isActive()) continue;
                double breath = -1.0;
                if (a > 0.0)
                {
                    // the breath itself, or a mix with the keys' (held: the velocity's level, released: none)
                    const double keys = v.v.isHeld() ? v.dyn : 0.0;
                    breath = (1.0 - a) * keys + a * c.breathOf (v.channel);
                }
                y += v.v.process (c.bendOf (v.channel), breath) * v.env.next (decayCoef, set.sustain);
            }
            if (set.studio && builtStudio) y = studioEq.process (y);
            outL = outR = y * (set.version == 1 ? 0.7 : 1.0);   // (v1 sounds 3 dB louder)
        }

    private:
        struct V { ClarinetVoice v; int note { -1 }, channel { 1 }; double dyn { 0.6 }; bool sustained { false }; uint32_t age { 0 }; DecayEnv env; };

        V& pick (int note)
        {
            const int n = std::clamp (set.polyphony, 1, kMaxVoices);
            for (int i = 0; i < n; ++i) if (voices[(size_t) i].note == note && voices[(size_t) i].v.isActive()) return voices[(size_t) i];
            for (int i = 0; i < n; ++i) if (! voices[(size_t) i].v.isActive()) return voices[(size_t) i];
            V* best = &voices[0];
            for (int i = 0; i < n; ++i) if (voices[(size_t) i].age < best->age) best = &voices[(size_t) i];
            return *best;
        }

        double sr { 48000.0 };
        double decayCoef { 0.0 };
        Settings set;
        int preparedVersion { -1 };
        std::array<V, kMaxVoices> voices;
        NoteStack stack;
        bool sustain { false };
        uint64_t counter { 0 };
        uint32_t ageCounter { 0 };
        studio::EqChain studioEq;
        bool builtStudio { false };
    };

    //==========================================================================
    // GUITAR
    //==========================================================================
    class GuitarEngine
    {
    public:
        static constexpr int kStrings = 6;

        // the styles, from the fitted voices (PhysInstrumentData.h)
        static int numStyles (bool electric) { return electric ? 5 : 2; }
        static const char* styleName (bool electric, int i)
        {
            static const char* e[5] = { "Clean", "Edge", "Crunch", "Heavy", "Lead" };
            static const char* a[2] = { "Bright Steel", "Dark Steel" };
            return electric ? e[std::clamp (i, 0, 4)] : a[std::clamp (i, 0, 1)];
        }
        // the voice behind a style, and how hard its raw strings come in (the amp was fitted to a normalised string)
        static GuitarParams styleParams (bool electric, int i, double& rawPeak)
        {
            static const int ev[5] = { 0, 2, 1, 3, 1 };
            static const double ep[5] = { 0.8431, 0.7164, 1.1896, 1.5458, 1.1896 };
            if (electric)
            {
                i = std::clamp (i, 0, 4);
                auto p = guitarParamsFrom (guitarVoices[ev[i]]);
                rawPeak = ep[i];
                if (i == 4) { p.drive = 1.6; p.t60 *= 1.8; p.presence = 0.8; p.mid = 2.0; }   // Lead: the crunch pushed, longer sustain
                return p;
            }
            i = std::clamp (i, 0, 1);
            rawPeak = i == 0 ? 0.9281 : 0.9153;
            return guitarParamsFrom (guitarVoices[4 + i]);
        }

        struct Settings
        {
            bool electric { true };
            int style { 0 };
            double drive { 1.0 }, bass { 0.0 }, mid { 0.0 }, treble { 0.0 }, presence { 0.0 };   // drive: x the style's; tone: dB on top
            double pickup { 1.0 }, pickPos { 1.0 }, pick { 1.0 }, sustain { 1.0 }, mute { 0.0 }, strumMs { 18.0 }, body { 1.0 }, bright { 1.0 };
            double release { 0.08 };
            double breathAmount { 0.0 };
        };

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            for (auto& s : strings) s.str.prepare (sr);
            amp.prepare (sr);
            body.prepare (sr);
            applied = false;
            reset();
        }

        void reset()
        {
            for (auto& s : strings) { s.str.reset(); s.note = -1; s.sustained = false; }
            amp.reset(); body.reset();
            swell = 0.0;
        }

        void setSettings (const Settings& s)
        {
            const bool styleChanged = ! applied || s.electric != set.electric || s.style != set.style;
            const bool toneChanged = styleChanged || s.drive != set.drive || s.bass != set.bass || s.mid != set.mid || s.treble != set.treble
                                     || s.presence != set.presence || s.pickup != set.pickup || s.body != set.body || s.bright != set.bright;
            set = s;
            if (! toneChanged) return;
            gp = styleParams (set.electric, set.style, rawPeak);
            gp.drive *= set.drive;
            gp.bass += set.bass; gp.mid += set.mid; gp.treble += set.treble; gp.presence += set.presence;
            gp.puPos = std::clamp (gp.puPos * set.pickup, 0.03, 0.45);
            gp.air *= set.body; gp.top *= set.body; gp.bodyBright *= set.bright;
            if (set.electric) amp.setParams (gp, 1.0 / rawPeak);   // (filters' coefficients only)
            else body.setParams (gp.air, gp.top, gp.bodyBright);
            // the styles brought to one level (a single note near -6 dBFS)
            static const double eDb[5] = { -0.4, -7.7, -7.9, -4.9, -17.3 }, aDb[2] = { 0.4, 2.8 };
            styleGain = std::pow (10.0, (set.electric ? eDb[std::clamp (set.style, 0, 4)] : aDb[std::clamp (set.style, 0, 1)]) / 20.0);
            applied = true;
        }

        void noteOn (int note, double velocity, double sinceLastOnSeconds = 1.0, int channel = 1)
        {
            // keys that come together are strummed: each a little after the one before
            if (sinceLastOnSeconds < 0.03) chordIndex = std::min (chordIndex + 1, 5); else chordIndex = 0;
            const double delay = std::min (0.2, chordIndex * set.strumMs * 0.001);
            auto& s = freeString (note);
            GuitarParams p = gp;
            p.pos = std::clamp (p.pos * set.pickPos, 0.03, 0.49);
            p.pickMs *= set.pick;
            p.t60 *= set.sustain;
            s.str.start (note, midiHz (note), 0.25 + 0.75 * std::clamp (velocity, 0.0, 1.0), std::clamp (set.mute, 0.0, 1.0), p, (uint64_t) (++counter) * 977u, delay);
            s.note = note; s.sustained = false; s.age = ++ageCounter; s.channel = std::clamp (channel, 1, 16);
        }

        void noteOff (int note)
        {
            for (auto& s : strings)
                if (s.note == note && s.str.isHeld())
                {
                    if (sustainPedal) s.sustained = true;
                    else s.str.release (set.release);
                }
        }

        void setSustain (bool on)
        {
            sustainPedal = on;
            if (! on) for (auto& s : strings) if (s.sustained) { s.sustained = false; s.str.release (set.release); }
        }

        void allNotesOff() { for (auto& s : strings) { s.sustained = false; s.str.release (set.release); } }

        void process (const LiveControls& c, double& outL, double& outR)
        {
            double x = 0.0;
            for (auto& s : strings) if (s.str.isActive()) x += s.str.process (c.bendOf (s.channel));
            // with breath: a volume swell (the pedal under the foot)
            const double a = std::clamp (set.breathAmount, 0.0, 1.0);
            const double target = (1.0 - a) + a * c.breath;
            swell += 0.002 * (target - swell);
            double y = set.electric ? amp.process (x) : body.process (x);
            y *= swell * styleGain;
            outL = outR = y;
        }

    private:
        struct S { GuitarString str; int note { -1 }, channel { 1 }; bool sustained { false }; uint32_t age { 0 }; };

        S& freeString (int note)
        {
            for (auto& s : strings) if (s.note == note && s.str.isActive()) return s;   // the same note: its own string again
            for (auto& s : strings) if (! s.str.isActive()) return s;
            S* best = nullptr;
            for (auto& s : strings) if (! s.str.isHeld() && (best == nullptr || s.age < best->age)) best = &s;
            if (best == nullptr) for (auto& s : strings) if (best == nullptr || s.age < best->age) best = &s;
            return *best;
        }

        double sr { 48000.0 }, rawPeak { 1.0 }, swell { 1.0 }, styleGain { 1.0 };
        Settings set;
        GuitarParams gp;
        bool applied { false }, sustainPedal { false };
        std::array<S, kStrings> strings;
        GuitarAmp amp;
        GuitarBodyRes body;
        int chordIndex { 0 };
        uint64_t counter { 0 };
        uint32_t ageCounter { 0 };
    };
}
