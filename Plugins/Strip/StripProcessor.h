#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "AnalogDSP.h"
#include "ArtifactsProcessor.h"
#include <deque>

// Kintsugi: an imaginary analog chain for one track (or the mix bus):
// a console channel (line amp, transformer iron, hum, bus glue),
// a valve stage (ECC82 / ECC83 / EL84), and a reel-to-reel machine
// (tape saturation, head bump, wow, flutter, tape age).
class StripProcessor : public ArtifactsProcessor
{
public:
    StripProcessor();
    ~StripProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();


private:
    void processChunk (float* L, float* R, int n);
    void updateFilters();
    void applyTolerances();

    double sr = 44100.0;
    int maxBlock = 512;
    std::atomic<int> currentProgram { 0 };   // read by the host from any thread

    static constexpr int osOrder = 2;   // 4x oversampling around everything that saturates
    juce::dsp::Oversampling<float> oversampling { 2, osOrder, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false };
    juce::AudioBuffer<float> chunkBuf, dryBuf;

    // ---- latency & tape transport ----
    using Interp = juce::dsp::DelayLineInterpolationTypes::Lagrange3rd;
    juce::dsp::DelayLine<float, Interp> transport { 4096 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 8192 };
    float tapeBaseDelay = 0.0f;   // base-rate samples; chosen so total latency is a whole number
    static constexpr int chunkSize = 64;
    float modArr[chunkSize + 1] {}, azArr[chunkSize + 1] {};
    float lastMod = 0.0f, lastAz = 0.0f;
    int totalLatency = 0;

    // ---- per-channel tolerances (each console channel number is slightly different) ----
    struct Tolerance
    {
        float gain[2] { 1.0f, 1.0f };
        float driveScale = 1.0f, asym = 0.0f, ironFreq = 1.0f, hfFreq = 1.0f, azimuth = 0.0f, humPhase = 0.0f;
    } tol;
    int appliedChannel = -1;

    // ---- console ----
    analog::OnePoleLP ironLP[2], transLP[2];
    double humPhase = 0.0;
    analog::Wander humWobble;

    // ---- valve ----
    analog::OnePoleHP valveDC[2];
    analog::OnePoleLP millerLP[2];
    analog::Follower sagEnv[2];
    juce::dsp::StateVariableTPTFilter<float> valveTone;
    float heatAcc = 0.0f;

    // ---- tape ----
    juce::dsp::IIR::Filter<float> preEmph[2], deEmph[2], headBump[2], lowEnd[2];
    juce::dsp::StateVariableTPTFilter<float> tapeHF;
    analog::OnePoleLP scrapeLP;
    int appliedSpeed = -1;
    double wowPh1 = 0.0, wowPh2 = 0.0, flutPh1 = 0.0, flutPh2 = 0.0;
    analog::Wander wowWander, azWander, wowRate;
    analog::Noise scrapeNoise;

    // ---- drift ----
    analog::Wander dGain, dTone, dSpeed, dSat, dBias, dBalL, dBalR;

    // ---- glue (bus compressor) ----
    analog::OnePoleHP glueSC[2];
    float glueEnvDb = 0.0f, glueRms = 0.0f;

    // ---- output ----
    analog::OnePoleHP outDC[2];

    // ---- smoothed controls ----
    juce::SmoothedValue<float> sConsoleDrive, sHum, sGlue, sDrift,
                               sValveDrive, sBias, sSag, sValveMix, sTone,
                               sTapeSat, sWow, sFlutter, sWear, sMix, sOut,
                               sConsoleOn, sValveOn, sTapeOn;

    std::atomic<float> *pConsoleDrive, *pXfmr, *pHum, *pChannel, *pGlue, *pDrift,
                       *pValveDrive, *pBias, *pSag, *pValveType, *pTone, *pValveMix,
                       *pSpeed, *pTapeSat, *pWow, *pFlutter, *pWear,
                       *pMix, *pOutput, *pConsoleOn, *pValveOn, *pTapeOn;

    std::deque<std::atomic<float>> fixedValues;   // controls this plugin does not have: they sit at a fixed value

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripProcessor)
};
