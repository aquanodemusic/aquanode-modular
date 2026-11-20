#pragma once
#include <vector>

class AquanodeModularAudioProcessor;

//==============================================================================
// Fruit Arranger (6.7.12): what its check of the patch hands the graph builder.
// The arranger drives its piano rolls and the knobs it automates through
// hidden sockets - no cables to draw, the links are its own (numbered in its
// sidebar) - so the graph builder adds these edges itself, as it does for the
// Reroute buses. And the modules of its lanes line their inputs up (latency
// compensation), whatever the Patch menu's switch says.
//==============================================================================
struct FruitArrangerLinks
{
    struct Song { int arranger { -1 }; int output { 0 }; int roll { -1 }; };                  // output: 0..15 (its "roll1".. socket)
    struct Knob { int arranger { -1 }; int output { 0 }; int module { -1 }; int param { -1 }; };   // output: 0..15 ("auto1"..)
    std::vector<Song> songs;
    std::vector<Knob> knobs;
    std::vector<int> aligned;      // instance ids whose inputs are lined up
};

// message thread, at the start of every graph build: each Fruit Arranger in
// the patch checks its lanes against the patch as it is now (FruitArrangerModule.cpp)
void fruitArrangerSync (AquanodeModularAudioProcessor& processor, FruitArrangerLinks& links);
