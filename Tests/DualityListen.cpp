// DualityListen: Duality's ears from the command line, for the build checks.
//   DualityListen <audio file> [lyrics text file]   - fetches the model if needed, prints each heard word with its time,
//                                                     then (with lyrics) the lyrics placed on them
//   DualityListen --align                          - the lyrics placement on a made-up example (needs no model)
// Exit code 0 when words were heard (or the example placed as expected).

#include "DualityWords.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace juce;

static void print (const std::vector<DualityWords::Word>& ws)
{
    for (auto& w : ws)
        std::cout << String (w.t0, 2) << "\t" << String (w.t1, 2) << "\t" << String (w.confidence, 2) << "\t" << w.text << "\n";
}

int main (int argc, char** argv)
{
    if (argc > 1 && String (argv[1]) == "--align")
    {
        std::vector<DualityWords::Word> heard = { { "the", 0.2, 0.4 }, { "lamps", 0.45, 0.9 }, { "are", 1.0, 1.2 }, { "low", 1.25, 1.8 },
                                                  { "and", 2.4, 2.6 }, { "I", 2.65, 2.8 }, { "keep", 2.85, 3.2 }, { "falling", 3.3, 3.9 },
                                                  { "in", 4.0, 4.2 }, { "the", 4.3, 4.4 }, { "quite", 4.5, 5.1 } };
        auto placed = DualityWords::placeLyrics (heard, "The lamps are low along the shore,\nand I keep falling into the quiet", 0, 6);
        print (placed);
        bool ok = placed.size() == 14 && placed[4].text == "along" && placed[13].text == "quiet"
               && std::abs (placed[13].t0 - 4.5) < 0.01 && placed[4].t0 >= 1.8 - 1e-6 && placed[11].t0 >= 3.9 - 1e-6;
        std::cout << (ok ? "ALIGN OK" : "ALIGN WRONG") << "\n";
        return ok ? 0 : 1;
    }
    if (argc > 1 && String (argv[1]) == "--starts")
    {
        // a word heard late: its "f" (quiet, 0.40-0.50 s) and vowel (0.50-0.90 s) after silence; the model says 0.60
        std::vector<float> a (16000 * 2, 0.0f);
        Random rng (1);
        for (int i = 6400; i < 8000; ++i) a[(size_t) i] = 0.03f * (rng.nextFloat() * 2 - 1);
        for (int i = 8000; i < 14400; ++i) a[(size_t) i] = 0.4f * std::sin (i * 0.12f);
        // a second word joined to the first (no silence): a dip at 0.95 s, the model says 1.10
        for (int i = 14400; i < 15200; ++i) a[(size_t) i] = 0.08f * std::sin (i * 0.12f);
        for (int i = 15200; i < 24000; ++i) a[(size_t) i] = 0.4f * std::sin (i * 0.09f);
        std::vector<DualityWords::Word> ws = { { "fly", 0.60, 0.90 }, { "away", 1.10, 1.50 } };
        DualityWords::refineStarts (ws, a);
        print (ws);
        const bool ok = std::abs (ws[0].t0 - 0.40) < 0.03 && ws[1].t0 > 0.88 && ws[1].t0 < 0.97;
        std::cout << (ok ? "STARTS OK" : "STARTS WRONG") << "\n";
        return ok ? 0 : 1;
    }
    if (argc < 2) { std::cerr << "usage: DualityListen <audio> [lyrics.txt] | --align\n"; return 2; }

    AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (File (String (argv[1]))));
    if (r == nullptr) { std::cerr << "can't read " << argv[1] << "\n"; return 2; }
    AudioBuffer<float> buf ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true);
    const auto a16 = DualityWords::toListeningRate (buf, r->sampleRate);
    std::cout << "audio: " << String (a16.size() / 16000.0, 2) << " s; sung stretches: " << (int) DualityWords::sungStretches (a16).size() << "\n";

    std::atomic<bool> abort { false };
    String err;
    float lastShown = -1;
    if (! DualityWords::fetchModel ([&] (float p) { if (p - lastShown >= 0.1f) { lastShown = p; std::cout << "model " << roundToInt (p * 100) << "%\n"; } }, abort, err))
    { std::cerr << "model: " << err << "\n"; return 3; }
    std::cout << "model: " << DualityWords::modelFile().getFullPathName() << " (" << DualityWords::modelFile().getSize() / 1000000 << " MB)\n";

    DualityWords::Ears ears;
    const auto t = Time::getMillisecondCounterHiRes();
    auto words = ears.hear (a16, {}, abort, err);
    std::cout << "heard " << (int) words.size() << " words in " << String ((Time::getMillisecondCounterHiRes() - t) / 1000.0, 1) << " s" << (err.isNotEmpty() ? " (" + err + ")" : String()) << "\n";
    print (words);
    if (argc > 2)
    {
        std::cout << "-- lyrics placed:\n";
        print (DualityWords::placeLyrics (words, File (String (argv[2])).loadFileAsString(), 0, a16.size() / 16000.0));
    }
    return words.empty() ? 1 : 0;
}
