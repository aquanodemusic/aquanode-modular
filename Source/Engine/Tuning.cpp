#include "Tuning.h"

#include <atomic>
#include <cmath>

// The same switch as VirtualDX7's (Plugins/PluginVirtualDX7Config.h): its copy
// of libMTSClient is the one these calls link to, so both are on together.
#ifndef AQUANODE_USE_MTS_ESP
 #if defined (__ANDROID__) || defined (ANDROID)
  #define AQUANODE_USE_MTS_ESP 0
 #else
  #define AQUANODE_USE_MTS_ESP 1
 #endif
#endif

#if AQUANODE_USE_MTS_ESP
 #include "../PluginSources/virtualdx7/Source/External/MTS-ESP/libMTSClient.h"
#endif

namespace aquanode::tuning
{
namespace
{
    double equalTempered (double note) noexcept { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

    std::atomic<bool> masterPresent { false };

   #if AQUANODE_USE_MTS_ESP
    // one client for the whole app, registered on first use and let go at exit
    // (before the client library's own statics, which were made first)
    struct Client
    {
        MTSClient* client = MTS_RegisterClient();
        ~Client() { if (client != nullptr) MTS_DeregisterClient (client); }
    };

    MTSClient* client() noexcept
    {
        static Client c;
        return c.client;
    }
   #endif
}

bool isCompiledIn() noexcept { return AQUANODE_USE_MTS_ESP != 0; }

bool hasMaster() noexcept { return masterPresent.load (std::memory_order_relaxed); }

void refresh() noexcept
{
   #if AQUANODE_USE_MTS_ESP
    auto* c = client();
    masterPresent.store (c != nullptr && MTS_HasMaster (c), std::memory_order_relaxed);
   #endif
}

namespace
{
    thread_local const Table* tlTable = nullptr;

    double tableHz (const Table& t, int k) noexcept
    {
        if (k < 0)   return t.hz[0] * std::exp2 ((double) k / 12.0);
        if (k > 127) return t.hz[127] * std::exp2 ((double) (k - 127) / 12.0);
        return t.hz[k];
    }
}

const Table* current() noexcept { return tlTable; }

Scope::Scope (const Table* t) noexcept
{
    if (t != nullptr)
    {
        previous = tlTable;
        tlTable = t;
        set = true;
    }
}

Scope::~Scope()
{
    if (set)
        tlTable = previous;
}

double tablePitch (const Table& t, int key) noexcept
{
    const double hz = tableHz (t, key);
    return hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : (double) key;
}

double noteToHz (double note) noexcept
{
    if (const auto* t = tlTable)
    {
        // between two keys in proportion (as with an MTS-ESP master)
        const double lo = std::floor (note);
        const int k = (int) lo;
        const double f0 = tableHz (*t, k);
        const double frac = note - lo;
        if (frac <= 0.0)
            return f0;
        const double f1 = tableHz (*t, k + 1);
        return f0 > 0.0 && f1 > 0.0 ? f0 * std::pow (f1 / f0, frac) : equalTempered (note);
    }

   #if AQUANODE_USE_MTS_ESP
    if (masterPresent.load (std::memory_order_relaxed))
        if (auto* c = client())
        {
            // between two keys: the master's two frequencies, in proportion (in
            // pitch); outside 0..127 the end key's frequency, octaves away
            const double lo = std::floor (note);
            const int k = (int) lo;
            if (k < 0)   return MTS_NoteToFrequency (c, 0, -1) * std::exp2 (note / 12.0);
            if (k >= 127) return MTS_NoteToFrequency (c, 127, -1) * std::exp2 ((note - 127.0) / 12.0);
            const double f0 = MTS_NoteToFrequency (c, (char) k, -1);
            const double frac = note - lo;
            if (frac <= 0.0)
                return f0;
            const double f1 = MTS_NoteToFrequency (c, (char) (k + 1), -1);
            return f0 > 0.0 && f1 > 0.0 ? f0 * std::pow (f1 / f0, frac) : equalTempered (note);
        }
   #endif
    return equalTempered (note);
}

bool shouldFilterNote (int note) noexcept
{
    if (const auto* t = tlTable)
        return note >= 0 && note < 128 && ! t->mapped[note];

   #if AQUANODE_USE_MTS_ESP
    if (masterPresent.load (std::memory_order_relaxed) && note >= 0 && note < 128)
        if (auto* c = client())
            return MTS_ShouldFilterNote (c, (char) note, -1);
   #endif
    (void) note;
    return false;
}

const char* scaleName()
{
   #if AQUANODE_USE_MTS_ESP
    if (masterPresent.load (std::memory_order_relaxed))
        if (auto* c = client())
            if (const char* n = MTS_GetScaleName (c))
                return n;
   #endif
    return "";
}

} // namespace aquanode::tuning
