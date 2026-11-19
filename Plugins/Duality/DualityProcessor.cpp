#include "DualityProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr double maxIRSeconds = 12.0;
    constexpr int numBuilt = 3;
    const char* const builtNames[numBuilt] = { "Stone Room", "Chapel", "Deep Cave" };

    String pctText (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }

    // A made-up room, as an impulse response: early reflections, then a diffuse tail whose highs die
    // faster than its lows, the way air and soft surfaces do it. Different noise in each ear.
    AudioBuffer<float> makeBuiltIn (int kind, double rate)
    {
        const double rt60[numBuilt]   = { 0.9, 2.8, 6.5 };
        const double tone0[numBuilt]  = { 9000.0, 7000.0, 4500.0 };    // brightness of the tail at the start
        const double toneEnd[numBuilt] = { 2500.0, 1400.0, 700.0 };    // and where it ends up
        const double predelay[numBuilt] = { 0.004, 0.009, 0.016 };
        const int n = (int) (rt60[kind] * 1.15 * rate);
        AudioBuffer<float> ir (2, n);
        ir.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            Random rng (1234 + kind * 77 + ch * 991);
            auto* x = ir.getWritePointer (ch);
            double lp = 0.0, lp2 = 0.0;
            const int start = (int) (predelay[kind] * rate);
            for (int i = start; i < n; ++i)
            {
                const double t = (double) (i - start) / rate;
                const double env = std::pow (10.0, -3.0 * t / rt60[kind]);
                const double fc = tone0[kind] * std::pow (toneEnd[kind] / tone0[kind], std::min (1.0, t / (rt60[kind] * 0.8)));
                const double a = 1.0 - std::exp (-MathConstants<double>::twoPi * fc / rate);
                const double noise = rng.nextDouble() * 2.0 - 1.0;
                lp += a * (noise - lp);
                lp2 += a * (lp - lp2);   // two poles: a smoother, less hissy tail
                const double fadeIn = std::min (1.0, t / (0.004 + 0.012 * kind));   // density builds up
                x[i] = (float) (lp2 * env * fadeIn * 3.0);
            }
            // early reflections: a handful of discrete taps in the first 80 ms, alternating polarity
            Random er (55 + kind * 13 + ch * 7);
            for (int k = 0; k < 14; ++k)
            {
                const double ms = 3.0 + (0.012 + 0.014 * kind) * 1000.0 * ((double) k / 14.0) * (1.0 + er.nextDouble() * 0.4);
                const int pos = start + (int) (ms * 0.001 * rate);
                if (pos < n - 8)
                    x[pos] += (float) ((k % 2 ? -1.0 : 1.0) * 0.5 * std::pow (0.82, k) * (0.7 + 0.6 * er.nextDouble()));
            }
        }
        // a high-pass at 35 Hz so no sub rumble rides in with the tail
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* x = ir.getWritePointer (ch);
            double lp = 0.0;
            const double a = 1.0 - std::exp (-MathConstants<double>::twoPi * 35.0 / rate);
            for (int i = 0; i < n; ++i) { lp += a * (x[i] - lp); x[i] -= (float) lp; }
        }
        return ir;
    }

    IRInfo describe (const AudioBuffer<float>& ir, double rate, const String& name, bool mono)
    {
        IRInfo info;
        info.loaded = true;
        info.name = name;
        info.length = String (ir.getNumSamples() / rate, 2) + " s";
        info.rate = String (rate / 1000.0, rate == std::floor (rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz";
        info.format = mono ? "MONO" : "STEREO";
        const int bins = 48, n = ir.getNumSamples();
        info.peaks.assign ((size_t) bins, 0.0f);
        const float peak = std::max (1.0e-9f, std::max (ir.getMagnitude (0, 0, n), ir.getMagnitude (1, 0, n)));
        for (int b = 0; b < bins; ++b)
        {
            const int s0 = (int) ((double) b / bins * n), s1 = std::max (s0 + 1, (int) ((double) (b + 1) / bins * n));
            const float m = std::max (ir.getMagnitude (0, s0, s1 - s0), ir.getMagnitude (1, s0, s1 - s0)) / peak;
            info.peaks[(size_t) b] = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (m, -80.0f) + 60.0f) / 60.0f);
        }
        return info;
    }

    struct Program { const char* name; std::vector<std::pair<const char*, float>> values; };
    const std::vector<Program>& programs()
    {
        static const std::vector<Program> list {
            { "Duality", {} },
            { "Object: Plate", { { "perspective", 0.0f }, { "rtype", 2.0f }, { "rsize", 0.45f }, { "rdamp", 0.35f }, { "rlevel", 0.7f } } },
            { "Object: Hall", { { "perspective", 0.0f }, { "rtype", 1.0f }, { "rsize", 0.6f }, { "rdamp", 0.45f }, { "rlevel", 0.7f } } },
            { "Subject: Chapel", { { "perspective", 2.0f }, { "idecay", 1.0f }, { "idamp", 0.2f }, { "ilevel", 0.7f } } },
            { "Kensho", { { "perspective", 1.0f }, { "rlevel", 0.45f }, { "ilevel", 0.55f }, { "width", 1.0f } } },
        };
        return list;
    }
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout DualityProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (pctText);
    auto unit = NormalisableRange<float> (0.0f, 1.0f, 0.001f);
    auto addPct = [&] (const char* id, const char* name, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, unit, def, pct));
    };
    auto msParam = [&] (const char* id, const char* name, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 200.0f, 0.1f, 0.5f), def,
            AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " ms"; })));
    };

    // Object: the algorithmic reverb
    addPct ("rsize", "Reverb Size", 0.50f);
    addPct ("rdamp", "Reverb Damping", 0.40f);
    msParam ("rpre", "Reverb Pre-delay", 12.0f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "rtype", 1 }, "Reverb Type", StringArray { "Room", "Hall", "Plate" }, 1));
    addPct ("rlevel", "Reverb Level", 0.50f);

    // The point between the two
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "perspective", 1 }, "Perspective", StringArray { "Object", "Kensho", "Subject" }, 1));

    // Subject: the impulse response
    addPct ("idecay", "IR Decay", 1.0f);
    addPct ("idamp", "IR Damping", 0.20f);
    msParam ("ipre", "IR Pre-delay", 0.0f);
    addPct ("ilevel", "IR Level", 0.50f);

    // Output
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width", NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
    addPct ("mix", "Mix", 0.30f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return { p.begin(), p.end() };
}

DualityProcessor::DualityProcessor()
    : ArtifactsProcessor (BusesProperties()
                            .withInput  ("Input",  AudioChannelSet::stereo(), true)
                            .withOutput ("Output", AudioChannelSet::stereo(), true),
                          createLayout(), "Duality")
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pSize = get ("rsize"); pDamp = get ("rdamp"); pPre = get ("rpre"); pType = get ("rtype"); pLevel = get ("rlevel");
    pPersp = get ("perspective"); pIDecay = get ("idecay"); pIDamp = get ("idamp"); pIPre = get ("ipre"); pILevel = get ("ilevel");
    pWidth = get ("width"); pMix = get ("mix"); pOutput = get ("output");
    loadBuiltInIR (2 - 1);   // Chapel to begin with
}

bool DualityProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet(), in = layouts.getMainInputChannelSet();
    return out == AudioChannelSet::stereo() && (in == AudioChannelSet::stereo() || in == AudioChannelSet::mono());
}

int DualityProcessor::numBuiltInIRs() { return numBuilt; }
String DualityProcessor::builtInIRName (int i) { return builtNames[jlimit (0, numBuilt - 1, i)]; }

//==============================================================================
void DualityProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = samplesPerBlock;
    dsp::ProcessSpec spec { sampleRate, (uint32) samplesPerBlock, 2 };
    for (auto* d : { &preRev, &preIR })
    {
        d->setMaximumDelayInSamples ((int) std::ceil (0.21 * sampleRate) + 8);
        d->prepare (spec);
    }
    plate.prepare (sampleRate);
    hall.prepare (sampleRate);
    hall.setOutputGain (1.8f);
    conv.prepare (spec);
    pushIRToEngine();

    for (int c = 0; c < 2; ++c)
    {
        // a gentle high shelf on the wet bus: the top of a real space is always a little softer than the source
        fuse[c].coefficients = dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 7500.0, 0.6, Decibels::decibelsToGain (-1.5f));
        fuse[c].reset();
        air[c].coefficients = dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 30.0, 0.7);   // no sub-bass mud from the tails
        air[c].reset();
        const int k = (int) (sampleRate / 48000.0 * 100.0) / 100;
        const int lens[2][2] = { { 241, 389 }, { 281, 433 } };
        { const int rl[2][3] = { { 113, 181, 257 }, { 127, 199, 281 } };
          for (int s = 0; s < 3; ++s) { roomDiff[c][s].setSize (std::max (2, (int) (rl[c][s] * sampleRate / 48000.0))); roomDiff[c][s].g = 0.6f; } }
        for (int s = 0; s < 2; ++s) { blend[c][s].setSize (std::max (2, (int) (lens[c][s] * sampleRate / 48000.0))); blend[c][s].g = 0.42f; }
        juce::ignoreUnused (k);
    }

    for (auto* s : { &sSize, &sDamp, &sLevel, &sILevel, &sMix, &sIDecay, &sIDamp }) s->reset (sampleRate, 0.05);
    sPre.reset (sampleRate, 0.08); sIPre.reset (sampleRate, 0.08);
    sWidth.reset (sampleRate, 0.05); sOut.reset (sampleRate, 0.03);
    gAlg.reset (sampleRate, 0.08); gIR.reset (sampleRate, 0.08);
    sSize.setCurrentAndTargetValue (pSize->load()); sDamp.setCurrentAndTargetValue (pDamp->load());
    sPre.setCurrentAndTargetValue (pPre->load() * 0.001f * (float) sr); sIPre.setCurrentAndTargetValue (pIPre->load() * 0.001f * (float) sr);
    sLevel.setCurrentAndTargetValue (pLevel->load()); sILevel.setCurrentAndTargetValue (pILevel->load());
    sWidth.setCurrentAndTargetValue (pWidth->load()); sMix.setCurrentAndTargetValue (pMix->load());
    sOut.setCurrentAndTargetValue (Decibels::decibelsToGain (pOutput->load()));
    const int persp = roundToInt (pPersp->load());
    gAlg.setCurrentAndTargetValue (persp <= 1 ? 1.0f : 0.0f);
    gIR.setCurrentAndTargetValue (persp >= 1 ? 1.0f : 0.0f);
    algRunning = persp <= 1; irRunning = persp >= 1;

    dryBuf.setSize (2, samplesPerBlock); algBuf.setSize (2, samplesPerBlock); irBuf.setSize (2, samplesPerBlock);
    setLatencySamples (0);
}

//==============================================================================
void DualityProcessor::shapeIR (AudioBuffer<float>& shaped, double rate, bool mono, float decay, float damp)
{
    const int n = shaped.getNumSamples();
    if (mono)
    {
        // different short allpass chains on each side: same tone, different fine structure, so it is wide
        const float msL[] = { 1.3f, 2.9f, 4.7f, 7.1f }, msR[] = { 1.7f, 3.4f, 5.3f, 6.2f };
        for (int ch = 0; ch < 2; ++ch)
            for (float ms : (ch == 0 ? msL : msR))
            {
                const int d = jmax (1, (int) (ms * 0.001 * rate));
                std::vector<float> buf ((size_t) d, 0.0f);
                int w = 0;
                auto* x = shaped.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    const float delayed = buf[(size_t) w];
                    const float v = x[i] + 0.55f * delayed;
                    buf[(size_t) w] = v;
                    w = (w + 1) % d;
                    x[i] = delayed - 0.55f * v;
                }
            }
    }
    if (decay < 0.995f)   // Decay: 100 % is the IR as recorded; below that its tail is shortened with a natural, faster fall
    {
        const double amount = 1.0 - decay;
        const double dbPerSecond = 90.0 * std::pow (amount, 1.4);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* x = shaped.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                x[i] *= (float) std::pow (10.0, -dbPerSecond * (i / rate) / 20.0);
        }
    }
    if (damp > 0.01f)   // Damping: the tail darkens over time, like air absorption; the first reflections stay intact
    {
        const double fcEnd = 20000.0 * std::pow (0.12, (double) damp);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* x = shaped.getWritePointer (ch);
            double y1 = 0.0, y2 = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double t = jmin (1.0, (i / rate) / 1.5);
                const double fc = jmin (20000.0 * std::pow (fcEnd / 20000.0, t), rate * 0.45);
                const double a = 1.0 - std::exp (-MathConstants<double>::twoPi * fc / rate);
                y1 += a * (x[i] - y1);
                y2 += a * (y1 - y2);   // two poles, so the roll-off is smooth rather than abrupt
                const double ease = jmin (1.0, t / 0.02);
                x[i] = (float) (x[i] * (1.0 - ease) + y2 * ease);
            }
        }
    }
}

void DualityProcessor::pushIRToEngine()
{
    AudioBuffer<float> shaped;
    double rate;
    bool mono;
    const float decay = pIDecay->load(), damp = pIDamp->load();
    {
        const ScopedLock sl (irLock);
        if (irSource.getNumSamples() == 0) return;
        shaped = irSource;
        rate = irRate; mono = irMono;
    }
    shapeIR (shaped, rate, mono, decay, damp);
    appliedIRDecay = decay; appliedIRDamp = damp;
    conv.loadImpulseResponse (std::move (shaped), rate, dsp::Convolution::Stereo::yes, dsp::Convolution::Trim::yes, dsp::Convolution::Normalise::yes);
}

void DualityProcessor::loadBuiltInIR (int index)
{
    index = jlimit (0, numBuilt - 1, index);
    auto ir = makeBuiltIn (index, 48000.0);
    {
        const ScopedLock sl (irLock);
        irInfo = describe (ir, 48000.0, builtNames[index], false);
        irSource = std::move (ir);
        irRate = 48000.0; irMono = false;
    }
    apvts.state.setProperty ("irFile", "", nullptr);
    apvts.state.setProperty ("irBuiltIn", index, nullptr);
    pushIRToEngine();
}

String DualityProcessor::loadIR (const File& file)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 16 || reader->sampleRate <= 0)
        return "Couldn't read " + file.getFileName();
    const double fileRate = reader->sampleRate;
    const int len = (int) std::min<int64> (reader->lengthInSamples, (int64) (maxIRSeconds * fileRate));
    const int chans = (int) jlimit (1u, 2u, reader->numChannels);
    AudioBuffer<float> tmp (chans, len);
    reader->read (&tmp, 0, len, 0, true, chans > 1);
    AudioBuffer<float> ir (2, len);
    ir.copyFrom (0, 0, tmp, 0, 0, len);
    ir.copyFrom (1, 0, tmp, chans > 1 ? 1 : 0, 0, len);
    if (ir.getMagnitude (0, len) <= 0.0f)
        return file.getFileName() + " is silent";
    {
        const ScopedLock sl (irLock);
        irInfo = describe (ir, fileRate, file.getFileNameWithoutExtension(), chans == 1);
        irSource = std::move (ir);
        irRate = fileRate; irMono = chans == 1;
    }
    apvts.state.setProperty ("irFile", file.getFullPathName(), nullptr);
    pushIRToEngine();
    return {};
}

IRInfo DualityProcessor::getIRInfo() const
{
    const ScopedLock sl (irLock);
    return irInfo;
}

//==============================================================================
void DualityProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    if (buffer.getNumChannels() < 2 || n == 0)
        return;

    // reshape the IR on the message-thread side of things when the knobs moved
    const float dc = pIDecay->load(), dm = pIDamp->load();
    if ((std::abs (dc - appliedIRDecay) > 0.004f || std::abs (dm - appliedIRDamp) > 0.004f) && ! irDirty.exchange (true))
    {
        pendingDecay = dc; pendingDamp = dm;
        MessageManager::callAsync ([this] { irDirty = false; pushIRToEngine(); });
    }

    const int persp = roundToInt (pPersp->load());
    const bool wantAlg = persp <= 1, wantIR = persp >= 1;
    gAlg.setTargetValue (wantAlg ? (persp == 1 ? 0.72f : 1.0f) : 0.0f);
    gIR.setTargetValue (wantIR ? (persp == 1 ? 0.72f : 1.0f) : 0.0f);   // Kensho: both come down a little, so they sum like one space
    sSize.setTargetValue (pSize->load()); sDamp.setTargetValue (pDamp->load());
    sPre.setTargetValue (pPre->load() * 0.001f * (float) sr); sIPre.setTargetValue (pIPre->load() * 0.001f * (float) sr);
    sLevel.setTargetValue (pLevel->load()); sILevel.setTargetValue (pILevel->load());
    sWidth.setTargetValue (pWidth->load()); sMix.setTargetValue (pMix->load());
    sOut.setTargetValue (Decibels::decibelsToGain (pOutput->load()));

    for (int c = 0; c < 2; ++c) dryBuf.copyFrom (c, 0, buffer, c, 0, n);
    algBuf.clear (0, n); irBuf.clear (0, n);
    float* aL = algBuf.getWritePointer (0); float* aR = algBuf.getWritePointer (1);
    float* iL = irBuf.getWritePointer (0);  float* iR = irBuf.getWritePointer (1);
    const float* inL = dryBuf.getReadPointer (0); const float* inR = dryBuf.getReadPointer (1);

    // ---- Object: pre-delay, then the algorithmic reverb ----
    const bool algActive = wantAlg || gAlg.isSmoothing() || gAlg.getCurrentValue() > 1.0e-4f;
    if (algActive)
    {
        if (! algRunning) { plate.reset(); hall.reset(); algRunning = true; }
        const int type = jlimit (0, 2, roundToInt (pType->load()));
        const float size = sSize.getTargetValue(), damp = sDamp.getTargetValue();
        for (int i = 0; i < n; ++i)
        {
            const float pd = sPre.getNextValue();
            preRev.pushSample (0, inL[i]); preRev.pushSample (1, inR[i]);
            aL[i] = preRev.popSample (0, pd); aR[i] = preRev.popSample (1, pd);
        }
        if (type == 2) { plate.setParameters (size, damp); plate.process (aL, aR, n); }
        else if (type == 1) { hall.setParameters (size, damp); hall.process (aL, aR, n); }
        else
        {   // Room: the hall engine made small and close, with the input diffused first so small spaces don't ring
            for (int i = 0; i < n; ++i)
            {
                float l = aL[i], r = aR[i];
                for (int s = 0; s < 3; ++s) { l = roomDiff[0][s].process (l); r = roomDiff[1][s].process (r); }
                aL[i] = l; aR[i] = r;
            }
            hall.setParameters (size * 0.4f, jlimit (0.0f, 1.0f, damp * 0.8f + 0.1f)); hall.process (aL, aR, n);
        }
        const float makeup = type == 2 ? 1.0f : type == 1 ? 1.0f : 1.35f;
        for (int i = 0; i < n; ++i)
        {
            const float g = gAlg.getNextValue() * sLevel.getNextValue() * 1.6f * makeup;
            aL[i] *= g; aR[i] *= g;
        }
    }
    else { if (algRunning) { plate.reset(); hall.reset(); algRunning = false; } gAlg.skip (n); sLevel.skip (n); sSize.skip (n); sDamp.skip (n); sPre.skip (n); }

    // ---- Subject: pre-delay, then the impulse response ----
    const bool irActive = wantIR || gIR.isSmoothing() || gIR.getCurrentValue() > 1.0e-4f;
    if (irActive)
    {
        irRunning = true;
        for (int i = 0; i < n; ++i)
        {
            const float pd = sIPre.getNextValue();
            preIR.pushSample (0, inL[i]); preIR.pushSample (1, inR[i]);
            iL[i] = preIR.popSample (0, pd); iR[i] = preIR.popSample (1, pd);
        }
        dsp::AudioBlock<float> blk (irBuf.getArrayOfWritePointers(), 2, (size_t) n);
        conv.process (dsp::ProcessContextReplacing<float> (blk));
        for (int i = 0; i < n; ++i)
        {
            const float g = gIR.getNextValue() * sILevel.getNextValue() * 10.3f;   // the convolution engine normalises the IR; this brings it to the same loudness as the reverbs
            iL[i] *= g; iR[i] *= g;
        }
    }
    else { if (irRunning) { conv.reset(); irRunning = false; } gIR.skip (n); sILevel.skip (n); sIPre.skip (n); }

    // ---- Kensho: fuse the two spaces with a shared diffusion, so they read as one ----
    const bool kensho = persp == 1;
    float* oL = buffer.getWritePointer (0); float* oR = buffer.getWritePointer (1);
    double sumL = 0, sumR = 0, inSumL = 0, inSumR = 0;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float wl = aL[i] + iL[i], wr = aR[i] + iR[i];
        if (kensho)
        {
            const float fl = blend[0][1].process (blend[0][0].process (wl));
            const float fr = blend[1][1].process (blend[1][0].process (wr));
            wl = fl; wr = fr;   // a pure all-pass chain: it smears the phase of both spaces together without colouring the tone
        }
        wl = air[0].processSample (fuse[0].processSample (wl));
        wr = air[1].processSample (fuse[1].processSample (wr));
        // width of the wet signal (mid/side)
        const float w = sWidth.getNextValue();
        const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * w;
        wl = m + s; wr = m - s;
        const float mix = sMix.getNextValue(), og = sOut.getNextValue();
        oL[i] = (inL[i] * (1.0f - mix) + wl * mix) * og;
        oR[i] = (inR[i] * (1.0f - mix) + wr * mix) * og;
        sumL += oL[i] * oL[i]; sumR += oR[i] * oR[i];
        inSumL += inL[i] * inL[i]; inSumR += inR[i] * inR[i];
        peak = std::max ({ peak, std::abs (oL[i]), std::abs (oR[i]) });
    }
    meterOut[0] = (float) (sumL / n); meterOut[1] = (float) (sumR / n);
    meterIn[0] = (float) (inSumL / n); meterIn[1] = (float) (inSumR / n);
    peakOut = peak;
}

//==============================================================================
int DualityProcessor::getNumPrograms() { return (int) programs().size(); }
const String DualityProcessor::getProgramName (int i) { return isPositiveAndBelow (i, getNumPrograms()) ? String (programs()[(size_t) i].name) : String(); }

void DualityProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (param))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, value] : programs()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void DualityProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DualityProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = ValueTree::fromXml (*xml);
    currentProgram = jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
    apvts.replaceState (state);
    // bring back the impulse response this session was using
    const String file = state.getProperty ("irFile").toString();
    if (file.isNotEmpty() && File (file).existsAsFile()) loadIR (File (file));
    else loadBuiltInIR ((int) state.getProperty ("irBuiltIn", 1));
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

AudioProcessorEditor* DualityProcessor::createEditor() { return new ArtifactsEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DualityProcessor(); }
