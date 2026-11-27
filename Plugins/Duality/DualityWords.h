#pragma once

// Duality's ears: the vocal's words, heard on this computer with whisper.cpp (OpenAI's Whisper, small English model).
// Nothing here knows about ARA: it takes audio and gives back words with their times. The document (DualityARA) decides
// which recordings to hear and keeps the words; the small DualityListen tool runs the same code from the command line.
// Built only into the Duality plugin and DualityListen (DUALITY_WORDS=1).

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <array>
#include <functional>
#include <vector>

struct whisper_context;

namespace DualityWords
{
    struct Word
    {
        juce::String text;
        double t0 = 0, t1 = 0;       // seconds, in the recording
        float confidence = 1.0f;     // how sure the listening was (0..1); a word placed from the lyrics: how well it matched
        bool selected = false;
        int effects = 0;             // the Focus effects on it (bits: DualityParams::Effect)
        std::array<float, 8> echo {}, thr {};   // its own Echo and Throw settings (DualityParams::echoSet / throwSet order)
    };

    // the listening model: where it lives on this computer, and fetching it once (about 190 MB)
    juce::File modelFile();
    juce::String modelURL();
    bool fetchModel (std::function<void (float)> progress, const std::atomic<bool>& abort, juce::String& error);

    // the word aligner (wav2vec2, ~95 MB, and the ONNX Runtime library that runs it, ~30 MB): once it knows the words, it
    // finds exactly where each is sung - the listening model's own times run late on held notes
    juce::File alignerModelFile();
    juce::File alignerLibraryFile();
    bool alignerPresent();
    bool fetchAligner (std::function<void (float)> progress, const std::atomic<bool>& abort, juce::String& error);

    class Aligner
    {
    public:
        Aligner();
        ~Aligner();
        bool load (juce::String& error);
        // the words of one phrase (16 kHz audio from `offset` seconds), each given its start and end; false if it could not
        bool align (std::vector<Word>& words, const float* audio16k, int numSamples, double offset);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    // any audio -> one channel at 16 kHz, what the model hears
    std::vector<float> toListeningRate (const juce::AudioBuffer<float>& audio, double sampleRate);

    // the stretches of a vocal where someone sings: start and end samples (16 kHz), each at most ~28 s
    std::vector<std::pair<int, int>> sungStretches (const std::vector<float>& audio16k);

    class Ears
    {
    public:
        Ears() = default;
        ~Ears();
        bool load (juce::String& error);   // the model into memory (once)
        // the words of a vocal (16 kHz, one channel), each with its start and end in seconds
        std::vector<Word> hear (const std::vector<float>& audio16k, std::function<void (float)> progress,
                                const std::atomic<bool>& abort, juce::String& error);
        // the lyrics as written, put on the vocal: matched to the heard words, then each phrase lined up by the aligner
        std::vector<Word> placeLyrics (const std::vector<Word>& heard, const juce::String& lyrics, const std::vector<float>& audio16k);
        bool aligned = false;   // the last hearing was lined up by the aligner (not just the listening model's times)
        int phrasesAligned = 0, phrasesRough = 0;
        juce::String alignerError;   // why the aligner is not in use, if it is not
    private:
        whisper_context* ctx = nullptr;
        Aligner aligner;
        bool alignerTried = false, alignerOk = false;
        bool ensureAligner();
        JUCE_DECLARE_NON_COPYABLE (Ears)
    };

    // the CTC alignment itself: log-probabilities [frames x 32] of the letters, words -> first and last frame of each
    bool ctcAlign (const std::vector<float>& logProbs, int frames, const std::vector<juce::String>& words,
                   std::vector<int>& firstFrame, std::vector<int>& lastFrame);

    // a phrase's words slid into place: the shift that puts the most of them on sung sound (in [from, to] seconds)
    void fitToVoice (std::vector<Word>& words, const std::vector<float>& audio16k, double from, double to);

    // each word's start moved back to where its sound begins (the quiet just before it, within 150 ms)
    void refineStarts (std::vector<Word>& words, const std::vector<float>& audio16k);

    // the lyrics as written, placed on the heard words: matching words take the heard word's time, words not heard
    // share the time between their neighbours. Without heard words, the lyrics are spread over [start, end].
    std::vector<Word> placeLyrics (const std::vector<Word>& heard, const juce::String& lyrics, double start, double end);

    // a word as it is shown: no punctuation at its ends, no bracketed noise like [Music]
    juce::String cleanWord (const juce::String& raw);
}
