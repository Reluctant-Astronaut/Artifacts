#pragma once
#include <array>

// The settings of Duality that belong to a track (and are shared between Cubase's instances when it is an ARA extension):
// the plate, and the words' Echo and Throw. Their order is the order they are saved in; new ones go at the end.
namespace DualityParams
{
    inline constexpr int numShared = 23;
    inline const char* const sharedIDs[numShared] {
        "pdecay", "pdamp", "ppre", "plowcut", "phighcut", "pduck", "perspective", "width", "mix", "output",
        "esync", "etime", "enote", "efb", "etype", "etone", "eamt",
        "tfreeze", "tdecay", "tpre", "tsize", "ttone", "tamt" };
    enum Index { pdecay, pdamp, ppre, plowcut, phighcut, pduck, perspective, width, mix, output,
                 esync, etime, enote, efb, etype, etone, eamt,
                 tfreeze, tdecay, tpre, tsize, ttone, tamt };

    // the Focus keys' effects, as bits of a word's effects
    enum Effect { throwFx = 0, echoFx = 1, swellFx = 2, doubleFx = 3, harmonyFx = 4, shiftFx = 5 };

    // each word's own settings for its effects (the Words knobs edit the selected words'): which settings, in what order
    inline constexpr int numEcho = 7, numThrow = 6;
    inline constexpr Index echoSet[numEcho]   { esync, etime, enote, efb, etype, etone, eamt };
    inline constexpr Index throwSet[numThrow] { tfreeze, tdecay, tpre, tsize, ttone, tamt };
    inline constexpr int voices = 4;   // words whose tails overlap each ring in a voice of their own, up to this many at once

    // the settings a word starts with in a project from before words had their own (the parameters' defaults)
    inline std::array<float, 8> defaultEchoSettings()  { return { 1.0f, 375.0f, 5.0f, 0.35f, 0.0f, -0.3f, 0.5f, 0.0f }; }
    inline std::array<float, 8> defaultThrowSettings() { return { 0.0f, 4.0f, 30.0f, 1.0f, 0.0f, 0.6f, 0.0f, 0.0f }; }

    // echo times in note values (Sync): name and length in quarter notes
    inline const char* const noteNames[] { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1/1" };
    inline constexpr double noteQuarters[] { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
    inline constexpr int numNotes = 13;
}
