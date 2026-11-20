#pragma once
//==============================================================================
// Cove Groove - rendering a lane's modelled-kit sound off the audio thread.
//
// The owner (the processor, or a patch drum) calls request() on the message
// thread with the lane's settings whenever it likes; a render runs on a shared
// background pool only when the settings changed (a newer request supersedes
// an older one still waiting), and take() hands the finished three variations
// back on the message thread. Nothing here touches the audio thread: the owner
// publishes what take() gives it the way it publishes a loaded sample.
//==============================================================================
#include <JuceHeader.h>
#include "CgVoices.h"

namespace covegroove::phys
{
    using Variants = std::array<std::shared_ptr<const SampleData>, 3>;

    inline Variants renderSpec (const Spec& s)
    {
        Variants out;
        const auto* p = presetFor (s.kit, s.lane, s.choice);
        if (p == nullptr || s.sr <= 0.0) return out;
        pd::Mods m;
        m.pitch = std::pow (2.0, (double) s.tune / 12.0);
        m.decay = juce::jlimit (0.25, 4.0, (double) s.decay);
        m.tone = juce::jlimit (0.0, 1.0, (double) s.tone);
        for (int k = 0; k < 3; ++k) m.extra[k] = juce::jlimit (-1.0, 1.0, (double) s.extra[k]);
        for (int v = 0; v < 3; ++v)
        {
            const auto r = pd::render (*p, m, s.sr, 1.0, (uint64_t) (v + 1), true);
            if (r.l.empty()) continue;
            auto d = std::make_shared<SampleData>();
            d->rate = s.sr;
            d->left.assign (r.l.begin(), r.l.end());
            if (r.stereo && r.r.size() == r.l.size()) d->right.assign (r.r.begin(), r.r.end());
            // the tail below -90 dB is cut (the hits end where they are heard to)
            float peak = 0.0f;
            for (float x : d->left) peak = std::max (peak, std::abs (x));
            const float floor = peak * 3.0e-5f;
            size_t end = d->left.size();
            while (end > 16 && std::abs (d->left[end - 1]) < floor && (d->right.empty() || std::abs (d->right[end - 1]) < floor)) --end;
            d->left.resize (end);
            if (! d->right.empty()) d->right.resize (end);
            out[(size_t) v] = std::move (d);
        }
        return out;
    }

    inline juce::ThreadPool& pool()
    {
        static juce::ThreadPool p { juce::ThreadPoolOptions().withThreadName ("Cove Groove Models").withNumberOfThreads (2) };
        return p;
    }

    class Renderer
    {
    public:
        Renderer() : shared (std::make_shared<Shared>()) {}
        ~Renderer() { shared->alive = false; }

        // message thread: render these settings unless they are what was last asked for
        void request (const Spec& s)
        {
            if (hasRequested && s == requested) return;
            requested = s;
            hasRequested = true;
            const int ticket = ++shared->ticket;
            auto sh = shared;
            pool().addJob ([sh, s, ticket]
            {
                if (! sh->alive || sh->ticket.load() != ticket) return;     // a newer request is on its way
                auto v = renderSpec (s);
                if (! sh->alive || sh->ticket.load() != ticket) return;
                const juce::SpinLock::ScopedLockType lk (sh->lock);
                sh->ready = std::move (v);
                sh->hasReady = true;
            });
        }

        // message thread: the newest finished render, once
        bool take (Variants& out)
        {
            const juce::SpinLock::ScopedLockType lk (shared->lock);
            if (! shared->hasReady) return false;
            out = std::move (shared->ready);
            shared->ready = {};
            shared->hasReady = false;
            return true;
        }

        void forget() { hasRequested = false; }

    private:
        struct Shared
        {
            juce::SpinLock lock;
            Variants ready;
            bool hasReady { false };
            std::atomic<int> ticket { 0 };
            std::atomic<bool> alive { true };
        };
        std::shared_ptr<Shared> shared;
        Spec requested;
        bool hasRequested { false };
    };

    // a lane's sound rendered on this thread, for showing it (the tweak row's
    // envelope view): the voice itself, played to its end (at most 4 s)
    inline std::shared_ptr<SampleData> preview (int lane, const Sound& s, float sr)
    {
        auto v = std::make_unique<DrumVoice>();
        v->prepare (sr);
        v->start (lane, s, 1.0f, false, 12345u);
        auto d = std::make_shared<SampleData>();
        d->rate = sr;
        const int maxN = (int) (sr * 4.0f);
        d->left.reserve ((size_t) (sr * 0.5f));
        d->right.reserve ((size_t) (sr * 0.5f));
        for (int i = 0; i < maxN && v->isActive(); ++i)
        {
            float l = 0.0f, r = 0.0f;
            v->render (l, r);
            d->left.push_back (l);
            d->right.push_back (r);
        }
        return d;
    }
}
