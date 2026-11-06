#pragma once

// MTS-ESP microtuning for the whole of Aquanode Modular.
//
// MTS-ESP (ODDSound) is a small shared library a tuning master plugin and its
// clients talk to: when a master runs in the session, every client follows its
// scale, live, under held notes too. Aquanode Modular registers one client for
// all of its modules and follows a master whenever there is one - always on, as
// in VirtualDX7 - and is plain equal temperament (A4 = 440 Hz) when there is
// none. midiNoteToHz (ModuleCore.h), which the pitched modules use, goes
// through noteToHz; a note the master leaves unmapped is not played.
//
// The client is ODDSound's libMTSClient, already compiled into Aquanode Modular
// with VirtualDX7 (its functions have C linkage, so that one copy serves the
// whole app). It loads the system's MTS-ESP library at run time and reports "no
// master" when there is none, so a machine without MTS-ESP is unaffected. Not on
// Android (no MTS-ESP there): equal temperament always.
namespace aquanode::tuning
{
    // is a master connected? Refreshed once per audio block (refresh)
    bool hasMaster() noexcept;

    // the processor calls this at the start of every block (audio thread)
    void refresh() noexcept;

    // the frequency of a (fractional) MIDI note: the master's, between two of
    // its keys in proportion; equal temperament without a master
    double noteToHz (double note) noexcept;

    // a key the master's keyboard map leaves out: drop its note-on
    bool shouldFilterNote (int note) noexcept;

    // the master's scale name, empty without one (message thread)
    const char* scaleName();

    // built with MTS-ESP at all (false on Android)
    bool isCompiledIn() noexcept;
}
