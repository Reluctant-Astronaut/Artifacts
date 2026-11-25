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

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<DualityARA::DocumentController>();
}

#endif
