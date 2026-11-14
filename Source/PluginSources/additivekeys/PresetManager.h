#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class K5AudioProcessor;

// Additive Keys presets: a plain, readable XML file - every parameter by its
// real value (a choice by its index), plus the drawn harmonics of both sources:
//
//   <K5Preset name="Glass Bells">
//     <PARAM id="s1_cutoff" value="8000"/>
//     <PARAM id="s1_harmMode" value="3"/>
//     <HARMONICS source="1" levels="1,0.5,..." groups="0,1,2,3,..."/>
//   </K5Preset>
//
// A parameter a file leaves out stays at its default, harmonics it leaves out
// are the default 1/n. Files are only ever read and written where you pick
// them (the panel's Save / Load) - nothing is written on its own.
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".akpreset";
    static constexpr const char* presetTag = "K5Preset";

    explicit PresetManager (K5AudioProcessor& p) : processor (p) {}

    std::unique_ptr<juce::XmlElement> toXml (const juce::String& name) const;
    bool fromXml (const juce::XmlElement& xml);

    bool loadPreset (const juce::File& file);
    bool savePreset (const juce::File& file, const juce::String& name);

    // every parameter back to its default and the default harmonics
    void resetToDefaults();

private:
    K5AudioProcessor& processor;
};
