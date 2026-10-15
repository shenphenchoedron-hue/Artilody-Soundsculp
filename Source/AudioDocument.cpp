#include "AudioDocument.h"

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
juce::AudioBuffer<float> AudioDocument::copyRange (juce::int64 start, juce::int64 end) const
{
    start = juce::jlimit<juce::int64> (0, getLength(), start);
    end   = juce::jlimit<juce::int64> (start, getLength(), end);
    juce::AudioBuffer<float> out (getNumChannels(), (int) (end - start));
    for (int c = 0; c < getNumChannels(); ++c)
        out.copyFrom (c, 0, buffer, c, (int) start, (int) (end - start));
    return out;
}

void AudioDocument::replaceRange (juce::int64 start, juce::int64 end, const juce::AudioBuffer<float>& newData,
                                  const juce::String& undoName)
{
    start = juce::jlimit<juce::int64> (0, getLength(), start);
    end   = juce::jlimit<juce::int64> (start, getLength(), end);

    if (undoName.isNotEmpty())
        pushUndo (undoName);

    const auto oldLen = getLength();
    const auto insLen = (juce::int64) newData.getNumSamples();
    const auto newLen = oldLen - (end - start) + insLen;
    const int channels = juce::jmax (1, getNumChannels());

    juce::AudioBuffer<float> result (channels, (int) newLen);
    for (int c = 0; c < channels; ++c)
    {
        if (start > 0)
            result.copyFrom (c, 0, buffer, c, 0, (int) start);

        if (insLen > 0)
        {
            int srcCh = juce::jmin (c, newData.getNumChannels() - 1);
            if (srcCh >= 0)
                result.copyFrom (c, (int) start, newData, srcCh, 0, (int) insLen);
            else
                result.clear (c, (int) start, (int) insLen);
        }

        if (oldLen > end)
            result.copyFrom (c, (int) (start + insLen), buffer, c, (int) end, (int) (oldLen - end));
    }

    // Shift annotations
    const double a = toSeconds (start), b = toSeconds (end);
    const double delta = toSeconds (insLen) - (b - a);

    {
        const juce::ScopedLock sl (lock);
        buffer = std::move (result);

        if (std::abs (delta) > 1e-9)
        {
            std::vector<EnvelopePoint> env;
            for (auto p : envelope)
            {
                if (p.time > a && p.time < b)
                    continue;                       // inside removed region
                if (p.time >= b) p.time += delta;
                env.push_back (p);
            }
            envelope = std::move (env);
        }
    }

    if (std::abs (delta) > 1e-9)
    {
        for (auto& m : markers)
        {
            if (m.time >= b)        m.time += delta;
            else if (m.time > a)    m.time = juce::jmin (m.time, a + toSeconds (insLen));
        }
    }

    // keep selection sane: select the new material
    selStart = start;
    selEnd = start + insLen;
    cursor = start;
    changed();
}

void AudioDocument::setBuffer (juce::AudioBuffer<float>&& newBuffer, double newRate)
{
    {
        const juce::ScopedLock sl (lock);
        buffer = std::move (newBuffer);
        sampleRate = newRate;
    }
    selStart = selEnd = cursor = 0;
    changed (false);
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
    m.colour = nextMarkerColour++;
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
    s.buffer.makeCopyOf (buffer);
    s.sampleRate = sampleRate;
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
        buffer.makeCopyOf (s.buffer);
        sampleRate = s.sampleRate;
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
void AudioDocument::rebuildPeaks (juce::int64 fromSample)
{
    const int channels = getNumChannels();
    const auto len = getLength();
    const auto numBlocks = (size_t) ((len + peakBlock - 1) / peakBlock);
    auto firstBlock = (size_t) std::max<juce::int64> (0, fromSample / peakBlock);

    if ((int) peaks.size() != channels)
    {
        peaks.assign ((size_t) channels, {});
        firstBlock = 0;
    }

    for (int c = 0; c < channels; ++c)
    {
        auto& pc = peaks[(size_t) c];
        pc.resize (numBlocks);
        auto* data = buffer.getReadPointer (c);
        for (size_t b = firstBlock; b < numBlocks; ++b)
        {
            auto s = (juce::int64) b * peakBlock;
            auto n = (int) std::min<juce::int64> (peakBlock, len - s);
            auto r = juce::FloatVectorOperations::findMinAndMax (data + s, n);
            pc[b] = { r.getStart(), r.getEnd() };
        }
    }
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
