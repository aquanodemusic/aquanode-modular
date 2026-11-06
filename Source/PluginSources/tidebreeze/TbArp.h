#pragma once
//==============================================================================
// Tidebreeze - arpeggiator
//
// Holds the keys you play and steps through them on a tempo grid. The order
// patterns (Up, Down, Alt 1/2, Random, As Played, Chord) walk the held notes
// over 1-4 octaves; the rhythm patterns (Bossa, Pulse 3+3+2, Gallop, Rise &
// Fall, Echo Steps) are step sequences that pick from the held notes, with
// their own accents, gates and rests.
//==============================================================================
#include "TbCommon.h"

namespace tb
{
    struct Arp
    {
        struct Settings { int pattern = 0, res = 2, octaves = 1, gate = 60, vel = 0, swing = 0; bool latch = false; };

        void noteOn (int note, float velocity)
        {
            if (latch && physical == 0) { notes.clear(); order.clear(); }
            ++physical;
            if (std::find (notes.begin(), notes.end(), note) == notes.end())
            {
                notes.push_back (note); order.push_back (note);
                std::sort (notes.begin(), notes.end());
            }
            lastVelocity = velocity;
            if (notes.size() == 1 && ! running) { running = true; stepIndex = 0; samplesToStep = 0.0; }
        }
        void noteOff (int note)
        {
            physical = std::max (0, physical - 1);
            if (latch) return;
            removeNote (note);
        }
        void setLatch (bool on)
        {
            if (latch && ! on && physical == 0) { notes.clear(); order.clear(); }
            latch = on;
        }
        void allOff (juce::MidiBuffer& out, int pos)
        {
            for (int n : sounding) out.addEvent (juce::MidiMessage::noteOff (1, n), pos);
            sounding.clear(); notes.clear(); order.clear(); running = false; physical = 0;
        }

        // generates the arpeggio's note events for a block of n samples
        void process (const Settings& s, double tempoBpm, double sr, int n, juce::MidiBuffer& out)
        {
            latchSetting (s.latch);
            if (notes.empty())
            {
                for (int k : sounding) out.addEvent (juce::MidiMessage::noteOff (1, k), 0);
                sounding.clear(); running = false; offs.clear();
                return;
            }
            const double beatSamples = 60.0 / std::max (20.0, tempoBpm) * sr;
            const double step = beatSamples * (double) arpRes (s.res);
            for (int i = 0; i < n; ++i)
            {
                // pending note-offs
                for (size_t k = 0; k < offs.size();)
                {
                    if (--offs[k].left <= 0) { out.addEvent (juce::MidiMessage::noteOff (1, offs[k].note), i); eraseSounding (offs[k].note); offs.erase (offs.begin() + (long) k); }
                    else ++k;
                }
                if (samplesToStep <= 0.0)
                {
                    const double swing = (stepIndex & 1) ? (double) s.swing / 100.0 * step * 0.5 : 0.0;
                    playStep (s, step, i, out);
                    samplesToStep += step + ((stepIndex & 1) ? -swing : swing);
                    ++stepIndex;
                }
                samplesToStep -= 1.0;
            }
        }

    private:
        struct Off { int note; int left; };
        std::vector<int> notes, order, sounding;
        std::vector<Off> offs;
        int physical = 0, stepIndex = 0;
        bool running = false, latch = false;
        double samplesToStep = 0.0;
        float lastVelocity = 0.8f;
        juce::uint32 rng = 2463534242u;

        static float arpRes (int i) { static const float b[] = { 0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 1.0f }; return b[juce::jlimit (0, 6, i)]; }
        void latchSetting (bool on) { if (on != latch) setLatch (on); }
        void removeNote (int note)
        {
            notes.erase (std::remove (notes.begin(), notes.end(), note), notes.end());
            order.erase (std::remove (order.begin(), order.end(), note), order.end());
        }
        void eraseSounding (int note) { sounding.erase (std::remove (sounding.begin(), sounding.end(), note), sounding.end()); }
        int rnd (int n) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (int) (rng % (juce::uint32) std::max (1, n)); }

        void emit (int note, float vel, int gateSamples, int pos, juce::MidiBuffer& out)
        {
            if (note < 0 || note > 127) return;
            out.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, (int) (vel * 127.0f))), pos);
            sounding.push_back (note);
            offs.push_back ({ note, std::max (1, gateSamples) });
        }

        void playStep (const Settings& s, double step, int pos, juce::MidiBuffer& out)
        {
            const int count = (int) notes.size();
            const int oct = juce::jlimit (1, 4, s.octaves);
            const int total = count * oct;
            const float baseVel = s.vel > 0 ? (float) s.vel / 127.0f : lastVelocity;
            const int gate = (int) (step * juce::jlimit (0.02, 1.0, s.gate / 100.0));
            auto noteAt = [&] (int k, bool played)
            {
                k = ((k % total) + total) % total;
                const int o = k / count, idx = k % count;
                return (played ? order[(size_t) std::min (idx, (int) order.size() - 1)] : notes[(size_t) idx]) + 12 * o;
            };
            const int st = stepIndex;
            switch (s.pattern)
            {
                case 0: emit (noteAt (st, false), baseVel, gate, pos, out); break;                       // Up
                case 1: emit (noteAt (total - 1 - (st % total), false), baseVel, gate, pos, out); break; // Down
                case 2:                                                                                   // Alt 1: up and down, ends once
                {
                    const int cyc = std::max (1, 2 * total - 2);
                    const int k = st % cyc;
                    emit (noteAt (k < total ? k : cyc - k, false), baseVel, gate, pos, out);
                    break;
                }
                case 3:                                                                                   // Alt 2: ends twice
                {
                    const int cyc = 2 * total;
                    const int k = st % cyc;
                    emit (noteAt (k < total ? k : cyc - 1 - k, false), baseVel, gate, pos, out);
                    break;
                }
                case 4: emit (noteAt (rnd (total), false), baseVel, gate, pos, out); break;              // Random
                case 5: emit (noteAt (st, true), baseVel, gate, pos, out); break;                        // As Played
                case 6:                                                                                   // Chord
                    for (int k = 0; k < count; ++k) emit (notes[(size_t) k] + 12 * ((st / 1) % oct), baseVel, gate, pos, out);
                    break;
                default:
                {
                    // rhythm patterns: { which held note (-1 rest, 9 chord), velocity, gate }
                    struct S { int n; float v, g; };
                    static const S bossa[8] = { { 0, 1, 0.9f }, { -1, 0, 0 }, { 2, 0.7f, 0.5f }, { 1, 0.8f, 0.5f }, { -1, 0, 0 }, { 0, 0.9f, 0.6f }, { 3, 0.7f, 0.5f }, { -1, 0, 0 } };
                    static const S pulse[8] = { { 0, 1, 0.5f }, { -1, 0, 0 }, { -1, 0, 0 }, { 1, 0.85f, 0.5f }, { -1, 0, 0 }, { -1, 0, 0 }, { 2, 0.9f, 0.5f }, { -1, 0, 0 } };
                    static const S gallop[8] = { { 0, 1, 0.4f }, { 0, 0.6f, 0.3f }, { 1, 0.8f, 0.4f }, { -1, 0, 0 }, { 2, 1, 0.4f }, { 2, 0.6f, 0.3f }, { 3, 0.8f, 0.4f }, { -1, 0, 0 } };
                    static const S rise[8] = { { 0, 0.6f, 0.5f }, { 1, 0.7f, 0.5f }, { 2, 0.8f, 0.5f }, { 3, 1, 0.5f }, { 4, 1, 0.5f }, { 3, 0.8f, 0.5f }, { 2, 0.7f, 0.5f }, { 1, 0.6f, 0.5f } };
                    static const S echo[8] = { { 0, 1, 0.3f }, { 0, 0.5f, 0.3f }, { 0, 0.25f, 0.3f }, { 9, 0.9f, 0.8f }, { 1, 1, 0.3f }, { 1, 0.5f, 0.3f }, { 2, 0.8f, 0.3f }, { 2, 0.4f, 0.3f } };
                    const S* seq = s.pattern == 7 ? bossa : s.pattern == 8 ? pulse : s.pattern == 9 ? gallop : s.pattern == 10 ? rise : echo;
                    const auto& e = seq[st % 8];
                    if (e.n < 0) break;
                    const int g = (int) (step * 2.0 * e.g * juce::jlimit (0.05, 1.0, s.gate / 60.0));
                    const int octShift = 12 * ((st / 8) % oct);
                    if (e.n == 9) { for (int k = 0; k < count; ++k) emit (notes[(size_t) k] + octShift, baseVel * e.v, g, pos, out); }
                    else emit (noteAt (e.n, false) + octShift, baseVel * e.v, g, pos, out);
                    break;
                }
            }
        }
    };
}
