# Black-box measurements of the original synth (version 1.7, headless VST2 host under wine)

Values are VST normalized 0..1 unless stated. "sel" = discrete index k set as k/(N-1).

## Gain staging (unit-amplitude wavetable, filters None, env full, vel ignored)
- OSC AMP: amplitude ∝ amp² (knob squared).
- MASTER VOLUME: ∝ vol²; vol 1.0 = 0.8968 relative to ... (vol 0.5 -> x0.25).
- Default patch output = 0.224 per unit wave: = 0.16 (F path, one filter bus) * 1.414 (both buses, mix 0.5 equal power) * ... with master 0.5.
  F1-only (route=1, mix=1) master 0.5: 0.16 per unit wave.
- ROUTING FADERS (OSC/NOISE/FB  F1-F2): value 1 = 100% F1, 0 = 100% F2. Gain per side = min(1, 2*v) (F1) / min(1, 2*(1-v)) (F2). 0.5 = both full.
- FLT 1/2 MIX: value 1 = F1 out, 0 = F2 out, equal power (cos/sin).
- SER/PAR: 0 = parallel, 1 = serial. F2 input = cos(pi/2*s)*bus2 + sin(pi/2*s)*F1out (equal power).
- PAN: constant power sin/cos, center 0.707.
- FILTER OUT GAIN: only when a filter type is set; gain ∝ (v/0.8)² (default 0.8 = unity-ish).
- Amp section: output amplitude = ENV² (env value squared).

## Envelopes (ENV4 -> amp)
- Attack linear in env domain, T = 9.2 s * k^4.
- Decay/Release exponential (linear in dB). t(-20 dB amplitude) decay: k=.3 .056s, .4 .12, .5 .28, .6 .63, .7 1.41, .8 3.18, .9 7.16, 1.0 16.0. release ~ same (5% faster).
  amplitude = env² so env τ = 2*τ_amp.
- Levels: env linear in knob (amp = level²).
- Sustain loop count default 0 (stored in preset, not a VST param): env holds decay level.
- VEL fader: env scaled by (1-v + v*vel/127).
- KTR fader: env scaled by clamp(1 - kt*max(0,note-69)/31, 0, 1).

## Oscillators
- Pitch knob: ±64 semitones linear (0.5 = 0). Note 60 = 261.63 Hz.
- Wavetables: 2048-sample frames; position p -> frame p*(N-1), linear crossfade between frames.
  Some tables play an octave (or more) below the note (cycle = 2 periods?): see per-table pitch div in osc_all.json (/2 entries).
- SPECTRUM mode (intensity I): octave-band mipmap, band b (harmonics 2^b..2^(b+1)-1) fades in between I=(b-1)/10 and b/10 (s-curve, ~0.57 at mid);
  overall gain g = 0.5+0.5*sin(pi*min(I,0.5)) (0.5 at I=0, 1 at I>=0.5). Band 0 always on.
- BEND+ (mode idx1): I=0 identity; warp each half: w(t)=0.5*(2t)^p (t<.5), 1-0.5*(2(1-t))^p; p = 4^I.
- BEND- (mode idx3): symmetric power about center: w=0.5+0.5*sgn(u)|u|^p, u=2t-1, p≈2.92^I.
- BEND-/+ (idx2): I<0.5 -> Bend- with amount (0.5-I)*2; I>0.5 -> Bend+ amount (I-0.5)*2.
- FORMANT (idx4): out = table(frac(r*phase)) * (1-cos(2pi*phase))/2, r = 2^(4I) (1..16).

## Mod osc
- Sine. RM: out = (1-a)*x + a*x*m (linear crossfade). Pitch knob ±64 st like oscs.
- PM/Position: to be fitted.

## Filters (noise measurements, F1 path)
- Cutoff: fc ≈ 1100 Hz * 2^(10*(c-0.5)) (LP2 res0 = 2-pole Q 0.5 fits).
- Resonance reduces passband: LP2 res1 passband -16 dB, peak +29 dB @ ~1044 Hz (c=.5).
- LP4 24 dB/oct, passband +1.9 dB (res0). Res1: passband -14, peak +25.
- Acid: 24dB/oct, res modest (+14 dB at res1), passband ~ -15dB at 50Hz?? (has HP-ish lows).
- Daft: 12 dB/oct, fc lower, res peak at 0.7x cutoff.
- Scream: 12dB/oct; resonance peak at ~0.35x cutoff; BW knob = "Scream" amount (bw0 huge low peak). Nonlinear - needs sine tests.
- BP/BR: BW sets width (bw 0.5 ≈ ±2.5 octaves edges, bw1 ~flat), res adds peaks at edges.
- AP: allpass (-8 dB?!), res adds peak at fc and reduces lows.
- DN: two notches (c=.5: ~400 & ~3200 Hz), base -9 dB.
- Comb: no effect at res 0; res = feedback.

## Noise
- 12 sample-based types. COLOR = playback rate: c=1 full band, each -0.25 ≈ /2.5 bandwidth (≈5.5 octaves over range).
- White c=1 flat. Level ~0.105 rms at amp 1 (F path).

## Unison
- Pitch spread: voice spacing = S semitones (S=fader display 0..1, with default range fields), centered.
- Pan spread decorrelates.

## EQ (master)
- LoShelf: boost +12 dB below ~150Hz; cut acts like 6dB/oct HP from ~2kHz downward (-34 dB @30Hz).
- HiShelf: boost +11.5 dB @16k; cut like 6dB/oct LP from ~400 Hz.
- Peak: ±12 dB (cut -35 notch), freq 0 ≈ 170 Hz, 0.5 = 1 kHz, 1 ≈ 6 kHz.

## Voice
- Glide time ≈ 3 s * k^4 (equal mode), smooth.
- Vibrato depth ±2 semitones * k. Rate: k=.25 1.66 Hz, .5 3.88 Hz, 1.0 32.7 Hz.

## Preset file (.nmsv) format study
- Chunked container; payload LZF compressed (find "01 <u32 raw> <u32 comp>" near end).
- 137 floats (VST params 0..136 in order) start after "tsrp" tag (new v24) or right after version word (old v22: at 0xc0).
- Knob records: [u32 id][u8 0][u32 ver][...][u32 count][count x (f32 amount, i32 source, u8, u8)]. id = VST param index.
- Selectors are stored in module blocks as internal enum ids (not menu order).

## Preset file: what the importer reads, and what is still open (state of 5.20.0)
- A knob is stored as `[u32 version 2|3][f32 lo][f32 hi][u32 n][n x (f32 amount, i32 source, u8, u8)]`
  FOLLOWED by its tag `[u32 index][00]`; the index is the knob's place in the automation list (0..136).
  (Reading the tag as a header - as the first importer did - gives every slot to the knob before.)
- Sources: 0-3 Env 1-4, 4-7 Mod 5-8, 13-20 Macro 1-8, 21 KTr, 22 Vel, 23 AT, 24 TrR (8-12 rare, unknown).
- Check over the 1352 factory files, macro name against the knob its slots sit on: "Cutoff" 33 (566) / 36 (332),
  "Reso" 35 / 38, "Attack" 97, "Release" 125, "Decay" 105, "Glide" 126, "WT-Pos" 9 / 14 / 19,
  "Intensity" 10 / 15 / 20, "Feedback" 26, "FX Mix" 44 / 48, "Vibrato" 127 / 128, "Speed" 58-61.
- lo/hi are the knob's range in its own unit: pitch 0..10.583 octaves (127 semitones), cutoff -3..10,
  bandwidth 0..10.583 (0..8.6 for some types), mod-osc phase 0..1.5, noise colour -6..6, pitched insert
  knob -5.33..10.67, LFO rate -108..144, vibrato rate -48..72.
- Slot counts: pitch 2, position and intensity 3, amp 2, cutoff and bandwidth 3, resonance 2, envelope and
  mod-page knobs 1.
- Two knobs have no tag: the slots after tag 54 are the bypass gain's, after tag 56 the amp's (both Env 4
  by default).
- Oscillator mode + wavetable follow the oscillator's amp tag (11 / 16 / 21); filter 1's type follows
  oscillator 3's pair, filter 2's type follows tag 35; insert types follow 38 / 41.
- Against the previews (80 random factory sounds, one held note): spectral distance median 23.7 dB with the
  first importer, 16.1 dB with this one; better in 58, worse in 13.
- Not found yet: mod page types, LFO shapes, stepper / performer steps, envelope loop counts, the two flag
  bytes of a slot (one of them is probably the side chain switch).

## Round 3 (static A/B against the original, noise and tone sweeps)

- **EQ:** first-order shelves at 400 Hz (low) and 4 kHz (high), peak at 150 Hz x 46.7^freq (RBJ, Q 1.3); gains for all
  three: above mid +12 (2b - b^2) dB, below mid -(25 b + 10 b^2) dB, b = 2 knob - 1 (cuts reach -35 dB).
- **Ladder family:** LP4, LP2, HP4, HP2, Allpass and Double Notch are one 4-pole ladder (pole at 1052 Hz x 2^(10(cut-0.5)))
  read through tap mixes (u, y1..y4): LP4 y4, LP2 y2, HP4 (1,-4,6,-4,1), HP2 (1,-2,1), AP (1,-4,4), DN (1,-4,12,-16,8).
  Feedback k = 4.21 (1 - (1-res)^2) for all (self-oscillates from res 0.78); level 1 + 0.504 k - 0.135 k^2 times
  1.368 / 1.035 / 1.037 / 0.681 / 0.395 / 0.408. Saturation in the loop, drive 0.28 (self-oscillation level within 0.5 dB).
  Double Notch: notches +-1.25 octaves around the cutoff, the bandwidth knob does nothing.
- **Daft:** the 2-pole tap with the saturation in the feedback only; pole 1053 Hz x 2^(-1.166 res^0.795) up to res 0.5,
  then 0.628 + 0.184 (res - 0.5); k = 2.13 res + 18.09 res^3 (sings from 0.55); gain 1.517 (1 + 1.5 res).
- **Acid:** the 4-pole ladder behind a 128 Hz one-pole high-pass, k = 1.91 + 1.31 res, gain 2.0, pole 1095 Hz.
- **Band-reject:** 2-pole LP at the lower edge + (HP + half BP) at the upper edge; edges 5.29 bw octaves either side,
  damping 2 (1-res)^2.08 + 0.042, gain 0.70. **Bandpass:** (HP + half BP) at the lower edge into a 2-pole LP at the
  upper, same edges and damping, gain 0.41 + 0.59 (1-res)^2.2.
- **Scream:** 2-pole LP with positive feedback of its output through a first-order HP at 8.38 Hz x 2^(8.48 bw)
  (independent of the cutoff), cubic soft limiter (1.236) before the LP, feedback 1.76 (1 - (1-res)^1.84), Q 0.5 + 1.137 res.
- **Comb:** teeth at multiples of the cutoff (1048 Hz at mid), feedback 1 - 0.906 (1-res)^2.18, one-pole damping in the
  loop at 0.706 x cutoff x 2^(12.4 bw) (fully open from bw 0.5), saturation 0.17.
- **Noise colour:** linear-interpolated white read at 2^(6 (colour - 1)) of the sample rate.
- **Bypass:** taps its source before the amp knob, gain^2 x 1.12 against the main path.
- **VA-Sync:** -3.5 dB as the slave ratio climbs: -clamp((I - 0.53) 9.5, 0, 3.5) dB.
- **Formant mode:** the table is read half a frame on: frac(r u + 0.5) x hann(u), r = 2^(4I) (0.5 dB rms at every I).
- **Bend modes:** the warped read keeps the full table (no band limiting by the warp).
- Missing from the usual wavetable folder (stand-ins are used): Woody, Sync, Carbon, Melan, Luna. Gentle Speech's
  file differs from what the original plays.
- Static A/B (same knobs in both, no modulation): seed 9 5.7 -> 3.8 dB median (mean 6.4 -> 4.4, over 10 dB 9 -> 4);
  fresh seed 31: 3.2 median, 4.3 mean, 29 of 60 under 3 dB.

## Round 4: driving the original's own editor (sounds loaded through File > Open, crafted test sounds)

How: the original's editor runs under a virtual display; its File > Open Sound dialog loads `.nmsv` files typed in
by a script, and the host renders them. Its saved state (getChunk) after clicking a control shows where that
control lives in the file. Test sounds are written by changing bytes of an init sound (LZF re-packed, container
sizes fixed) and load like any other sound. 126 sounds (120 factory + 6 user) were rendered as references at the
same note and timing as Massif's renders. The measuring patch: a sine (table "Sin-Squ" at position 0) whose pitch
the source under test modulates; the pitch is read from the analytic signal.

### Modulation pages (5..8)
- Stored right after the GLIDE-TIME knob record: 17 bytes, then per page `[u32 type]` + block, then `[u32 3]`.
  LFO (type 0): `[u32 2][f32 phase upper][f32 phase lower][u32 shape upper][u32 shape lower]`;
  performer (1): `[u32 1]` 16 x `[f32 level up][u32 curve up][f32 level low][u32 curve low][u8 xfade row]`,
  `[u32 2]` 16 x `[u8 amp-mod row][u8 1][u8 0]`, `[u32 play marker]`;
  stepper (2): `[u32 2]` 16 x f32 step, 16 x u8 glide row, `[u32 2]` 16 x `[u8 amp-mod row][u8 1][u8 0]`, `[u32 marker]`.
- After each page's Amp Mod fader tag (70..73): `[u32 loop start][u32 loop end][u8 mono]`.
- After each rate tag (58..61): `[u8 sync][u8 position sync (newer files only)][u8 restart][u32 ratio numerator]
  [u32 ratio denominator as a power of two]`; after page 8's: 5 x `[u32 2][f32 attack][f32 decay]` = the internal
  envelopes of pages 5..8 and of the OSC page.
- LFO shapes: 0 sine, 1 saw (falling), 2 square, 3 triangle (starts at -1), 4.. the 31 table curves (bipolar except
  the "uni" ones, which run 0..1). Performer curves: 16 unipolar curves. Massif carries both as breakpoint models.
- Rate (LFO cycles or steps per second): 1.0221 Hz x 2^(12 (k - 0.5)) (k = .75: 8.18 Hz, 1: 65.4 Hz). Synced: one
  cycle / step = num/den bars. Phase offsets are added per curve. Restart off: free running.
- Amp knob: x k^2 (also when modulated). LFO XFade: linear, up = upper curve.
- Stepper: steps 0..1; steps in the glide row approach the next value exponentially, time constant from the Glide
  fader: 0.25 -> 11 ms, 0.5 -> 52 ms, 0.75 -> 285 ms, 1 -> 348 ms (independent of the rate); steps in the amp row
  are multiplied by the Amp Mod fader (linear; down = no modulation, i.e. silent).
- Performer: one curve cycle per step times its level; steps in the xfade row mix lower -> upper by the XFade fader,
  the others play the upper; the amp row as the stepper's.
- Each note starts at the loop start (the play marker is not a start point).
- Internal envelope: linear attack 0 -> 1, linear decay 1 -> 0 (decay fully up: holds). Time for the full travel:
  .1 0.056 s, .25 0.113, .4 0.278, .5 0.542, .6 1.24, .7 3.08, .75 4.79, .85 8.8, .95 9.9, 1 10 s.
  Stored sources 8..11 are the pages' internal envelopes (used on their own page's rate / amp / xfade), 12 the OSC
  page's. Modulation of a page's knobs adds in knob units like any other (checked with a macro on the rate).

### Envelope pages
- After each release knob's tag (122..125): `[u32 morph shape A][u32 shape B][u32 loop count (33 = inf)][u8 trigger
  zero reset][u8 linear][u8 play mode 0 gate / 1 one shot / 2 hold]`. Shapes index the 22 sustain curves (Linear,
  Sinoid, Extreme 1/2, Curve 1..6, Steps, Triangle 1.5/2.5/3.5, Sine 1.5/2.5/3.5, Expo x2/x4, Peak Expo x2/x4, Switch).
- With a sustain loop the decay hands over after 3 time constants; without one it keeps settling.
- Linear decay: a straight line taking 2.35 time constants (2.04 x the 20 dB time).
- Sustain loop pass time: .3 0.119 s, .5 0.42, .7 1.54, .85 3.87, .92 6.0, 1 10 s; the path follows the shape
  (both directions start fast for the Expo shapes; the way back is the same path reversed), morph mixes A -> B.

### Unison and selectors
- The unison pitch knob's record carries the spread range twice ([lo][hi - lo][lo][hi], octaves). Spread
  S = lo + fader x (hi - lo); three voices sit at -S, 0, +S, two at +-S/2; chord mode stacks 0, +S, -S, +2S, ...
  The voicing page's Chord switch: macro-name anchor - 0x15 (0 = on).
- Insert 2's type sits after insert 1's on byte (tag 41 + 10); FX types and other selectors are now read relative to
  the knob records' tags (the older header scan missed records with two modulation slots).

### Result against the real renders (126 sounds, one held note)
- 5.20.1: spectral distance median 9.3 dB, 73 of 126 under 10 dB, envelope correlation 0.72.
- With the mod pages: 6.9 dB, 90 under 10 dB, 0.80 (better in 50, worse in 13).

### Keytracking
- Five curves before the macro names: the oscillators' user curve, cutoff and resonance curves of filter sets 1 and 2.
  Each `[u32 2][u32 128][u32 128][u8 n]` then 2n - 2 floats: y of the first point, x / y of the inner points, y of the
  last point (x 0 and 1 implied), notes / 127 (any point count; the original's editor shows them as dragged).
  After them, per filter `[u32 1][u32 set 1 / 2 (else off)][u32 60][u32 0]`.
- Cutoff follows the curve 1 : 1 around note 60 (measured with a resonant LP4 at notes 36 / 48 / 60 / 72: 188, 371,
  738, 1480 Hz). Many sounds flatten it (e.g. y 44 at note 0, 64 from 36 to 64, 85 at 127).
- The unison pitch spread and the keytracking curves explained most of the remaining difference in the dub techno chords.

### More from round 4
- Insert places (the routing page): after insert 2's on byte (tag 43 + 6) seven i32 slots - pre F1, pre F2, post F1,
  serial, post F2, feedback loop, pre amp - holding the insert placed there (0 / 1) or -1.
- Oscillator keytracking modes: after the oscillators' curve, per osc 1..3, mod osc and inserts `[u32 1][u32 mode]
  [u32 60]`, mode 0 off, 1 linear, 2 the curve.
- Envelope knobs take their modulation (velocity on the levels, macros on the times) - Massif had read them unmodulated,
  which made sounds with velocity on the amp envelope's levels up to 30 dB too quiet.
- Tubes (classic / tube-tube / bright): fitted to rendered sines - gain 2^(a + b drive) into a soft-kneed clipper with
  a bias (even harmonics), then a fixed make-up (classic 1.77 / 4.148 / -2.111, knee 0.609, bias 0.179; tube-tube 2.374 /
  3.34 / -2.313, 0.889, 0.382; bright 2.511 / 3.463 / -2.595, hard, 0), on the signal before the master volume; the
  third and fourth knobs do nothing audible.
- Effect dry / wet: equal power (wet sin, dry cos of dw x 90 degrees).
- Effects sit before the master volume (the master scales a tube's output without changing its distortion).
- Against the real renders after these: spectral distance median 5.2 dB, 113 of 126 under 10 dB, envelope correlation
  0.85, level difference median -0.7 dB (it was -5.1).
- Effect wet levels checked with white noise at mid settings: chorus, ensemble chorus, dimension and the big reverb
  already within 1 dB; the small reverb sits 5 dB lower, flangers 3.6 dB and the phaser 2 dB higher than Massif had
  them; the delays' damping is a one-pole low-pass near 500 Hz at 0.5 (22 kHz x 2^(-10.6 damp)).

## Round 5: writing .nmsv files (export)
- The writer works over a template file (the sound's own file while it is the sound, else an export template - any
  sound file of the user's, an init sound is best) and writes only what differs from the template as Massif reads it.
  Every one of the 2705 sounds goes import -> export (over its own file, and over an init sound) -> import with the
  same patch; over its own file an unchanged sound comes back byte for byte.
- The payload has chunk headers of its own before the knobs: u64 sizes at 0, 84, 124 and 180, ending 0, 0, 8 and
  24 bytes before the payload's end (all factory sounds). A payload that changes length (keytracking curves with
  another point count, mod pages of another type, macro names) must change them too - without that the original
  answers "Couldn't open File". The container's sizes are the u64 12 bytes before each "hsin" / "DSIN" chunk that
  covers the payload (or the name).
- The sound's name: the first length-prefixed UTF-16 string 32 bytes after a "DSIN" in the container (all 2704 named
  factory sounds). The original shows this name, not the file's.
- The macro names were not found in 142 of the 2705 sounds - which also lost their voicing (unison count, voice mode,
  trigger, the unison switches): 34 had an 8-bit character in a name (an umlaut), 108 store the float after the names
  as 0x41295556 rather than ...55. Both are read now (names as UTF-8, written back as 8-bit text).
- The knob record scan read an LFO page (shapes, phases) as a knob record for knob 2 in some sounds: the scan now
  steps over the mod pages after the glide knob.
- Tables: the format names one of the built-in tables per oscillator, never the table itself; a generated or loaded
  table is written as the default table.
- Against the fresh references (245 sounds, 3 s pre-roll): spectral distance median 5.1 dB, 206 of 245 under 10 dB,
  level median 0.0 dB, envelope correlation 0.87.

## Round 6: bug hunt (export over the init sound as a probe)
- Method: every reference sound exported over the init sound and played by the original. Where that differs from
  the original's own rendering of the sound, the file holds something Massif does not read.
- Keytracking curves come in two forms: [u32 2][128][128][u8 n] + 2n-2 floats, and the older [u32 1][128][128] + 8
  floats (always five points) - 442 of the 1352 factory sounds. The older form was not read at all: those sounds
  played with default keytracking (curves, the oscillators' keytrack modes, the filters' curve sets). Brutal
  Electro 2 went from 25.6 to 4.0 dB, Analogue Dreams 16.8 -> 2.9, Cumin 11.9 -> 2.7.
- The bytes before the macro names (c = their start): c-0x26 [u32 1], c-0x22 u32 unison voices, c-0x1e u32 voice
  mode, c-0x1a u32 trigger, c-0x16 u8 unison pitch on, c-0x15 u8 chord (0 = on), c-0x14 u8 unison wt on,
  c-0x13 f32 bend down (semitones, negative), c-0x0f f32 bend up, c-0x0b u8 unison pan on, c-0x0a u8 1,
  c-0x06 u8 ?, c-0x05 u8 ?, c-0x04 [u32 1]. The bend range is read now.
- The mod pages' rate records hold their sync and restart switches as bytes too ([sync][pos][restart] in files
  of version 23/24, [sync][restart] before); they always equal the switch knobs 74..81. Export writes them.
- Noise through every filter type (white noise, 2 cutoffs x 3 resonances x 2 third-knob values): the low-, high-,
  all-pass and double notch types match within about 1 dB everywhere (see the band-pass and the rest below).
- Hubschraube ran away to infinity (a singing Daft filter fed back on itself): the feedback signal is now bent
  above 8 (never reached by a factory sound otherwise), so such a loop stays bounded.
- Band-pass with the bandwidth near 0: the two resonances stacked in Massif (up to +21 dB at res 0.9, noise in);
  the band's width matched already. A gain law from 12 measured points (res 0.3/0.6/0.9 x bandwidth
  0/0.02/0.05/0.1) brings the band's power within 0.2 dB and the peak within 1.6 dB. 27 factory sounds.
- Still apart (noise survey): Daft at resonance 0.95 sings about 6 dB louder in Massif, Acid at a low cutoff has
  more resonance (+5 dB peak at cutoff 0.4), Scream at res 0.95 with a wide third knob.
- Filter FM (the modulation oscillator's fourth target): with noise through a band-pass, mod osc at 3, 30, 65, 130
  and 260 Hz and amounts up to 1, the original's output does not change at all - Massif does not apply it either.
- Two bytes 12 and 11 before the first keytracking curve (545 sounds 3/1, 401 1/0, 200 4/1, 164 0/1, the init
  sound 0/6) and the bytes 6 and 5 before the macro names change how the original lines up its oscillators'
  phases (two equal sines: summed with the init's 0/6, 9 dB lower with 3/1). Not modelled yet; kept on import and
  written back on export (Just Bass re-saved over the init sound: 5.4 dB -> 0.2 dB with them).
- Performance (the same samples, bit for bit): the table read's per-block parts (band limit, spectrum gain, bend
  power), the noise rate, the effects' coefficients (reverb decays, delay damping, tube gains) and the knob
  modulation walk (only knobs that have modulation) are worked out once per block: 35-50 % less CPU.

## Round 7: phases, glide, Daft, filter FM, Acid
- Voicing bytes before the macro names: c-0x06 is the glide's "Equal" (1) / "Rate" (0) switch, c-0x05 "Restart via
  Gate". The four oscillator phases (osc 1-3, mod osc; 0..1 = 0..360 degrees) are floats at 6/10/14/18 bytes after
  the glide knob's record tag. Read and written back.
- Restart via Gate on: every note starts the oscillators at their phase settings (two equal sines sum to the
  expected level, cancel exactly with the second at 0.5). Off: they run freely, so the phase at a note is anywhere.
  Massif starts every voice's oscillators at scattered phases and lets them run when the switch is off.
- Glide (sine, 12 semitones): pitch fraction 1 - (1 - t/T)^1.8. Equal: T from the knob only; Rate: T scales with the
  interval / 12. T(knob) 0.3 -> 0.031 s, 0.4 -> 0.076, 0.5 -> 0.185, 0.6 -> 0.43, 0.7 -> 0.78, 0.8 -> 1.5.
  Massif t50/t90 at 0.5: 0.060/0.135 s (original 0.059/0.137); Rate over 24 semitones 0.119/0.268 (0.118/0.274).
- Filter FM works on the ladder types only (LP4, LP2, HP4, HP2, Allpass, Double Notch, Daft, Acid); the original
  greys the button out for Bandpass, Band-reject, Scream and Comb and ignores a stored target there (that is why the
  band-pass test of round 6 showed nothing). Measured on the self-oscillating LP4 (noise 0.1, res 1): the singing
  pitch follows cutoff + D sin(mod osc), linear in Hz: D = 75 / 466 / 1866 / 4212 Hz at amount 0.1 / 0.25 / 0.5 /
  0.75, i.e. 7470 x amount^2 Hz; the same at mod osc 3 and 30 Hz, cutoff 0.3 / 0.5 / 0.7, notes 48 / 60 / 72 and on
  filter 2. Below 0 Hz the cutoff stops at the bottom (no fold-over). On Daft the swing is added before Daft's own
  pole-frequency scaling (max 1690 Hz at amount 0.5, Massif 1673). Propellersolo (FM by a modulated amount): 13.4 ->
  7.9 dB.
- Daft sings by itself from res 0.55. With noise at two levels and two cutoffs per resonance the original's tone was
  30-85 cents higher, cleaner over loud noise (90 % of the power in the tone against 49 %), with the noise around it
  5-11 dB lower. Drive, gain, feedback, pole frequency and a touch of the 3-pole tap fitted per resonance (0.6 /
  0.75 / 0.9 / 1.0), interpolated in between: pitch within 20 cents, level within 1 dB, the noise's spectrum within
  about 2 dB rms (fit cost 63 / 868 / 670 / 259 -> 28 / 21 / 16 / 18).
- Scream re-measured (noise, cutoff 0.4/0.65 x res 0.8/0.95 x third knob 0.3/0.6/0.9): the tone's pitch within 0.5
  semitone, its third harmonic within 1 dB, the level within 1 dB (2 dB at cutoff 0.65, res 0.8, knob 0.9). The
  round-6 figure for it came from the tone falling between analysis bands; no change.
- Filter FM at audio rates (self-oscillating LP4, mod osc 50 / 131 / 262 / 523 Hz, swing 200 and 467 Hz): the
  sidebands match within about 0.5 dB; Double Notch and LP2 with noise and the mod osc at 262 Hz match band for band.
- Acid refitted (noise, cutoff 0.25 / 0.4 / 0.55 x res 0.3 / 0.6 / 0.9; Massif was up to 7.5 dB too loud at the low
  cutoff, with too much resonance there): the same ladder as LP4 (1052 Hz), a one-pole high-pass at 237 Hz in front
  and another at 107 Hz inside the feedback path, feedback 2.17 + 1.42 res, gain 2.47. Every band within 1 dB rms,
  the level within 0.5 dB (fit cost 376 -> 7.7). Filter FM on Acid: max 2422 Hz at amount 0.5 (original 2485).
- Modulation slots: 10 bytes each - f32 amount, i32 source, u8 muted, u8 side chain flag. Neither flag was read.
  The last slot of a knob with its flag set is a side chain: it scales the knob's slots that have the flag set as
  well, by its source's value as it is (bipolar sources too: an LFO's negative half turns the modulation round);
  its amount does nothing. 329 factory sounds use one, mostly a macro. Measured with filter FM driven by velocity
  0.3 over an amount of 0.1 (swing in Hz, original / Massif): side chain macro 1 at 0.5 356 / 353, slot not
  flagged 685 / 683, macro at 0 75 / 74, key tracking 267 / 266, an LFO: the swing follows it to nothing and back,
  window for window. A muted slot (46 in the factory sounds) does nothing (tested: macro at 0.5 and at 1 on the
  cutoff - both as with no modulation). Read, played, written back (flags included; a slot written over a muted
  one is unmuted). Hollow Coppice's filter FM (velocity, side chain macro 2 at 0) is why it played too bright once
  filter FM was in.
- Envelopes, measured on the envelope itself (envelope 3 on the cutoff of a singing LP4, its pitch read every 5 ms):
  - the stages run at their current rate: a time knob that is being modulated changes the speed from then on.
    Chordissimo's envelope 3 raises its own attack time (0.127 + 0.258 x itself): in the original the attack takes
    60 ms, rising ever slower; Massif had computed the position from the elapsed time over the current length and
    finished in 7 ms. Now the progress is summed (identical for unmodulated knobs).
  - linear decay: the whole way takes 9.2 s x k^4 - the attack's law (9.11 .. 9.16 s x k^4 at knob 0.25 .. 0.95).
    It had been 2.35 time constants of the exponential decay, up to 2.5 x too long at the top of the knob.
  - the exponential decay's time constant is 6 % longer than the table from the amp measurements gave (knob 0.4 ..
    0.85 on the cutoff; the amp at 0.55 / 0.75 says 5 - 10 %): decay and release x 1.06.
  - against the 245 references: envelope correlation better in 11, worse in 2; Chordissimo 9.4 -> 8.6 dB (its
    level curve now follows the original's).
- The preset BPM: a float 22 bytes before the payload's end, in a fixed tail [u32 x4][u32 0..2][f32 BPM][u8][u8]
  [00 00][00 00 0d 62 65 85 01 00][00 x6] (1350 of 1353 factory sounds; every loop sound's BPM in its name
  matches). Read and written back.

## 5.20.6
- Silent on Android: the built-in stand-in / generated tables were made with std::polar, which in libc++ (Android,
  macOS) returns NaN for a negative radius (the saw's even harmonics) - every such table was NaN and the output
  muted. Now a cos / sin pair. Checked with an aarch64 build under qemu: the init sound renders as on the desktop.

## 5.20.7
- Android: loading a preset, a sound of the original or the wavetable zip did nothing. Massif read the picked
  document through its URI, which Android reports as a local file in shared storage that the app may not read.
  Now it takes the picker's own copy of the document (getResult()) first and the URI's stream only as the
  fallback; saving writes the picker's scratch file, which goes into the chosen document afterwards.

## 5.20.8
- Preset bundles: a zip with .nmsv sounds anywhere inside (Import, or Load) - the sounds are read with their folders
  as categories, packed once and kept whole with the state (so in Aquanode Modular's patches; Massif's own .massif
  presets stay single sounds). The preset menu lists them by folder (long folders in runs of 50), < > step through
  them while the current sound is one of them. The 1353 factory sounds: 1.3 s to import, 4.8 MB of state.
- Reverb, measured with noise bursts through the original (size 0.3 / 0.5 / 0.8, density 0.2 / 0.8, colour 0 / 0.5
  / 1, the small one at 0.5): a fixed 20 ms pre-delay (none in the small one); left and right uncorrelated (0.00 -
  Massif's were +0.5); the decay time bell-shaped over frequency, the lows too dying away sooner than the 250 - 500
  Hz band (colour 0: 63 Hz 0.53 x, 8 kHz 0.39 x; colour 1 nearly flat), more so at large sizes; density lengthens
  it a little (x 1.15 from 0.2 to 0.8); the level nearly the same at every size (Massif's rose with the size, up to
  4 dB too loud). New reverb: pre-delay, two four-stage all-pass diffusers (one per side), an eight-line feedback
  delay network spread over 1:2.5 with slowly wandering delays (no metallic ringing or flutter echo - the "clacks"
  of a click through the old one), three-band losses per line, two orthogonal read-outs.
- Dub Techno chords: the harsh ones had a 4-pole low-pass at resonance 0.82 (it sings from 0.78) opened to the top
  by the filter envelope - a tone past 10 kHz folding back as noise. Its low-pass is now a 2-pole one below
  self-oscillation, with a smaller envelope.

## 5.20.9
- Diagrammatic blew up now and then (once 1e21): the reverb's delay lines read one sample past their buffer when the
  read position landed a hair below zero (in float that wraps to exactly the length). The index now wraps as an
  integer too; valgrind clean, the 1353 factory sounds at 44.1 / 48 kHz and four block sizes without a loud one.
- Reverb at small sizes: measured T60 0.062 s at size 0.1 and 0.12 s at 0.2 (Massif rang on for 0.3 s, the
  diffusers ringing): the input and the diffusers scale down with the size, the early part is mono. Qubo 7.1 -> 4.9.
- Daft with a hot input (Bumper was 17 dB too loud; three detuned saws and noise at four levels x cutoff 0.5 / 0.75
  / 1 x res 0 ... 1, 132 cases): the original squeezes a loud input at every resonance (3 dB at res 0, 5.5 dB at res
  0.45 with three saws at full level), while Massif's feedback-only saturation let the loop lose its grip and the
  level climb (+11 dB at res 0.45). Now a soft input stage (drive 0.45 + 3.11 res - 3.25 res^2), a feedback
  saturation that fades in towards res 0.5 (drive 8 (res/0.5)^1.74), the gain trimmed by (-1.64 res + 2.74 res^2) dB:
  levels within 1.0 dB rms (was 5.0), worst 3.7 dB (res 0.75 with three saws). Bumper +17 -> +2 dB, Hubschraube
  +17 -> 0 dB; reference set: 10 better, 1 worse (Pitch Black spec 3.1 -> 4.3), median 4.1 dB.
- Measured, not yet in (next round): the feedback is much stronger in the original than Massif's a^2 - loop gain
  2.5 a^2 before / after the amp and 3.75 a^2 after a filter (+1.5 / +8.5 dB at 0.25 / 0.5 through open filters,
  runaway from about 0.65), the same through LP4 / BP / BR / Acid / HP2 within 0.8 dB, in parallel and serial and
  with the feedback routed to one or both filters. Past unity it does not latch but flips like a relaxation
  oscillator (70 Hz at 0.75, 26 Hz at 1.0 before the amp): a DC block at 8.3 Hz and a cubic limit at 3.65 match it
  within half a semitone. On the reference set 24 better and 16 worse - the worse ones have Scream or Comb inside the
  loop, where Massif's response near self-oscillation still differs (Comb before the amp at 0.4: +8.8 dB), so those
  two come first.

## 5.20.10
- The feedback, continued (still not in): Scream and Comb inside the loop measured (Comb res 0.2 / 0.34 / 0.5 x
  bandwidth 0.17 / 0.5; Scream cutoff 0.4 / 0.6 x res 0.3 / 0.5 x third knob 0.25 / 0.6; amounts 0.2 / 0.3, after filter
  1 and before the amp): with the measured law they match within 1 - 2 dB, except right at the edge of runaway (Comb
  res 0.5 before the amp at 0.3: Massif runs away, the original not). The law also holds with a hot input (three saws,
  -12 dB: within 1.7 dB) and with the amount set by a macro (0 + 0.33 by Macro 1 = 0.33 on the knob, within 0.1 dB).
  So Scream and Comb are not what makes 18 of the reference sounds worse. Incinerator, the worst (spec 3.3 -> 14.2):
  in the original its feedback (0.33 after filter 1, to both filters, Comb into Scream, serial 0.58) adds nothing
  (-13.4 dB with, -13.5 without), while Massif's reshapes the sound through the Comb (+9.4 dB with filter 2 off).
  Ruled out as the cause: unison, the filters' out gains, modulation of the amount. Next: Incinerator's routing
  (oscillators to both filters, serial blend, the filter mix) taken apart one by one in the original.
