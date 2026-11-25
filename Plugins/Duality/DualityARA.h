#pragma once

// Duality as an ARA extension (Cubase: select the vocal event, Audio > Extensions > Duality).
// Bound to ARA, Duality plays the vocal itself from the event's audio, so it can see the whole performance:
// the Words window shows its waveform, and (later) its transcribed words with their effects.
// This part is built only into the plugins (JucePlugin_Enable_ARA), never into the offline test rig.

#include <juce_audio_processors/juce_audio_processors.h>

#if JucePlugin_Enable_ARA

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <array>
#include <mutex>

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

    // Cubase runs Duality as several instances on one ARA document: one plays each track's events, another shows the editor
    // in the Lower Zone. The plate's settings therefore live here, in the document, one set per track (region sequence),
    // shared by every instance: the editor's knobs write them, the instance that plays the track reads them.
    // They are saved with the project, one copy per vocal event (audio modification) of the track.
    static constexpr int numShared = 10;
    inline const char* const sharedIDs[numShared] { "pdecay", "pdamp", "ppre", "plowcut", "phighcut", "pduck", "perspective", "width", "mix", "output" };
    struct Settings
    {
        std::array<std::atomic<float>, numShared> v {};
        void set (const float* x) { for (int i = 0; i < numShared; ++i) v[(size_t) i].store (x[i]); }
        void get (float* x) const { for (int i = 0; i < numShared; ++i) x[i] = v[(size_t) i].load(); }
    };

    class DocumentController final : public juce::ARADocumentControllerSpecialisation, private ProcessingLock
    {
    public:
        using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

        // the settings of a track; made from `initial` the first time (or from the project, if it saved some). Never freed
        // while the document lives, so the audio thread may keep the pointer. Returns nullptr if the lock is busy (audio thread).
        Settings* settingsFor (const void* track, const juce::ARARegionSequence* seq, const float* initial, bool mayWait);
        // where the song is: written by whichever instance plays, read by the editor's
        std::atomic<double> songTime { -1.0 };
        std::atomic<bool> playing { false };

    protected:
        void willBeginEditing (juce::ARADocument*) override { editLock.enterWrite(); }
        void didEndEditing (juce::ARADocument*) override    { editLock.exitWrite(); }
        juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override { return new PlaybackRenderer (getDocumentController(), *this); }
        // nothing of Duality's own is stored in the project yet (the words and their effects will be)
        bool doRestoreObjectsFromStream (juce::ARAInputStream&, const juce::ARARestoreObjectsFilter*) noexcept override;
        bool doStoreObjectsToStream (juce::ARAOutputStream&, const juce::ARAStoreObjectsFilter*) noexcept override;

    private:
        juce::ScopedTryReadLock getProcessingLock() override { return juce::ScopedTryReadLock { editLock }; }
        juce::ReadWriteLock editLock;
        std::mutex settingsMutex;
        std::map<const void*, std::unique_ptr<Settings>> byTrack;
        std::map<const void*, std::array<float, numShared>> restored;   // from the project: by audio modification
    };
}

#endif
