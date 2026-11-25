#include "DualityARA.h"

#if JucePlugin_Enable_ARA

using namespace juce;

namespace DualityARA
{
void PlaybackRenderer::prepareToPlay (double sr, int maximumSamplesPerBlock, int channels,
                                      AudioProcessor::ProcessingPrecision, AlwaysNonRealtime alwaysNonRealtime)
{
    numChannels = channels; sampleRate = sr; maxBlock = maximumSamplesPerBlock;
    temp.setSize (jmax (1, numChannels), jmax (1, maxBlock));
    buffered = alwaysNonRealtime == AlwaysNonRealtime::no;
    for (auto* region : getPlaybackRegions())
    {
        auto* source = region->getAudioModification()->getAudioSource();
        if (readers.count (source) != 0) continue;
        auto plain = std::make_unique<ARAAudioSourceReader> (source);
        Reader r;
        if (buffered)
        {
            auto* b = new BufferingAudioReader (plain.release(), *thread, jmax (4 * maxBlock, roundToInt (2.0 * sampleRate)));
            r.buffering = b;
            r.reader.reset (b);
        }
        else
            r.reader = std::move (plain);
        readers.emplace (source, std::move (r));
    }
}

void PlaybackRenderer::releaseResources()
{
    readers.clear();
}

bool PlaybackRenderer::processBlock (AudioBuffer<float>& buffer, AudioProcessor::Realtime realtime,
                                     const AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const auto locked = lock.getProcessingLock();
    if (! locked.isLocked()) { buffer.clear(); return true; }   // the host is editing the document: silence for this block

    const int n = buffer.getNumSamples();
    bool ok = true, rendered = false;
    if (positionInfo.getIsPlaying() && n <= temp.getNumSamples())
    {
        const auto block = Range<int64>::withStartAndLength (positionInfo.getTimeInSamples().orFallback (0), n);
        for (auto* region : getPlaybackRegions())
        {
            const auto onTimeline = region->getSampleRange (sampleRate, ARAPlaybackRegion::IncludeHeadAndTail::no);
            auto range = block.getIntersectionWith (onTimeline);
            if (range.isEmpty()) continue;
            const Range<int64> inSource { region->getStartInAudioModificationSamples(), region->getEndInAudioModificationSamples() };
            const auto offset = inSource.getStart() - onTimeline.getStart();
            range = range.getIntersectionWith (inSource.movedToStartAt (onTimeline.getStart()));
            if (range.isEmpty()) continue;

            auto* source = region->getAudioModification()->getAudioSource();
            const auto it = readers.find (source);
            // the source must be at the session's rate (Cubase keeps it so); a mono vocal plays on both sides
            if (it == readers.end() || std::abs (source->getSampleRate() - sampleRate) > 0.5) { ok = false; continue; }
            if (it->second.buffering != nullptr) it->second.buffering->setReadTimeout (realtime == AudioProcessor::Realtime::no ? 100 : 0);

            const int count = (int) range.getLength(), start = (int) (range.getStart() - block.getStart());
            auto& dest = rendered ? temp : buffer;
            if (! it->second.reader->read (&dest, start, count, range.getStart() + offset, true, true)) { ok = false; continue; }
            if (rendered)
                for (int c = 0; c < buffer.getNumChannels() && c < temp.getNumChannels(); ++c)
                    buffer.addFrom (c, start, temp, c, start, count);
            else
            {
                if (start > 0) buffer.clear (0, start);
                if (start + count < n) buffer.clear (start + count, n - start - count);
                rendered = true;
            }
        }
    }
    if (! rendered) buffer.clear();
    return ok;
}
}

namespace DualityARA
{
Settings* DocumentController::settingsFor (const void* track, const ARARegionSequence* seq, const float* initial, bool mayWait)
{
    std::unique_lock<std::mutex> l (settingsMutex, std::defer_lock);
    if (mayWait) l.lock(); else if (! l.try_lock()) return nullptr;
    auto it = byTrack.find (track);
    if (it != byTrack.end()) return it->second.get();
    if (initial == nullptr) return nullptr;
    auto st = std::make_unique<Settings>();
    st->set (initial);
    if (seq != nullptr)   // the project had settings for one of this track's events: those, then
        for (auto* region : seq->getPlaybackRegions())
        {
            const auto r = restored.find (region->getAudioModification());
            if (r != restored.end()) { st->set (r->second.data()); break; }
        }
    auto* raw = st.get();
    byTrack.emplace (track, std::move (st));
    return raw;
}

// the project: for each vocal event, the settings of its track
bool DocumentController::doStoreObjectsToStream (ARAOutputStream& out, const ARAStoreObjectsFilter* filter) noexcept
{
    const auto& mods = filter->getAudioModificationsToStore<ARAAudioModification>();
    std::lock_guard<std::mutex> l (settingsMutex);
    std::vector<std::pair<std::string, std::array<float, numShared>>> rows;
    for (auto* m : mods)
        for (auto* region : m->getPlaybackRegions())
        {
            auto it = byTrack.find (region->getRegionSequence());
            if (it == byTrack.end()) continue;
            std::array<float, numShared> x {}; it->second->get (x.data());
            rows.push_back ({ m->getPersistentID(), x });
            break;
        }
    bool ok = out.writeInt (1);   // format version
    ok = ok && out.writeInt ((int) rows.size());
    for (auto& r : rows)
    {
        ok = ok && out.writeString (String::fromUTF8 (r.first.c_str()));
        ok = ok && out.writeInt (numShared);
        for (float f : r.second) ok = ok && out.writeFloat (f);
    }
    return ok;
}

bool DocumentController::doRestoreObjectsFromStream (ARAInputStream& in, const ARARestoreObjectsFilter* filter) noexcept
{
    if (in.getNumBytesRemaining() <= 0) return true;   // a project from before Duality kept anything
    const int version = in.readInt();
    if (version != 1) return true;
    const int count = in.readInt();
    std::lock_guard<std::mutex> l (settingsMutex);
    for (int i = 0; i < count && ! in.failed(); ++i)
    {
        const auto id = in.readString();
        const int n = in.readInt();
        std::array<float, numShared> x {};
        for (int k = 0; k < n; ++k) { const float f = in.readFloat(); if (k < numShared) x[(size_t) k] = f; }
        if (n < numShared) continue;
        if (auto* m = filter->getAudioModificationToRestoreStateWithID<ARAAudioModification> (id.toRawUTF8()))
        {
            restored[m] = x;
            for (auto* region : m->getPlaybackRegions())   // a track already in use takes them at once
            {
                auto it = byTrack.find (region->getRegionSequence());
                if (it != byTrack.end()) it->second->set (x.data());
            }
        }
    }
    return ! in.failed();
}
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<DualityARA::DocumentController>();
}

#endif
