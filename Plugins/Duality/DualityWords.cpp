#include "DualityWords.h"
#include "whisper.h"
#include "onnxruntime_c_api.h"
#include <algorithm>
#include <cmath>

using namespace juce;

namespace DualityWords
{
//==============================================================================
File modelFile()
{
    const auto env = SystemStats::getEnvironmentVariable ("DUALITY_MODEL", {});
    if (env.isNotEmpty()) return File (env);
   #if JUCE_MAC
    auto dir = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Application Support");
   #else
    auto dir = File::getSpecialLocation (File::userApplicationDataDirectory);
   #endif
    return dir.getChildFile ("Reluctant Astronaut").getChildFile ("Duality").getChildFile ("ggml-small.en-q5_1.bin");
}

String modelURL()
{
    return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en-q5_1.bin";
}

static constexpr int64 minModelBytes = 150 * 1024 * 1024;   // the real file is ~190 MB; anything much smaller is a broken download

// a file fetched once into place (through a .part file, so a broken download never looks finished)
static bool fetchFile (const String& url, const File& dest, int64 minBytes, std::function<void (float)> progress,
                       const std::atomic<bool>& abort, String& error, int64 expectedBytes)
{
    if (dest.existsAsFile() && dest.getSize() >= minBytes) return true;
    if (! dest.getParentDirectory().createDirectory()) { error = "COULDN'T MAKE THE MODEL FOLDER"; return false; }
    const auto part = dest.getSiblingFile (dest.getFileName() + ".part");
    part.deleteFile();
    int status = 0;
    auto in = URL (url).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                              .withConnectionTimeoutMs (20000)
                                              .withNumRedirectsToFollow (10)
                                              .withStatusCode (&status));
    if (in == nullptr || (status != 0 && status != 200)) { error = "COULDN'T REACH THE MODEL (IS THE COMPUTER ONLINE?)"; return false; }
    const int64 total = in->getTotalLength();
    {
        FileOutputStream out (part);
        if (! out.openedOk()) { error = "COULDN'T WRITE THE MODEL"; return false; }
        HeapBlock<char> buf (1 << 18);
        int64 got = 0;
        for (;;)
        {
            if (abort.load()) { out.flush(); part.deleteFile(); error = "STOPPED"; return false; }
            const int n = in->read (buf.get(), 1 << 18);
            if (n < 0) { error = "THE MODEL DOWNLOAD BROKE OFF"; return false; }
            if (n == 0) { if (in->isExhausted() || (total > 0 && got >= total)) break; Thread::sleep (20); continue; }
            if (! out.write (buf.get(), (size_t) n)) { error = "COULDN'T WRITE THE MODEL (DISK FULL?)"; return false; }
            got += n;
            if (progress) progress (total > 0 ? (float) ((double) got / (double) total) : jmin (0.99f, (float) got / (float) expectedBytes));
        }
        out.flush();
    }
    if (part.getSize() < minBytes) { part.deleteFile(); error = "THE MODEL DOWNLOAD WAS INCOMPLETE"; return false; }
    dest.deleteFile();
    if (! part.moveFileTo (dest)) { error = "COULDN'T SAVE THE MODEL"; return false; }
    return true;
}

bool fetchModel (std::function<void (float)> progress, const std::atomic<bool>& abort, String& error)
{
    return fetchFile (modelURL(), modelFile(), minModelBytes, progress, abort, error, 190000000);
}

//==============================================================================
// the aligner: its model and its engine, from the plug-ins' own release (the files are MIT / Apache-2.0)
static String alignerURL (const String& file) { return "https://github.com/Reluctant-Astronaut/Artifacts/releases/download/models/" + file; }
static String libraryName()
{
   #if JUCE_MAC
    return "libonnxruntime.dylib";
   #elif JUCE_WINDOWS
    return "onnxruntime.dll";
   #else
    return "libonnxruntime-linux.so";
   #endif
}

File alignerModelFile()
{
    const auto env = SystemStats::getEnvironmentVariable ("DUALITY_ALIGNER", {});
    if (env.isNotEmpty()) return File (env);
    return modelFile().getSiblingFile ("w2v-model_quantized.onnx");
}

File alignerLibraryFile()
{
    const auto env = SystemStats::getEnvironmentVariable ("DUALITY_ORT", {});
    if (env.isNotEmpty()) return File (env);
    return modelFile().getSiblingFile (libraryName());
}

bool alignerPresent()
{
    return alignerModelFile().getSize() > 80 * 1024 * 1024 && alignerLibraryFile().getSize() > 5 * 1024 * 1024;
}

bool fetchAligner (std::function<void (float)> progress, const std::atomic<bool>& abort, String& error)
{
    // the engine (a small part of the wait), then the model
    if (! fetchFile (alignerURL (libraryName()), alignerLibraryFile(), 5 * 1024 * 1024,
                     [&] (float p) { if (progress) progress (p * 0.25f); }, abort, error, 30000000)) return false;
    return fetchFile (alignerURL ("w2v-model_quantized.onnx"), alignerModelFile(), 80 * 1024 * 1024,
                      [&] (float p) { if (progress) progress (0.25f + p * 0.75f); }, abort, error, 95286046);
}

//==============================================================================
std::vector<float> toListeningRate (const AudioBuffer<float>& audio, double sampleRate)
{
    const int n = audio.getNumSamples(), chans = jmax (1, audio.getNumChannels());
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int c = 0; c < audio.getNumChannels(); ++c)
    {
        const float* p = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i) mono[(size_t) i] += p[i] / (float) chans;
    }
    if (std::abs (sampleRate - 16000.0) < 1.0) return mono;
    // no aliasing into the model's band: two low-passes at 7 kHz, then to 16 kHz
    for (int k = 0; k < 2; ++k)
    {
        IIRFilter lp; lp.setCoefficients (IIRCoefficients::makeLowPass (sampleRate, jmin (7000.0, sampleRate * 0.45), 0.707));
        lp.processSamples (mono.data(), n);
    }
    const double ratio = sampleRate / 16000.0;
    const int outN = (int) std::floor ((double) n / ratio);
    std::vector<float> out ((size_t) jmax (0, outN));
    LagrangeInterpolator li;
    if (outN > 0) li.process (ratio, mono.data(), out.data(), outN, n, 0);
    return out;
}

// the loudness of a 16 kHz signal in 10 ms frames (dB)
static std::vector<float> framesDb (const float* a, int n)
{
    const int hop = 160, frames = n / hop;
    std::vector<float> db ((size_t) jmax (0, frames));
    for (int f = 0; f < frames; ++f)
    {
        double s = 0; for (int i = 0; i < hop; ++i) { const float x = a[f * hop + i]; s += x * x; }
        db[(size_t) f] = (float) (10.0 * std::log10 (s / hop + 1e-12));
    }
    return db;
}

// The phrases of a vocal: sung stretches split at every pause of 0.3 s or more, each at most 12 s (longer ones split
// at their quietest moment after 4 s). Short pieces keep the model's timing honest: over a long stretch of singing its
// word times can wander by seconds.
std::vector<std::pair<int, int>> sungStretches (const std::vector<float>& a)
{
    const int hop = 160;
    const auto db = framesDb (a.data(), (int) a.size());
    const int frames = (int) db.size();
    std::vector<std::pair<int, int>> out;
    if (frames == 0) return out;
    auto sorted = db; std::sort (sorted.begin(), sorted.end());
    const float loud = sorted[(size_t) ((frames - 1) * 95 / 100)];
    const float thresh = jmax (-60.0f, loud - 30.0f);
    std::vector<std::pair<int, int>> runs;   // sung frames, pauses under 0.3 s closed
    for (int f = 0; f < frames;)
    {
        if (db[(size_t) f] <= thresh) { ++f; continue; }
        int e = f; while (e < frames && db[(size_t) e] > thresh) ++e;
        if (! runs.empty() && f - runs.back().second < 30) runs.back().second = e; else runs.push_back ({ f, e });
        f = e;
    }
    for (auto r : runs)
    {
        if (r.second - r.first < 8) continue;   // a click, a breath
        int s = jmax (0, r.first - 15), e = jmin (frames, r.second + 15);   // 150 ms around it
        while (e - s > 1200)
        {
            int cut = s + 400; float q = 1e9f;
            for (int f = s + 400; f < s + 1200; ++f) if (db[(size_t) f] < q) { q = db[(size_t) f]; cut = f; }
            out.push_back ({ s * hop, cut * hop }); s = cut;
        }
        out.push_back ({ s * hop, e * hop });
    }
    return out;
}

// The model's words of one phrase, put on the phrase's sound. In singing the model's times run late (often most of a
// second) and its first word starts well after the phrase does; what it gets right is the order and the proportions.
// So: the phrase's words are stretched to begin where the singing begins and end where it ends, keeping their
// proportions (each start is then moved back to the quiet just before it, in refineStarts).
void fitToVoice (std::vector<Word>& words, const std::vector<float>& a, double from, double to)
{
    if (words.empty()) return;
    const int f0 = jmax (0, (int) (from * 100.0)), f1 = jmin ((int) (a.size() / 160), (int) (to * 100.0));
    if (f1 - f0 < 10) return;
    const auto db = framesDb (a.data() + f0 * 160, (f1 - f0) * 160);
    float loud = -200.0f; for (float x : db) loud = jmax (loud, x);
    int v0 = 0, v1 = (int) db.size() - 1;
    while (v0 < v1 && db[(size_t) v0] < loud - 30.0f) ++v0;
    while (v1 > v0 && db[(size_t) v1] < loud - 30.0f) --v1;
    const double sungFrom = from + v0 * 0.01, sungTo = from + (v1 + 1) * 0.01;
    const double wa = words.front().t0, wb = words.back().t1;
    if (sungTo - sungFrom < 0.1) return;
    double scale = wb - wa > 0.05 ? (sungTo - sungFrom) / (wb - wa) : 1.0;
    scale = jlimit (0.5, 2.0, scale);
    for (auto& w : words)
    {
        w.t0 = sungFrom + (w.t0 - wa) * scale;
        w.t1 = sungFrom + (w.t1 - wa) * scale;
    }
}

//==============================================================================
String cleanWord (const String& raw)
{
    auto t = raw.trim();
    if (t.containsAnyOf ("[]()*#") || t.contains (String::fromUTF8 ("\xe2\x99\xaa")) || t.contains (String::fromUTF8 ("\xe2\x99\xab"))) return {};
    auto keep = [] (juce_wchar c) { return CharacterFunctions::isLetterOrDigit (c) || c > 127; };
    int a = 0, b = t.length();
    while (a < b && ! keep (t[a])) ++a;
    while (b > a && ! keep (t[b - 1])) --b;
    return t.substring (a, b);
}

// Each word's start, moved back to where its sound begins: the quiet just before it (a breath, a gap, the dip between
// two words) when there is one within 200 ms, so a word begins with its first consonant ("fly", not "-y")
void refineStarts (std::vector<Word>& words, const std::vector<float>& a)
{
    const int hop = 160;   // 10 ms
    const int frames = (int) (a.size() / (size_t) hop);
    if (frames < 4) return;
    std::vector<float> db ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        double s = 0; for (int i = 0; i < hop; ++i) { const float x = a[(size_t) (f * hop + i)]; s += x * x; }
        db[(size_t) f] = (float) (10.0 * std::log10 (s / hop + 1e-12));
    }
    // quiet: near the recording's own floor (a breath, the room), not just softer than the vowel - an "f" or "s" is
    // 20-30 dB under its vowel and belongs to the word
    auto sorted = db; std::sort (sorted.begin(), sorted.end());
    const float floorDb = sorted[(size_t) (frames / 10)];
    for (size_t w = 0; w < words.size(); ++w)
    {
        const int f0 = (int) std::floor (words[w].t0 * 100.0);
        if (f0 <= 0 || f0 >= frames) continue;
        float peak = -200.0f;
        for (int f = f0; f < jmin (frames, f0 + 20); ++f) peak = jmax (peak, db[(size_t) f]);
        const int lowest = jmax (0, jmax (f0 - 20, w > 0 ? (int) std::ceil (words[w - 1].t0 * 100.0) + 3 : 0));
        const float quiet = jmax (peak - 40.0f, floorDb + 8.0f);
        if (db[(size_t) f0] < quiet) continue;   // it already starts in the quiet before the word
        // back to the last quiet frame (well under the word), where its sound begins; else the deepest dip on the way
        int start = -1, dip = -1; float dipDb = peak - 6.0f;
        for (int f = f0 - 1; f >= lowest; --f)
        {
            if (db[(size_t) f] < quiet) { start = f + 1; break; }
            if (db[(size_t) f] < dipDb) { dipDb = db[(size_t) f]; dip = f; }
        }
        if (start < 0) start = dip;
        if (start >= 0) words[w].t0 = jmin (words[w].t0, start * 0.01);
    }
}

//==============================================================================
// The word aligner: wav2vec2 (a letter recogniser, run by ONNX Runtime, loaded from its library the first time) gives,
// for every 20 ms, how likely each letter is; the words' letters are then laid along the phrase in the most likely way
// (CTC alignment). A word starts at the first frame of its first letter and ends at the last frame of its last.
static int letterIndex (juce_wchar c)
{
    // the model's letters (its vocab.json): 0 = nothing (blank), 4 = between words
    static const char* order = "ETAONIHSRDLUMWCFGYPBVK'XJQZ";   // indices 5..31
    c = CharacterFunctions::toUpperCase (c);
    if (c == 0x2019) c = '\'';
    for (int i = 0; order[i] != 0; ++i) if ((juce_wchar) order[i] == c) return 5 + i;
    return -1;
}

bool ctcAlign (const std::vector<float>& lp, int T, const std::vector<String>& words, std::vector<int>& firstFrame, std::vector<int>& lastFrame)
{
    // the extended sequence: blank, letter, blank, letter, ... blank (a word's letters, then the next word's)
    std::vector<int> S { 0 }, wordOf { -1 };
    for (int w = 0; w < (int) words.size(); ++w)
        for (auto c : words[(size_t) w])
            if (const int k = letterIndex (c); k >= 0) { S.push_back (k); wordOf.push_back (w); S.push_back (0); wordOf.push_back (-1); }
    const int L = (int) S.size(), C = 32;
    if (L < 3 || T < (L - 1) / 2 + 1) return false;
    const float ninf = -1.0e30f;
    std::vector<float> prev ((size_t) L, ninf), cur ((size_t) L, ninf);
    std::vector<uint8_t> back ((size_t) T * (size_t) L, 0);   // 0: stay, 1: from s-1, 2: from s-2
    prev[0] = lp[(size_t) S[0]]; prev[1] = lp[(size_t) S[1]];
    for (int t = 1; t < T; ++t)
    {
        const float* row = lp.data() + (size_t) t * C;
        for (int s = 0; s < L; ++s)
        {
            float best = prev[(size_t) s]; uint8_t arg = 0;
            if (s >= 1 && prev[(size_t) s - 1] > best) { best = prev[(size_t) s - 1]; arg = 1; }
            if (s >= 2 && S[(size_t) s] != 0 && S[(size_t) s] != S[(size_t) s - 2] && prev[(size_t) s - 2] > best) { best = prev[(size_t) s - 2]; arg = 2; }
            cur[(size_t) s] = best <= ninf * 0.5f ? ninf : best + row[S[(size_t) s]];
            back[(size_t) t * (size_t) L + (size_t) s] = arg;
        }
        std::swap (prev, cur);
    }
    int s = prev[(size_t) L - 1] > prev[(size_t) L - 2] ? L - 1 : L - 2;
    if (prev[(size_t) s] <= ninf * 0.5f) return false;
    firstFrame.assign (words.size(), -1); lastFrame.assign (words.size(), -1);
    for (int t = T - 1; t >= 0; --t)
    {
        const int w = wordOf[(size_t) s];
        if (w >= 0) { firstFrame[(size_t) w] = t; if (lastFrame[(size_t) w] < 0) lastFrame[(size_t) w] = t; }
        s -= back[(size_t) t * (size_t) L + (size_t) s];
    }
    return true;
}

struct Aligner::Impl
{
    DynamicLibrary lib;
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    ~Impl()
    {
        if (api != nullptr)
        {
            if (session != nullptr) api->ReleaseSession (session);
            if (memory != nullptr) api->ReleaseMemoryInfo (memory);
            if (env != nullptr) api->ReleaseEnv (env);
        }
    }
    bool ok (OrtStatus* st, String& error)
    {
        if (st == nullptr) return true;
        error = "ALIGNER: " + String (api->GetErrorMessage (st)).toUpperCase().substring (0, 80);
        api->ReleaseStatus (st);
        return false;
    }
};

Aligner::Aligner() = default;
Aligner::~Aligner() = default;

bool Aligner::load (String& error)
{
    if (impl != nullptr && impl->session != nullptr) return true;
    impl = std::make_unique<Impl>();
    if (! impl->lib.open (alignerLibraryFile().getFullPathName())) { error = "THE ALIGNER'S ENGINE WOULD NOT LOAD"; impl.reset(); return false; }
    using GetApiBase = const OrtApiBase* (ORT_API_CALL*)();
    auto base = (GetApiBase) impl->lib.getFunction ("OrtGetApiBase");
    if (base == nullptr || base() == nullptr) { error = "THE ALIGNER'S ENGINE IS NOT ONNX RUNTIME"; impl.reset(); return false; }
    impl->api = base()->GetApi (ORT_API_VERSION);
    if (impl->api == nullptr) { error = "THE ALIGNER'S ENGINE IS TOO OLD"; impl.reset(); return false; }
    auto* api = impl->api;
    if (! impl->ok (api->CreateEnv (ORT_LOGGING_LEVEL_ERROR, "duality", &impl->env), error)) { impl.reset(); return false; }
    OrtSessionOptions* so = nullptr;
    if (! impl->ok (api->CreateSessionOptions (&so), error)) { impl.reset(); return false; }
    api->SetIntraOpNumThreads (so, jlimit (1, 4, SystemStats::getNumPhysicalCpus() / 2));
    api->SetSessionGraphOptimizationLevel (so, ORT_ENABLE_ALL);
   #if JUCE_WINDOWS
    const auto path = alignerModelFile().getFullPathName();
    OrtStatus* st = api->CreateSession (impl->env, path.toWideCharPointer(), so, &impl->session);
   #else
    OrtStatus* st = api->CreateSession (impl->env, alignerModelFile().getFullPathName().toRawUTF8(), so, &impl->session);
   #endif
    api->ReleaseSessionOptions (so);
    if (! impl->ok (st, error)) { impl.reset(); return false; }
    if (! impl->ok (api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &impl->memory), error)) { impl.reset(); return false; }
    return true;
}

bool Aligner::align (std::vector<Word>& words, const float* audio, int n, double offset)
{
    if (impl == nullptr || impl->session == nullptr || words.empty() || n < 1600) return false;
    auto* api = impl->api;
    // the model hears the phrase normalised (zero mean, unit variance)
    std::vector<float> x (audio, audio + n);
    double mean = 0; for (float v : x) mean += v; mean /= n;
    double var = 0; for (float v : x) var += (v - mean) * (v - mean); var /= n;
    const float k = (float) (1.0 / std::sqrt (var + 1.0e-7));
    for (auto& v : x) v = (float) ((v - mean) * k);
    int64_t shape[2] { 1, (int64_t) n };
    OrtValue* in = nullptr; OrtValue* out = nullptr;
    String err;
    if (! impl->ok (api->CreateTensorWithDataAsOrtValue (impl->memory, x.data(), x.size() * sizeof (float), shape, 2,
                                                          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), err)) return false;
    const char* inNames[] { "input_values" };
    const char* outNames[] { "logits" };
    const bool ran = impl->ok (api->Run (impl->session, nullptr, inNames, &in, 1, outNames, 1, &out), err);
    api->ReleaseValue (in);
    if (! ran || out == nullptr) return false;
    float* logits = nullptr;
    OrtTensorTypeAndShapeInfo* info = nullptr;
    int64_t dims[3] { 0, 0, 0 };
    bool got = impl->ok (api->GetTensorMutableData (out, (void**) &logits), err) && impl->ok (api->GetTensorTypeAndShape (out, &info), err);
    if (got) { got = impl->ok (api->GetDimensions (info, dims, 3), err); api->ReleaseTensorTypeAndShapeInfo (info); }
    const int T = (int) dims[1];
    if (! got || dims[2] != 32 || T < 2) { api->ReleaseValue (out); return false; }
    // log-probabilities of the letters, frame by frame
    std::vector<float> lp ((size_t) T * 32);
    for (int t = 0; t < T; ++t)
    {
        const float* r = logits + (size_t) t * 32;
        float m = r[0]; for (int c = 1; c < 32; ++c) m = jmax (m, r[c]);
        double sum = 0; for (int c = 0; c < 32; ++c) sum += std::exp (r[c] - m);
        const float lse = m + (float) std::log (sum);
        for (int c = 0; c < 32; ++c) lp[(size_t) t * 32 + (size_t) c] = r[c] - lse;
    }
    api->ReleaseValue (out);
    std::vector<String> texts; for (auto& w : words) texts.push_back (w.text);
    std::vector<int> first, last;
    if (! ctcAlign (lp, T, texts, first, last)) return false;
    const double frame = (double) n / 16000.0 / (double) T;   // ~20 ms
    for (size_t i = 0; i < words.size(); ++i)
    {
        if (first[i] < 0) continue;   // (a word with no letters, e.g. "2": keeps its time, set below)
        words[i].t0 = offset + first[i] * frame;
        words[i].t1 = offset + (last[i] + 1) * frame;
    }
    // a word lasts as long as it is sung: its end runs on through held sound, up to the next word or the quiet
    {
        const auto db = framesDb (audio, n);
        float loud = -200.0f; for (float v : db) loud = jmax (loud, v);
        auto voiced = [&] (double t) { const int f = (int) ((t - offset) * 100.0); return f >= 0 && f < (int) db.size() && db[(size_t) f] > loud - 30.0f; };
        for (size_t i = 0; i < words.size(); ++i)
        {
            if (first[i] < 0) continue;
            const double limit = i + 1 < words.size() && first[i + 1] >= 0 ? words[i + 1].t0 : offset + (double) n / 16000.0;
            while (words[i].t1 + 0.01 <= limit && voiced (words[i].t1)) words[i].t1 += 0.01;
        }
    }
    // words without letters: between their neighbours
    for (size_t i = 0; i < words.size(); ++i)
        if (first[i] < 0)
        {
            const double a = i > 0 ? words[i - 1].t1 : offset, b = i + 1 < words.size() && first[i + 1] >= 0 ? words[i + 1].t0 : a + 0.3;
            words[i].t0 = a; words[i].t1 = jmax (a + 0.05, b);
        }
    return true;
}

bool Ears::ensureAligner()
{
    if (! alignerTried && alignerPresent()) { alignerTried = true; String e; alignerOk = aligner.load (e); }
    return alignerOk;
}

std::vector<Word> Ears::placeLyrics (const std::vector<Word>& heard, const String& lyrics, const std::vector<float>& audio)
{
    const double length = (double) audio.size() / 16000.0;
    auto out = DualityWords::placeLyrics (heard, lyrics, 0.0, length);
    if (out.empty() || ! ensureAligner()) return out;
    // each phrase of the vocal with the lyrics' words that fell in it (by their rough times), lined up exactly
    for (auto& s : sungStretches (audio))
    {
        const double a = s.first / 16000.0, b = s.second / 16000.0;
        std::vector<size_t> idx;
        for (size_t i = 0; i < out.size(); ++i) { const double mid = 0.5 * (out[i].t0 + out[i].t1); if (mid >= a && mid < b) idx.push_back (i); }
        if (idx.empty()) continue;
        std::vector<Word> phrase; for (auto i : idx) phrase.push_back (out[i]);
        if (aligner.align (phrase, audio.data() + s.first, s.second - s.first, a))
            for (size_t k = 0; k < idx.size(); ++k) { out[idx[k]].t0 = phrase[k].t0; out[idx[k]].t1 = phrase[k].t1; out[idx[k]].confidence = 1.0f; }
    }
    std::sort (out.begin(), out.end(), [] (auto& x, auto& y) { return x.t0 < y.t0; });
    return out;
}

Ears::~Ears() { if (ctx != nullptr) whisper_free (ctx); }

static void quietLog (ggml_log_level, const char*, void*) {}

bool Ears::load (String& error)
{
    if (ctx != nullptr) return true;
    whisper_log_set (quietLog, nullptr);
    const auto f = modelFile();
    if (! f.existsAsFile()) { error = "THE LISTENING MODEL IS MISSING"; return false; }
    auto cp = whisper_context_default_params();
    cp.use_gpu = true;   // Metal on a Mac with Apple Silicon; the processor otherwise
    // where each word starts, from the model's attention (DTW): much closer than its own timestamps, which run late
    cp.flash_attn = false;   // (DTW needs it off)
    cp.dtw_token_timestamps = true;
    cp.dtw_aheads_preset = WHISPER_AHEADS_SMALL_EN;
    ctx = whisper_init_from_file_with_params (f.getFullPathName().toRawUTF8(), cp);
    if (ctx == nullptr) { f.deleteFile(); error = "THE LISTENING MODEL WAS DAMAGED (IT WILL BE FETCHED AGAIN)"; return false; }
    return true;
}

namespace
{
    struct Progress { std::function<void (float)> fn; double base = 0, share = 0; };
}

std::vector<Word> Ears::hear (const std::vector<float>& audio, std::function<void (float)> progress,
                              const std::atomic<bool>& abort, String& error)
{
    std::vector<Word> words;
    if (! load (error)) return words;
    aligned = false;
    const auto stretches = sungStretches (audio);
    double totalLen = 0; for (auto& s : stretches) totalLen += s.second - s.first;
    double done = 0;
    for (auto& s : stretches)
    {
        if (abort.load()) { error = "STOPPED"; return {}; }
        // each stretch on its own, padded to at least a second
        std::vector<float> chunk (audio.begin() + s.first, audio.begin() + s.second);
        if (chunk.size() < 16000) chunk.resize (16000, 0.0f);

        auto p = whisper_full_default_params (WHISPER_SAMPLING_GREEDY);
        p.n_threads = jlimit (1, 8, SystemStats::getNumPhysicalCpus() / 2);
        p.language = "en"; p.translate = false; p.no_context = true;
        p.print_progress = p.print_realtime = p.print_special = p.print_timestamps = false;
        p.token_timestamps = true; p.max_len = 1; p.split_on_word = true;
        p.suppress_nst = true; p.suppress_blank = true;
        p.abort_callback = [] (void* u) { return ((const std::atomic<bool>*) u)->load(); };
        p.abort_callback_user_data = (void*) &abort;
        Progress pr { progress, totalLen > 0 ? done / totalLen : 0.0, totalLen > 0 ? (s.second - s.first) / totalLen : 1.0 };
        p.progress_callback = [] (whisper_context*, whisper_state*, int pc, void* u)
        {
            auto* q = (Progress*) u;
            if (q->fn) q->fn ((float) (q->base + q->share * pc / 100.0));
        };
        p.progress_callback_user_data = &pr;

        if (whisper_full (ctx, p, chunk.data(), (int) chunk.size()) != 0)
        {
            if (abort.load()) { error = "STOPPED"; return {}; }
            error = "THE LISTENING FAILED"; return {};
        }
        const double offset = s.first / 16000.0, end = s.second / 16000.0;
        const int eot = whisper_token_eot (ctx);
        std::vector<Word> phraseWords;
        for (int i = 0; i < whisper_full_n_segments (ctx); ++i)
        {
            const auto text = cleanWord (String::fromUTF8 (whisper_full_get_segment_text (ctx, i)));
            if (text.isEmpty()) continue;
            if (whisper_full_get_segment_no_speech_prob (ctx, i) > 0.85f) continue;
            float sum = 0; int n = 0;
            int64_t dtw = -1;
            for (int k = 0; k < whisper_full_n_tokens (ctx, i); ++k)
            {
                const auto d = whisper_full_get_token_data (ctx, i, k);
                if (d.id >= eot) continue;
                sum += d.p; ++n;
                if (dtw < 0 && d.t_dtw >= 0) dtw = d.t_dtw;
            }
            Word w;
            w.text = text;
            const int64_t segT0 = whisper_full_get_segment_t0 (ctx, i);
            // the start: the earlier of the two estimates (a late start loses the word's first consonants)
            w.t0 = offset + (dtw >= 0 ? (double) jmin (dtw, segT0) : (double) segT0) * 0.01;
            w.t1 = offset + whisper_full_get_segment_t1 (ctx, i) * 0.01;
            if (w.t0 >= end) continue;   // (in the padding)
            w.t1 = jlimit (w.t0 + 0.05, jmax (w.t0 + 0.05, end), w.t1);
            w.confidence = n > 0 ? sum / (float) n : 0.5f;
            phraseWords.push_back (w);
        }
        // where each word is sung: the aligner when it is here (its times are exact), else the phrase's sound
        bool placed = false;
        if (ensureAligner() && ! phraseWords.empty())
            placed = aligner.align (phraseWords, audio.data() + s.first, s.second - s.first, offset);
        if (! placed)
        {
            fitToVoice (phraseWords, audio, offset, end);
            refineStarts (phraseWords, audio);
        }
        aligned = aligned || placed;
        for (auto& w : phraseWords) words.push_back (w);
        done += s.second - s.first;
        if (progress) progress (totalLen > 0 ? (float) (done / totalLen) : 1.0f);
    }
    // in time order, never overlapping
    std::sort (words.begin(), words.end(), [] (auto& a, auto& b) { return a.t0 < b.t0; });
    for (size_t i = 1; i < words.size(); ++i)
        if (words[i].t0 < words[i - 1].t1) words[i - 1].t1 = jmax (words[i - 1].t0 + 0.02, words[i].t0);
    return words;
}

//==============================================================================
// lyrics on the heard words: an alignment of the two word lists (edit distance, with near-misses as cheap substitutions)
static String norm (const String& s) { return cleanWord (s).toLowerCase().removeCharacters ("'\xe2\x80\x99"); }

static float likeness (const String& a, const String& b)
{
    if (a == b) return 1.0f;
    const int n = a.length(), m = b.length();
    if (n == 0 || m == 0) return 0.0f;
    std::vector<int> prev ((size_t) m + 1), cur ((size_t) m + 1);
    for (int j = 0; j <= m; ++j) prev[(size_t) j] = j;
    for (int i = 1; i <= n; ++i)
    {
        cur[0] = i;
        for (int j = 1; j <= m; ++j)
            cur[(size_t) j] = jmin (prev[(size_t) j] + 1, cur[(size_t) j - 1] + 1, prev[(size_t) j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1));
        std::swap (prev, cur);
    }
    return 1.0f - (float) prev[(size_t) m] / (float) jmax (n, m);
}

std::vector<Word> placeLyrics (const std::vector<Word>& heard, const String& lyrics, double start, double end)
{
    StringArray tokens;
    tokens.addTokens (lyrics.replaceCharacters ("\r\n\t", "   "), " ", "");
    std::vector<Word> out;
    for (auto& t : tokens) { auto c = cleanWord (t); if (c.isNotEmpty()) { Word w; w.text = c; out.push_back (w); } }
    const int L = (int) out.size(), H = (int) heard.size();
    if (L == 0) return out;
    if (H == 0)
    {
        for (int i = 0; i < L; ++i)
        {
            out[(size_t) i].t0 = start + (end - start) * i / L; out[(size_t) i].t1 = start + (end - start) * (i + 0.9) / L;
            out[(size_t) i].confidence = 0.3f;
        }
        return out;
    }
    // cost table: substitution costs 1 - likeness (a near-miss "quite"/"quiet" is cheap), a skipped word costs 0.8
    std::vector<std::vector<float>> D ((size_t) L + 1, std::vector<float> ((size_t) H + 1, 0.0f));
    std::vector<String> ln ((size_t) L), hn ((size_t) H);
    for (int i = 0; i < L; ++i) ln[(size_t) i] = norm (out[(size_t) i].text);
    for (int j = 0; j < H; ++j) hn[(size_t) j] = norm (heard[(size_t) j].text);
    const float skip = 0.8f;
    for (int i = 0; i <= L; ++i) D[(size_t) i][0] = skip * i;
    for (int j = 0; j <= H; ++j) D[0][(size_t) j] = skip * j;
    for (int i = 1; i <= L; ++i)
        for (int j = 1; j <= H; ++j)
            D[(size_t) i][(size_t) j] = jmin (D[(size_t) i - 1][(size_t) j - 1] + (1.0f - likeness (ln[(size_t) i - 1], hn[(size_t) j - 1])) * 1.2f,
                                              D[(size_t) i - 1][(size_t) j] + skip, D[(size_t) i][(size_t) j - 1] + skip);
    std::vector<int> match ((size_t) L, -1);
    std::vector<float> like ((size_t) L, 0.0f);
    for (int i = L, j = H; i > 0 && j > 0;)
    {
        const float sub = D[(size_t) i - 1][(size_t) j - 1] + (1.0f - likeness (ln[(size_t) i - 1], hn[(size_t) j - 1])) * 1.2f;
        if (std::abs (D[(size_t) i][(size_t) j] - sub) < 1e-4f)
        {
            const float lk = likeness (ln[(size_t) i - 1], hn[(size_t) j - 1]);
            if (lk >= 0.34f) { match[(size_t) i - 1] = j - 1; like[(size_t) i - 1] = lk; }
            --i; --j;
        }
        else if (std::abs (D[(size_t) i][(size_t) j] - (D[(size_t) i - 1][(size_t) j] + skip)) < 1e-4f) --i;
        else --j;
    }
    for (int i = 0; i < L; ++i)
        if (match[(size_t) i] >= 0)
        {
            auto& h = heard[(size_t) match[(size_t) i]];
            out[(size_t) i].t0 = h.t0; out[(size_t) i].t1 = h.t1;
            out[(size_t) i].confidence = like[(size_t) i] >= 0.99f ? 1.0f : 0.45f;   // a near-miss: placed, worth a look
            out[(size_t) i].effects = h.effects; out[(size_t) i].selected = h.selected; out[(size_t) i].echo = h.echo; out[(size_t) i].thr = h.thr;
        }
    // the lyrics' words nobody heard: between their placed neighbours, sharing the time by length
    for (int i = 0; i < L;)
    {
        if (match[(size_t) i] >= 0) { ++i; continue; }
        int e = i; while (e < L && match[(size_t) e] < 0) ++e;
        const int prev = i - 1, next = e < L ? e : -1;
        double a = prev >= 0 ? out[(size_t) prev].t1 : (next >= 0 ? jmax (start, out[(size_t) next].t0 - 0.4 * (e - i)) : start);
        double b = next >= 0 ? out[(size_t) next].t0 : (prev >= 0 ? jmin (end, out[(size_t) prev].t1 + 0.4 * (e - i)) : end);
        if (b - a < 0.12 * (e - i) && prev >= 0)   // no gap: take part of the word before
        {
            auto& p = out[(size_t) prev];
            int chars = 0; for (int k = i; k < e; ++k) chars += out[(size_t) k].text.length();
            const double mid = p.t0 + (b - p.t0) * p.text.length() / (double) (p.text.length() + chars);
            p.t1 = mid; a = mid;
        }
        int chars = 0; for (int k = i; k < e; ++k) chars += out[(size_t) k].text.length() + 1;
        double t = a;
        for (int k = i; k < e; ++k)
        {
            const double d = (b - a) * (out[(size_t) k].text.length() + 1) / (double) jmax (1, chars);
            out[(size_t) k].t0 = t; out[(size_t) k].t1 = t + d * 0.92; out[(size_t) k].confidence = 0.4f;
            t += d;
        }
        i = e;
    }
    return out;
}
}
