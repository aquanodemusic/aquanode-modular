#pragma once

//==============================================================================
// "Don't slow down": the Android Performance Hint (ADPF, Android 12 / 13+).
// The audio thread tells the system how long each block took against how long
// it may take, so the phone keeps the CPU clock up while the patch plays
// instead of lowering it and then missing the audio deadline. The functions
// are looked up at run time (libandroid's APerformanceHint_*), so the app still
// runs on older phones, where this simply does nothing. One session for the
// app, for the thread that runs the top-level patch. The switch is in the top
// bar's Block menu (on by default, remembered in the app's settings).
//==============================================================================
#include <JuceHeader.h>
#include <atomic>
#include <chrono>
#include <thread>

#if JUCE_ANDROID
 #include <dlfcn.h>
 #include <unistd.h>
#endif

namespace aquanode::perfhint
{
    inline std::atomic<bool>& switchedOn()
    {
        static std::atomic<bool> on { true };
        return on;
    }

   #if JUCE_ANDROID
    struct Api
    {
        using GetManager    = void* (*) ();
        using CreateSession = void* (*) (void*, const int32_t*, size_t, int64_t);
        using Report        = int   (*) (void*, int64_t);
        using UpdateTarget  = int   (*) (void*, int64_t);
        using CloseSession  = void  (*) (void*);

        GetManager getManager { nullptr };
        CreateSession createSession { nullptr };
        Report report { nullptr };
        UpdateTarget updateTarget { nullptr };
        CloseSession closeSession { nullptr };
        bool ok { false };

        Api()
        {
            if (auto* lib = dlopen ("libandroid.so", RTLD_NOW | RTLD_LOCAL))
            {
                getManager    = (GetManager)    dlsym (lib, "APerformanceHint_getManager");
                createSession = (CreateSession) dlsym (lib, "APerformanceHint_createSession");
                report        = (Report)        dlsym (lib, "APerformanceHint_reportActualWorkDuration");
                updateTarget  = (UpdateTarget)  dlsym (lib, "APerformanceHint_updateTargetWorkDuration");
                closeSession  = (CloseSession)  dlsym (lib, "APerformanceHint_closeSession");
                ok = getManager && createSession && report && updateTarget && closeSession;
            }
        }
    };

    struct State
    {
        Api api;
        std::atomic<int> audioTid { 0 };
        std::atomic<void*> session { nullptr };
        std::atomic<bool> inUse { false };
        std::atomic<long long> wantedTarget { 0 };   // the block's length (ns), from the audio thread
        long long sessionTarget { 0 };              // audio thread only
        int sessionTid { 0 };                       // message thread only
    };

    inline State& state()
    {
        static State s;
        return s;
    }
   #endif

    // audio thread: one top-level block took 'workNs' of its 'blockNs'
    inline void reportBlock (long long workNs, long long blockNs) noexcept
    {
       #if JUCE_ANDROID
        auto& s = state();
        if (! s.api.ok)
            return;
        s.audioTid.store ((int) gettid(), std::memory_order_relaxed);   // (a restarted device: a new thread)
        s.wantedTarget.store (blockNs, std::memory_order_relaxed);

        s.inUse.store (true, std::memory_order_seq_cst);
        if (auto* session = s.session.load (std::memory_order_seq_cst))
        {
            if (s.sessionTarget != blockNs && blockNs > 0)
            {
                s.api.updateTarget (session, blockNs);
                s.sessionTarget = blockNs;
            }
            if (workNs > 0)
                s.api.report (session, workNs);
        }
        s.inUse.store (false, std::memory_order_release);
       #else
        juce::ignoreUnused (workNs, blockNs);
       #endif
    }

    // message thread: opens or closes the session as the switch says, once
    // the audio thread is known
    inline void update()
    {
       #if JUCE_ANDROID
        auto& s = state();
        if (! s.api.ok)
            return;
        const bool want = switchedOn().load();
        auto* current = s.session.load();
        auto close = [&s] (void* session)
        {
            s.session.store (nullptr);
            while (s.inUse.load())   // a report still running finishes first
                std::this_thread::yield();
            s.api.closeSession (session);
        };
        if (want && current != nullptr && s.sessionTid != s.audioTid.load())
        {
            close (current);   // the audio runs on another thread now
            current = nullptr;
        }
        if (want && current == nullptr)
        {
            const int32_t tid = s.audioTid.load();
            const long long target = s.wantedTarget.load();
            if (tid == 0 || target <= 0)
                return;   // (no block has run yet)
            if (auto* manager = s.api.getManager())
                if (auto* made = s.api.createSession (manager, &tid, 1, target))
                {
                    s.sessionTid = tid;
                    s.session.store (made);
                }
        }
        else if (! want && current != nullptr)
            close (current);
       #endif
    }

    // what the menu says about it
    inline bool available()
    {
       #if JUCE_ANDROID
        return state().api.ok;
       #else
        return false;
       #endif
    }

    // keeps update() running once a second for as long as the app does
    struct Updater final : public juce::Timer
    {
        Updater() { startTimer (1000); }
        void timerCallback() override { update(); }
    };
}
