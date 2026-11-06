#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tb;

TidebreezeAudioProcessor::TidebreezeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Tidebreeze", createLayout ([this] (int v) { return waveName (v); }))
{
    visitParams (P, [this] (const juce::String& id, auto& field)
    {
        auto* raw = apvts.getRawParameterValue (id);
        jassert (raw != nullptr);
        Binding bd { raw, nullptr, nullptr };
        if constexpr (std::is_same_v<std::remove_reference_t<decltype (field)>, bool>) bd.b = &field; else bd.i = &field;
        bindings.push_back (bd);
    });
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto& id = rp->getParameterID();
            if (id.endsWith ("hiWave") || id.endsWith ("loWave") || id.endsWith ("_type") || id == "mode")
                apvts.addParameterListener (id, this);
        }
    const char* slotIds[5] = { "ifx1", "ifx2", "ifx3", "mfx1", "mfx2" };
    for (int i = 0; i < 5; ++i) fxSlots[(size_t) i].id = slotIds[i];
    loadFactoryProgram (0);
}

TidebreezeAudioProcessor::~TidebreezeAudioProcessor()
{
    cancelPendingUpdate();
}

template <typename F>
void TidebreezeAudioProcessor::visitParams (Params& p, F&& f)
{
    f ("mode", p.mode); f ("voiceMode", p.voiceMode); f ("porta", p.porta); f ("bend", p.bend); f ("volume", p.volume);
    f ("legato", p.legato); f ("hold", p.hold);
    const char* peg[6] = { "pegStart", "pegAttack", "pegRelease", "pegAttackT", "pegDecayT", "pegReleaseT" };
    for (int k = 0; k < 6; ++k) f (peg[k], p.peg[k]);
    f ("rtCutoff", p.rtCutoff); f ("rtReso", p.rtReso); f ("rtEgInt", p.rtEgInt); f ("rtRelease", p.rtRelease);
    for (int o = 0; o < 2; ++o)
    {
        auto& q = p.osc[o];
        auto id = [o] (const char* n) { return oscId (o, n); };
        f (id ("hiWave"), q.hiWave); f (id ("loWave"), q.loWave); f (id ("velSw"), q.velSw);
        f (id ("hiRev"), q.hiRev); f (id ("hiOfs"), q.hiOfs); f (id ("hiLvl"), q.hiLvl);
        f (id ("loRev"), q.loRev); f (id ("loOfs"), q.loOfs); f (id ("loLvl"), q.loLvl);
        f (id ("octave"), q.octave); f (id ("transpose"), q.transpose); f (id ("tune"), q.tune); f (id ("delay"), q.delayMs); f (id ("pSlope"), q.pSlope);
        f (id ("velLo"), q.velLo); f (id ("velHi"), q.velHi);
        f (id ("pegInt"), q.pegInt); f (id ("l1Pitch"), q.l1Pitch); f (id ("l2Pitch"), q.l2Pitch);
        f (id ("fType"), q.fType); f (id ("fA"), q.fA); f (id ("fRes"), q.fRes); f (id ("fB"), q.fB); f (id ("fTrim"), q.fTrim);
        f (id ("fKt"), q.fKt); f (id ("fEgA"), q.fEgA); f (id ("fEgB"), q.fEgB); f (id ("fVelA"), q.fVelA);
        f (id ("fL1A"), q.fL1A); f (id ("fL2A"), q.fL2A);
        const char* feg[9] = { "fegStart", "fegAttack", "fegBreak", "fegSustain", "fegRelease", "fegAttackT", "fegDecayT", "fegSlopeT", "fegReleaseT" };
        for (int k = 0; k < 9; ++k) f (id (feg[k]), q.feg[k]);
        const char* aeg[8] = { "aegStart", "aegAttack", "aegBreak", "aegSustain", "aegAttackT", "aegDecayT", "aegSlopeT", "aegReleaseT" };
        for (int k = 0; k < 8; ++k) f (id (aeg[k]), q.aeg[k]);
        f (id ("aLvl"), q.aLvl); f (id ("aPan"), q.aPan); f (id ("aVel"), q.aVel); f (id ("aL1"), q.aL1); f (id ("aL2"), q.aL2);
        for (int l = 0; l < 2; ++l)
        {
            auto lid = [o, l] (const char* n) { return oscId (o, (juce::String ("l") + juce::String (l + 1) + n).toRawUTF8()); };
            auto& L = q.lfo[l];
            f (lid ("Wave"), L.wave); f (lid ("Freq"), L.freq); f (lid ("Sync"), L.sync); f (lid ("Div"), L.div);
            f (lid ("Key"), L.key); f (lid ("Fade"), L.fade); f (lid ("Delay"), L.delay); f (lid ("Ofs"), L.ofs);
        }
    }
    f ("arpOn", p.arpOn); f ("arpPat", p.arpPat); f ("arpRes", p.arpRes); f ("arpOct", p.arpOct); f ("arpGate", p.arpGate);
    f ("arpVel", p.arpVel); f ("arpSwing", p.arpSwing); f ("arpLatch", p.arpLatch); f ("arpTempo", p.arpTempo);
    for (int n = 0; n < 5; ++n)
    {
        const char* slot = n < 3 ? "ifx" : "mfx";
        const int k = n < 3 ? n : n - 3;
        auto& x = p.fx[n];
        f (fxId (slot, k, "type"), x.type); f (fxId (slot, k, "on"), x.on);
        for (int q = 0; q < 5; ++q) f (fxId (slot, k, ("p" + juce::String (q + 1)).toRawUTF8()), x.p[q]);
        if (n < 3) { f (fxId (slot, k, "mix"), x.mix); x.send = 0; x.ret = 0; }
        else { f (fxId (slot, k, "send"), x.send); f (fxId (slot, k, "ret"), x.ret); x.mix = 100; }
    }
    f ("eqLo", p.eqLo); f ("eqMid", p.eqMid); f ("eqMidHz", p.eqMidHz); f ("eqHi", p.eqHi);
}

void TidebreezeAudioProcessor::readParams()
{
    for (auto& bd : bindings)
    {
        const float v = bd.raw->load (std::memory_order_relaxed);
        if (bd.b != nullptr) *bd.b = v > 0.5f;
        else *bd.i = (int) std::lround (v);
    }
}

//==============================================================================
bool TidebreezeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void TidebreezeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    sendBuf.setSize (2, std::max (64, samplesPerBlock), false, true, false);
    mfxBuf.setSize (2, std::max (64, samplesPerBlock), false, true, false);
    for (auto& v : voices) v.active = false;
    {
        const juce::SpinLock::ScopedLockType sl (fxLock);
        for (auto& s : fxSlots) { s.type = -1; s.fx.reset(); }
    }
    for (int c = 0; c < 2; ++c) { eqLo[c].reset(); eqMid[c].reset(); eqHi[c].reset(); }
    syncEffects();
    ensureProgramWaves();
}

//==============================================================================
// waves
//==============================================================================
void TidebreezeAudioProcessor::ensureProgramWaves()
{
    // what the program plays (both oscillators: Double can be switched on any time),
    // read from the parameters themselves (P belongs to the audio thread); the
    // rest is dropped (and freed here, not on the audio thread, once no voice
    // holds it)
    std::vector<bool> used ((size_t) numWaveChoices(), false);
    for (int o = 0; o < 2; ++o)
        for (const char* n : { "hiWave", "loWave" })
            if (auto* raw = apvts.getRawParameterValue (oscId (o, n)))
            {
                const int w = (int) std::lround (raw->load());
                if (w >= 0 && w < numWaveChoices()) used[(size_t) w] = true;
            }
    bank.ensure (used);
}

//==============================================================================
// programs
//==============================================================================
void TidebreezeAudioProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    bank.clearAll();
}

ProgramTarget TidebreezeAudioProcessor::makeTarget()
{
    ProgramTarget t;
    t.set = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        else
            jassertfalse;
    };
    t.rollWave = [this] (int arch, juce::uint32 seed) { return rollWave (arch, seed); };
    return t;
}

int TidebreezeAudioProcessor::getNumPrograms() { return (int) factoryPrograms().size(); }

const juce::String TidebreezeAudioProcessor::getProgramName (int index)
{
    const auto& p = factoryPrograms();
    return index >= 0 && index < (int) p.size() ? juce::String (p[(size_t) index].name) : juce::String();
}

void TidebreezeAudioProcessor::setCurrentProgram (int index) { loadFactoryProgram (index); }

void TidebreezeAudioProcessor::loadFactoryProgram (int index)
{
    const auto& progs = factoryPrograms();
    index = juce::jlimit (0, (int) progs.size() - 1, index);
    {
        // one sync of waves and effects at the end, not one per parameter
        const juce::ScopedValueSetter<int> building (buildingProgram, buildingProgram + 1);
        resetToDefaults();
        auto t = makeTarget();
        progs[(size_t) index].build (t);
    }
    programName = progs[(size_t) index].name;
    programCategory = categoryNames()[progs[(size_t) index].category];
    currentProgram = index;
    ensureProgramWaves();
    syncEffects();
    if (onProgramChanged) onProgramChanged();
}

void TidebreezeAudioProcessor::rollProgram (int category, juce::uint32 seed)
{
    if (seed == 0) seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7fffffff) | 1u;
    {
        const juce::ScopedValueSetter<int> building (buildingProgram, buildingProgram + 1);
        resetToDefaults();
        auto t = makeTarget();
        if (category == kSuperRandom)
        {
            programName = superRandomProgram (t, seed);
            programCategory = "Super Random";
        }
        else
        {
            if (category < 0 || category >= NumCategories) category = (int) (seed % (juce::uint32) NumCategories);
            programName = randomProgram (t, category, seed);
            programCategory = categoryNames()[category];
        }
    }
    currentProgram = -1;
    ensureProgramWaves();
    syncEffects();
    if (onProgramChanged) onProgramChanged();
}

void TidebreezeAudioProcessor::initProgram()
{
    const auto& progs = factoryPrograms();
    for (int i = 0; i < (int) progs.size(); ++i)
        if (juce::String (progs[(size_t) i].name) == "Init Preset") { loadFactoryProgram (i); return; }
}

//==============================================================================
// effects: built on the message thread, swapped in under a lock
//==============================================================================
void TidebreezeAudioProcessor::syncEffects()
{
    for (auto& slot : fxSlots)
    {
        auto* p = apvts.getRawParameterValue (slot.id + "_type");
        const int type = p != nullptr ? (int) std::lround (p->load()) : 0;
        if (type == slot.type) continue;
        std::unique_ptr<Effect> fx = makeEffect (type);
        if (fx != nullptr) { fx->prepare (sr); fx->reset(); }
        {
            const juce::SpinLock::ScopedLockType sl (fxLock);
            slot.fx.swap (fx);
            slot.type = type;
        }
        // the old one goes here, outside the lock
    }
}

void TidebreezeAudioProcessor::parameterChanged (const juce::String&, float)
{
    if (buildingProgram > 0) return;       // a program being put together syncs once, at its end
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        ensureProgramWaves();
        syncEffects();
    }
    else
        triggerAsyncUpdate();
}

void TidebreezeAudioProcessor::handleAsyncUpdate()
{
    ensureProgramWaves();
    syncEffects();
}

//==============================================================================
// voices
//==============================================================================
static inline float ampCurve (float level01)
{
    const float l = juce::jlimit (0.0f, 1.0f, level01);
    return l * l;
}

void TidebreezeAudioProcessor::startVoice (Voice& v, int note, float velocity, bool legatoRetrigger)
{
    const bool wasActive = v.active;
    v.note = note; v.velocity = velocity; v.active = true; v.held = true; v.sustained = false;
    v.age = ++noteCounter;
    v.glideTarget = (float) note;
    if (! wasActive || P.porta == 0) v.glideNote = (float) note;
    if (legatoRetrigger && wasActive) return;     // legato: only the pitch moves

    v.peg.startL = (float) P.peg[0]; v.peg.attackL = (float) P.peg[1]; v.peg.releaseL = (float) P.peg[2];
    v.peg.breakL = v.peg.sustainL = 0.0f;
    v.peg.attackT = egTime ((float) P.peg[3]); v.peg.decayT = egTime ((float) P.peg[4]); v.peg.slopeT = 0.0f;
    v.peg.releaseT = egTime ((float) P.peg[5]);
    v.peg.trigger();

    const int vel127 = juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f));
    for (int o = 0; o < 2; ++o)
    {
        auto& ov = v.osc[o];
        const auto& q = P.osc[o];
        ov.active = false;
        if (o == 1 && P.mode != 1) continue;
        if (vel127 < q.velLo || vel127 > q.velHi) continue;
        const bool high = vel127 >= q.velSw;
        const int choice = high ? q.hiWave : q.loWave;
        WavePtr w = bank.tryWave (choice);
        if (w == nullptr) { wavesWanted = true; continue; }
        // the zone follows where the oscillator really plays, tune included
        const float played = 60.0f + ((float) note - 60.0f) * (float) q.pSlope * 0.1f
                           + 12.0f * (float) (q.octave - 2) + (float) q.transpose + (float) q.tune * 0.01f;
        ov.reader.start (w, played, high ? q.hiOfs : q.loOfs, high ? q.hiRev : q.loRev);
        if (ov.reader.done) continue;
        ov.active = true;

        const float waveLevel = (float) (high ? q.hiLvl : q.loLvl) / 127.0f;
        const float sens = (float) q.aVel / 99.0f;
        // velocity sensitivity in the level's own (dB) terms: 0 none, 40 about
        // -10 dB from velocity 100 down to 40, 99 about -24 dB
        const float velGain = std::pow (juce::jmax (0.01f, velocity), sens * 3.0f);
        ov.level = (float) q.aLvl / 127.0f * waveLevel * velGain;
        const float pan = juce::jlimit (-1.0f, 1.0f, (float) q.aPan / 63.0f);
        ov.panL = std::cos ((pan + 1.0f) * kPi * 0.25f) * 1.414f;
        ov.panR = std::sin ((pan + 1.0f) * kPi * 0.25f) * 1.414f;
        ov.filter.type = q.fType; ov.filter.reset();
        ov.gain = 0.0f; ov.gainTarget = 0.0f;
        ov.delaySamples = (int) (q.delayMs * 0.001 * sr);

        auto& f = ov.feg;
        f.startL = (float) q.feg[0]; f.attackL = (float) q.feg[1]; f.breakL = (float) q.feg[2]; f.sustainL = (float) q.feg[3]; f.releaseL = (float) q.feg[4];
        f.attackT = egTime ((float) q.feg[5]); f.decayT = egTime ((float) q.feg[6]); f.slopeT = egTime ((float) q.feg[7]); f.releaseT = egTime ((float) q.feg[8]);
        auto& a = ov.aeg;
        a.startL = (float) q.aeg[0]; a.attackL = (float) q.aeg[1]; a.breakL = (float) q.aeg[2]; a.sustainL = (float) q.aeg[3]; a.releaseL = 0.0f;
        a.attackT = egTime ((float) q.aeg[4]); a.decayT = egTime ((float) q.aeg[5]); a.slopeT = egTime ((float) q.aeg[6]);
        a.releaseT = egTime (juce::jlimit (0.0f, 99.0f, (float) q.aeg[7] + (float) P.rtRelease * 0.6f));
        f.stage = Envelope::Idle; a.stage = Envelope::Idle; f.value = f.startL; a.value = a.startL;
        if (ov.delaySamples == 0) { f.trigger(); a.trigger(); }

        for (int l = 0; l < 2; ++l)
        {
            auto& L = ov.lfo[l];
            const auto& lp = q.lfo[l];
            L.shape = lp.wave;
            L.hz = lp.sync ? (float) (tempo.load() / 60.0) / divBeats (lp.div) : lfoHz ((float) lp.freq);
            L.keySync = lp.key;
            L.fadeSec = egTime ((float) lp.fade);
            L.delaySec = egTime ((float) lp.delay);
            L.offset = (float) lp.ofs / 99.0f;
            L.seed = (juce::uint32) (v.age * 2654435761u + (juce::uint32) (o * 2 + l) * 40503u);
            L.trigger (lp.key ? 0.0f : freePhase[o][l]);
        }
    }
}

void TidebreezeAudioProcessor::releaseVoice (Voice& v)
{
    v.held = false;
    if (P.hold) return;
    if (sustainPedal) { v.sustained = true; return; }
    v.peg.release();
    for (auto& ov : v.osc)
    {
        if (! ov.active) continue;
        if (ov.delaySamples > 0) { ov.active = false; continue; }   // released before it started
        ov.feg.release(); ov.aeg.release();
    }
}

void TidebreezeAudioProcessor::handleNoteOn (int note, float velocity)
{
    if (P.voiceMode == 1)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);
        auto& v = voices[0];
        const bool legato = P.legato && v.active && v.held;
        startVoice (v, note, velocity, legato);
        return;
    }
    Voice* target = nullptr;
    for (auto& v : voices) if (v.active && v.note == note && ! v.held) { target = &v; break; }   // same key again
    if (target == nullptr) for (auto& v : voices) if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        // steal: the oldest released voice, else the oldest
        juce::uint64 best = std::numeric_limits<juce::uint64>::max();
        for (auto& v : voices) if (! v.held && v.age < best) { best = v.age; target = &v; }
        if (target == nullptr) for (auto& v : voices) if (v.age < best) { best = v.age; target = &v; }
    }
    if (target != nullptr) startVoice (*target, note, velocity, false);
}

void TidebreezeAudioProcessor::handleNoteOff (int note)
{
    if (P.voiceMode == 1)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        auto& v = voices[0];
        if (! v.active || v.note != note) return;
        if (! heldNotes.empty())
        {
            const int back = heldNotes.back();
            startVoice (v, back, v.velocity, P.legato);
            return;
        }
        releaseVoice (v);
        return;
    }
    for (auto& v : voices) if (v.active && v.held && v.note == note) releaseVoice (v);
}

void TidebreezeAudioProcessor::renderVoices (float* L, float* R, int n)
{
    if (n <= 0) return;
    constexpr int kCtl = 16;
    const float fsr = (float) sr;
    const float egScale = 1.0f + (float) P.rtEgInt / 63.0f;
    const float glideK = P.porta > 0 ? 1.0f - std::exp (-1.0f / (egTime ((float) P.porta) * 0.35f * fsr) * (float) kCtl) : 1.0f;
    const int oscs = P.mode == 1 ? 2 : 1;

    for (auto& v : voices)
    {
        if (! v.active) continue;
        bool anySounding = false;
        for (int start = 0; start < n; start += kCtl)
        {
            const int len = std::min (kCtl, n - start);
            const float dt = (float) len / fsr;
            v.glideNote += (v.glideTarget - v.glideNote) * glideK;
            const float peg = v.peg.advance (dt);
            for (int o = 0; o < oscs; ++o)
            {
                auto& ov = v.osc[o];
                if (! ov.active) continue;
                int s0 = start;
                if (ov.delaySamples > 0)
                {
                    // (">": a delay ending right on a block boundary still starts the layer in the next one)
                    if (ov.delaySamples > len) { ov.delaySamples -= len; anySounding = true; continue; }
                    s0 += ov.delaySamples; ov.delaySamples = 0;
                    ov.feg.trigger(); ov.aeg.trigger();
                    if (! v.held && ! v.sustained && ! P.hold) { ov.feg.release(); ov.aeg.release(); }
                }
                const auto& q = P.osc[o];
                const float l1 = ov.lfo[0].advance (dt), l2 = ov.lfo[1].advance (dt);
                const float feg = ov.feg.advance (dt), aeg = ov.aeg.advance (dt);

                const float semis = 60.0f + (v.glideNote - 60.0f) * (float) q.pSlope * 0.1f
                                  + 12.0f * (float) (q.octave - 2) + (float) q.transpose + (float) q.tune * 0.01f
                                  + bendSemis + (float) q.pegInt * peg + 0.1f * ((float) q.l1Pitch * l1 + (float) q.l2Pitch * l2);
                ov.inc = (double) midiToHz (semis) / (double) ov.reader.rootHz() * ov.reader.rate() / sr;

                const float keyUnits = (float) q.fKt / 99.0f * 0.8777f * ((float) v.note - 60.0f);
                const float cA = (float) q.fA + (float) P.rtCutoff * 0.8f + keyUnits + (float) q.fEgA * egScale * feg
                               + (float) q.fVelA * v.velocity * 0.6f + 0.5f * ((float) q.fL1A * l1 + (float) q.fL2A * l2);
                const float cB = (float) q.fB + (float) q.fEgB * egScale * feg + keyUnits;
                const float reso = juce::jlimit (0.0f, 1.0f, ((float) q.fRes + (float) P.rtReso * 0.8f) / 99.0f);
                ov.filter.set (cutoffHz (juce::jlimit (0.0f, 104.0f, cA)), cutoffHz (juce::jlimit (0.0f, 104.0f, cB)), reso, sr);

                const float trem = 1.0f + 0.5f * ((float) q.aL1 * l1 + (float) q.aL2 * l2) / 99.0f;
                ov.gainTarget = ov.level * ampCurve (aeg) * juce::jmax (0.0f, trem);
                const float trim = dbToGain (((float) q.fTrim - 99.0f) * 0.3f + 3.0f);

                const int cnt = start + len - s0;
                const float g0 = ov.gain, dg = (ov.gainTarget - ov.gain) / (float) std::max (1, cnt);
                float g = g0;
                for (int i = s0; i < start + len; ++i)
                {
                    const float x = ov.reader.next (ov.inc) * trim;
                    const float y = ov.filter.process (x) * g;
                    L[i] += y * ov.panL; R[i] += y * ov.panR;
                    g += dg;
                }
                ov.gain = ov.gainTarget;
                if (ov.reader.done || (ov.aeg.finished() && ov.gain < 1.0e-5f)) ov.active = false;
                else anySounding = true;
            }
            if (! anySounding) break;
        }
        if (! anySounding) v.active = false;
    }
}

//==============================================================================
void TidebreezeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (buffer.getNumChannels() < 2) return;
    // an offline render waits for the program's waves (a live one plays without them meanwhile)
    if (isNonRealtime() && wavesPending()) waitForWaves (20000);
    readParams();

    double bpm = (double) P.arpTempo;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    tempo = bpm;
    for (int o = 0; o < 2; ++o)
        for (int l = 0; l < 2; ++l)
        {
            const auto& lp = P.osc[o].lfo[l];
            const float hz = lp.sync ? (float) (bpm / 60.0) / divBeats (lp.div) : lfoHz ((float) lp.freq);
            freePhase[o][l] += hz * (float) n / (float) sr;
            freePhase[o][l] -= std::floor (freePhase[o][l]);
        }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    juce::MidiBuffer* events = &midi;
    if (P.arpOn)
    {
        arpOut.clear();
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn()) arp.noteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) arp.noteOff (m.getNoteNumber());
            else arpOut.addEvent (m, meta.samplePosition);
        }
        Arp::Settings s { P.arpPat, P.arpRes, P.arpOct, P.arpGate, P.arpVel, P.arpSwing, P.arpLatch };
        arp.process (s, bpm, sr, n, arpOut);
        events = &arpOut;
        arpWasOn = true;
    }
    else if (arpWasOn)
    {
        arpOut.clear(); arp.allOff (arpOut, 0);
        for (const auto meta : arpOut) handleNoteOff (meta.getMessage().getNoteNumber());
        arpWasOn = false;
    }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto meta : *events)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { renderVoices (L + pos, R + pos, at - pos); pos = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) handleNoteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) handleNoteOff (m.getNoteNumber());
        else if (m.isPitchWheel()) bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * (float) P.bend;
        else if (m.isSustainPedalOn()) sustainPedal = true;
        else if (m.isSustainPedalOff())
        {
            sustainPedal = false;
            for (auto& v : voices) if (v.active && v.sustained) { v.sustained = false; releaseVoice (v); }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : voices) if (v.active) releaseVoice (v);
    }
    if (pos < n) renderVoices (L + pos, R + pos, n - pos);

    if (wavesWanted.exchange (false)) triggerAsyncUpdate();

    // ---- effects -------------------------------------------------------------
    const juce::SpinLock::ScopedTryLockType fl (fxLock);
    if (fl.isLocked())
    {
        if (mfxBuf.getNumSamples() < n) return;   // prepareToPlay sizes these
        for (int s = 0; s < 3; ++s)
        {
            auto& slot = fxSlots[(size_t) s];
            const auto& fp = P.fx[s];
            if (slot.fx == nullptr || slot.type != fp.type || ! fp.on || fp.type == FxNone) continue;
            float pr[5]; for (int k = 0; k < 5; ++k) pr[k] = (float) fp.p[k] * 0.01f;
            slot.fx->set (pr, bpm);
            mfxBuf.copyFrom (0, 0, buffer, 0, 0, n); mfxBuf.copyFrom (1, 0, buffer, 1, 0, n);
            slot.fx->process (L, R, n);
            const float wet = (float) fp.mix * 0.01f, dry = 1.0f - wet;
            const float* dl = mfxBuf.getReadPointer (0); const float* dr = mfxBuf.getReadPointer (1);
            for (int i = 0; i < n; ++i) { L[i] = dl[i] * dry + L[i] * wet; R[i] = dr[i] * dry + R[i] * wet; }
        }
        sendBuf.copyFrom (0, 0, buffer, 0, 0, n); sendBuf.copyFrom (1, 0, buffer, 1, 0, n);
        for (int s = 3; s < 5; ++s)
        {
            auto& slot = fxSlots[(size_t) s];
            const auto& fp = P.fx[s];
            if (slot.fx == nullptr || slot.type != fp.type || ! fp.on || fp.type == FxNone || fp.send == 0) continue;
            float pr[5]; for (int k = 0; k < 5; ++k) pr[k] = (float) fp.p[k] * 0.01f;
            slot.fx->set (pr, bpm);
            const float send = (float) fp.send / 127.0f;
            for (int c = 0; c < 2; ++c) { mfxBuf.copyFrom (c, 0, sendBuf, c, 0, n); mfxBuf.applyGain (c, 0, n, send); }
            slot.fx->process (mfxBuf.getWritePointer (0), mfxBuf.getWritePointer (1), n);
            // reverbs return their wet signal; the others their full output minus the dry
            const float ret = (float) fp.ret / 127.0f;
            const bool wetOnly = returnsWetOnly (fp.type);
            const float* wl = mfxBuf.getReadPointer (0); const float* wr = mfxBuf.getReadPointer (1);
            const float* sl = sendBuf.getReadPointer (0); const float* sr2 = sendBuf.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                L[i] += ret * (wetOnly ? wl[i] : wl[i] - 0.5f * sl[i] * send);
                R[i] += ret * (wetOnly ? wr[i] : wr[i] - 0.5f * sr2[i] * send);
            }
        }
    }

    // ---- master EQ and volume ----------------------------------------------------
    const float gLo = dbToGain ((float) P.eqLo), gMid = dbToGain ((float) P.eqMid), gHi = dbToGain ((float) P.eqHi);
    const bool eq = P.eqLo != 0 || P.eqMid != 0 || P.eqHi != 0;
    const float vol = std::pow ((float) P.volume / 127.0f, 2.0f) * 1.2f;
    float peak = 0.0f;
    float* ch[2] = { L, R };
    for (int c = 0; c < 2; ++c)
    {
        if (eq) { eqLo[c].set (150.0f, 0.6f, sr); eqMid[c].set ((float) P.eqMidHz, 0.8f, sr); eqHi[c].set (6000.0f, 0.6f, sr); }
        for (int i = 0; i < n; ++i)
        {
            float x = ch[c][i];
            if (eq)
            {
                float b, h;
                const float lo = eqLo[c].process (x, b, h); x += (gLo - 1.0f) * lo;
                eqMid[c].process (x, b, h); x += (gMid - 1.0f) * b;
                eqHi[c].process (x, b, h); x += (gHi - 1.0f) * h;
            }
            x *= vol;
            // a soft knee above -2 dBFS keeps big chords from clipping hard
            const float ax = std::abs (x);
            if (ax > 0.8f) x = std::copysign (0.8f + 0.2f * std::tanh ((ax - 0.8f) * 5.0f), x);
            ch[c][i] = x;
            peak = std::max (peak, std::abs (x));
        }
    }
    meter = std::max (peak, meter.load() * 0.9f);
}

//==============================================================================
// state
//==============================================================================
void TidebreezeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    xml->setAttribute ("programName", programName);
    xml->setAttribute ("programCategory", programCategory);
    xml->setAttribute ("currentProgram", currentProgram);
    bank.writeXml (*xml);   // the user waves: rolled ones as their recipe, loaded ones as their audio
    copyXmlToBinary (*xml, destData);
}

void TidebreezeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    bank.readXml (xml->getChildByName ("UserWaves"));
    programName = xml->getStringAttribute ("programName", "Preset");
    programCategory = xml->getStringAttribute ("programCategory", "Pads");
    {
        // the first version's category names
        static const char* const was[][2] = { { "Keyboard", "Keys" }, { "Organ", "Organs" }, { "Bell/Mallet", "Bells" }, { "Vocal/Airy", "Voices & Air" },
                                              { "Slow Synth", "Pads" }, { "Fast Synth", "Plucks & Stabs" }, { "Lead Synth", "Leads" },
                                              { "Motion Synth", "Currents" }, { "Bass", "Basses" }, { "Water/SE", "Sea & Shore" }, { "Hit/Arpg", "Arps & Ripples" } };
        for (auto& w : was) if (programCategory == w[0]) programCategory = w[1];
    }
    currentProgram = xml->getIntAttribute ("currentProgram", -1);
    xml->removeChildElement (xml->getChildByName ("UserWaves"), true);
    {
        const juce::ScopedValueSetter<int> building (buildingProgram, buildingProgram + 1);
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
    ensureProgramWaves();
    syncEffects();
    if (onProgramChanged) onProgramChanged();
}

juce::AudioProcessorEditor* TidebreezeAudioProcessor::createEditor()
{
    return new TidebreezeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TidebreezeAudioProcessor();
}
