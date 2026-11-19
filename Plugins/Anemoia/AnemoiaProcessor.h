#pragma once

#include "ArtifactsProcessor.h"

// Anemoia: nostalgia for a time you never knew.
//   Echos    - a stereo delay (free or tempo-synced) with a warm, dark feedback path
//   Memories - a granular cloud that remembers what was played, and lets it bloom back
// The two are in series: dry -> Echos -> Memories. Either can be switched out.
class AnemoiaProcessor : public ArtifactsProcessor
{
public:
    AnemoiaProcessor();
    ~AnemoiaProcessor() override = default;

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
    float getDelayMs() const override { return shownDelayMs.load(); }

private:
    static constexpr int maxGrains = 40;
    static constexpr double memSeconds = 8.0;
    static constexpr double maxDelaySeconds = 3.2;

    struct Grain
    {
        bool active = false;
        double pos = 0.0;      // absolute read position in samples
        float rate = 1.0f, slope = 0.0f;
        int age = 0, len = 1;
        float gL = 1.0f, gR = 1.0f, alpha = 0.6f, amp = 1.0f;
        bool useLP = false;
    };

    struct Biquad2   // two cascaded one-poles: a soft, non-resonant low-pass
    {
        float a = 0.5f, y1 = 0.0f, y2 = 0.0f;
        void set (double fc, double sr) { a = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (fc, sr * 0.45) / sr)); }
        float process (float x) { y1 += a * (x - y1); y2 += a * (y1 - y2); return y2; }
        void reset() { y1 = y2 = 0.0f; }
    };

    float readH (const std::vector<float>& b, double p) const;
    void spawnGrain (int pattern, float texture, float blur, float shimmer, float nostalgia, int pitchSt, float width);
    float windowAt (float x, float alpha) const;

    double sr = 48000.0;
    std::atomic<int> currentProgram { 0 };
    std::atomic<float> shownDelayMs { 375.0f };

    // Echos
    std::vector<float> dBuf[2];
    int dN = 0, dW = 0;
    float dLP[2][2] {}, dHPx[2] {}, dHPy[2] {};
    float wowPh[2] { 0.0f, 0.37f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sTime;
    juce::SmoothedValue<float> sFb, sTone, sWidth, sMix, sOut, gEcho, gMem;
    float toneLP[2][2] {};

    // Memories
    std::vector<float> mBuf[2], mBufLP[2];
    int mN = 0, mW = 0, frozenW = 0;
    bool frozen = false;
    Grain grains[maxGrains];
    int spawnCount = 0;
    float glideBack = 0.3f, glideDir = 1.0f, swellPh = 0.0f, wobblePh = 0.0f;
    Biquad2 recLP[2], warmLP[2], regenLP[2];
    float regenPrev[2] {};
    float dcX[2] {}, dcY[2] {};
    juce::Random rng { 20240611 };
    juce::SmoothedValue<float> sTexture, sBlur, sBloom, sShimmer, sNost;
    struct Diff
    {
        std::vector<float> b; int w = 0; float g = 0.5f;
        void setSize (int n) { b.assign ((size_t) std::max (2, n), 0.0f); w = 0; }
        float process (float x) { const float d = b[(size_t) w]; const float v = x + g * d; b[(size_t) w] = v; if (++w >= (int) b.size()) w = 0; return d - g * v; }
    } diff[2][2];

    std::atomic<float> *pTime, *pDiv, *pFeel, *pSync, *pFb, *pTone, *pWidth, *pEchoOn, *pMemOn,
                       *pTexture, *pBlur, *pBloom, *pShimmer, *pPattern, *pNost, *pPitch, *pMix, *pOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnemoiaProcessor)
};
