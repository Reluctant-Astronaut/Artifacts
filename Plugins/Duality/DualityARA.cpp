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
    bool ok = out.writeInt (3);   // format version (1: the settings only; 2: and the words; 3: and their effects)
    ok = ok && out.writeInt ((int) rows.size());
    for (auto& r : rows)
    {
        ok = ok && out.writeString (String::fromUTF8 (r.first.c_str()));
        ok = ok && out.writeInt (numShared);
        for (float f : r.second) ok = ok && out.writeFloat (f);
    }
   #if DUALITY_WORDS
    // the words of each recording
    std::vector<std::pair<std::string, std::vector<DualityWords::Word>>> heard;
    {
        std::lock_guard<std::mutex> w (wordsMutex);
        for (auto* src : filter->getAudioSourcesToStore<ARAAudioSource>())
        {
            auto it = wordsBySource.find (src->getPersistentID());
            if (it != wordsBySource.end() && it->second.state == Hearing::done) heard.push_back ({ src->getPersistentID(), it->second.words });
        }
    }
    ok = ok && out.writeInt ((int) heard.size());
    for (auto& h : heard)
    {
        ok = ok && out.writeString (String::fromUTF8 (h.first.c_str())) && out.writeInt ((int) h.second.size());
        for (auto& wd : h.second)
            ok = ok && out.writeString (wd.text) && out.writeDouble (wd.t0) && out.writeDouble (wd.t1)
                    && out.writeFloat (wd.confidence) && out.writeBool (wd.selected) && out.writeInt (wd.effects);
    }
   #else
    ok = ok && out.writeInt (0);
   #endif
    return ok;
}

bool DocumentController::doRestoreObjectsFromStream (ARAInputStream& in, const ARARestoreObjectsFilter* filter) noexcept
{
    if (in.getNumBytesRemaining() <= 0) return true;   // a project from before Duality kept anything
    const int version = in.readInt();
    if (version < 1 || version > 3) return true;
    const int count = in.readInt();
    std::unique_lock<std::mutex> l (settingsMutex);
    for (int i = 0; i < count && ! in.failed(); ++i)
    {
        const auto id = in.readString();
        const int n = in.readInt();
        std::array<float, numShared> x {};
        x.fill (std::numeric_limits<float>::quiet_NaN());   // (settings newer than the project keep their defaults)
        for (int k = 0; k < n; ++k) { const float f = in.readFloat(); if (k < numShared) x[(size_t) k] = f; }
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
    l.unlock();
    if (version >= 2 && ! in.failed())
    {
        const int sources = in.readInt();
        for (int i = 0; i < sources && ! in.failed(); ++i)
        {
            const auto id = in.readString();
            const int n = in.readInt();
           #if DUALITY_WORDS
            std::vector<DualityWords::Word> ws;
           #endif
            for (int k = 0; k < n && ! in.failed(); ++k)
            {
                const auto text = in.readString(); const double t0 = in.readDouble(), t1 = in.readDouble();
                const float conf = in.readFloat(); const bool sel = in.readBool();
                const int fx = version >= 3 ? in.readInt() : 0;
               #if DUALITY_WORDS
                DualityWords::Word wd; wd.text = text; wd.t0 = t0; wd.t1 = t1; wd.confidence = conf; wd.selected = sel; wd.effects = fx;
                ws.push_back (wd);
               #else
                ignoreUnused (text, t0, t1, conf, sel, fx);
               #endif
            }
           #if DUALITY_WORDS
            if (auto* src = filter->getAudioSourceToRestoreStateWithID<ARAAudioSource> (id.toRawUTF8()))
            {
                std::lock_guard<std::mutex> w (wordsMutex);
                auto& e = wordsBySource[src->getPersistentID()];
                e.words = std::move (ws); e.state = Hearing::done; e.progress = 1.0f; e.error.clear();
                publishMarks (src->getPersistentID());
            }
           #endif
        }
    }
    return ! in.failed();
}

DocumentController::~DocumentController()
{
   #if DUALITY_WORDS
    stopping.store (true);
    if (listener != nullptr) listener->stopThread (10000);
   #endif
}

#if DUALITY_WORDS
//==============================================================================
// the words: heard on a thread of their own, one recording at a time
namespace
{
    struct ListenerThread final : Thread
    {
        ListenerThread (std::function<void (Thread&)> f) : Thread ("Duality words"), fn (std::move (f)) {}
        void run() override { fn (*this); }
        std::function<void (Thread&)> fn;
    };
}

DocumentController::Words DocumentController::wordsOf (ARAAudioSource* src)
{
    std::lock_guard<std::mutex> w (wordsMutex);
    auto it = wordsBySource.find (src->getPersistentID());
    return it != wordsBySource.end() ? it->second : Words {};
}

void DocumentController::setHearing (const std::string& id, Hearing h, float progress, const String& error)
{
    std::lock_guard<std::mutex> w (wordsMutex);
    auto& e = wordsBySource[id];
    e.state = h; e.progress = progress; e.error = error;
}

void DocumentController::hear (ARAAudioSource* src, bool again)
{
    const auto id = src->getPersistentID();
    {
        std::lock_guard<std::mutex> w (wordsMutex);
        auto& e = wordsBySource[id];
        if (! again && e.state != Hearing::none) return;
        if (e.state == Hearing::waiting || e.state == Hearing::fetching || e.state == Hearing::reading || e.state == Hearing::hearing) return;
        if (! src->isSampleAccessEnabled()) return;   // (the host lets Duality read it a moment later)
        e.state = Hearing::waiting; e.progress = 0.0f; e.error.clear();
        auto reader = std::make_shared<std::unique_ptr<AudioFormatReader>> (std::make_unique<ARAAudioSourceReader> (src));
        jobs.push_back ({ id, reader, src->getSampleRate(), src->getSampleCount(), src->getChannelCount() });
    }
    if (listener == nullptr)
    {
        listener = std::make_unique<ListenerThread> ([this] (Thread& t) { listen (t); });
        listener->startThread (Thread::Priority::low);
    }
    listener->notify();
}

void DocumentController::listen (Thread& thread)
{
    while (! thread.threadShouldExit())
    {
        Job job;
        {
            std::lock_guard<std::mutex> w (wordsMutex);
            if (! jobs.empty()) { job = jobs.front(); jobs.pop_front(); }
        }
        if (job.reader == nullptr) { thread.wait (500); continue; }
        auto letGo = [r = job.reader] { MessageManager::callAsync ([r] { r->reset(); }); };   // the reader goes on the message thread

        // 1. the recording, read whole
        setHearing (job.id, Hearing::reading, 0.0f);
        // (one channel: the sides summed as it is read, so a long take stays small in memory)
        const int total = (int) jmin<int64> (job.length, (int64) 16000 * 60 * 60 * 3);
        AudioBuffer<float> audio (1, jmax (1, total));
        audio.clear();
        bool ok = true;
        const int chunk = 1 << 16, chans = jmax (1, job.channels);
        AudioBuffer<float> part (chans, chunk);
        for (int pos = 0; pos < total && ok && ! stopping.load(); pos += chunk)
        {
            const int n = jmin (chunk, total - pos);
            ok = (*job.reader)->read (&part, 0, n, pos, true, true);
            for (int c = 0; c < chans; ++c) audio.addFrom (0, pos, part, c, 0, n, 1.0f / (float) chans);
            setHearing (job.id, Hearing::reading, (float) pos / (float) total);
        }
        letGo();
        if (stopping.load()) return;
        if (! ok) { setHearing (job.id, Hearing::failed, 0.0f, "COULDN'T READ THE RECORDING"); continue; }
        const auto a16 = DualityWords::toListeningRate (audio, job.sampleRate);
        audio.setSize (1, 1);

        // 2. the model, fetched the first time
        String err;
        if (! DualityWords::modelFile().existsAsFile())
        {
            setHearing (job.id, Hearing::fetching, 0.0f);
            if (! DualityWords::fetchModel ([&] (float p) { setHearing (job.id, Hearing::fetching, p); }, stopping, err))
            { if (stopping.load()) return; setHearing (job.id, Hearing::failed, 0.0f, err); continue; }
        }

        // 3. the words
        setHearing (job.id, Hearing::hearing, 0.0f);
        auto words = ears.hear (a16, [&] (float p) { setHearing (job.id, Hearing::hearing, p); }, stopping, err);
        if (stopping.load()) return;
        if (err.isNotEmpty() && words.empty()) { setHearing (job.id, Hearing::failed, 0.0f, err); continue; }
        std::lock_guard<std::mutex> w (wordsMutex);
        auto& e = wordsBySource[job.id];
        if (e.lyrics.isNotEmpty())   // lyrics pasted while it listened: those words, on what it heard
        {
            words = DualityWords::placeLyrics (words, e.lyrics, 0.0, (double) a16.size() / 16000.0);
            e.lyrics.clear();
        }
        e.words = std::move (words); e.state = Hearing::done; e.progress = 1.0f; e.error.clear();
        publishMarks (job.id);
    }
}

void DocumentController::selectWords (ARAAudioSource* src, int first, int last, double from, double to)
{
    std::lock_guard<std::mutex> w (wordsMutex);
    auto& ws = wordsBySource[src->getPersistentID()].words;
    if (first > last) std::swap (first, last);
    for (int i = 0; i < (int) ws.size(); ++i)
    {
        auto& wd = ws[(size_t) i];
        if (wd.t1 < from || wd.t0 > to) continue;   // only this event's words change
        wd.selected = first >= 0 && i >= first && i <= last;
    }
}

void DocumentController::editWord (ARAAudioSource* src, int index, const String& text)
{
    std::lock_guard<std::mutex> w (wordsMutex);
    auto& ws = wordsBySource[src->getPersistentID()].words;
    if (index < 0 || index >= (int) ws.size()) return;
    StringArray parts; parts.addTokens (text, " ", "");
    std::vector<DualityWords::Word> now;
    for (auto& p : parts) { auto c = DualityWords::cleanWord (p); if (c.isNotEmpty()) { DualityWords::Word x; x.text = c; now.push_back (x); } }
    const auto old = ws[(size_t) index];
    ws.erase (ws.begin() + index);
    if (now.empty()) { publishMarks (src->getPersistentID()); return; }
    // one word becomes several: its time shared by their lengths
    int chars = 0; for (auto& x : now) chars += x.text.length() + 1;
    double t = old.t0;
    for (auto& x : now)
    {
        const double d = (old.t1 - old.t0) * (x.text.length() + 1) / (double) chars;
        x.t0 = t; x.t1 = t + d * (now.size() > 1 ? 0.94 : 1.0); x.confidence = 1.0f; x.selected = old.selected; x.effects = old.effects; t += d;
    }
    ws.insert (ws.begin() + index, now.begin(), now.end());
    publishMarks (src->getPersistentID());
}

void DocumentController::removeWords (ARAAudioSource* src, int first, int last)
{
    std::lock_guard<std::mutex> w (wordsMutex);
    auto& ws = wordsBySource[src->getPersistentID()].words;
    if (first > last) std::swap (first, last);
    first = jmax (0, first); last = jmin ((int) ws.size() - 1, last);
    if (first <= last) ws.erase (ws.begin() + first, ws.begin() + last + 1);
    publishMarks (src->getPersistentID());
}

void DocumentController::placeLyrics (ARAAudioSource* src, const String& lyrics)
{
    {
        std::lock_guard<std::mutex> w (wordsMutex);
        auto& e = wordsBySource[src->getPersistentID()];
        if (e.state == Hearing::done)
        {
            e.words = DualityWords::placeLyrics (e.words, lyrics, 0.0, src->getDuration());
            publishMarks (src->getPersistentID());
            return;
        }
        e.lyrics = lyrics;   // still listening (or not yet): placed when it is done
    }
    hear (src, wordsOf (src).state == Hearing::failed);
}
void DocumentController::setEffect (ARAAudioSource* src, int effect, bool on, double from, double to)
{
    std::lock_guard<std::mutex> w (wordsMutex);
    auto& ws = wordsBySource[src->getPersistentID()].words;
    for (auto& wd : ws)
        if (wd.selected && wd.t1 > from && wd.t0 < to)
            wd.effects = on ? (wd.effects | (1 << effect)) : (wd.effects & ~(1 << effect));
    publishMarks (src->getPersistentID());
}

void DocumentController::publishMarks (const std::string& id)
{
    auto m = std::make_shared<Marks>();
    auto it = wordsBySource.find (id);
    if (it != wordsBySource.end())
        for (auto& wd : it->second.words)
            if (wd.effects != 0) m->push_back ({ wd.t0, wd.t1, wd.effects });
    std::shared_ptr<const Marks> c = std::move (m);
    const SpinLock::ScopedLockType l (marksLock);
    auto& slot = marks[id];
    if (slot != nullptr) retired.push_back (slot);
    slot = std::move (c);
}
#endif

std::shared_ptr<const DocumentController::Marks> DocumentController::marksOf (const std::string& id)
{
    const SpinLock::ScopedTryLockType l (marksLock);
    if (! l.isLocked()) return {};
    auto it = marks.find (id);
    return it != marks.end() ? it->second : nullptr;
}
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<DualityARA::DocumentController>();
}

#endif
