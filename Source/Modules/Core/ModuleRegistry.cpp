#include "ModuleRegistry.h"

// Each AQUANODE_REGISTER_MODULE(ClassName, ...) defines an external "C"
// registration symbol named  ClassName##_aqRegistered . Every one of them is
// declared and odr-used below, which forces the linker to keep each module's
// translation unit (and therefore run its registration) - MSVC otherwise
// discards them and the module list comes up empty.
//
// To add a module: add its class name to the matching list. Nothing else
// outside the module's own .h/.cpp needs touching.

#define AQUANODE_CORE_MODULES(X) \
    X (FruitNoteFeed) \
    X (FruitMeter) \
    X (FruitRackPart) \
    X (FruitRollPart) \
    X (FruitPlaylistPart) \
    X (FruitMixerPart) \
    X (AudioInModule) \
    X (AudioOutModule) \
    X (OscillatorModule) \
    X (SamplerModule) \
    X (MultiSampleModule) \
    X (SVFFilterModule) \
    X (LadderFilterModule) \
    X (DelayModule) \
    X (ReverbModule) \
    X (ChorusModule) \
    X (PhaserModule) \
    X (FlangerModule) \
    X (LFOModule) \
    X (ADSRModule) \
    X (SplitterModule) \
    X (BandSplitterModule) \
    X (VolumeModule) \
    X (PolySumModule) \
    X (PanningModule) \
    X (GranulateModule) \
    X (GranulationModule) \
    X (RecordModule) \
    X (ADelaySRModule) \
    X (AquatonReverbModule) \
    X (SpringerModule) \
    X (LiquidChorModule) \
    X (CenterCombModule) \
    X (PrecisionUtilityModule) \
    X (KeyTrackModule) \
    X (VelocityModule) \
    X (GateModule) \
    X (NoiseModule) \
    X (SlewModule) \
    X (RingModModule) \
    X (WaveshaperModule) \
    X (MixerModule) \
    X (ClockModule) \
    X (StepSeqModule) \
    X (EuclidModule) \
    X (SampleHoldModule) \
    X (QuantizerModule) \
    X (LogicModule) \
    X (EnvFollowModule) \
    X (CrossfadeModule) \
    X (ABSwitchModule) \
    X (LatencyModule) \
    X (CompressorModule) \
    X (OptoCompModule) \
    X (ProgramEQModule) \
    X (FetCompModule) \
    X (KickModule) \
    X (ModalDrumModule) \
    X (PluckModule) \
    X (HatsModule) \
    X (AnyFMModule) \
    X (PitchEQModule) \
    X (VocoderModule) \
    X (SpecFilterModule) \
    X (SpecGateModule) \
    X (SpecEnhanceModule) \
    X (SpecDelayModule) \
    X (SpecMorphModule) \
    X (SpecFreezeModule) \
    X (CepstralMorphModule) \
    X (BitcrushModule) \
    X (SRReduceModule) \
    X (GraphicMidiAddModule) \
    X (DCOffsetModule) \
    X (DawModModule) \
    X (SnareModule) \
    X (ClapModule) \
    X (PhaseDistortModule) \
    X (ComplexOscModule) \
    X (UnisonModule) \
    X (FormantModule) \
    X (BowedModule) \
    X (WindModule) \
    X (WavetableModule) \
    X (AdditiveModule) \
    X (ArpModule) \
    X (AlwaysMidiModule) \
    X (StereoWidthModule) \
    X (AnalogDriftModule) \
    X (AudioThruModule) \
    X (LimiterModule) \
    X (DiscardMidiModule) \
    X (KeyboardMidiModule) \
    X (PianoRollModule) \
    X (MpePianoRollModule) \
    X (LowpassModule) \
    X (HighpassModule) \
    X (NotchModule) \
    X (BellModule) \
    X (MidiControlsModule) \
    X (MidiLearnModule) \
    X (CurveCVModule) \
    X (TranswaveModule) \
    X (DXOperatorModule) \
    X (DrawLFOModule) \
    X (BellEQModule) \
    X (CombFilterModule) \
    X (FormantFilterModule) \
    X (TapeWobbleModule) \
    X (AquaFilterModule) \
    X (DropletsModule) \
    X (ChordWavetableModule) \
    X (MultiLFOModule) \
    X (ThreeBandCompModule) \
    X (DXPitchEGModule) \
    X (DXLFOModule) \
    X (DXEnvelopeModule) \
    X (SpectralLockModule) \
    X (SpectralPitchQuantizerModule) \
    X (IRForgeModule) \
    X (ConvolverModule) \
    X (BandpassModule) \
    X (ClipFoldModule) \
    X (SplineFolderModule) \
    X (TextNoteModule) \
    X (ChordNameModule) \
    X (FeltPianoModule) \
    X (MidPianoModule) \
    X (HardPianoModule) \
    X (PianoModule) \
    X (GrandPianoModule) \
    X (TR808Module) \
    X (TR909Module) \
    X (DrumPadsModule) \
    X (StringsModule) \
    X (CelloModule) \
    X (ClarinetModule) \
    X (ElectricGuitarModule) \
    X (AcousticGuitarModule) \
    X (WarpOscModule) \
    X (RandomPresetModule) \
    X (CurveEnvModule) \
    X (RandomLFOModule) \
    X (EchoModule) \
    X (ShelfEQModule) \
    X (DualVcoModule) \
    X (DualFilterModule) \
    X (SkewLFOModule) \
    X (AnalogEGModule) \
    X (PWMOscModule) \
    X (WarmLadderModule) \
    X (EnsembleModule) \
    X (StereoPhaserModule) \
    X (CrossDelayModule) \
    X (SmoothHallModule) \
    X (ResonanceSuppressorModule) \
    X (DriveModule) \
    X (DCBlockerModule) \
    X (WahModule) \
    X (Acid303Module) \
    X (MacroModule) \
    X (TunerModule) \
    X (CpuRamModule) \
    X (ClipPreserveModule) \
    X (SidechainCompModule) \
    X (RerouteSendModule) \
    X (RerouteReturnModule) \
    X (ReassignedSpectrogramModule) \
    X (SpectrumCompareModule) \
    X (WaveformViewerModule) \
    X (SpectralResynthModule) \
    X (PitchShifterModule) \
    X (FreeDelayModule) \
    X (VectorModule) \
    X (MpeModule) \
    X (NoteRandomModule) \
    X (ResynthPianoModule) \
    X (DubEchoModule) \
    X (RotaryModule) \
    X (CirculatorModule) \
    X (DualVcoAnalogModule) \
    X (InspectorModule) \
    X (MultibandDelayModule) \
    X (FilterTableModule) \
    X (ModalBodyModule) \
    X (BloomReverbModule) \
    X (BeatRepeatModule) \
    X (DrumMorphModule) \
    X (PresetCloudModule) \
    X (ElectricPianoModule) \
    X (TempoModule) \
    X (MidiRainModule) \
    X (MidiBloomModule) \
    X (MidiStampModule) \
    X (SoftPadModule) \
    X (SummerWindsModule) \
    X (ShimmerReverbModule) \
    X (AutomationLanesModule) \
    X (StutterModule) \
    X (ReverserModule) \
    X (TapeStopModule) \
    X (NoiseExciterModule) \
    X (FruitArrangerModule) \
    X (DrawbarOrganModule) \
    X (FormantFMModule) \
    X (SupersawModule) \
    X (PlayableEQModule) \
    X (TapeEchoModule) \
    X (GrowlModule) \
    X (SitarModule) \
    X (SpringTankModule) \
    X (MicrotuningModule) \
    X (XYModule) \
    X (AquaDrumkitModule) \
    X (SyncOscModule) \
    X (SuperMidiModule) \
    X (SpecDiffModule) \
    X (FM12OperatorModule) \
    X (NyquistFolderModule) \
    X (PhaseInvertModule) \
    X (EqualLoudnessModule) \
    X (QuadratureModule) \
    X (AudioLFOModule) \
    X (RandomSweepModule) \
    X (RippleEQModule) \
    X (FMFilterModule) \
    X (AudioToFilterModule) \
    X (TiltEQModule) \
    X (Graphic7EQModule) \
    X (PanFilterModule)

// The GUI versions of the plugins merged into Aquanode Modular (src/Plugins)
#define AQUANODE_PLUGIN_GUI_MODULES(X) \
    X (VitaFxGuiModule) \
    X (AquanovaGuiModule) \
    X (AquaVibrioGuiModule) \
    X (AquaMassifGuiModule) \
    X (ReflexGuiModule) \
    X (AM20GuiModule) \
    X (AlphaBetaGuiModule) \
    X (PhizmoGuiModule) \
    X (PhizmOscGuiModule) \
    X (TidebreezeGuiModule) \
    X (CoveGrooveGuiModule) \
    X (VirtualDX7GuiModule) \
    X (VitaGuiModule) \
    X (NoteAdderGuiModule) \
    X (SoundscapeGuiModule) \
    X (SlicerGuiModule) \
    X (CombWeaveGuiModule) \
    X (GrainFreezeGuiModule) \
    X (AudioStretcherGuiModule) \
    X (TablaGuiModule) \
    X (SampleFieldGuiModule) \
    X (ChorusVerbGuiModule) \
    X (CenterCombGuiModule) \
    X (FlowEQGuiModule) \
    X (TapElectricGuiModule) \
    X (VocodeGuiModule) \
    X (AlphaBetaFXGuiModule) \
    X (AutoMorphEQGuiModule) \
    X (FullFilterGuiModule) \
    X (SpringerGuiModule) \
    X (GranulateGuiModule) \
    X (GranulateFXGuiModule) \
    X (PhorestGuiModule) \
    X (AliasSynthGuiModule) \
    X (PrecisionUtilityGuiModule) \
    X (ResonateGuiModule) \
    X (FM12GuiModule) \
    X (AdditiveKeysGuiModule) \
    X (FrequencyDrawerGuiModule) \
    X (SpectralCompressGuiModule) \
    X (SpectralStereoizeGuiModule) \
    X (SpectralFilterGuiModule) \
    X (SpectralCompareGuiModule) \
    X (EcoSeeGuiModule) \
    X (BandpassModulatorGuiModule) \
    X (AquanodeNestedGuiModule) \
    X (FruitGuiModule) \
    X (FruitMixerModule)

// The patch versions' modules ("PluginName - Kind", src/Plugins/<Name>)
#define AQUANODE_PLUGIN_PATCH_MODULES(X) \
    X (AM20Cards) \
    X (AdditiveKeysWholeModule) \
    X (GranulateWholeModule) \
    X (GranulateFXWholeModule) \
    X (FM12Cards) \
    X (FM12TwoOperators) \
    X (BandpassModulatorWholeModule) \
    X (AudioStretcherWholeModule) \
    X (SpectralStereoizeWholeModule) \
    X (SpectralFilterWholeModule) \
    X (SpectralCompareWholeModule) \
    X (SpectralCompressWholeModule) \
    X (EcoSeeWholeModule) \
    X (ChorusVerbChorusModule) \
    X (ChorusVerbDelayModule) \
    X (ChorusVerbPhaserModule) \
    X (ChorusVerbReverbModule) \
    X (TapElectricHumModule) \
    X (TapElectricNoiseModule) \
    X (TapElectricTapeInModule) \
    X (CenterCombBankModule) \
    X (FlowEQBellModule) \
    X (FlowEQCurveModule) \
    X (VocodeVocoderModule) \
    X (AlphaBetaOscModule) \
    X (AlphaBetaDriveModule) \
    X (AlphaBetaFilterModule) \
    X (AlphaBetaEnvModule) \
    X (AlphaBetaLfoModule) \
    X (AlphaBetaMatrixModule) \
    X (AlphaBetaAmpModule) \
    X (AlphaBetaChorusModule) \
    X (AlphaBetaOutputModule) \
    X (AquanovaOscModule) \
    X (AquanovaNoiseModule) \
    X (AquanovaRingModule) \
    X (AquanovaMixerModule) \
    X (AquanovaFilterModule) \
    X (AquanovaAmpModule) \
    X (AquanovaEnvModule) \
    X (AquanovaLfoModule) \
    X (AquanovaVocoderModule) \
    X (AquanovaEqModule) \
    X (AquanovaDistortionModule) \
    X (AquanovaCombModule) \
    X (AquanovaChorusModule) \
    X (AquanovaDelayModule) \
    X (AquanovaReverbModule) \
    X (AquanovaFxMixModule) \
    X (AquanovaPannerModule) \
    X (AquanovaOutputModule) \
    X (AquanovaArpModule) \
    X (PhizmOscOscModule) \
    X (PhizmOscScannerModule) \
    X (PhizmOscMixerModule) \
    X (PhizmOscFilterModule) \
    X (PhizmOscBusFilterModule) \
    X (PhizmOscEnvModule) \
    X (PhizmOscPitchEnvModule) \
    X (PhizmOscLfoModule) \
    X (PhizmOscBusModule) \
    X (PhizmOscChorusModule) \
    X (PhizmOscReverbModule) \
    X (NoteAdderFilterModule) \
    X (NoteAdderChordModule) \
    X (NoteAdderTransposeModule) \
    X (NoteAdderManualModule) \
    X (NoteAdderScaleModule) \
    X (NoteAdderPerNoteModule) \
    X (SlicerPlayerModule) \
    X (VDX7FunctionModule) \
    X (VDX7ChordModule) \
    X (PhizmoSoundModule) \
    X (PhizmoVoiceModule) \
    X (PhizmoOscModule) \
    X (PhizmoFilterModule) \
    X (PhizmoRingModule) \
    X (AquaMassifVoiceModule) \
    X (AquaMassifEngineModule) \
    X (AquaMassifEffectsModule) \
    X (ReflexVoiceModule) \
    X (ReflexEffectsModule) \
    X (AquaVibrioVoiceModule) \
    X (AquaVibrioEffectsModule) \
    X (SoundscapeSlotModule) \
    X (SoundscapeFieldModule) \
    X (SoundscapeGateModule) \
    X (SoundscapeClimateModule) \
    X (SoundscapeHabitatModule) \
    X (SoundscapeOutputModule) \
    X (PhizmoBusModule) \
    X (PhizmoInsertModule) \
    X (PhizmoReverbModule) \
    X (PhizmoOutputModule) \
    X (CombWeaveOscModule) \
    X (CombWeaveOutputModule) \
    X (GrainFreezePlayerModule) \
    X (SampleFieldModule) \
    X (AlphaBetaFXFilterModule) \
    X (AlphaBetaFXChorusModule) \
    X (AutoMorphEQBandModule) \
    X (AutoMorphEQOutputModule) \
    X (FullFilterBankModule) \
    X (PhorestPhaserModule) \
    X (SpringerCoilModule) \
    X (SpringerOutputModule) \
    X (TablaBayanModule) \
    X (TablaDayanModule) \
    X (TablaOutputModule) \
    X (VitaPatchVoiceModule) \
    X (VitaPatchOscModule) \
    X (VitaPatchSamplerModule) \
    X (VitaPatchFilterModule) \
    X (VitaPatchAmpModule) \
    X (VitaPatchEnvModule) \
    X (VitaPatchLfoModule) \
    X (VitaPatchRandomModule) \
    X (VitaPatchMacrosModule) \
    X (VitaPatchModModule) \
    X (VitaPatchMonoVoiceModule) \
    X (VitaPatchMonoEnvModule) \
    X (VitaPatchMonoLfoModule) \
    X (VitaPatchMonoRandomModule) \
    X (VitaPatchChorusModule) \
    X (VitaPatchCompressorModule) \
    X (VitaPatchDelayModule) \
    X (VitaPatchDistortionModule) \
    X (VitaPatchEqModule) \
    X (VitaPatchFilterFxModule) \
    X (VitaPatchFlangerModule) \
    X (VitaPatchPhaserModule) \
    X (VitaPatchReverbModule) \
    X (VitaPatchOutputModule) \
    X (TidebreezeWaveModule) \
    X (TidebreezeEnvModule) \
    X (TidebreezeLfoModule) \
    X (TidebreezeFilterModule) \
    X (TidebreezeAmpModule) \
    X (TidebreezeArpModule) \
    X (TidebreezeInsertModule) \
    X (TidebreezeSendModule) \
    X (TidebreezeOutputModule) \
    X (CoveGrooveSequencerModule) \
    X (CoveGrooveKickModule) \
    X (CoveGrooveBassModule) \
    X (CoveGrooveClapModule) \
    X (CoveGrooveClosedHatModule) \
    X (CoveGrooveOpenHatModule) \
    X (CoveGrooveCymbalModule) \
    X (CoveGroovePercModule) \
    X (CoveGrooveFxDrumModule) \
    X (CoveGrooveFxModule) \
    X (AliasSynthOscModule) \
    X (PrecisionUtilityUtilityModule) \
    X (ResonatorSingleModule) \
    X (ResonateBankPatchModule)

// the VST3 Plugin module exists only where plugin hosting is built in (the desktop exporters)
#if JUCE_PLUGINHOST_VST3
 #define AQUANODE_HOST_MODULES(X) X (ExternalPluginModuleReg)
#else
 #define AQUANODE_HOST_MODULES(X)
#endif

#define AQUANODE_DECLARE_REGISTRATION(ClassName) extern "C" bool ClassName##_aqRegistered;
AQUANODE_HOST_MODULES (AQUANODE_DECLARE_REGISTRATION)
AQUANODE_CORE_MODULES (AQUANODE_DECLARE_REGISTRATION)
AQUANODE_PLUGIN_GUI_MODULES (AQUANODE_DECLARE_REGISTRATION)
AQUANODE_PLUGIN_PATCH_MODULES (AQUANODE_DECLARE_REGISTRATION)

namespace aquanode
{

int forceLinkAllModules()
{
    // volatile so the compiler cannot fold these references away
    volatile bool sink = false;

   #define AQUANODE_TOUCH_REGISTRATION(ClassName) sink = sink ^ ClassName##_aqRegistered;
    AQUANODE_CORE_MODULES (AQUANODE_TOUCH_REGISTRATION)
    AQUANODE_PLUGIN_GUI_MODULES (AQUANODE_TOUCH_REGISTRATION)
    AQUANODE_PLUGIN_PATCH_MODULES (AQUANODE_TOUCH_REGISTRATION)
    AQUANODE_HOST_MODULES (AQUANODE_TOUCH_REGISTRATION)
   #undef AQUANODE_TOUCH_REGISTRATION

    return sink ? 1 : 0;
}

} // namespace aquanode
