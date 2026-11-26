#pragma once

#include "ArtifactsProcessor.h"
#include "VocalPlate.h"
#include "DualityShared.h"
#include "DualityARA.h"
#include <mutex>
#include <array>

// Duality: the vocal plate, and the words.
//   Object  - the plate on the whole vocal (the Plate section)
//   Subject - effects only on the words you mark (the Words section: throw, echo, swell, double, harmony, shift)
//   Kensho  - both: the plate on everything, and the words' effects on top
// This build: the plate. The Words section (with ARA and the transcription) follows.
class DualityProcessor : public ArtifactsProcessor
#if JucePlugin_Enable_ARA
                       , public juce::AudioProcessorARAExtension   // Duality as an extension on the vocal event (ARA)
#endif
{
public:
    DualityProcessor();
    ~DualityProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override
    {
       #if JucePlugin_Enable_ARA
        releaseResourcesForARA();
       #endif
    }
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
    WordsView getWordsView() override;
    void wordsAction (const WordsAction&) override;

private:
    void updateFilters (float lowCut, float highCut);
    void processStereo (juce::AudioBuffer<float>&);

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

    juce::AudioBuffer<float> dryBuf, wetBuf, monoBuf;
    std::vector<float> send;
    juce::SmoothedValue<float> sPre, sWidth, sMix, sOut, gPlate, sLow, sHigh;
    bool plateRunning = true;

    // ARA: where the song is (seconds) and whether it plays, from the audio thread; the event's waveform, read in the background
    std::atomic<double> araTime { -1.0 };
    std::atomic<bool> araPlaying { false };
    std::mutex peakMutex;
    std::vector<float> peaks;
    juce::String peakKey;   // which event (and which part of its audio) the peaks are of
    juce::ThreadPool peakPool { 1 };   // (last: it is gone, its job finished, before the things the job uses)

    std::atomic<float> *pDecay, *pDamp, *pPre, *pLow, *pHigh, *pDuck, *pPersp, *pWidth, *pMix, *pOutput;
    // the track's settings (DualityParams): own = this instance's knobs; cur = what plays (the track's shared settings while
    // Duality plays an ARA track: Cubase's Lower Zone editor is another instance)
    using ParamSet = std::array<std::atomic<float>*, DualityParams::numShared>;
    ParamSet own {}, cur {};
    void pointParamsAt (const ParamSet&);
    float P (DualityParams::Index i) const { return cur[(size_t) i]->load(); }

    // the words' effects (ARA): the marked words of the event that plays, Throw (a plate of its own) and Echo
    void wordSends (const juce::AudioBuffer<float>& dry, int numSamples);   // (before the plate) which words feed which effect
    void wordReturns (juce::AudioBuffer<float>& out, int numSamples);        // (after the plate) the effects, added in
    juce::AudioPlayHead::PositionInfo blockPos;
    bool wordsBound = false;
    int testGates = -1;   // the offline tests: every sample is a marked word with these effects (DUALITY_TEST_GATES)
    // each effect has a few voices: a marked word plays in one with its own settings, so tails with different settings overlap
    struct Echo
    {
        std::vector<float> l, r; int w = 0;
        float time = 0;             // samples, glides to the target (a tape echo bends as its time moves)
        float lp[2] {}, hp[2] {}, wow = 0;
        void prepare (int maxSamples) { l.assign ((size_t) maxSamples, 0.0f); r.assign ((size_t) maxSamples, 0.0f); w = 0; time = 0; lp[0] = lp[1] = hp[0] = hp[1] = 0; }
        float read (const std::vector<float>& b, float d) const;
    };
    struct ThrowVoice
    {
        VocalPlate plate;
        std::vector<float> send, pre, outL, outR;
        int preW = 0;
        float env = 0, hpz = 0, lp[2] {}, hp[2] {};
        std::array<float, 8> set {};   // the settings of the word that fed it last
        bool used = false;              // (a voice no word has fed yet stays silent and costs nothing)
    };
    struct EchoVoice
    {
        Echo echo;
        std::vector<float> send;
        float env = 0;
        std::array<float, 8> set {};
        bool used = false;
    };
    std::array<ThrowVoice, DualityParams::voices> throwVoices;
    std::array<EchoVoice, DualityParams::voices> echoVoices;
    float fxRet = 0;
    double fxSampleRate = 48000.0;
   #if JucePlugin_Enable_ARA
    DualityARA::DocumentController* araDoc = nullptr;   // the document of this instance (audio thread's copy)
    const void* araTrack = nullptr;                     // the track this instance plays
    DualityARA::Settings* araSettings = nullptr;
    // the event the editor shows: what is selected in the host, else what this instance plays, else the track shown last
    bool pickRegion (juce::ARAPlaybackRegion*& region, DualityARA::DocumentController*& doc);
    // the editor's side: which track it shows, and the settings as last agreed between its knobs and the document
    const void* editTrack = nullptr;
    std::array<float, DualityParams::numShared> mirror {};
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualityProcessor)
};
