#include "AudioDocument.h"
#include "Theme.h"

//==============================================================================
float AudioDocument::gainAt (double t) const
{
    if (envelope.empty())
        return 1.0f;

    if (t <= envelope.front().time) return envelope.front().gain;
    if (t >= envelope.back().time)  return envelope.back().gain;

    auto it = std::upper_bound (envelope.begin(), envelope.end(), t,
                                [] (double v, const EnvelopePoint& p) { return v < p.time; });
    auto& b = *it;
    auto& a = *(it - 1);
    auto span = b.time - a.time;
    if (span <= 0) return b.gain;
    auto frac = (float) ((t - a.time) / span);
    return a.gain + (b.gain - a.gain) * frac;
}

void AudioDocument::fillGains (juce::int64 startSample, int num, float* dest) const
{
    if (envelope.empty())
    {
        juce::FloatVectorOperations::fill (dest, 1.0f, num);
        return;
    }

    // Envelope changes slowly - evaluate every 32 samples and interpolate.
    constexpr int step = 32;
    float prev = gainAt ((double) startSample / sampleRate);
    for (int i = 0; i < num; i += step)
    {
        auto n = juce::jmin (step, num - i);
        float next = gainAt ((double) (startSample + i + n) / sampleRate);
        for (int k = 0; k < n; ++k)
            dest[i + k] = prev + (next - prev) * (float) k / (float) n;
        prev = next;
    }
}

//==============================================================================
//==============================================================================
// Clips
//==============================================================================
void Clip::rebuildPeaks (juce::int64 fromSample)
{
    const int channels = audio.getNumChannels();
    const auto len = length();
    const auto numBlocks = (size_t) ((len + AudioDocument::peakBlock - 1) / AudioDocument::peakBlock);
    auto firstBlock = (size_t) std::max<juce::int64> (0, fromSample / AudioDocument::peakBlock);

    if ((int) peaks.size() != channels)
    {
        peaks.assign ((size_t) channels, {});
        firstBlock = 0;
    }

    for (int c = 0; c < channels; ++c)
    {
        auto& pc = peaks[(size_t) c];
        pc.resize (numBlocks);
        auto* data = audio.getReadPointer (c);
        for (size_t b = firstBlock; b < numBlocks; ++b)
        {
            auto s = (juce::int64) b * AudioDocument::peakBlock;
            auto n = (int) std::min<juce::int64> (AudioDocument::peakBlock, len - s);
            auto r = juce::FloatVectorOperations::findMinAndMax (data + s, n);
            pc[b] = { r.getStart(), r.getEnd() };
        }
    }
}

static juce::AudioBuffer<float> convertChannels (const juce::AudioBuffer<float>& in, int channels)
{
    juce::AudioBuffer<float> out (channels, in.getNumSamples());
    out.clear();
    if (in.getNumChannels() == 0) return out;
    if (channels == 1 && in.getNumChannels() > 1)
    {
        for (int c = 0; c < in.getNumChannels(); ++c)
            out.addFrom (0, 0, in, c, 0, in.getNumSamples(), 1.0f / (float) in.getNumChannels());
    }
    else
    {
        for (int c = 0; c < channels; ++c)
            out.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), 0, in.getNumSamples());
    }
    return out;
}

static juce::AudioBuffer<float> slice (const juce::AudioBuffer<float>& in, juce::int64 from, juce::int64 num)
{
    juce::AudioBuffer<float> out (in.getNumChannels(), (int) std::max<juce::int64> (0, num));
    for (int c = 0; c < in.getNumChannels(); ++c)
        if (num > 0) out.copyFrom (c, 0, in, c, (int) from, (int) num);
    return out;
}

juce::String AudioDocument::newClipName()
{
    auto base = name.containsChar ('.') ? name.upToLastOccurrenceOf (".", false, false) : name;
    return base + " #" + juce::String (++nextClipNumber);
}

juce::int64 AudioDocument::getLength() const
{
    juce::int64 end = 0;
    for (auto& c : clips) end = std::max (end, c.end());
    return end;
}

int AudioDocument::clipAt (juce::int64 pos) const
{
    for (int i = 0; i < (int) clips.size(); ++i)
        if (pos >= clips[(size_t) i].start && pos < clips[(size_t) i].end())
            return i;
    return -1;
}

bool AudioDocument::hasAudioIn (juce::int64 s, juce::int64 e) const
{
    for (auto& c : clips)
        if (c.start < e && c.end() > s) return true;
    return false;
}

void AudioDocument::renderInto (juce::int64 start, int numSamples, juce::AudioBuffer<float>& dest, int destStart) const
{
    const auto end = start + numSamples;
    for (auto& c : clips)
    {
        const auto s = std::max (start, c.start);
        const auto e = std::min (end, c.end());
        if (e <= s || c.audio.getNumChannels() == 0) continue;
        for (int ch = 0; ch < dest.getNumChannels(); ++ch)
            dest.addFrom (ch, destStart + (int) (s - start), c.audio, juce::jmin (ch, c.audio.getNumChannels() - 1),
                          (int) (s - c.start), (int) (e - s));
    }
}

juce::AudioBuffer<float> AudioDocument::copyRange (juce::int64 start, juce::int64 end) const
{
    start = std::max<juce::int64> (0, start);
    end = std::max (start, end);
    juce::AudioBuffer<float> out (juce::jmax (1, numChannels), (int) (end - start));
    out.clear();
    renderInto (start, (int) (end - start), out, 0);
    return out;
}

void AudioDocument::setBuffer (juce::AudioBuffer<float>&& newBuffer, double newRate)
{
    {
        const juce::ScopedLock sl (lock);
        sampleRate = newRate;
        numChannels = juce::jmax (1, newBuffer.getNumChannels());
        clips.clear();
        if (newBuffer.getNumSamples() > 0)
        {
            Clip c;
            c.start = 0;
            c.audio = std::move (newBuffer);
            c.name = name.upToLastOccurrenceOf (".", false, false).isNotEmpty() ? name.upToLastOccurrenceOf (".", false, false) : name;
            clips.push_back (std::move (c));
        }
    }
    selStart = selEnd = cursor = 0;
    changed (false);
}

void AudioDocument::removeRange (juce::int64 s, juce::int64 e)
{
    if (e <= s) return;
    std::vector<Clip> result;
    for (auto& c : clips)
    {
        if (c.end() <= s || c.start >= e)
        {
            result.push_back (c);
            continue;
        }
        if (c.start < s)
        {
            Clip left;
            left.start = c.start;
            left.audio = slice (c.audio, 0, s - c.start);
            left.name = c.name;
            result.push_back (std::move (left));
        }
        if (c.end() > e)
        {
            Clip right;
            right.start = e;
            right.audio = slice (c.audio, e - c.start, c.end() - e);
            right.name = c.name;
            result.push_back (std::move (right));
        }
    }
    const juce::ScopedLock sl (lock);
    clips = std::move (result);
}

void AudioDocument::shiftAnnotations (double a, double b, double delta)
{
    const juce::ScopedLock sl (lock);
    std::vector<EnvelopePoint> env;
    for (auto p : envelope)
    {
        if (p.time > a && p.time < b) continue;      // inside removed region
        if (p.time >= b) p.time += delta;
        env.push_back (p);
    }
    envelope = std::move (env);
    for (auto& m : markers)
    {
        if (m.time >= b)        m.time += delta;
        else if (m.time > a)    m.time = a;
    }
}

void AudioDocument::rippleRemove (juce::int64 s, juce::int64 e)
{
    if (e <= s) return;
    removeRange (s, e);
    {
        const juce::ScopedLock sl (lock);
        for (auto& c : clips)
            if (c.start >= e) c.start -= (e - s);
    }
    shiftAnnotations (toSeconds (s), toSeconds (e), -toSeconds (e - s));
}

void AudioDocument::splitAt (juce::int64 pos)
{
    int idx = clipAt (pos);
    if (idx < 0) return;
    auto& c = clips[(size_t) idx];
    if (pos <= c.start || pos >= c.end()) return;
    Clip right;
    right.start = pos;
    right.audio = slice (c.audio, pos - c.start, c.end() - pos);
    right.name = c.name;
    auto leftAudio = slice (c.audio, 0, pos - c.start);
    const juce::ScopedLock sl (lock);
    clips[(size_t) idx].audio = std::move (leftAudio);
    clips.push_back (std::move (right));
}

void AudioDocument::insertClip (juce::int64 pos, juce::AudioBuffer<float> data, const juce::String& clipName)
{
    pos = std::max<juce::int64> (0, pos);
    if (data.getNumSamples() == 0) return;
    if (data.getNumChannels() != numChannels)
        data = convertChannels (data, numChannels);
    const juce::int64 len = data.getNumSamples();

    juce::int64 shift = 0;
    int idx = clipAt (pos);
    if (idx >= 0 && pos > clips[(size_t) idx].start)
    {
        splitAt (pos);
        shift = len;
    }
    else
    {
        // In a gap: only push following clips if the new audio does not fit
        juce::int64 nextStart = -1;
        for (auto& c : clips)
            if (c.start >= pos && (nextStart < 0 || c.start < nextStart)) nextStart = c.start;
        if (nextStart >= 0)
            shift = std::max<juce::int64> (0, len - (nextStart - pos));
    }

    {
        const juce::ScopedLock sl (lock);
        if (shift > 0)
            for (auto& c : clips)
                if (c.start >= pos) c.start += shift;
        Clip n;
        n.start = pos;
        n.audio = std::move (data);
        n.name = clipName.isNotEmpty() ? clipName : newClipName();
        clips.push_back (std::move (n));
    }
    if (shift > 0)
        shiftAnnotations (toSeconds (pos), toSeconds (pos), toSeconds (shift));
}

void AudioDocument::joinRange (juce::int64 s, juce::int64 e)
{
    std::vector<int> idx;
    for (int i = 0; i < (int) clips.size(); ++i)
        if (clips[(size_t) i].start < e && clips[(size_t) i].end() > s)
            idx.push_back (i);
    if (idx.size() < 2) return;

    juce::int64 a = std::numeric_limits<juce::int64>::max(), b = 0;
    juce::String firstName;
    for (auto i : idx)
    {
        if (clips[(size_t) i].start < a) { a = clips[(size_t) i].start; firstName = clips[(size_t) i].name; }
        b = std::max (b, clips[(size_t) i].end());
    }
    Clip joined;
    joined.start = a;
    joined.name = firstName;
    joined.audio.setSize (numChannels, (int) (b - a));
    joined.audio.clear();
    for (auto i : idx)
    {
        auto& c = clips[(size_t) i];
        for (int ch = 0; ch < numChannels; ++ch)
            joined.audio.addFrom (ch, (int) (c.start - a), c.audio, juce::jmin (ch, c.audio.getNumChannels() - 1), 0, (int) c.length());
    }

    const juce::ScopedLock sl (lock);
    for (int k = (int) idx.size(); --k >= 0;)
        clips.erase (clips.begin() + idx[(size_t) k]);
    clips.push_back (std::move (joined));
}

void AudioDocument::processRange (juce::int64 s, juce::int64 e,
                                  const std::function<void (juce::AudioBuffer<float>&, juce::int64)>& fn)
{
    for (int i = 0; i < (int) clips.size(); ++i)
    {
        auto& c = clips[(size_t) i];
        const auto ps = std::max (s, c.start), pe = std::min (e, c.end());
        if (pe <= ps) continue;
        auto portion = slice (c.audio, ps - c.start, pe - ps);
        fn (portion, ps);
        if (portion.getNumSamples() != (int) (pe - ps)) continue;   // safety: effects must keep length
        const juce::ScopedLock sl (lock);
        for (int ch = 0; ch < c.audio.getNumChannels(); ++ch)
            c.audio.copyFrom (ch, (int) (ps - c.start), portion, juce::jmin (ch, portion.getNumChannels() - 1), 0, (int) (pe - ps));
    }
}

juce::int64 AudioDocument::findFreePosition (juce::int64 desired, juce::int64 len, int exclude) const
{
    desired = std::max<juce::int64> (0, desired);
    auto isFree = [&] (juce::int64 p)
    {
        if (p < 0) return false;
        for (int i = 0; i < (int) clips.size(); ++i)
            if (i != exclude && p < clips[(size_t) i].end() && p + len > clips[(size_t) i].start)
                return false;
        return true;
    };
    if (isFree (desired)) return desired;

    juce::int64 best = -1;
    for (int i = 0; i < (int) clips.size(); ++i)
    {
        if (i == exclude) continue;
        for (auto cand : { clips[(size_t) i].end(), clips[(size_t) i].start - len })
            if (isFree (cand) && (best < 0 || std::abs (cand - desired) < std::abs (best - desired)))
                best = cand;
    }
    return best;
}

void AudioDocument::moveClip (int index, juce::int64 newStart)
{
    if (! juce::isPositiveAndBelow (index, (int) clips.size())) return;
    auto pos = findFreePosition (newStart, clips[(size_t) index].length(), index);
    if (pos < 0) return;
    const juce::ScopedLock sl (lock);
    clips[(size_t) index].start = pos;
}

Clip AudioDocument::takeClip (int index)
{
    const juce::ScopedLock sl (lock);
    Clip c = std::move (clips[(size_t) index]);
    clips.erase (clips.begin() + index);
    return c;
}

int AudioDocument::addClip (Clip c)
{
    if (c.audio.getNumChannels() != numChannels)
        c.audio = convertChannels (c.audio, numChannels);
    auto pos = findFreePosition (c.start, c.length(), -1);
    c.start = pos >= 0 ? pos : getLength();
    c.rebuildPeaks();
    const juce::ScopedLock sl (lock);
    clips.push_back (std::move (c));
    return (int) clips.size() - 1;
}

void AudioDocument::setChannelCount (int channels)
{
    channels = juce::jlimit (1, 8, channels);
    const juce::ScopedLock sl (lock);
    numChannels = channels;
    for (auto& c : clips)
        if (c.audio.getNumChannels() != channels)
            c.audio = convertChannels (c.audio, channels);
}

//==============================================================================
void AudioDocument::sortEnvelope()
{
    std::stable_sort (envelope.begin(), envelope.end(),
                      [] (const EnvelopePoint& x, const EnvelopePoint& y) { return x.time < y.time; });
}

int AudioDocument::addEnvelopePoint (double time, float gain)
{
    gain = juce::jlimit (0.0f, maxEnvelopeGain, gain);
    const juce::ScopedLock sl (lock);

    // When adding the very first point, anchor the ends so the rest stays at unity.
    if (envelope.empty())
    {
        envelope.push_back ({ 0.0, 1.0f });
        envelope.push_back ({ getLengthSeconds(), 1.0f });
    }

    envelope.push_back ({ time, gain });
    sortEnvelope();
    for (int i = 0; i < (int) envelope.size(); ++i)
        if (envelope[(size_t) i].time == time && envelope[(size_t) i].gain == gain)
            return i;
    return -1;
}

void AudioDocument::moveEnvelopePoint (int index, double time, float gain)
{
    const juce::ScopedLock sl (lock);
    if (! juce::isPositiveAndBelow (index, (int) envelope.size()))
        return;

    double lo = index > 0 ? envelope[(size_t) index - 1].time : 0.0;
    double hi = index < (int) envelope.size() - 1 ? envelope[(size_t) index + 1].time : getLengthSeconds();
    envelope[(size_t) index].time = juce::jlimit (lo, juce::jmax (lo, hi), time);
    envelope[(size_t) index].gain = juce::jlimit (0.0f, maxEnvelopeGain, gain);
}

void AudioDocument::removeEnvelopePoint (int index)
{
    const juce::ScopedLock sl (lock);
    if (juce::isPositiveAndBelow (index, (int) envelope.size()))
        envelope.erase (envelope.begin() + index);

    // If only flat unity points remain, clear the envelope completely.
    bool allUnity = std::all_of (envelope.begin(), envelope.end(),
                                 [] (const EnvelopePoint& p) { return std::abs (p.gain - 1.0f) < 1e-4f; });
    if (allUnity)
        envelope.clear();
}

void AudioDocument::resetEnvelope()
{
    const juce::ScopedLock sl (lock);
    envelope.clear();
}

int AudioDocument::addMarker (double time, const juce::String& markerName)
{
    Marker m;
    m.time = juce::jlimit (0.0, getLengthSeconds(), time);
    m.colour = Theme::markerColour (nextMarkerColour++);
    m.name = markerName.isNotEmpty() ? markerName
                                     : juce::String ("Marker ") + juce::String ((int) markers.size() + 1);
    markers.push_back (m);
    sortMarkers();
    for (int i = 0; i < (int) markers.size(); ++i)
        if (markers[(size_t) i].time == m.time && markers[(size_t) i].name == m.name)
            return i;
    return -1;
}

void AudioDocument::sortMarkers()
{
    std::stable_sort (markers.begin(), markers.end(),
                      [] (const Marker& x, const Marker& y) { return x.time < y.time; });
}

//==============================================================================
AudioDocument::State AudioDocument::captureState (const juce::String& desc) const
{
    State s;
    s.clips = clips;
    s.sampleRate = sampleRate;
    s.numChannels = numChannels;
    s.envelope = envelope;
    s.markers = markers;
    s.selStart = selStart;
    s.selEnd = selEnd;
    s.cursor = cursor;
    s.description = desc;
    return s;
}

void AudioDocument::restoreState (const State& s)
{
    {
        const juce::ScopedLock sl (lock);
        clips = s.clips;
        sampleRate = s.sampleRate;
        numChannels = s.numChannels;
        envelope = s.envelope;
    }
    markers = s.markers;
    selStart = s.selStart;
    selEnd = s.selEnd;
    cursor = s.cursor;
}

void AudioDocument::pushUndo (const juce::String& description)
{
    undoStack.push_back (captureState (description));
    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool AudioDocument::undo()
{
    if (undoStack.empty())
        return false;

    auto desc = undoStack.back().description;
    redoStack.push_back (captureState (desc));
    restoreState (undoStack.back());
    undoStack.pop_back();
    changed();
    return true;
}

bool AudioDocument::redo()
{
    if (redoStack.empty())
        return false;

    auto desc = redoStack.back().description;
    undoStack.push_back (captureState (desc));
    restoreState (redoStack.back());
    redoStack.pop_back();
    changed();
    return true;
}

void AudioDocument::clearHistory()
{
    undoStack.clear();
    redoStack.clear();
}

//==============================================================================
void AudioDocument::rebuildPeaks()
{
    for (auto& c : clips)
        c.rebuildPeaks();
}

void AudioDocument::changed (bool markModified)
{
    rebuildPeaks();
    if (markModified)
        modified = true;
    sendChangeMessage();
}

void AudioDocument::annotationsChanged()
{
    modified = true;
    sendChangeMessage();
}
