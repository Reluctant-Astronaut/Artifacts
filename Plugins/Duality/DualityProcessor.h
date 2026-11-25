#pragma once

#include "ArtifactsProcessor.h"
#include "VocalPlate.h"

// Duality: the vocal plate, and the words.
//   Object  - the plate on the whole vocal (the Plate section)
//   Subject - effects only on the words you mark (the Words section: throw, echo, swell, double, harmony, shift)
//   Kensho  - both: the plate on everything, and the words' effects on top
// This build: the plate. The Words section (with ARA and the transcription) follows.
class DualityProcessor : public ArtifactsProcessor
{
public:
    DualityProcessor();
    ~DualityProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void updateFilters (float lowCut, float highCut);

    double sr = 48000.0;
    std::atomic<int> currentProgram { 0 };

    using Interp = juce::dsp::DelayLineInterpolationTypes::Lagrange3rd;
    juce::dsp::DelayLine<float, Interp> preDelay { 48000 };
    VocalPlate plate;

    // the send's filters, as on the Abbey Road plate sends: two-pole high-pass and low-pass
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() { z1 = z2 = 0; }
    } hp[2], lp[2];   // two in a row each: 24 dB per octave, as steep as the classic filters
    float appliedLow = -1.0f, appliedHigh = -1.0f;
    // the plate's own voice, measured from a real EMT 140: lean under 250 Hz, round above 2 kHz (on the wet, both sides)
    Biquad voiceHP[2], voiceLP[2];

    // De-esser on the send, always on: an S goes into the plate softened, so the tail never hisses
    Biquad deSplit; float deHi = 0.0f, deLo = 0.0f, deGain = 1.0f;
    // Duck: the plate steps back while the voice sings, and blooms in the gaps
    float duckEnv = 0.0f, duckGain = 1.0f; int duckHold = 0;

    juce::AudioBuffer<float> dryBuf, wetBuf;
    std::vector<float> send;
    juce::SmoothedValue<float> sPre, sWidth, sMix, sOut, gPlate, sLow, sHigh;
    bool plateRunning = true;

    std::atomic<float> *pDecay, *pDamp, *pPre, *pLow, *pHigh, *pDuck, *pPersp, *pWidth, *pMix, *pOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualityProcessor)
};
