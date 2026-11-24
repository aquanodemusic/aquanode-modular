#pragma once
// Massif - processor.
//
// The sound lives in two places: the 137 knobs and the selectors are host
// parameters (automation, Aquanode cables); everything else of the patch
// (modulation slots, macro names, envelope loops, LFO shapes, stepper and
// performer steps, keytracking curves) lives in `extra`, a massif::Patch
// edited on the message thread and handed to the audio thread as a copy.
#include <JuceHeader.h>
#include "massif/Engine.h"
#include "massif/PresetImport.h"
#include "massif/PresetExport.h"
#include "massif/Factory.h"
#include "massif/GeneratorLevel.h"

class AquaMassifProcessor : public juce::AudioProcessor,
                            public aquanode::MpeReceiver,      // (Aquanode: MPE once a member channel plays)
                            private juce::Timer
{
public:
    void enableMpe (bool on) override { mpeOn = on; }
    void setSlideWavetableDepth (float eachWay) override { engine.slideDepth = eachWay; }   // (6.9.13)
    bool mpeOn { false };
    AquaMassifProcessor();
    ~AquaMassifProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Massif"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return extra.name; }
    void changeProgramName (int, const juce::String& n) override { extra.name = n.toStdString(); }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // the whole patch as the editor sees it (parameters folded in)
    massif::Patch currentPatch() const;
    // replaces the whole sound (parameters + extra)
    void applyPatch (const massif::Patch& p);
    // the non-parameter part changed (mod slots, names, steps ...)
    void extraChanged() { extraSerial.fetch_add (1); }
    massif::Patch extra;                                     // message thread

    // Massif's own presets (.massif): every setting and the wavetables the oscillators play (a generated
    // table as its recipe, any other as audio), so a preset sounds the same anywhere. Aquanode Modular's
    // own patches keep Massif's state, which holds the same.
    bool loadPresetFile (const juce::File& f, juce::String& error);          // .massif (and the older .xml form), or .nmsv
    bool loadPresetData (const juce::MemoryBlock& data, const juce::String& name, juce::String& error);
    juce::MemoryBlock presetData();                                           // the current sound as a .massif file
    bool savePresetFile (const juce::File& f);
    void presetSaved (const juce::File& f, const juce::String& name);       // after the editor wrote presetData() somewhere
    juce::File getLastPresetFile() const { return lastPresetFile; }
    void initPatch();
    // the original's sound files (.nmsv): import, and export over a template file (the sound the patch was imported
    // from, else the export template: any .nmsv of the user's, an init sound is best). Tables the original does not
    // have (generated, loaded) are written as its default table.
    bool importNmsvData (const juce::MemoryBlock& data, const juce::String& name, const juce::File& from, juce::String& error);
    bool exportNmsvData (juce::MemoryBlock& out, juce::String& error, const juce::String& name = {});   // name: the sound's name in the file
    bool hasExportTemplate() const;
    bool setExportTemplate (const juce::MemoryBlock& nmsv, juce::String& error);
    // A bundle of the original's sounds: a zip with .nmsv files anywhere inside (folders kept as categories).
    // Kept whole with the state (so in Aquanode Modular's patches), stepped through with < >; the presets menu
    // lists it. Massif's own Save / Export still write the one current sound.
    struct BundleSound { juce::String name, folder; juce::MemoryBlock data; };
    int importBundle (const juce::MemoryBlock& zip, const juce::String& name, juce::String& error);   // sounds taken
    bool loadBundleSound (int index, juce::String& error);
    int getNumBundleSounds() const { return (int) bundle.size(); }
    const BundleSound& getBundleSound (int i) const { return bundle[(size_t) i]; }
    int getBundleIndex() const { return bundleIndex; }        // -1: the current sound is not from the bundle
    juce::String getBundleName() const { return bundleName; }
    void clearBundle();
    static bool isBundleZip (const juce::MemoryBlock& zip);  // a zip with .nmsv files (and no massif.xml)
    // the built-in sounds
    static int getNumFactoryPatches() { return (int) massif::factoryPatches().size(); }
    void loadFactoryPatch (int index);
    int getFactoryIndex() const { return factoryIndex; }       // -1: none

    // wavetables
    int setWavetableFolder (const juce::File& dir, int onlySlot = -1);         // number of slots filled from files
    juce::File getWavetableFolder() const { return tableFolder; }
    int getNumFileTables() const;
    // a zip of wavetables (as is: any folders inside): its tables go into Massif's own folder and are used at once
    int importWavetableZip (juce::InputStream& zip, juce::String& error);
    static juce::File ownTablesFolder();                                       // Massif's own copy (app data)
    // one table into an oscillator: a file named like one of the original's goes into that table's place (and
    // into Massif's own folder), any other replaces the oscillator's current table for this sound
    int loadOscTable (int osc, const juce::MemoryBlock& wav, const juce::String& fileName, juce::String& error);
    // where a slot's table comes from
    enum TableKind { tkStandIn = 0, tkFile, tkGenerated, tkUser };
    int slotKind (int slot) const { return slot >= 0 && slot < 87 ? rawKind[(size_t) slot] : 0; }
    juce::String slotUserName (int slot) const { return slot >= 0 && slot < 87 ? rawName[(size_t) slot] : juce::String(); }
    bool slotFromFile (int slot) const { return slotKind (slot) == tkFile; }
    std::shared_ptr<const massif::Wavetable> tableFor (int slot);   // builds on demand
    std::shared_ptr<const massif::Wavetable> builtTable (int slot); // the table if it is built already (UI)
    // the wavetable generator: writes a recipe's table into a slot (kept with the state)
    void generateTable (int slot, int recipe, float a, float b, float c, int seed, int frames);
    void restoreTable (int slot);                                   // back to the folder's file / the stand-in
    bool exportTable (int slot, const juce::File& wavFile);        // 2048-sample frames, 32 bit float
    static juce::StringArray tableNames();

    // the editor's colours (see amui in PluginEditor.h): kept with the state, not with a preset
    juce::uint32 themeMetal { 0xffb8bdc8 }, themeGlow { 0xff3fe0f5 };   // AlphaBeta's grey and the Aquanode cyan
    // the patch generator: the last type and genre picked (remembered with the state)
    int genType { massif::generator::tAnything }, genGenre { massif::generator::gAny };
    juce::String generatePatch (int type, int genre);   // type -1: Super Random; returns the patch name

    // meters / UI feedback
    std::atomic<float> outLevel { 0.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> modView[8] {};                         // newest voice's env/mod values, for the UI
    std::atomic<float> posView[3] {};                         // ... and its wavetable positions

    // parameter helpers
    static juce::String knobId (int k) { return "k" + juce::String (k); }
    float knob (int k) const { return knobRaw[(size_t) k]->load(); }
    void setKnob (int k, float v);
    int choice (const juce::String& id) const { return (int) apvts.getRawParameterValue (id)->load(); }
    void setChoice (const juce::String& id, int v);

    static const char* knobName (int k);
    static juce::StringArray choiceList (const juce::String& paramId);   // a selector's options (empty: not a selector)
    static int choiceDefault (const juce::String& paramId);

    //==============================================================================
    // The patch version's split (see AquaMassifPatch.cpp): the voices alone, or
    // the effects alone. The module sets these around its processBlock.
    enum Split { splitFull = 0, splitVoices, splitEffects };
    int split { splitFull };
    static constexpr int kSplitBlock = 64;
    std::array<std::array<float, kSplitBlock>, 2> splitBypass {};   // voices: out / effects: in
    std::array<float, 8> splitFxMod {};                             // voices: out / effects: in
    double tempoOverride { 0.0 };                                   // > 0: the tempo when there is no play head

private:
    void timerCallback() override;
    void readParams (massif::Patch& p) const;
    void writeParams (const massif::Patch& p);
    void loadDefaultFolder();
    juce::ValueTree patchToTree (const massif::Patch& p) const;
    static void treeToPatch (const juce::ValueTree& t, massif::Patch& p);
    void decodeTables (const juce::String& s);  // the older form (states up to 5.20.2): read only
    // Presets and the state are one container: a zip of massif.xml (the settings, and per table its recipe or
    // its file) and samples/ (the tables' sound files as they came)
    using Files = std::vector<std::pair<juce::String, juce::MemoryBlock>>;
    using FileSource = std::function<bool (const juce::String& path, juce::MemoryBlock& bytes)>;
    juce::ValueTree tablesToTree (Files& files);
    void treeToTables (const juce::ValueTree& t, const FileSource& files);
    void releaseOwnTables();                                     // generated / loaded tables back to the originals
    int rescanTables (const std::array<bool, 87>& which);        // the folders, for these slots
    int scanFolder (const juce::File& dir, std::array<std::vector<float>, 87>& fresh, std::array<juce::File, 87>& from,
                    std::array<bool, 87>& got, const std::array<bool, 87>& want) const;
    juce::MemoryBlock writeContainer (bool forPreset);
    bool readContainer (const juce::MemoryBlock& data, bool fromPreset, juce::String& error);
    bool restoreTree (const juce::ValueTree& root, bool fromPreset, const FileSource& files, juce::String& error);
    static juce::File exportTemplateFile();

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    massif::Engine engine;
    massif::Patch audioPatch;                                 // audio thread copy of `extra`
    juce::SpinLock patchLock;
    massif::Patch pendingPatch; bool pendingValid { false };
    std::atomic<int> extraSerial { 0 }; int sentSerial { -1 };

    // tables: raw frames per slot (from files or generated), built mipmaps cached
    std::array<std::vector<float>, 87> rawFrames;
    std::array<int, 87> rawKind {};
    // the parameters' values, looked up once (the audio thread reads them every block)
    std::array<std::atomic<float>*, massif::kNumKnobs> knobRaw {};
    enum ChoiceIdx { cWt = 0, cMode = 3, cOscOn = 6, cModOscOn = 9, cRmT, cPmT, cPosT, cFmT, cNoiseOn, cNoiseType, cFbOn, cFbPos,
                     cF1On, cF1Type, cF2On, cF2Type, cI1On, cI1Type, cI1Pos, cI2On, cI2Type, cI2Pos, cFx1On, cFx1Type, cFx2On, cFx2Type,
                     cAmpOn, cEqOn, cBypassOn, cBypassSrc, cBypassTgt, cVoices, cUnison, cUniPitchOn, cUniWtOn, cUniPanOn,
                     cVoiceMode, cTrigger, cVibMono, cModType, cNumChoices = cModType + 4 };
    std::array<std::atomic<float>*, cNumChoices> choiceRaw {};
    int rc (int i) const { return (int) choiceRaw[(size_t) i]->load(); }
    std::array<float, 87> rawDiv {};          // a generated table's playback octave (gen::recipeDiv)
    struct Recipe { int type { -1 }; float a { 0 }, b { 0 }, c { 0 }; int seed { 0 }, frames { 0 }; };
    std::array<Recipe, 87> rawRecipe {};      // how a generated table was made (presets keep this, not the audio)
    std::array<juce::String, 87> rawName;     // the file name of a loaded table (or one a preset brought)
    std::array<juce::MemoryBlock, 87> rawFile;// ... and its file, kept as it is for the presets
    std::array<juce::File, 87> rawPath;       // a folder table's file
    juce::MemoryBlock importTemplate;         // the last .nmsv imported (the export's template while it is the sound)
    bool soundFromImport { false };
    std::vector<BundleSound> bundle;
    juce::MemoryBlock bundleZip;           // the bundle as kept with the state (its .nmsv files, packed once)
    juce::String bundleName;
    int bundleIndex { -1 };
    bool readBundleZip (const juce::MemoryBlock& zip, std::vector<BundleSound>& out) const;
    std::mutex buildMutex;
    std::array<std::atomic<bool>, 87> buildQueued {};
    int factoryIndex { -1 };
    std::array<std::shared_ptr<const massif::Wavetable>, 87> built;
    std::array<std::shared_ptr<const massif::Wavetable>, 3> pendingTable;
    // a table the engine lets go of is freed here, not on the audio thread
    juce::AbstractFifo retireFifo { 16 };
    std::array<std::shared_ptr<const massif::Wavetable>, 16> retired;
    bool swapTable (int osc, std::shared_ptr<const massif::Wavetable> t);   // audio thread; false: try again next block
    void freeRetired();
    std::array<int, 3> pendingSlot { { -1, -1, -1 } }, liveSlot { { -1, -1, -1 } };
    juce::SpinLock tableLock;
    juce::File tableFolder, lastPresetFile;
    juce::ThreadPool builder { 1 };

    double sr { 44100.0 };
    std::vector<float> tmpL, tmpR, tmpBL, tmpBR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquaMassifProcessor)
};
