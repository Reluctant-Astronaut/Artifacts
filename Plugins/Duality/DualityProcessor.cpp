#include "DualityProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
    String pctText (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }
    String hzText (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; }

    struct Program { const char* name; std::vector<std::pair<const char*, float>> values; };
    const std::vector<Program>& programs()
    {
        static const std::vector<Program> list {
            { "Duality", {} },
            { "Vocal Plate", { { "pdecay", 2.2f }, { "pdamp", 0.35f }, { "ppre", 25.0f } } },
            { "Abbey Road Plate", { { "pdecay", 2.4f }, { "pdamp", 0.3f }, { "ppre", 30.0f }, { "plowcut", 600.0f }, { "phighcut", 10000.0f } } },
            { "Short Bright Plate", { { "pdecay", 1.1f }, { "pdamp", 0.15f }, { "ppre", 10.0f }, { "plowcut", 200.0f } } },
            { "Ballad Plate", { { "pdecay", 3.8f }, { "pdamp", 0.45f }, { "ppre", 60.0f }, { "pduck", 0.4f } } },
            { "Dark Ducked Plate", { { "pdecay", 3.0f }, { "pdamp", 0.75f }, { "ppre", 40.0f }, { "pduck", 0.6f }, { "phighcut", 7000.0f } } },
        };
        return list;
    }

    // two-pole filters from the RBJ cookbook
    template <typename BQ> void designLP (BQ& f, double sr, double fc, double q)
    {
        const double w = MathConstants<double>::twoPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        f.b0 = (1.0 - c) * 0.5 / a0; f.b1 = (1.0 - c) / a0; f.b2 = f.b0; f.a1 = -2.0 * c / a0; f.a2 = (1.0 - al) / a0;
    }
    template <typename BQ> void designHP (BQ& f, double sr, double fc, double q)
    {
        const double w = MathConstants<double>::twoPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        f.b0 = (1.0 + c) * 0.5 / a0; f.b1 = -(1.0 + c) / a0; f.b2 = f.b0; f.a1 = -2.0 * c / a0; f.a2 = (1.0 - al) / a0;
    }
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout DualityProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (pctText);
    auto unit = NormalisableRange<float> (0.0f, 1.0f, 0.001f);
    auto skewed = [] (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi, 0.0f); r.setSkewForCentre (centre); return r; };

    // Plate (Object)
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "pdecay", 2 }, "Plate Decay", skewed (0.3f, 8.0f, 2.0f), 2.2f,
        AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 2 : 1) + " s"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "pdamp", 2 }, "Plate Damping", unit, 0.35f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ppre", 2 }, "Plate Pre-delay", skewed (0.0f, 250.0f, 40.0f), 20.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "plowcut", 2 }, "Plate Low Cut", skewed (20.0f, 1000.0f, 150.0f), 60.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "phighcut", 2 }, "Plate High Cut", skewed (2000.0f, 20000.0f, 8000.0f), 12000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "pduck", 2 }, "Plate Duck", unit, 0.0f, pct));

    // The point between the two
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "perspective", 1 }, "Perspective", StringArray { "Object", "Kensho", "Subject" }, 1));

    // Output
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width", NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", unit, 0.25f, pct));
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
    pDecay = get ("pdecay"); pDamp = get ("pdamp"); pPre = get ("ppre"); pLow = get ("plowcut"); pHigh = get ("phighcut");
    pDuck = get ("pduck"); pPersp = get ("perspective"); pWidth = get ("width"); pMix = get ("mix"); pOutput = get ("output");
    own = { pDecay, pDamp, pPre, pLow, pHigh, pDuck, pPersp, pWidth, pMix, pOutput };
}

void DualityProcessor::pointParamsAt (std::array<std::atomic<float>*, 10> p)
{
    pDecay = p[0]; pDamp = p[1]; pPre = p[2]; pLow = p[3]; pHigh = p[4]; pDuck = p[5]; pPersp = p[6]; pWidth = p[7]; pMix = p[8]; pOutput = p[9];
}

bool DualityProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet(), in = layouts.getMainInputChannelSet();
    // stereo out (from mono or stereo), or mono in and out: a mono vocal track in Cubase, where Duality is often an ARA extension
    return (out == AudioChannelSet::stereo() && (in == AudioChannelSet::stereo() || in == AudioChannelSet::mono()))
        || (out == AudioChannelSet::mono() && in == AudioChannelSet::mono());
}

//==============================================================================
void DualityProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    dsp::ProcessSpec spec { sampleRate, (uint32) samplesPerBlock, 1 };
    preDelay.setMaximumDelayInSamples ((int) std::ceil (0.26 * sampleRate) + 8);
    preDelay.prepare (spec);
    plate.prepare (sampleRate);
    for (auto* f : { &hp[0], &hp[1], &lp[0], &lp[1] }) f->reset();
    appliedLow = appliedHigh = -1.0f;
    duckEnv = -100.0f; duckGain = 1.0f; duckHold = 0;
    deSplit.reset(); designLP (deSplit, sampleRate, 4500.0, 0.7071);
    for (int c = 0; c < 2; ++c)   // the EMT 140's own voice: measured, within 2 dB in every octave from 63 Hz to 12 kHz
    {
        voiceHP[c].reset(); voiceLP[c].reset();
        designHP (voiceHP[c], sampleRate, 230.0, 0.6);
        designLP (voiceLP[c], sampleRate, jmin (2100.0, sampleRate * 0.45), 0.6);
    } deHi = deLo = 0.0f; deGain = 1.0f;

    sPre.reset (sampleRate, 0.08); sWidth.reset (sampleRate, 0.05); sMix.reset (sampleRate, 0.05);
    sOut.reset (sampleRate, 0.03); gPlate.reset (sampleRate, 0.08); sLow.reset (sampleRate, 0.05); sHigh.reset (sampleRate, 0.05);
    sPre.setCurrentAndTargetValue (pPre->load() * 0.001f * (float) sr);
    sWidth.setCurrentAndTargetValue (pWidth->load()); sMix.setCurrentAndTargetValue (pMix->load());
    sOut.setCurrentAndTargetValue (Decibels::decibelsToGain (pOutput->load()));
    sLow.setCurrentAndTargetValue (pLow->load()); sHigh.setCurrentAndTargetValue (pHigh->load());
    const bool on = roundToInt (pPersp->load()) <= 1;
    gPlate.setCurrentAndTargetValue (on ? 1.0f : 0.0f);
    plateRunning = on;
    updateFilters (pLow->load(), pHigh->load());

    dryBuf.setSize (2, samplesPerBlock); wetBuf.setSize (2, samplesPerBlock);
   #if JucePlugin_Enable_ARA
    prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
   #endif
    send.assign ((size_t) samplesPerBlock, 0.0f);
    setLatencySamples (0);
}

void DualityProcessor::updateFilters (float lowCut, float highCut)
{
    if (std::abs (lowCut - appliedLow) < 0.05f && std::abs (highCut - appliedHigh) < 1.0f) return;
    appliedLow = lowCut; appliedHigh = highCut;
    const double nyq = sr * 0.45;
    // fourth-order Butterworth: two sections with Q 0.541 and 1.307
    designHP (hp[0], sr, lowCut, 0.5412); designHP (hp[1], sr, lowCut, 1.3066);
    designLP (lp[0], sr, jmin ((double) highCut, nyq), 0.5412); designLP (lp[1], sr, jmin ((double) highCut, nyq), 1.3066);
}

//==============================================================================
void DualityProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 1) return;
   #if JucePlugin_Enable_ARA
    if (isBoundToARA())
    {
        // an extension on the vocal event: the vocal is Duality's own playback of the event, not the track's input
        AudioPlayHead::PositionInfo pos;
        if (auto* ph = getPlayHead()) if (auto p = ph->getPosition()) pos = *p;
        processBlockForARA (buffer, isRealtime(), pos);
        araTime.store (pos.getTimeInSeconds().orFallback (-1.0));
        araPlaying.store (pos.getIsPlaying());

        // the plate plays with its track's settings, the ones the Lower Zone editor (another instance) sets
        std::array<std::atomic<float>*, 10> use = own;
        if (auto* renderer = getPlaybackRenderer<DualityARA::PlaybackRenderer>(); renderer != nullptr && ! renderer->getPlaybackRegions().empty())
        {
            auto* seq = renderer->getPlaybackRegions().front()->getRegionSequence();
            if (araDoc == nullptr)
                araDoc = ARADocumentControllerSpecialisation::getSpecialisedDocumentController<DualityARA::DocumentController> (renderer->getDocumentController());
            if (seq != araTrack || araSettings == nullptr)
            {
                float init[10]; for (int i = 0; i < 10; ++i) init[i] = own[(size_t) i]->load();
                if (auto* st = araDoc != nullptr ? araDoc->settingsFor (seq, seq, init, false) : nullptr) { araSettings = st; araTrack = seq; }
            }
            if (araSettings != nullptr && araTrack == seq)
                for (int i = 0; i < 10; ++i) use[(size_t) i] = &araSettings->v[(size_t) i];
            if (araDoc != nullptr)
            {
                araDoc->songTime.store (pos.getTimeInSeconds().orFallback (-1.0));
                araDoc->playing.store (pos.getIsPlaying());
            }
        }
        pointParamsAt (use);
    }
    else
        pointParamsAt (own);
   #endif
    if (buffer.getNumChannels() == 1)
    {
        // mono in and out: the plate runs in stereo as always, and is folded back to one channel
        if (monoBuf.getNumSamples() < n) monoBuf.setSize (2, n, false, false, true);
        AudioBuffer<float> st (monoBuf.getArrayOfWritePointers(), 2, n);
        st.copyFrom (0, 0, buffer, 0, 0, n); st.copyFrom (1, 0, buffer, 0, 0, n);
        processStereo (st);
        buffer.copyFrom (0, 0, st, 0, 0, n); buffer.addFrom (0, 0, st, 1, 0, n); buffer.applyGain (0, 0, n, 0.5f);
        return;
    }
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    processStereo (buffer);
}

void DualityProcessor::processStereo (AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    if (n > dryBuf.getNumSamples()) { dryBuf.setSize (2, n, false, false, true); wetBuf.setSize (2, n, false, false, true); send.resize ((size_t) n); }

    const int persp = roundToInt (pPersp->load());
    const bool wantPlate = persp <= 1;   // Object and Kensho; Subject is the words alone
    gPlate.setTargetValue (wantPlate ? 1.0f : 0.0f);
    sPre.setTargetValue (pPre->load() * 0.001f * (float) sr);
    sWidth.setTargetValue (pWidth->load()); sMix.setTargetValue (pMix->load());
    sOut.setTargetValue (Decibels::decibelsToGain (pOutput->load()));
    sLow.setTargetValue (pLow->load()); sHigh.setTargetValue (pHigh->load());

    for (int c = 0; c < 2; ++c) dryBuf.copyFrom (c, 0, buffer, c, 0, n);
    wetBuf.clear (0, n);
    const float* inL = dryBuf.getReadPointer (0); const float* inR = dryBuf.getReadPointer (1);
    float* wL = wetBuf.getWritePointer (0); float* wR = wetBuf.getWritePointer (1);

    const bool plateActive = wantPlate || gPlate.isSmoothing() || gPlate.getCurrentValue() > 1.0e-4f;
    if (plateActive)
    {
        if (! plateRunning) { plate.reset(); preDelay.reset(); for (auto* f : { &hp[0], &hp[1], &lp[0], &lp[1] }) f->reset(); plateRunning = true; }
        // the send: mono, pre-delayed, then the low and high cut
        sLow.skip (n); sHigh.skip (n);
        updateFilters (sLow.getCurrentValue(), sHigh.getCurrentValue());
        const float eAtk = 1.0f - std::exp (-1.0f / (0.0005f * (float) sr)), eRel = 1.0f - std::exp (-1.0f / (0.030f * (float) sr));
        const float dAtk = 1.0f - std::exp (-1.0f / (0.0008f * (float) sr)), dRel = 1.0f - std::exp (-1.0f / (0.060f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            preDelay.pushSample (0, 0.5f * (inL[i] + inR[i]));
            float x = preDelay.popSample (0, sPre.getNextValue());
            x = hp[1].process (hp[0].process (x));
            // the de-esser: above 4.5 kHz may not be louder than half of what is below it; an S is held down to that
            const float lo = deSplit.process (x), hi = x - lo;
            deHi += (std::abs (hi) > deHi ? eAtk : eRel) * (std::abs (hi) - deHi);
            deLo += (std::abs (lo) > deLo ? eAtk : eRel) * (std::abs (lo) - deLo);
            const float allowed = 0.5f * deLo + 1.0e-6f;
            const float target = deHi > allowed ? juce::jmax (0.25f, allowed / deHi) : 1.0f;
            deGain += (target < deGain ? dAtk : dRel) * (target - deGain);
            x = lo + hi * deGain;
            x = lp[1].process (lp[0].process (x));
            send[(size_t) i] = x;
        }
        const float rt = pDecay->load();
        plate.setParameters (rt, pDamp->load());
        // a longer plate holds more energy: part of that is taken back, so a long plate sits at about the level of a short one
        const float levelTrim = std::pow (2.2f / rt, 0.25f);
        plate.process (send.data(), wL, wR, n);
        for (int i = 0; i < n; ++i)
        {
            wL[i] = voiceLP[0].process (voiceHP[0].process (wL[i]));
            wR[i] = voiceLP[1].process (voiceHP[1].process (wR[i]));
        }

        // Duck: follows the dry voice. While it sings the plate steps back (up to 24 dB); the moment it stops,
        // the plate comes back up within a fifth of a second, so the tail blooms in the gap.
        // The voice is followed in dB, so a falling level is seen at once (a linear follower takes a second to fall 30 dB).
        const float duck = pDuck->load();
        const float atk = 1.0f - std::exp (-1.0f / (0.003f * (float) sr)), rel = 1.0f - std::exp (-1.0f / (0.040f * (float) sr));
        const float gAtk = 1.0f - std::exp (-1.0f / (0.020f * (float) sr)), gRel = 1.0f - std::exp (-1.0f / (0.080f * (float) sr));
        const int holdSamples = (int) (0.05 * sr);
        for (int i = 0; i < n; ++i)
        {
            const float lvlDb = Decibels::gainToDecibels (std::max (std::abs (inL[i]), std::abs (inR[i])), -100.0f);
            if (lvlDb > duckEnv) { duckEnv += atk * (lvlDb - duckEnv); duckHold = holdSamples; }
            else if (duckHold > 0) --duckHold;
            else duckEnv += rel * (lvlDb - duckEnv);
            float target = 1.0f;
            if (duck > 0.001f)
            {
                const float amount = jlimit (0.0f, 1.0f, (duckEnv + 50.0f) / 20.0f);   // from -50 dBFS to -30 dBFS the ducking comes in fully
                target = Decibels::decibelsToGain (-24.0f * duck * amount);
            }
            duckGain += (target < duckGain ? gAtk : gRel) * (target - duckGain);
            const float g = gPlate.getNextValue() * duckGain * 1.6f * levelTrim;   // 1.6: the EMT voicing takes ~4 dB, given back
            wL[i] *= g; wR[i] *= g;
        }
    }
    else
    {
        if (plateRunning) { plate.reset(); plateRunning = false; }
        gPlate.skip (n); sPre.skip (n); sLow.skip (n); sHigh.skip (n);
    }

    float* oL = buffer.getWritePointer (0); float* oR = buffer.getWritePointer (1);
    double sumL = 0, sumR = 0, inSumL = 0, inSumR = 0;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float wl = wL[i], wr = wR[i];
        const float w = sWidth.getNextValue();   // the width of the plate (mid / side)
        const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * w;
        wl = m + s; wr = m - s;
        // Mix: the balance of plate to voice is mix : (1 - mix) as before, but the louder of the two stays at full level,
        // so the voice keeps its level up to the middle (Duality is an insert on the vocal, or an ARA extension on it)
        const float mix = sMix.getNextValue(), og = sOut.getNextValue();
        const float dryG = mix <= 0.5f ? 1.0f : (1.0f - mix) / mix, wetG = mix <= 0.5f ? mix / (1.0f - mix) : 1.0f;
        oL[i] = (inL[i] * dryG + wl * wetG) * og;
        oR[i] = (inR[i] * dryG + wr * wetG) * og;
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
            if (rp->getParameterID() != "mix" && rp->getParameterID() != "output")   // a preset keeps your mix and level
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
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

AudioProcessorEditor* DualityProcessor::createEditor() { return new ArtifactsEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DualityProcessor(); }

//==============================================================================
// The Words window's view of the vocal event (ARA): its names, its waveform (read once, in the background), where the song is.
WordsView DualityProcessor::getWordsView()
{
    WordsView v;
   #if JucePlugin_Enable_ARA
    if (! isBoundToARA()) return v;
    v.ara = true;
    // Cubase shows the editor in an instance of its own, which plays nothing: the event comes from what is selected,
    // else what this instance plays, else the track shown last, else the document's first track
    ARAPlaybackRegion* region = nullptr;
    ARA::PlugIn::DocumentController* araDC = nullptr;
    if (auto* ev = getEditorView())
    {
        araDC = ev->getDocumentController();
        const auto sel = ev->getViewSelection().getEffectivePlaybackRegions<ARAPlaybackRegion>();
        if (! sel.empty()) region = sel.front();
    }
    if (auto* renderer = getPlaybackRenderer<DualityARA::PlaybackRenderer>())
    {
        if (araDC == nullptr) araDC = renderer->getDocumentController();
        if (region == nullptr && ! renderer->getPlaybackRegions().empty()) region = renderer->getPlaybackRegions().front();
    }
    if (auto* r = getEditorRenderer())
        if (araDC == nullptr) araDC = r->getDocumentController();
    auto* doc = araDC != nullptr ? ARADocumentControllerSpecialisation::getSpecialisedDocumentController<DualityARA::DocumentController> (araDC) : nullptr;
    if (doc == nullptr) return v;
    if (region == nullptr)
    {
        ARARegionSequence* pick = nullptr;
        for (auto* seq : doc->getDocument()->getRegionSequences())
            if (! seq->getPlaybackRegions().empty() && (pick == nullptr || seq == editTrack)) pick = seq;
        if (pick != nullptr) region = pick->getPlaybackRegions().front();
    }
    if (region == nullptr) return v;

    // the knobs and the document agree: the track's settings on a new track, then whichever side moved
    {
        auto* seq = region->getRegionSequence();
        float cur[10]; for (int i = 0; i < 10; ++i) cur[i] = own[(size_t) i]->load();
        if (auto* st = doc->settingsFor (seq, seq, cur, true))
        {
            float sh[10]; st->get (sh);
            auto push = [this] (int i, float x)
            {
                if (auto* p = apvts.getParameter (DualityARA::sharedIDs[i])) p->setValueNotifyingHost (p->convertTo0to1 (x));
                mirror[(size_t) i] = x;   // (a value the knob rounds comes back next time, and is then the track's)
            };
            if (seq != editTrack)
            {
                editTrack = seq;
                for (int i = 0; i < 10; ++i) push (i, sh[i]);
                st->get (mirror.data());
            }
            else
                for (int i = 0; i < 10; ++i)
                {
                    if (cur[i] != mirror[(size_t) i]) { st->v[(size_t) i].store (cur[i]); mirror[(size_t) i] = cur[i]; }
                    else if (sh[i] != mirror[(size_t) i]) push (i, sh[i]);
                }
        }
    }
    auto* modification = region->getAudioModification();
    auto* source = modification->getAudioSource();
    auto text = [] (const char* s) { return s != nullptr ? String::fromUTF8 (s) : String(); };
    if (auto* seq = region->getRegionSequence()) v.track = text (seq->getName());
    v.clip = text (region->getEffectiveName());
    if (v.clip.isEmpty()) v.clip = text (source->getName());
    const double startInSource = region->getStartInAudioModificationTime(), length = region->getDurationInAudioModificationTime();
    v.length = length;
    const double t = doc->songTime.load();
    if (t >= 0.0)
    {
        const double rel = t - region->getStartInPlaybackTime();
        v.playhead = rel >= 0.0 && rel <= region->getDurationInPlaybackTime() ? rel : -1.0;
    }
    v.playing = doc->playing.load();

    // the waveform: one peak per 10 ms of the event's own audio, read once per event (and again if it is trimmed)
    const auto key = String::toHexString ((pointer_sized_int) source) + ":" + String (startInSource, 3) + ":" + String (length, 3);
    {
        std::lock_guard<std::mutex> g (peakMutex);
        if (key == peakKey) { v.peaks = peaks; return v; }
        peakKey = key; peaks.clear();
    }
    if (! source->isSampleAccessEnabled()) { std::lock_guard<std::mutex> g (peakMutex); peakKey.clear(); return v; }
    auto reader = std::make_shared<std::unique_ptr<ARAAudioSourceReader>> (std::make_unique<ARAAudioSourceReader> (source));
    const double sr = source->getSampleRate();
    const int64 first = (int64) std::llround (startInSource * sr), total = (int64) std::llround (length * sr);
    peakPool.addJob ([this, reader, key, sr, first, total]
    {
        std::vector<float> out;
        const int per = jmax (1, (int) std::lround (sr * 0.01)), chunk = per * 100;
        AudioBuffer<float> buf (2, chunk);
        for (int64 pos = 0; pos < total; pos += chunk)
        {
            const int count = (int) jmin ((int64) chunk, total - pos);
            if (! (*reader)->read (&buf, 0, count, first + pos, true, true)) break;
            for (int i = 0; i < count; i += per)
            {
                float m = 0.0f;
                for (int j = i; j < jmin (count, i + per); ++j) m = jmax (m, std::abs (buf.getSample (0, j)), std::abs (buf.getSample (1, j)));
                out.push_back (jmin (1.0f, m));
            }
            std::lock_guard<std::mutex> g (peakMutex);
            if (peakKey != key) break;   // another event in the meantime
        }
        {
            std::lock_guard<std::mutex> g (peakMutex);
            if (peakKey == key) peaks = std::move (out);
        }
        MessageManager::callAsync ([reader] { reader->reset(); });   // the reader is let go on the message thread, where it was made
    });
   #endif
    return v;
}
