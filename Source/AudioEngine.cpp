#include "AudioEngine.h"
#include "Theme.h"

AudioEngine::AudioEngine()
{
    for (auto& l : levels) l = 0.0f;
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise (const juce::XmlElement* savedState)
{
    auto err = deviceManager.initialise (2, 2, savedState, true);
    if (err.isNotEmpty())
        err = deviceManager.initialise (0, 2, nullptr, true);
    deviceManager.addAudioCallback (this);
    startTimerHz (30);
    return err;
}

void AudioEngine::setDocuments (const std::vector<AudioDocument*>& all, AudioDocument* cur)
{
    const juce::ScopedLock sl (docsLock);
    documents = all;
    current = cur;
}

//==============================================================================
void AudioEngine::play()
{
    previewing = false;
    reachedEnd = false;
    playing = true;
    if (onTransportChanged) onTransportChanged();
}

void AudioEngine::stop()
{
    playing = false;
    previewing = false;
    if (onTransportChanged) onTransportChanged();
}

void AudioEngine::setLoopRange (double s, double e)
{
    loopStart = juce::jmax (0.0, s);
    loopEnd = juce::jmax (s, e);
}

void AudioEngine::playPreview (juce::AudioBuffer<float>&& buffer, double sampleRate)
{
    playing = false;
    {
        const juce::ScopedLock sl (docsLock);
        previewBuffer = std::move (buffer);
        previewRate = sampleRate;
        previewPos = 0.0;
    }
    previewing = true;
    if (onTransportChanged) onTransportChanged();
}

double AudioEngine::getEndTime() const
{
    double end = 0.0;
    if (mixAll)
    {
        for (auto* d : documents)
            end = juce::jmax (end, d->getLengthSeconds());
    }
    else if (current != nullptr)
    {
        end = current->getLengthSeconds();
    }
    return end;
}

int AudioEngine::getNumInputChannels() const
{
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        return dev->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

//==============================================================================
void AudioEngine::setEQ (const EQSettings& settings, bool enabled)
{
    auto coeffs = DSP::makeCoefficients (settings, deviceRate.load());
    {
        const juce::SpinLock::ScopedLockType sl (eqLock);
        eqSettings = settings;
        pendingCoeffs = coeffs;
        eqDirty = true;
    }
    eqEnabled = enabled;
}

void AudioEngine::rebuildEQ()
{
    EQSettings s;
    {
        const juce::SpinLock::ScopedLockType sl (eqLock);
        s = eqSettings;
    }
    setEQ (s, eqEnabled.load());
}

//==============================================================================
bool AudioEngine::startRecording (AudioDocument* target, juce::String& error)
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr)
    {
        error = T ("No audio device is open.", "Ingen lydenhed er åben.");
        return false;
    }

    auto ins = dev->getActiveInputChannels().countNumberOfSetBits();
    if (ins <= 0)
    {
        error = T ("No input channels are enabled. Choose an input device in Settings > Audio settings.",
                   "Ingen indgange er aktiveret. Vælg en indgangsenhed under Indstillinger > Lydindstillinger.");
        return false;
    }

    stop();
    recordChannels = juce::jlimit (1, 2, ins);
    recordTarget = target;
    recordedSamples = 0;
    recordData.assign ((size_t) recordChannels, {});
    for (auto& v : recordData) v.reserve ((size_t) (deviceRate.load() * 60.0));
    recordPointers.assign ((size_t) recordChannels, nullptr);
    recordFifo.reset();

    {
        const juce::ScopedLock sl (target->lock);
        target->sampleRate = deviceRate.load();
        target->buffer.setSize (recordChannels, 0);
    }

    recording = true;
    if (onTransportChanged) onTransportChanged();
    return true;
}

void AudioEngine::stopRecording()
{
    if (! recording.load())
        return;

    recording = false;
    timerCallback();   // drain remaining audio

    if (recordTarget != nullptr)
    {
        juce::AudioBuffer<float> owned (recordChannels, (int) recordedSamples);
        for (int c = 0; c < recordChannels; ++c)
            if (recordedSamples > 0)
                owned.copyFrom (c, 0, recordData[(size_t) c].data(), (int) recordedSamples);

        {
            const juce::ScopedLock sl (recordTarget->lock);
            recordTarget->buffer = std::move (owned);
        }
        recordTarget->changed();
    }

    recordTarget = nullptr;
    recordData.clear();
    if (onTransportChanged) onTransportChanged();
}

void AudioEngine::timerCallback()
{
    if (reachedEnd.exchange (false))
        if (onTransportChanged) onTransportChanged();

    if (recordTarget == nullptr)
        return;

    int ready = recordFifo.getNumReady();
    if (ready <= 0)
        return;

    const auto oldLen = recordedSamples;
    const auto newLen = oldLen + ready;
    bool reallocated = false;

    for (auto& v : recordData)
    {
        if ((size_t) newLen > v.capacity())
        {
            v.reserve (juce::jmax ((size_t) newLen, v.capacity() * 2));
            reallocated = true;
        }
        v.resize ((size_t) newLen);
    }

    int start1, size1, start2, size2;
    recordFifo.prepareToRead (ready, start1, size1, start2, size2);
    for (int c = 0; c < recordChannels; ++c)
    {
        auto* dst = recordData[(size_t) c].data() + oldLen;
        if (size1 > 0) std::copy (recordFifoBuffer.getReadPointer (c, start1), recordFifoBuffer.getReadPointer (c, start1) + size1, dst);
        if (size2 > 0) std::copy (recordFifoBuffer.getReadPointer (c, start2), recordFifoBuffer.getReadPointer (c, start2) + size2, dst + size1);
    }
    recordFifo.finishedRead (size1 + size2);
    recordedSamples = newLen;
    juce::ignoreUnused (reallocated);

    for (int c = 0; c < recordChannels; ++c)
        recordPointers[(size_t) c] = recordData[(size_t) c].data();

    {
        const juce::ScopedLock sl (recordTarget->lock);
        recordTarget->buffer.setDataToReferTo (recordPointers.data(), recordChannels, (int) recordedSamples);
    }
    recordTarget->rebuildPeaks (oldLen);
    recordTarget->cursor = recordedSamples;
    recordTarget->sendChangeMessage();
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceRate = device->getCurrentSampleRate();
    auto block = device->getCurrentBufferSizeSamples();
    mixBuffer.setSize (2, juce::jmax (block, 4096));
    chunkBuffer.setSize (2, juce::jmax (block, 4096));
    for (auto& ch : eqFilters) for (auto& f : ch) f.reset();
    rebuildEQ();
}

void AudioEngine::audioDeviceStopped()
{
    playing = false;
    previewing = false;
}

void AudioEngine::renderDocument (AudioDocument& doc, double startTime, int numSamples, juce::AudioBuffer<float>& dest)
{
    const juce::ScopedTryLock tl (doc.lock);
    if (! tl.isLocked())
        return;

    const auto len = doc.getLength();
    const int chans = doc.getNumChannels();
    if (len == 0 || chans == 0) return;

    const double ratio = doc.sampleRate / deviceRate.load();
    const double startPos = startTime * doc.sampleRate;

    // envelope gains at chunk edges, linearly interpolated in between
    const float g0 = doc.gainAt (startTime);
    const float g1 = doc.gainAt (startTime + numSamples / deviceRate.load());

    for (int oc = 0; oc < dest.getNumChannels(); ++oc)
    {
        const int sc = juce::jmin (oc, chans - 1);
        const float* src = doc.buffer.getReadPointer (sc);
        float* out = dest.getWritePointer (oc);

        for (int i = 0; i < numSamples; ++i)
        {
            const double p = startPos + i * ratio;
            const auto idx = (juce::int64) p;
            if (idx < 0 || idx >= len) continue;
            const float frac = (float) (p - (double) idx);
            const float a = src[idx];
            const float b = idx + 1 < len ? src[idx + 1] : a;
            const float g = g0 + (g1 - g0) * ((float) i / (float) numSamples);
            out[i] += (a + (b - a) * frac) * g;
        }
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numIns,
                                                    float* const* outputs, int numOuts, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOuts; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::clear (outputs[c], numSamples);

    // ---- recording
    if (recording.load() && numIns > 0)
    {
        int s1, n1, s2, n2;
        recordFifo.prepareToWrite (numSamples, s1, n1, s2, n2);
        for (int c = 0; c < recordChannels; ++c)
        {
            const float* in = inputs[juce::jmin (c, numIns - 1)];
            if (in == nullptr) continue;
            if (n1 > 0) recordFifoBuffer.copyFrom (c, s1, in, n1);
            if (n2 > 0) recordFifoBuffer.copyFrom (c, s2, in + n1, n2);
        }
        recordFifo.finishedWrite (n1 + n2);

        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            if (inputs[0] != nullptr) peak = juce::jmax (peak, std::abs (inputs[0][i]));
        levels[0] = peak; levels[1] = peak;
        return;
    }

    if (mixBuffer.getNumSamples() < numSamples)
        return;   // should not happen; avoid allocating on the audio thread

    const juce::ScopedTryLock tl (docsLock);
    if (! tl.isLocked())
        return;

    juce::AudioBuffer<float> mix (mixBuffer.getArrayOfWritePointers(), 2, numSamples);
    mix.clear();

    const double rate = deviceRate.load();

    // ---- preview
    if (previewing.load())
    {
        const auto len = previewBuffer.getNumSamples();
        const double ratio = previewRate / rate;
        double pos = previewPos.load() * previewRate;
        for (int c = 0; c < 2; ++c)
        {
            if (previewBuffer.getNumChannels() == 0) break;
            const float* src = previewBuffer.getReadPointer (juce::jmin (c, previewBuffer.getNumChannels() - 1));
            for (int i = 0; i < numSamples; ++i)
            {
                auto p = pos + i * ratio;
                auto idx = (juce::int64) p;
                if (idx + 1 >= len) break;
                auto frac = (float) (p - (double) idx);
                mix.setSample (c, i, src[idx] + (src[idx + 1] - src[idx]) * frac);
            }
        }
        pos += numSamples * ratio;
        previewPos = pos / previewRate;
        if (pos >= (double) len)
        {
            previewing = false;
            reachedEnd = true;
        }
    }
    // ---- normal playback
    else if (playing.load())
    {
        const double end = getEndTime();
        double t = position.load();
        int done = 0;
        const bool loopOn = looping.load() && loopEnd.load() > loopStart.load() + 0.001;

        while (done < numSamples)
        {
            int todo = numSamples - done;
            const double limit = loopOn ? loopEnd.load() : end;
            const int untilLimit = (int) std::ceil ((limit - t) * rate);

            if (untilLimit <= 0)
            {
                if (loopOn) { t = loopStart.load(); continue; }
                playing = false;
                reachedEnd = true;
                break;
            }
            todo = juce::jmin (todo, untilLimit);

            juce::AudioBuffer<float> chunk (chunkBuffer.getArrayOfWritePointers(), 2, todo);
            chunk.clear();

            if (mixAll.load())
            {
                for (auto* d : documents)
                    renderDocument (*d, t, todo, chunk);
            }
            else if (current != nullptr)
            {
                renderDocument (*current, t, todo, chunk);
            }

            for (int c = 0; c < 2; ++c)
                mix.copyFrom (c, done, chunk, c, 0, todo);

            done += todo;
            t += todo / rate;
        }
        position = t;

        // real-time EQ
        if (eqEnabled.load())
        {
            const juce::SpinLock::ScopedTryLockType sl (eqLock);
            if (sl.isLocked() && eqDirty)
            {
                activeCoeffs = pendingCoeffs;
                eqDirty = false;
                for (auto& ch : eqFilters)
                    for (int b = 0; b < (int) ch.size(); ++b)
                        if (b < activeCoeffs.size())
                            ch[(size_t) b].coefficients = activeCoeffs[b];
            }

            for (int c = 0; c < 2 && c < (int) eqFilters.size(); ++c)
            {
                auto* d = mix.getWritePointer (c);
                for (int b = 0; b < activeCoeffs.size(); ++b)
                {
                    auto& f = eqFilters[(size_t) c][(size_t) b];
                    for (int i = 0; i < numSamples; ++i)
                        d[i] = f.processSample (d[i]);
                }
            }
        }
    }

    for (int c = 0; c < 2; ++c)
        levels[(size_t) c] = mix.getMagnitude (c, 0, numSamples);

    for (int c = 0; c < numOuts; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::copy (outputs[c], mix.getReadPointer (juce::jmin (c, 1)), numSamples);
}
