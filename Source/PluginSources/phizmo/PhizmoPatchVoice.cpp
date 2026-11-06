/*
    PhizmoPatchVoice.cpp  (Aquanode Modular)
    ---------------------
    Phizmo's voice taken apart for the patch version, where a Sound is several modules:
    Phizmo - Voice (this control: envelopes, evolution scans, LFO, noise, pitch, cutoff),
    Phizmo - Osc (the wavetable reads below), Phizmo - Filter and Phizmo - Ring.

    patchControl() is synthesiseVoice() without the audio: the same steps in the same order,
    up to the oscillators' frequencies and frame positions, the envelope levels and the
    filters' cutoffs, which it hands out as VoiceTaps instead of reading the tables itself.
    The card keeps synthesiseVoice() unchanged.
*/

//==============================================================================
void PhizmoAudioProcessor::patchBeginBlock(int numSamples)
{
    // the block-level part of renderVoiceBuses(): LFOs, bend, coefficients, edit buffer
    const float pitchLFORate = pPitchLFORate->load();
    const float pitchLFODep = pPitchLFO->load();
    const float posLFORate = pPosLFORate->load();
    const int N = numSamples;

    pitchLFOPhase += pitchLFORate * (N * 0.5) / currentSampleRate;
    if (pitchLFOPhase > 1.0) pitchLFOPhase -= 1.0;
    double posLFOPhaseBlock = 0.0;
    posLFOPhaseBlock += posLFORate * (N * 0.5) / currentSampleRate;
    if (posLFOPhaseBlock > 1.0) posLFOPhaseBlock -= 1.0;
    blockPosLFO = (float)std::sin(posLFOPhaseBlock * juce::MathConstants<double>::twoPi);
    blockPitchLFO = (float)std::sin(pitchLFOPhase * juce::MathConstants<double>::twoPi);
    const double bendSemis = (double)bendNorm.load() * (pBendRange ? (double)pBendRange->load() : 2.0);
    globalLfoPhase += 5.2 / currentSampleRate * numSamples;
    globalLfoPhase -= std::floor(globalLfoPhase);
    globalLfo = (float)std::sin(globalLfoPhase * juce::MathConstants<double>::twoPi);
    const double vib = (double)globalLfo
                     * ((double)modWheel.load() + (double)chanPressure.load() * 0.5) * 0.5;
    patchPitchMult = std::pow(2.0, ((double)(blockPitchLFO * pitchLFODep) + bendSemis + vib) / 12.0);

    if (!soundSwapInProgress.load() && !presetLoading.load())
        captureEditBuffer(editSound);
    refreshLayers();

    const float isr = (float)(1.0 / currentSampleRate);
    auto makeDecCoeff = [&](float time) { return std::exp(-isr * 5.f / juce::jmax(0.001f, time)); };
    for (int i = 0; i < NUM_SOUNDS; ++i)
    {
        const SoundParams& sp = soundParams[i];
        BlockEnvCoeffs& b = blkS[i];
        b.decCoeff  = makeDecCoeff(sp.decay);
        b.relCoeff  = makeDecCoeff(sp.release);
        b.dec2Coeff = makeDecCoeff(sp.decay2);
        b.rel2Coeff = makeDecCoeff(sp.release2);
        b.fdecCoeff = makeDecCoeff(sp.filterDec);
        b.frelCoeff = makeDecCoeff(sp.filterRel);
        b.penvDecCoeff = makeDecCoeff(sp.pitchEnvDec);
        b.twDecCoeff   = makeDecCoeff(sp.twDec);
        b.twRelCoeff   = makeDecCoeff(sp.twRel);
        b.dm = std::pow(2.0, (double)sp.detune / 1200.0);
        const float sc = sp.spread;
        b.sA = std::pow(2.0, -(double)sc / 2400.0);
        b.sB = std::pow(2.0, (double)sc / 2400.0);
        b.octA = std::pow(2.0, std::round((double)sp.octaveA));
        b.octB = std::pow(2.0, std::round((double)sp.octaveB));
        b.tuneMA = std::pow(2.0, ((double)sp.tuneA + (double)sp.fineA * 0.01) / 12.0);
        b.tuneMB = std::pow(2.0, ((double)sp.tuneB + (double)sp.fineB * 0.01) / 12.0);
    }

    // FIX mode's perpetual master scans: a block at a time
    for (int s = 0; s < N; ++s)
        for (int snd = 0; snd < NUM_SOUNDS; ++snd)
        {
            const SoundParams& sp = soundParams[snd];
            for (int o = 0; o < 2; ++o)
            {
                if ((int)std::round(o == 0 ? sp.evoMode : sp.evoModeB) != 3)
                    continue;
                double& ph = evoFixPhase[snd][o];
                int& dir = evoFixDir[snd][o];
                const double inc = 1.0 / ((double)juce::jmax(0.1f, o == 0 ? sp.evoTime : sp.evoTimeB) * currentSampleRate);
                switch ((ScanMode)juce::jlimit(0, 4, (int)(o == 0 ? sp.scanStyle : sp.scanStyleB)))
                {
                case ScanMode::BackForth:
                    ph += inc * dir;
                    if      (ph >= 1.0) { ph = 1.0; dir = -1; }
                    else if (ph <= 0.0) { ph = 0.0; dir =  1; }
                    break;
                case ScanMode::Backward:
                case ScanMode::BwdStay:
                    ph -= inc; if (ph <= 0.0) ph += 1.0;
                    break;
                default:
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    break;
                }
            }
        }
}

void PhizmoAudioProcessor::patchNoteOn(int vi, int sound, int note, float vel01)
{
    if (vi < 0 || vi >= MAX_VOICES) return;
    sound = juce::jlimit(0, NUM_SOUNDS - 1, sound);
    const float velAmt = (pVelToFrame && pVelToFrame->load() > 0.5f) ? 1.0f : 0.f;
    const bool carry = (pEvoPhaseCarry && pEvoPhaseCarry->load() > 0.5f)
                    && !(pEvoRestart && pEvoRestart->load() > 0.5f);
    float carryPh = 0.f;
    if (carry)
        for (int i = 0; i < MAX_VOICES; ++i)
            if (i != vi && voices[i].active && voices[i].soundIndex == sound)
            { carryPh = (float)voices[i].curvePhase; break; }

    voices[vi].soundIndex = sound;
    voices[vi].noteOn(note, applyVelocityCurve(vel01), 0.0, 0.f, velAmt, carry, carryPh, false);
    applyEvoStart(vi, sound);
    voiceEvoLFOPhase[vi] = (double)(note & 0xF) / 16.0;
    voicePosLFOPhase[vi] = (double)((note * 7) & 0xF) / 16.0;
}

void PhizmoAudioProcessor::patchNoteOff(int vi)
{
    if (vi < 0 || vi >= MAX_VOICES) return;
    if (pEnvMode && (int)pEnvMode->load() == 1) return;   // "Finish": runs through its stages
    if (voices[vi].active && voices[vi].envStage != PhizmoVoice::Env::Release)
        voices[vi].noteOff();
}

void PhizmoAudioProcessor::patchVoiceReset(int vi)
{
    if (vi >= 0 && vi < MAX_VOICES)
        voices[vi].active = false;
}

//==============================================================================
void PhizmoAudioProcessor::patchControl(int vi, VoiceTaps& out)
{
    out = VoiceTaps();
    if (vi < 0 || vi >= MAX_VOICES) return;
    PhizmoVoice& v = voices[vi];
    if (!v.active) return;
    const int sIdx = juce::jlimit(0, NUM_SOUNDS - 1, v.soundIndex);
    const SoundParams& SP = soundParams[sIdx];
    const BlockEnvCoeffs& bc = blkS[sIdx];
    const double pitchMult = patchPitchMult;

    float isr = (float)(1.0 / currentSampleRate);

    // --- the envelopes, as in synthesiseVoice() ---
    float att = juce::jmax(0.001f, SP.attack);
    float sus = SP.sustain;
    PhizmoVoice::Env nextAmpStage = v.envStage;
    float nextAmpRelStart = v.releaseStartLevel;
    v.envLevel = advanceEnvExp(v.envLevel, sus, v.envStage, att, sus,
        bc.decCoeff, bc.relCoeff, isr, nextAmpStage, nextAmpRelStart,
        pEnvMode ? (int)pEnvMode->load() : 0);
    v.envStage = nextAmpStage;

    float att2 = juce::jmax(0.001f, SP.attack2);
    float sus2 = SP.sustain2;
    PhizmoVoice::Env nextEnv2Stage = v.env2Stage;
    float nextEnv2RelStart = v.rel2StartLevel;
    v.env2Level = advanceEnvExp(v.env2Level, sus2, v.env2Stage, att2, sus2,
        bc.dec2Coeff, bc.rel2Coeff, isr, nextEnv2Stage, nextEnv2RelStart);
    v.env2Stage = nextEnv2Stage;

    if (v.envStage == PhizmoVoice::Env::Idle && v.env2Stage == PhizmoVoice::Env::Idle)
    {
        v.active = false;
        return;
    }

    float fatt = juce::jmax(0.001f, SP.filterAtt);
    float fsus = SP.filterSus;
    PhizmoVoice::Env nextFEnvStage = v.fenvStage;
    float nextFRelStart = v.fenvReleaseStart;
    v.fenvLevel = advanceEnvExp(v.fenvLevel, fsus, v.fenvStage, fatt, fsus,
        bc.fdecCoeff, bc.frelCoeff, isr, nextFEnvStage, nextFRelStart);
    v.fenvStage = nextFEnvStage;

    float penvAmt = SP.pitchEnvAmt;
    if (!v.penvDone)
    {
        v.penvLevel *= bc.penvDecCoeff;
        if (v.penvLevel < 1e-4f) { v.penvLevel = 0.f; v.penvDone = true; }
    }
    double pitchEnvMult = v.penvMemo((double)(v.penvLevel * penvAmt) / 12.0, [](double x) { return std::pow(2.0, x); });

    float phizOval  = pFizO ? pFizO->load() : 0.f;
    int   phizODest = pFizODest ? (int)pFizODest->load() : 0;

    const float sysBpm = pTempo ? pTempo->load() : 120.f;
    int   lfoShape = pLfoShape ? (int)SP.lfoShape : 2;
    float lfoSpeed = syncedRate((int)SP.lfoSync, SP.lfoSpeed, sysBpm);
    float noiseHz  = syncedRate((int)SP.noiseSync, SP.noiseRate, sysBpm);

    voiceEvoLFOPhase[vi] += SP.evoLFORate / currentSampleRate;
    if (voiceEvoLFOPhase[vi] > 1.0) voiceEvoLFOPhase[vi] -= 1.0;
    float evoLFO = lfoValue((float)voiceEvoLFOPhase[vi], lfoShape);

    v.noisePhase += noiseHz / currentSampleRate;
    if (v.noisePhase >= 1.0)
    {
        v.noisePhase -= 1.0;
        v.sahNoise = fastRand() * 2.f - 1.f;
    }
    {
        const float sm = juce::jlimit(0.0002f, 0.2f, (float)(noiseHz * 6.0 / currentSampleRate));
        v.lpfNoise += sm * ((fastRand() * 2.f - 1.f) - v.lpfNoise);
    }

    voicePosLFOPhase[vi] += lfoSpeed / currentSampleRate;
    if (voicePosLFOPhase[vi] > 1.0) voicePosLFOPhase[vi] -= 1.0;
    float vPosLFO = lfoValue((float)voicePosLFOPhase[vi], lfoShape);

    // --- the evolution scans ---
    float evoTime = juce::jmax(0.1f, SP.evoTime);
    if (phizODest == 1) evoTime = juce::jmax(0.1f, evoTime * std::pow(2.f, -phizOval * 2.f));
    float evoTimeB = juce::jmax(0.1f, SP.evoTimeB);
    double phaseInc = 1.0 / (evoTime * currentSampleRate);
    double phaseIncB = 1.0 / (evoTimeB * currentSampleRate);
    auto scanMode = (ScanMode)(int)juce::jlimit(0.f, 4.f, SP.scanStyle);

    if (!v.curveFinished)
    {
        switch (scanMode)
        {
        case ScanMode::Forward:
            v.curvePhase += phaseInc;
            if (v.curvePhase >= 1.0) { v.curvePhase -= 1.0; v.curveWrapA = true; }
            break;
        case ScanMode::FwdStay:
            v.curvePhase += phaseInc;
            if (v.curvePhase >= 1.0) { v.curvePhase = 1.0; v.curveFinished = true; }
            break;
        case ScanMode::BackForth:
            v.curvePhase += phaseInc * v.scanDir;
            if (v.curvePhase >= 1.0) { v.curvePhase = 1.0; v.scanDir = -1; }
            else if (v.curvePhase <= 0.0) { v.curvePhase = 0.0; v.scanDir = 1; }
            break;
        case ScanMode::BwdStay:
            v.curvePhase -= phaseInc;
            if (v.curvePhase <= 0.0) { v.curvePhase = 0.0; v.curveFinished = true; }
            break;
        case ScanMode::Backward:
            v.curvePhase -= phaseInc;
            if (v.curvePhase <= 0.0) { v.curvePhase += 1.0; v.curveWrapA = true; }
            break;
        }
    }

    float bPhaseOff = SP.evoBPhaseOff;
    const auto scanModeB = (ScanMode)(int)juce::jlimit(0.f, 4.f, SP.scanStyleB);
    if (!v.curveFinishedB)
    {
        switch (scanModeB)
        {
        case ScanMode::Forward:
            v.curvePhaseB += phaseIncB;
            if (v.curvePhaseB >= 1.0) { v.curvePhaseB -= 1.0; v.curveWrapB = true; }
            break;
        case ScanMode::FwdStay:
            v.curvePhaseB += phaseIncB;
            if (v.curvePhaseB >= 1.0) { v.curvePhaseB = 1.0; v.curveFinishedB = true; }
            break;
        case ScanMode::BackForth:
            v.curvePhaseB += phaseIncB * v.scanDirB;
            if (v.curvePhaseB >= 1.0) { v.curvePhaseB = 1.0; v.scanDirB = -1; }
            else if (v.curvePhaseB <= 0.0) { v.curvePhaseB = 0.0; v.scanDirB = 1; }
            break;
        case ScanMode::BwdStay:
            v.curvePhaseB -= phaseIncB;
            if (v.curvePhaseB <= 0.0) { v.curvePhaseB = 0.0; v.curveFinishedB = true; }
            break;
        default:
            v.curvePhaseB -= phaseIncB;
            if (v.curvePhaseB <= 0.0) { v.curvePhaseB += 1.0; v.curveWrapB = true; }
            break;
        }
    }
    double curvePhaseBraw = v.curvePhaseB + (double)bPhaseOff;
    curvePhaseBraw = std::fmod(curvePhaseBraw, 1.0);
    if (curvePhaseBraw < 0.0) curvePhaseBraw += 1.0;

    const int evoModeA = (int)std::round(SP.evoMode);
    const int evoModeB = (int)std::round(SP.evoModeB);
    if (evoModeA == 3)
    {
        const double m = evoFixPhase[v.soundIndex][0];
        if (std::abs(m - v.curvePhase) > 0.5) v.curveWrapA = true;
        v.curvePhase = m;
    }
    if (evoModeB == 3)
    {
        const double m = evoFixPhase[v.soundIndex][1];
        if (std::abs(m - v.curvePhaseB) > 0.5) v.curveWrapB = true;
        v.curvePhaseB = m;
        curvePhaseBraw = m + (double)bPhaseOff;
        curvePhaseBraw = std::fmod(curvePhaseBraw, 1.0);
        if (curvePhaseBraw < 0.0) curvePhaseBraw += 1.0;
    }

    float keytrackAmt = SP.keytrack;
    float keytrackMod = ((float)(v.midiNote - 60) / 60.f) * keytrackAmt * 0.5f;

    float twSus = SP.twSus;
    PhizmoVoice::Env nextTwStage = v.twenvStage;
    float twRelStartUnused = v.twenvRelStart;
    v.twenvLevel = advanceEnvExp(v.twenvLevel, twSus, v.twenvStage,
        juce::jmax(0.001f, SP.twAtt), twSus, bc.twDecCoeff, bc.twRelCoeff,
        isr, nextTwStage, twRelStartUnused);
    v.twenvStage = nextTwStage;

    float twDepth = SP.twAmt;
    float twVel = SP.twVelAmt;
    twDepth *= (1.f - twVel) + v.velocity * twVel;
    float twMod = v.twenvLevel * twDepth;

    double curvePhaseAraw = (double)v.curvePhase + (double)SP.evoPhaseOff;
    curvePhaseAraw = std::fmod(curvePhaseAraw, 1.0);
    if (curvePhaseAraw < 0.0) curvePhaseAraw += 1.0;
    float curveTargetA = evalCurveS((float)curvePhaseAraw, 0, SP);
    float curveTargetB = evalCurveS((float)curvePhaseBraw, 1, SP);
    const bool stepA = SP.evoStepped  > 0.5f;
    const bool stepB = SP.evoSteppedB > 0.5f;
    auto slewCoeff = [sr = (float)currentSampleRate](float ms)
    {
        const float tau = ms * 0.001f;
        return (tau < 1.0e-5f) ? 1.f : 1.f - std::exp(-1.f / (tau * sr));
    };
    const float slewA = slewCoeff(SP.evoSlew);
    const float slewB = slewCoeff(SP.evoSlewB);
    if (stepA || v.curveWrapA)
    {
        v.curveSmoothA += slewA * (curveTargetA - v.curveSmoothA);
        if (v.curveWrapA && std::abs(curveTargetA - v.curveSmoothA) < 1.0e-4f) v.curveWrapA = false;
    }
    else { v.curveSmoothA = curveTargetA; }
    if (stepB || v.curveWrapB)
    {
        v.curveSmoothB += slewB * (curveTargetB - v.curveSmoothB);
        if (v.curveWrapB && std::abs(curveTargetB - v.curveSmoothB) < 1.0e-4f) v.curveWrapB = false;
    }
    else { v.curveSmoothB = curveTargetB; }
    float curveOutA = v.curveSmoothA;
    float curveOutB = v.curveSmoothB;

    float evoLFOAmt = evoLFO * SP.evoLFODepth * 0.5f;
    float phizIWave  = pFizI ? pFizI->load() * 0.5f : 0.f;
    float wStartA = (SP.waveStartA) + phizIWave;
    float wStartB = (SP.waveStartB) + phizIWave;
    float wModA   = SP.waveModA * modSource((int)SP.waveModSrcA, v, vPosLFO) * 0.5f;
    float wModB   = SP.waveModB * modSource((int)SP.waveModSrcB, v, vPosLFO) * 0.5f;

    float rawEvoPosA = juce::jlimit(0.f, 1.f, curveOutA + twMod + evoLFOAmt + keytrackMod + wStartA + wModA);
    float rawEvoPosB = juce::jlimit(0.f, 1.f, curveOutB + twMod + evoLFOAmt + keytrackMod + wStartB + wModB);

    float snapAmt = SP.frameSnap;
    if (snapAmt > 0.001f)
    {
        float steps = std::pow(2.f, (1.f - snapAmt) * 6.f + 1.f);
        rawEvoPosA = std::round(rawEvoPosA * steps) / steps;
        rawEvoPosB = std::round(rawEvoPosB * steps) / steps;
    }

    float velFilterMod = v.velocity;

    // --- pitch ---
    float glideTime = SP.glide;
    double baseMF;
    if (v.glideProgress < 1.0f && glideTime > 0.001f)
    {
        v.glideProgress += isr / glideTime;
        if (v.glideProgress > 1.0f) v.glideProgress = 1.0f;
        double logStart = std::log(juce::jmax(1.0, v.glideStartFreq));
        double logTarget = std::log(juce::jmax(1.0, v.glideTargetFreq));
        baseMF = std::exp(logStart + (double)v.glideProgress * (logTarget - logStart));
    }
    else
        baseMF = v.glideTargetFreq;

    float  ur = SP.uniDetune;
    float  uRand = (float)((v.midiNote * 1664525 + 1013904223) & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    double um = v.unisonMemo((uRand * 2.f - 1.f) * ur / 1200.0, [](double x) { return std::pow(2.0, x); });
    double phizM   = pFizM ? (double)pFizM->load() : 0.0;
    auto exp2Of = [](double x) { return std::pow(2.0, x); };
    double phizMA  = v.macroMemoA(( phizM * 0.5) / 12.0, exp2Of);
    double phizMB  = v.macroMemoB((-phizM * 0.5) / 12.0, exp2Of);
    const double pmA = v.pitchModMemoA((double)(modSource((int)SP.pitchModSrcA, v, vPosLFO)
                                              * SP.pitchModAmtA * 12.f) / 12.0, exp2Of);
    const double pmB = v.pitchModMemoB((double)(modSource((int)SP.pitchModSrcB, v, vPosLFO)
                                              * SP.pitchModAmtB * 12.f) / 12.0, exp2Of);
    double fA = baseMF * bc.dm * um * pitchMult * pitchEnvMult * bc.sA * bc.octA * bc.tuneMA * phizMA * pmA;
    double fB = baseMF * bc.dm * um * pitchMult * pitchEnvMult * bc.sB * bc.octB * bc.tuneMB * phizMB * pmB;

    // the random jump needs the oscillator's cycle: this voice keeps its own copy of osc A's phase
    v.phaseA += fA / currentSampleRate;
    v.phaseB += fB / currentSampleRate;
    if (v.phaseA >= 1.0)
    {
        v.phaseA -= 1.0;
        float jp = SP.jumpProb;
        if (jp > 0.f && fastRand() < jp * 0.5f)
            v.frameOffset = fastRand() - rawEvoPosA;
    }
    if (v.phaseB >= 1.0) v.phaseB -= 1.0;

    const float velOffA = (evoModeA == 0) ? v.velFrameOffset : 0.f;
    const float velOffB = (evoModeB == 0) ? v.velFrameOffset : 0.f;
    float rvA = rawEvoPosA + v.frameOffset + velOffA + vPosLFO * SP.posLFODepth * 0.5f;
    rvA = std::fmod(rvA, 1.f); if (rvA < 0.f) rvA += 1.f;
    float rvB = rawEvoPosB + v.frameOffset + velOffB + vPosLFO * SP.posLFODepth * 0.5f;
    rvB = std::fmod(rvB, 1.f); if (rvB < 0.f) rvB += 1.f;

    // --- amplitude: envelope x velocity x level x on x A/B balance ---
    const float aVel = SP.ampVelAmt;
    const float velGain = (1.f - aVel) + v.velocity * aVel;
    const float lvA = SP.levelA * (SP.oscAOn > 0.5f ? 1.f : 0.f);
    const float lvB = SP.levelB * (SP.oscBOn > 0.5f ? 1.f : 0.f);
    const float mix = SP.oscMix;
    const float balA = 1.f - juce::jmax(0.f, mix - 0.5f) * 2.f;
    const float balB = 1.f - juce::jmax(0.f, 0.5f - mix) * 2.f;

    // --- the filters' cutoffs, in octaves from Cutoff (velocity scaling included) ---
    float filterEnv = v.fenvLevel * SP.filterEnvAmt;
    float filterLFO = blockPosLFO * SP.filterLFODep;
    float twFilterMod = twMod * SP.twToFilter * 4.f;
    float velScale = 0.3f + 0.7f * velFilterMod;
    float ktOct  = ((float)(v.midiNote - 60) / 12.f) * (SP.filterKeytrack);
    float phizZOct = pFizZ ? pFizZ->load() * 4.f : 0.f;
    const float fmA = modSource((int)SP.filtModSrcA, v, vPosLFO) * SP.filtModAmtA * 4.f;
    const float fmB = modSource((int)SP.filtModSrcB, v, vPosLFO) * SP.filtModAmtB * 4.f;
    float modOctaves = filterEnv * 6.f + filterLFO * 3.f + twFilterMod + ktOct + phizZOct;
    const float velOct = std::log2(juce::jmax(1.0e-6f, velScale));
    float q = juce::jmax(0.1f, SP.filterQ);
    if (phizODest == 2) q = juce::jlimit(0.1f, 12.f, q + phizOval * 6.f);
    float ringAmt = SP.ringMod;
    if (phizODest == 3) ringAmt = juce::jlimit(0.f, 1.f, ringAmt + phizOval);

    out.active = true;
    out.freqA = fA;
    out.freqB = fB;
    out.posA = rvA;
    out.posB = rvB;
    out.ampA = v.envLevel * velGain * lvA * balA;
    out.ampB = v.env2Level * velGain * lvB * balB;
    out.cutOctA = modOctaves + fmA + velOct;
    out.cutOctB = modOctaves + fmB + velOct;
    out.q = q;
    out.ringAmt = ringAmt;
}

//==============================================================================
// the wavetable reads of sampleFrameRaw(), for a table outside the processor
float PhizmoAudioProcessor::readFrames(const std::vector<std::vector<float>>& frames, int cycleSamples,
                                       float fi, double ph, bool interp)
{
    const int numFrames = (int)frames.size();
    if (numFrames <= 0 || cycleSamples <= 0) return 0.f;
    int i0 = juce::jlimit(0, numFrames - 1, (int)fi);
    int i1 = juce::jlimit(0, numFrames - 1, i0 + 1);
    float bl = fi - (float)i0;
    if (interp)
    {
        float fsi = (float)(ph * cycleSamples);
        int si0 = (int)fsi % cycleSamples;
        int si1 = (si0 + 1) % cycleSamples;
        float sfrac = fsi - (float)(int)fsi;
        float smp0 = frames[(size_t)i0][(size_t)si0] + sfrac * (frames[(size_t)i0][(size_t)si1] - frames[(size_t)i0][(size_t)si0]);
        float smp1 = frames[(size_t)i1][(size_t)si0] + sfrac * (frames[(size_t)i1][(size_t)si1] - frames[(size_t)i1][(size_t)si0]);
        return smp0 + bl * (smp1 - smp0);
    }
    int si = juce::jlimit(0, cycleSamples - 1, (int)(ph * cycleSamples) % cycleSamples);
    return frames[(size_t)i0][(size_t)si] + bl * (frames[(size_t)i1][(size_t)si] - frames[(size_t)i0][(size_t)si]);
}

float PhizmoAudioProcessor::bitCrush(float s, float bits)
{
    // as applyBitCrush()
    if (bits >= 15.9f) return s;
    float lv = std::pow(2.f, bits) - 1.f; return std::round(s * lv) / lv;
}

bool PhizmoAudioProcessor::copySlot(int slot, std::vector<std::vector<float>>& frames, int& cycleSamples) const
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) return false;
    const juce::ScopedLock sl(wt[slot].lock);
    if (!wt[slot].loaded) return false;
    frames = wt[slot].frames;
    cycleSamples = wt[slot].cycleSamples;
    return true;
}

bool PhizmoAudioProcessor::bandlimitedEngine() const { return isBandlimitedActive(); }
bool PhizmoAudioProcessor::frameInterpolation(int sound) const
{
    return pFrameInterp != nullptr && soundParams[juce::jlimit(0, NUM_SOUNDS - 1, sound)].frameInterp > 0.5f;
}
bool PhizmoAudioProcessor::gritForcesVintage() const { return pGritVintage == nullptr || pGritVintage->load() > 0.5f; }
