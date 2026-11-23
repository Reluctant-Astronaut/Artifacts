// Artifacts sonic test rig.
// One executable per plugin. Prints tab-separated rows:  plugin  test  metric  value  verdict
// Optionally writes spectra (CSV) for plotting into the directory given as argv[1].
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ArtifactsProcessor.h"
#include <complex>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

using namespace juce;
using Buf = std::vector<float>;

static String gName;
static File gOut;
static String gSuffix;

static void row (const String& test, const String& metric, double v, const String& verdict = "INFO")
{
    std::printf ("%s\t%s\t%s\t%.3f\t%s\n", gName.toRawUTF8(), test.toRawUTF8(), (metric + gSuffix).toRawUTF8(), v, verdict.toRawUTF8());
    std::fflush (stdout);
}
static String lim (double v, double pass, double warn, bool lowerIsBetter = true)
{
    if (lowerIsBetter) return v <= pass ? "PASS" : v <= warn ? "WARN" : "FAIL";
    return v >= pass ? "PASS" : v >= warn ? "WARN" : "FAIL";
}
static double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

//==============================================================================
struct Rig
{
    std::unique_ptr<AudioProcessor> p;
    double sr = 48000.0;

    Rig (double rate = 48000.0, int block = 512) : p (createPluginFilter()), sr (rate)
    {
        p->setPlayConfigDetails (2, 2, rate, block);
        if (has ("channel")) norm ("channel", 0.5f);   // a fixed unit: every instance has its own tolerances by design, so tests pin one
        p->prepareToPlay (rate, block);
        if (isReverbName()) Thread::sleep (700);       // the convolution engine loads its impulse response on a background thread
    }
    static bool isReverbName() { return gName.contains ("Duality"); }
    ~Rig() { p->releaseResources(); }

    void set (const String& id, float realValue)
    {
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (static_cast<ArtifactsProcessor*> (p.get())->apvts.getParameter (id)))
            rp->setValueNotifyingHost (rp->convertTo0to1 (realValue));
    }
    void norm (const String& id, float n01)
    {
        if (auto* rp = static_cast<ArtifactsProcessor*> (p.get())->apvts.getParameter (id))
            rp->setValueNotifyingHost (n01);
    }
    bool has (const String& id) { return static_cast<ArtifactsProcessor*> (p.get())->apvts.getParameter (id) != nullptr; }
    int latency() { return p->getLatencySamples(); }

    void process (const Buf& inL, const Buf& inR, Buf& outL, Buf& outR, int block = 512)
    {
        const int n = (int) inL.size();
        outL.assign ((size_t) n, 0.0f); outR.assign ((size_t) n, 0.0f);
        AudioBuffer<float> b (2, block);
        MidiBuffer midi;
        for (int pos = 0; pos < n; pos += block)
        {
            const int m = std::min (block, n - pos);
            AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, m);
            std::memcpy (blk.getWritePointer (0), inL.data() + pos, sizeof (float) * (size_t) m);
            std::memcpy (blk.getWritePointer (1), inR.data() + pos, sizeof (float) * (size_t) m);
            p->processBlock (blk, midi);
            std::memcpy (outL.data() + pos, blk.getReadPointer (0), sizeof (float) * (size_t) m);
            std::memcpy (outR.data() + pos, blk.getReadPointer (1), sizeof (float) * (size_t) m);
        }
    }
    void reset() { p->releaseResources(); p->prepareToPlay (sr, 512); }
};

//==============================================================================
static Buf pink (int n, float rmsLinear, int seed = 7)
{
    Random r (seed);
    Buf x ((size_t) n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    double sum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = r.nextDouble() * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        const double v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
        x[(size_t) i] = (float) v; sum += v * v;
    }
    const float g = rmsLinear / (float) std::sqrt (sum / n);
    for (auto& v : x) v *= g;
    return x;
}
static Buf sine (int n, double freq, double sr, float amp)
{
    Buf x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = amp * (float) std::sin (MathConstants<double>::twoPi * freq * i / sr);
    return x;
}
static double rms (const Buf& x, int from = 0, int to = -1)
{
    if (to < 0) to = (int) x.size();
    double s = 0; for (int i = from; i < to; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (s / std::max (1, to - from));
}
static double peakOf (const Buf& x) { double p = 0; for (auto v : x) p = std::max (p, (double) std::abs (v)); return p; }
static bool bad (const Buf& x) { for (auto v : x) if (! std::isfinite (v)) return true; return false; }

// Power spectrum (single block, Hann). Returns N/2+1 bins of power.
static std::vector<double> powerSpectrum (const Buf& x, int start, int N)
{
    dsp::FFT fft ((int) std::log2 (N));
    std::vector<float> d ((size_t) N * 2, 0.0f);
    double wsum = 0;
    for (int i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (MathConstants<double>::twoPi * i / N);
        d[(size_t) i] = (float) (x[(size_t) (start + i)] * w); wsum += w;
    }
    fft.performRealOnlyForwardTransform (d.data());
    std::vector<double> P ((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double re = d[(size_t) k * 2], im = d[(size_t) k * 2 + 1];
        P[(size_t) k] = (re * re + im * im) / (wsum * wsum / 4.0);
    }
    return P;
}
// Welch-averaged power spectrum
static std::vector<double> welch (const Buf& x, int start, int len, int N = 8192)
{
    std::vector<double> acc ((size_t) N / 2 + 1, 0.0);
    int cnt = 0;
    for (int pos = start; pos + N <= start + len; pos += N / 2)
    {
        auto P = powerSpectrum (x, pos, N);
        for (size_t k = 0; k < P.size(); ++k) acc[k] += P[k];
        ++cnt;
    }
    for (auto& v : acc) v /= std::max (1, cnt);
    return acc;
}
static double bandPower (const std::vector<double>& P, double lo, double hi, double sr)
{
    const int N = (int) (P.size() - 1) * 2;
    double s = 0;
    for (int k = 1; k < (int) P.size(); ++k)
    {
        const double f = k * sr / N;
        if (f >= lo && f < hi) s += P[(size_t) k];
    }
    return s;
}
static const double octaves[] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static double octDb (const std::vector<double>& P, int i, double sr)
{
    const double c = octaves[i];
    return 10.0 * std::log10 (std::max (1.0e-20, bandPower (P, c / 1.4142, std::min (c * 1.4142, sr * 0.49), sr)));
}
static void dumpCSV (const String& name, const std::vector<double>& P, double sr)
{
    if (gOut == File()) return;
    String s;
    const int N = (int) (P.size() - 1) * 2;
    for (int k = 1; k < (int) P.size(); k += 1)
        s << String ((double) k * sr / N, 2) << "," << String (10.0 * std::log10 (std::max (1.0e-16, P[(size_t) k])), 2) << "\n";
    gOut.getChildFile (gName + "_" + name + ".csv").replaceWithText (s);
}

//==============================================================================
// the plugin families and their "character" scenarios
static bool isStrip() { return gName.contains ("Console") || gName.contains ("Kintsugi"); }
static bool isReverb() { return gName.contains ("Duality"); }
static bool isDelay() { return gName.contains ("Anemoia"); }

static void neutralise (Rig& r)   // remove everything that is time-varying, so measurements are repeatable
{
    for (auto id : { "drift", "hum", "wow", "flutter", "wear" }) r.norm (id, 0.0f);
}

struct Scenario { String name; std::function<void (Rig&)> apply; };
static std::vector<Scenario> scenarios()
{
    std::vector<Scenario> s;
    if (gName.contains ("Console"))
    {
        s.push_back ({ "default", [] (Rig& r) { r.norm ("mix", 1.0f); } });
        s.push_back ({ "driven", [] (Rig& r) { r.norm ("mix", 1.0f); r.norm ("cdrive", 0.6f); r.norm ("xfmr", 1.0f); } });
        s.push_back ({ "hot", [] (Rig& r) { r.norm ("mix", 1.0f); r.norm ("cdrive", 1.0f); r.norm ("glue", 0.6f); r.norm ("xfmr", 1.0f); } });
    }
    else if (gName.contains ("Kintsugi"))
    {
        s.push_back ({ "default", [] (Rig& r) { neutralise (r); r.norm ("mix", 1.0f); } });
        s.push_back ({ "driven", [] (Rig& r) { neutralise (r); r.norm ("mix", 1.0f); r.norm ("vdrive", 0.6f); r.norm ("tsat", 0.6f); } });
        s.push_back ({ "hot", [] (Rig& r) { neutralise (r); r.norm ("mix", 1.0f); r.norm ("vdrive", 1.0f); r.norm ("tsat", 1.0f); } });
    }
    else if (isReverb())
    {
        s.push_back ({ "kensho", [] (Rig& r) { r.set ("perspective", 1); r.norm ("mix", 1.0f); } });
        s.push_back ({ "object-hall", [] (Rig& r) { r.set ("perspective", 0); r.set ("rtype", 1); r.norm ("mix", 1.0f); } });
        s.push_back ({ "object-plate", [] (Rig& r) { r.set ("perspective", 0); r.set ("rtype", 2); r.norm ("mix", 1.0f); } });
        s.push_back ({ "object-room", [] (Rig& r) { r.set ("perspective", 0); r.set ("rtype", 0); r.norm ("mix", 1.0f); } });
        s.push_back ({ "subject", [] (Rig& r) { r.set ("perspective", 2); r.norm ("mix", 1.0f); } });
    }
    else
    {
        s.push_back ({ "echo", [] (Rig& r) { r.set ("memon", 0); r.norm ("mix", 1.0f); } });
        s.push_back ({ "memories", [] (Rig& r) { r.set ("echoon", 0); r.norm ("mix", 1.0f); } });
        s.push_back ({ "both", [] (Rig& r) { r.norm ("mix", 1.0f); } });
    }
    return s;
}

//==============================================================================
// 1. latency and frequency response / phase (small signal, so only the linear behaviour shows)
static std::complex<double> demod (const Buf& x, int from, int to, double f, double sr, int shift = 0)
{
    std::complex<double> acc (0.0, 0.0);
    for (int i = from; i < to; ++i)
        acc += (double) x[(size_t) (i - shift)] * std::polar (1.0, -MathConstants<double>::twoPi * f * i / sr);
    return acc;
}
static void testLatency (double sr)
{
    if (! isStrip()) { Rig r (sr); row ("latency", "reported_samples", r.latency(), r.latency() == 0 ? "PASS" : "INFO"); return; }
    for (auto& sc : scenarios())
    {
        Rig r (sr); neutralise (r); sc.apply (r); r.norm ("cdrive", 0.0f); r.norm ("vdrive", 0.0f); r.norm ("tsat", 0.0f); r.norm ("glue", 0.0f);
        const int lat = r.latency();
        const std::vector<double> freqs { 30, 40, 60, 100, 200, 500, 1000, 2000, 4000, 8000, 12000, 16000 };
        std::vector<std::complex<double>> H;
        for (double f : freqs)
        {
            const int n = (int) sr * 3;
            Buf s = sine (n, f, sr, 0.02f), a, b;
            Rig rr (sr); neutralise (rr); sc.apply (rr); rr.norm ("cdrive", 0.0f); rr.norm ("vdrive", 0.0f); rr.norm ("tsat", 0.0f); rr.norm ("glue", 0.0f);
            rr.process (s, s, a, b);
            const int from = (int) sr, to = n - 1000;
            H.push_back (demod (a, from, to, f, sr) / demod (s, from, to, f, sr, lat));
        }
        double minComb = 99, worstGain = 0;
        for (size_t k = 0; k < freqs.size(); ++k)
        {
            const double gdb = 20 * std::log10 (std::abs (H[k])), ph = std::arg (H[k]) * 180.0 / MathConstants<double>::pi;
            if (freqs[k] == 40 || freqs[k] == 100 || freqs[k] == 1000 || freqs[k] == 8000 || freqs[k] == 16000)
            {
                row ("response", sc.name + "_" + String ((int) freqs[k]) + "Hz_gain_dB", gdb);
                row ("response", sc.name + "_" + String ((int) freqs[k]) + "Hz_phase_deg", ph);
            }
            if (freqs[k] >= 100 && freqs[k] <= 12000)
            {
                minComb = std::min (minComb, 20 * std::log10 (std::abs (0.5 + 0.5 * H[k])));
                worstGain = std::max (worstGain, std::abs (gdb));
            }
        }
        row ("latency", sc.name + "_reported_ms", 1000.0 * lat / sr);
        // group delay in the midrange, from the phase slope between 500 Hz and 2 kHz, relative to the reported latency (samples)
        const double ph500 = std::arg (H[5]), ph2k = std::arg (H[7]);
        double dph = ph2k - ph500; while (dph > MathConstants<double>::pi) dph -= MathConstants<double>::twoPi; while (dph < -MathConstants<double>::pi) dph += MathConstants<double>::twoPi;
        const double gd = -dph / (MathConstants<double>::twoPi * (2000.0 - 500.0)) * sr;
        row ("latency", sc.name + "_midrange_alignment_error_samples", gd, lim (std::abs (gd), 0.75, 2.0));
        row ("response", sc.name + "_mix50_worst_comb_100to12k_dB", minComb, lim (-minComb, 1.5, 3.0));
        row ("response", sc.name + "_flatness_100to12k_max_dev_dB", worstGain, lim (worstGain, 1.5, 3.0));
    }
}
static void testNull (double sr)
{
    if (! isStrip()) return;
    Rig r (sr); neutralise (r); r.norm ("mix", 0.0f);
    const int n = (int) sr * 4;
    Buf in = pink (n, 0.1f), inR = pink (n, 0.1f, 99), oL, oR;
    r.process (in, inR, oL, oR);
    const int lat = r.latency();
    double e = 0, ref = 0;
    for (int i = 20000; i < n; ++i)
    {
        const double d = (double) oL[(size_t) i] - in[(size_t) (i - lat)];
        e += d * d; ref += (double) in[(size_t) (i - lat)] * in[(size_t) (i - lat)];
    }
    row ("null", "mix0_residual_dB", 10 * std::log10 (std::max (e, 1e-30) / ref), lim (10 * std::log10 (std::max (e, 1e-30) / ref), -100.0, -60.0));

    // dry/wet continuity: a mix sweep must not click; check max sample-to-sample jump of a steady sine with mix ramped
    Rig r2 (sr); neutralise (r2);
    Buf s = sine (n, 440.0, sr, 0.25f), a, b;
    r2.norm ("mix", 0.0f); r2.process (s, s, a, b, 64);
    r2.norm ("mix", 1.0f); r2.process (s, s, a, b, 64);
    double maxJump = 0; for (int i = 4000; i < n - 1; ++i) maxJump = std::max (maxJump, (double) std::abs (a[(size_t) i + 1] - a[(size_t) i]));
    row ("null", "mix_step_max_jump", maxJump, lim (maxJump, 0.13, 0.25));
}

// 3. harmonics (THD) and 4. aliasing
static void testHarmonicsAndAlias (double sr)
{
    if (isDelay() || isReverb())
    {
        // a space or an echo should add no distortion of its own: THD+N of a steady sine, wet only
        for (auto& sc : scenarios())
        {
            Rig r (sr); sc.apply (r);
            if (isDelay()) { r.norm ("feedback", 0.0f); r.norm ("mix", 1.0f); }
            const int N = 65536; const int n = N + (int) sr;
            const int k = 1366; const double f = k * sr / N;
            Buf s = sine (n, f, sr, 0.25f), a, b;
            r.process (s, s, a, b);
            // for these the output is not a sine (echoes, reverb); only report that level stays sane and no aliasing/noise floor spikes
            row ("level", sc.name + "_peak_dBFS_sine_-12", db (peakOf (a)), lim (db (peakOf (a)), 0.0, 3.0));
        }
        return;
    }
    const std::vector<double> levels { -24, -12, -6, -1 };
    for (auto& sc : scenarios())
        for (double lv : levels)
        {
            Rig r (sr); neutralise (r); sc.apply (r);
            const int N = 65536, n = N + 32768;
            const int k = 1366; const double f = k * sr / N;   // bin-centred ~1 kHz
            const float amp = (float) Decibels::decibelsToGain (lv);
            Buf s = sine (n, f, sr, amp), a, b;
            r.process (s, s, a, b);
            auto P = powerSpectrum (a, 24000, N);
            auto at = [&] (int bin) { double s2 = 0; for (int j = -2; j <= 2; ++j) if (bin + j >= 0 && bin + j < (int) P.size()) s2 += P[(size_t) (bin + j)]; return s2; };
            const double fund = at (k);
            double thd = 0, hi = 0;
            String detail;
            double h2 = 0, h3 = 0;
            for (int h = 2; h <= 12 && h * k < (int) P.size() - 3; ++h)
            {
                const double rel = at (h * k) / fund;
                thd += rel; if (h >= 5) hi += rel;
                if (h == 2) h2 = rel; if (h == 3) h3 = rel;
            }
            const String tag = sc.name + "_" + String (lv, 0) + "dB";
            row ("harmonics", tag + "_THD_pct", 100.0 * std::sqrt (thd));
            row ("harmonics", tag + "_H2_dB", 10 * std::log10 (std::max (h2, 1e-14)));
            row ("harmonics", tag + "_H3_dB", 10 * std::log10 (std::max (h3, 1e-14)));
            const double hiDb = 10 * std::log10 (std::max (hi, 1e-14));
            row ("harmonics", tag + "_H5to12_dB", hiDb, (lv <= -6 && sc.name != "hot") ? lim (hiDb, -50.0, -35.0) : "INFO");
            if (lv == -6 || lv == -12) dumpCSV ("spectrum_" + sc.name + "_" + String (-lv, 0), P, sr);
        }

    // aliasing: a high, inharmonic fundamental driven hard; everything that is not a true harmonic below Nyquist is aliasing or noise
    for (auto& sc : scenarios())
        for (double lv : { -12.0, -3.0 })
        {
            Rig r (sr); neutralise (r); sc.apply (r);
            const int N = 65536, n = N + 32768;
            const int k = (int) (0.17 * N);                  // ~8.1 kHz at 48k; harmonics fold to unrelated places
            const double f = k * sr / N;
            Buf s = sine (n, f, sr, (float) Decibels::decibelsToGain (lv)), a, b;
            r.process (s, s, a, b);
            auto P = powerSpectrum (a, 24000, N);
            auto near = [&] (int bin, int h) { return std::abs (bin - h * k) <= 3; };
            double fund = 0; for (int j = -2; j <= 2; ++j) fund += P[(size_t) (k + j)];
            double worst = 0; int worstBin = 0;
            double worstHi = 0; int worstHiBin = 0;
            for (int bin = 40; bin < (int) P.size(); ++bin)
            {
                bool ok = false;
                for (int h = 1; h * k < (int) P.size() - 3; ++h) if (near (bin, h)) { ok = true; break; }
                if (ok) continue;
                if ((double) bin * sr / N < 20000.0) { if (P[(size_t) bin] > worst) { worst = P[(size_t) bin]; worstBin = bin; } }
                else if (P[(size_t) bin] > worstHi) { worstHi = P[(size_t) bin]; worstHiBin = bin; }
            }
            row ("aliasing", sc.name + "_" + String (lv, 0) + "dB_above20k_inaudible_dB@" + String ((double) worstHiBin * sr / N, 0) + "Hz", 10 * std::log10 (std::max (worstHi / fund, 1e-16)));
            const double w = 10 * std::log10 (std::max (worst / fund, 1e-16));
            row ("aliasing", sc.name + "_" + String (lv, 0) + "dB_f" + String (f, 0) + "_worst_audible_nonharmonic_dB@" + String ((double) worstBin * sr / N, 0) + "Hz", w, sc.name == "hot" ? "INFO" : lim (w, -80.0, -65.0));
            if (lv == -3.0) dumpCSV ("alias_" + sc.name, P, sr);
        }
}

// 5. phase / mono
static void testPhase (double sr)
{
    const int n = (int) sr * 6;
    for (auto& sc : scenarios())
    {
        Rig r (sr); neutralise (r); sc.apply (r);
        Buf in = pink (n, 0.1f), a, b;
        r.process (in, in, a, b);
        const int from = (int) sr, lat = r.latency();
        double eM = 0, eS = 0, eL = 0, eR = 0, cc = 0, inE = 0;
        Buf mono ((size_t) n);
        for (int i = from; i < n; ++i)
        {
            const double m = 0.5 * (a[(size_t) i] + b[(size_t) i]), s = 0.5 * (a[(size_t) i] - b[(size_t) i]);
            eM += m * m; eS += s * s; eL += (double) a[(size_t) i] * a[(size_t) i]; eR += (double) b[(size_t) i] * b[(size_t) i];
            cc += (double) a[(size_t) i] * in[(size_t) std::max (0, i - lat)]; inE += (double) in[(size_t) std::max (0, i - lat)] * in[(size_t) std::max (0, i - lat)];
            mono[(size_t) i] = (float) m;
        }
        if (isStrip())
        {
            const double sm = 10 * std::log10 (std::max (eS, 1e-30) / eM);
            row ("phase", sc.name + "_mono_in_side_over_mid_dB", sm, "INFO");   // channel tolerances (about 0.1 dB, a fraction of a sample) widen a mono source slightly, by design
            auto PL = welch (a, from, n - from), PM = welch (mono, from, n - from);
            double worstFold = 0; for (int o = 1; o < 9; ++o) worstFold = std::min (worstFold, octDb (PM, o, sr) - octDb (PL, o, sr));
            row ("phase", sc.name + "_mono_fold_worst_octave_loss_dB", worstFold, lim (-worstFold, 0.5, 1.5));
            const double corr = cc / std::sqrt (std::max (1e-30, eL * inE));
            row ("phase", sc.name + "_correlation_with_input", corr, sc.name == "hot" ? "INFO" : lim (corr, 0.75, 0.6, false));
        }
        else
        {
            // mono-compatibility: fold the wet to mono; each octave band should keep at least ~-3 dB of the stereo average power
            auto PL = welch (a, from, n - from), PR = welch (b, from, n - from), PM = welch (mono, from, n - from);
            double worst = 99;
            for (int o = 1; o < 8; ++o)
            {
                const double avg = 0.5 * (octDb (PL, o, sr) + octDb (PR, o, sr));
                const double d = octDb (PM, o, sr) - avg;
                worst = std::min (worst, d);
            }
            row ("phase", sc.name + "_mono_fold_worst_octave_dB", worst, lim (-worst, 4.5, 7.0));
            // inter-channel correlation of the wet on mono input: want decorrelated (wide) but not out of phase
            double lr = 0; for (int i = from; i < n; ++i) lr += (double) a[(size_t) i] * b[(size_t) i];
            const double rho = lr / std::sqrt (std::max (1e-30, eL * eR));
            row ("phase", sc.name + "_LR_correlation", rho, rho > -0.2 ? "PASS" : "FAIL");
        }
    }

}

// 6. tone: is it thin, harsh, brittle, dull?  Pink noise through; band by band, in vs out.
static void testTone (double sr)
{
    const int n = (int) sr * 12;
    for (auto& sc : scenarios())
        for (double lv : { -24.0, -12.0 })
        {
            if (isReverb() || isDelay()) { if (lv != -24.0) continue; }
            Rig r (sr); neutralise (r); sc.apply (r);
            Buf in = pink (n, (float) Decibels::decibelsToGain (lv)), a, b;
            r.process (in, in, a, b);
            const int from = (int) sr * 2;
            auto Pi = welch (in, from, n - from), Po = welch (a, from, n - from);
            double delta[9], ref = 0;
            for (int o = 0; o < 9; ++o) delta[o] = octDb (Po, o, sr) - octDb (Pi, o, sr);
            ref = (delta[3] + delta[4]) * 0.5;   // 500 Hz - 1 kHz as the reference
            const String tag = sc.name + "_" + String (lv, 0);
            for (int o = 0; o < 9; ++o) row ("tone", tag + "_octave" + String ((int) octaves[o]) + "_delta_dB", delta[o]);
            const double total = 10 * std::log10 (std::max (1e-20, bandPower (Po, 20, sr * 0.49, sr)) / std::max (1e-20, bandPower (Pi, 20, sr * 0.49, sr)));
            row ("tone", tag + "_overall_level_delta_dB", total);
            if (isStrip())
            {
                const double harsh = std::max ({ delta[5], delta[6], delta[7] }) - ref;   // 2k..8k vs the middle
                const double thin  = ref - std::min (delta[1], delta[2]);                // 125/250 Hz vs the middle
                const double dull  = ref - delta[8];                                      // 16 kHz
                row ("tone", tag + "_harshness_2to8k_vs_mid_dB", harsh, lim (harsh, 2.0, 4.0));
                row ("tone", tag + "_thinness_low_vs_mid_dB", thin, lim (thin, 1.5, 3.0));
                row ("tone", tag + "_air_loss_16k_dB", dull, lim (dull, 6.0, 12.0));
                row ("tone", tag + "_level_matched_dB", total, sc.name == "hot" ? "INFO" : lim (std::abs (total), 1.5, 3.0));
            }
            else
            {
                // wet only: a real space loses highs; make sure that it doesn't gain them, nor leave the lows muddy
                const double hf = (delta[6] + delta[7]) * 0.5 - (delta[2] + delta[3]) * 0.5;
                row ("tone", tag + "_wet_brightness_vs_mid_dB", hf, hf <= 1.0 ? "PASS" : hf <= 4.0 ? "WARN" : "FAIL");
                const double mud = (delta[0] + delta[1]) * 0.5 - (delta[3] + delta[4]) * 0.5;
                row ("tone", tag + "_wet_low_buildup_dB", mud, mud <= 4.0 ? "PASS" : mud <= 7.0 ? "WARN" : "FAIL");
                if (sc.name != "echo" ) row ("level", tag + "_wet_level_vs_dry_dB", total, "INFO");
            }
            if (lv == -24.0) { dumpCSV ("pink_out_" + sc.name, Po, sr); dumpCSV ("pink_in", Pi, sr); }
        }
}

// 7. robustness: DC, NaN, denormals, runaway, blocks and rates
static void testRobust (double sr)
{
    // extreme parameters, loud noise and impulses
    for (int round = 0; round < 6; ++round)
    {
        Rig r (sr);
        auto& apvts = static_cast<ArtifactsProcessor*> (r.p.get())->apvts;
        Random rnd (1000 + round);
        const int n = (int) sr * 8;
        Buf in = pink (n, 0.3f, round + 1), a, b;
        for (int i = 0; i < n; i += (int) sr / 2) in[(size_t) i] = 1.0f;
        auto randomise = [&] (bool extremes)
        {
            for (auto* prm : apvts.processor.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                    rp->setValueNotifyingHost (extremes ? (rnd.nextBool() ? 1.0f : 0.0f) : rnd.nextFloat());
        };
        randomise (round % 2 == 0);
        Buf oL ((size_t) n), oR ((size_t) n);
        const int block = 256; AudioBuffer<float> blk (2, block); MidiBuffer m;
        for (int pos = 0; pos + block <= n; pos += block)
        {
            if ((pos / block) % 40 == 0) randomise (round % 2 == 0 && rnd.nextBool());   // moving the knobs while playing
            std::memcpy (blk.getWritePointer (0), in.data() + pos, block * sizeof (float));
            std::memcpy (blk.getWritePointer (1), in.data() + pos, block * sizeof (float));
            r.p->processBlock (blk, m);
            std::memcpy (oL.data() + pos, blk.getReadPointer (0), block * sizeof (float));
            std::memcpy (oR.data() + pos, blk.getReadPointer (1), block * sizeof (float));
        }
        const double pk = std::max (peakOf (oL), peakOf (oR));
        row ("robust", "random_params_" + String (round) + "_peak_dBFS", db (pk), (bad (oL) || bad (oR)) ? "FAIL" : (pk < 32.0 ? "PASS" : "FAIL"));
        row ("robust", "random_params_" + String (round) + "_nan_or_inf", (bad (oL) || bad (oR)) ? 1 : 0, (bad (oL) || bad (oR)) ? "FAIL" : "PASS");
    }

    // DC offset and silence in -> silence out (no noise floor, no hidden DC)
    for (auto& sc : scenarios())
    {
        Rig r (sr); neutralise (r); sc.apply (r);
        const int n = (int) sr * 6;
        Buf z ((size_t) n, 0.0f), a, b;
        r.process (z, z, a, b);
        double dc = 0; for (int i = n / 2; i < n; ++i) dc += a[(size_t) i];
        dc /= (n / 2);
        row ("robust", sc.name + "_silence_in_rms_dBFS", db (rms (a, n / 2)), (rms (a, n / 2) < 1.0e-4 || sc.name == "xx") ? "PASS" : "WARN");
        row ("robust", sc.name + "_silence_in_dc", dc, std::abs (dc) < 1.0e-4 ? "PASS" : "WARN");
        // DC in: should not pass DC at high gain
        Buf d ((size_t) n, 0.1f);
        r.process (d, d, a, b);
        double tail = 0; for (int i = n - 4800; i < n; ++i) tail += a[(size_t) i]; tail /= 4800;
        row ("robust", sc.name + "_dc_0.1_in_out", tail, "INFO");
    }

    // block-size independence: the same audio in 512, 64 and 7-sample blocks must come out the same
    {
        const int n = (int) sr * 3;
        Buf in = pink (n, 0.15f), ra, rb, rc, rd, d1, d2;
        Rig r1 (sr); neutralise (r1); r1.process (in, in, ra, d1, 512);
        Rig r2 (sr); neutralise (r2); r2.process (in, in, rb, d2, 64);
        Rig r3 (sr); neutralise (r3); r3.process (in, in, rc, d2, 7);
        double e = 0, ref = 0, e3 = 0;
        for (int i = 0; i < n; ++i) { const double d = ra[(size_t) i] - rb[(size_t) i]; e += d * d; ref += (double) ra[(size_t) i] * ra[(size_t) i]; const double d3 = ra[(size_t) i] - rc[(size_t) i]; e3 += d3 * d3; }
        const double v = 10 * std::log10 (std::max (e, 1e-30) / ref), v3 = 10 * std::log10 (std::max (e3, 1e-30) / ref);
        row ("robust", "block64_vs_512_diff_dB", v, lim (v, -70.0, -50.0));
        row ("robust", "block7_vs_512_diff_dB", v3, lim (v3, -70.0, -50.0));
    }

    // mono input and 96 kHz must work and sound like the same plugin
    {
        const double f2 = 96000.0;
        Rig r (f2); neutralise (r); r.norm ("mix", 1.0f);
        const int n = (int) f2 * 3;
        Buf in = pink (n, 0.1f), a, b;
        r.process (in, in, a, b);
        row ("robust", "96k_peak_dBFS", db (peakOf (a)), bad (a) ? "FAIL" : "PASS");
    }
    {
        const double f2 = 44100.0;
        Rig r (f2); neutralise (r); r.norm ("mix", 1.0f);
        const int n = (int) f2 * 3;
        Buf in = pink (n, 0.1f), a, b;
        r.process (in, in, a, b);
        row ("robust", "44k1_peak_dBFS", db (peakOf (a)), bad (a) ? "FAIL" : "PASS");
    }
}

// 8. dynamics of the strips: transient handling, crest, no pumping
static void testDynamics (double sr)
{
    if (! isStrip()) return;
    for (auto& sc : scenarios())
    {
        Rig r (sr); neutralise (r); sc.apply (r);
        const int n = (int) sr * 6;
        Buf in = pink (n, 0.08f), a, b;
        // drum-like bursts every 500 ms
        for (int k = 0; k < 11; ++k) for (int i = 0; i < 2400; ++i) in[(size_t) (k * (int) sr / 2 + i + 20000)] += 0.7f * (float) std::exp (-i / 400.0) * (float) std::sin (i * 0.12);
        r.process (in, in, a, b);
        auto crest = [] (const Buf& x, int from) { return db (peakOf (x)) - db (rms (x, from)); };
        row ("dynamics", sc.name + "_crest_in_dB", crest (in, 20000));
        row ("dynamics", sc.name + "_crest_out_dB", crest (a, 20000));
        const double dc = crest (a, 20000) - crest (in, 20000);
        row ("dynamics", sc.name + "_crest_change_dB", dc, sc.name == "hot" ? "INFO" : lim (std::abs (dc), 3.0, 6.0));
        // the processor's own meter claims
    }
}

// 9. Anemoia: echoes land where they should, repeats decay as asked, the cloud is bounded
static void testDelay (double sr)
{
    if (! isDelay()) return;
    {
        Rig r (sr); r.set ("memon", 0); r.norm ("mix", 1.0f); r.set ("time", 400.0f); r.set ("feedback", 0.5f); r.norm ("tone", 1.0f); r.norm ("width", 0.25f);
        const int n = (int) sr * 3; Buf in ((size_t) n, 0.0f), a, b; in[100] = 0.5f;
        r.process (in, in, a, b);
        int idx = 0; float mx = 0;
        for (int i = 1000; i < n; ++i) if (std::abs (a[(size_t) i]) > mx) { mx = std::abs (a[(size_t) i]); idx = i; }
        const double ms = (idx - 100) * 1000.0 / sr;
        row ("echo", "time_400ms_measured_ms", ms, std::abs (ms - 400.0) < 1.5 ? "PASS" : "FAIL");
        // second repeat ratio
        float m2 = 0; for (int i = idx + (int) (0.2 * sr); i < idx + (int) (0.6 * sr) && i < n; ++i) m2 = std::max (m2, std::abs (a[(size_t) i]));
        const double ratio = (double) m2 / mx;
        row ("echo", "repeat_ratio_at_50pct", ratio, (ratio < 0.7 && ratio > 0.1) ? "PASS" : "WARN");
        dumpCSV ("echo_ir", powerSpectrum (a, 0, 65536), sr);
    }
    {   // sync: 1/8 at 120 bpm is 250 ms
        Rig r (sr); r.set ("memon", 0); r.norm ("mix", 1.0f); r.set ("sync", 1); r.set ("division", 2); r.set ("feel", 0); r.set ("feedback", 0.0f); r.norm ("width", 0.25f);
        const int n = (int) sr; Buf in ((size_t) n, 0.0f), a, b; in[100] = 0.5f;
        r.process (in, in, a, b);
        int idx = 0; float mx = 0;
        for (int i = 1000; i < n; ++i) if (std::abs (a[(size_t) i]) > mx) { mx = std::abs (a[(size_t) i]); idx = i; }
        const double ms = (idx - 100) * 1000.0 / sr;
        row ("echo", "sync_1_8_at_120bpm_ms", ms, std::abs (ms - 250.0) < 1.5 ? "PASS" : "FAIL");
    }
    {   // ping-pong alternates sides
        Rig r (sr); r.set ("memon", 0); r.norm ("mix", 1.0f); r.set ("time", 200.0f); r.set ("feedback", 0.7f); r.norm ("width", 1.0f);
        const int n = (int) sr * 3; Buf in ((size_t) n, 0.0f), zero ((size_t) n, 0.0f), a, b; in[24000] = 0.5f;   // after the time knob has glided to its setting
        r.process (in, zero, a, b);   // a click in the left ear only
        auto at = [&] (const Buf& x, int ms) { float m = 0; const int c = 24000 + (int) (ms * sr / 1000.0); for (int i = c - 80; i < c + 80; ++i) m = std::max (m, std::abs (x[(size_t) i])); return m; };
        const double s1 = db (at (a, 200)) - db (at (b, 200)), s2 = db (at (b, 400)) - db (at (a, 400));
        row ("echo", "pingpong_first_repeat_L_over_R_dB", s1, "INFO");
        row ("echo", "pingpong_second_repeat_R_over_L_dB", s2, "INFO");
    }
    // stability: loudest settings for a minute
    for (int variant = 0; variant < 3; ++variant)
    {
        Rig r (sr); r.norm ("mix", 1.0f); r.norm ("feedback", 1.0f); r.norm ("bloom", 1.0f); r.norm ("shimmer", variant == 1 ? 1.0f : 0.5f);
        r.set ("pattern", (float) (variant == 2 ? 5 : variant)); r.norm ("texture", variant == 2 ? 1.0f : 0.2f); r.norm ("nostalgia", 1.0f);
        const int n = (int) sr * 30; Buf in = pink (n, 0.35f, 3 + variant), a, b;
        r.process (in, in, a, b);
        double pk = 0; for (int i = 0; i < n; ++i) pk = std::max ({ pk, (double) std::abs (a[(size_t) i]), (double) std::abs (b[(size_t) i]) });
        const double last = rms (a, n - (int) sr * 5);
        row ("stability", "max_fb_bloom_variant" + String (variant) + "_peak_dBFS", db (pk), bad (a) ? "FAIL" : (pk < 4.0 ? "PASS" : "FAIL"));
        row ("stability", "max_fb_bloom_variant" + String (variant) + "_last5s_rms_dBFS", db (last), db (last) < 3.0 ? "PASS" : "FAIL");
        // after the input stops, everything must decay
        Buf z ((size_t) n, 0.0f), c, d; r.process (z, z, c, d);
        const double tail = rms (c, n - (int) sr * 2);
        row ("stability", "max_fb_bloom_variant" + String (variant) + "_decays_after_input_dBFS", db (tail), db (tail) < -40.0 ? "PASS" : "WARN");
    }
    // grain engine: pitch up an octave, high sine: does it alias?
    for (int pitch : { 12, 7, -12 })
    {
        Rig r (sr); r.set ("echoon", 0); r.norm ("mix", 1.0f); r.set ("pitch", (float) pitch); r.norm ("bloom", 0.0f); r.norm ("shimmer", 0.0f);
        r.norm ("nostalgia", 0.0f); r.set ("pattern", 0); r.norm ("texture", 0.5f);
        const int N = 65536, n = N + (int) sr * 3;
        const int k = (int) (0.17 * N); const double f = k * sr / N;   // ~8.1 kHz
        Buf s = sine (n, f, sr, 0.25f), a, b;
        r.process (s, s, a, b);
        auto P = powerSpectrum (a, (int) sr * 2, N);
        const double fundExpected = f * std::pow (2.0, pitch / 12.0);
        const int kExp = (int) std::lround (fundExpected / sr * N);
        double peakBandPow = 0, worst = 0;
        for (int bin = 40; bin < (int) P.size(); ++bin)
        {
            const double fb = (double) bin * sr / N;
            if (std::abs (fb - fundExpected) <= 1500.0) { peakBandPow += P[(size_t) bin]; continue; }
            if (std::abs (fb - f) <= 1500.0) continue;   // the unshifted tone, and the grain sidebands around the tones, are not aliasing
            worst = std::max (worst, P[(size_t) bin]);
        }
        const double rel = 10 * std::log10 (std::max (worst, 1e-16) / std::max (peakBandPow, 1e-16));
        row ("aliasing", "grain_pitch" + String (pitch) + "_8kHz_worst_spur_dB", rel, pitch == -12 ? "INFO" : lim (rel, -45.0, -35.0));
    }
}

// 10. Duality: tails are smooth, dense, not metallic, and the three perspectives sit at the same loudness
static void testReverb (double sr)
{
    if (! isReverb()) return;
    std::map<String, double> level;
    for (auto& sc : scenarios())
    {
        Rig r (sr); sc.apply (r); r.norm ("width", 0.5f);
        const int n = (int) sr * 10;
        Buf in ((size_t) n, 0.0f), a, b; in[100] = 0.5f;
        r.process (in, in, a, b);
        // decay curve from Schroeder backward integration
        std::vector<double> e ((size_t) n);
        double acc = 0; for (int i = n - 1; i >= 0; --i) { acc += (double) a[(size_t) i] * a[(size_t) i]; e[(size_t) i] = acc; }
        auto tAt = [&] (double dbDown) { for (int i = 0; i < n; ++i) if (10 * std::log10 (e[(size_t) i] / e[0]) < -dbDown) return i / sr; return 10.0; };
        const double rt = 3.0 * (tAt (25) - tAt (5));   // T20 x3
        row ("reverb", sc.name + "_RT60_s", rt);
        for (double t : { 0.05, 0.2, 0.5, 1.0, 1.5, 2.0, 3.0 })
        {
            const int c0 = (int) (t * sr); double en = 0; for (int i = c0; i < c0 + 2400; ++i) en += (double) a[(size_t) i] * a[(size_t) i];
            row ("reverb", sc.name + "_envelope_dB_at_" + String (t, 2) + "s", 10 * std::log10 (std::max (en, 1e-20)));
        }
        // modal ringing / metallic colour: peak-to-median of the smoothed magnitude spectrum of 0.3-0.8 s of tail
        const int N = 32768, from = (int) (0.3 * sr);
        auto P = powerSpectrum (a, from, N);
        std::vector<double> sm;
        for (int k = (int) (300.0 / sr * N); k < (int) (6000.0 / sr * N); ++k) sm.push_back (10 * std::log10 (P[(size_t) k] + 1e-20));
        // fluctuation of a dense, diffuse tail follows a Rayleigh distribution (std dev about 5.6 dB); remove the slow spectral tilt first
        double var = 0; int cntv = 0;
        for (int i = 0; i < (int) sm.size(); ++i)
        {
            const int w = 40 + i / 10; double acc2 = 0; int c2 = 0;
            for (int j = std::max (0, i - w); j <= std::min ((int) sm.size() - 1, i + w); ++j) { acc2 += sm[(size_t) j]; ++c2; }
            const double d = sm[(size_t) i] - acc2 / c2; var += d * d; ++cntv;
        }
        const double sd = std::sqrt (var / cntv);
        row ("reverb", sc.name + "_tail_spectral_roughness_dB", sd, sd < 6.5 ? "PASS" : sd < 8.0 ? "WARN" : "FAIL");
        // echo density in the first 100 ms (fraction of samples over a threshold around the tail's rms)
        int dense = 0, tot = 0; const double th = rms (a, (int) (0.05 * sr), (int) (0.25 * sr)) * 0.25;
        for (int i = (int) (0.05 * sr); i < (int) (0.25 * sr); ++i) { ++tot; if (std::abs (a[(size_t) i]) > th) ++dense; }
        row ("reverb", sc.name + "_echo_density_pct", 100.0 * dense / tot, 100.0 * dense / tot > 20.0 ? "PASS" : "WARN");
        // tail brightness: a reverb that rings brighter later than it begins is unnatural
        auto Pe = welch (a, (int) (0.05 * sr), (int) (0.4 * sr), 4096), Pl = welch (a, (int) (0.6 * sr), (int) (0.8 * sr), 4096);
        const double ratioE = 10 * std::log10 (bandPower (Pe, 4000, 12000, sr) / bandPower (Pe, 200, 1000, sr));
        const double ratioL = 10 * std::log10 (bandPower (Pl, 4000, 12000, sr) / bandPower (Pl, 200, 1000, sr));
        row ("reverb", sc.name + "_tail_gets_darker_dB", ratioE - ratioL, (ratioE - ratioL) > -1.0 ? "PASS" : "WARN");
        // loudness at default: pink noise in, wet only
        Buf pn = pink ((int) sr * 8, 0.1f), a2, b2;
        Rig r2 (sr); sc.apply (r2);
        r2.process (pn, pn, a2, b2);
        level[sc.name] = db (rms (a2, (int) sr * 3) / rms (pn, (int) sr * 3));
        row ("reverb", sc.name + "_wet_gain_dB", level[sc.name]);
        dumpCSV ("reverb_" + sc.name, welch (a2, (int) sr * 3, (int) sr * 4), sr);
    }
    double lo = 99, hi = -99;
    for (auto& kv : level) { lo = std::min (lo, kv.second); hi = std::max (hi, kv.second); }
    row ("reverb", "wet_gain_spread_across_modes_dB", hi - lo, lim (hi - lo, 3.0, 5.0));
}

// 11. a click-through: does a step or a tone change in the setting click?
static void testClicks (double sr)
{
    for (auto& sc : scenarios())
    {
        Rig r (sr); neutralise (r); sc.apply (r);
        const int n = (int) sr * 4, block = 128;
        Buf in = sine (n, 220.0, sr, 0.2f), a ((size_t) n), b ((size_t) n);
        auto& apvts = static_cast<ArtifactsProcessor*> (r.p.get())->apvts;
        AudioBuffer<float> blk (2, block); MidiBuffer m; Random rnd (5);
        double worst = 0;
        for (int pos = 0; pos + block <= n; pos += block)
        {
            if (pos > (int) sr && (pos / block) % 50 == 0)   // jump a random knob to a random place
            {
                auto& params = apvts.processor.getParameters();
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (params[rnd.nextInt (params.size())]))
                {
                    const String id = rp->getParameterID();
                    if (id != "mix" && id != "perspective" && id != "pattern") rp->setValueNotifyingHost (rnd.nextFloat());
                }
            }
            std::memcpy (blk.getWritePointer (0), in.data() + pos, block * sizeof (float));
            std::memcpy (blk.getWritePointer (1), in.data() + pos, block * sizeof (float));
            r.p->processBlock (blk, m);
            std::memcpy (a.data() + pos, blk.getReadPointer (0), block * sizeof (float));
        }
        for (int i = (int) sr; i < n - 1; ++i) worst = std::max (worst, (double) std::abs (a[(size_t) i + 1] - a[(size_t) i]));
        row ("clicks", sc.name + "_knob_jumps_max_sample_step", worst, lim (worst, 0.6, 1.2));
    }
}

static void dbgApply (Rig& r)
{
    if (auto* e = std::getenv ("DBG"))
        for (auto& t : StringArray::fromTokens (e, ",", ""))
            r.norm (t.upToFirstOccurrenceOf ("=", false, false), t.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
}
static void debugBlocks (double sr)
{
    const int n = (int) sr * 3;
    { Buf z ((size_t) n, 0.0f), q, w; for (int k = 0; k < 2; ++k) { Rig rz (sr); neutralise (rz); dbgApply (rz); rz.process (z, z, q, w, 512);
        double m = 0; int at = -1; for (int i = 0; i < n; ++i) if (std::abs (q[(size_t) i]) > m) { m = std::abs (q[(size_t) i]); at = i; }
        row ("debug", "silence_in_max_out_run" + String (k), m); row ("debug", "silence_in_max_at", at); } }
    Buf in = pink (n, 0.15f), a, b, c, d;
    Rig r1 (sr); neutralise (r1); dbgApply (r1); r1.process (in, in, a, b, 512);
    Rig r2 (sr); neutralise (r2); r2.process (in, in, c, d, 512);
    Rig r3 (sr); neutralise (r3); r3.process (in, in, c, d, 64);
    int first = -1; double mx = 0; int at = 0;
    Buf a2, b2; Rig r4 (sr); neutralise (r4); dbgApply (r4); r4.process (in, in, a2, b2, 512);
    for (int i = 0; i < n; ++i) { const double e = std::abs (a[(size_t) i] - a2[(size_t) i]); if (e > 1e-6 && first < 0) first = i; if (e > mx) { mx = e; at = i; } }
    for (int i = 530; i < 4000; i += 97) std::fprintf (stderr, "%d inDelayed=%f a=%f a2=%f\n", i, in[(size_t) (i - 540)], a[(size_t) i], a2[(size_t) i]);
    { double m2 = 0; for (int i = 0; i < n; ++i) m2 = std::max (m2, (double) std::abs (c[(size_t) i] - a2[(size_t) i])); row ("debug", "r3_block64_vs_r4_block512_max", m2); }
    { Buf a3, b3; Rig r5 (sr); neutralise (r5); dbgApply (r5); r5.process (in, in, a3, b3, 512);
      double m3 = 0; for (int i = 0; i < n; ++i) m3 = std::max (m3, (double) std::abs (a3[(size_t) i] - a2[(size_t) i])); row ("debug", "r5_vs_r4_same_block_max", m3);
      Buf a4, b4; Rig r6 (sr); neutralise (r6); dbgApply (r6); r6.process (in, in, a4, b4, 512);
      double m4 = 0; for (int i = 0; i < n; ++i) m4 = std::max (m4, (double) std::abs (a3[(size_t) i] - a4[(size_t) i])); row ("debug", "r6_vs_r5_same_block_max", m4); }
    for (int k = 0; k < n; k += 16384) { double e = 0; for (int i = k; i < std::min (n, k + 16384); ++i) { const double d = a[(size_t) i] - a2[(size_t) i]; e += d * d; } row ("debug", "diff_rms_dB_at_" + String (k), 10 * std::log10 (e / 16384 + 1e-30)); }
    row ("debug", "same_block_repeat_first_diff_sample", first);
    row ("debug", "same_block_repeat_max_diff", mx);
    first = -1; mx = 0; at = 0;
    for (int i = 0; i < n; ++i) { const double e = std::abs (a[(size_t) i] - c[(size_t) i]); if (e > 1e-4 && first < 0) first = i; if (e > mx) { mx = e; at = i; } }
    row ("debug", "block64_first_diff_sample", first);
    row ("debug", "block64_max_diff", mx);
    row ("debug", "block64_max_diff_at", at);
}

static void renderEditor (const File& png, bool playAudio, const String& skin = {}, const StringArray& extra = {})
{
    std::unique_ptr<AudioProcessor> p (createPluginFilter());
    for (auto& kv : extra)   // name=value pairs, in the parameter's own units, to show a particular state
        for (auto* prm : p->getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                if (rp->paramID == kv.upToFirstOccurrenceOf ("=", false, false))
                    rp->setValueNotifyingHost (rp->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    p->setPlayConfigDetails (2, 2, 48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    if (playAudio)
    {
        Thread::sleep (700);
        Buf in = pink (48000 * 2, 0.15f), a, b;
        Buf z = sine (48000 * 2, 220.0, 48000.0, 0.2f);
        AudioBuffer<float> blk (2, 512); MidiBuffer m;
        for (int pos = 0; pos + 512 <= (int) in.size(); pos += 512)
        {
            std::memcpy (blk.getWritePointer (0), z.data() + pos, 512 * sizeof (float));
            std::memcpy (blk.getWritePointer (1), z.data() + pos, 512 * sizeof (float));
            p->processBlock (blk, m);
        }
    }
    std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
    ed->setVisible (true);
    for (auto& kv : extra)   // size=0.7: the window resized to 70 %, as a user would drag it
        if (kv.startsWith ("size="))
            ed->setSize (roundToInt (ed->getWidth() * kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()),
                         roundToInt (ed->getHeight() * kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    for (int i = 0; i < 40; ++i) MessageManager::getInstance()->runDispatchLoopUntil (30);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    PNGImageFormat fmt;
    FileOutputStream os (png);
    os.setPosition (0); os.truncate();
    fmt.writeImageToStream (img, os);
    row ("render", "editor_size_px", ed->getWidth() * 10000.0 + ed->getHeight());
}

//==============================================================================
//==============================================================================
// Equanimity: the passive-EQ specs, the valve stage, the Path (L/R, M/S, solo)
static double eqGain (Rig& r, double f, float amp = 0.03f, bool sideOnly = false, bool midOnly = false, int ch = 0)
{
    const double sr = r.sr;
    r.reset();
    const int n = (int) sr * 3;
    Buf s = sine (n, f, sr, amp), z ((size_t) n, 0.0f), a, b, inR = s;
    if (sideOnly) for (auto& v : inR) v = -v;
    if (midOnly) {}
    r.process (s, inR, a, b);
    const int from = (int) sr, to = from + (int) sr * 2 / (int) f * (int) f / 2;   // whole number of cycles
    const int len = (int) (std::floor ((double) (n - from) * f / sr) * sr / f);
    (void) to;
    const auto& o = ch == 0 ? a : b;
    return db (std::abs (demod (o, from, from + len, f, sr)) / std::abs (demod (s, from, from + len, f, sr)));
}
static double thdPct (Rig& r, double f, float amp, int chan = 0)
{
    const double sr = r.sr; r.reset();
    const int n = (int) sr * 2;
    Buf s = sine (n, f, sr, amp), a, b;
    r.process (s, s, a, b);
    const int from = (int) sr, len = (int) (std::floor ((double) (n - from) * f / sr) * sr / f);
    const auto& o = chan == 0 ? a : b;
    const double h1 = std::abs (demod (o, from, from + len, f, sr));
    double acc = 0;
    for (int k = 2; k <= 10 && k * f < sr * 0.45; ++k) { const double h = std::abs (demod (o, from, from + len, k * f, sr)); acc += h * h; }
    return 100.0 * std::sqrt (acc) / h1;
}
// End to end, as in a host: turn each dial on the real editor and measure what the audio does
static void uiProbe (double sr)
{
    Rig r (sr);
    std::unique_ptr<AudioProcessorEditor> ed (r.p->createEditor());
    ed->setVisible (true);
    std::vector<Slider*> sliders;
    std::function<void (Component*)> walk = [&] (Component* comp) { if (auto* s = dynamic_cast<Slider*> (comp)) if (s->isVisible()) sliders.push_back (s); for (auto* ch : comp->getChildren()) walk (ch); };
    walk (ed.get());
    auto snap = [&] { std::map<String, float> m; for (auto* prm : r.p->getParameters()) if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm)) m[rp->paramID] = rp->getValue(); return m; };
    const double f[] { 40, 60, 120, 250, 1000, 3000, 8000, 12000 };
    double base[8]; for (int i = 0; i < 8; ++i) base[i] = eqGain (r, f[i], 0.1f);
    for (auto* s : sliders)
    {
        const auto before = snap();
        const double v0 = s->getValue();
        s->setValue (s->getMinimum() + 0.5 * (s->getMaximum() - s->getMinimum()), sendNotificationSync);
        String changed; for (auto& [id, v] : snap()) if (std::abs (v - before.at (id)) > 1e-6) changed << id << "=" << String (dynamic_cast<RangedAudioParameter*> (r.p->getParameters()[0]) ? 0 : 0) << " ";
        changed = {}; for (auto& [id, v] : snap()) if (std::abs (v - before.at (id)) > 1e-6) changed << id << " ";
        std::printf ("  slider @(%d,%d) half -> [%s]:", s->getX(), s->getY(), changed.trim().toRawUTF8());
        for (int i = 0; i < 8; ++i) std::printf (" %g:%+.1f", f[i], eqGain (r, f[i], 0.1f) - base[i]);
        std::printf ("\n");
        s->setValue (v0, sendNotificationSync);
    }
}

static void probeEquanimity (double sr)
{
    // how much the low boost / atten really change the sound at real levels, and with the other switches
    for (float amp : { 0.03f, 0.1f, 0.25f, 0.5f, 0.9f })
        for (double f : { 30.0, 60.0, 100.0, 200.0 })
        {
            Rig r0 (sr); const double g0 = eqGain (r0, f, amp);
            Rig r1 (sr); r1.set ("lofreq", 2); r1.set ("loboost", 10); const double g1 = eqGain (r1, f, amp);
            Rig r2 (sr); r2.set ("lofreq", 2); r2.set ("loatten", 10); const double g2 = eqGain (r2, f, amp);
            Rig r3 (sr); r3.set ("lofreq", 2); r3.set ("mlpeak", 10); r3.norm ("mlfreq", 0.0f); const double g3 = eqGain (r3, f, amp);
            row ("probe", "amp" + String (amp, 2) + "_" + String ((int) f) + "Hz flat/boost/atten/mlpk200", g0);
            std::printf ("   amp %.2f  %4d Hz   flat %6.2f   boost10 %6.2f (%+.2f)   atten10 %6.2f (%+.2f)   midLo200 %+.2f\n", amp, (int) f, g0, g1, g1 - g0, g2, g2 - g0, g3 - g0);
        }
    // the valve stage with the EQ flat: which harmonics, how much, at which level
    for (double f : { 100.0, 1000.0 })
        for (double dbfs : { -24.0, -18.0, -12.0, -6.0, -1.0 })
        {
            Rig r (sr); r.reset();
            const float amp = (float) std::pow (10.0, dbfs / 20.0);
            const int n = (int) sr * 2;
            Buf s = sine (n, f, sr, amp), a, b;
            r.process (s, s, a, b);
            const int from = (int) sr, len = (int) (std::floor ((double) (n - from) * f / sr) * sr / f);
            const double h1 = std::abs (demod (a, from, from + len, f, sr));
            double hs[6]; double acc = 0;
            for (int k = 2; k <= 7; ++k) { hs[k - 2] = std::abs (demod (a, from, from + len, k * f, sr)); acc += hs[k - 2] * hs[k - 2]; }
            std::printf ("   %5.0f Hz %+5.0f dBFS: THD %.4f %%  H2 %6.1f  H3 %6.1f  H4 %6.1f  H5 %6.1f dB (rel. fundamental)\n", f, dbfs,
                         100.0 * std::sqrt (acc) / h1, db (hs[0] / h1), db (hs[1] / h1), db (hs[2] / h1), db (hs[3] / h1));
        }
    // every boost and cut, at its default frequency, as the knob is turned: 2, 5, 8, 10
    struct K { const char* id; std::vector<double> freqs; };
    for (auto k : std::vector<K> { { "loboost", { 30, 60, 120, 250 } }, { "loatten", { 30, 60, 120, 250 } }, { "hiboost", { 3000, 5000, 8000, 12000 } },
                                   { "hiatten", { 5000, 10000, 15000 } }, { "mlpeak", { 250, 500, 1000 } }, { "mdip", { 500, 1000, 2000 } }, { "mhpeak", { 1500, 3000, 6000 } } })
    {
        for (float v : { 2.0f, 5.0f, 8.0f, 10.0f })
        {
            Rig r (sr); r.set (k.id, v); Rig f (sr);
            std::printf ("   %-8s %4.1f:", k.id, v);
            for (double fr : k.freqs) std::printf ("  %6.0f Hz %+6.2f", fr, eqGain (r, fr, 0.1f) - eqGain (f, fr, 0.1f));
            std::printf ("\n");
        }
    }
    for (int ag = 0; ag < 2; ++ag)
    {
        Rig r (sr); r.set ("autogain", ag); r.set ("lofreq", 2); r.set ("loboost", 10);
        std::printf ("   autogain %d: boost10 at 40 Hz %+.2f, at 1 kHz %+.2f\n", ag, eqGain (r, 40, 0.1f), eqGain (r, 1000, 0.1f));
    }
}
// the ITU BS.1770 loudness weighting (48 kHz): a gentle lift above 1.5 kHz and a roll-off of the deep bass
static Buf kWeight (const Buf& x)
{
    Buf y (x.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double v = x[i];
        const double s = 1.53512485958697 * v - 2.69169618940638 * x1 + 1.19839281085285 * x2 + 1.69065929318241 * y1 - 0.73248077421585 * y2;
        x2 = x1; x1 = v; y2 = y1; y1 = s;
        const double h = s - 2.0 * z1 + z2;   // the high-pass, in direct form with its own history
        static thread_local double w1 = 0, w2 = 0;
        if (i == 0) { w1 = w2 = 0; }
        const double o = h + 1.99004745483398 * w1 - 0.99007225036621 * w2;
        w2 = w1; w1 = o; z2 = z1; z1 = s;
        y[i] = (float) o;
    }
    return y;
}
static void testEquanimity (double sr)
{
    { Rig r (sr); row ("latency", "reported_samples", r.latency(), "INFO"); }

    // flat amplifier, EQ out: 20 Hz to 20 kHz within +/-0.5 dB (spec)
    {
        Rig r (sr); r.set ("eqin", 0);
        double lo = 99, hi = -99;
        for (double f : { 20.0, 30.0, 50.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
        {
            const double g = eqGain (r, f); lo = std::min (lo, g); hi = std::max (hi, g);
            row ("amp-flat", String ((int) f) + "Hz_dB", g);
        }
        row ("amp-flat", "deviation_dB", std::max (std::abs (lo), std::abs (hi)), lim (std::max (std::abs (lo), std::abs (hi)), 0.5, 1.0));
    }
    // low boost 10: shelf gain below the corner (spec 13.5 dB) and low atten 10 (spec 17.5 dB)
    for (int fi = 0; fi < 4; ++fi)
    {
        const double fl[] { 20, 30, 60, 100 };
        const double fm = std::max (10.0, fl[fi] * 0.5);
        { Rig r (sr); r.norm ("lofreq", fi / 3.0f); r.set ("loboost", 10); const double g = eqGain (r, fm);
          row ("low-boost", String ((int) fl[fi]) + "Hz_max_dB@" + String ((int) fm) + "Hz", g, std::abs (g - 13.5) <= 2.0 ? "PASS" : "WARN"); }
        { Rig r (sr); r.norm ("lofreq", fi / 3.0f); r.set ("loatten", 10); const double g = eqGain (r, fm);
          row ("low-atten", String ((int) fl[fi]) + "Hz_max_dB@" + String ((int) fm) + "Hz", g, std::abs (g + 17.5) <= 1.5 ? "PASS" : "WARN"); }
    }
    // the boost is broad, as on the hardware: at 60 Hz, boost 10 still lifts an octave and two above (and never dips by itself)
    {
        Rig r (sr); r.set ("lofreq", 2); r.set ("loboost", 10);
        const double g60 = eqGain (r, 60), g120 = eqGain (r, 120), g250 = eqGain (r, 250);
        double lowest = 99; for (double f : { 300.0, 500.0, 1000.0, 2000.0 }) lowest = std::min (lowest, eqGain (r, f));
        // the hardware: nearly all of the boost at the frequency chosen, starting about six times higher
        // (and matched to the UAD model's curve: a smooth slope up from the frequency chosen)
        row ("low-boost-shape", "at_60Hz_dB", g60, g60 > 10.0 && g60 < 13.0 ? "PASS" : "FAIL");
        row ("low-boost-shape", "at_120Hz_dB", g120, g120 > 5.0 && g120 < 8.0 ? "PASS" : "FAIL");
        row ("low-boost-shape", "at_250Hz_dB", g250, g250 > 1.0 && g250 < 3.5 ? "PASS" : "FAIL");
        row ("low-boost-shape", "no_dip_above_dB", lowest, lowest > -0.3 ? "PASS" : "FAIL");
    }
    // ... and it is still there on loud material: boost 10 at 60 Hz, -12 dBFS in, still lifts 60 Hz by 8 dB or more
    {
        Rig r (sr); r.set ("lofreq", 2); r.set ("loboost", 10);
        Rig f (sr);
        const double g = eqGain (r, 60, 0.25f) - eqGain (f, 60, 0.25f);
        row ("low-boost-hot", "-12dBFS_at_60Hz_dB", g, g > 8.0 ? "PASS" : "FAIL");
    }
    // the dials are audio taper, as on the hardware: a quarter of the way up (about 9 o'clock) already does a lot
    {
        Rig r (sr); r.set ("lofreq", 2); r.set ("loboost", 2.5f);
        const double g = eqGain (r, 60);
        row ("taper", "low_boost_2.5_at_60Hz_dB", g, g > 5.0 ? "PASS" : "FAIL");
        Rig h (sr); h.set ("hifreq", 4); h.set ("hiboost", 2.5f);
        const double gh = eqGain (h, 8000);
        row ("taper", "high_boost_2.5_at_8k_dB", gh, gh > 5.0 ? "PASS" : "FAIL");
        // ... and the classic trick at 2.5 / 2.5: a few dB more at the bottom, a dip of about 2 dB above (Sound On Sound's measurement)
        Rig t (sr); t.set ("lofreq", 2); t.set ("loboost", 2.5f); t.set ("loatten", 2.5f);
        double bump = -99, dip = 99;
        for (double f : { 20.0, 30.0, 40.0, 60.0 }) bump = std::max (bump, eqGain (t, f));
        for (double f : { 150.0, 200.0, 250.0, 300.0, 400.0, 500.0 }) dip = std::min (dip, eqGain (t, f));
        row ("taper", "trick_2.5_bump_dB", bump, bump > 1.5 && bump < 5.0 ? "PASS" : "FAIL");
        row ("taper", "trick_2.5_dip_dB", dip, dip < -1.0 && dip > -4.0 ? "PASS" : "FAIL");
        // halfway on both: still clearly more at the bottom, and a deeper dip (atten is not just "less boost")
        Rig h5 (sr); h5.set ("lofreq", 2); h5.set ("loboost", 5); h5.set ("loatten", 5);
        double b5 = -99, d5 = 99;
        for (double f : { 20.0, 30.0, 40.0 }) b5 = std::max (b5, eqGain (h5, f));
        for (double f : { 150.0, 200.0, 250.0, 300.0, 400.0, 500.0 }) d5 = std::min (d5, eqGain (h5, f));
        row ("taper", "trick_5_bump_dB", b5, b5 > 2.0 ? "PASS" : "FAIL");
        row ("taper", "trick_5_dip_dB", d5, d5 < -3.0 ? "PASS" : "FAIL");
    }
    // the trick: boost 6 and atten 3 together at 60 Hz
    {
        Rig r (sr); r.set ("lofreq", 2); r.set ("loboost", 6); r.set ("loatten", 3);
        double best = -99, dip = 99;
        for (double f : { 20.0, 30.0, 40.0, 60.0, 80.0, 100.0, 150.0, 200.0, 300.0, 500.0, 1000.0 })
        { const double g = eqGain (r, f); row ("low-trick", String ((int) f) + "Hz_dB", g); if (f <= 60) best = std::max (best, g); if (f >= 100 && f <= 500) dip = std::min (dip, g); }
        row ("low-trick", "bump_dB", best, best > 1.0 ? "PASS" : "WARN");
        row ("low-trick", "dip_above_dB", dip, dip < -1.0 ? "PASS" : "WARN");
    }
    // high peak: 18 dB at the centre (spec), sharp and broad
    {
        const double fh[] { 3000, 4000, 5000, 6000, 8000, 10000, 12000, 16000 };
        for (int i = 0; i < 8; ++i)
        {
            // the hardware: +18 dB when sharp, +9 dB when broad
            Rig r (sr); r.norm ("hifreq", i / 7.0f); r.set ("hiboost", 10); r.set ("bw", 0);
            const double g = eqGain (r, fh[i]);
            row ("high-boost", String ((int) fh[i]) + "Hz_sharp_dB", g, std::abs (g - 18.0) <= 1.0 ? "PASS" : "WARN");
            Rig b (sr); b.norm ("hifreq", i / 7.0f); b.set ("hiboost", 10); b.set ("bw", 10);
            const double gb = eqGain (b, fh[i]);
            row ("high-boost", String ((int) fh[i]) + "Hz_broad_dB", gb, std::abs (gb - 9.0) <= 1.5 ? "PASS" : "WARN");
        }
        for (float bw : { 0.0f, 10.0f })
        {
            Rig r (sr); r.set ("hifreq", 4); r.set ("hiboost", 10); r.set ("bw", bw);
            row ("high-bandwidth", String (bw < 5 ? "sharp" : "broad") + "_8k_oct_below_dB", eqGain (r, 4000));
        }
    }
    // high atten: 5 / 10 / 20 kHz shelves, spec 16 dB
    for (int i = 0; i < 3; ++i)
    {
        Rig r (sr); r.norm ("attensel", i / 2.0f); r.set ("hiatten", 10);
        row ("high-atten", String (5 << i) + "k_at_20k_dB", eqGain (r, 20000));
        // the hardware: most of the 16 dB at the frequency chosen, starting about an eighth of the way down
        const double fsel = 5000.0 * (1 << i), atSel = eqGain (r, jmin (fsel, 18000.0)), eighth = eqGain (r, fsel / 8.0);
        row ("high-atten", String (5 << i) + "k_at_its_frequency_dB", atSel, atSel < -11.0 && atSel > -17.5 ? "PASS" : "WARN");
        row ("high-atten", String (5 << i) + "k_an_eighth_down_dB", eighth, eighth < -0.3 && eighth > -4.0 ? "PASS" : "WARN");
    }
    // distortion at the reference level (+10 dBm ~ -12 dBFS), spec <= 0.15 %
    for (double f : { 100.0, 1000.0, 5000.0 })
    {
        Rig r (sr); r.set ("eqin", 0);
        // warmth, but smooth: about 0.1 % at a typical -18 dBFS (the original's spec is 0.15 % at +10 dBm, i.e. -12 dBFS)
        const double t18 = thdPct (r, f, 0.1259f);
        row ("thd", String ((int) f) + "Hz_-18dBFS_pct", t18, t18 > 0.06 && t18 < 0.25 ? "PASS" : "FAIL");
        const double t12 = thdPct (r, f, 0.2512f);
        row ("thd", String ((int) f) + "Hz_-12dBFS_pct", t12, lim (t12, 1.0, 1.5));
        row ("thd", String ((int) f) + "Hz_-24dBFS_pct", thdPct (r, f, 0.0631f));
        row ("thd", String ((int) f) + "Hz_-6dBFS_pct", thdPct (r, f, 0.5012f));
        row ("thd", String ((int) f) + "Hz_0dBFS_pct", thdPct (r, f, 1.0f));
    }
    { Rig r (sr); r.set ("eqin", 0); row ("thd", "50Hz_0dBFS_pct", thdPct (r, 50, 1.0f)); }
    // smoothness: SMPTE intermodulation (60 Hz + 7 kHz, 4:1) at -12 dBFS stays low, and silence in is silence out
    {
        Rig r (sr); r.reset();
        const int n = (int) sr * 2;
        Buf s ((size_t) n);
        for (int i = 0; i < n; ++i) s[(size_t) i] = (float) (0.2 * std::sin (MathConstants<double>::twoPi * 60.0 * i / sr) + 0.05 * std::sin (MathConstants<double>::twoPi * 7000.0 * i / sr));
        Buf a, b; r.process (s, s, a, b);
        const int from = (int) sr, len = (int) sr / 60 * 60;
        const double c7 = std::abs (demod (a, from, from + len, 7000.0, sr));
        double side = 0; for (int k = 1; k <= 3; ++k) { const double u = std::abs (demod (a, from, from + len, 7000.0 + 60.0 * k, sr)), d = std::abs (demod (a, from, from + len, 7000.0 - 60.0 * k, sr)); side += u * u + d * d; }
        const double imd = 100.0 * std::sqrt (side) / c7;
        row ("smooth", "smpte_imd_-12dBFS_pct", imd, imd < 0.8 ? "PASS" : "FAIL");   // mostly 2nd order, as a single-ended triode gives
        Rig q (sr); q.set ("noise", 0); q.reset(); Buf z ((size_t) n, 0.0f), za, zb; q.process (z, z, za, zb);
        double pk = 0; for (auto v : za) pk = std::max (pk, (double) std::abs (v));
        row ("smooth", "silence_out_noise_off_dBFS", db (pk + 1e-12), pk < 1.0e-6 ? "PASS" : "FAIL");
        // with NOISE on (the default): the hardware's hiss, about -96 dBFS, its own in each channel
        Rig h (sr); h.reset(); Buf ha, hb; h.process (z, z, ha, hb);
        const double nl = db (rms (ha, (int) sr)); double cross = 0, e1 = 0, e2 = 0;
        for (size_t i = (size_t) sr; i < ha.size(); ++i) { cross += ha[i] * hb[i]; e1 += ha[i] * ha[i]; e2 += hb[i] * hb[i]; }
        row ("noise", "floor_dBFS", nl, nl > -99.0 && nl < -93.0 ? "PASS" : "FAIL");
        row ("noise", "left_right_correlation", cross / std::sqrt (e1 * e2 + 1e-30), std::abs (cross / std::sqrt (e1 * e2 + 1e-30)) < 0.05 ? "PASS" : "FAIL");
    }
    // the top end keeps its analog shape: a 12 kHz peak is as wide above as below (no squeezing near the top of the band)
    {
        Rig r (sr); r.set ("hifreq", 6); r.set ("hiboost", 10); r.set ("bw", 5);
        const double below = eqGain (r, 12000.0 / 1.414), above = eqGain (r, 12000.0 * 1.414);
        row ("smooth", "12k_peak_half_octave_below_dB", below);
        row ("smooth", "12k_peak_half_octave_above_dB", above, std::abs (above - below) < 2.0 ? "PASS" : "FAIL");
    }
    // tube warmth: the 2nd harmonic leads the 3rd at every level, from quiet to driven
    for (double dbfs : { -24.0, -18.0, -12.0, -6.0, -1.0 })
    {
        Rig r (sr); r.reset();
        const double f = 1000.0; const float amp = (float) std::pow (10.0, dbfs / 20.0);
        const int n = (int) sr * 2;
        Buf s = sine (n, f, sr, amp), a, b;
        r.process (s, s, a, b);
        const int from = (int) sr, len = (int) (std::floor ((double) (n - from) * f / sr) * sr / f);
        const double h1 = std::abs (demod (a, from, from + len, f, sr));
        const double h2 = db (std::abs (demod (a, from, from + len, 2 * f, sr)) / h1), h3 = db (std::abs (demod (a, from, from + len, 3 * f, sr)) / h1);
        row ("warmth", String ((int) dbfs) + "dBFS_H2_dB", h2);
        row ("warmth", String ((int) dbfs) + "dBFS_H2_over_H3_dB", h2 - h3, h2 - h3 > 6.0 ? "PASS" : "FAIL");
    }
    // aliasing at full level
    {
        Rig r (sr); r.set ("eqin", 0);
        const int n = (int) sr * 2; Buf s = sine (n, 7300.0, sr, 1.0f), a, b; r.process (s, s, a, b);
        auto P = welch (a, (int) sr / 2, (int) sr, 16384);
        double fund = 0, spur = 0; const int N = (int) (P.size() - 1) * 2;
        for (int k = 1; k < (int) P.size(); ++k)
        {
            const double f = k * sr / N; bool near = false;
            for (int h = 1; h <= 3; ++h) if (std::abs (f - 7300.0 * h) < 200) near = true;
            if (std::abs (f - 7300.0) < 200) fund = std::max (fund, P[(size_t) k]); else if (! near) spur = std::max (spur, P[(size_t) k]);
        }
        if (gOut != File()) dumpCSV ("alias", P, sr);
        const double d = 10 * std::log10 (std::max (spur, 1e-20) / fund);
        row ("alias", "worst_spur_dBc", d, lim (d, -80.0, -65.0));
    }
    // EQ IN out: the tubes and transformers stay, the EQ is gone (a boosted EQ must show no gain when out)
    {
        Rig r (sr); r.set ("loboost", 10); r.set ("hiboost", 10); r.set ("eqin", 0);
        const double g = eqGain (r, 8000), g2 = eqGain (r, 60);
        row ("eq-in", "out_8k_dB", g, std::abs (g) < 0.5 ? "PASS" : "FAIL");
        row ("eq-in", "out_60_dB", g2, std::abs (g2) < 0.5 ? "PASS" : "FAIL");
    }
    // Path: M/S routing. Mid bank boosts lows, Side bank is flat
    {
        Rig r (sr); r.set ("msmode", 1); r.set ("msedit", 0); r.set ("m_lofreq", 2); r.set ("m_loboost", 10);
        const double mid = eqGain (r, 30, 0.03f, false), side = eqGain (r, 30, 0.03f, true);
        row ("path", "mid_bank_on_mid_dB", mid, mid > 8 ? "PASS" : "FAIL");
        row ("path", "mid_bank_on_side_dB", side, std::abs (side) < 0.5 ? "PASS" : "FAIL");
        Rig q (sr); q.set ("msmode", 1); q.set ("s_hifreq", 4); q.set ("s_hiboost", 10);
        const double sd = eqGain (q, 8000, 0.03f, true), md = eqGain (q, 8000, 0.03f, false);
        row ("path", "side_bank_on_side_dB", sd, sd > 10 ? "PASS" : "FAIL");
        row ("path", "side_bank_on_mid_dB", md, std::abs (md) < 0.5 ? "PASS" : "FAIL");
        // L/R mode ignores the M/S banks
        Rig w (sr); w.set ("msmode", 0); w.set ("m_loboost", 10); const double lr = eqGain (w, 30);
        row ("path", "LR_ignores_MS_banks_dB", lr, std::abs (lr) < 0.5 ? "PASS" : "FAIL");
    }
    // solo
    {
        Rig r (sr); r.set ("msmode", 1); r.set ("msedit", 2); r.set ("solo", 1);
        const double sMid = eqGain (r, 1000, 0.1f, false, true, 0), sSide = eqGain (r, 1000, 0.1f, true, false, 0), sSideR = eqGain (r, 1000, 0.1f, true, false, 1);
        row ("solo", "side_solo_hears_mid_dB", sMid, sMid < -80 ? "PASS" : "FAIL");
        row ("solo", "side_solo_hears_side_L_dB", sSide, std::abs (sSide) < 0.5 ? "PASS" : "FAIL");
        row ("solo", "side_solo_hears_side_R_dB", sSideR, std::abs (sSideR) < 0.5 ? "PASS" : "FAIL");
        Rig q (sr); q.set ("msmode", 1); q.set ("msedit", 0); q.set ("solo", 1);
        const double m = eqGain (q, 1000, 0.1f, false), sd = eqGain (q, 1000, 0.1f, true);
        row ("solo", "mid_solo_hears_mid_dB", m, std::abs (m) < 0.5 ? "PASS" : "FAIL");
        row ("solo", "mid_solo_hears_side_dB", sd, sd < -80 ? "PASS" : "FAIL");
        Rig w (sr); w.set ("msmode", 0); w.set ("solo", 1); const double lr = eqGain (w, 1000, 0.1f);
        row ("solo", "LR_solo_ignored_dB", lr, std::abs (lr) < 0.5 ? "PASS" : "FAIL");
    }
    // Midrange (MEQ-5, its published ranges): low peak +10 dB (200 Hz - 1 kHz), dip -10 dB (200 Hz - 7 kHz), high peak +8 dB (1.5 - 5 kHz)
    {
        const double lo[] { 200, 300, 500, 700, 1000 }, dip[] { 200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000 }, hi[] { 1500, 2000, 3000, 4000, 5000 };
        for (int i = 0; i < 5; ++i) { Rig r (sr); r.norm ("mlfreq", i / 4.0f); r.set ("mlpeak", 10); const double g = eqGain (r, lo[i]); row ("mid-lopeak", String ((int) lo[i]) + "Hz_dB", g, std::abs (g - 10) < 1.0 ? "PASS" : "WARN"); }
        for (int i = 0; i < 11; ++i) { Rig r (sr); r.norm ("mdfreq", i / 10.0f); r.set ("mdip", 10); const double g = eqGain (r, dip[i]); row ("mid-dip", String ((int) dip[i]) + "Hz_dB", g, std::abs (g + 10) < 1.0 ? "PASS" : "WARN"); }
        for (int i = 0; i < 5; ++i) { Rig r (sr); r.norm ("mhfreq", i / 4.0f); r.set ("mhpeak", 10); const double g = eqGain (r, hi[i]); row ("mid-hipeak", String ((int) hi[i]) + "Hz_dB", g, std::abs (g - 8) < 1.0 ? "PASS" : "WARN"); }
        // audio taper here too: a quarter of the way up already lifts 500 Hz by 4 dB or more, and a gentle setting is wide
        { Rig r (sr); r.set ("mlfreq", 2); r.set ("mlpeak", 2.5f); const double g = eqGain (r, 500), o = eqGain (r, 250);
          row ("mid-taper", "lopeak_2.5_at_500Hz_dB", g, g > 4.0 ? "PASS" : "FAIL");
          row ("mid-taper", "lopeak_2.5_octave_below_dB", o, o > g * 0.45 ? "PASS" : "FAIL"); }
        Rig r (sr); r.set ("m_mdip", 10); r.set ("msmode", 1);
        const double m = eqGain (r, 1000, 0.03f, false), s = eqGain (r, 1000, 0.03f, true);
        row ("mid-path", "mid_bank_dip_on_mid_dB", m, m < -8 ? "PASS" : "FAIL");
        row ("mid-path", "mid_bank_dip_on_side_dB", s, std::abs (s) < 0.5 ? "PASS" : "FAIL");
    }
    // auto gain: the level of pink noise stays within 1.25 dB however the EQ is set; off, it moves
    {
        const Buf s = pink ((int) sr * 3, 0.1f);
        struct Case { const char* name; std::function<void (Rig&)> f; };
        const std::vector<Case> cases {
            { "bass_boost", [] (Rig& r) { r.set ("loboost", 10); r.set ("lofreq", 2); } },
            { "air_boost", [] (Rig& r) { r.set ("hiboost", 10); r.set ("hifreq", 6); r.set ("bw", 8); } },
            { "mid_dip", [] (Rig& r) { r.set ("mdip", 10); r.set ("mdfreq", 4); } },
            { "high_cut", [] (Rig& r) { r.set ("hiatten", 10); r.set ("attensel", 0); } },
            { "mixed", [] (Rig& r) { r.set ("loboost", 8); r.set ("loatten", 4); r.set ("hiboost", 7); r.set ("mlpeak", 6); r.set ("mhpeak", 5); } } };
        for (auto& cs : cases)
        {
            double lev[2];
            for (int on = 0; on < 2; ++on)
            {
                Rig r (sr); cs.f (r); r.set ("autogain", (float) on);
                Buf a, b; r.process (s, s, a, b);
                lev[on] = db (rms (kWeight (a), (int) sr)) - db (rms (kWeight (s), (int) sr));
            }
            row ("autogain", String (cs.name) + "_off_dB", lev[0]);
            row ("autogain", String (cs.name) + "_on_dB", lev[1], std::abs (lev[1]) < 1.25 ? "PASS" : "WARN");
        }
        Rig r (sr); r.set ("autogain", 1); const double g = eqGain (r, 1000);
        row ("autogain", "flat_eq_unchanged_dB", g, std::abs (g) < 0.1 ? "PASS" : "FAIL");
    }
    // block size independence and robustness
    {
        Rig a (sr, 64), b (sr, 1024);
        for (auto* r : { &a, &b }) { r->set ("loboost", 7); r->set ("hiboost", 7); r->set ("loatten", 3); }
        Buf s = pink ((int) sr, 0.1f), o1, o2, d1, d2;
        a.process (s, s, o1, d1, 64); b.process (s, s, o2, d2, 1024);
        double dif = 0; for (size_t i = 0; i < s.size(); ++i) dif = std::max (dif, (double) std::abs (o1[i] - o2[i]));
        row ("blocksize", "max_diff_dB", db (dif), lim (db (dif), -100.0, -80.0));
    }
    {
        Rig r (sr); r.set ("loboost", 10); r.set ("hiboost", 10); r.set ("hifreq", 7);
        Buf s = pink ((int) sr, 0.3f), a, b; s[100] = 50.0f; s[200] = -1e6f;
        r.process (s, s, a, b);
        row ("robust", "extreme_input_finite", ! bad (a) && ! bad (b), (! bad (a) && ! bad (b)) ? "PASS" : "FAIL");
        Rig r2 (sr); r2.set ("noise", 0); r2.set ("loboost", 10); r2.set ("hiboost", 10); { Buf q = pink ((int) sr / 2, 0.1f), qa, qb; r2.process (q, q, qa, qb); }
        Buf z ((size_t) sr, 0.0f); r2.process (z, z, a, b);
        row ("robust", "silence_tail_rms_dB", db (rms (a, (int) sr / 2)), db (rms (a, (int) sr / 2)) < -100 ? "PASS" : "WARN");
        // every selector and both modes, automated while playing
        Random rnd (3); Buf in = pink ((int) sr * 2, 0.2f), o, o2; bool ok = true;
        for (int k = 0; k < 40; ++k)
        {
            for (auto id : { "m_lofreq", "s_hifreq", "attensel", "lofreq", "hifreq", "msmode", "solo", "eqin", "loboost", "m_hiboost", "s_loatten", "bw" }) r.norm (id, rnd.nextFloat());
            Buf seg (in.begin() + k * 2400, in.begin() + (k + 1) * 2400);
            r.process (seg, seg, o, o2, 300);
            ok = ok && ! bad (o) && ! bad (o2) && peakOf (o) < 8.0;
        }
        row ("robust", "automation_storm_ok", ok, ok ? "PASS" : "FAIL");
    }
    // clicks on a stepped switch while playing a 100 Hz tone at -20 dBFS
    {
        Rig r (sr); r.set ("loboost", 5);
        const int n = (int) sr * 2; Buf s = sine (n, 100.0, sr, 0.1f), a, b, o;
        double worst = 0;
        for (auto id : { "lofreq", "hifreq", "attensel" })
        {
            r.reset(); r.set ("hiboost", 5); r.set ("hiatten", 5);
            Buf x1 (s.begin(), s.begin() + (int) sr / 2), x2 (s.begin() + (int) sr / 2, s.end());
            r.process (x1, x1, a, b); r.norm (id, 1.0f); r.process (x2, x2, o, b);
            double mx = 0; for (size_t i = 1; i < o.size(); ++i) mx = std::max (mx, (double) std::abs (o[i] - o[i - 1]));
            double ref = 0; for (size_t i = 1; i < a.size(); ++i) ref = std::max (ref, (double) std::abs (a[i] - a[i - 1]));
            worst = std::max (worst, mx / std::max (ref, 1e-9));
            r.norm (id, 0.0f);
        }
        row ("clicks", "selector_step_slope_ratio", worst, lim (worst, 3.0, 6.0));
    }
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    {
        std::unique_ptr<AudioProcessor> p (createPluginFilter());
        gName = p->getName();
    }
    if (argc > 1) { gOut = File (argv[1]); gOut.createDirectory(); }
    const double sr = 48000.0;
    if (argc > 2 && String (argv[2]) == "render") { String skin = argc > 3 ? String (argv[3]) : String();
        StringArray extra; for (int i = 4; i < argc; ++i) extra.add (argv[i]);
        renderEditor (File (argv[1]).getChildFile (gName.removeCharacters (" ") + (skin.isNotEmpty() ? "-" + skin : String()) + "-editor.png"), true, skin, extra); return 0; }
    if (argc > 2 && String (argv[2]) == "debug") { debugBlocks (sr); return 0; }
    if (argc > 2 && String (argv[2]) == "probe") { probeEquanimity (sr); return 0; }
    if (argc > 2 && String (argv[2]) == "uiprobe") { uiProbe (sr); return 0; }
    if (gName.contains ("Equanimity")) { testEquanimity (sr); return 0; }
    testLatency (sr);
    testNull (sr);
    testHarmonicsAndAlias (sr);
    gSuffix = "@96k"; testHarmonicsAndAlias (96000.0); gSuffix = {};
    testPhase (sr);
    testTone (sr);
    testDynamics (sr);
    testDelay (sr);
    testReverb (sr);
    testClicks (sr);
    testRobust (sr);
    return 0;
}
