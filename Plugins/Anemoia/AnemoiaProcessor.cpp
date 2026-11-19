#include "AnemoiaProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
    String pctText (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }

    float softLimit (float v)   // transparent below 0.8, a gentle round-off above it
    {
        const float a = std::abs (v);
        if (a < 0.8f) return v;
        return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), v);
    }

    struct Program { const char* name; std::vector<std::pair<const char*, float>> values; };
    const std::vector<Program>& programs()
    {
        static const std::vector<Program> list {
            { "Anemoia", {} },
            { "Tape Memory", { { "time", 410.0f }, { "feedback", 0.45f }, { "tone", 0.35f }, { "memon", 0.0f }, { "mix", 0.3f } } },
            { "Dotted Evening", { { "sync", 1.0f }, { "division", 2.0f }, { "feel", 1.0f }, { "feedback", 0.4f }, { "tone", 0.45f }, { "width", 1.0f }, { "texture", 0.35f }, { "nostalgia", 0.35f }, { "mix", 0.3f } } },
            { "Cloud Nine", { { "echoon", 0.0f }, { "pattern", 5.0f }, { "texture", 0.3f }, { "blur", 0.8f }, { "bloom", 0.55f }, { "shimmer", 0.4f }, { "nostalgia", 0.6f }, { "mix", 0.45f } } },
            { "Frozen Hall", { { "echoon", 0.0f }, { "pattern", 2.0f }, { "texture", 0.2f }, { "blur", 0.9f }, { "bloom", 0.4f }, { "nostalgia", 0.5f }, { "mix", 0.5f } } },
        };
        return list;
    }
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout AnemoiaProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (pctText);
    auto unit = NormalisableRange<float> (0.0f, 1.0f, 0.001f);
    auto addPct = [&] (const char* id, const char* name, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, unit, def, pct));
    };
    auto addBool = [&] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, StringArray { "Out", "In" }, def ? 1 : 0));
    };

    // Echos
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "time", 1 }, "Time", NormalisableRange<float> (20.0f, 2000.0f, 0.1f, 0.4f), 375.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "division", 1 }, "Division", StringArray { "1/2", "1/4", "1/8", "1/16", "1/32" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "feel", 1 }, "Feel", StringArray { "Straight", "Dotted", "Triplet" }, 0));
    addBool ("sync", "Sync", false);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "feedback", 1 }, "Feedback", NormalisableRange<float> (0.0f, 0.92f, 0.001f), 0.35f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f / 0.92f)) + " %"; })));
    addPct ("tone", "Tone", 0.5f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width", unit, 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.25f ? String ("Mono") : v > 0.75f ? String ("Ping-pong") : String ("Stereo"); })));
    addBool ("echoon", "Echos", true);

    // Memories
    addPct ("texture", "Texture", 0.4f);
    addPct ("blur", "Blur", 0.5f);
    addPct ("bloom", "Bloom", 0.25f);
    addPct ("shimmer", "Shimmer", 0.0f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "pattern", 1 }, "Pattern", StringArray { "Scatter", "Glide", "Hold", "Reverse", "Swell", "Cloud" }, 0));
    addPct ("nostalgia", "Nostalgia", 0.3f);
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "pitch", 1 }, "Pitch", -12, 12, 0));
    addBool ("memon", "Memories", true);

    // Output
    addPct ("mix", "Mix", 0.30f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return { p.begin(), p.end() };
}

AnemoiaProcessor::AnemoiaProcessor()
    : ArtifactsProcessor (BusesProperties()
                            .withInput  ("Input",  AudioChannelSet::stereo(), true)
                            .withOutput ("Output", AudioChannelSet::stereo(), true),
                          createLayout(), "Anemoia")
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pTime = get ("time"); pDiv = get ("division"); pFeel = get ("feel"); pSync = get ("sync"); pFb = get ("feedback");
    pTone = get ("tone"); pWidth = get ("width"); pEchoOn = get ("echoon"); pMemOn = get ("memon");
    pTexture = get ("texture"); pBlur = get ("blur"); pBloom = get ("bloom"); pShimmer = get ("shimmer");
    pPattern = get ("pattern"); pNost = get ("nostalgia"); pPitch = get ("pitch"); pMix = get ("mix"); pOutput = get ("output");
}

bool AnemoiaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet(), in = layouts.getMainInputChannelSet();
    return out == AudioChannelSet::stereo() && (in == AudioChannelSet::stereo() || in == AudioChannelSet::mono());
}

//==============================================================================
void AnemoiaProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    dN = (int) std::ceil (maxDelaySeconds * sr) + 8;
    mN = (int) std::ceil (memSeconds * sr) + 8;
    for (int c = 0; c < 2; ++c)
    {
        dBuf[c].assign ((size_t) dN, 0.0f);
        mBuf[c].assign ((size_t) mN, 0.0f);
        mBufLP[c].assign ((size_t) mN, 0.0f);
        recLP[c].set (0.23 * sr, sr); recLP[c].reset(); warmLP[c].reset(); regenLP[c].set (5500.0, sr); regenLP[c].reset();
        regenPrev[c] = 0.0f; dcX[c] = dcY[c] = 0.0f; dHPx[c] = dHPy[c] = 0.0f;
        for (int k = 0; k < 2; ++k) { dLP[c][k] = 0.0f; toneLP[c][k] = 0.0f; }
        const int lens[2][2] = { { 223, 367 }, { 257, 401 } };
        for (int s = 0; s < 2; ++s) { diff[c][s].setSize ((int) (lens[c][s] * sr / 48000.0)); diff[c][s].g = 0.5f; }
    }
    dW = mW = frozenW = 0; frozen = false;
    for (auto& g : grains) g.active = false;
    spawnCount = 0;
    sTime.reset (sampleRate, 0.25);     // time changes glide like a tape machine's, instead of clicking
    sTime.setCurrentAndTargetValue (jmax (20.0f, pTime->load()));
    for (auto* s : { &sFb, &sTone, &sWidth, &sMix, &sTexture, &sBlur, &sBloom, &sShimmer, &sNost }) s->reset (sampleRate, 0.05);
    sOut.reset (sampleRate, 0.03); gEcho.reset (sampleRate, 0.04); gMem.reset (sampleRate, 0.04);
    sFb.setCurrentAndTargetValue (pFb->load()); sTone.setCurrentAndTargetValue (pTone->load());
    sWidth.setCurrentAndTargetValue (pWidth->load()); sMix.setCurrentAndTargetValue (pMix->load());
    sTexture.setCurrentAndTargetValue (pTexture->load()); sBlur.setCurrentAndTargetValue (pBlur->load());
    sBloom.setCurrentAndTargetValue (pBloom->load()); sShimmer.setCurrentAndTargetValue (pShimmer->load());
    sNost.setCurrentAndTargetValue (pNost->load());
    sOut.setCurrentAndTargetValue (Decibels::decibelsToGain (pOutput->load()));
    gEcho.setCurrentAndTargetValue (pEchoOn->load() > 0.5f ? 1.0f : 0.0f);
    gMem.setCurrentAndTargetValue (pMemOn->load() > 0.5f ? 1.0f : 0.0f);
    setLatencySamples (0);
}

//==============================================================================
float AnemoiaProcessor::readH (const std::vector<float>& b, double p) const
{
    const int n = (int) b.size();
    p = std::fmod (p, (double) n); if (p < 0) p += n;
    const int i = (int) p;
    const float f = (float) (p - i);
    const float y0 = b[(size_t) ((i + n - 1) % n)], y1 = b[(size_t) i], y2 = b[(size_t) ((i + 1) % n)], y3 = b[(size_t) ((i + 2) % n)];
    const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

float AnemoiaProcessor::windowAt (float x, float alpha) const   // Tukey: flat top with cosine skirts; Blur widens the skirts
{
    if (x <= 0.0f || x >= 1.0f) return 0.0f;
    const float h = alpha * 0.5f;
    if (x < h) return 0.5f * (1.0f - std::cos (MathConstants<float>::pi * x / h));
    if (x > 1.0f - h) return 0.5f * (1.0f - std::cos (MathConstants<float>::pi * (1.0f - x) / h));
    return 1.0f;
}

void AnemoiaProcessor::spawnGrain (int pattern, float texture, float blur, float shimmer, float nost, int pitchSt, float width)
{
    Grain* g = nullptr;
    for (auto& c : grains) if (! c.active) { g = &c; break; }
    if (g == nullptr) return;

    float sizeS = 0.55f * std::pow (0.08f / 0.55f, texture);
    if (pattern == 4) sizeS = jmin (0.7f, sizeS * 1.8f);       // Swell: longer grains
    if (pattern == 5) sizeS = jmin (0.6f, sizeS * 1.4f);       // Cloud
    float semis = (float) pitchSt;
    float slope = 0.0f;
    float spread = 0.4f + 0.6f * nost;                         // how far back into the past a grain may reach (seconds * 3)
    float pan = 0.5f;
    const float detune = (rng.nextFloat() * 2.0f - 1.0f) * (0.04f + 0.22f * nost);   // semitones: a slightly unsteady memory
    semis += detune;
    if (pattern == 5)
    {
        static const float set[] = { 0, 0, 7, 12, -12, 0, 5 };
        semis += set[rng.nextInt (7)];
        pan = rng.nextFloat();
    }
    else pan = 0.5f + (rng.nextFloat() - 0.5f) * (0.35f + 0.65f * nost);
    if (rng.nextFloat() < shimmer * 0.85f) { semis += 12.0f; }   // an octave above: the shimmer
    if (pattern == 1) slope = (rng.nextBool() ? 1.0f : -1.0f) * (0.08f + 0.1f * nost);

    float rate = std::pow (2.0f, semis / 12.0f);
    if (pattern == 3) rate = -rate;
    const bool frozenNow = (pattern == 2);

    const int len = jmax (64, (int) (sizeS * (float) sr));
    float back;   // seconds behind the write head the grain starts
    const float lenS = len / (float) sr;
    switch (pattern)
    {
        case 1:  glideBack += glideDir * (0.05f + 0.15f * rng.nextFloat()); if (glideBack > 2.5f * spread + 0.5f) glideDir = -1.0f; if (glideBack < 0.1f) glideDir = 1.0f;
                 back = glideBack; break;
        case 2:  back = 0.1f + rng.nextFloat() * (0.5f + 1.5f * spread); break;
        case 3:  back = 0.15f + rng.nextFloat() * (0.4f + 2.0f * spread); break;
        case 5:  back = 0.1f + rng.nextFloat() * (1.0f + 3.0f * spread); break;
        default: back = 0.06f + rng.nextFloat() * (0.35f + 2.5f * spread); break;
    }
    const float drift = frozenNow ? -std::abs (rate) : (1.0f - rate);   // how the distance to the head changes over a grain
    const float lo = jmax (0.012f, 0.012f - lenS * drift);
    const float hi = (float) memSeconds - 0.4f - jmax (0.0f, lenS * drift);
    back = jlimit (lo, jmax (lo, hi), back);

    const int head = frozenNow ? frozenW : mW;
    g->pos = (double) head - (double) back * sr;
    g->rate = rate; g->slope = slope; g->age = 0; g->len = len;
    g->alpha = 0.3f + 0.7f * blur;
    g->amp = 1.0f;
    const float p = jlimit (0.0f, 1.0f, 0.5f + (pan - 0.5f) * (0.4f + width));   // keep it centred when the plugin is set to mono
    g->gL = jmin (1.0f, 2.0f * (1.0f - p)); g->gR = jmin (1.0f, 2.0f * p);
    g->useLP = std::abs (rate) > 1.05f;
    g->active = true;
}

//==============================================================================
void AnemoiaProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    if (buffer.getNumChannels() < 2 || n == 0) return;

    // ---- host tempo ----
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) if (*b > 20.0 && *b < 400.0) bpm = *b;
    float timeMs = pTime->load();
    if (pSync->load() > 0.5f)
    {
        static const double beats[] = { 2.0, 1.0, 0.5, 0.25, 0.125 };
        const int d = jlimit (0, 4, roundToInt (pDiv->load())), f = jlimit (0, 2, roundToInt (pFeel->load()));
        timeMs = (float) (60000.0 / bpm * beats[d] * (f == 1 ? 1.5 : f == 2 ? 2.0 / 3.0 : 1.0));
    }
    timeMs = jlimit (20.0f, (float) (maxDelaySeconds * 1000.0 - 80.0), timeMs);
    sTime.setTargetValue (timeMs);
    shownDelayMs = timeMs;

    sFb.setTargetValue (pFb->load()); sTone.setTargetValue (pTone->load());
    sWidth.setTargetValue (pWidth->load()); sMix.setTargetValue (pMix->load());
    sTexture.setTargetValue (pTexture->load()); sBlur.setTargetValue (pBlur->load());
    sBloom.setTargetValue (pBloom->load()); sShimmer.setTargetValue (pShimmer->load()); sNost.setTargetValue (pNost->load());
    sOut.setTargetValue (Decibels::decibelsToGain (pOutput->load()));
    gEcho.setTargetValue (pEchoOn->load() > 0.5f ? 1.0f : 0.0f);
    gMem.setTargetValue (pMemOn->load() > 0.5f ? 1.0f : 0.0f);
    const int pattern = jlimit (0, 5, roundToInt (pPattern->load()));
    const int pitchSt = roundToInt (pPitch->load());

    if (pattern == 2 && ! frozen) { frozen = true; frozenW = mW; }
    if (pattern != 2) frozen = false;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    double sI = 0, sO = 0, sIR = 0, sOR = 0;
    float peak = 0.0f;
    const float toneCoefScale = 1.0f;
    juce::ignoreUnused (toneCoefScale);

    for (int i = 0; i < n; ++i)
    {
        const float in[2] = { L[i], R[i] };
        const float fb = sFb.getNextValue(), tone = sTone.getNextValue(), width = sWidth.getNextValue(), mix = sMix.getNextValue();
        const float ge = gEcho.getNextValue(), gm = gMem.getNextValue();
        const float texture = sTexture.getNextValue(), blur = sBlur.getNextValue(), bloom = sBloom.getNextValue(),
                    shimmer = sShimmer.getNextValue(), nost = sNost.getNextValue();
        const float og = sOut.getNextValue();

        // ================= Echos =================
        const float tMs = sTime.getNextValue();
        const float delaySamp = tMs * 0.001f * (float) sr;
        // tone: 0 = dark (tape-like), 1 = open. Applied in the feedback path only, so every repeat is a little softer than the last.
        const double fcTone = 1800.0 * std::pow (12000.0 / 1800.0, (double) tone);
        const float aT = (float) (1.0 - std::exp (-MathConstants<double>::twoPi * fcTone / sr));
        const float mono = 0.5f * (in[0] + in[1]);
        const float wm = jlimit (0.0f, 1.0f, (0.5f - width) * 2.0f), x = jlimit (0.0f, 1.0f, (width - 0.5f) * 2.0f);
        const float src[2] = { in[0] + wm * (mono - in[0]), in[1] + wm * (mono - in[1]) };

        float dl[2];
        for (int c = 0; c < 2; ++c)
        {
            wowPh[c] += (c == 0 ? 0.31f : 0.47f) / (float) sr * MathConstants<float>::twoPi;
            if (wowPh[c] > MathConstants<float>::twoPi) wowPh[c] -= MathConstants<float>::twoPi;
            const float wow = std::sin (wowPh[c]) * 0.00025f * (float) sr * jmin (1.0f, tMs / 300.0f);   // a very slight tape unsteadiness
            const double rp = (double) dW - (double) (delaySamp + wow);
            dl[c] = readH (dBuf[c], rp);
        }
        float echoOut[2], fbIn[2];
        for (int c = 0; c < 2; ++c)
        {
            const float own = dl[c], other = dl[1 - c];
            float f = (1.0f - x) * own + x * other;
            f *= fb * ge;
            // tone + a gentle low cut so repeated bass doesn't build up
            toneLP[c][0] += aT * (f - toneLP[c][0]);
            toneLP[c][1] += aT * (toneLP[c][0] - toneLP[c][1]);
            float t = toneLP[c][1];
            const float hp = t - dHPx[c] + 0.9955f * dHPy[c]; dHPx[c] = t; dHPy[c] = hp;
            fbIn[c] = softLimit (hp);
            echoOut[c] = dl[c] * ge;
        }
        const float inGain = ge;
        const float w0 = src[0] * inGain + fbIn[0];
        const float w1 = src[1] * inGain * (1.0f - 0.85f * x) + fbIn[1];   // ping-pong: the signal starts on the left
        dBuf[0][(size_t) dW] = w0; dBuf[1][(size_t) dW] = w1;
        if (++dW >= dN) dW = 0;

        // ================= Memories =================
        const float mIn[2] = { src[0] * ge + src[0] * (1.0f - ge) + echoOut[0], src[1] * ge + src[1] * (1.0f - ge) + echoOut[1] };
        float grainL = 0.0f, grainR = 0.0f;
        const bool memActive = gm > 1.0e-4f;
        if (memActive)
        {
            // write what we hear, plus what bloomed back (through a soft low-pass)
            if (! frozen)
            {
                for (int c = 0; c < 2; ++c)
                {
                    float v = mIn[c] + bloom * 0.55f * regenPrev[c];
                    // remove DC from the loop
                    const float y = v - dcX[c] + 0.995f * dcY[c]; dcX[c] = v; dcY[c] = y;
                    v = softLimit (y);
                    mBuf[c][(size_t) mW] = v;
                    mBufLP[c][(size_t) mW] = recLP[c].process (v);
                }
                if (++mW >= mN) mW = 0;
            }
            else for (int c = 0; c < 2; ++c) recLP[c].process (0.0f);

            // scheduling
            const float sizeS = 0.55f * std::pow (0.08f / 0.55f, texture) * (pattern == 4 ? 1.8f : pattern == 5 ? 1.4f : 1.0f);
            const float overlap = 2.2f + 3.5f * nost + (pattern == 5 ? 1.5f : 0.0f);
            if (--spawnCount <= 0)
            {
                spawnGrain (pattern, texture, blur, shimmer, nost, pitchSt, width);
                spawnCount = jmax (16, (int) (sizeS * (float) sr / overlap * (0.75f + 0.5f * rng.nextFloat())));
            }
            wobblePh += 0.6f / (float) sr * MathConstants<float>::twoPi; if (wobblePh > MathConstants<float>::twoPi) wobblePh -= MathConstants<float>::twoPi;
            const float wobble = 1.0f + nost * 0.0035f * std::sin (wobblePh);
            int live = 0;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                ++live;
                const float ph = (float) g.age / (float) g.len;
                const float env = windowAt (ph, g.alpha) * g.amp;
                const auto& bufL = g.useLP ? mBufLP[0] : mBuf[0];
                const auto& bufR = g.useLP ? mBufLP[1] : mBuf[1];
                const float sl = readH (bufL, g.pos), sr_ = readH (bufR, g.pos);
                grainL += sl * env * g.gL; grainR += sr_ * env * g.gR;
                g.pos += (double) (g.rate * wobble * (1.0f + g.slope * (ph - 0.5f)));
                if (++g.age >= g.len) g.active = false;
            }
            // level: constant perceived loudness whatever the density
            const float norm = 1.0f / std::sqrt (jmax (1.0f, 0.45f * (float) jmax (1, live)));
            float lvl = 0.85f * norm;
            if (pattern == 4)
            {
                swellPh += 0.11f / (float) sr; if (swellPh > 1.0f) swellPh -= 1.0f;
                lvl *= 0.15f + 0.85f * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * swellPh));
            }
            grainL *= lvl; grainR *= lvl;
            // warmth: Nostalgia darkens the cloud
            const double fcW = 15000.0 * std::pow (3600.0 / 15000.0, (double) nost);
            warmLP[0].set (fcW, sr); warmLP[1].set (fcW, sr);
            grainL = warmLP[0].process (grainL); grainR = warmLP[1].process (grainR);
            // blur: a short shared diffusion, so the cloud smears into itself
            const float dm = 0.55f * blur;
            const float dL = diff[0][1].process (diff[0][0].process (grainL)), dR = diff[1][1].process (diff[1][0].process (grainR));
            grainL += dm * (dL - grainL); grainR += dm * (dR - grainR);
            regenPrev[0] = regenLP[0].process (grainL); regenPrev[1] = regenLP[1].process (grainR);
            grainL = softLimit (grainL) * gm; grainR = softLimit (grainR) * gm;
        }
        else { regenPrev[0] = regenPrev[1] = 0.0f; }

        // ================= Mix =================
        // the wet side is the repeats plus the cloud; the dry side is the untouched input
        const float wetL = echoOut[0] + grainL, wetR = echoOut[1] + grainR;
        const float wG = 1.25f;
        const float oL = (in[0] * (1.0f - mix) + wetL * mix * wG) * og;
        const float oR = (in[1] * (1.0f - mix) + wetR * mix * wG) * og;
        L[i] = oL; R[i] = oR;
        sI += in[0] * in[0]; sIR += in[1] * in[1]; sO += oL * oL; sOR += oR * oR;
        peak = jmax (peak, std::abs (oL), std::abs (oR));
    }
    meterIn[0] = (float) (sI / n); meterIn[1] = (float) (sIR / n);
    meterOut[0] = (float) (sO / n); meterOut[1] = (float) (sOR / n);
    peakOut = peak;
}

//==============================================================================
int AnemoiaProcessor::getNumPrograms() { return (int) programs().size(); }
const String AnemoiaProcessor::getProgramName (int i) { return isPositiveAndBelow (i, getNumPrograms()) ? String (programs()[(size_t) i].name) : String(); }

void AnemoiaProcessor::setCurrentProgram (int index)
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

void AnemoiaProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AnemoiaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = ValueTree::fromXml (*xml);
    currentProgram = jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
    apvts.replaceState (state);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

AudioProcessorEditor* AnemoiaProcessor::createEditor() { return new ArtifactsEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AnemoiaProcessor(); }
