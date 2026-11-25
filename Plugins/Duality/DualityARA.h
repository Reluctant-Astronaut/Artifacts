#pragma once

// Duality as an ARA extension (Cubase: select the vocal event, Audio > Extensions > Duality).
// Bound to ARA, Duality plays the vocal itself from the event's audio, so it can see the whole performance:
// the Words window shows its waveform, and (later) its transcribed words with their effects.
// This part is built only into the plugins (JucePlugin_Enable_ARA), never into the offline test rig.

#include <juce_audio_processors/juce_audio_processors.h>

#if JucePlugin_Enable_ARA

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace DualityARA
{
    // one reading thread for every instance: it reads ahead of the playhead so playback never waits on the host
    class SharedTimeSliceThread final : public juce::TimeSliceThread
    {
    public:
        SharedTimeSliceThread() : juce::TimeSliceThread ("Duality ARA sample reading") { startThread (Priority::high); }
    };

    // the host edits the document between willBeginEditing and didEndEditing: rendering steps aside meanwhile
    struct ProcessingLock
    {
        virtual ~ProcessingLock() = default;
        virtual juce::ScopedTryReadLock getProcessingLock() = 0;
    };

    // plays the vocal: each playback region's audio, at its place on the timeline
    class PlaybackRenderer final : public juce::ARAPlaybackRenderer
    {
    public:
        PlaybackRenderer (ARA::PlugIn::DocumentController* dc, ProcessingLock& lockIn) : juce::ARAPlaybackRenderer (dc), lock (lockIn) {}

        void prepareToPlay (double sampleRate, int maximumSamplesPerBlock, int numChannels,
                            juce::AudioProcessor::ProcessingPrecision, AlwaysNonRealtime alwaysNonRealtime) override;
        void releaseResources() override;
        bool processBlock (juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime,
                           const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;
        using juce::ARAPlaybackRenderer::processBlock;

    private:
        struct Reader
        {
            std::unique_ptr<juce::AudioFormatReader> reader;
            juce::BufferingAudioReader* buffering = nullptr;   // when the reader reads ahead (real-time playback)
        };
        ProcessingLock& lock;
        juce::SharedResourcePointer<SharedTimeSliceThread> thread;
        std::map<juce::ARAAudioSource*, Reader> readers;
        bool buffered = true;
        int numChannels = 2, maxBlock = 512;
        double sampleRate = 48000.0;
        juce::AudioBuffer<float> temp;
    };

    class DocumentController final : public juce::ARADocumentControllerSpecialisation, private ProcessingLock
    {
    public:
        using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    protected:
        void willBeginEditing (juce::ARADocument*) override { editLock.enterWrite(); }
        void didEndEditing (juce::ARADocument*) override    { editLock.exitWrite(); }
        juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override { return new PlaybackRenderer (getDocumentController(), *this); }
        // nothing of Duality's own is stored in the project yet (the words and their effects will be)
        bool doRestoreObjectsFromStream (juce::ARAInputStream&, const juce::ARARestoreObjectsFilter*) noexcept override { return true; }
        bool doStoreObjectsToStream (juce::ARAOutputStream&, const juce::ARAStoreObjectsFilter*) noexcept override { return true; }

    private:
        juce::ScopedTryReadLock getProcessingLock() override { return juce::ScopedTryReadLock { editLock }; }
        juce::ReadWriteLock editLock;
    };
}

#endif
