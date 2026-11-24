# Tape Echo (Dusk Audio)

The files in this folder are the DSP core of **Tape Echo 2** by **Dusk Audio**, from
[dusk-audio-plugins](https://github.com/dusk-audio/dusk-audio-plugins) (commit 052d672),
licensed under the GNU General Public License v3.0 or later (Copyright (C) 2026 Dusk Audio).
GPLv3 section 13 allows them to be combined with Aquanode Modular, which is licensed under the
GNU AGPLv3; these files themselves stay under the GPLv3.

Taken: `plugins/tape-echo/core/TapeEchoDSP.hpp/.cpp`, `plugins/tape-echo/daf-plugin/TapeEchoParams.hpp`
and `plugins/shared-daf/dsp/DuskSmoothed.hpp`, `DuskFilters.hpp`, `DuskOversampler.hpp`,
`DuskDenormals.hpp`. The only change: TapeEchoDSP.hpp includes TapeEchoParams.hpp from this folder.
Not taken: the plugin shell, its UI, presets and tape visualisation. Aquanode's own wrapper is
`../TapeEchoModule.cpp`.

Aquanode Modular 6.9.11 changed one thing in this core: SpringReverb::process
steps its three springs side by side, section by section (the same arithmetic
per spring, so the output is the same sample for sample), to run faster.
