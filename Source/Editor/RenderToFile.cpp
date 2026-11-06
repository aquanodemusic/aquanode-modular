// Render to File: the panel and the offline render.
#include "PluginEditor.h"
#include "../Engine/Mutation.h"
#include "../Presets/EffectAdders.h"
#include "../Presets/PresetGenerators.h"
#include "../Plugins/Host/PluginCatalog.h"
#include "../Plugins/Host/HostedPlugin.h"
#include "../Platform/AndroidDocuments.h"
#include <map>
#include <set>
#include "EditorShared.h"

//==============================================================================
// Render to File
//==============================================================================
namespace
{
    // The panel Render opens, in the top bar's colours: WAV or FLAC, the
    // Piano Rolls each at their own tempo or all at one, the sample rate; then
    // the render's progress, with Stop.
    class RenderPanel final : public juce::Component, private juce::Timer
    {
    public:
        std::function<void (aquanode::render::Settings)> onRender;
        std::function<void()> onCancel;
        std::function<float()> getProgress;

        explicit RenderPanel (std::vector<aquanode::render::RollInfo> rollsToPlay) : rolls (std::move (rollsToPlay))
        {
            for (auto* b : { &wavButton, &flacButton })
            {
                b->setRadioGroupId (1);
                b->setClickingTogglesState (true);
            }
            for (auto* b : { &eachButton, &oneButton })
            {
                b->setRadioGroupId (2);
                b->setClickingTogglesState (true);
            }
            wavButton.setToggleState (true, juce::dontSendNotification);
            eachButton.setToggleState (true, juce::dontSendNotification);
            eachButton.setTooltip ("Every Piano Roll plays at the tempo it is set to");
            oneButton.setTooltip ("Every Piano Roll plays at the tempo set here");

            bpm.setSliderStyle (juce::Slider::IncDecButtons);
            bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 24);
            bpm.setRange (20.0, 300.0, 0.1);
            bpm.setValue (rolls.empty() ? 120.0 : rolls.front().bpm, juce::dontSendNotification);
            bpm.setTextValueSuffix (" BPM");

            rate.addItemList ({ "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz" }, 1);
            rate.setSelectedId (2, juce::dontSendNotification);

            for (auto* b : { &wavButton, &flacButton, &eachButton, &oneButton })
                b->onClick = [this] { refresh(); };
            bpm.onValueChange = [this] { refresh(); };
            renderButton.onClick = [this] { if (onRender) onRender (settings()); };
            cancelButton.onClick = [this] { if (onCancel) onCancel(); };

            for (juce::Component* c : { (juce::Component*) &wavButton, (juce::Component*) &flacButton,
                                        (juce::Component*) &eachButton, (juce::Component*) &oneButton,
                                        (juce::Component*) &bpm, (juce::Component*) &rate,
                                        (juce::Component*) &renderButton, (juce::Component*) &cancelButton })
                addAndMakeVisible (c);
            addChildComponent (progressBar);
            colour();
            refresh();
        }

        aquanode::render::Settings settings() const
        {
            aquanode::render::Settings s;
            s.flac = flacButton.getToggleState();
            s.oneTempo = oneButton.getToggleState();
            s.bpm = bpm.getValue();
            static const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
            s.sampleRate = rates[juce::jlimit (0, 3, rate.getSelectedId() - 1)];
            return s;
        }

        void setRendering (const juce::String& name)
        {
            rendering = true;
            target = name;
            for (juce::Component* c : { (juce::Component*) &wavButton, (juce::Component*) &flacButton,
                                        (juce::Component*) &eachButton, (juce::Component*) &oneButton,
                                        (juce::Component*) &bpm, (juce::Component*) &rate, (juce::Component*) &renderButton })
                c->setVisible (false);
            progressBar.setVisible (true);
            cancelButton.setButtonText ("Stop");
            startTimerHz (15);
            resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto bar = aquanode::barColour();
            const auto text = aquanode::barTextColour();
            auto r = getLocalBounds().toFloat();
            g.setColour (bar);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (text.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

            auto area = getLocalBounds().reduced (16, 12);
            g.setColour (text);
            g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
            g.drawText ("Render to File", area.removeFromTop (26), juce::Justification::centredLeft);

            g.setFont (juce::FontOptions (13.0f));
            if (rendering)
            {
                g.drawText ("Rendering " + target + " ...", labelRow (0), juce::Justification::centredLeft);
                return;
            }
            g.drawText ("Format",      labelRow (0), juce::Justification::centredLeft);
            g.drawText ("Tempo",       labelRow (1), juce::Justification::centredLeft);
            g.drawText ("Sample rate", labelRow (3), juce::Justification::centredLeft);
            g.setColour (text.withAlpha (0.75f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (info, getLocalBounds().reduced (16, 0).withTop (rowY (4)).withHeight (34),
                              juce::Justification::centredLeft, 2);
        }

        void resized() override
        {
            const int x = 16 + labelW, w = getWidth() - x - 16;
            const int half = (w - 6) / 2;
            wavButton.setBounds (x, rowY (0), half, rowH);
            flacButton.setBounds (x + half + 6, rowY (0), half, rowH);
            eachButton.setBounds (x, rowY (1), half, rowH);
            oneButton.setBounds (x + half + 6, rowY (1), half, rowH);
            bpm.setBounds (x + half + 6, rowY (2), half, rowH);
            rate.setBounds (x, rowY (3), half, rowH);
            const int by = getHeight() - 12 - rowH;
            cancelButton.setBounds (getWidth() - 16 - 100, by, 100, rowH);
            renderButton.setBounds (getWidth() - 16 - 100 - 8 - 120, by, 120, rowH);
            progressBar.setBounds (16, rowY (1), getWidth() - 32, rowH);
        }

    private:
        static constexpr int rowH = 30, labelW = 96;
        int rowY (int row) const { return 12 + 26 + 8 + row * (rowH + 8); }
        juce::Rectangle<int> labelRow (int row) const { return { 16, rowY (row), labelW, rowH }; }

        void refresh()
        {
            bpm.setEnabled (oneButton.getToggleState());
            const auto s = settings();
            const double secs = aquanode::render::rollSeconds (rolls, s);
            const auto longest = std::max_element (rolls.begin(), rolls.end(), [&s] (const auto& a, const auto& b)
            {
                return a.beats / (s.oneTempo ? s.bpm : a.bpm) < b.beats / (s.oneTempo ? s.bpm : b.bpm);
            });
            const int whole = (int) std::ceil (secs);
            info = juce::String (rolls.size()) + (rolls.size() == 1 ? " Piano Roll" : " Piano Rolls") + ", "
                 + juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2) + " and the tails after it";
            if (longest != rolls.end())
                info << " (the longest: " << juce::String ((int) std::lround (longest->beats / 4.0)) << " bars at "
                     << juce::String (s.oneTempo ? s.bpm : longest->bpm, 1) << " BPM)";
            info << ". 24-bit" << (s.flac ? ", FLAC at its smallest." : ".");
            repaint();
        }

        void colour()
        {
            if (! aquanode::barsAreThemed())
            {
                // the default look: the picked choice lit in the keyboard grip's blue
                for (auto* b : { &wavButton, &flacButton, &eachButton, &oneButton })
                {
                    b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4fc3f7));
                    b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10161c));
                }
                return;
            }
            const auto bar = aquanode::barColour();
            const auto face = bar.interpolatedWith (themeColours().get (ThemeColours::Oscillator), 0.35f).brighter (0.15f);
            const auto on = themeColours().get (ThemeColours::Oscillator);
            for (auto* b : { &wavButton, &flacButton, &eachButton, &oneButton, &renderButton, &cancelButton })
            {
                b->setColour (juce::TextButton::buttonColourId, face);
                b->setColour (juce::TextButton::buttonOnColourId, on);
                b->setColour (juce::TextButton::textColourOffId, themeColours().textOn (face));
                b->setColour (juce::TextButton::textColourOnId, themeColours().textOn (on));
            }
        }

        void timerCallback() override
        {
            progress = getProgress != nullptr ? (double) getProgress() : 0.0;
        }

        std::vector<aquanode::render::RollInfo> rolls;
        juce::TextButton wavButton { "WAV" }, flacButton { "FLAC" };
        juce::TextButton eachButton { "Each its own" }, oneButton { "One for all" };
        juce::Slider bpm;
        juce::ComboBox rate;
        juce::TextButton renderButton { "Render..." }, cancelButton { "Cancel" };
        double progress { 0.0 };
        juce::ProgressBar progressBar { progress };
        juce::String info, target;
        bool rendering { false };
    };
}

void AquanodeModularAudioProcessorEditor::showRenderPanel()
{
    if (renderPanel != nullptr)
    {
        if (renderJob == nullptr)
            closeRenderPanel();
        return;
    }
    auto rolls = aquanode::render::rollsToRender (processor);
    if (rolls.empty())
        return;

    auto panel = std::make_unique<RenderPanel> (std::move (rolls));
    panel->onRender = [this] (aquanode::render::Settings s) { chooseRenderFile (s); };
    panel->onCancel = [this]
    {
        // not from inside the panel's own button callback
        juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeRenderPanel(); });
    };
    renderPanel = std::move (panel);
    addAndMakeVisible (*renderPanel);
    resized();
}

void AquanodeModularAudioProcessorEditor::closeRenderPanel()
{
    if (renderJob != nullptr)
    {
        renderJob->signalThreadShouldExit();
        renderJob = nullptr;   // waits for it to stop
        renderTemp.deleteFile();
    }
    renderPanel = nullptr;
}

void AquanodeModularAudioProcessorEditor::chooseRenderFile (const aquanode::render::Settings& settings)
{
    const juce::String ext = settings.flac ? ".flac" : ".wav";
    auto base = juce::File::createLegalFileName (processor.patchName.trim());
    if (base.isEmpty() || base == "Init") base = "Render";
    const auto suggested = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (base + ext);
    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
    fileChooser = std::make_unique<juce::FileChooser> ("Render to File", suggested, "*" + ext);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, settings, ext] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || safe->renderPanel == nullptr)
                return;
           #if JUCE_ANDROID
            const auto url = fc.getURLResult();
            if (url.isEmpty())
                return;
            juce::String name = url.getFileName();
            if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                name = doc.getInfo().getName();
            safe->startRender (settings, juce::File(), url, name);
           #else
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            file = file.withFileExtension (ext);
            safe->startRender (settings, file, juce::URL(), file.getFileName());
           #endif
        });
}

void AquanodeModularAudioProcessorEditor::startRender (const aquanode::render::Settings& settings, const juce::File& destination,
                                                       const juce::URL& document, const juce::String& shownName)
{
    auto* panel = dynamic_cast<RenderPanel*> (renderPanel.get());
    if (panel == nullptr || renderJob != nullptr)
        return;

    renderTemp = juce::File::createTempFile (settings.flac ? ".flac" : ".wav");
    renderJob = std::make_unique<aquanode::render::Job> (processor, settings, renderTemp);
    panel->setRendering (shownName);
    panel->getProgress = [this] { return renderJob != nullptr ? renderJob->progress() : 1.0f; };

    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
    renderJob->onFinished = [safe, destination, document, shownName]
    {
        if (safe != nullptr)
            safe->finishRender (destination, document, shownName);
    };
    renderJob->startThread (juce::Thread::Priority::low);
}

void AquanodeModularAudioProcessorEditor::finishRender (const juce::File& destination, const juce::URL& document,
                                                        const juce::String& shownName)
{
    if (renderJob == nullptr)
        return;   // stopped meanwhile
    renderJob->stopThread (2000);
    auto problem = renderJob->error();
    const double secs = renderJob->renderedSeconds();
    renderJob = nullptr;

    if (problem.isEmpty())
    {
       #if JUCE_ANDROID
        juce::ignoreUnused (destination);
        problem = aqp::android::replaceDocument (document, renderTemp);
       #else
        juce::ignoreUnused (document);
        destination.deleteFile();
        if (! renderTemp.moveFileTo (destination) && ! renderTemp.copyFileTo (destination))
            problem = "it could not be saved to " + destination.getFullPathName();
       #endif
    }
    renderTemp.deleteFile();
    renderPanel = nullptr;

    const int whole = (int) std::lround (secs);
    if (problem.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Render to File",
            shownName + " is ready (" + juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2) + ").");
    else
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Render to File",
                                                "The render did not work: " + problem + ".");
}
