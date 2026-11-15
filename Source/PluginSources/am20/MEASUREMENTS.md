# AM-20: how it was measured

AM-20 is modelled on the MS-20 plugin (Windows, VST2), measured only from the outside, the
same way as other remakes in Aquanode Modular. Nothing in the original was disassembled or decompiled.
None of its data ships with AM-20: the factory sounds in `Factory.h` were made for AM-20, and the original's
programs only load from the user's own `.fxp` / `.fxb` files.

## Setup

- The original ran under Wine (64-bit) in a small VST2 host written for this job. The host only uses the
  public VST2 interface: setParameter / getParameter, the parameter names and display strings, getChunk
  (the saved state), MIDI in, and audio out at 48 kHz.
- The patch-panel cables cannot be set through parameters. They were drawn in the original's own window
  (an automated mouse drag from one jack to another) and the saved state read back after every cable.

## Preset format (`PresetIO.h`)

- 224 VST parameters. The panel knobs are 0..35; voice, MG-sync and external-modulation controls follow.
  Some of those only change from the original's window (Assign, Trigger, Poly and Unison counts, the
  external-modulation sources and amounts). They were changed there one at a time, and the slot in the
  saved state was found by comparison.
- Program chunk: 2745 bytes with tag `gorp` (or `prog` in a bank and after a browser load) and a name at
  byte 16. The floats start at byte 48, one 4-byte slot per control (`am20::chunkSlot`). Up to 17 cables
  are stored at byte 384 as `jack, jack, colour, 0`, in either order. Two effect slots sit at 2048 and 2344,
  and a trailer at 2640. Banks use a `bolg` chunk with `prog` records.
- Jack numbers (0..34) were found by drawing 15 cables in two sessions with different partners. Input
  jacks always appear as the second byte when dragged from the output.
- Every factory `.fxp` (293) parses. `.fxp`, `.fxb` and `.am20` round trips are exact.

## Sound: what was measured and how AM-20 follows it

| Part | Measurement | In AM-20 |
| :--- | :--- | :--- |
| VCO 1 | harmonics of each wave at C2; pulse-width nulls; levels | tri: skewed (0.534), slightly rounded, a trace of saw; falling saw; pulse duty = 50 % x (1 - PW); noise through ~3.8 kHz |
| VCO 2 | the same; ring mod against VCO 1; 10 % pulse | ring = VCO1 rect x VCO2 rect + 0.28 of each; the two VCOs start in phase |
| Pitch | scales 32'..2', VCO2 +-12 st, fine +-1 st, 1 V/oct | the same; the top harmonics roll off more the higher the note (two poles at 13 kHz x (f/261 Hz)^-0.35) |
| Mixer | level of a saw at 20 knob positions | `mixLevel` table |
| HPF / LPF | self-oscillation pitch at 21 knob positions (12.8 Hz .. 21 kHz, no key tracking); noise through the filter for slopes and resonance peaks at 7 peak settings; self-oscillation level, onset (~7.2) and harmonics | Korg35 TPT filters, 2x oversampled, firm saturator in the loop, fitted feedback tables, pitch trim for strong resonance, weaker resonance near the top |
| EG 2 | attack / decay / release curves at 12 knob positions, sustain at 21, hold at 11 | RC attack toward 1.6, stops at 1.1; decay and release head 0.035 below target; the VCA opens from 0.1135 and is full at 1.0 |
| EG 1 | delay, attack, release through the pitch | plain RC curves; its normal path into the pitch is EG1 - 1 (pitch sits at the knob while the envelope is up) |
| EG 2 to the filters | self-oscillating filter tracked over a note at three sustain levels | the filters move by EG2 - sustain (the cutoff knob is where the sustain sits) |
| Mod depths | pitch: MG and EG1 knobs at 10 positions; filters: EG2 and MG | `pitchDepth` table (64.6 st per unit at 10); filters 1.37 x as many octaves |
| MG | frequency (linear, 0.1..19 Hz), wave morph saw-down / triangle / saw-up | the same |
| Portamento | glide time constant at 5 positions | `portaTau` table |
| Patch panel | each output into the LPF CUTOFF jack of a self-oscillating filter, read as voltage | KBD CV = (note - 40) / 20 units; trigger outputs high at rest, low while active (S-trigger); trigger inputs fire when pulled low; wheel -5..+5 V (CC 1 0..127, rests at -5 V); EG2 REV = -(EG2 - sustain); EXT SIGNAL IN adds to the mixer; INITIAL GAIN adds 1.22 x its input to the VCA; S&H with no clock cable tracks its input |
| External modulation | velocity at five values into Amp, LPF, HPF and VCO2 Pitch, amounts +-5 | sources swing -1..+1 around their middle (velocity) or 0..1 (wheel, breath, pressure); +-5 moves the filters over the whole knob range and VCO 2 by +-24 st; Amp uses velocity squared, gain 1 + a x (2v^2 - 1) |
| Voices | output level of ~160 factory programs | a unison stack scales by U^-0.42, the poly mode plays ~3 dB below mono |

The same 58 test scenarios (oscillators, filter drive, resonance, envelopes, modulation) were rendered
through the original and AM-20 and compared in 1/3-octave bands, level and envelope. Most land within about
0.5 dB. The largest remaining differences are in strongly driven resonant filters: the original makes a
little more distortion there, and more harmonics in self-oscillation. Over the ~160 factory programs the
median level difference is about 3 dB and the median envelope difference about 4 dB. Those programs use
cables, unison and external modulation heavily.

## Not measured, or only estimated

- The ESP's preamp gain, band-pass slopes, F-V converter and envelope follower. They are modelled from the
  hardware's specification (low cut 50 Hz..2.5 kHz, high cut 100 Hz..5 kHz, 24 dB/oct).
- The scaling of the KBD Track, Pressure, Pitch Bend, CC 16/17, MG and EG sources for external modulation
  (velocity and the wheel were measured).
- The momentary switch reads 0 V at every jack. It only acts on trigger inputs, where it fires while pressed.
- The "Analog" drift amount and the unison detune range (10 = +-50 cents) are estimates.
- The 20-minute limit of the demo was never reached; each measuring session took seconds.

## Checking against a licensed copy

Load an `.fxp` of the original in AM-20, play the same phrase in both, and compare. `Init` is the most direct
check (AM-20's Init in Mono assign with 1 voice equals the original's start-up state).
