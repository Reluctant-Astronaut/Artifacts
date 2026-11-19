#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// What the Duality editor shows about the loaded impulse response
struct IRInfo
{
    bool loaded = false;
    juce::String name { "Built-in" }, length { "--" }, rate { "--" }, format { "--" };
    std::vector<float> peaks;   // a coarse envelope for the little waveform, 0..1
};

// The common face of all four Artifacts plugins: the parameter tree, and the numbers the shared
// editor reads (meters, gain reduction, lamps). Each plugin fills in what it has.
class ArtifactsProcessor : public juce::AudioProcessor
{
public:
    ArtifactsProcessor (const BusesProperties& buses, juce::AudioProcessorValueTreeState::ParameterLayout layout, const juce::Identifier& stateName)
        : AudioProcessor (buses), apvts (*this, nullptr, stateName, std::move (layout)) {}

    // Mean square of the last block, per channel
    float getMeterLevel (int ch) const   { return meterOut[ch & 1].load(); }
    float getInputLevel (int ch) const   { return meterIn[ch & 1].load(); }
    // Gain reduction in dB (positive numbers), for the Console's GR meter
    float getGainReduction() const       { return grDb.load(); }
    // How hard the valve is working right now, 0..1 (for the glowing lamp)
    float getValveHeat() const           { return valveHeat.load(); }
    // Highest absolute output sample of the last block (for the clip lamp)
    float getPeak() const                { return peakOut.load(); }
    // Duality: the loaded impulse response, and a way to load another (message thread)
    virtual IRInfo getIRInfo() const { return {}; }
    virtual juce::String loadIR (const juce::File&) { return "No impulse responses here"; }
    // Anemoia: the delay time actually in use, in ms
    virtual float getDelayMs() const { return 0.0f; }
    virtual int irPresetCount() const { return 0; }
    virtual juce::String irPresetName (int) const { return {}; }
    virtual void loadIRPreset (int) {}

    juce::AudioProcessorValueTreeState apvts;

protected:
    std::atomic<float> meterOut[2] { 0.0f, 0.0f }, meterIn[2] { 0.0f, 0.0f };
    std::atomic<float> grDb { 0.0f }, valveHeat { 0.0f }, peakOut { 0.0f };
};
