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
    if (argc > 1 && String (argv[1]) == "--fit")
    {
        // a phrase sung 1.0-3.0 s (four syllables with small dips); the model put its words 1.5 s late
        std::vector<float> a (16000 * 6, 0.0f);
        for (int i = 16000; i < 48000; ++i) a[(size_t) i] = (0.6f + 0.4f * std::sin (i * 0.0004f)) * 0.3f * std::sin (i * 0.1f);
        std::vector<DualityWords::Word> ws = { { "I", 2.5, 2.8 }, { "keep", 2.85, 3.3 }, { "falling", 3.4, 4.1 }, { "down", 4.1, 4.5 } };
        DualityWords::fitToVoice (ws, a, 0.85, 3.15);
        print (ws);
        // and a phrase already in place stays
        std::vector<DualityWords::Word> ok2 = { { "I", 1.0, 1.3 }, { "keep", 1.35, 1.8 }, { "falling", 1.9, 2.6 }, { "down", 2.6, 3.0 } };
        DualityWords::fitToVoice (ok2, a, 0.85, 3.15);
        const bool ok = std::abs (ws[0].t0 - 1.0) < 0.12 && std::abs (ws[3].t1 - 3.0) < 0.12 && std::abs (ok2[0].t0 - 1.0) < 0.001;
        std::cout << (ok ? "FIT OK" : "FIT WRONG") << "\n";
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
    auto a16 = DualityWords::toListeningRate (buf, r->sampleRate);
    std::cout << "audio: " << String (a16.size() / 16000.0, 2) << " s; sung stretches: " << (int) DualityWords::sungStretches (a16).size() << "\n";

    // --composite: the line after 3 s of silence and again after 2 s more, to check the times far into a take
    const bool composite = argc > 2 && String (argv[2]) == "--composite";
    std::vector<float> onsets;
    if (composite)
    {
        std::vector<float> c ((size_t) (16000 * 3), 0.0f);
        onsets.push_back (3.0f);
        c.insert (c.end(), a16.begin(), a16.end());
        c.resize (c.size() + 16000 * 2, 0.0f);
        onsets.push_back ((float) c.size() / 16000.0f);
        c.insert (c.end(), a16.begin(), a16.end());
        a16 = c;
        // where the speech really starts in each copy
        int first = 0; while (first < (int) a16.size() && std::abs (a16[(size_t) first]) < 0.01f) ++first;
        const float lead = (float) first / 16000.0f - 3.0f;
        for (auto& o : onsets) o += lead;
    }
    std::atomic<bool> abort { false };
    String err;
    float lastShown = -1;
    if (! DualityWords::fetchModel ([&] (float p) { if (p - lastShown >= 0.1f) { lastShown = p; std::cout << "model " << roundToInt (p * 100) << "%\n"; } }, abort, err))
    { std::cerr << "model: " << err << "\n"; return 3; }
    std::cout << "model: " << DualityWords::modelFile().getFullPathName() << " (" << DualityWords::modelFile().getSize() / 1000000 << " MB)\n";

    if (! DualityWords::alignerPresent())
    {
        if (! DualityWords::fetchAligner ({}, abort, err)) std::cout << "aligner: " << err << " (the rougher times)\n";
    }
    std::cout << "aligner: " << DualityWords::alignerLibraryFile().getFullPathName() << ", " << DualityWords::alignerModelFile().getFileName() << "\n";
    DualityWords::Ears ears;
    const auto t = Time::getMillisecondCounterHiRes();
    auto words = ears.hear (a16, {}, abort, err);
    std::cout << "heard " << (int) words.size() << " words in " << String ((Time::getMillisecondCounterHiRes() - t) / 1000.0, 1) << " s" << (err.isNotEmpty() ? " (" + err + ")" : String()) << "\n";
    std::cout << (ears.aligned ? "ALIGNED by the aligner" : "NOT ALIGNED (rough times)") << "\n";
    print (words);
    if (composite)
    {
        // each line's first word near where its speech starts, and every word on sound
        bool ok = ! words.empty();
        for (auto o : onsets)
        {
            double nearest = 1e9; for (auto& w : words) nearest = jmin (nearest, std::abs (w.t0 - o));
            std::cout << "line at " << String (o, 2) << " s: nearest word start " << String (nearest, 2) << " s away\n";
            ok = ok && nearest < 0.25;
        }
        std::cout << (ok ? "TIMING OK" : "TIMING WRONG") << "\n";
        return ok ? 0 : 1;
    }
    if (argc > 2 && ! composite)
    {
        std::cout << "-- lyrics placed:\n";
        print (ears.placeLyrics (words, File (String (argv[2])).loadFileAsString(), a16));
    }
    return words.empty() ? 1 : 0;
}
