#pragma once

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

    // echo times in note values (Sync): name and length in quarter notes
    inline const char* const noteNames[] { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1/1" };
    inline constexpr double noteQuarters[] { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
    inline constexpr int numNotes = 13;
}
