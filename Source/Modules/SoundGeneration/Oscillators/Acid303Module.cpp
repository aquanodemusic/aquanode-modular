#include "Acid303Module.h"
#include "Acid303Calibration.h"

using namespace aquanode;

namespace
{
    constexpr int kFmInput = 1;          // sockets: Midi In, FM In
    constexpr double kGateLength = 0.5;  // a step's gate, as on the 303: the first half of the step
    // the fitted voice plays at the recordings' level; this brings it up towards the others' (a
    // plain note at the default knobs peaks near -7 dBFS, the loudest accents near 0 dBFS)
    constexpr double kMakeupDb = 9.0;

    const char* kNoteNames[13] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C" };
}

//==============================================================================
// patterns
Acid303Pattern Acid303Pattern::classic()
{
    // note, octave, accent, slide, time (0 note, 1 tie, 2 rest)
    static const int line[16][5] = {
        { 0, 0, 1, 0, 0 }, { 0, 1, 0, 1, 0 }, { 3, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
        { 7, 0, 1, 0, 0 }, { 0, 0, 0, 0, 2 }, { 10, 0, 0, 1, 0 }, { 0, 1, 1, 0, 0 },
        { 0, 0, 0, 0, 0 }, { 5, 0, 0, 1, 0 }, { 7, 0, 1, 0, 0 }, { 7, 0, 0, 0, 1 },
        { 0, 0, 0, 0, 0 }, { 3, 1, 1, 1, 0 }, { 0, 1, 0, 0, 0 }, { 7, -1, 0, 0, 0 } };
    Acid303Pattern p;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto* l = line[i % 16];
        auto& s = p.steps[(size_t) i];
        s.note = (int8_t) l[0]; s.octave = (int8_t) l[1]; s.accent = l[2] != 0; s.slide = l[3] != 0; s.time = (uint8_t) l[4];
    }
    return p;
}

Acid303Pattern Acid303Pattern::random (juce::Random& rng, int bars)
{
    // an acid line: a minor-ish scale, the root and the fifth most of all, some octave jumps,
    // accents and slides scattered, a few rests and ties - and every bar after the first a
    // variation of it rather than something new
    static const int scales[4][7] = { { 0, 2, 3, 5, 7, 8, 10 },     // minor
                                      { 0, 1, 3, 5, 7, 8, 10 },     // phrygian
                                      { 0, 2, 3, 5, 7, 9, 10 },     // dorian
                                      { 0, 3, 5, 6, 7, 10, 12 } };  // blues
    const int* sc = scales[rng.nextInt (4)];
    auto pickNote = [&]
    {
        const float r = rng.nextFloat();
        if (r < 0.34f) return 0;
        if (r < 0.48f) return 7;
        if (r < 0.56f) return 12;
        return juce::jmin (12, sc[rng.nextInt (7)]);
    };
    const float accentP = 0.18f + 0.14f * rng.nextFloat(), slideP = 0.12f + 0.14f * rng.nextFloat();
    const float restP = 0.06f + 0.12f * rng.nextFloat(), tieP = 0.04f + 0.08f * rng.nextFloat();
    Acid303Pattern p;
    for (int i = 0; i < kStepsPerBar; ++i)
    {
        auto& s = p.steps[(size_t) i];
        s.note = (int8_t) pickNote();
        const float o = rng.nextFloat();
        s.octave = (int8_t) (o < 0.18f ? 1 : o < 0.27f ? -1 : 0);
        s.accent = rng.nextFloat() < accentP;
        s.slide = rng.nextFloat() < slideP;
        const float t = rng.nextFloat();
        s.time = (uint8_t) (i > 0 && t < tieP ? 1 : t < tieP + restP ? 2 : 0);
    }
    p.steps[0].time = 0;
    for (int i = kStepsPerBar; i < kMaxSteps; ++i)
    {
        auto s = p.steps[(size_t) (i % kStepsPerBar)];
        if (i / kStepsPerBar < bars && rng.nextFloat() < 0.25f)
        {
            // a variation: another note, accent or slide here and there
            switch (rng.nextInt (3))
            {
                case 0:  s.note = (int8_t) pickNote(); break;
                case 1:  s.accent = ! s.accent; break;
                default: s.slide = ! s.slide; break;
            }
        }
        p.steps[(size_t) i] = s;
    }
    return p;
}

const char* Acid303Pattern::hookStyleName (int style)
{
    static const char* n[kNumHookStyles] = { "ANY", "ARP", "OCT", "ACID", "DRIFT", "BLOOM", "TIDE" };
    return n[juce::jlimit (0, kNumHookStyles - 1, style)];
}

//==============================================================================
// The long lines: four bars that start from a home loop, wander off it gently and come back,
// so every pass of the pattern finds the same loop again - the slow, liquid pentatonic 303
// of ambient and psychill rather than a techno riff. Few accents, slides between near notes,
// ties for the longer notes.
//   DRIFT  a flowing sixteenth loop; bar 2 changes a note or two, bar 3 drifts up or down
//          the scale, bar 4 is home again
//   BLOOM  a few long notes; each bar adds one or two more until bar 3 is in full bloom,
//          bar 4 folds back to the few
//   TIDE   a rolling wave up and down the scale; it swells over bars 2 and 3 (and slips
//          against the bar), bar 4 is the first bar's wave again, settling on the root
namespace
{
    struct LongStep { int degree { 0 }; int time { 0 }; bool accent { false }, slide { false }; };
    using LongBar = std::array<LongStep, Acid303Pattern::kStepsPerBar>;

    // the pentatonic scales: minor, suspended (no third), major
    const int kPentas[3][5] = { { 0, 3, 5, 7, 10 }, { 0, 2, 5, 7, 10 }, { 0, 2, 4, 7, 9 } };

    int pentaSemis (const int* scale, int degree)
    {
        const int oct = degree >= 0 ? degree / 5 : -((4 - degree) / 5);
        return scale[degree - 5 * oct] + 12 * oct;
    }

    // a tied step holds the note before it: it carries that note's degree, so the lanes read right
    void settleTies (LongBar& bar, int degreeBefore)
    {
        int last = degreeBefore;
        for (auto& st : bar)
        {
            if (st.time == 1) { st.degree = last; st.accent = false; }
            else if (st.time == 0) last = st.degree;
        }
    }

    // slides only between near notes, into a note (not a rest)
    void settleSlides (std::array<LongStep, Acid303Pattern::kMaxSteps>& line)
    {
        for (int i = 0; i < Acid303Pattern::kMaxSteps; ++i)
        {
            auto& st = line[(size_t) i];
            const auto& next = line[(size_t) ((i + 1) % Acid303Pattern::kMaxSteps)];
            if (st.time == 2 || next.time == 2 || std::abs (next.degree - st.degree) > 2)
                st.slide = false;
        }
    }

    // one or two gentle changes: a note a step up or down the scale, here and there
    void nudge (LongBar& bar, juce::Random& rng, int count, int lo, int hi)
    {
        for (int n = 0; n < count; ++n)
            for (int tries = 0; tries < 8; ++tries)
            {
                const int i = 1 + rng.nextInt (Acid303Pattern::kStepsPerBar - 1);
                auto& st = bar[(size_t) i];
                if (st.time != 0 || st.accent)
                    continue;
                st.degree = juce::jlimit (lo, hi, st.degree + (rng.nextBool() ? 1 : -1));
                break;
            }
    }

    LongBar driftHome (juce::Random& rng)
    {
        LongBar a;
        // a walk on the scale that keeps leaning back to the root and its octave
        const int len = rng.nextFloat() < 0.65f ? 8 : 16;
        int d = 0;
        for (int i = 0; i < len; ++i)
        {
            if (i > 0)
            {
                const float r = rng.nextFloat();
                const int stepBy = r < 0.30f ? -1 : r < 0.60f ? 1 : r < 0.72f ? 0 : r < 0.84f ? 2 : r < 0.92f ? -2 : (d >= 3 ? -5 : 5);
                d = juce::jlimit (-2, 7, d + stepBy);
                if (i % 4 == 0 && rng.nextFloat() < 0.5f)
                    d = rng.nextFloat() < 0.7f ? 0 : 5;
            }
            a[(size_t) i].degree = d;
        }
        if (len == 8)
        {
            // the second half answers the first: the same, its last notes leading home
            for (int i = 8; i < 16; ++i)
                a[(size_t) i].degree = a[(size_t) (i - 8)].degree;
            a[14].degree = juce::jlimit (-2, 7, a[13].degree + (rng.nextBool() ? 1 : -1));
            a[15].degree = rng.nextFloat() < 0.6f ? 1 : -1;
        }
        else if (std::abs (a[15].degree) > 2)
            a[15].degree = rng.nextBool() ? 1 : 2;
        // longer notes and a breath now and then
        const float tieP = 0.10f + 0.10f * rng.nextFloat();
        for (int i = 1; i < 16; ++i)
        {
            const float r = rng.nextFloat();
            if (r < 0.04f && a[(size_t) (i - 1)].time != 2) a[(size_t) i].time = 2;
            else if (r < 0.04f + tieP && a[(size_t) (i - 1)].time != 2) a[(size_t) i].time = 1;
        }
        // a light, fixed emphasis
        static const int accentSets[5][2] = { { 0, -1 }, { 0, 8 }, { 0, 10 }, { 6, -1 }, { -1, -1 } };
        const auto* acc = accentSets[rng.nextInt (5)];
        for (int k = 0; k < 2; ++k)
            if (acc[k] >= 0 && a[(size_t) acc[k]].time == 0)
                a[(size_t) acc[k]].accent = true;
        const float slideP = 0.15f + 0.15f * rng.nextFloat();
        for (auto& st : a)
            st.slide = rng.nextFloat() < slideP;
        settleTies (a, 0);
        return a;
    }

    LongBar bloomSeed (juce::Random& rng)
    {
        LongBar a;
        static const int starts[6][4] = { { 0, 6, 12, -1 }, { 0, 8, -1, -1 }, { 0, 10, -1, -1 },
                                          { 0, 6, 10, -1 }, { 0, 4, 10, -1 }, { 0, 3, 8, 12 } };
        const auto* st = starts[rng.nextInt (6)];
        static const int partners[6] = { 3, 5, 3, 2, 4, 5 };   // the fifth and the octave most
        for (auto& s : a) s.time = 1;
        for (int k = 0; k < 4 && st[k] >= 0; ++k)
        {
            auto& s = a[(size_t) st[k]];
            s.time = 0;
            s.degree = k == 0 ? 0 : partners[rng.nextInt (6)] - (rng.nextFloat() < 0.2f ? 5 : 0);
            // a breath before the next long note
            const int next = k + 1 < 4 && st[k + 1] >= 0 ? st[k + 1] : 16;
            if (next - st[k] >= 4 && rng.nextFloat() < 0.4f)
                a[(size_t) (next - 1)].time = 2;
        }
        a[0].accent = rng.nextFloat() < 0.4f;
        settleTies (a, 0);
        return a;
    }

    // one or two more notes into a long one: a neighbour on the scale, often slid into
    void bloomGrow (LongBar& bar, juce::Random& rng, int count, bool peak)
    {
        for (int n = 0; n < count; ++n)
            for (int tries = 0; tries < 12; ++tries)
            {
                const int i = 2 + rng.nextInt (Acid303Pattern::kStepsPerBar - 2);
                if (bar[(size_t) i].time != 1)
                    continue;
                const int here = bar[(size_t) i].degree;
                bar[(size_t) i].time = 0;
                bar[(size_t) i].degree = juce::jlimit (-2, 9, here + (peak && n == 0 ? (rng.nextBool() ? 5 : 3)
                                                                                    : (rng.nextBool() ? 1 : -1)));
                bar[(size_t) (i - 1)].slide = rng.nextFloat() < 0.6f;
                break;
            }
    }

    LongBar tideBar (juce::Random& rng, int period, int amp, int offset, int phaseFrom, float tieP, float accentP)
    {
        LongBar a;
        const int half = juce::jmax (1, period / 2);
        for (int i = 0; i < Acid303Pattern::kStepsPerBar; ++i)
        {
            const int k = (phaseFrom + i) % period;
            const int tri = k <= half ? k : period - k;
            a[(size_t) i].degree = offset + (int) std::lround ((double) tri * amp / half);
            // slides where the wave turns, a held crest now and then
            a[(size_t) i].slide = k == half - 1 || k == period - 1;
            if (k == half + 1 && k < period && rng.nextFloat() < tieP) a[(size_t) i].time = 1;   // holding the crest
            a[(size_t) i].accent = k == 0 && rng.nextFloat() < accentP;
        }
        return a;
    }
}

Acid303Pattern Acid303Pattern::longLine (juce::Random& rng, int style)
{
    const float sr = rng.nextFloat();
    const int* scale = kPentas[sr < 0.6f ? 0 : sr < 0.85f ? 1 : 2];
    std::array<LongStep, kMaxSteps> line {};
    auto put = [&line] (int bar, const LongBar& b) { for (int i = 0; i < kStepsPerBar; ++i) line[(size_t) (bar * kStepsPerBar + i)] = b[(size_t) i]; };

    if (style == 5)
    {
        const auto seed = bloomSeed (rng);
        auto grown = seed;
        bloomGrow (grown, rng, 1 + rng.nextInt (2), false);
        settleTies (grown, 0);
        auto full = grown;
        bloomGrow (full, rng, 1 + rng.nextInt (2), true);
        settleTies (full, 0);
        auto home = seed;
        if (rng.nextFloat() < 0.5f) { home[15].time = 0; home[15].degree = rng.nextBool() ? 1 : -1; home[14].slide = true; home[15].slide = true; }
        put (0, seed); put (1, grown); put (2, full); put (3, home);
    }
    else if (style == 6)
    {
        const float pr = rng.nextFloat();
        const int period = pr < 0.5f ? 6 : pr < 0.85f ? 8 : 5;
        const int amp = 3 + rng.nextInt (2);
        const float tieP = 0.15f + 0.25f * rng.nextFloat(), accentP = 0.15f + 0.25f * rng.nextFloat();
        const int swell = rng.nextBool() ? 1 : 2;
        auto first = tideBar (rng, period, amp, 0, 0, tieP, accentP);
        // bars 2 and 3 run on across the bar line, so the wave slips against it; bar 3 rides higher
        auto second = tideBar (rng, period, amp + 1, 0, kStepsPerBar % period, tieP, accentP);
        auto third = tideBar (rng, period, amp + swell, rng.nextBool() ? 1 : 0, (2 * kStepsPerBar) % period, tieP, accentP);
        auto home = first;
        home[14].degree = 2; home[14].time = 0; home[15].degree = 1; home[15].time = 0;
        home[14].slide = home[15].slide = true;   // settling on the root as the loop comes round
        settleTies (first, 0); settleTies (second, 0); settleTies (third, 0); settleTies (home, 0);
        put (0, first); put (1, second); put (2, third); put (3, home);
    }
    else
    {
        const auto home = driftHome (rng);
        auto again = home;
        nudge (again, rng, 1 + rng.nextInt (2), -2, 7);
        settleTies (again, 0);
        auto away = home;
        const float dr = rng.nextFloat();
        const int shift = dr < 0.35f ? 1 : dr < 0.6f ? 2 : dr < 0.85f ? -1 : 0;
        for (int i = 0; i < kStepsPerBar; ++i)
            away[(size_t) i].degree = juce::jlimit (-3, 9, away[(size_t) i].degree + (shift != 0 ? shift : (i >= 8 ? 5 : 0)));
        nudge (away, rng, 1 + rng.nextInt (2), -3, 9);
        away[15].degree = rng.nextBool() ? 1 : -1;   // turning back towards home
        if (away[15].time == 1) away[15].time = 0;
        settleTies (away, 0);
        auto back = home;
        if (rng.nextFloat() < 0.5f) back[15].slide = true;
        // home, home again with a change, away, home - or now and then away a bar early
        if (rng.nextFloat() < 0.7f) { put (0, home); put (1, again); put (2, away); put (3, back); }
        else                        { put (0, home); put (1, away); put (2, again); put (3, back); }
    }

    line[0].time = 0;
    settleSlides (line);
    Acid303Pattern p;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto& l = line[(size_t) i];
        int semis = pentaSemis (scale, l.degree), oct = 0;
        while (semis > 12) { semis -= 12; ++oct; }
        while (semis < 0)  { semis += 12; --oct; }
        auto& s = p.steps[(size_t) i];
        s.note = (int8_t) semis;
        s.octave = (int8_t) juce::jlimit (-1, 1, oct);
        s.accent = l.accent && l.time == 0;
        s.slide = l.slide;
        s.time = (uint8_t) l.time;
    }
    return p;
}

Acid303Pattern Acid303Pattern::hook (juce::Random& rng, int bars, int style)
{
    bars = juce::jlimit (1, kMaxBars, bars);
    if (isLongStyle (style))
        return longLine (rng, style);
    if (style <= 0 || style > 3)
        style = 1 + rng.nextInt (3);
    if (style == 3)
        return random (rng, bars);

    Acid303Pattern p;
    auto set = [&p] (int i, int semis, bool accent, bool slide, int time = 0)
    {
        // semitones from the low C, -12..24: the note within the octave and the octave key
        auto& s = p.steps[(size_t) i];
        int oct = 0;
        while (semis > 12) { semis -= 12; ++oct; }
        while (semis < 0)  { semis += 12; --oct; }
        if (oct > 1) oct = 1;
        s.note = (int8_t) semis; s.octave = (int8_t) oct;
        s.accent = accent; s.slide = slide; s.time = (uint8_t) time;
    };
    // where the roots of the bars go: a one-bar hook stays home, longer ones move and come back
    static const int moves[6][4] = { { 0, 0, 0, 0 }, { 0, 0, 5, 3 }, { 0, 3, 5, 7 }, { 0, -2, -4, -5 },
                                     { 0, 5, 0, 7 }, { 0, 0, -2, 3 } };
    const int* move = bars == 1 ? moves[0] : moves[1 + rng.nextInt (5)];
    // accents on fixed places every bar, so the groove repeats
    static const int accentSets[4][4] = { { 0, 6, 10, -1 }, { 0, 4, 8, 12 }, { 2, 7, 12, -1 }, { 0, 3, 8, 14 } };
    const int* accents = accentSets[rng.nextInt (4)];
    auto accented = [accents] (int st) { for (int a = 0; a < 4; ++a) if (accents[a] == st) return true; return false; };

    if (style == 1)
    {
        // a pentatonic arp: a four-step shape on the scale, the bar's last step turning back home
        static const int minor[] = { 0, 3, 5, 7, 10, 12, 15, 17, 19 }, major[] = { 0, 2, 4, 7, 9, 12, 14, 16, 19 };
        const int* scale = rng.nextFloat() < 0.75f ? minor : major;
        static const int shapes[6][4] = { { 0, 1, 2, 3 }, { 0, 2, 4, 2 }, { 0, 2, 1, 3 }, { 0, 3, 0, 4 },
                                          { 4, 2, 1, 0 }, { 0, 1, 3, 5 } };
        const int* shape = shapes[rng.nextInt (6)];
        const int lift = rng.nextInt (3);           // the second half of the bar a step or two higher
        const bool slideUp = rng.nextFloat() < 0.6f;
        for (int b = 0; b < bars; ++b)
            for (int st = 0; st < kStepsPerBar; ++st)
            {
                const int half = st / 8, pos = st % 4;
                int degree = shape[pos] + (half == 1 ? lift : 0);
                if (st == 15) degree = b == bars - 1 ? 3 : 1;   // the turnaround: the fifth (or the 2nd degree) leads back
                const int semis = scale[juce::jlimit (0, 8, degree)] + move[b % 4];
                const bool slide = slideUp && pos == 3 && st != 15;
                set (b * kStepsPerBar + st, semis, accented (st), slide);
            }
    }
    else
    {
        // octave steps: the root and its octave in a fixed figure, a fifth or a seventh now and then
        static const int figures[5][16] = {
            { 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12 },
            { 0, 0, 12, 0, 0, 12, 0, 12, 0, 0, 12, 0, 0, 12, 0, 12 },
            { 0, 12, 12, 0, 0, 12, 12, 0, 0, 12, 12, 0, 0, 12, 7, 10 },
            { 0, -99, 12, 0, -99, 12, 0, 12, 0, -99, 12, 0, 7, 12, 10, 12 },
            { 12, 0, 0, 12, 0, 0, 12, 0, 12, 0, 0, 12, 0, 0, 12, 7 } };
        const int* fig = figures[rng.nextInt (5)];
        const bool slideOctaves = rng.nextFloat() < 0.5f;
        for (int b = 0; b < bars; ++b)
            for (int st = 0; st < kStepsPerBar; ++st)
            {
                const int f = fig[st];
                if (f == -99) { set (b * kStepsPerBar + st, move[b % 4], false, false, 2); continue; }
                const bool up = f == 12;
                const bool slide = slideOctaves && ! up && st < 15 && fig[st + 1] == 12 && (st % 4) == 1;
                set (b * kStepsPerBar + st, f + move[b % 4], accented (st) || (up && (st % 4) == 2), slide);
            }
    }
    // the bars after the pattern's length repeat it, so a longer Bars setting plays it again
    for (int i = bars * kStepsPerBar; i < kMaxSteps; ++i)
        p.steps[(size_t) i] = p.steps[(size_t) (i % (bars * kStepsPerBar))];
    return p;
}

juce::String Acid303Pattern::toString() const
{
    // "A3P1 " then five characters a step: note (hex), octave (d - u), accent, slide (0 1),
    // time (n t r)
    juce::String s ("A3P1 ");
    for (const auto& st : steps)
    {
        s << juce::String::toHexString ((int) juce::jlimit (0, 12, (int) st.note)).toLowerCase()
          << (st.octave < 0 ? "d" : st.octave > 0 ? "u" : "-")
          << (st.accent ? "1" : "0") << (st.slide ? "1" : "0")
          << (st.time == 1 ? "t" : st.time == 2 ? "r" : "n");
    }
    return s;
}

bool Acid303Pattern::fromString (const juce::String& text, Acid303Pattern& out)
{
    const auto t = text.trim();
    if (! t.startsWith ("A3P1 "))
        return false;
    const auto body = t.substring (5);
    if (body.length() < 5)
        return false;
    Acid303Pattern p;
    for (int i = 0; i < kMaxSteps && (i + 1) * 5 <= body.length(); ++i)
    {
        const auto c = body.substring (i * 5, i * 5 + 5);
        auto& st = p.steps[(size_t) i];
        st.note = (int8_t) juce::jlimit (0, 12, c.substring (0, 1).getHexValue32());
        st.octave = (int8_t) (c[1] == 'd' ? -1 : c[1] == 'u' ? 1 : 0);
        st.accent = c[2] == '1';
        st.slide = c[3] == '1';
        st.time = (uint8_t) (c[4] == 't' ? 1 : c[4] == 'r' ? 2 : 0);
    }
    out = p;
    return true;
}

//==============================================================================
Acid303Module::Acid303Module()
{
    voiceNote.fill (-1);
}

void Acid303Module::setPattern (const Acid303Pattern& p)
{
    pattern = p;
    {
        const juce::SpinLock::ScopedLockType sl (patternLock);
        pending = p;
    }
    patternVersion.fetch_add (1, std::memory_order_release);
    editCounter.fetch_add (1, std::memory_order_relaxed);
}

juce::String Acid303Module::saveCustomState() const
{
    return pattern.toString();
}

void Acid303Module::loadCustomState (const juce::String& state)
{
    Acid303Pattern p;
    if (Acid303Pattern::fromString (state, p))
        setPattern (p);
}

void Acid303Module::prepare (double sr)
{
    SynthModule::prepare (sr);
    for (auto& v : voices)
        v.prepare (sr, acid303::fittedCalibration());
    reset();
}

void Acid303Module::reset()
{
    for (auto& v : voices)
        v.reset();
    voiceNote.fill (-1);
    voiceAge.fill (0);
    held.clear();
    held.reserve (32);
    step = -1;
    stepPos = 0.0;
    gridNote = -1;
    gateOffDone = true;
    gridWasRunning = false;
    lastMode = lastPlay = lastWave = -1;
    lastSlide = -1.0f;
    uiStep.store (-1, std::memory_order_relaxed);
}

void Acid303Module::blockStart()
{
    const auto v = patternVersion.load (std::memory_order_acquire);
    if (v != seenVersion)
    {
        const juce::SpinLock::ScopedTryLockType sl (patternLock);
        if (sl.isLocked())
        {
            audioPattern = pending;
            seenVersion = v;
        }
    }

    // Play All / Stop All from a Piano Roll (a 303 made after the press only learns the count)
    if (! rollCommandKnown)
    {
        lastRollCommand = hostTransport.rollCommand;
        rollCommandKnown = true;
    }
    else if (hostTransport.rollCommand != lastRollCommand)
    {
        lastRollCommand = hostTransport.rollCommand;
        rollCommandPending = true;
        rollCommandPlays = hostTransport.rollCommandPlays;
    }
}

acid303::Knobs Acid303Module::currentKnobs() const
{
    acid303::Knobs k;
    k.cutoff = param (pCutoff) * 0.01;
    k.resonance = param (pResonance) * 0.01;
    k.envMod = param (pEnvMod) * 0.01;
    k.decay = param (pDecay) * 0.01;
    k.accent = param (pAccent) * 0.01;
    k.accentDecay = param (pAccentDecay) * 0.01;
    k.sweepTime = param (pSweepTime) * 0.01;
    k.track = param (pTrack) * 0.01;
    k.drive = param (pDrive) * 0.01;
    return k;
}

double Acid303Module::noteHz (int note) const
{
    return midiNoteToHz (juce::jlimit (0, 127, note));
}

int Acid303Module::allocateVoice (int note)
{
    // the same key again, else an idle voice, else a released one, else the oldest
    int best = -1;
    for (int v = 0; v < kPolyVoices && best < 0; ++v)
        if (voiceNote[(size_t) v] == note) best = v;
    for (int v = 0; v < kPolyVoices && best < 0; ++v)
        if (voices[(size_t) v].isIdle()) best = v;
    if (best < 0)
    {
        juce::uint32 oldest = 0xffffffffu;
        for (int pass = 0; pass < 2 && best < 0; ++pass)
            for (int v = 0; v < kPolyVoices; ++v)
                if ((pass == 1 || ! voices[(size_t) v].isGateOn()) && voiceAge[(size_t) v] < oldest)
                {
                    oldest = voiceAge[(size_t) v];
                    best = v;
                }
    }
    best = juce::jmax (0, best);
    voiceAge[(size_t) best] = ++ageCounter;
    voiceNote[(size_t) best] = note;
    return best;
}

void Acid303Module::startNote (int note, bool accent, bool slide)
{
    const auto k = currentKnobs();
    if ((int) param (pVoiceMode) == 0)
    {
        voices[0].noteOn (noteHz (note), accent, slide, k);
        voiceNote[0] = note;
    }
    else
    {
        const int v = allocateVoice (note);
        voices[(size_t) v].noteOn (noteHz (note), accent, false, k);
    }
}

void Acid303Module::stopNote (int note)
{
    for (int v = 0; v < kPolyVoices; ++v)
        if (voiceNote[(size_t) v] == note && voices[(size_t) v].isGateOn())
        {
            voices[(size_t) v].noteOff();
            voiceNote[(size_t) v] = -1;
        }
}

void Acid303Module::allVoicesOff()
{
    for (auto& v : voices)
        v.noteOff();
    voiceNote.fill (-1);
    held.clear();
    gridNote = -1;
}

void Acid303Module::rawMidiMessage (const juce::MidiMessage& m)
{
    if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        if ((int) param (pPlay) == 0)
            allVoicesOff();
        return;
    }
    if ((int) param (pPlay) != 0 || ! m.isNoteOnOrOff())
        return;   // Grid plays its own pattern

    const int note = m.getNoteNumber();
    const bool mono = (int) param (pVoiceMode) == 0;
    if (m.isNoteOn())
    {
        const bool accent = m.getFloatVelocity() * 127.0f >= param (pAccentVel) - 0.5f;
        if (mono)
        {
            const bool legato = ! held.empty();
            held.erase (std::remove_if (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; }), held.end());
            if (held.size() < 32)
                held.push_back ({ note, accent });
            startNote (note, accent, legato);   // a note under a held one slides, as on the 303
        }
        else
            startNote (note, accent, false);
        return;
    }

    // note off
    if (mono)
    {
        const bool wasTop = ! held.empty() && held.back().note == note;
        held.erase (std::remove_if (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; }), held.end());
        if (! wasTop)
            return;
        if (! held.empty())
            startNote (held.back().note, held.back().accent, true);   // back to the key still down
        else
        {
            voices[0].noteOff();
            voiceNote[0] = -1;
        }
    }
    else
        stopNote (note);
}

//==============================================================================
// the grid
void Acid303Module::gridStep (int s)
{
    const int L = numBarsAudio() * Acid303Pattern::kStepsPerBar;
    const auto& st = audioPattern.steps[(size_t) s];
    const auto& next = audioPattern.steps[(size_t) ((s + 1) % L)];
    const bool mono = (int) param (pVoiceMode) == 0;
    const int note = juce::jlimit (0, 127, kBaseNote + st.note + 12 * st.octave + (int) std::lround (param (pTranspose)));

    auto release = [this]
    {
        if (gridNote >= 0)
            stopNote (gridNote);
        gridNote = -1;
    };

    if (st.time == 2 || (st.time == 1 && gridNote < 0))
    {
        release();
        gridPrevSlide = false;
        gateOffDone = true;
        return;
    }
    if (st.time == 0)
    {
        if (mono && gridPrevSlide && gridNote >= 0)
        {
            voices[0].noteOn (noteHz (note), st.accent, true, currentKnobs());   // slide: no new envelopes
            voiceNote[0] = note;
        }
        else
        {
            if (! mono) release();
            startNote (note, st.accent, false);
        }
        gridNote = note;
    }
    // a tie keeps the note going; the gate drops halfway through the step unless the step
    // slides on or the next one ties
    gateOffDone = st.slide || next.time == 1;
    gridPrevSlide = st.slide;
}

void Acid303Module::gridAdvance()
{
    bool running = param (pRunning) > 0.5f;
    if (rollCommandPending)
    {
        rollCommandPending = false;
        if (gridNote >= 0) stopNote (gridNote);
        gridNote = -1;
        step = -1;
        gridWasRunning = false;
        running = rollCommandPlays;
        setParameterByIndex (pRunning, running ? 1.0f : 0.0f);
    }
    if (! running)
    {
        if (gridWasRunning)
        {
            if (gridNote >= 0) stopNote (gridNote);
            gridNote = -1;
            step = -1;
            uiStep.store (-1, std::memory_order_relaxed);
        }
        gridWasRunning = false;
        return;
    }

    const bool hostAvailable = hostTransport.hasBpm && hostTransport.bpm > 0.0;
    const bool useHost = param (pSync) > 0.5f && hostAvailable;
    const double bpm = juce::jlimit (10.0, 999.0, useHost ? hostTransport.bpm : (double) param (pTempo));
    uiBpm.store (bpm, std::memory_order_relaxed);
    uiHostTempo.store (hostAvailable, std::memory_order_relaxed);
    // swing: the first sixteenth of every pair longer, the second shorter (100 %: three to one)
    const double sw = 0.5 * juce::jlimit (0.0, 1.0, (double) param (pSwing) * 0.01);
    const double stepSamples = 60.0 / bpm / 4.0 * sampleRate * (step >= 0 && (step % 2) == 1 ? 1.0 - sw : 1.0 + sw);
    const int L = numBarsAudio() * Acid303Pattern::kStepsPerBar;

    if (! gridWasRunning)
    {
        gridWasRunning = true;
        gridPrevSlide = false;
        step = 0;
        stepPos = 0.0;
        gridStep (0);
        uiStep.store (0, std::memory_order_relaxed);
        return;
    }
    stepPos += 1.0;
    if (stepPos >= stepSamples)
    {
        stepPos -= stepSamples;
        step = (step + 1) % L;
        gridStep (step);
        uiStep.store (step, std::memory_order_relaxed);
    }
    else if (! gateOffDone && stepPos >= kGateLength * stepSamples)
    {
        gateOffDone = true;
        if (gridNote >= 0) stopNote (gridNote);
        gridNote = -1;
        gridPrevSlide = false;
    }
}

int Acid303Module::numBarsAudio() const
{
    return juce::jlimit (1, Acid303Pattern::kMaxBars, (int) std::lround (param (pBars)));
}

//==============================================================================
void Acid303Module::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int mode = (int) param (pVoiceMode), play = (int) param (pPlay), wave = (int) param (pWave);
    if (mode != lastMode || play != lastPlay)
    {
        if (lastMode >= 0)
            allVoicesOff();
        step = -1;
        gridWasRunning = false;
        uiStep.store (-1, std::memory_order_relaxed);
        lastMode = mode;
        lastPlay = play;
    }
    if (wave != lastWave)
    {
        for (auto& v : voices) v.setWave (wave);
        lastWave = wave;
    }
    if (const float sl = param (pSlide); sl != lastSlide)
    {
        for (auto& v : voices) v.setSlideTime (sl * 0.001);
        lastSlide = sl;
    }

    if (play == 1)
        gridAdvance();
    else if (rollCommandPending)
        rollCommandPending = false;

    const auto k = currentKnobs();
    const double tune = param (pTune) / 12.0;
    const double fm = (double) inputs[kFmInput][0] * param (pFm);
    double sum = 0.0;
    int sounding = 0;
    const int n = mode == 0 ? 1 : kPolyVoices;
    for (int v = 0; v < n; ++v)
    {
        auto& voice = voices[(size_t) v];
        if (voice.isIdle())
            continue;
        voice.setTuneOctaves (tune);
        sum += voice.process (k, fm);
        ++sounding;
    }
    uiVoices.store (sounding, std::memory_order_relaxed);
    const float g = juce::Decibels::decibelsToGain ((float) (param (pVolume) + kMakeupDb), -100.0f);
    const float out = (float) sum * g;
    outputs[0] = { out, out };
}

//==============================================================================
// the pattern in the card: Cove Groove's lanes - steps in a row, every other group of four
// a shade lighter, the playhead in cyan
namespace
{
    namespace col
    {
        const juce::Colour back    { 0xff17130f };
        const juce::Colour slot    { 0xff1f1915 };
        const juce::Colour text    { 0xffeee2c9 };
        const juce::Colour dim     { 0xffb2a189 };
        const juce::Colour cyan    { 0xff1ad6ec };
        const juce::Colour green   { 0xff35e08a };
        const juce::Colour blue    { 0xff3a8dff };
        const juce::Colour coral   { 0xffff5d4a };
        const juce::Colour cream   { 0xffeadcc0 };
        const juce::Colour amber   { 0xffffb648 };
    }

    juce::Font uiFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    class Acid303Panel final : public juce::Component,
                               public aquanode::ExtraContentParamListener,
                               private juce::Timer
    {
    public:
        explicit Acid303Panel (Acid303Module& m) : module (&m)
        {
            shown = m.getPattern();
            for (auto* b : { &runButton, &randomButton, &clearButton, &syncButton, &styleButton })
            {
                addAndMakeVisible (*b);
                b->setColour (juce::TextButton::buttonColourId, col::slot.brighter (0.12f));
                b->setColour (juce::TextButton::buttonOnColourId, col::cyan.darker (0.35f));
                b->setColour (juce::TextButton::textColourOffId, col::text);
                b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            }
            runButton.setClickingTogglesState (false);
            runButton.onClick = [this] { if (auto* mm = mod()) { mm->setRunning (! mm->isRunning()); refreshButtons(); } };
            syncButton.onClick = [this]
            {
                if (auto* mm = mod())
                {
                    mm->setParameter ("sync", mm->getParameter ("sync") > 0.5f ? 0.0f : 1.0f);
                    refreshButtons();
                }
            };
            randomButton.onClick = [this]
            {
                if (auto* mm = mod())
                {
                    juce::Random rng;
                    const int style = (int) std::lround (mm->getParameter ("hookStyle"));
                    if (Acid303Pattern::isLongStyle (style) && mm->numBars() != Acid303Pattern::kMaxBars)
                    {
                        // the long lines are four bars: the loop, its wandering and the way home
                        mm->setParameter ("bars", (float) Acid303Pattern::kMaxBars);
                        refreshButtons();
                        if (paramValuesChangedByContent) paramValuesChangedByContent();
                    }
                    edit (Acid303Pattern::hook (rng, mm->numBars(), style));
                }
            };
            styleButton.setTooltip ("What RANDOM makes: a pentatonic ARP, OCTave steps, an ACID line or ANY of those three - "
                                    "or a long, gentle 4-bar line that wanders off its loop and comes home: DRIFT (a flowing "
                                    "sixteenth loop), BLOOM (a few long notes that grow and fold back), TIDE (a rolling "
                                    "pentatonic wave that swells and settles)");
            styleButton.onClick = [this]
            {
                if (auto* mm = mod())
                {
                    mm->setParameter ("hookStyle", (float) (((int) std::lround (mm->getParameter ("hookStyle")) + 1) % Acid303Pattern::kNumHookStyles));
                    refreshButtons();
                }
            };
            clearButton.onClick = [this]
            {
                Acid303Pattern p;
                for (auto& s : p.steps) s.time = 2;
                edit (p);
            };
            for (int b = 0; b < Acid303Pattern::kMaxBars; ++b)
            {
                auto& bb = barButtons[(size_t) b];
                bb.setButtonText (juce::String (b + 1));
                bb.setColour (juce::TextButton::buttonColourId, col::slot.brighter (0.12f));
                bb.setColour (juce::TextButton::buttonOnColourId, col::cyan.darker (0.35f));
                bb.setColour (juce::TextButton::textColourOffId, col::text);
                bb.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
                bb.onClick = [this, b]
                {
                    if (auto* mm = mod())
                    {
                        mm->setParameter ("bars", (float) (b + 1));
                        refreshButtons();
                        repaint();
                        if (paramValuesChangedByContent) paramValuesChangedByContent();
                    }
                };
                addAndMakeVisible (bb);
            }
            refreshButtons();
            startTimerHz (30);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 3);
            auto top = r.removeFromTop (22);
            runButton.setBounds (top.removeFromLeft (48));
            top.removeFromLeft (8);
            barsLabelArea = top.removeFromLeft (34);
            for (auto& b : barButtons) b.setBounds (top.removeFromLeft (22).reduced (1, 0));
            top.removeFromLeft (10);
            tempoArea = top.removeFromLeft (92);
            syncButton.setBounds (top.removeFromLeft (52));
            top.removeFromLeft (10);
            transposeArea = top.removeFromLeft (84);
            clearButton.setBounds (top.removeFromRight (52));
            top.removeFromRight (4);
            randomButton.setBounds (top.removeFromRight (64));
            top.removeFromRight (2);
            styleButton.setBounds (top.removeFromRight (46));
            r.removeFromTop (5);
            lanesArea = r;
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = mod();
            g.setColour (col::back);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            if (m == nullptr) return;

            // header texts
            g.setFont (uiFont (10.0f, true));
            g.setColour (col::dim);
            g.drawText ("BARS", barsLabelArea, juce::Justification::centredRight, false);
            {
                const bool host = m->getParameter ("sync") > 0.5f;
                const bool hostOk = m->isHostTempoAvailable();
                const double bpm = host && hostOk ? m->getEffectiveBpm() : (double) m->getParameter ("tempo");
                auto t = tempoArea.reduced (2, 1);
                g.setColour (col::slot.brighter (0.06f));
                g.fillRoundedRectangle (t.toFloat(), 3.0f);
                g.setColour (host ? col::green : col::text);
                g.setFont (uiFont (11.0f, true));
                g.drawText (juce::String (bpm, 1) + " BPM", t, juce::Justification::centred, false);
                auto x = transposeArea.reduced (2, 1);
                g.setColour (col::slot.brighter (0.06f));
                g.fillRoundedRectangle (x.toFloat(), 3.0f);
                const int tr = (int) std::lround (m->getParameter ("transpose"));
                g.setColour (tr == 0 ? col::dim : col::amber);
                g.drawText ("KEY " + juce::String (tr > 0 ? "+" : "") + juce::String (tr), x, juce::Justification::centred, false);
            }

            const int L = m->numBars() * Acid303Pattern::kStepsPerBar;
            const int playing = m->getPlayStep();
            auto lanes = lanesArea;
            auto labels = lanes.removeFromLeft (kLabelW);
            const float w = (float) lanes.getWidth() / (float) L;
            const char* names[kLanes] = { "NOTE", "OCT", "ACC", "SLIDE", "TIME" };
            const juce::Colour laneCol[kLanes] = { col::cyan, col::blue, col::coral, col::green, col::cream };
            for (int lane = 0; lane < kLanes; ++lane)
            {
                const auto row = laneRect (lane);
                g.setFont (uiFont (9.5f, true));
                g.setColour (col::dim);
                g.drawText (names[lane], labels.withY (row.getY()).withHeight (row.getHeight()), juce::Justification::centredLeft, false);
                for (int s = 0; s < L; ++s)
                {
                    const auto r = slotRect (lane, s, L);
                    const auto& st = shown.steps[(size_t) s];
                    const bool lightGroup = ((s / 4) % 2) == 0;
                    g.setColour ((lightGroup ? col::slot.brighter (0.16f) : col::slot.brighter (0.05f)).interpolatedWith (laneCol[lane], 0.10f));
                    g.fillRect (r);
                    g.setColour (juce::Colours::black.withAlpha (0.35f));
                    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.2f);
                    const bool rest = st.time == 2;
                    switch (lane)
                    {
                        case 0:   // the note: a bar as tall as the pitch, its name on top
                        {
                            const float v = (float) st.note / 12.0f;
                            const auto bar = r.withTop (r.getBottom() - r.getHeight() * (0.10f + 0.80f * v));
                            g.setColour (laneCol[0].withAlpha (rest ? 0.25f : st.time == 1 ? 0.55f : 1.0f));
                            g.fillRect (bar);
                            if (w >= 13.0f)
                            {
                                g.setColour (col::text.withAlpha (rest ? 0.35f : 0.9f));
                                g.setFont (uiFont (juce::jlimit (7.0f, 10.0f, w * 0.42f), true));
                                g.drawText (kNoteNames[juce::jlimit (0, 12, (int) st.note)], r.withHeight (12.0f), juce::Justification::centred, false);
                            }
                            break;
                        }
                        case 1:   // the octave: an arrow up or down
                            if (st.octave != 0)
                            {
                                const auto c = r.reduced (juce::jmax (1.0f, r.getWidth() * 0.22f), 3.0f);
                                juce::Path p;
                                if (st.octave > 0) p.addTriangle (c.getX(), c.getBottom(), c.getRight(), c.getBottom(), c.getCentreX(), c.getY());
                                else               p.addTriangle (c.getX(), c.getY(), c.getRight(), c.getY(), c.getCentreX(), c.getBottom());
                                g.setColour (laneCol[1].withAlpha (rest ? 0.35f : 1.0f));
                                g.fillPath (p);
                            }
                            break;
                        case 2:   // the accent
                        case 3:   // the slide: a pill, the slide reaching on into the next step
                        {
                            const bool on = lane == 2 ? st.accent : st.slide;
                            if (on)
                            {
                                auto pill = r.reduced (juce::jmax (1.0f, r.getWidth() * 0.12f), r.getHeight() * 0.22f);
                                if (lane == 3 && s + 1 < L) pill = pill.withRight (r.getRight() + r.getWidth() * 0.5f);
                                g.setColour (laneCol[lane].withAlpha (rest ? 0.35f : 1.0f));
                                g.fillRoundedRectangle (pill, juce::jmin (pill.getHeight(), pill.getWidth()) * 0.5f);
                            }
                            break;
                        }
                        default:  // the time: a dot for a note, a line on for a tie, nothing for a rest
                        {
                            g.setColour (laneCol[4]);
                            if (st.time == 0)
                            {
                                const float d = juce::jmin (r.getWidth() - 2.0f, r.getHeight() * 0.55f);
                                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (r.getCentre()));
                            }
                            else if (st.time == 1)
                                g.fillRect (r.getX() - 1.0f, r.getCentreY() - 1.5f, r.getWidth() + 2.0f, 3.0f);
                            break;
                        }
                    }
                }
            }
            // the bars
            g.setColour (col::cyan.withAlpha (0.45f));
            for (int b = 1; b < m->numBars(); ++b)
            {
                const float x = (float) lanes.getX() + (float) (b * Acid303Pattern::kStepsPerBar) * w;
                g.fillRect (x - 0.75f, (float) lanes.getY(), 1.5f, (float) lanes.getHeight());
            }
            // the playhead
            if (playing >= 0 && playing < L)
            {
                const auto top = slotRect (0, playing, L);
                const auto colRect = juce::Rectangle<float> (top.getX(), (float) lanes.getY(), top.getWidth(), (float) lanes.getHeight());
                g.setColour (col::cyan.withAlpha (0.13f));
                g.fillRect (colRect);
                g.setColour (col::cyan.withAlpha (0.85f));
                g.fillRect (colRect.getX() + 1.0f, colRect.getY(), colRect.getWidth() - 2.0f, 2.0f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* m = mod();
            if (m == nullptr) return;
            const auto pos = e.getPosition();
            dragStartY = pos.y;
            if (tempoArea.contains (pos) || transposeArea.contains (pos))
            {
                dragging = tempoArea.contains (pos) ? DragTempo : DragKey;
                dragStartValue = m->getParameter (dragging == DragTempo ? "tempo" : "transpose");
                return;
            }
            int lane, s;
            if (! cellAt (pos, lane, s)) { dragging = DragNone; return; }
            editing = shown;
            auto& st = editing.steps[(size_t) s];
            pressLane = lane;
            lastStep = s;
            switch (lane)
            {
                case 0: dragging = DragNote; setNoteFromY (st, lane, pos.y); break;
                case 1: dragging = DragNone; st.octave = (int8_t) (st.octave == 0 ? 1 : st.octave > 0 ? -1 : 0); break;
                case 2: dragging = DragPaint; paintOn = ! st.accent; st.accent = paintOn; break;
                case 3: dragging = DragPaint; paintOn = ! st.slide; st.slide = paintOn; break;
                default: dragging = DragNone; st.time = (uint8_t) ((st.time + 1) % 3); break;
            }
            edit (editing);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* m = mod();
            if (m == nullptr) return;
            const auto pos = e.getPosition();
            if (dragging == DragTempo || dragging == DragKey)
            {
                const float dy = (float) (dragStartY - pos.y);
                if (dragging == DragTempo)
                    m->setParameter ("tempo", juce::jlimit (20.0f, 300.0f, std::round ((dragStartValue + dy * 0.5f) * 2.0f) * 0.5f));
                else
                    m->setParameter ("transpose", juce::jlimit (-12.0f, 12.0f, std::round (dragStartValue + dy / 8.0f)));
                repaint();
                return;
            }
            if (dragging != DragNote && dragging != DragPaint) return;
            const int L = m->numBars() * Acid303Pattern::kStepsPerBar;
            const int s = juce::jlimit (0, L - 1, (int) ((float) (pos.x - lanesArea.getX() - kLabelW) / ((float) (lanesArea.getWidth() - kLabelW) / (float) L)));
            auto& st = editing.steps[(size_t) s];
            if (dragging == DragNote) setNoteFromY (st, 0, pos.y);
            else if (pressLane == 2) st.accent = paintOn;
            else st.slide = paintOn;
            lastStep = s;
            edit (editing);
        }

        void mouseUp (const juce::MouseEvent&) override { dragging = DragNone; }

    private:
        static constexpr int kLanes = 5, kLabelW = 40;
        enum Drag { DragNone, DragNote, DragPaint, DragTempo, DragKey };

        Acid303Module* mod() const { return dynamic_cast<Acid303Module*> (module.get()); }

        void edit (const Acid303Pattern& p)
        {
            if (auto* m = mod())
            {
                m->setPattern (p);
                shown = p;
                shownCounter = m->getEditCounter();
                repaint();
            }
        }

        void refreshButtons()
        {
            if (auto* m = mod())
            {
                runButton.setToggleState (m->isRunning(), juce::dontSendNotification);
                runButton.setButtonText (m->isRunning() ? "STOP" : "RUN");
                styleButton.setButtonText (Acid303Pattern::hookStyleName ((int) std::lround (m->getParameter ("hookStyle"))));
                syncButton.setToggleState (m->getParameter ("sync") > 0.5f, juce::dontSendNotification);
                const int bars = m->numBars();
                for (int b = 0; b < Acid303Pattern::kMaxBars; ++b)
                    barButtons[(size_t) b].setToggleState (b + 1 == bars, juce::dontSendNotification);
            }
        }

        // the lanes: the note lane takes most of the height
        juce::Rectangle<float> laneRect (int lane) const
        {
            const float H = (float) lanesArea.getHeight();
            const float small = juce::jmax (14.0f, (H - 6.0f) * 0.135f);
            const float noteH = H - 4.0f * small - 4.0f;
            float y = (float) lanesArea.getY();
            if (lane == 0) return { (float) lanesArea.getX(), y, (float) lanesArea.getWidth(), noteH };
            y += noteH + 4.0f + small * (float) (lane - 1);
            return { (float) lanesArea.getX(), y, (float) lanesArea.getWidth(), small };
        }

        juce::Rectangle<float> slotRect (int lane, int s, int L) const
        {
            const auto row = laneRect (lane);
            const float x0 = row.getX() + (float) kLabelW, w = (row.getWidth() - (float) kLabelW) / (float) L;
            return { x0 + (float) s * w + 1.0f, row.getY() + 1.0f, juce::jmax (1.0f, w - 2.0f), row.getHeight() - 2.0f };
        }

        bool cellAt (juce::Point<int> pos, int& lane, int& s) const
        {
            auto* m = mod();
            if (m == nullptr || ! lanesArea.contains (pos) || pos.x < lanesArea.getX() + kLabelW) return false;
            const int L = m->numBars() * Acid303Pattern::kStepsPerBar;
            for (lane = 0; lane < kLanes; ++lane)
                if (laneRect (lane).contains (pos.toFloat()))
                {
                    s = juce::jlimit (0, L - 1, (int) ((float) (pos.x - lanesArea.getX() - kLabelW) / ((float) (lanesArea.getWidth() - kLabelW) / (float) L)));
                    return true;
                }
            return false;
        }

        void setNoteFromY (Acid303Step& st, int lane, int y) const
        {
            const auto r = laneRect (lane);
            const float v = juce::jlimit (0.0f, 1.0f, (r.getBottom() - (float) y - r.getHeight() * 0.10f) / (r.getHeight() * 0.80f));
            st.note = (int8_t) juce::jlimit (0, 12, (int) std::lround (v * 12.0f));
            if (st.time == 2) st.time = 0;   // setting a note on a rest plays it
        }

        void timerCallback() override
        {
            if (auto* m = mod())
            {
                if (m->getEditCounter() != shownCounter && dragging == DragNone)
                {
                    shown = m->getPattern();
                    shownCounter = m->getEditCounter();
                }
                const int ps = m->getPlayStep();
                const bool run = m->isRunning();
                if (ps != shownPlay || run != shownRun || m->numBars() != shownBars)
                {
                    shownPlay = ps; shownRun = run; shownBars = m->numBars();
                    refreshButtons();
                    repaint();
                }
            }
        }

        juce::WeakReference<aquanode::SynthModule> module;
        Acid303Pattern shown, editing;
        int shownCounter { -1 }, shownPlay { -2 }, shownBars { -1 };
        bool shownRun { false };
        juce::TextButton runButton { "RUN" }, randomButton { "RANDOM" }, clearButton { "CLEAR" }, syncButton { "HOST" }, styleButton { "ANY" };
        std::array<juce::TextButton, Acid303Pattern::kMaxBars> barButtons;
        juce::Rectangle<int> barsLabelArea, tempoArea, transposeArea, lanesArea;
        Drag dragging { DragNone };
        int pressLane { -1 }, lastStep { -1 }, dragStartY { 0 };
        float dragStartValue { 0.0f };
        bool paintOn { false };
    };
}

std::unique_ptr<juce::Component> Acid303Module::createExtraContentComponent()
{
    return std::make_unique<Acid303Panel> (*this);
}

//==============================================================================
static ModuleDescriptor acid303Descriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.acid303";
    d.displayName = "303";
    d.description =
        "A bass line synth fitted to recordings of the original hardware: its saw and its slanted square, the "
        "four-pole filter with resonance, the cutoff envelope (Env Mod, Decay), the accent sweep and the VCA. "
        "Free: played from its Midi In, a note as loud as Acc Vel is an accent; Mono slides into a note played "
        "under a held one, Poly gives every note its own voice (no slide). Grid: its own 303 pattern of 1-4 "
        "bars in the card - note, octave, accent, slide and note/tie/rest lanes - on its own tempo or the "
        "host's; Play All on a Piano Roll starts it too. FM In: phase modulation, as on the other oscillators.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 30;
    d.sockets = {
        midiIn   ("midiIn",   "Midi In"),
        modIn    ("fmIn",     "FM In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("wave",      "Wave",   { "Saw", "Square" }, 0, 0, 2).noMod(),
        makeCombo  ("voiceMode", "Voices", { "Mono", "Poly" }, 0, 0, 2).noMod(),
        makeCombo  ("play",      "Play",   { "Free", "Grid" }, 0, 0, 2).noMod(),
        makeRotary ("cutoff",    "Cutoff",    0.0f, 100.0f, 45.0f, 1, "%"),
        makeRotary ("resonance", "Resonance", 0.0f, 100.0f, 60.0f, 1, "%"),
        makeRotary ("envMod",    "Env Mod",   0.0f, 100.0f, 55.0f, 1, "%"),
        makeRotary ("decay",     "Decay",     0.0f, 100.0f, 40.0f, 1, "%"),
        makeRotary ("accent",    "Accent",    0.0f, 100.0f, 70.0f, 1, "%"),
        makeRotary ("volume",    "Volume",  -48.0f, 12.0f, 0.0f, 1, "dB"),
        makeRotary ("tune",      "Tune",    -12.0f, 12.0f, 0.0f, 2, "st"),
        makeRotary ("slide",     "Slide",    10.0f, 300.0f, 60.0f, 2, "ms", true).visibleWhen ("voiceMode", 0.0f),
        makeRotary ("accVel",    "Acc Vel",   1.0f, 127.0f, 100.0f, 2, {}, false, 1.0f).visibleWhen ("play", 0.0f),
        makeRotary ("fm",        "FM",        0.0f, 4.0f, 1.0f, 2),
        // the grid's own, in the card
        makeRotary ("bars",      "Bars",      1.0f, (float) Acid303Pattern::kMaxBars, 1.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("tempo",     "Tempo",    20.0f, 300.0f, 126.0f, 9, {}, false, 0.5f).hide().noMod(),
        makeCombo  ("sync",      "Clock",  { "Own Tempo", "Host Tempo" }, 0, 9, 2).hide().noMod(),
        makeRotary ("running",   "Running",   0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("transpose", "Key",     -12.0f, 12.0f, 0.0f, 9, {}, false, 1.0f).hide().noMod(),
        // beyond the 303's own knobs - at their defaults it is the 303 as measured
        makeRotary ("accDecay",  "Acc Decay", 25.0f, 400.0f, 100.0f, 3, "%", true),
        makeRotary ("sweep",     "Acc Sweep", 25.0f, 400.0f, 100.0f, 3, "%", true),
        makeRotary ("track",     "Key Track", 0.0f, 100.0f, 0.0f, 3, "%"),
        makeRotary ("drive",     "Drive",     0.0f, 100.0f, 0.0f, 3, "%"),
        makeRotary ("swing",     "Swing",     0.0f, 100.0f, 0.0f, 3, "%").visibleWhen ("play", 1.0f),
        makeRotary ("hookStyle", "Style",     0.0f, (float) (Acid303Pattern::kNumHookStyles - 1), 0.0f, 9, {}, false, 1.0f).hide().noMod()
    };
    d.params.push_back (makeOutputTrim (-4.5f));   // brought down to sit with the other sources
    return d;
}

AQUANODE_REGISTER_MODULE (Acid303Module, acid303Descriptor)
