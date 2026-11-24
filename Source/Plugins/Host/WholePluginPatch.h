#pragma once
//==============================================================================
// The patch version of a merged plugin that is one module: the original
// plugin's own engine, whole (kit::InstanceModule), with a knob for every one
// of its parameters - read from a live instance of the plugin, so the knobs are
// always the plugin's own, in its own units, at the values its default sound
// starts with. Every knob takes cables. A plugin whose controls are pictures
// rather than knobs (a drawn curve, a loaded sound with its region) shows its
// own panel on the card as well.
//
// Used by AM-20, AudioStretcher, Additive Keys, Bandpass Modulator, FM12 and
// SpectralStereoize (each registers its module at the end of its
// Plugin<Name>.cpp with AQP_WHOLE_PLUGIN_PATCH).
//==============================================================================
#include "PatchKit.h"

namespace aquanode::plugins::whole
{
    struct Def
    {
        juce::String pluginId, kind, description;
        ModuleSection section { ModuleSection::Oscillator };
        bool midi { true }, audioIn { false }, sideIn { false };
        juce::String sideInLabel { "Side In" };
        bool showPanel { false };            // the plugin's own panel on the card
        int panelWidth { 0 }, panelHeight { 0 }, cardWidth { 520 };
        std::function<bool (const juce::String& paramId)> hide;   // parameters kept but not shown as knobs
        juce::String suffix { "whole" };     // (6.9.14) "engine": the one with section cards
        // (6.9.16) knobs held at a value (in the plugin's units; a menu's index), not shown:
        // what this patch version is (EcoSee's: its Custom 3D Grid view)
        std::vector<std::pair<juce::String, float>> fixed;
        // (6.9.21) shown knobs that start elsewhere than the plugin's own start (EcoSee's Auto Rotate: off),
        // and its On / Off switches as wide menus (so the word shows)
        std::vector<std::pair<juce::String, float>> startAt;
        bool wideSwitches { false };
        // (6.9.16) only this part of the plugin's panel on the card (its own pixels; empty: all of it)
        juce::Rectangle<int> panelCrop;
        bool cardsIn { false };              // (6.9.14) a Cards In socket for section cards
        juce::String typeId() const { return "plugin." + pluginId + "." + suffix; }
    };

    // what the sidebar lists before the app runs (no knobs yet)
    ModuleDescriptor baseDescriptor (const Def& def);
    // the knobs, from a fresh instance of the plugin (right before the first module is made)
    void completeDescriptor (const Def& def, ModuleDescriptor& d);

    class Module : public kit::InstanceModule
    {
    public:
        explicit Module (const Def& d);

        void prepare (double sr) override;
        void processSample (const StereoFrame* in, StereoFrame* out) override;

        std::unique_ptr<juce::Component> createExtraContentComponent() override;
        int extraContentHeight() const override;
        int preferredModuleWidth() const override { return def.showPanel ? def.cardWidth : 0; }

        // a sound the plugin holds (AudioStretcher), saved with the patch
        bool hasAudioToSave() const override;
        std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override;
        void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override;

    protected:
        void render (juce::AudioBuffer<float>& io, juce::MidiBuffer& midiIn) override;

    private:
        const Def& def;
        int sideIdx { -1 }, sidePos { 0 };
        std::array<std::array<float, kBlock>, 2> sideFifo {};
    };

    PatchPorts build (const Def& def, AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original, juce::Point<int> origin);

    // (6.9.14) the patch version in smaller cards: the engine (engineDef: its own
    // knobs the ones no card holds) and the section cards chained into its Cards
    // In. The whole module (wholeDef) stays registered for patches that have it.
    // All defs must be statics.
    bool registerWithCards (const Def& wholeDef, const Def& engineDef, const std::vector<kit::CardDef>& cards,
                            PresetLoadFn loader, int columns = 4);
    // a card's knob spec, from a fresh instance of the plugin (by its parameter id)
    ParamSpec pluginSpec (const juce::String& pluginId, const juce::String& paramId);
}

// At the end of Plugin<Name>.cpp: the plugin's one-module patch version.
// defFn() returns a static Def; loader is the PresetLoadFn (or nullptr: the
// patch version builds from the default sound only).
#define AQP_WHOLE_PLUGIN_PATCH(ClassName, defFn, loader) \
    class ClassName final : public aquanode::plugins::whole::Module \
    { \
    public: \
        ClassName() : aquanode::plugins::whole::Module (defFn()) {} \
    }; \
    extern "C" AQUANODE_USED_ATTR bool ClassName##_aqRegistered = \
        aquanode::ModuleFactory::instance().registerModule ( \
            aquanode::plugins::whole::baseDescriptor (defFn()), [] { return std::make_unique<ClassName>(); }, \
            [] (aquanode::ModuleDescriptor& d) { aquanode::plugins::whole::completeDescriptor (defFn(), d); }) \
        && aquanode::plugins::registerPatchVersion (defFn().pluginId, \
            [] (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original, juce::Point<int> origin) \
            { return aquanode::plugins::whole::build (defFn(), patch, original, origin); }, \
            loader); \
    AQUANODE_KEEP_SYMBOL (ClassName##_aqRegistered)
