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
#include <deque>
#include "DualityShared.h"
#if DUALITY_WORDS
 #include "DualityWords.h"
#endif

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
    using DualityParams::numShared;
    using DualityParams::sharedIDs;
    struct Settings
    {
        std::array<std::atomic<float>, numShared> v {};
        // (a value that is not a number is left as it is: a project saved before that setting existed)
        void set (const float* x) { for (int i = 0; i < numShared; ++i) if (! std::isnan (x[i])) v[(size_t) i].store (x[i]); }
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
        std::atomic<double> bpm { 120.0 };

        ~DocumentController() override;

       #if DUALITY_WORDS
        // the words of each recording (audio source), heard once in the background and kept with the project.
        // All of these are for the message thread.
        enum class Hearing { none, waiting, fetching, reading, hearing, done, failed };
        struct Words
        {
            std::vector<DualityWords::Word> words;   // in the recording's own time
            Hearing state = Hearing::none;
            float progress = 0.0f;
            juce::String error, lyrics;               // lyrics: pasted while it was still listening, placed when it is done
        };
        Words wordsOf (juce::ARAAudioSource*);
        void hear (juce::ARAAudioSource*, bool again, bool lyricsOnly = false);   // if not heard yet (or again; or only to place pasted lyrics)
        void selectWords (juce::ARAAudioSource*, int first, int last, double from, double to);   // -1: none (only those in [from, to])
        void editWord (juce::ARAAudioSource*, int index, const juce::String& text);   // "" removes it; "two words" splits it
        void removeWords (juce::ARAAudioSource*, int first, int last);
        void placeLyrics (juce::ARAAudioSource*, const juce::String& lyrics);
        // on the selected words in [from, to]: an effect on (with these settings) or off; or new settings for those that have it
        void setEffect (juce::ARAAudioSource*, int effect, bool on, double from, double to, const std::vector<float>& settings);
        void setWordSettings (juce::ARAAudioSource*, int effect, double from, double to, const std::vector<float>& settings);
        void retimeWord (juce::ARAAudioSource*, int index, double t0, double t1);   // timing by hand (the recording's time)
       #endif

        // the marked words of a recording, for the audio thread: [t0, t1) in the recording's time, and the effects on them
        struct Mark
        {
            double t0, t1; int effects;
            int throwVoice = 0, echoVoice = 0;      // the voice its tail rings in (words in turn, so neighbours overlap)
            std::array<float, 8> echo {}, thr {};   // its own settings
        };
        using Marks = std::vector<Mark>;
        // (audio thread: never waits; an empty pointer if the words are being changed this very moment)
        std::shared_ptr<const Marks> marksOf (const std::string& sourceID);

    protected:
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
       #if DUALITY_WORDS
        std::mutex wordsMutex;
        std::map<std::string, Words> wordsBySource;   // by the audio source's persistent ID
        struct Job { std::string id; std::shared_ptr<std::unique_ptr<juce::AudioFormatReader>> reader; double sampleRate; juce::int64 length; int channels; bool lyricsOnly = false; };
        std::deque<Job> jobs;
        std::atomic<bool> stopping { false };
        std::unique_ptr<juce::Thread> listener;
        void listen (juce::Thread&);   // the listener thread: one recording at a time
        void setHearing (const std::string& id, Hearing h, float progress, const juce::String& error = {});
        DualityWords::Ears ears;
        void publishMarks (const std::string& id);   // (wordsMutex held)
       #endif
        juce::SpinLock marksLock;
        std::map<std::string, std::shared_ptr<const Marks>> marks;
        std::vector<std::shared_ptr<const Marks>> retired;   // the audio thread may still hold these: never freed there
    };
}

#endif
