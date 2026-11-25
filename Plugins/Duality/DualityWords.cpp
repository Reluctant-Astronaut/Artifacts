#include "DualityWords.h"
#include "whisper.h"
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

bool fetchModel (std::function<void (float)> progress, const std::atomic<bool>& abort, String& error)
{
    const auto dest = modelFile();
    if (dest.existsAsFile() && dest.getSize() >= minModelBytes) return true;
    if (! dest.getParentDirectory().createDirectory()) { error = "COULDN'T MAKE THE MODEL FOLDER"; return false; }
    const auto part = dest.getSiblingFile (dest.getFileName() + ".part");
    part.deleteFile();

    int status = 0;
    auto in = URL (modelURL()).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
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
            if (progress) progress (total > 0 ? (float) ((double) got / (double) total) : jmin (0.99f, (float) got / 190.0e6f));
        }
        out.flush();
    }
    if (part.getSize() < minModelBytes) { part.deleteFile(); error = "THE MODEL DOWNLOAD WAS INCOMPLETE"; return false; }
    dest.deleteFile();
    if (! part.moveFileTo (dest)) { error = "COULDN'T SAVE THE MODEL"; return false; }
    return true;
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

std::vector<std::pair<int, int>> sungStretches (const std::vector<float>& a)
{
    const int frame = 320;   // 20 ms
    const int frames = (int) (a.size() / (size_t) frame);
    std::vector<std::pair<int, int>> out;
    if (frames == 0) return out;
    std::vector<float> db ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        double s = 0; for (int i = 0; i < frame; ++i) { const float x = a[(size_t) (f * frame + i)]; s += x * x; }
        db[(size_t) f] = (float) (10.0 * std::log10 (s / frame + 1e-12));
    }
    auto sorted = db; std::sort (sorted.begin(), sorted.end());
    const float loud = sorted[(size_t) ((frames - 1) * 95 / 100)];
    const float thresh = jmax (-60.0f, loud - 35.0f);
    // sung frames, widened by 0.2 s each side, gaps under 0.8 s closed
    std::vector<bool> on ((size_t) frames, false);
    for (int f = 0; f < frames; ++f)
        if (db[(size_t) f] > thresh)
            for (int g = jmax (0, f - 10); g <= jmin (frames - 1, f + 10); ++g) on[(size_t) g] = true;
    std::vector<std::pair<int, int>> runs;
    for (int f = 0; f < frames;)
    {
        if (! on[(size_t) f]) { ++f; continue; }
        int e = f; while (e < frames && on[(size_t) e]) ++e;
        if (! runs.empty() && f - runs.back().second < 40) runs.back().second = e; else runs.push_back ({ f, e });
        f = e;
    }
    // each at most 28 s (the model hears 30 s at a time): longer ones split at their quietest moment after 15 s
    for (auto r : runs)
    {
        int s = r.first;
        while (r.second - s > 1400)
        {
            int cut = s + 750; float q = 1e9f;
            for (int f = s + 750; f < s + 1400; ++f) if (db[(size_t) f] < q) { q = db[(size_t) f]; cut = f; }
            out.push_back ({ s * frame, cut * frame }); s = cut;
        }
        if (r.second - s >= 12) out.push_back ({ s * frame, r.second * frame });
    }
    return out;
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
        for (int i = 0; i < whisper_full_n_segments (ctx); ++i)
        {
            const auto text = cleanWord (String::fromUTF8 (whisper_full_get_segment_text (ctx, i)));
            if (text.isEmpty()) continue;
            if (whisper_full_get_segment_no_speech_prob (ctx, i) > 0.85f) continue;
            float sum = 0; int n = 0;
            for (int k = 0; k < whisper_full_n_tokens (ctx, i); ++k)
            {
                const auto d = whisper_full_get_token_data (ctx, i, k);
                if (d.id >= eot) continue;
                sum += d.p; ++n;
            }
            Word w;
            w.text = text;
            w.t0 = offset + whisper_full_get_segment_t0 (ctx, i) * 0.01;
            w.t1 = offset + whisper_full_get_segment_t1 (ctx, i) * 0.01;
            if (w.t0 >= end) continue;   // (in the padding)
            w.t1 = jlimit (w.t0 + 0.05, jmax (w.t0 + 0.05, end), w.t1);
            w.confidence = n > 0 ? sum / (float) n : 0.5f;
            words.push_back (w);
        }
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
            out[(size_t) i].effects = h.effects; out[(size_t) i].selected = h.selected;
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
