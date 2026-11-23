#include <complex>
#include "EquanimityProcessor.h"
#include "PluginEditor.h"
#include <string>

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout EquanimityProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto dial = [&] (const char* id, const char* name, float def)   // the 0 - 10 dials of the original
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
    };
    auto choice = [&] (const char* id, const char* name, StringArray items, int def, bool automatable = true)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def,
                                                             AudioParameterChoiceAttributes().withAutomatable (automatable)));
    };

    // Three banks of the same controls: Stereo (left / right), Mid and Side. All three are heard at once;
    // the Path keys choose the mode and which bank the panel shows.
    for (int bank = 0; bank < 3; ++bank)
    {
        const std::string pre = bank == 0 ? "" : bank == 1 ? "m_" : "s_";
        const String nm = bank == 0 ? "" : bank == 1 ? "Mid " : "Side ";
        auto id = [&] (const char* base) { return pre + base; };
        auto idc = [&] (const char* base) { return ParameterID { id (base), 1 }; };
        auto dial2 = [&] (const char* base, const char* name, float def)
        {
            p.push_back (std::make_unique<AudioParameterFloat> (idc (base), nm + name, NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
        };
        auto choice2 = [&] (const char* base, const char* name, StringArray items, int def)
        {
            p.push_back (std::make_unique<AudioParameterChoice> (idc (base), nm + name, items, def));
        };
        // Low
        choice2 ("lofreq", "Low Frequency", { "20 Hz", "30 Hz", "60 Hz", "100 Hz" }, 2);
        dial2 ("loboost", "Low Boost", 0.0f);
        dial2 ("loatten", "Low Atten", 0.0f);
        // High
        choice2 ("hifreq", "High Frequency", { "3 kHz", "4 kHz", "5 kHz", "6 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" }, 4);
        dial2 ("bw", "Bandwidth", 5.0f);
        dial2 ("hiboost", "High Boost", 0.0f);
        // High atten
        choice2 ("attensel", "Atten Sel", { "5 kHz", "10 kHz", "20 kHz" }, 1);
        dial2 ("hiatten", "High Atten", 0.0f);
        // Midrange (MEQ-5): low peak, dip, high peak
        choice2 ("mlfreq", "Mid Low Frequency", { "200 Hz", "300 Hz", "500 Hz", "700 Hz", "1 kHz" }, 2);
        dial2 ("mlpeak", "Mid Low Peak", 0.0f);
        choice2 ("mdfreq", "Mid Dip Frequency", { "200 Hz", "300 Hz", "500 Hz", "700 Hz", "1 kHz", "1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz", "7 kHz" }, 4);
        dial2 ("mdip", "Mid Dip", 0.0f);
        choice2 ("mhfreq", "Mid High Frequency", { "1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz" }, 2);
        dial2 ("mhpeak", "Mid High Peak", 0.0f);
    }
    // Switches
    choice ("eqin", "EQ In", { "Out", "In" }, 1);
    choice ("msmode", "Path", { "L/R", "M/S" }, 0);                       // the whole EQ in left / right or in mid / side
    choice ("msedit", "Path Edit", { "Mid", "Link", "Side" }, 0, false);   // in M/S: which channel the panel edits (Link = both)
    choice ("autogain", "Auto Gain", { "Out", "In" }, 0);                     // keeps the level steady as the EQ changes
    choice ("noise", "Noise", { "Off", "On" }, 1);                             // the hardware's own faint hiss
    choice ("solo", "Solo", { "Out", "In" }, 0);                                  // listen to the Mid or the Side alone
    // Output
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return { p.begin(), p.end() };
}


// Triode curve, slope 1 at the origin. k sets how hard the signal drives it, b how lopsided (even harmonics) it sits.
// In double precision throughout: the curve is used very gently, and float would leave a grain of rounding noise on quiet signals.
static inline double triode (double x, double k, double b)
{
    const double t0 = std::tanh (b), slope = 1.0 - t0 * t0;
    return (std::tanh (k * x + b) - t0) / (k * slope);
}
static inline double satLow (double l) { return std::tanh (0.08 * l) / 0.08; }   // gentle: boosted bass thickens, isn't crushed

EquanimityProcessor::EquanimityProcessor()
    : ArtifactsProcessor (BusesProperties()
                            .withInput  ("Input",  AudioChannelSet::stereo(), true)
                            .withOutput ("Output", AudioChannelSet::stereo(), true),
                          createLayout(), "Equanimity")
{
    pOutput = apvts.getRawParameterValue ("output");
    pEqIn = apvts.getRawParameterValue ("eqin");
    pMode = apvts.getRawParameterValue ("msmode");
    pEdit = apvts.getRawParameterValue ("msedit");
    pSolo = apvts.getRawParameterValue ("solo");
    static const char* base[14] { "lofreq", "loboost", "loatten", "hifreq", "bw", "hiboost", "attensel", "hiatten", "mlfreq", "mlpeak", "mdfreq", "mdip", "mhfreq", "mhpeak" };
    pAuto = apvts.getRawParameterValue ("autogain");
    pNoise = apvts.getRawParameterValue ("noise");
    static const char* pre[3] { "", "m_", "s_" };
    for (int b = 0; b < 3; ++b)
        for (int k = 0; k < 14; ++k)
            pBank[b][k] = apvts.getRawParameterValue (String (pre[b]) + base[k]);
}

bool EquanimityProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet(), in = layouts.getMainInputChannelSet();
    return out == AudioChannelSet::stereo() && (in == AudioChannelSet::stereo() || in == AudioChannelSet::mono());
}

double EquanimityProcessor::loFreqHz (int i)  { static const double f[] { 20, 30, 60, 100 }; return f[jlimit (0, 3, i)]; }
double EquanimityProcessor::hiFreqHz (int i)  { static const double f[] { 3000, 4000, 5000, 6000, 8000, 10000, 12000, 16000 }; return f[jlimit (0, 7, i)]; }
double EquanimityProcessor::mLoHz (int i)  { static const double f[] { 200, 300, 500, 700, 1000 }; return f[jlimit (0, 4, i)]; }
double EquanimityProcessor::mDipHz (int i) { static const double f[] { 200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000 }; return f[jlimit (0, 10, i)]; }
double EquanimityProcessor::mHiHz (int i)  { static const double f[] { 1500, 2000, 3000, 4000, 5000 }; return f[jlimit (0, 4, i)]; }
double EquanimityProcessor::attSelHz (int i)  { static const double f[] { 5000, 10000, 20000 }; return f[jlimit (0, 2, i)]; }


// The EQP-1A, as measured on the hardware:
//  - Low boost: a broad shelf that has almost all of its 13.5 dB at the frequency chosen and starts about six times higher
//    (60 Hz, boost 10: +13.8 dB at 60, +9.9 at 120, +3.5 at 250, matched on a 60 Hz tone to the UAD model). Low atten: a shelf of up to -17.5 dB that turns over higher still,
//    so the two together give more low end with a dip above it (the Pultec trick; at 60 Hz the dip is near 250 - 300 Hz).
//  - The dials are audio-taper pots: much of each control's effect comes in the first quarter of its travel, the boost faster
//    than the atten (which is why, with both at the same number, the boost wins at the bottom: about +3 dB and a -2 dB dip at 2.5).
//  - High boost: a peak of up to +18 dB when sharp, +9 dB when broad. High atten: a broad shelf of up to -16 dB that has most of it
//    at the frequency chosen and starts about an eighth of the way down (5 kHz: from about 600 Hz).
static double boostTaper (double v) { return std::pow (jlimit (0.0, 10.0, v) / 10.0, 0.5); }
static double attenTaperHigh (double v) { return std::pow (jlimit (0.0, 10.0, v) / 10.0, 0.8); }
static double attenTaper (double v) { return std::pow (jlimit (0.0, 10.0, v) / 10.0, 0.8); }

void EquanimityProcessor::designEq (EqChannel& e, const Settings& s) const
{
    // designed at the 4x rate the whole unit runs at: the curves keep their analog shapes right up to 20 kHz
    // (at the base rate, peaks and shelves near the top would be squeezed narrower and harder than the hardware's)
    const double fsr = sr * 4.0, nyq = 0.45 * sr;
    auto shelfLow = [&] (Biquad& b, double f, double dB, double Q)
    {
        const double A = std::pow (10.0, dB / 40.0), w = MathConstants<double>::twoPi * jmin (f, nyq) / fsr, cs = std::cos (w), al = std::sin (w) / (2 * Q), r = 2 * std::sqrt (A) * al;
        b.set (A * ((A + 1) - (A - 1) * cs + r), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - r),
               (A + 1) + (A - 1) * cs + r, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - r);
    };
    auto shelfHigh = [&] (Biquad& b, double f, double dB, double Q)
    {
        const double A = std::pow (10.0, dB / 40.0), w = MathConstants<double>::twoPi * jmin (f, nyq) / fsr, cs = std::cos (w), al = std::sin (w) / (2 * Q), r = 2 * std::sqrt (A) * al;
        b.set (A * ((A + 1) + (A - 1) * cs + r), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - r),
               (A + 1) - (A - 1) * cs + r, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - r);
    };
    auto peak = [&] (Biquad& b, double f, double dB, double Q)
    {
        const double A = std::pow (10.0, dB / 40.0), w = MathConstants<double>::twoPi * jmin (f, nyq) / fsr, cs = std::cos (w), al = std::sin (w) / (2 * Q);
        b.set (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
    };
    const double fl = s.hz[0];
    const double tb = boostTaper (s.loBoost), ta = attenTaper (s.loAtten);
    shelfLow (e.loBoost, fl * 2.6, 15.9 * tb, 0.6);                                  // broad, no dip of its own (60 Hz, 10: +14.6 dB at 60, as the UAD model on pink noise)
    // The atten and the boost share one passive network. On its own the atten is a low shelf cut (up to -17.5 dB). With the boost
    // up, the network turns it into a dip above the boost: the level at the frequency chosen hardly moves, the curve is
    // reshaped instead (the Pultec trick). So the atten is a shelf and a dip, weighed by how far the boost is up:
    // 10 / 10 at 60 Hz: -0.4 dB at 60 Hz, a -7.7 dB dip near 300 Hz; 2.5 / 2.5: a lift of a few dB with -2 dB near 300 Hz.
    shelfLow (e.loAtten, fl * 5.0, -17.5 * ta * (1.0 - tb), 0.6);
    peak (e.loNotch, fl * 5.0, -10.0 * ta * tb, 1.2);
    // bandwidth: sharp (0) is a narrow peak of up to +18 dB, broad (10) a wide one of up to +9 dB
    const double q = 2.5 * std::pow (0.45 / 2.5, s.bw / 10.0);                       // 2.5 ... 0.45
    peak (e.hiPeak, s.hz[1], (18.0 - 9.0 * s.bw / 10.0) * boostTaper (s.hiBoost), q);
    // MEQ-5 midrange (its published ranges): low peak up to +10 dB, dip up to -10 dB (200 Hz - 7 kHz), high peak up to +8 dB.
    // Audio-taper dials like the EQP-1A's, and the passive network's way with bandwidth: a gentle setting is a wide, soft lift;
    // turned up, the curve also tightens round its frequency
    const double tl = boostTaper (s.mLoPk), td = attenTaperHigh (s.mDip), th = boostTaper (s.mHiPk);
    peak (e.mLo,  s.hz[3],  10.0 * tl, 0.45 + 0.45 * tl);    // Q 0.45 ... 0.9
    peak (e.mDip, s.hz[4], -10.0 * td, 0.60 + 0.60 * td);    // Q 0.6 ... 1.2
    peak (e.mHi,  s.hz[5],   8.0 * th, 0.45 + 0.45 * th);
    shelfHigh (e.hiAtten, s.hz[2] * 0.35, -16.5 * attenTaperHigh (s.hiAtten), 0.55);   // most of the cut at the frequency chosen
}

// How much the EQ changes the overall level, as heard: the power gain over 20 Hz - 20 kHz, equal weight per octave (as pink noise
// or music spreads it), each frequency weighted as loudness meters weigh it (ITU BS.1770: the deep bass counts for less,
// the presence range a little more). Auto gain applies the opposite, so tonal changes can be judged without the level jumping.
// |K-weighting|^2 at f (the BS.1770 pre-filter and high-pass, as specified at 48 kHz)
static double kWeight2 (double f)
{
    const std::complex<double> z1 = std::polar (1.0, -MathConstants<double>::twoPi * f / 48000.0), z2 = z1 * z1;
    const auto s = (1.53512485958697 - 2.69169618940638 * z1 + 1.19839281085285 * z2) / (1.0 - 1.69065929318241 * z1 + 0.73248077421585 * z2);
    const auto h = (1.0 - 2.0 * z1 + z2) / (1.0 - 1.99004745483398 * z1 + 0.99007225036621 * z2);
    return std::norm (s * h);
}
double EquanimityProcessor::loudnessComp (const EqChannel& e) const
{
    constexpr int N = 40;
    double acc = 0, wsum = 0;
    for (int i = 0; i < N; ++i)
    {
        const double f = 20.0 * std::pow (20000.0 / 20.0, (double) i / (N - 1)), w = MathConstants<double>::twoPi * f / (sr * 4.0);
        const double p = e.loBoost.mag2 (w) * e.loAtten.mag2 (w) * e.loNotch.mag2 (w) * e.mLo.mag2 (w) * e.mDip.mag2 (w) * e.mHi.mag2 (w) * e.hiPeak.mag2 (w) * e.hiAtten.mag2 (w);
        const double weight = kWeight2 (f);
        acc += weight * p;
        wsum += weight;
    }
    return std::pow (10.0, jlimit (-12.0, 12.0, -10.0 * std::log10 (std::max (acc / wsum, 1.0e-12))) / 20.0);
}

void EquanimityProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = jmax (32, samplesPerBlock);
    oversampling.reset();
    oversampling.initProcessing ((size_t) maxBlock);
    setLatencySamples ((int) std::ceil (oversampling.getLatencyInSamples()));
    sOut.reset (sampleRate, 0.03);
    sOut.setCurrentAndTargetValue (Decibels::decibelsToGain (pOutput->load()));
    for (auto& v : valve) v = {};
    for (auto& e : eq) { e.loBoost.clear(); e.loAtten.clear(); e.loNotch.clear(); e.mLo.clear(); e.mDip.clear(); e.mHi.clear(); e.hiPeak.clear(); e.hiAtten.clear(); e.autoGain = 1.0; }
    heat = 0.0f;
    // start from the current settings, no glide
    for (int c = 0; c < 2; ++c) { cur[c] = tgt[c]; designEq (eq[c], cur[c]); }
}

void EquanimityProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // a mono source feeds both sides

    // ---- which banks the two EQ channels use, and the dial settings (as the original: 0 - 10, stepped selectors)
    const bool ms = pMode->load() > 0.5f;
    const int edit = (int) pEdit->load();   // 0 Mid, 1 Link, 2 Side
    const bool eqIn = pEqIn->load() > 0.5f;
    const int bankA = ms ? 1 : 0, bankB = ms ? 2 : 0;   // channel 0 (L or Mid), channel 1 (R or Side)
    const bool autoOn = pAuto->load() > 0.5f && pEqIn->load() > 0.5f;
    const bool solo = ms && pSolo->load() > 0.5f && edit != 1;
    const bool soloSide = edit == 2;

    auto load = [&] (int bank, Settings& s)
    {
        auto* p = pBank[bank];
        s.loFreq = (int) p[0]->load(); s.loBoost = p[1]->load(); s.loAtten = p[2]->load();
        s.hiFreq = (int) p[3]->load(); s.bw = p[4]->load(); s.hiBoost = p[5]->load();
        s.attSel = (int) p[6]->load(); s.hiAtten = p[7]->load();
        s.mLoFreq = (int) p[8]->load(); s.mLoPk = p[9]->load(); s.mDipFreq = (int) p[10]->load(); s.mDip = p[11]->load();
        s.mHiFreq = (int) p[12]->load(); s.mHiPk = p[13]->load();
        s.hz[0] = loFreqHz (s.loFreq); s.hz[1] = hiFreqHz (s.hiFreq); s.hz[2] = attSelHz (s.attSel);
        s.hz[3] = mLoHz (s.mLoFreq); s.hz[4] = mDipHz (s.mDipFreq); s.hz[5] = mHiHz (s.mHiFreq);
    };
    load (bankA, tgt[0]);
    load (bankB, tgt[1]);

    float peak = 0.0f, ins[2] { 0.0f, 0.0f };
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i) { ins[0] += L[i] * L[i]; ins[1] += R[i] * R[i]; }

    // ---- the whole unit runs at 4x: the passive network (M/S, the EQ), then the valve make-up stage and the transformers.
    //      12AX7 gain stage into the 12AU7 output stage, a 6X4 supply that gives a little under load.
    const double osRate = sr * 4.0;
    constexpr int step = 128;                                                // the dials move every 128 samples at 4x (32 at the base rate)
    const double glide = 1.0 - std::exp (-(double) step / (0.02 * osRate));
    const double hzGlide = 1.0 - std::exp (-(double) step / (0.03 * osRate));
    const double aAtt = 1.0 - std::exp (-1.0 / (0.030 * osRate)), aRel = 1.0 - std::exp (-1.0 / (0.300 * osRate));
    const double aDc = MathConstants<double>::twoPi * 3.0 / osRate, aLow = MathConstants<double>::twoPi * 70.0 / osRate;
    const double aTop = 1.0 - std::exp (-MathConstants<double>::twoPi * 60000.0 / osRate);   // output transformer: a gentle roll-off far above (-0.5 dB at 20 kHz)
    double envMax = 0.0;
    for (int off = 0; off < n; off += maxBlock)
    {
        const int len = jmin (maxBlock, n - off);
        float* chans[2] { L + off, R + off };
        dsp::AudioBlock<float> blk (chans, 2, (size_t) len);
        auto up = oversampling.processSamplesUp (blk);
        const int m = (int) up.getNumSamples();
        auto* X0 = up.getChannelPointer (0);
        auto* X1 = up.getChannelPointer (1);
        for (int start = 0; start < m; start += step)
        {
            // dials glide, stepped selectors jump (as the switches do) while the filters glide to the new frequency
            for (int c = 0; c < 2; ++c)
            {
                auto& cs = cur[c]; const auto& t = tgt[c];
                cs.loBoost += (float) ((t.loBoost - cs.loBoost) * glide); cs.loAtten += (float) ((t.loAtten - cs.loAtten) * glide);
                cs.hiBoost += (float) ((t.hiBoost - cs.hiBoost) * glide); cs.bw += (float) ((t.bw - cs.bw) * glide); cs.hiAtten += (float) ((t.hiAtten - cs.hiAtten) * glide);
                cs.mLoPk += (float) ((t.mLoPk - cs.mLoPk) * glide); cs.mDip += (float) ((t.mDip - cs.mDip) * glide); cs.mHiPk += (float) ((t.mHiPk - cs.mHiPk) * glide);
                cs.mLoFreq = t.mLoFreq; cs.mDipFreq = t.mDipFreq; cs.mHiFreq = t.mHiFreq;
                cs.loFreq = t.loFreq; cs.hiFreq = t.hiFreq; cs.attSel = t.attSel;
                for (int k = 0; k < 6; ++k)   // the frequencies glide, on a log scale
                    cs.hz[k] = std::exp (std::log (cs.hz[k]) + (std::log (t.hz[k]) - std::log (cs.hz[k])) * hzGlide);
                designEq (eq[c], cs);
                const double target = autoOn ? loudnessComp (eq[c]) : 1.0;
                eq[c].autoGain += (target - eq[c].autoGain) * 0.25;   // the level follows the dials, not the other way round
            }
            const int end = jmin (m, start + step);
            for (int i = start; i < end; ++i)
            {
                double a = X0[i], b = X1[i];
                if (ms) { const double mm = 0.5 * (a + b), ss = 0.5 * (a - b); a = mm; b = ss; }
                if (eqIn)
                {
                    a = eq[0].hiAtten.tick (eq[0].hiPeak.tick (eq[0].mHi.tick (eq[0].mDip.tick (eq[0].mLo.tick (eq[0].loNotch.tick (eq[0].loAtten.tick (eq[0].loBoost.tick (a)))))))) * eq[0].autoGain;
                    b = eq[1].hiAtten.tick (eq[1].hiPeak.tick (eq[1].mHi.tick (eq[1].mDip.tick (eq[1].mLo.tick (eq[1].loNotch.tick (eq[1].loAtten.tick (eq[1].loBoost.tick (b)))))))) * eq[1].autoGain;
                }
                if (solo) { const double v = soloSide ? b : a; a = b = v; }
                else if (ms) { const double l = a + b, r = a - b; a = l; b = r; }
                double* io[2] { &a, &b };
                for (int c = 0; c < 2; ++c)
                {
                    auto& v = valve[c];
                    double s = *io[c];
                    v.dc += aDc * (s - v.dc); s -= v.dc;                                   // input transformer: no DC, nothing below a few Hz
                    const double ab = std::abs (s);
                    v.env += (ab - v.env) * (ab > v.env ? aAtt : aRel);                    // 6X4: the supply gives a little under load
                    const double sag = 1.0 + 0.15 * v.env;
                    // 12AX7: biased off centre, so the 2nd harmonic leads and the 3rd stays well below it at any level. The amounts are
                    // matched to the UAD model on a 60 Hz tone with the low boost full up: 2nd about -50 dB, 3rd about -64 dB near full scale
                    s = triode (s, 0.016 * sag, 0.45);
                    s = triode (s, 0.06 * sag, -0.05);                                    // 12AU7: plenty of headroom, a little odd order when driven
                    v.lowband += aLow * (s - v.lowband);                                    // output transformer: the iron gives in first at the bottom
                    s += satLow (v.lowband) - v.lowband;
                    v.top += aTop * (s - v.top); s = v.top;                                 // ... and rolls off gently far above
                    v.dc2 += aDc * (s - v.dc2); s -= v.dc2;                                 // no DC out
                    *io[c] = s;
                    envMax = jmax (envMax, v.env);
                }
                X0[i] = (float) a; X1[i] = (float) b;
            }
        }
        oversampling.processSamplesDown (blk);
    }
    heat += ((float) jlimit (0.0, 1.0, envMax * 2.0) - heat) * 0.3f;
    valveHeat = heat;

    // ---- the hardware's own noise floor: a faint, even hiss from the tubes and transformers, about -96 dBFS, its own in each
    //      channel. Below hearing on its own; across a mix it is part of what keeps it from sounding sterile.
    if (pNoise->load() > 0.5f)
    {
        const float amp = Decibels::decibelsToGain (-96.0f) * 1.732f;   // uniform noise of this peak has the RMS above
        for (int c = 0; c < 2; ++c)
        {
            auto* d = c == 0 ? L : R;
            auto& st = noiseState[c];
            for (int i = 0; i < n; ++i)
            {
                st ^= st << 13; st ^= st >> 17; st ^= st << 5;
                d[i] += amp * ((float) (st & 0xffffff) / 8388608.0f - 1.0f);
            }
        }
    }

    // ---- output level
    sOut.setTargetValue (Decibels::decibelsToGain (pOutput->load()));
    float ms2[2] { 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        const float g = sOut.getNextValue();
        L[i] *= g; R[i] *= g;
        ms2[0] += L[i] * L[i]; ms2[1] += R[i] * R[i];
        peak = jmax (peak, std::abs (L[i]), std::abs (R[i]));
    }
    for (int c = 0; c < 2; ++c) { meterOut[c] = ms2[c] / (float) jmax (1, n); meterIn[c] = ins[c] / (float) jmax (1, n); }
    peakOut = peak;
}

void EquanimityProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void EquanimityProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* EquanimityProcessor::createEditor() { return new ArtifactsEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EquanimityProcessor(); }
