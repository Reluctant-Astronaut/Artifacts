#pragma once

#include "ArtifactsProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "PlateReverb.h"
#include "HallReverb.h"

// Duality: two ways of making a space, and the point where they become one.
//   Object  - an algorithmic reverb (Room, Hall or Plate): a space built from rules
//   Subject - an impulse response: a space that was really there
//   Kensho  - both at once, fused into a single space
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
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    IRInfo getIRInfo() const override;
    juce::String loadIR (const juce::File&) override;      // message thread
    void loadBuiltInIR (int index);                          // message thread
    int irPresetCount() const override { return numBuiltInIRs(); }
    juce::String irPresetName (int i) const override { return builtInIRName (i); }
    void loadIRPreset (int i) override { loadBuiltInIR (i); }
    static int numBuiltInIRs();
    static juce::String builtInIRName (int index);

private:
    void pushIRToEngine();
    static void shapeIR (juce::AudioBuffer<float>& ir, double rate, bool mono, float decay, float damping);

    double sr = 48000.0;
    int maxBlock = 512;
    std::atomic<int> currentProgram { 0 };

    using Interp = juce::dsp::DelayLineInterpolationTypes::Lagrange3rd;
    juce::dsp::DelayLine<float, Interp> preRev { 48000 }, preIR { 48000 };
    PlateReverb plate;
    HallReverb hall;
    juce::dsp::Convolution conv;
    juce::dsp::IIR::Filter<float> fuse[2], air[2];

    // Kensho: a short shared diffusion that fuses the two reverbs into one space
    struct Allpass
    {
        std::vector<float> buf; int w = 0; float g = 0.45f;
        void setSize (int n) { buf.assign ((size_t) std::max (2, n), 0.0f); w = 0; }
        float process (float x)
        {
            const float d = buf[(size_t) w];
            const float v = x + g * d;
            buf[(size_t) w] = v;
            if (++w >= (int) buf.size()) w = 0;
            return d - g * v;
        }
    } blend[2][2], roomDiff[2][3];

    juce::AudioBuffer<float> dryBuf, algBuf, irBuf;
    juce::SmoothedValue<float> sSize, sDamp, sPre, sLevel, sIDecay, sIDamp, sIPre, sILevel, sWidth, sMix, sOut, gAlg, gIR;
    bool algRunning = true, irRunning = true;
    float appliedIRDecay = -1.0f, appliedIRDamp = -1.0f;

    // IR source, kept as loaded so Decay and Damping can reshape it (message thread, guarded by irLock)
    mutable juce::CriticalSection irLock;
    juce::AudioBuffer<float> irSource;
    double irRate = 48000.0;
    bool irMono = false;
    IRInfo irInfo;
    std::atomic<bool> irDirty { false };
    std::atomic<float> pendingDecay { 1.0f }, pendingDamp { 0.0f };

    std::atomic<float> *pSize, *pDamp, *pPre, *pType, *pLevel, *pPersp,
                       *pIDecay, *pIDamp, *pIPre, *pILevel, *pWidth, *pMix, *pOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualityProcessor)
};
