#pragma once

#include "ArtifactsProcessor.h"
#include "AnalogDSP.h"

// Equanimity: a program equalizer in the manner of the Pultec EQP-1A.
//   Low  - frequency (20 / 30 / 60 / 100 Hz), boost, atten
//   High - frequency (3 ... 16 kHz), bandwidth, boost
//   High atten - 5 / 10 / 20 kHz, atten
//   Midrange (MEQ-5 style) - low peak, dip and high peak, 200 Hz to 5 kHz
//   Auto gain - holds the overall level steady while the EQ is changed
//   EQ IN - the EQ in or out; the tube stage and transformers stay in circuit
//   Path - L/R or M/S for the whole EQ. L/R: one bank (left / right). M/S: a Mid bank and a Side bank, both heard;
//                M / = / S chooses which one the panel edits (= edits both together). The bank not in use keeps its settings.
//                Solo lets you hear only the Mid or only the Side.
class EquanimityProcessor : public ArtifactsProcessor
{
public:
    EquanimityProcessor();
    ~EquanimityProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Equanimity"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    // One biquad (transposed direct form II, double precision)
    struct Biquad
    {
        // power gain at normalised angular frequency w
        double mag2 (double w) const
        {
            const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
            const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
            return (nr * nr + ni * ni) / std::max (1.0e-30, dr * dr + di * di);
        }
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        { b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0; }
        double tick (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void clear() { z1 = z2 = 0; }
    };
    // The EQ of one channel: the low network (boost shelf + atten shelf, one coupled pair), the high peak, the high atten shelf
    struct EqChannel { Biquad loBoost, loAtten, mLo, mDip, mHi, hiPeak, hiAtten; double autoGain = 1.0; };
    // The dial settings of one bank, smoothed
    struct Settings
    {
        float loBoost = 0, loAtten = 0, hiBoost = 0, bw = 5, hiAtten = 0;   // dB / 0..10
        float mLoPk = 0, mDip = 0, mHiPk = 0;                               // the midrange peaks and dip, 0..10
        int loFreq = 2, hiFreq = 4, attSel = 1, mLoFreq = 2, mDipFreq = 4, mHiFreq = 2;
        // the selectors' frequencies in Hz: low, high peak, high atten, mid low, mid dip, mid high. A switch jumps, but the filter
        // glides to its new frequency over a few milliseconds, so turning a selector on a playing track doesn't click
        double hz[6] { 60, 8000, 10000, 500, 1000, 3000 };
    };
    void designEq (EqChannel& e, const Settings& s) const;
    static double hiFreqHz (int i);
    static double loFreqHz (int i);
    static double attSelHz (int i);
    static double mLoHz (int i);
    static double mDipHz (int i);
    static double mHiHz (int i);
    double loudnessComp (const EqChannel&) const;

    // The valve stage (per channel, at 4x)
    struct Valve { double dc = 0, dc2 = 0, lowband = 0, env = 0, top = 0; };

    double sr = 48000.0;
    int maxBlock = 512;
    std::atomic<float> *pOutput = nullptr, *pEqIn = nullptr, *pMode = nullptr, *pEdit = nullptr, *pSolo = nullptr;
    std::atomic<float>* pBank[3][14] {};
    std::atomic<float>* pAuto = nullptr;
    std::atomic<float>* pNoise = nullptr;
    uint32_t noiseState[2] { 0x9e3779b9u, 0x7f4a7c15u };
    juce::SmoothedValue<float> sOut;
    EqChannel eq[2];
    Settings cur[2], tgt[2];
    Valve valve[2];
    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };   // minimum phase, as analog: no pre-ringing
    float heat = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EquanimityProcessor)
};
