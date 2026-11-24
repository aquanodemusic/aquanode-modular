//==============================================================================
// Microtuning (6.9.10) - the pitch of every key
//
// A tuning (historical Western temperaments, world tunings, equal divisions,
// or a Scala .scl file with an optional .kbm keyboard map), a Root note and a
// reference pitch (A4). Two ways to use it:
//
//  * Global: every sound in the patch follows it. The internal generators take
//    the tuned pitch directly (midiNoteToHz goes through the table for the
//    block, Tuning.h), and so do the bundled plugins (their note-to-pitch goes
//    through aquanode::tuning on the same thread; HostedPluginModule::
//    followsTuningItself). Another plugin that plays MPE gets each note on a
//    member channel of its own, bent to its pitch (TuningRetuner, ModuleCore.h).
//  * MIDI Chain: only what its MIDI Out feeds. Each note leaves as an MPE note
//    on the nearest key with its bend set to the tuned pitch, so a generator
//    (its voice bend) or an MPE plugin plays it in tune.
//
// Keys: no new keyboard designs. A 12-note tuning sits on the normal keys (the
// Root key is degree 0). A tuning with more or fewer notes per octave plays
// either "Keys" (each key the degree nearest its usual pitch) or "Linear" (each
// key the next step of the tuning, from the Root key). With Discard MIDI both
// work together: Discard decides which keys sound, Microtuning their pitch; in
// Linear a 12-note key and scale mean little, so Discard's Global key/scale is
// greyed out then (usesLinearKeys).
//
// The numbers: just ratios and the temperaments built from their fifths
// (Werckmeister III, Kirnberger III, Vallotti, Young), quarter-tone (24-TET)
// maqam degrees, the Arel-Ezgi-Uzdilek 53-comma makam degrees, Persian koron
// degrees, average gamelan tunings (every gamelan has its own) and the equal
// divisions. Nothing is taken from other software.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../../Platform/AndroidDocuments.h"

using namespace aquanode;

namespace microtuning
{
    struct Scale
    {
        std::vector<double> cents { 0.0 };   // degree 0 = 0, ascending
        double period { 1200.0 };
        juce::String name;
        int notes() const { return (int) cents.size(); }
    };

    // a Scala keyboard map (.kbm)
    struct KeyMap
    {
        bool valid { false };
        int size { 12 }, first { 0 }, last { 127 }, middle { 60 }, refNote { 69 };
        double refHz { 440.0 };
        int octaveDegree { 12 };
        std::vector<int> map;   // degree per key of the map, -1 = "x" (unmapped)
        juce::String name;
    };

    const juce::StringArray& tuningNames()
    {
        static const juce::StringArray n {
            "12-TET (Off)",
            "Pure Intonation (Major)", "Pure Intonation (Minor)", "Pythagorean", "Quarter-Comma Meantone",
            "Werckmeister III", "Kirnberger III", "Vallotti", "Young",
            "Indian Shruti (Bilawal)", "Indian Shruti (Bhairav)", "Indian Shruti (Todi)",
            "Arabic Rast", "Arabic Bayati", "Arabic Saba", "Turkish Rast", "Turkish Hicaz",
            "Persian Shur", "Persian Segah", "Gamelan Slendro", "Gamelan Pelog", "Thai 7-TET",
            "19-TET", "22-TET", "24-TET", "31-TET", "53-TET", "Bohlen-Pierce",
            "Scala File" };
        return n;
    }
    constexpr int kScalaIndex = 28;

    inline double ratioCents (double r) { return 1200.0 * std::log2 (r); }

    Scale fromCents (std::initializer_list<double> c, double period = 1200.0)
    {
        Scale s;
        s.cents.assign (c.begin(), c.end());
        s.period = period;
        return s;
    }

    Scale fromRatios (std::initializer_list<double> r)
    {
        Scale s;
        s.cents.clear();
        for (double x : r) s.cents.push_back (ratioCents (x));
        return s;
    }

    Scale equal (int n, double period = 1200.0)
    {
        Scale s;
        s.cents.clear();
        for (int i = 0; i < n; ++i) s.cents.push_back (period * i / n);
        s.period = period;
        return s;
    }

    // twelve notes from the circle of fifths C-G, G-D ... F-C, each fifth's size given
    Scale fromFifths (const double (&size)[12])
    {
        static const int circle[12] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };
        Scale s;
        s.cents.assign (12, 0.0);
        double cum = 0.0;
        for (int i = 0; i < 11; ++i)
        {
            cum += size[i];
            s.cents[(size_t) circle[i + 1]] = std::fmod (cum, 1200.0);
        }
        return s;
    }

    // a chain of equal fifths from k = lo to hi (C = 0)
    Scale fifthChain (double fifth, int lo, int hi)
    {
        Scale s;
        s.cents.assign (12, 0.0);
        for (int k = lo; k <= hi; ++k)
        {
            const int pc = ((7 * k) % 12 + 12) % 12;
            double c = std::fmod (k * fifth, 1200.0);
            if (c < 0.0) c += 1200.0;
            s.cents[(size_t) pc] = c;
        }
        return s;
    }

    Scale commas53 (std::initializer_list<int> c)
    {
        Scale s;
        s.cents.clear();
        for (int x : c) s.cents.push_back (1200.0 * x / 53.0);
        return s;
    }

    Scale builtIn (int index)
    {
        const double P5 = ratioCents (1.5), PC = ratioCents (531441.0 / 524288.0), SC = ratioCents (81.0 / 80.0);
        const double schisma = PC - SC;
        switch (index)
        {
            case 1: return fromRatios ({ 1, 16.0/15, 9.0/8, 6.0/5, 5.0/4, 4.0/3, 45.0/32, 3.0/2, 8.0/5, 5.0/3, 16.0/9, 15.0/8 });
            case 2: return fromRatios ({ 1, 16.0/15, 9.0/8, 6.0/5, 5.0/4, 4.0/3, 64.0/45, 3.0/2, 8.0/5, 5.0/3, 9.0/5, 15.0/8 });
            case 3: return fifthChain (P5, -3, 8);                          // Eb .. G#
            case 4: return fifthChain (P5 - SC / 4.0, -3, 8);               // Eb .. G#
            case 5: { const double q = P5 - PC / 4.0; const double f[12] = { q, q, q, P5, P5, q, P5, P5, P5, P5, P5, P5 }; return fromFifths (f); }
            case 6: { const double q = P5 - SC / 4.0; const double f[12] = { q, q, q, q, P5, P5, P5 - schisma, P5, P5, P5, P5, P5 }; return fromFifths (f); }
            case 7: { const double t = P5 - PC / 6.0; const double f[12] = { t, t, t, t, t, P5, P5, P5, P5, P5, P5, t }; return fromFifths (f); }
            case 8: { const double t = P5 - PC / 6.0; const double f[12] = { t, t, t, t, t, t, P5, P5, P5, P5, P5, P5 }; return fromFifths (f); }
            // twelve of the 22 shrutis, as the raga takes them
            case 9:  return fromRatios ({ 1, 16.0/15, 9.0/8, 6.0/5, 5.0/4, 4.0/3, 45.0/32, 3.0/2, 8.0/5, 5.0/3, 9.0/5, 15.0/8 });
            case 10: return fromRatios ({ 1, 256.0/243, 9.0/8, 32.0/27, 5.0/4, 4.0/3, 45.0/32, 3.0/2, 128.0/81, 5.0/3, 16.0/9, 15.0/8 });
            case 11: return fromRatios ({ 1, 256.0/243, 9.0/8, 32.0/27, 5.0/4, 4.0/3, 729.0/512, 3.0/2, 128.0/81, 5.0/3, 16.0/9, 243.0/128 });
            // maqam degrees in quarter tones, on the keys nearest them (Root = the tonic)
            case 12: return fromCents ({ 0, 100, 200, 300, 350, 500, 600, 700, 800, 900, 1000, 1050 });
            case 13: return fromCents ({ 0, 150, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100 });
            case 14: return fromCents ({ 0, 150, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100 });
            // makam degrees in 53-commas (Arel-Ezgi-Uzdilek)
            case 15: return commas53 ({ 0, 4, 9, 13, 17, 22, 26, 31, 35, 40, 44, 48 });
            case 16: return commas53 ({ 0, 5, 9, 13, 17, 22, 26, 31, 35, 40, 44, 48 });
            // Persian: Shur's koron second (and Pythagorean-like flats), Segah from its koron tonic
            case 17: return fromCents ({ 0, 135, 200, 294, 400, 498, 600, 702, 792, 900, 996, 1100 });
            case 18: return fromCents ({ 0, 150, 200, 350, 400, 500, 600, 700, 850, 900, 1050, 1100 });
            // average gamelan tunings (every gamelan has its own)
            case 19: return fromCents ({ 0, 231, 474, 717, 955 });
            case 20: return fromCents ({ 0, 120, 258, 539, 675, 785, 943 });
            case 21: return equal (7);
            case 22: return equal (19);
            case 23: return equal (22);
            case 24: return equal (24);
            case 25: return equal (31);
            case 26: return equal (53);
            case 27: return equal (13, ratioCents (3.0));   // Bohlen-Pierce: 13 steps of the tritave 3:1
            default: break;
        }
        return equal (12);
    }

    // every built-in tuning, made once
    const std::vector<Scale>& allBuiltIn()
    {
        static const std::vector<Scale> all = []
        {
            std::vector<Scale> v;
            for (int i = 0; i < kScalaIndex; ++i)
                v.push_back (builtIn (i));
            return v;
        }();
        return all;
    }

    //=== Scala files ===========================================================
    juce::StringArray dataLines (const juce::String& text)
    {
        juce::StringArray lines, out;
        lines.addLines (text);
        for (auto& l : lines)
            if (! l.trimStart().startsWithChar ('!'))
                out.add (l);
        return out;
    }

    // a pitch line: cents (with a dot) or a ratio a/b (or a whole number)
    bool parsePitch (const juce::String& line, double& cents)
    {
        const auto s = line.trim().upToFirstOccurrenceOf (" ", false, false).trim();
        if (s.isEmpty()) return false;
        if (s.containsChar ('.'))
        {
            cents = s.getDoubleValue();
            return true;
        }
        const double a = s.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
        const double b = s.containsChar ('/') ? s.fromFirstOccurrenceOf ("/", false, false).getDoubleValue() : 1.0;
        if (a <= 0.0 || b <= 0.0) return false;
        cents = ratioCents (a / b);
        return true;
    }

    juce::String parseScl (const juce::String& text, Scale& out)
    {
        const auto lines = dataLines (text);
        if (lines.size() < 2) return "not a Scala scale";
        const int n = lines[1].trim().getIntValue();
        if (n < 1 || n > 1024 || lines.size() < 2 + n) return "not a Scala scale";
        Scale s;
        s.name = lines[0].trim();
        s.cents = { 0.0 };
        for (int i = 0; i < n; ++i)
        {
            double c = 0.0;
            if (! parsePitch (lines[2 + i], c)) return "a pitch line could not be read";
            if (i < n - 1) s.cents.push_back (c);
            else s.period = c;
        }
        if (s.period <= 0.0) return "its period is not above the start";
        std::sort (s.cents.begin(), s.cents.end());
        out = s;
        return {};
    }

    juce::String parseKbm (const juce::String& text, KeyMap& out)
    {
        const auto lines = dataLines (text);
        if (lines.size() < 7) return "not a Scala keyboard map";
        KeyMap k;
        k.size = lines[0].trim().getIntValue();
        k.first = juce::jlimit (0, 127, lines[1].trim().getIntValue());
        k.last = juce::jlimit (0, 127, lines[2].trim().getIntValue());
        k.middle = lines[3].trim().getIntValue();
        k.refNote = juce::jlimit (0, 127, lines[4].trim().getIntValue());
        k.refHz = lines[5].trim().getDoubleValue();
        k.octaveDegree = lines[6].trim().getIntValue();
        if (k.size < 0 || k.size > 1024 || k.refHz <= 0.0) return "not a Scala keyboard map";
        for (int i = 0; i < k.size; ++i)
        {
            const auto l = i + 7 < lines.size() ? lines[i + 7].trim() : juce::String ("x");
            k.map.push_back (l.startsWithIgnoreCase ("x") || l.isEmpty() ? -1 : l.getIntValue());
        }
        k.valid = true;
        out = k;
        return {};
    }

    inline int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

    double degreeCents (const Scale& s, int degree)
    {
        const int n = juce::jmax (1, s.notes());
        const int oct = floorDiv (degree, n);
        return s.cents[(size_t) (degree - oct * n)] + oct * s.period;
    }

    // every key's frequency
    void build (tuning::Table& t, const Scale& s, int tuningIndex, bool linear, int root, double refHz, const KeyMap* km)
    {
        const int n = juce::jmax (1, s.notes());
        if (km != nullptr && km->valid)
        {
            // the keyboard map decides: which degree each key plays, and the reference
            auto keyCents = [&] (int key, bool& mapped) -> double
            {
                mapped = key >= km->first && key <= km->last;
                const int off = key - km->middle;
                if (km->size == 0)
                    return degreeCents (s, off);
                const int octs = floorDiv (off, km->size);
                const int idx = off - octs * km->size;
                const int deg = km->map[(size_t) idx];
                if (deg < 0) { mapped = false; return degreeCents (s, octs * km->octaveDegree + idx); }
                return degreeCents (s, octs * km->octaveDegree + deg);
            };
            bool m = true;
            const double refC = keyCents (km->refNote, m);
            for (int k = 0; k < 128; ++k)
            {
                bool mapped = true;
                const double c = keyCents (k, mapped);
                t.hz[k] = km->refHz * std::exp2 ((c - refC) / 1200.0);
                t.mapped[k] = mapped;
            }
            t.linear = km->size != 12 && ! (km->size == 0 && n == 12);
            t.identity = false;
            return;
        }

        const int rootKey = 60 + juce::jlimit (0, 11, root);
        const double fRoot = refHz * std::exp2 ((rootKey - 69) / 12.0);
        const bool steps = linear || n == 12;
        for (int k = 0; k < 128; ++k)
        {
            const int d = k - rootKey;
            double c = 0.0;
            if (steps)
                c = degreeCents (s, d);
            else
            {
                // Keys: the degree nearest the key's usual pitch
                const double target = d * 100.0;
                const double oct = std::floor (target / s.period);
                const double within = target - oct * s.period;
                double best = 0.0, bestDist = 1.0e9;
                for (int i = 0; i <= n; ++i)
                {
                    const double cand = i < n ? s.cents[(size_t) i] : s.period;
                    if (std::abs (cand - within) < bestDist - 1.0e-9) { bestDist = std::abs (cand - within); best = cand; }
                }
                c = oct * s.period + best;
            }
            t.hz[k] = fRoot * std::exp2 (c / 1200.0);
            t.mapped[k] = true;
        }
        t.linear = linear && n != 12;
        t.identity = tuningIndex == 0 && std::abs (refHz - 440.0) < 1.0e-6;
    }
}

//==============================================================================
class MicrotuningModule final : public SynthModule
{
public:
    enum P { pTuning, pRoot, pRef, pScope, pMapping, pDraw };

    MicrotuningModule() { microtuning::allBuiltIn(); }   // (made here, on the message thread)

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.microtuning";
        d.displayName = "Microtuning";
        d.searchKeywords = "tuning microtonal temperament pure just intonation scala scl kbm maqam raga gamelan edo tet a432";
        d.description =
            "The pitch of every key: historical Western tunings, world tunings, equal divisions of the octave, or a "
            "Scala file (.scl, with a .kbm keyboard map if you like). Root is the key that plays degree 0, A4 the "
            "reference pitch. Global retunes every sound in the patch - the generators and the bundled plugins (Vita, "
            "VirtualDX7, Massif, AM-20, FM12...) directly, MPE switch or not; another plugin that plays MPE by a pitch "
            "bend per note on a channel of its own. MIDI Chain retunes "
            "only what its MIDI Out feeds (into a generator's MIDI In). A 12-note tuning sits on the usual keys; one "
            "with more or fewer notes per octave plays Keys (each key the nearest degree) or Linear (each key the "
            "next step). With Discard MIDI: Discard decides which keys sound, Microtuning their pitch. Bayati and "
            "Saba share their quarter tone (on the 2nd) and differ in the notes their scale plays. Draw: drag the "
            "twelve bars up and down - each key's own offset from the Root, up to 50 cents either way, on top of the "
            "tuning (saved with the patch; Off leaves them out). With Linear steps the on-screen keys show each "
            "key's step (and where the scale's octave - its period - starts).";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 21;
        d.sockets = { midiIn ("midiIn", "MIDI In"), midiOut ("midiOut", "MIDI Out") };

        d.params.push_back (makeCombo ("tuning", "Tuning", microtuning::tuningNames(), 0, 0, 3).noMod());
        {
            juce::StringArray roots { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            d.params.push_back (makeCombo ("root", "Root", roots, 0, 0, 2).noMod());
        }
        auto ref = makeRotary ("ref", "A4", 392.0f, 470.0f, 440.0f, 1, "Hz", false, 0.1f).noMod();
        d.params.push_back (ref);
        d.params.push_back (makeCombo ("scope", "Mode", { "Global", "MIDI Chain" }, 0, 1, 2).noMod());
        d.params.push_back (makeCombo ("mapping", "Mapping", { "Keys", "Linear" }, 0, 1, 2).noMod());
        // (6.9.15) Draw: the twelve bars on the card can be dragged up and down - each key's own
        // offset (from the Root) on top of the tuning, up to a quarter tone each way
        d.params.push_back (makeCombo ("draw", "Draw", { "Off", "On" }, 0, 1, 1).noMod().whenMissing (0.0f));
        return d;
    }

    // which keys sound stays with the player (and Discard MIDI): a cable from
    // here replaces the keys at either MIDI input
    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiProcessor() const override { return true; }
    bool acceptsMpeEvents() const override { return true; }

    //=== the table (audio thread) ================================================
    void blockStart() override
    {
        refreshTable();
    }

    const tuning::Table* globalTuningTable() const override
    {
        return isGlobal() && built ? &table : nullptr;
    }

    bool usesLinearKeys() const override { return built && table.linear; }

    // (6.9.15) for Aquanode's on-screen keys: the steps of a Linear scale that is not 12 notes
    bool keyboardSteps (int& notesPerPeriod, int& degreeZeroKey) const override
    {
        if (getParameterBase (pScope) > 0.5f || getParameterBase (pMapping) < 0.5f) return false;
        tuning::Table t;
        microtuning::Scale sc;
        tableForView (t, sc);
        if (sc.notes() == 12 || sc.notes() < 1) return false;
        notesPerPeriod = sc.notes();
        degreeZeroKey = 60 + juce::jlimit (0, 11, (int) std::lround (getParameterBase (pRoot)));
        return true;
    }

    //=== Draw (6.9.15): each of the twelve keys from the Root, its own offset in cents ===
    float drawnOffset (int i) const { const juce::SpinLock::ScopedLockType sl (customLock); return drawn[(size_t) juce::jlimit (0, 11, i)]; }
    void setDrawnOffset (int i, float cents)
    {
        {
            const juce::SpinLock::ScopedLockType sl (customLock);
            drawn[(size_t) juce::jlimit (0, 11, i)] = juce::jlimit (-50.0f, 50.0f, cents);
        }
        ++customSerial;
    }
    bool drawOn (bool base = true) const { return (base ? getParameterBase (pDraw) : param (pDraw)) > 0.5f; }

    //=== MIDI Chain: each note on its nearest key, bent to its pitch ===========
    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent* out, int maxOut) override
    {
        if (maxOut <= 0)
            return 0;
        if (isGlobal() || ! built)
        {
            out[0] = in;   // (Global: the whole patch is tuned already)
            return 1;
        }
        const int key = juce::jlimit (0, 127, in.note);
        auto& h = held[key];
        if (in.isExpression)
        {
            if (! h.on) return 0;
            out[0] = in;
            out[0].note = h.outNote;
            out[0].mpe = true;
            out[0].bend = in.bend + h.offset;
            return 1;
        }
        if (! in.isOn)
        {
            if (! h.on) return 0;
            h.on = false;
            MidiDriverEvent off;
            off.note = h.outNote;
            off.velocity = 0.0f;
            off.isOn = false;
            off.mpe = true;
            out[0] = off;
            return 1;
        }
        int n = 0;
        if (h.on)
        {
            MidiDriverEvent off;
            off.note = h.outNote;
            off.velocity = 0.0f;
            off.isOn = false;
            off.mpe = true;
            out[n++] = off;
            h.on = false;
        }
        if (! table.mapped[key] || n >= maxOut)
            return n;
        const double p = tuning::tablePitch (table, key);
        h.outNote = juce::jlimit (0, 127, (int) std::lround (p));
        h.offset = (float) (p - h.outNote);
        h.on = true;
        auto e = in;
        e.note = h.outNote;
        e.mpe = true;
        e.bend = (in.mpe ? in.bend : 0.0f) + h.offset;
        out[n++] = e;
        return n;
    }

    void midiAllNotesOff() override { for (auto& h : held) h.on = false; }
    void reset() override { midiAllNotesOff(); }

    //=== Scala files (message thread) ============================================
    juce::String loadScalaText (const juce::String& text, bool isKbm, const juce::String& fileName)
    {
        if (isKbm)
        {
            microtuning::KeyMap k;
            if (auto problem = microtuning::parseKbm (text, k); problem.isNotEmpty())
                return problem;
            k.name = fileName;
            const juce::SpinLock::ScopedLockType sl (customLock);
            keyMap = k;
            kbmText = text;
        }
        else
        {
            microtuning::Scale s;
            if (auto problem = microtuning::parseScl (text, s); problem.isNotEmpty())
                return problem;
            if (s.name.isEmpty()) s.name = fileName;
            const juce::SpinLock::ScopedLockType sl (customLock);
            custom = s;
            sclText = text;
        }
        ++customSerial;
        setParameter ("tuning", (float) microtuning::kScalaIndex);
        return {};
    }

    void clearKeyMap()
    {
        {
            const juce::SpinLock::ScopedLockType sl (customLock);
            keyMap = {};
            kbmText.clear();
        }
        ++customSerial;
    }

    bool hasKeyMap() const { const juce::SpinLock::ScopedLockType sl (customLock); return keyMap.valid; }
    juce::String keyMapName() const { const juce::SpinLock::ScopedLockType sl (customLock); return keyMap.name; }

    juce::String saveCustomState() const override
    {
        const juce::SpinLock::ScopedLockType sl (customLock);
        bool anyDrawn = false;
        for (float c : drawn) anyDrawn = anyDrawn || c != 0.0f;
        if (sclText.isEmpty() && kbmText.isEmpty() && ! anyDrawn)
            return {};
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("scl", sclText);
        o->setProperty ("kbm", kbmText);
        o->setProperty ("kbmName", keyMap.name);
        if (anyDrawn)
        {
            juce::Array<juce::var> d;
            for (float c : drawn) d.add ((double) c);
            o->setProperty ("drawn", d);
        }
        return juce::JSON::toString (juce::var (o.get()), true);
    }

    void loadCustomState (const juce::String& state) override
    {
        const auto v = juce::JSON::parse (state);
        if (! v.isObject()) return;
        const auto scl = v.getProperty ("scl", {}).toString(), kbm = v.getProperty ("kbm", {}).toString();
        microtuning::Scale s;
        microtuning::KeyMap k;
        const bool sOk = scl.isNotEmpty() && microtuning::parseScl (scl, s).isEmpty();
        const bool kOk = kbm.isNotEmpty() && microtuning::parseKbm (kbm, k).isEmpty();
        k.name = v.getProperty ("kbmName", {}).toString();
        {
            const juce::SpinLock::ScopedLockType sl (customLock);
            if (sOk) { custom = s; sclText = scl; }
            if (kOk) { keyMap = k; kbmText = kbm; }
            drawn.fill (0.0f);
            if (const auto* d = v.getProperty ("drawn", {}).getArray())
                for (int i = 0; i < juce::jmin (12, d->size()); ++i) drawn[(size_t) i] = (float) (double) (*d)[i];
        }
        ++customSerial;
    }

    // for the card (message thread): the same table, worked out here
    void tableForView (tuning::Table& t, microtuning::Scale& s) const
    {
        const int idx = tuningIndex (true);
        microtuning::KeyMap km;
        if (idx == microtuning::kScalaIndex)
        {
            const juce::SpinLock::ScopedLockType sl (customLock);
            s = custom;
            km = keyMap;
        }
        else
            s = microtuning::allBuiltIn()[(size_t) idx];
        microtuning::build (t, s, idx, mappingLinear (true), rootIndex (true), refHz (true), idx == microtuning::kScalaIndex ? &km : nullptr);
        if (drawOn (true))
        {
            std::array<float, 12> d;
            { const juce::SpinLock::ScopedLockType sl (customLock); d = drawn; }
            applyDrawn (t, d, rootIndex (true));
        }
    }

    // (6.9.15) the drawn offsets on a table: each key by its place from the Root
    static void applyDrawn (tuning::Table& t, const std::array<float, 12>& d, int root)
    {
        bool any = false;
        for (float c : d) any = any || c != 0.0f;
        if (! any) return;
        for (int k = 0; k < 128; ++k)
        {
            const int i = ((k - root) % 12 + 12) % 12;
            if (d[(size_t) i] != 0.0f) t.hz[k] *= std::pow (2.0, (double) d[(size_t) i] / 1200.0);
        }
        t.identity = false;
    }

    bool isGlobalForView() const { return getParameterBase (pScope) < 0.5f; }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 112; }

private:
    bool isGlobal() const { return param (pScope) < 0.5f; }
    int tuningIndex (bool base = false) const
    {
        return juce::jlimit (0, microtuning::kScalaIndex, (int) std::lround (base ? getParameterBase (pTuning) : param (pTuning)));
    }
    int rootIndex (bool base = false) const { return juce::jlimit (0, 11, (int) std::lround (base ? getParameterBase (pRoot) : param (pRoot))); }
    double refHz (bool base = false) const { return juce::jlimit (300.0, 500.0, (double) (base ? getParameterBase (pRef) : param (pRef))); }
    bool mappingLinear (bool base = false) const { return (base ? getParameterBase (pMapping) : param (pMapping)) > 0.5f; }

    // (audio thread) the table again when anything it is made of moved
    void refreshTable()
    {
        const int idx = tuningIndex();
        const int root = rootIndex();
        const double ref = refHz();
        const bool lin = mappingLinear();
        const bool drw = drawOn (false);
        const unsigned serialNow = customSerial.load();
        if (built && idx == lastIndex && root == lastRoot && ref == lastRef && lin == lastLinear && serialNow == lastSerial && drw == lastDraw)
            return;
        std::array<float, 12> d {};
        if (drw)
        {
            const juce::SpinLock::ScopedTryLockType sl (customLock);
            if (! sl.isLocked()) return;   // (being drawn right now: the next block)
            d = drawn;
        }
        if (idx == microtuning::kScalaIndex)
        {
            const juce::SpinLock::ScopedTryLockType sl (customLock);
            if (! sl.isLocked())
                return;   // (being loaded right now: the next block)
            microtuning::build (table, custom, idx, lin, root, ref, &keyMap);
        }
        else
        {
            microtuning::build (table, microtuning::allBuiltIn()[(size_t) idx], idx, lin, root, ref, nullptr);
        }
        if (drw) applyDrawn (table, d, root);
        ++table.serial;
        built = true;
        lastIndex = idx; lastRoot = root; lastRef = ref; lastLinear = lin; lastSerial = serialNow; lastDraw = drw;
    }

    tuning::Table table;
    bool built { false };
    int lastIndex { -1 }, lastRoot { -1 };
    double lastRef { 0.0 };
    bool lastLinear { false }, lastDraw { false };
    unsigned lastSerial { 0 };
    std::array<float, 12> drawn {};   // (6.9.15) Draw

    struct Held { bool on { false }; int outNote { 0 }; float offset { 0.0f }; };
    Held held[128];

    mutable juce::SpinLock customLock;
    microtuning::Scale custom { microtuning::equal (12) };
    microtuning::KeyMap keyMap;
    juce::String sclText, kbmText;
    std::atomic<unsigned> customSerial { 1 };
};

namespace
{
    // the card: what the tuning does to the twelve keys from the Root, and the
    // Scala buttons
    class MicrotuningView final : public juce::Component,
                                  public aquanode::ExtraContentParamListener,
                                  private juce::Timer
    {
    public:
        explicit MicrotuningView (MicrotuningModule& m) : module (&m)
        {
            for (auto* b : { &loadButton, &clearButton })
            {
                addAndMakeVisible (b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff22303a));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            }
            loadButton.setTooltip ("Loads a Scala scale (.scl) or keyboard map (.kbm)");
            clearButton.setTooltip ("Takes the keyboard map away (the scale stays)");
            loadButton.onClick = [this] { load(); };
            clearButton.onClick = [this]
            {
                if (auto* mt = dynamic_cast<MicrotuningModule*> (module.get())) mt->clearKeyMap();
                timerCallback();
            };
            startTimerHz (6);
            timerCallback();
        }

        void resized() override
        {
            auto top = getLocalBounds().withTrimmedTop (18).removeFromTop (22).reduced (2, 1);
            loadButton.setBounds (top.removeFromRight (96));
            top.removeFromRight (4);
            clearButton.setBounds (top.removeFromRight (78));
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (r.withTrimmedTop (44.0f), 3.0f);

            // the summary over the buttons, the whole width
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawFittedText (summary, juce::Rectangle<int> (2, 0, getWidth() - 4, 18), juce::Justification::centredLeft, 1, 0.6f);

            // the twelve keys from the Root: how far each sits from its usual pitch
            auto area = r.withTrimmedTop (46.0f).reduced (4.0f, 2.0f);
            const float w = area.getWidth() / 12.0f;
            const float mid = area.getY() + (area.getHeight() - 12.0f) * 0.5f;
            const float half = (area.getHeight() - 12.0f) * 0.5f - 2.0f;
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRect (area.getX(), mid, area.getWidth(), 1.0f);
            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            for (int i = 0; i < 12; ++i)
            {
                const float dev = devs[(size_t) i];
                const float x = area.getX() + (float) i * w;
                const float h = juce::jlimit (-1.0f, 1.0f, dev / 50.0f) * half;
                const auto bar = juce::Rectangle<float> (x + w * 0.2f, h < 0 ? mid : mid - h, w * 0.6f, std::abs (h) + 1.0f);
                g.setColour (! mappedKeys[(size_t) i] ? juce::Colours::grey.withAlpha (0.3f)
                                                      : (std::abs (dev) > 50.0f ? juce::Colour (0xffffb36b) : juce::Colour (0xff6fd0c8)));
                g.fillRect (bar);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.setFont (juce::Font (juce::FontOptions (8.5f)));
                g.drawText (names[(rootPc + i) % 12], juce::Rectangle<float> (x, area.getBottom() - 11.0f, w, 11.0f), juce::Justification::centred, false);
            }
            g.setFont (juce::Font (juce::FontOptions (8.5f)));
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.drawText ("+50 c", area.withHeight (10.0f), juce::Justification::topRight, false);
            if (auto* mt = dynamic_cast<MicrotuningModule*> (module.get()); mt != nullptr && mt->drawOn())
            {
                g.setColour (juce::Colour (0xffffd23f).withAlpha (0.8f));
                g.drawText ("DRAW: drag the bars", area.withHeight (10.0f), juce::Justification::topLeft, false);
            }
        }

        // (6.9.15) Draw: drag a bar up or down to set that key's offset
        void mouseDown (const juce::MouseEvent& e) override { drawAt (e.position); }
        void mouseDrag (const juce::MouseEvent& e) override { drawAt (e.position); }

    private:
        void drawAt (juce::Point<float> p)
        {
            auto* m = dynamic_cast<MicrotuningModule*> (module.get());
            if (m == nullptr || ! m->drawOn()) return;
            auto area = getLocalBounds().toFloat().withTrimmedTop (46.0f).reduced (4.0f, 2.0f);
            if (! area.expanded (0.0f, 6.0f).contains (p)) return;
            const float w = area.getWidth() / 12.0f;
            const int i = juce::jlimit (0, 11, (int) ((p.x - area.getX()) / w));
            const float mid = area.getY() + (area.getHeight() - 12.0f) * 0.5f;
            const float half = (area.getHeight() - 12.0f) * 0.5f - 2.0f;
            const float want = juce::jlimit (-50.0f, 50.0f, (mid - p.y) / juce::jmax (1.0f, half) * 50.0f);
            // the bar shows the tuning and the offset together: the offset is what is left
            const float base = devs[(size_t) i] - m->drawnOffset (i);
            m->setDrawnOffset (i, want - base);
            timerCallback();
        }

        void load()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a Scala file", juce::File(), aqp::android::pickerPatterns ("*.scl;*.kbm"));
            juce::Component::SafePointer<MicrotuningView> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto* m = dynamic_cast<MicrotuningModule*> (safe->module.get());
                const auto url = fc.getURLResult();
                if (m == nullptr || url.isEmpty()) return;
                juce::MemoryBlock data;
                if (const auto problem = aqp::files::readDocument (url, data); problem.isNotEmpty()) return;
                juce::String name = url.isLocalFile() ? url.getLocalFile().getFileName() : juce::String();
               #if JUCE_ANDROID
                if (name.isEmpty())
                    if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                        name = doc.getInfo().getName();
               #endif
                if (name.isEmpty()) name = juce::URL::removeEscapeChars (url.getFileName());
                const auto text = data.toString();
                const bool kbm = name.endsWithIgnoreCase (".kbm")
                              || (! name.endsWithIgnoreCase (".scl") && microtuning::dataLines (text).size() >= 7
                                  && microtuning::dataLines (text)[5].containsChar ('.'));
                const auto problem = m->loadScalaText (text, kbm, name.upToLastOccurrenceOf (".", false, false));
                if (problem.isNotEmpty())
                {
                    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Microtuning", "The file could not be loaded: " + problem + ".");
                    return;
                }
                safe->timerCallback();
                if (safe->paramValuesChangedByContent != nullptr)
                    safe->paramValuesChangedByContent();   // (the Tuning menu reads Scala File)
            });
        }

        void timerCallback() override
        {
            auto* m = dynamic_cast<MicrotuningModule*> (module.get());
            if (m == nullptr) return;
            tuning::Table t;
            microtuning::Scale s;
            m->tableForView (t, s);
            rootPc = juce::jlimit (0, 11, (int) std::lround (m->getParameterBase (MicrotuningModule::pRoot)));
            const int rootKey = 60 + rootPc;
            for (int i = 0; i < 12; ++i)
            {
                const int k = rootKey + i;
                devs[(size_t) i] = (float) (100.0 * (tuning::tablePitch (t, k) - (double) k));
                mappedKeys[(size_t) i] = t.mapped[k];
            }
            juce::String sum;
            const int idx = juce::jlimit (0, microtuning::kScalaIndex, (int) std::lround (m->getParameterBase (MicrotuningModule::pTuning)));
            if (idx == microtuning::kScalaIndex && s.name.isNotEmpty()) sum << s.name << ": ";
            sum << s.notes() << " notes per " << (std::abs (s.period - 1200.0) < 0.01 ? juce::String ("octave") : juce::String (s.period, 1) + " c");
            if (s.notes() != 12 && ! m->hasKeyMap())
                sum << (m->getParameterBase (MicrotuningModule::pMapping) > 0.5f ? ", each key the next step" : ", each key the nearest degree");
            if (idx == microtuning::kScalaIndex && m->hasKeyMap())
                sum << ", map " << m->keyMapName() << " (Root and A4 from the map)";
            sum << (m->isGlobalForView() ? "  |  every sound" : "  |  its MIDI Out");
            const bool showClear = idx == microtuning::kScalaIndex && m->hasKeyMap();
            if (sum != summary || showClear != clearButton.isVisible())
            {
                summary = sum;
                clearButton.setVisible (showClear);
            }
            repaint();
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton loadButton { "Load Scala" }, clearButton { "Clear Map" };
        std::unique_ptr<juce::FileChooser> chooser;
        std::array<float, 12> devs {};
        std::array<bool, 12> mappedKeys {};
        int rootPc { 0 };
        juce::String summary;
    };
}

std::unique_ptr<juce::Component> MicrotuningModule::createExtraContentComponent()
{
    return std::make_unique<MicrotuningView> (*this);
}

AQUANODE_REGISTER_MODULE (MicrotuningModule, MicrotuningModule::descriptor)

#if AQUANODE_TEST_HARNESS
// (tests) a Scala text into a Microtuning module, as its Load button would
juce::String microtuningLoadForTest (aquanode::SynthModule* m, const juce::String& text, bool kbm)
{
    if (auto* mt = dynamic_cast<MicrotuningModule*> (m))
        return mt->loadScalaText (text, kbm, "test");
    return "not a Microtuning module";
}
#endif

#if AQUANODE_TEST_HARNESS
// (tests, 6.9.15) a drawn offset, as a drag on the card would set it
void microtuningDrawForTest (aquanode::SynthModule* m, int key, float cents)
{
    if (auto* mt = dynamic_cast<MicrotuningModule*> (m))
        mt->setDrawnOffset (key, cents);
}
#endif
