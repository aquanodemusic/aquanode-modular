// The device input of the standalone app and Android (6.7.3): the microphone
// is open only while the patch has an Audio In module, and closed again when
// the last one goes - so the phone's mic (and its green dot) is off whenever
// nothing listens. JUCE's own input mute, with its yellow "Audio input is
// muted" bar, is never used (6.7.2).
//
// Opening or closing means restarting the audio device (a short gap in the
// sound); the device keeps its rate and the patch keeps playing as it was
// (deviceRestartKeepsState).
//
// The microphone permission (6.7.9): asked once, at the start (JUCE's
// standalone app does that) - never again when an Audio In is added; not
// allowed, Audio In just hears nothing. JUCE asks through an empty dialog of
// its own (a DialogFragment), which some phones draw as a grey box over a
// dimmed screen behind the permission's own dialog: it is made invisible here
// whenever it appears (it also carries the file pickers).
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1   // (Android: the app's settings page, below)
#include "PluginProcessor.h"

// set around the device restarts made here; prepareToPlay (PluginProcessor.cpp)
// then keeps the patch as it is - same rate, nothing to prepare again
static std::atomic<bool> deviceRestartKeepsState { false };
bool aquanodeDeviceRestartKeepsState() { return deviceRestartKeepsState.load(); }

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
    // how the input was before it was closed, to open it the same way again
    juce::BigInteger savedInputChannels;
    juce::String savedInputName;

    // what was last decided, and for which device (one the user picks in the
    // settings is looked at anew); message thread only
    int lastWanted = -1;
    juce::AudioIODevice* lastDevice = nullptr;

    void refreshLater (bool force);

    void refreshLater (bool force)
    {
        juce::MessageManager::callAsync ([force]
        {
            if (force)
                lastWanted = -1;
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                if (auto* p = dynamic_cast<AquanodeModularAudioProcessor*> (holder->processor.get()))
                    p->refreshStandaloneInputMute();
        });
    }
}
#endif

void AquanodeModularAudioProcessor::updateStandaloneInputMute (bool patchHasAudioIn)
{
   #if JucePlugin_Build_Standalone
    if (wrapperType != wrapperType_Standalone)
        return;

    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr)
        return;
    if (! mm->isThisTheMessageThread())
    {
        refreshLater (false);   // (a graph built on another thread: decided on the message thread)
        return;
    }

    // a nested Aquanode inside a card is standalone-wrapped too, but only the
    // processor the app itself holds owns the device; while the app is still
    // making it (holder->processor not set yet) it waits for the next call
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr || holder->processor.get() != this)
        return;

    // JUCE's own mute (and its yellow bar): never
    auto& mute = holder->getMuteInputValue();
    if ((bool) mute.getValue())
        mute.setValue (false);

    auto& dm = holder->deviceManager;
    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr)
        return;   // (decided once there is one: the next graph change, the editor)

    const int wanted = patchHasAudioIn ? 1 : 0;
    if (wanted == lastWanted && device == lastDevice)
        return;
    lastWanted = wanted;
    lastDevice = device;

    const bool isOpen = ! device->getActiveInputChannels().isZero();
    if (patchHasAudioIn == isOpen)
        return;

    const auto before = dm.getAudioDeviceSetup();
    auto setup = before;

    if (! patchHasAudioIn)
    {
        // close: no input channels, and no input device where it is a device
        // of its own (Android, WASAPI)
        savedInputChannels = device->getActiveInputChannels();
        savedInputName = setup.inputDeviceName;
        setup.inputDeviceName = {};
        setup.inputChannels.clear();
        setup.useDefaultInputChannels = false;
    }
    else
    {
        // open - if the microphone is allowed (it was asked for at the start;
        // not again here)
        if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
            && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
            return;

        auto* type = dm.getCurrentDeviceTypeObject();
        juce::String name = savedInputName;
        if (type != nullptr && type->hasSeparateInputsAndOutputs())
        {
            const auto names = type->getDeviceNames (true);
            if (names.isEmpty())
                return;   // no input on this machine
            if (name.isEmpty() || ! names.contains (name))
                name = names[juce::jlimit (0, names.size() - 1, type->getDefaultDeviceIndex (true))];
        }
        else
        {
            name = setup.outputDeviceName;   // (one device for both: ASIO and co)
        }

        setup.inputDeviceName = name;
        if (savedInputChannels.isZero())
            setup.inputChannels.setRange (0, juce::jmax (1, holder->getNumInputChannels()), true);
        else
            setup.inputChannels = savedInputChannels;
        setup.useDefaultInputChannels = false;
    }

    // (false: the setup the user chose in the settings stays the one remembered)
    deviceRestartKeepsState = true;
    auto error = dm.setAudioDeviceSetup (setup, false);
    if (error.isNotEmpty() && patchHasAudioIn)
        error = dm.setAudioDeviceSetup (before, false);   // the input wouldn't open: at least the sound as it was
    deviceRestartKeepsState = false;
    lastDevice = dm.getCurrentAudioDevice();
    if (error.isNotEmpty())
        DBG ("Aquanode: device input " << (patchHasAudioIn ? "open" : "close") << " failed: " << error);
   #else
    juce::ignoreUnused (patchHasAudioIn);
   #endif
}

//==============================================================================
#if JUCE_ANDROID
namespace
{
    // JUCE's empty DialogFragment ("FragmentOverlay"), which hosts a permission
    // request or a file picker: its window made see-through, untouchable and
    // without the dim behind it, so only the system's own dialog shows
    struct OverlayHider final : private juce::Timer
    {
        OverlayHider() { startTimer (120); }

        void timerCallback() override
        {
            auto* env = juce::getEnv();
            auto activity = juce::getCurrentActivity();
            if (env == nullptr || activity == nullptr) return;
            juce::LocalRef<jobject> fm (env->CallObjectMethod (activity.get(), juce::AndroidActivity.getFragmentManager));
            if (fm == nullptr) { env->ExceptionClear(); return; }
            juce::LocalRef<jclass> fmClass (env->GetObjectClass (fm.get()));
            const auto find = env->GetMethodID (fmClass.get(), "findFragmentByTag", "(Ljava/lang/String;)Landroid/app/Fragment;");
            if (find == nullptr) { env->ExceptionClear(); return; }
            juce::LocalRef<jobject> frag (env->CallObjectMethod (fm.get(), find, juce::javaString ("FragmentOverlay").get()));
            if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
            if (frag == nullptr) { lastHidden.clear(); return; }
            juce::LocalRef<jclass> fragClass (env->FindClass ("android/app/DialogFragment"));
            if (fragClass == nullptr) { env->ExceptionClear(); return; }
            const auto getDialog = env->GetMethodID (fragClass.get(), "getDialog", "()Landroid/app/Dialog;");
            juce::LocalRef<jobject> dialog (env->CallObjectMethod (frag.get(), getDialog));
            if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
            if (dialog == nullptr) return;
            if (lastHidden != nullptr && env->IsSameObject (lastHidden.get(), dialog.get())) return;   // (done already)
            juce::LocalRef<jclass> dialogClass (env->FindClass ("android/app/Dialog"));
            juce::LocalRef<jobject> window (env->CallObjectMethod (dialog.get(), env->GetMethodID (dialogClass.get(), "getWindow", "()Landroid/view/Window;")));
            if (env->ExceptionCheck() || window == nullptr) { env->ExceptionClear(); return; }
            juce::LocalRef<jclass> windowClass (env->FindClass ("android/view/Window"));
            juce::LocalRef<jclass> colourClass (env->FindClass ("android/graphics/drawable/ColorDrawable"));
            juce::LocalRef<jobject> clear (env->NewObject (colourClass.get(), env->GetMethodID (colourClass.get(), "<init>", "(I)V"), (jint) 0));
            env->CallVoidMethod (window.get(), env->GetMethodID (windowClass.get(), "setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V"), clear.get());
            env->CallVoidMethod (window.get(), env->GetMethodID (windowClass.get(), "clearFlags", "(I)V"), (jint) 0x2);    // FLAG_DIM_BEHIND
            env->CallVoidMethod (window.get(), env->GetMethodID (windowClass.get(), "addFlags", "(I)V"), (jint) (0x10 | 0x8));   // NOT_TOUCHABLE, NOT_FOCUSABLE
            env->CallVoidMethod (window.get(), env->GetMethodID (windowClass.get(), "setDimAmount", "(F)V"), (jfloat) 0.0f);
            if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
            lastHidden = juce::GlobalRef (dialog);
        }

        juce::GlobalRef lastHidden;
    };
}
#endif

// started with the first processor of the standalone app (before JUCE asks
// for the microphone): from then on JUCE's empty dialog never shows
void aquanodeHideAndroidOverlays()
{
   #if JUCE_ANDROID
    static std::unique_ptr<OverlayHider> hider;
    if (hider == nullptr && juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
        hider = std::make_unique<OverlayHider>();
   #endif
}
