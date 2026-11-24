//==============================================================================
// MIDI Bloom (6.7.10) - lush chord progressions from one held note.
//
// Hold a key (or switch Play to Autoplay) and it walks through chords in that
// key: each chord chosen from the ones that can follow the last (a weighted
// walk, seeded - Seed picks another path), voiced in the middle register
// without its root, moved to the nearest notes from the chord before (notes
// both chords share are held through), over the root in the bass. The newest
// held key sets the key; a new key takes over at the next chord.
//
// Character - what kind of harmony it walks through (a few among many):
//   Mellow 7ths       major-key progressions full of 7th, 9th and 11th
//                     chords, sharp-fifth dominants as three-note shells,
//                     chromatic passing chords and a minor v, so it circles
//                     rather than lands
//   Deep Minor        minor 9th and 11th chords moving by whole steps, major
//                     9ths on the sixth and third, suspended dominants
//   Bright Major      major 7th, 9th, 13th and 6/9 chords, sharp-eleven
//                     colour, sus4 dominants resolving
//   Floating Sus      sus2, maj7sus2 and add9 chords rocking a whole step
//   Lydian Haze       the major chord on the second step over the tonic,
//                     stacked fourths, never quite resolving
//   Turnaround        rootless ii-V-I-VI cycles with altered dominants and
//                     tritone substitutes
//   Cinematic Minor   plain minor-key triads
//   Quartal & Penta   chords from the major pentatonic only, voiced in
//                     stacked fourths and open clusters - no half steps
//
// Change: how long a chord lasts (passing chords take one beat of the chord
// before them; Passing: how often they come). Meter: 4/4, 3/4 or 5/4.
// Voicing: Close, Open (drop 2), Wide (drop 2 and 4). Register: where the
// chords sit (C2 .. E6). Bass: Off, Root, Root + Octave. Extra Bass (6.9.3):
// the root again one to three octaves further down. Transpose (6.9.3): all
// it plays, up or down two octaves. Roll: the notes bloom upward
// one after another. Velocity, Humanize. Hold Common: shared notes ring on.
// Restrike (6.7.11): the chance a chord is struck again, once to three times,
// on the Restrike Rate grid within it. The card rolls the chords past a line
// where they play now - the next one already coming in from the right.
// Rec on the card records what it plays into a .mid file.
// Inputs: Midi In (the played keys when empty). Output: Midi Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/MpeFollow.h"
#include "../../Shared/MidiTake.h"

using namespace aquanode;

namespace bloom
{
    struct Next { int to; float w; };
    struct Chord
    {
        juce::String roman, quality;
        int root { 0 }, bass { 0 };          // semitones above the key's tonic
        std::vector<int> iv;                 // the upper notes, semitones above the chord's root
        bool stack { false };                // voiced exactly as written (stacked fourths), not rearranged
        bool passing { false };              // a one-beat passing chord
        bool tense { false };                // altered / sharp-fifth colours
        std::vector<Next> next;
    };
    struct Character
    {
        juce::String name, description;
        bool minor { false };
        std::vector<Chord> chords;
    };

    inline Chord C (const char* roman, const char* quality, int root, std::initializer_list<int> iv,
                    std::initializer_list<Next> next, int bass = -1, bool tense = false, bool passing = false, bool stack = false)
    {
        Chord c;
        c.roman = roman; c.quality = quality; c.root = root; c.bass = bass < 0 ? root : bass;
        c.iv = iv; c.next = next; c.tense = tense; c.passing = passing; c.stack = stack;
        return c;
    }

    inline const std::vector<Character>& characters()
    {
        static const std::vector<Character> all = []
        {
            std::vector<Character> v;
            {   // Mellow 7ths (major)
                Character ch; ch.name = "Mellow 7ths";
                ch.description = "Major-key progressions full of 7th, 9th and 11th chords, sharp-fifth dominants as three-note shells, "
                                 "chromatic passing chords and a minor v instead of V, so the music circles rather than lands.";
                ch.chords = {
                    C ("Imaj9", "maj9", 0, { 4, 7, 11, 14 }, { { 1, 0.4f }, { 11, 0.2f }, { 6, 0.2f }, { 14, 0.2f } }),       // 0
                    C ("IVmaj9", "maj9", 5, { 4, 7, 11, 14 }, { { 2, 0.7f }, { 12, 0.15f }, { 4, 0.15f } }),                  // 1
                    C ("III7#5", "7#5", 4, { 4, 8, 10 }, { { 3, 0.85f }, { 6, 0.15f } }, -1, true),                            // 2
                    C ("vi9", "m9", 9, { 3, 7, 10, 14 }, { { 9, 0.3f }, { 10, 0.3f }, { 4, 0.4f } }),                          // 3
                    C ("v9", "m9", 7, { 3, 7, 10, 14 }, { { 5, 0.6f }, { 0, 0.2f }, { 1, 0.2f } }),                            // 4
                    C ("I9", "9", 0, { 4, 10, 14 }, { { 1, 0.7f }, { 13, 0.3f } }),                                            // 5
                    C ("ii9", "m9", 2, { 3, 7, 10, 14 }, { { 2, 0.4f }, { 16, 0.3f }, { 0, 0.15f }, { 15, 0.15f } }),         // 6
                    C ("iii9", "m9", 4, { 3, 7, 10, 14 }, { { 6, 0.6f }, { 3, 0.4f } }),                                       // 7
                    C ("VI9sus4", "9sus4", 9, { 5, 7, 10, 14 }, { { 4, 0.5f }, { 6, 0.5f } }),                                 // 8
                    C ("bvi7", "m7", 8, { 3, 7, 10 }, { { 4, 1.0f } }, -1, false, true),                                       // 9  (vi to v, one beat)
                    C ("bVI7", "7", 8, { 4, 7, 10 }, { { 4, 1.0f } }, -1, true, true),                                         // 10 (vi to v, one beat)
                    C ("bVII9", "9", 10, { 4, 7, 10, 14 }, { { 3, 0.7f }, { 0, 0.3f } }),                                      // 11
                    C ("iv6", "m6", 5, { 3, 7, 9 }, { { 11, 0.5f }, { 0, 0.3f }, { 2, 0.2f } }),                               // 12
                    C ("IVmaj13#11", "maj13#11", 5, { 4, 11, 14, 18, 21 }, { { 2, 1.0f } }),                                   // 13
                    C ("Imaj7sus2", "maj7sus2", 0, { 2, 7, 11, 14 }, { { 13, 0.6f }, { 1, 0.4f } }),                           // 14
                    C ("ii11", "m11", 2, { 0, 5, 10, 15, 19 }, { { 1, 0.5f }, { 0, 0.5f } }, -1, false, false, true),          // 15 (fourths, a third on top)
                    C ("III7alt", "7alt", 4, { 4, 10, 13, 20 }, { { 3, 1.0f } }, -1, true) };                                  // 16
                v.push_back (ch);
            }
            {   // Deep Minor
                Character ch; ch.name = "Deep Minor"; ch.minor = true;
                ch.description = "Minor 9th and 11th chords moving by whole steps, major 9ths on the sixth and third, suspended and "
                                 "flat-nine dominants - deep and rolling.";
                ch.chords = {
                    C ("im9", "m9", 0, { 3, 7, 10, 14 }, { { 2, 0.3f }, { 6, 0.2f }, { 5, 0.2f }, { 4, 0.15f }, { 1, 0.15f } }),  // 0
                    C ("im11", "m11", 0, { 3, 10, 14, 17 }, { { 2, 0.4f }, { 5, 0.3f }, { 3, 0.3f } }),                          // 1
                    C ("bVImaj9", "maj9", 8, { 4, 7, 11, 14 }, { { 4, 0.35f }, { 7, 0.25f }, { 3, 0.2f }, { 5, 0.2f } }),         // 2
                    C ("bIIImaj9", "maj9", 3, { 4, 7, 11, 14 }, { { 2, 0.4f }, { 5, 0.3f }, { 9, 0.3f } }),                       // 3
                    C ("bVII6/9", "6/9", 10, { 4, 7, 9, 14 }, { { 0, 0.5f }, { 3, 0.3f }, { 1, 0.2f } }),                         // 4
                    C ("iv9", "m9", 5, { 3, 7, 10, 14 }, { { 8, 0.3f }, { 9, 0.3f }, { 7, 0.2f }, { 0, 0.2f } }),                 // 5
                    C ("iim9", "m9", 2, { 3, 7, 10, 14 }, { { 0, 0.7f }, { 3, 0.3f } }),                                         // 6
                    C ("V7b9", "7b9", 7, { 4, 10, 13, 20 }, { { 0, 0.7f }, { 2, 0.3f } }, -1, true),                              // 7
                    C ("vm9", "m9", 7, { 3, 7, 10, 14 }, { { 2, 0.5f }, { 0, 0.5f } }),                                          // 8
                    C ("bVII9sus4", "9sus4", 10, { 5, 7, 10, 14 }, { { 0, 0.4f }, { 3, 0.3f }, { 2, 0.3f } }) };                // 9
                v.push_back (ch);
            }
            {   // Bright Major
                Character ch; ch.name = "Bright Major";
                ch.description = "Major 7th, 9th, 13th and 6/9 chords with sharp-eleven colour, sus4 dominants that resolve, "
                                 "first-inversion tonics - open and sunny.";
                ch.chords = {
                    C ("Imaj9", "maj9", 0, { 4, 7, 11, 14 }, { { 2, 0.35f }, { 3, 0.25f }, { 7, 0.15f }, { 1, 0.1f }, { 8, 0.15f } }),  // 0
                    C ("I6/9", "6/9", 0, { 4, 7, 9, 14 }, { { 2, 0.5f }, { 3, 0.5f } }),                                                // 1
                    C ("IVmaj9#11", "maj9#11", 5, { 4, 11, 14, 18 }, { { 4, 0.35f }, { 0, 0.25f }, { 6, 0.2f }, { 7, 0.2f } }),          // 2
                    C ("vi9", "m9", 9, { 3, 7, 10, 14 }, { { 2, 0.4f }, { 7, 0.3f }, { 4, 0.3f } }),                                    // 3
                    C ("V13sus4", "13sus4", 7, { 5, 10, 14, 21 }, { { 5, 0.5f }, { 0, 0.3f }, { 1, 0.2f } }),                           // 4
                    C ("V13", "13", 7, { 4, 10, 14, 21 }, { { 0, 0.6f }, { 3, 0.4f } }),                                                // 5
                    C ("iii7", "m7", 4, { 3, 7, 10 }, { { 3, 0.5f }, { 8, 0.5f } }),                                                    // 6
                    C ("ii9", "m9", 2, { 3, 7, 10, 14 }, { { 4, 0.6f }, { 9, 0.4f } }),                                                 // 7
                    C ("IVmaj7", "maj7", 5, { 4, 7, 11 }, { { 4, 0.4f }, { 9, 0.3f }, { 6, 0.3f } }),                                   // 8
                    C ("Iadd9/3", "add9", 0, { 4, 7, 12, 14 }, { { 2, 0.6f }, { 7, 0.4f } }, 4) };                                     // 9
                v.push_back (ch);
            }
            {   // Floating Sus
                Character ch; ch.name = "Floating Sus";
                ch.description = "Sus2, maj7sus2 and add9 chords rocking a whole step back and forth, suspended dominants that "
                                 "never push - wide, airy and slow.";
                ch.chords = {
                    C ("Imaj7sus2", "maj7sus2", 0, { 2, 7, 11 }, { { 1, 0.4f }, { 2, 0.3f }, { 4, 0.15f }, { 6, 0.15f } }),   // 0
                    C ("bVIIadd9", "add9", 10, { 4, 7, 14 }, { { 0, 0.5f }, { 2, 0.3f }, { 5, 0.2f } }),                      // 1
                    C ("IVsus2", "sus2", 5, { 2, 7, 12 }, { { 5, 0.3f }, { 3, 0.3f }, { 0, 0.2f }, { 7, 0.2f } }),             // 2
                    C ("V9sus4", "9sus4", 7, { 5, 10, 14 }, { { 0, 0.4f }, { 4, 0.3f }, { 6, 0.3f } }),                       // 3
                    C ("vi(add9)", "m(add9)", 9, { 3, 7, 14 }, { { 2, 0.5f }, { 1, 0.5f } }),                                 // 4
                    C ("Iadd9", "add9", 0, { 4, 7, 14 }, { { 1, 0.4f }, { 6, 0.4f }, { 7, 0.2f } }),                          // 5
                    C ("IVmaj7sus2", "maj7sus2", 5, { 2, 7, 11 }, { { 5, 0.5f }, { 3, 0.5f } }),                              // 6
                    C ("ii11", "m11", 2, { 3, 10, 17 }, { { 3, 0.5f }, { 0, 0.5f } }) };                                      // 7
                v.push_back (ch);
            }
            {   // Lydian Haze
                Character ch; ch.name = "Lydian Haze";
                ch.description = "The major chord on the second step over the tonic, sharp-eleven major chords, stacked fourths - "
                                 "bright, dreamy and never quite resolving.";
                ch.chords = {
                    C ("Imaj9#11", "maj9#11", 0, { 4, 6, 11, 14 }, { { 1, 0.5f }, { 4, 0.2f }, { 3, 0.15f }, { 5, 0.15f } }),   // 0
                    C ("II/I", "add9", 2, { 0, 4, 7, 14 }, { { 0, 0.5f }, { 2, 0.2f }, { 5, 0.3f } }, 0),                         // 1
                    C ("vii7", "m7", 11, { 3, 7, 10 }, { { 3, 0.5f }, { 1, 0.5f } }),                                             // 2
                    C ("iii9", "m9", 4, { 3, 7, 10, 14 }, { { 6, 0.4f }, { 1, 0.3f }, { 0, 0.3f } }),                             // 3
                    C ("V/I", "add9", 7, { 0, 4, 7, 14 }, { { 1, 0.5f }, { 0, 0.5f } }, 0),                                       // 4
                    C ("iii (4ths)", " quartal", 4, { 5, 10, 15, 20 }, { { 0, 0.5f }, { 1, 0.5f } }, -1, false, false, true),     // 5
                    C ("vi9", "m9", 9, { 3, 7, 10, 14 }, { { 1, 0.5f }, { 4, 0.5f } }) };                                         // 6
                v.push_back (ch);
            }
            {   // Turnaround
                Character ch; ch.name = "Turnaround";
                ch.description = "Rootless ii-V-I-VI cycles with thirteenths, altered dominants and tritone substitutes - "
                                 "the classic jazz turnaround, endlessly.";
                ch.chords = {
                    C ("Imaj9", "maj9", 0, { 4, 7, 11, 14 }, { { 1, 0.5f }, { 5, 0.2f }, { 9, 0.15f }, { 8, 0.15f } }),   // 0
                    C ("VI7alt", "7alt", 9, { 4, 10, 13, 20 }, { { 2, 1.0f } }, -1, true),                                  // 1
                    C ("ii9", "m9", 2, { 3, 7, 10, 14 }, { { 3, 0.5f }, { 4, 0.3f }, { 7, 0.2f } }),                        // 2
                    C ("V13", "13", 7, { 4, 10, 14, 21 }, { { 0, 0.8f }, { 5, 0.2f } }),                                     // 3
                    C ("bII9", "9", 1, { 4, 10, 14 }, { { 0, 1.0f } }, -1, true),                                            // 4
                    C ("iii9", "m9", 4, { 3, 7, 10, 14 }, { { 6, 0.7f }, { 1, 0.3f } }),                                     // 5
                    C ("VI7b9", "7b9", 9, { 4, 10, 13 }, { { 2, 1.0f } }, -1, true),                                         // 6
                    C ("V7alt", "7alt", 7, { 4, 10, 13, 15, 20 }, { { 0, 1.0f } }, -1, true),                                // 7
                    C ("#iv7b5", "m7b5", 6, { 3, 6, 10 }, { { 5, 0.5f }, { 9, 0.5f } }),                                     // 8
                    C ("IVmaj9", "maj9", 5, { 4, 7, 11, 14 }, { { 8, 0.3f }, { 5, 0.4f }, { 2, 0.3f } }) };                 // 9
                v.push_back (ch);
            }
            {   // Cinematic Minor
                Character ch; ch.name = "Cinematic Minor"; ch.minor = true;
                ch.description = "Plain minor-key triads - i, flat VI, flat III, flat VII, iv - with a sus4 dominant now and then "
                                 "resolving to the major V: big and simple.";
                ch.chords = {
                    C ("i", "m", 0, { 0, 3, 7 }, { { 1, 0.35f }, { 4, 0.2f }, { 3, 0.2f }, { 2, 0.15f }, { 6, 0.1f } }),   // 0
                    C ("bVI", "", 8, { 0, 4, 7 }, { { 2, 0.3f }, { 3, 0.35f }, { 4, 0.15f }, { 7, 0.2f } }),                // 1
                    C ("bIII", "", 3, { 0, 4, 7 }, { { 3, 0.4f }, { 1, 0.3f }, { 4, 0.3f } }),                              // 2
                    C ("bVII", "", 10, { 0, 4, 7 }, { { 0, 0.4f }, { 2, 0.3f }, { 1, 0.3f } }),                             // 3
                    C ("iv", "m", 5, { 0, 3, 7 }, { { 0, 0.3f }, { 3, 0.3f }, { 7, 0.2f }, { 6, 0.2f } }),                  // 4
                    C ("V", "", 7, { 0, 4, 7 }, { { 0, 0.8f }, { 1, 0.2f } }),                                              // 5
                    C ("v", "m", 7, { 0, 3, 7 }, { { 1, 0.5f }, { 0, 0.5f } }),                                             // 6
                    C ("Vsus4", "sus4", 7, { 0, 5, 7 }, { { 5, 0.8f }, { 0, 0.2f } }) };                                    // 7
                v.push_back (ch);
            }
            {   // Quartal & Penta
                Character ch; ch.name = "Quartal & Penta";
                ch.description = "Chords made only of the major pentatonic, voiced in stacked fourths and open clusters - no half "
                                 "steps anywhere, floating and modal.";
                ch.chords = {
                    C ("I6/9 (4ths)", "6/9", 0, { 4, 9, 14, 19 }, { { 1, 0.3f }, { 2, 0.25f }, { 3, 0.2f }, { 6, 0.25f } }, -1, false, false, true),  // 0  E A D G
                    C ("vi11 (4ths)", "m11", 9, { 5, 10, 15, 19 }, { { 0, 0.3f }, { 2, 0.35f }, { 4, 0.35f } }, -1, false, false, true),             // 1  D G C E
                    C ("ii9sus4", "9sus4", 2, { 5, 10, 14, 19 }, { { 3, 0.4f }, { 0, 0.3f }, { 7, 0.3f } }, -1, false, false, true),                // 2  G C E A
                    C ("Vsus4(add9) (4ths)", "sus4(add9)", 7, { 2, 7, 12, 17 }, { { 0, 0.4f }, { 1, 0.3f }, { 6, 0.3f } }, -1, false, false, true),  // 3  A D G C
                    C ("iii (4ths)", " quartal", 4, { 5, 10, 15 }, { { 1, 0.5f }, { 2, 0.5f } }, -1, false, false, true),                           // 4  A D G
                    C ("I6/9 (5ths)", "6/9", 0, { 7, 14, 21, 28 }, { { 1, 0.4f }, { 3, 0.3f }, { 2, 0.3f } }, -1, false, false, true),              // 5  G D A E
                    C ("Iadd9", "add9", 0, { 2, 4, 7 }, { { 7, 0.4f }, { 3, 0.3f }, { 5, 0.3f } }),                                                 // 6
                    C ("vi7(add11)", "m7(add11)", 9, { 3, 5, 7, 10 }, { { 0, 0.4f }, { 4, 0.3f }, { 2, 0.3f } }) };                                // 7
                v.push_back (ch);
            }
            {   // Bossa Nova
                Character ch; ch.name = "Bossa Nova";
                ch.description = "Major 7ths and 6/9 tonics, a major II that turns minor, tritone-substitute dominants sliding down "
                                 "by a half step, diminished passing chords, the minor iv6 and flat-VII9 back door - soft and swaying.";
                ch.chords = {
                    C ("Imaj9", "maj9", 0, { 4, 7, 11, 14 }, { { 1, 0.25f }, { 2, 0.25f }, { 3, 0.2f }, { 9, 0.15f }, { 11, 0.15f } }),  // 0
                    C ("II9", "9", 2, { 4, 10, 14 }, { { 2, 0.7f }, { 5, 0.3f } }),                                                      // 1  (major II turning minor)
                    C ("ii9", "m9", 2, { 3, 7, 10, 14 }, { { 4, 0.4f }, { 5, 0.35f }, { 6, 0.25f } }),                                  // 2
                    C ("vi9", "m9", 9, { 3, 7, 10, 14 }, { { 2, 0.5f }, { 1, 0.3f }, { 8, 0.2f } }),                                    // 3
                    C ("bII7#11", "7#11", 1, { 4, 10, 14, 18 }, { { 0, 0.75f }, { 13, 0.25f } }),                                       // 4  (tritone substitute)
                    C ("V13", "13", 7, { 4, 10, 14, 21 }, { { 0, 0.6f }, { 3, 0.4f } }),                                                // 5
                    C ("V7b9", "7b9", 7, { 4, 10, 13, 20 }, { { 0, 0.5f }, { 3, 0.3f }, { 13, 0.2f } }, -1, true),                      // 6
                    C ("iv6", "m6", 5, { 3, 7, 9 }, { { 0, 0.35f }, { 15, 0.35f }, { 9, 0.3f } }),                                      // 7
                    C ("VI7b9", "7b9", 9, { 4, 10, 13 }, { { 2, 1.0f } }, -1, true, true),                                              // 8  (to ii, one beat)
                    C ("iii7", "m7", 4, { 3, 7, 10 }, { { 8, 0.4f }, { 10, 0.3f }, { 3, 0.3f } }),                                      // 9
                    C ("biiidim7", "dim7", 3, { 3, 6, 9 }, { { 2, 1.0f } }, -1, false, true),                                           // 10 (iii to ii, one beat)
                    C ("I9", "9", 0, { 4, 10, 14 }, { { 12, 1.0f } }),                                                                  // 11 (to IV)
                    C ("IVmaj9", "maj9", 5, { 4, 7, 11, 14 }, { { 7, 0.4f }, { 14, 0.3f }, { 9, 0.3f } }),                              // 12
                    C ("I6/9", "6/9", 0, { 4, 7, 9, 14 }, { { 1, 0.4f }, { 2, 0.3f }, { 9, 0.3f } }),                                   // 13
                    C ("#ivm7b5", "m7b5", 6, { 3, 6, 10 }, { { 7, 0.5f }, { 9, 0.5f } }),                                               // 14
                    C ("bVII9", "9", 10, { 4, 10, 14 }, { { 0, 1.0f } }) };                                                             // 15 (back door)
                v.push_back (ch);
            }
            return v;
        }();
        return all;
    }

    // note names in a key: flats in the flat keys, else sharps
    inline juce::String pcName (int pc, int key, bool minor)
    {
        static const char* sharps[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* flats[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        const int major = minor ? (key + 3) % 12 : key;   // (a minor key spells as its relative major)
        const bool useFlats = major == 5 || major == 10 || major == 3 || major == 8 || major == 1 || major == 6;
        return (useFlats ? flats : sharps)[((pc % 12) + 12) % 12];
    }

    // b / # as the music signs: a b after a note letter (Eb), or before a digit
    // or a Roman numeral where no word runs into it (bVII, 7b9)
    inline juce::String pretty (const juce::String& s)
    {
        const juce::String flat (juce::CharPointer_UTF8 ("\xe2\x99\xad")), sharp (juce::CharPointer_UTF8 ("\xe2\x99\xaf"));
        juce::String out;
        for (int i = 0; i < s.length(); ++i)
        {
            const auto c = s[i];
            const auto prev = i > 0 ? s[i - 1] : (juce::juce_wchar) 0;
            const auto nx = i + 1 < s.length() ? s[i + 1] : (juce::juce_wchar) 0;
            const bool noteLetter = prev >= 'A' && prev <= 'G';
            const bool beforeNumber = (nx >= '0' && nx <= '9') || nx == 'I' || nx == 'V' || nx == 'i' || nx == 'v';
            const bool wordBefore = prev >= 'a' && prev <= 'z';
            if (c == 'b' && (noteLetter || (beforeNumber && ! wordBefore))) out << flat;
            else if (c == '#') out << sharp;
            else out << juce::String::charToString (c);
        }
        return out;
    }

    inline juce::String chordName (const Character& ch, const Chord& c, int key)
    {
        auto s = pcName (key + c.root, key, ch.minor) + c.quality;
        if (c.bass != c.root) s << "/" << pcName (key + c.bass, key, ch.minor);
        return pretty (s);
    }

    // (6.9.20) the chords Resolve's Home and Mixed modes play beside a character's own: the ones
    // that lead home (plain 7ths, the jazz ones with a 9th or 13th) and the home chords - triad,
    // 6 and 6/9 (the 6/9 as stacked fourths, 3rd - 6th - 9th, as pianists play it), no 7th on
    // the home chord. 0..9 for the major characters, 10..19 for the minor ones. On the card they
    // are shown as 1000 + their number.
    enum Special { sIV = 0, sV7, sii7, sV7ofV, sii9, sV13, sbII9, sI, sI6, sI69,
                   siv = 10, smV7, sii7b5, smV7ofV, smV7b9 = 15, smbII9, si, si6, si69, kNumSpecials };
    inline const std::vector<Chord>& specials()
    {
        static const std::vector<Chord> all = []
        {
            std::vector<Chord> v ((size_t) kNumSpecials);
            auto set = [&v] (int i, const char* roman, const char* quality, int root, std::initializer_list<int> iv, bool stack = false)
            { v[(size_t) i] = C (roman, quality, root, iv, {}, -1, false, false, stack); };
            set (sIV,    "IV",    "",     5, { 0, 4, 7 });
            set (sV7,    "V7",    "7",    7, { 0, 4, 7, 10 });
            set (sii7,   "ii7",   "m7",   2, { 0, 3, 7, 10 });
            set (sV7ofV, "V7/V",  "7",    2, { 0, 4, 7, 10 });
            set (sii9,   "ii9",   "m9",   2, { 3, 7, 10, 14 });
            set (sV13,   "V13",   "13",   7, { 4, 10, 21 });
            set (sbII9,  "bII9",  "9",    1, { 4, 10, 14 });
            set (sI,     "I",     "",     0, { 0, 4, 7 });
            set (sI6,    "I6",    "6",    0, { 0, 4, 7, 9 });
            set (sI69,   "I6/9",  "6/9",  0, { 4, 9, 14 }, true);
            set (siv,    "iv",    "m",    5, { 0, 3, 7 });
            set (smV7,   "V7",    "7",    7, { 0, 4, 7, 10 });
            set (sii7b5, "ii7b5", "m7b5", 2, { 0, 3, 6, 10 });
            set (smV7ofV,"II7",   "7",    2, { 0, 4, 7, 10 });
            set (14,     "V7",    "7",    7, { 0, 4, 7, 10 });   // (unused slot, kept for the numbering)
            set (smV7b9, "V7b9",  "7b9",  7, { 4, 10, 13 });
            set (smbII9, "bII9",  "9",    1, { 4, 10, 14 });
            set (si,     "i",     "m",    0, { 0, 3, 7 });
            set (si6,    "i6",    "m6",   0, { 0, 3, 7, 9 });
            set (si69,   "i6/9",  "m6/9", 0, { 3, 9, 14 }, true);
            return v;
        }();
        return all;
    }
    inline bool isHomeSpecial (int s) { return (s >= sI && s <= sI69) || (s >= si && s <= si69); }

    // a chord the card shows: the character's own, or 1000 + a special one
    inline const Chord* shownChordOf (const Character& ch, int idx)
    {
        if (idx >= 1000 && idx - 1000 < (int) specials().size()) return &specials()[(size_t) (idx - 1000)];
        if (idx >= 0 && idx < (int) ch.chords.size()) return &ch.chords[(size_t) idx];
        return nullptr;
    }
}

class MidiBloomModule final : public SynthModule
{
public:
    enum P { pCharacter = 0, pPlay, pKey, pChange, pMeter, pPassing, pTension, pVoicing, pRegister, pBass, pRoll,
             pVelocity, pHumanize, pHoldCommon, pSeed, pRestrike, pRestrikeRate, pExtraBass, pTranspose, pResolve,
             pResolveEvery, pHomeChord };
    enum ResolveMode { rOff = 0, rOn, rHome, rMixed };

    MidiBloomModule() { (void) bloom::characters(); (void) bloom::specials(); }   // (the tables made here, not on the audio thread)

    bool isMidiNoteDriver() const override { return true; }
    bool isMidiProcessor() const override { return true; }
    bool midiSourceReplacesInput() const override { return true; }   // the chords take the held key's place
    bool runsWithoutListeners() const override { return false; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override
    {
        numHeld = 0; running = false; numQueued = 0; numSounding = 0;
        for (auto& s : sounding) s = false;
        prevUpper.clear(); prevBass = -1;
        shownChord.store (-1); shownNext.store (-1);
        curSpecial = nextSpecial = -1; cadLen = -1; lastCharIdx = 0;
    }

    void blockStart() override { clock.blockStart (tempoBpm, sampleRate, tempoFromLane() ? nullptr : hostPlayHead); }

    //=== the played keys (audio thread): they set the key, they do not sound =====
    bool acceptsMpeEvents() const override { return true; }   // (6.9.11: its notes follow the leading finger)
    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent*, int) override
    {
        mpeFollow.heard (in);
        if (in.isExpression) return 0;
        if (in.isOn) addHeld (in.note); else removeHeld (in.note);
        return 0;
    }
    void midiDriverHeldNoteOn (int note) override { addHeld (note); }
    void midiDriverHeldNoteOff (int note) override { removeHeld (note); }
    void midiDriverAllHeldOff() override { numHeld = 0; }
    void midiAllNotesOff() override { numHeld = 0; }

    //=== the chords (audio thread, every sample) ===============================
    int midiDriverAdvanceEvents (double, MidiDriverEvent* events, int maxEvents) override
    {
        clock.tick();
        const double beat = clock.beat;
        beatNow.store (beat, std::memory_order_relaxed);
        int n = 0;
        const bool autoplay = param (pPlay) > 0.5f;
        const bool want = numHeld > 0 || autoplay;
        const int key = numHeld > 0 ? held[numHeld - 1] % 12 : juce::jlimit (0, 11, (int) std::lround (param (pKey)));
        const int charIdx = juce::jlimit (0, (int) bloom::characters().size() - 1, (int) std::lround (param (pCharacter)));

        if (! want && running)
        {
            stopAll (beat);
        }
        else if (want && ! running)
        {
            running = true;
            character = charIdx;
            currentKey = key;
            rng = (juce::uint32) (param (pSeed) * 7919.0f) * 2654435761u + 12345u;
            if (rng == 0) rng = 1;
            chordIdx = -1;
            nextIdx = startChord();
            phrasePos = 0;
            changeAt = beat;
            prevUpper.clear(); prevBass = -1;
            curSpecial = nextSpecial = -1; cadLen = -1; lastCharIdx = 0;
        }
        if (running && beat >= changeAt)
        {
            // the next chord: in the key held now, of the character chosen now
            if (charIdx != character) { character = charIdx; nextIdx = startChord(); phrasePos = 0; nextSpecial = -1; cadLen = -1; }
            currentKey = key;
            chordIdx = nextIdx;
            // Resolve: phrases of four chords (or three: Resolve Every), the last the home chord, low and
            // mellow; the one before leaning toward it (chosen among the ones that can go home, still at
            // random). Home (6.9.20): the same, landing on a triad, 6 or 6/9 with a deeper bass. Mixed: the
            // chord or two before home a cadence (IV, V7, ii-V, IV-V, V7/V-V, ii9-V13 or bII9), a new one
            // each phrase.
            resolvingNow = false;
            const int mode = juce::jlimit (0, 3, (int) std::lround (param (pResolve)));
            const int every = param (pResolveEvery) > 0.5f ? 4 : 3;
            curSpecial = mode >= rHome ? nextSpecial : -1;
            nextSpecial = -1;
            if (mode == rOn || mode == rHome)
            {
                if (! ch().chords[(size_t) chordIdx].passing) ++phrasePos;
                if (phrasePos >= every) { resolvingNow = chordIdx == startChord(); phrasePos = 0; }
                if (phrasePos == every - 1) nextIdx = startChord();
                else if (phrasePos == every - 2) nextIdx = pickTowardHome (chordIdx);
                else nextIdx = pickNext (chordIdx);
                if (mode == rHome)
                {
                    if (! resolvingNow) curSpecial = -1;
                    const int home = (ch().minor ? bloom::si : bloom::sI) + juce::jlimit (0, 2, (int) std::lround (param (pHomeChord)));
                    if (resolvingNow) curSpecial = home;
                    if (phrasePos == every - 1) nextSpecial = home;   // (so the card shows it coming)
                }
            }
            else if (mode == rMixed)
                mixedStep (every);
            else
                nextIdx = pickNext (chordIdx);
            playChord (beat);
        }
        // what is due goes out
        for (int i = 0; i < numQueued && n < maxEvents; )
        {
            if (queue[i].beat <= beat)
            {
                const auto q = queue[i];
                queue[i] = queue[--numQueued];
                if (q.on)
                {
                    if (! running || sounding[q.note]) continue;
                    sounding[q.note] = true;
                    events[n++] = { q.note, q.velocity, true };
                    take.add (beat, q.note, q.velocity, true);
                    logOn (q.note, q.velocity, beat);
                }
                else if (sounding[q.note])
                {
                    sounding[q.note] = false;
                    events[n++] = { q.note, 0.0f, false };
                    take.add (beat, q.note, 0.0f, false);
                    logOff (q.note, beat);
                }
            }
            else ++i;
        }
        return mpeFollow.decorate (events, n, maxEvents);
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.midibloom";
        d.displayName = "MIDI Bloom";
        d.searchKeywords = "chords progression generative harmony jazz lush pads chord generator quartal pentatonic";
        d.description =
            "Lush chord progressions from one held note: the newest held key sets the key (or Autoplay plays in Key), and "
            "it walks through chords that can follow one another, voiced in the middle without their roots, moved to the "
            "nearest notes, over the root in the bass. Character picks the harmony - Mellow 7ths, Deep Minor, Bright "
            "Major, Floating Sus, Lydian Haze, Turnaround, Cinematic Minor, Quartal & Penta (the card describes each). "
            "Change: how long a chord lasts; Passing: how often one-beat passing chords come; Tension: how often altered "
            "and sharp-fifth chords are chosen. Voicing, Register, Bass, Extra Bass (the root again one to three octaves "
            "further down), Roll (the notes bloom upward), Velocity, Humanize, Hold Common (shared notes ring on), Seed, "
            "Transpose (all it plays, up to two octaves either way), Resolve (On: phrases of four chords - the fourth comes home, low and "
            "mellow, the third leaning toward it; Home: the same, landing on a triad, 6 or 6/9 (Home Chord) over a deeper bass; "
            "Mixed: the chords before home a cadence - IV, V7, ii-V, IV-V, V7/V-V or a jazz ii9-V13 - a new one each phrase; "
            "Resolve Every: home on every 3rd or 4th chord). Rec on the card saves what it plays as a .mid file. With MPE, the notes it plays take the bend, pressure and slide of the finger that leads (the latest pressed) and follow them while they sound; what it records and saves is plain MIDI.";
        d.section = ModuleSection::Utility;
        d.sockets = { midiIn ("midiIn", "MIDI In"), midiOut ("midiOut", "MIDI Out"), tempoIn() };
        juce::StringArray chars;
        for (const auto& c : bloom::characters()) chars.add (c.name);
        auto reg = makeRotary ("register", "Register", 36.0f, 88.0f, 60.0f, 2, {}, false, 1.0f);
        reg.textFromValue = [] (double v) { return juce::MidiMessage::getMidiNoteName (juce::jlimit (0, 127, (int) std::lround (v)), true, true, 4); };
        d.params = {
            makeCombo ("character", "Character", chars, 0, 0, 3).noMod(),
            makeCombo ("play", "Play", { "Hold", "Autoplay" }, 0, 0, 2).noMod(),
            makeSteppedList ("key", "Key", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 3, 0),
            makeSteppedList ("change", "Change", { "1/2 bar", "1 bar", "2 bars", "4 bars" }, 1, 1),
            makeCombo ("meter", "Meter", { "4/4", "3/4", "5/4" }, 0, 1, 2).noMod(),
            makeRotary ("passing", "Passing", 0.0f, 100.0f, 50.0f, 1, "%", false, 1.0f),
            makeRotary ("tension", "Tension", 0.0f, 100.0f, 50.0f, 1, "%", false, 1.0f),
            makeCombo ("voicing", "Voicing", { "Close", "Open", "Wide" }, 1, 2, 2).noMod(),
            reg,
            makeSteppedList ("bass", "Bass", { "Off", "Root", "Root+8va" }, 1, 2).noMod(),
            makeRotary ("roll", "Roll", 0.0f, 400.0f, 45.0f, 2, "ms", false, 1.0f),
            makeRotary ("velocity", "Velocity", 1.0f, 127.0f, 72.0f, 3, {}, false, 1.0f),
            makeRotary ("humanize", "Humanize", 0.0f, 100.0f, 25.0f, 3, "%", false, 1.0f),
            makeCombo ("holdCommon", "Hold Common", { "Off", "On" }, 1, 3, 2).noMod(),
            makeRotary ("seed", "Seed", 0.0f, 999.0f, 0.0f, 3, {}, false, 1.0f).noMod(),
            makeRotary ("restrike", "Restrike", 0.0f, 100.0f, 0.0f, 4, "%", false, 1.0f),
            makeSteppedList ("restrikeRate", "Rate", { "1/4", "1/8", "1/8T", "1/16" }, 1, 4),
            makeRotary ("extraBass", "Extra Bass", 0.0f, 3.0f, 0.0f, 4, {}, false, 1.0f).noMod(),
            makeRotary ("transpose", "Transpose", -24.0f, 24.0f, 0.0f, 4, "st", false, 1.0f),
            // (6.9.20) its own row, wide enough to read (Home and Mixed added after On)
            makeCombo ("resolve", "Resolve", { "Off", "On", "Home", "Mixed" }, 0, 5, 2).noMod(),
            makeCombo ("resolveEvery", "Resolve Every", { "3rd Chord", "4th Chord" }, 1, 5, 2).noMod().visibleFrom ("resolve", 1.0f),
            makeCombo ("homeChord", "Home Chord", { "Triad", "6", "6/9" }, 2, 6, 2).noMod().visibleWhen ("resolve", 2.0f)
        };
        return d;
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 124; }

    // for the card (message thread): the rolling view's notes - played (start, end;
    // end < 0 while it sounds), the next chord's (voiced ahead) - and the beat now
    struct LoggedNote { int pitch { 0 }; float velocity { 0.0f }; double start { 0.0 }, end { 0.0 }; };
    static constexpr int kLog = 256;
    std::array<LoggedNote, kLog> noteLog {};
    std::atomic<int> logWrite { 0 };
    std::atomic<double> beatNow { 0.0 };
    std::array<int, 16> nextNotes {};
    std::atomic<int> nextCount { 0 };
    std::atomic<double> nextStart { 0.0 }, nextEnd { 0.0 }, changeBeat { 0.0 };
    MidiTake take;
    std::atomic<int> shownChord { -1 }, shownNext { -1 }, shownKey { 0 }, shownCharacter { 0 };
    std::atomic<int> chordsPlayed { 0 };   // (for tests: one more each chord)
    double bpmNow() const { return tempoBpm; }
    int beatsPerBar() const { const int m = (int) std::lround (getParameterBase (pMeter)); return m == 1 ? 3 : (m == 2 ? 5 : 4); }

private:
    aquanode::MpeFollow mpeFollow;
    struct Queued { double beat; int note; float velocity; bool on; };
    static constexpr int kQueue = 256;
    Queued queue[kQueue] {};
    int numQueued { 0 };
    bool sounding[128] {};
    int numSounding { 0 };
    int held[128] {};
    int numHeld { 0 };
    bool running { false };
    int character { 0 }, currentKey { 0 }, chordIdx { -1 }, nextIdx { 0 }, phrasePos { 0 };
    bool resolvingNow { false };
    double changeAt { 0.0 };
    juce::uint32 rng { 1 };
    std::vector<int> prevUpper;
    int prevBass { -1 };
    BeatClock clock;

    const bloom::Character& ch() const { return bloom::characters()[(size_t) character]; }

    void logOn (int pitch, float vel, double beat)
    {
        const int w = logWrite.load (std::memory_order_relaxed);
        noteLog[(size_t) (w % kLog)] = { pitch, vel, beat, -1.0 };
        logWrite.store (w + 1, std::memory_order_release);
    }
    void logOff (int pitch, double beat)
    {
        const int w = logWrite.load (std::memory_order_relaxed);
        for (int i = 1; i <= juce::jmin (64, w); ++i)
        {
            auto& e = noteLog[(size_t) ((w - i) % kLog)];
            if (e.pitch == pitch && e.end < 0.0) { e.end = beat; return; }
        }
    }

    void addHeld (int note)
    {
        for (int i = 0; i < numHeld; ++i) if (held[i] == note) return;
        if (numHeld < 128) held[numHeld++] = note;
    }
    void removeHeld (int note)
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[i] == note) { for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1]; --numHeld; return; }
    }

    float rand01() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng >> 8) / 16777216.0f;
    }

    int startChord() const { return 0; }

    // the chord after this one: a weighted choice of what may follow, Tension
    // weighting the altered ones, Passing deciding whether a passing chord comes
    int pickNext (int from)
    {
        const auto& chords = ch().chords;
        if (from < 0 || from >= (int) chords.size()) return 0;
        const float tension = param (pTension) * 0.01f, passing = param (pPassing) * 0.01f;
        for (int guard = 0; guard < 4; ++guard)
        {
            const auto& nx = chords[(size_t) from].next;
            float total = 0.0f;
            float w[8] {};
            for (size_t i = 0; i < nx.size() && i < 8; ++i)
            {
                const auto& c = chords[(size_t) juce::jlimit (0, (int) chords.size() - 1, nx[i].to)];
                w[i] = nx[i].w * (c.tense ? 2.0f * tension : 1.0f) + 1.0e-4f;
                total += w[i];
            }
            float r = rand01() * total;
            int pick = nx.empty() ? 0 : nx.back().to;
            for (size_t i = 0; i < nx.size() && i < 8; ++i) { r -= w[i]; if (r <= 0.0f) { pick = nx[i].to; break; } }
            pick = juce::jlimit (0, (int) chords.size() - 1, pick);
            if (chords[(size_t) pick].passing && rand01() >= passing) { from = pick; continue; }   // (skipped: what follows it)
            return pick;
        }
        return 0;
    }

    void queueEvent (double beat, int note, float vel, bool on)
    {
        if (numQueued < kQueue && note >= 0 && note <= 127) queue[numQueued++] = { beat, note, vel, on };
    }

    void stopAll (double beat)
    {
        running = false;
        numQueued = 0;
        for (int k = 0; k < 128; ++k) if (sounding[k]) queueEvent (beat, k, 0.0f, false);
        prevUpper.clear(); prevBass = -1;
        shownChord.store (-1); shownNext.store (-1);
    }

    // the upper notes of a chord, nearest to the ones before, in the register
    std::vector<int> voice (const bloom::Chord& c) const
    {
        const int centre = (int) std::lround (param (pRegister));
        const int style = (int) std::lround (param (pVoicing));
        std::vector<std::vector<int>> cands;
        const int root = currentKey + c.root;
        if (c.stack)
        {
            for (int base = centre - 24; base <= centre + 12; base += 12)
            {
                std::vector<int> v;
                const int r = ((root % 12) + 12) % 12 + 12 * (int) std::floor ((double) base / 12.0);
                for (int i : c.iv) v.push_back (r + i);
                cands.push_back (v);
            }
        }
        else
        {
            std::vector<int> pcs;
            for (int i : c.iv) { const int pc = ((root + i) % 12 + 12) % 12; if (std::find (pcs.begin(), pcs.end(), pc) == pcs.end()) pcs.push_back (pc); }
            std::sort (pcs.begin(), pcs.end());
            for (size_t s = 0; s < pcs.size(); ++s)
                for (int low = centre - 10; low <= centre + 4; ++low)
                {
                    if (((low % 12) + 12) % 12 != pcs[s]) continue;
                    std::vector<int> v { low };
                    for (size_t k = 1; k < pcs.size(); ++k)
                    {
                        int p = pcs[(s + k) % pcs.size()];
                        while (p <= v.back()) p += 12;
                        while (p - 12 > v.back()) p -= 12;
                        v.push_back (p);
                    }
                    if (style >= 1 && v.size() >= 4) { auto& x = v[v.size() - 2]; x -= 12; }        // drop 2
                    if (style >= 2 && v.size() >= 5) { auto& x = v[v.size() - 4]; x -= 12; }        // and 4
                    std::sort (v.begin(), v.end());
                    if (v.front() >= 28) cands.push_back (v);
                }
        }
        if (cands.empty()) return {};
        auto score = [&] (const std::vector<int>& v)
        {
            double mean = 0.0; for (int p : v) mean += p; mean /= (double) v.size();
            double s = std::abs (mean - (double) centre) * (prevUpper.empty() ? 1.0 : 0.35);
            if (! prevUpper.empty())
            {
                for (int p : v) { int best = 99; for (int q : prevUpper) best = juce::jmin (best, std::abs (p - q)); s += best; }
                for (int q : prevUpper) { int best = 99; for (int p : v) best = juce::jmin (best, std::abs (p - q)); s += best; }
                if (v.back() == prevUpper.back()) s -= 2.0;   // the top note stays: a common tone on top
            }
            return s;
        };
        size_t best = 0; double bs = 1.0e9;
        for (size_t i = 0; i < cands.size(); ++i) { const double s = score (cands[i]); if (s < bs) { bs = s; best = i; } }
        return cands[best];
    }

    // the next chord, chosen among those that can lead home (the first that can, of a few drawn)
    int pickTowardHome (int from)
    {
        const auto& chords = ch().chords;
        auto leadsHome = [&] (int i)
        {
            if (i == startChord() || chords[(size_t) i].passing) return false;
            for (const auto& nx : chords[(size_t) i].next) if (nx.to == startChord()) return true;
            return false;
        };
        for (int tries = 0; tries < 8; ++tries)
        {
            const int pick = pickNext (from);
            if (leadsHome (pick)) return pick;
        }
        // none drawn: one of the chords that can lead home, at random
        std::vector<int> any;
        for (int i = 0; i < (int) chords.size(); ++i) if (leadsHome (i)) any.push_back (i);
        if (! any.empty()) return any[(size_t) juce::jmin ((int) any.size() - 1, (int) (rand01() * (float) any.size()))];
        int pick = pickNext (from);
        return pick == startChord() && chords.size() > 1 ? 1 : pick;
    }

    // (6.9.20) Mixed: where the phrase is, and what comes next - a character chord, a cadence chord or home
    void mixedStep (int every)
    {
        const bool homeNow = curSpecial >= 0 && bloom::isHomeSpecial (curSpecial);
        const bool passingNow = curSpecial < 0 && ch().chords[(size_t) chordIdx].passing;
        if (! passingNow) ++phrasePos;
        if (homeNow) { resolvingNow = true; phrasePos = 0; cadLen = -1; }
        if (cadLen < 0) pickCadence (every);
        const int nextPos = phrasePos + 1;
        if (nextPos >= every) nextSpecial = cadHome;
        else if (nextPos >= every - cadLen) nextSpecial = cad[nextPos - (every - cadLen)];
        // a character chord follows the last character chord (after home: as from the home chord)
        if (curSpecial < 0) lastCharIdx = chordIdx;
        const int from = homeNow ? startChord() : lastCharIdx;
        nextIdx = nextSpecial < 0 ? pickNext (from) : from;
    }

    // a cadence for this phrase (one chord before home in a phrase of three, one or two in four)
    void pickCadence (int every)
    {
        using namespace bloom;
        const bool mi = ch().minor;
        const float r = rand01();
        cadLen = 1;
        int home = -1;   // (-1: a triad, 6 or 6/9 at random)
        if (every >= 4)
        {
            const int kind = juce::jmin (5, (int) (r * 6.0f));   // plagal, dominant, ii-V, IV-V, secondary, jazz
            switch (kind)
            {
                case 0:  cad[0] = mi ? siv : sIV; break;
                case 1:  cad[0] = mi ? smV7 : sV7; break;
                case 2:  cadLen = 2; cad[0] = mi ? sii7b5 : sii7; cad[1] = mi ? smV7 : sV7; break;
                case 3:  cadLen = 2; cad[0] = mi ? siv : sIV; cad[1] = mi ? smV7 : sV7; break;
                case 4:  cadLen = 2; cad[0] = mi ? smV7ofV : sV7ofV; cad[1] = mi ? smV7 : sV7; break;
                default: cadLen = 2; cad[0] = mi ? sii7b5 : sii9;
                         cad[1] = rand01() < 0.4f ? (mi ? smbII9 : sbII9) : (mi ? smV7b9 : sV13);
                         home = mi ? si69 : sI69; break;
            }
        }
        else
        {
            const int kind = juce::jmin (2, (int) (r * 3.0f));   // plagal, dominant, the tritone substitute
            cad[0] = kind == 0 ? (mi ? siv : sIV) : kind == 1 ? (mi ? smV7 : sV7) : (mi ? smbII9 : sbII9);
            if (kind == 2) home = mi ? si69 : sI69;
        }
        cadHome = home >= 0 ? home : (mi ? si : sI) + juce::jmin (2, (int) (rand01() * 3.0f));
    }

    int curSpecial { -1 }, nextSpecial { -1 }, cadLen { -1 }, cad[2] {}, cadHome { 0 }, lastCharIdx { 0 };

    const bloom::Chord& nextChordRef() const
    {
        if (nextSpecial >= 0) return bloom::specials()[(size_t) nextSpecial];
        const auto& chords = ch().chords;
        return chords[(size_t) juce::jlimit (0, (int) chords.size() - 1, nextIdx)];
    }

    void playChord (double beat)
    {
        const auto& chords = ch().chords;
        const auto& c = curSpecial >= 0 ? bloom::specials()[(size_t) curSpecial] : chords[(size_t) chordIdx];
        const int bpb = (int) std::lround (param (pMeter)) == 1 ? 3 : ((int) std::lround (param (pMeter)) == 2 ? 5 : 4);
        static constexpr double kChange[4] = { 0.5, 1.0, 2.0, 4.0 };
        const double base = kChange[juce::jlimit (0, 3, (int) std::lround (param (pChange)))] * bpb;
        const double passLen = juce::jmin (1.0, base * 0.5);
        double len = c.passing ? passLen : base;
        if (! c.passing && nextIdx >= 0 && nextChordRef().passing) len = juce::jmax (passLen, base - passLen);
        changeAt = beat + len;

        // the notes
        auto upper = voice (c);
        if (resolvingNow && ! upper.empty())
        {
            // the resolution: the home chord an octave down where it can go, softer
            const int lo = *std::min_element (upper.begin(), upper.end());
            if (lo - 12 >= 43) for (auto& u : upper) u -= 12;
        }
        std::vector<int> notes = upper;
        const int bassMode = (int) std::lround (param (pBass));
        if (bassMode > 0)
        {
            const int pc = ((currentKey + c.bass) % 12 + 12) % 12;
            int b = 36 + pc; if (b > 46) b -= 12;           // C2 .. A#2 / B1
            if (prevBass >= 0) { while (b - prevBass > 6) b -= 12; while (prevBass - b > 6) b += 12; }
            b = juce::jlimit (28, 50, b);
            prevBass = b;
            notes.push_back (b);
            if (bassMode == 2) notes.push_back (b + 12);
        }
        // Extra Bass: the root again, one to three octaves further down (below the bass, or below the chord)
        const int extra = juce::jlimit (0, 3, (int) std::lround (param (pExtraBass)));
        if (extra > 0)
        {
            const int pc = ((currentKey + c.bass) % 12 + 12) % 12;
            int r = bassMode > 0 ? prevBass : 36 + pc;
            if (bassMode == 0) { if (r > 46) r -= 12; }
            for (int k = 1; k <= extra; ++k)
                if (r - 12 * k >= 12) notes.push_back (r - 12 * k);
        }
        // (6.9.20) Home and Mixed: the home chord deeper - its root in the bass and the octave under it
        if (curSpecial >= 0 && bloom::isHomeSpecial (curSpecial))
        {
            // the root nearest the bass before (always the root: the Bass row's clamp at E1 can
            // land on another note), and the octave under it
            const int pc = ((currentKey + c.bass) % 12 + 12) % 12;
            int b = 36 + pc; if (b > 46) b -= 12;
            if (prevBass >= 0) { while (b - prevBass > 6 && b - 12 >= 28) b -= 12; while (prevBass - b > 6 && b + 12 <= 50) b += 12; }
            if (bassMode > 0 && prevBass >= 0 && ((prevBass % 12) + 12) % 12 != pc)
                notes.erase (std::remove (notes.begin(), notes.end(), prevBass), notes.end());
            notes.push_back (b);
            if (b - 12 >= 24) notes.push_back (b - 12);
            if (bassMode > 0) prevBass = b;
            std::sort (notes.begin(), notes.end());
            notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
        }
        prevUpper = upper;
        // Transpose: everything it plays, moved
        const int tr = (int) std::lround (param (pTranspose));
        const int bassOut = prevBass >= 0 ? prevBass + tr : -1;
        if (tr != 0)
        {
            std::vector<int> moved;
            for (int p : notes) if (p + tr >= 0 && p + tr <= 127) moved.push_back (p + tr);
            notes = moved;
        }


        const bool holdCommon = param (pHoldCommon) > 0.5f;
        bool want[128] {};
        for (int p : notes) if (p >= 0 && p <= 127) want[p] = true;
        // the ones that go: off now (shared ones ring on, unless Hold Common is off)
        numQueued = 0;   // (anything still to roll in belongs to the chord before)
        for (int k = 0; k < 128; ++k)
            if (sounding[k] && (! want[k] || ! holdCommon)) queueEvent (beat, k, 0.0f, false);
        // the new ones bloom in, upward, Roll apart
        std::sort (notes.begin(), notes.end());
        const double rollBeats = param (pRoll) * 0.001 * juce::jmax (1.0, tempoBpm) / 60.0;
        const float hum = param (pHumanize) * 0.01f;
        const float vel0 = param (pVelocity) / 127.0f * (resolvingNow ? 0.82f : 1.0f);
        int order = 0;
        for (int p : notes)
        {
            const bool keep = sounding[p] && holdCommon;
            if (keep) continue;
            const double jitter = hum * 0.012 * (rand01() - 0.5) * juce::jmax (1.0, tempoBpm) / 60.0;
            const double at = beat + 1.0e-6 + (double) order * rollBeats + juce::jmax (0.0, jitter);
            const float vel = juce::jlimit (0.03f, 1.0f, vel0 * (1.0f - hum * 0.35f * rand01()) * (p == bassOut ? 0.92f : 1.0f));
            queueEvent (at, p, vel, true);
            ++order;
        }
        // Restrike: struck again, once to three times, on its grid within the chord
        if (param (pRestrike) > 0.0f && rand01() < param (pRestrike) * 0.01f)
        {
            static constexpr double kRate[4] = { 1.0, 0.5, 1.0 / 3.0, 0.25 };
            const double step = kRate[juce::jlimit (0, 3, (int) std::lround (param (pRestrikeRate)))];
            const int slots = (int) std::floor ((len - 0.05) / step);
            if (slots >= 1)
            {
                const int hits = juce::jmin (slots, 1 + (int) (rand01() * 3.0f));
                std::vector<int> pick;
                for (int s = 1; s <= slots; ++s) pick.push_back (s);
                for (int i = (int) pick.size() - 1; i > 0; --i) std::swap (pick[(size_t) i], pick[(size_t) (rand01() * (float) (i + 1)) % (size_t) (i + 1)]);
                pick.resize ((size_t) hits);
                std::sort (pick.begin(), pick.end());
                int nth = 0;
                for (int s : pick)
                {
                    const double at = beat + s * step;
                    const float soft = juce::jmax (0.4f, 0.9f - 0.08f * (float) nth++);
                    int ord = 0;
                    for (int p : notes)
                    {
                        queueEvent (at - 0.004, p, 0.0f, false);   // (off just before: struck anew)
                        const float vel = juce::jlimit (0.03f, 1.0f, vel0 * soft * (1.0f - hum * 0.3f * rand01()));
                        queueEvent (at + (double) ord++ * rollBeats * 0.35, p, vel, true);
                    }
                }
            }
        }
        // the next chord, voiced ahead, for the card's rolling view
        {
            const auto& nc = nextChordRef();
            auto nv = voice (nc);
            if (bassMode > 0 && prevBass >= 0)
            {
                int b = 36 + ((currentKey + nc.bass) % 12 + 12) % 12; if (b > 46) b -= 12;
                while (b - prevBass > 6) b -= 12; while (prevBass - b > 6) b += 12;
                nv.push_back (juce::jlimit (28, 50, b));
            }
            const int cnt = juce::jmin ((int) nextNotes.size(), (int) nv.size());
            for (int i = 0; i < cnt; ++i) nextNotes[(size_t) i] = nv[(size_t) i];
            nextCount.store (cnt);
            nextStart.store (changeAt);
            nextEnd.store (changeAt + (nc.passing ? passLen : base));
            changeBeat.store (changeAt);
        }
        chordsPlayed.fetch_add (1, std::memory_order_relaxed);
        shownChord.store (curSpecial >= 0 ? 1000 + curSpecial : chordIdx); shownNext.store (nextSpecial >= 0 ? 1000 + nextSpecial : nextIdx);
        shownKey.store (currentKey); shownCharacter.store (character);
    }
};

//==============================================================================
namespace
{
    class BloomView final : public juce::Component, private juce::Timer
    {
    public:
        explicit BloomView (MidiBloomModule& m) : module (&m)
        {
            addAndMakeVisible (recButton);
            recButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3038));
            recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe05560));
            recButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            recButton.setTooltip ("Rec: records the chords MIDI Bloom plays; press again to save them as a .mid file");
            recButton.onClick = [this] { toggleRec(); };
            startTimerHz (30);
        }
        MidiBloomModule* mod() const { return dynamic_cast<MidiBloomModule*> (module.get()); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4);
            recButton.setBounds (r.removeFromBottom (24).removeFromRight (64).reduced (0, 1));
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = mod();
            if (m == nullptr) return;
            auto r = getLocalBounds().reduced (4).toFloat();
            juce::ColourGradient bg (juce::Colour (0xff1d1530), r.getX(), r.getY(), juce::Colour (0xff142a33), r.getRight(), r.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (r.withTrimmedBottom (28.0f), 6.0f);
            const auto& chars = bloom::characters();
            const int ci = juce::jlimit (0, (int) chars.size() - 1, (int) std::lround (m->getParameterBase (MidiBloomModule::pCharacter)));
            const int shown = m->shownChord.load(), next = m->shownNext.load(), key = m->shownKey.load();
            const int sc = juce::jlimit (0, (int) chars.size() - 1, m->shownCharacter.load());
            auto area = r.withTrimmedBottom (28.0f).reduced (10.0f, 6.0f);
            // the rolling view, right of the chord's name: the notes glide right to left
            // past the line where they play now, the next chord already coming in
            paintRoll (g, *m, juce::Rectangle<float> (area.getX() + area.getWidth() * 0.44f, area.getY(), area.getWidth() * 0.56f, 50.0f));
            if (const auto* shownC = bloom::shownChordOf (chars[(size_t) sc], shown))
            {
                const auto& c = *shownC;
                g.setColour (juce::Colours::white);
                g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
                auto nameRow = area.removeFromTop (34.0f);
                g.drawFittedText (bloom::chordName (chars[(size_t) sc], c, key), nameRow.withWidth (nameRow.getWidth() * 0.43f).toNearestInt(),
                                  juce::Justification::centredLeft, 1, 0.45f);
                g.setColour (juce::Colour (0xffffc35a));
                g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
                auto line = area.removeFromTop (18.0f);
                juce::String s = bloom::pretty (c.roman);
                if (const auto* nextC = bloom::shownChordOf (chars[(size_t) sc], next))
                    s << "   next: " << bloom::chordName (chars[(size_t) sc], *nextC, key);
                g.drawFittedText (s, line.withWidth (line.getWidth() * 0.43f).toNearestInt(), juce::Justification::centredLeft, 1, 0.55f);
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
                g.drawText ("Hold a key - it sets the key", area.removeFromTop (34.0f), juce::Justification::centredLeft, false);
                area.removeFromTop (18.0f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.62f));
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawFittedText (chars[(size_t) ci].description, area.toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
            if (m->take.isRecording())
            {
                g.setColour (juce::Colour (0xffe05560));
                g.drawText ("Rec ... " + juce::String (m->take.numEvents()) + " notes", r.removeFromBottom (24.0f).withTrimmedRight (72.0f),
                            juce::Justification::centredLeft, false);
            }
        }

    private:
        juce::String lastKey;
        float rollLo { 48.0f }, rollHi { 72.0f };

        void paintRoll (juce::Graphics& g, MidiBloomModule& m, juce::Rectangle<float> box)
        {
            const double now = m.beatNow.load (std::memory_order_relaxed);
            const double change = m.changeBeat.load(), nStart = m.nextStart.load(), nEnd = m.nextEnd.load();
            const double span = juce::jlimit (2.0, 8.0, nEnd - nStart);   // beats each side of the line
            const float nowX = box.getX() + box.getWidth() * 0.5f;
            const float ppb = box.getWidth() * 0.5f / (float) span;
            struct Bar { int pitch; double a, b; float velocity; bool future; };
            std::vector<Bar> bars;
            const int w = m.logWrite.load (std::memory_order_acquire);
            for (int i = 1; i <= juce::jmin (MidiBloomModule::kLog, w); ++i)
            {
                const auto e = m.noteLog[(size_t) ((w - i) % MidiBloomModule::kLog)];
                const double end = e.end < 0.0 ? now : e.end;
                if (end < now - span) continue;
                bars.push_back ({ e.pitch, e.start, juce::jmin (end, now), e.velocity, false });
                if (e.end < 0.0 && change > now) bars.push_back ({ e.pitch, now, change, e.velocity, true });   // (where it still rings)
            }
            const int nc = m.nextCount.load();
            if (m.shownChord.load() >= 0)
                for (int i = 0; i < nc; ++i) bars.push_back ({ m.nextNotes[(size_t) i], nStart, nEnd, 0.7f, true });
            if (bars.empty()) return;
            // the pitch range follows the notes, softly
            int lo = 127, hi = 0;
            for (const auto& b : bars) { lo = juce::jmin (lo, b.pitch); hi = juce::jmax (hi, b.pitch); }
            rollLo += ((float) lo - 1.5f - rollLo) * 0.15f;
            rollHi += ((float) hi + 1.5f - rollHi) * 0.15f;
            const float rowH = box.getHeight() / juce::jmax (6.0f, rollHi - rollLo);
            for (const auto& b : bars)
            {
                const float x1 = nowX + (float) (b.a - now) * ppb, x2 = nowX + (float) (b.b - now) * ppb;
                if (x2 < box.getX() || x1 > box.getRight()) continue;
                const float y = box.getBottom() - ((float) b.pitch - rollLo) * rowH;
                auto bar = juce::Rectangle<float> (x1, y - rowH * 0.42f, juce::jmax (1.5f, x2 - x1 - 1.0f), juce::jmax (1.6f, rowH * 0.8f))
                               .getIntersection (box);
                if (bar.isEmpty()) continue;
                const auto base = (b.pitch < 52 ? juce::Colour (0xff7fd8ff) : juce::Colour (0xffffc35a))
                                      .withAlpha ((b.future ? 0.38f : 0.95f) * (0.55f + 0.45f * b.velocity));
                // soft at both edges: the colour fades out towards them
                juce::ColourGradient fade (base.withAlpha (0.0f), box.getX(), 0.0f, base.withAlpha (0.0f), box.getRight(), 0.0f, false);
                fade.addColour (0.2, base); fade.addColour (0.8, base);
                g.setGradientFill (fade);
                g.fillRoundedRectangle (bar, juce::jmin (2.0f, bar.getHeight() * 0.5f));
            }
            // the line: where they play now
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (nowX - 2.5f, box.getY(), 5.0f, box.getHeight()));
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (nowX - 0.5f, box.getY(), 1.0f, box.getHeight()));
        }

        void timerCallback() override
        {
            auto* m = mod();
            if (m == nullptr) return;
            recButton.setToggleState (m->take.isRecording(), juce::dontSendNotification);
            recButton.setButtonText (m->take.isRecording() ? "Stop" : "Rec");
            const juce::String k = juce::String (m->shownChord.load()) + "," + juce::String (m->shownNext.load()) + ","
                                 + juce::String (m->shownKey.load()) + "," + juce::String (m->getParameterBase (MidiBloomModule::pCharacter))
                                 + (m->take.isRecording() ? juce::String (m->take.numEvents()) : juce::String());
            if (k != lastKey || m->shownChord.load() >= 0) { lastKey = k; repaint(); }   // (rolling: every frame while it plays)
        }
        void toggleRec()
        {
            auto* m = mod();
            if (m == nullptr) return;
            if (! m->take.isRecording()) { m->take.start(); return; }
            m->take.stop();
            m->take.save (chooser, "MIDI Bloom", "MIDI Bloom", m->bpmNow(), m->beatsPerBar());
        }
        juce::WeakReference<SynthModule> module;
        juce::TextButton recButton { "Rec" };
        std::unique_ptr<juce::FileChooser> chooser;
    };
}

std::unique_ptr<juce::Component> MidiBloomModule::createExtraContentComponent()
{
    return std::make_unique<BloomView> (*this);
}

// for tests: the chord names it has played so far are shown on the card
juce::String midiBloomShownChord (aquanode::SynthModule* m)
{
    auto* b = dynamic_cast<MidiBloomModule*> (m);
    if (b == nullptr) return {};
    const auto& chars = bloom::characters();
    const int sc = juce::jlimit (0, (int) chars.size() - 1, b->shownCharacter.load()), c = b->shownChord.load();
    const auto* chord = bloom::shownChordOf (chars[(size_t) sc], c);
    if (chord == nullptr) return "-";
    return bloom::chordName (chars[(size_t) sc], *chord, b->shownKey.load()) + " (" + bloom::pretty (chord->roman) + ")";
}

// for tests: how many chords it has played
int midiBloomChordsPlayed (aquanode::SynthModule* m)
{
    auto* b = dynamic_cast<MidiBloomModule*> (m);
    return b != nullptr ? b->chordsPlayed.load() : -1;
}

AQUANODE_REGISTER_MODULE (MidiBloomModule, MidiBloomModule::descriptor)
