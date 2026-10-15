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
    eqResetPending = true;     // no filter tail from the previous playback
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
    s = juce::jmax (0.0, s);
    loopStart = s;
    loopEnd = juce::jmax (s, e);
}

void AudioEngine::playPreview (juce::AudioBuffer<float>&& buffer, double sampleRate)
{
    playing = false;
    {
        const juce::ScopedLock sl (docsLock);
        std::swap (previewBuffer, buffer);     // the old preview is freed outside the lock
        previewRate = sampleRate > 0 ? sampleRate : 44100.0;
        previewPos = 0.0;
    }
    { auto old = std::move (buffer); }
    previewing = true;
    if (onTransportChanged) onTransportChanged();
}

double AudioEngine::getEndTime() const
{
    // Audio thread: only cached (atomic) lengths, never the clip vectors.
    double end = 0.0;
    if (mixAll)
    {
        for (auto* d : documents)
            end = juce::jmax (end, d->getCachedLengthSeconds());
    }
    else if (current != nullptr)
    {
        end = current->getCachedLengthSeconds();
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
    const auto rate = deviceRate.load();
    auto coeffs = DSP::makeCoefficients (settings, rate > 0 ? rate : 44100.0);

    std::array<Biquad, 4> plain {};
    for (int b = 0; b < (int) plain.size() && b < coeffs.size(); ++b)
    {
        if (! settings.bands[(size_t) b].enabled || coeffs[b] == nullptr)
            continue;
        const auto& raw = coeffs[b]->coefficients;     // b0 b1 b2 a1 a2 (normalised)
        if (raw.size() < 5)
            continue;
        plain[(size_t) b] = { raw[0], raw[1], raw[2], raw[3], raw[4], true };
    }

    {
        const juce::SpinLock::ScopedLockType sl (eqLock);
        eqSettings = settings;
        pendingCoeffs = plain;
        eqDirty = true;
    }
    if (enabled && ! eqEnabled.load())
        eqResetPending = true;
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
    if (target == nullptr)
    {
        error = T ("There is no track to record into.", "Der er ikke noget spor at optage i.");
        return false;
    }

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

    if (recording.load())
        stopRecording();

    stop();
    recordChannels = juce::jlimit (1, 2, ins);
    recordTarget = target;
    recordedSamples = 0;
    recordOverflow = false;
    recordData.assign ((size_t) recordChannels, {});
    for (auto& v : recordData) v.reserve ((size_t) (deviceRate.load() * 60.0));
    recordPointers.assign ((size_t) recordChannels, nullptr);
    recordFifo.reset();

    std::vector<Clip> oldClips;
    {
        const juce::ScopedLock sl (target->lock);
        target->sampleRate = deviceRate.load();
        target->numChannels = recordChannels;
        target->clips.swap (oldClips);
        Clip c;
        c.start = 0;
        c.name = target->name;
        c.audio.setSize (recordChannels, 0);
        target->clips.push_back (std::move (c));
    }
    oldClips.clear();
    target->updateCachedLength();

    recording = true;
    if (onTransportChanged) onTransportChanged();
    return true;
}

AudioDocument* AudioEngine::stopRecording()
{
    if (! recording.load())
        return nullptr;

    recording = false;

    // A callback that saw recording == true may still be writing into the FIFO: wait for it.
    for (int i = 0; i < 500 && callbackActive.load(); ++i)
        juce::Thread::sleep (1);

    drainRecordFifo();   // the remaining audio

    auto* target = recordTarget;
    if (target != nullptr)
    {
        juce::AudioBuffer<float> owned (recordChannels, (int) recordedSamples);
        for (int c = 0; c < recordChannels; ++c)
            if (recordedSamples > 0)
                owned.copyFrom (c, 0, recordData[(size_t) c].data(), (int) recordedSamples);

        std::vector<Clip> oldClips;
        {
            const juce::ScopedLock sl (target->lock);
            if (recordedSamples > 0 && ! target->clips.empty())
                std::swap (target->clips[0].audio, owned);
            else
                target->clips.swap (oldClips);
        }
        target->changed();
    }

    recordTarget = nullptr;
    recordData.clear();
    recordPointers.clear();
    if (onTransportChanged) onTransportChanged();
    return target;
}

void AudioEngine::drainRecordFifo()
{
    if (recordTarget == nullptr)
        return;

    const int ready = recordFifo.getNumReady();
    if (ready <= 0)
        return;

    const auto oldLen = recordedSamples;
    const auto room = std::max<juce::int64> (0, maxRecordSamples - oldLen);
    const int toKeep = (int) std::min<juce::int64> (ready, room);
    if (toKeep < ready)
        recordOverflow = true;   // length limit reached - the rest is dropped
    const auto newLen = oldLen + toKeep;

    int start1, size1, start2, size2;
    recordFifo.prepareToRead (ready, start1, size1, start2, size2);

    bool haveClip = false;
    if (toKeep > 0)
    {
        // The record clip refers to recordData, so grow it under the document lock.
        const juce::ScopedLock sl (recordTarget->lock);
        for (auto& v : recordData)
        {
            if ((size_t) newLen > v.capacity())
                v.reserve (juce::jmax ((size_t) newLen, v.capacity() * 2));
            v.resize ((size_t) newLen);
        }

        const int n1 = juce::jmin (size1, toKeep);
        const int n2 = juce::jmin (size2, toKeep - n1);
        for (int c = 0; c < recordChannels; ++c)
        {
            auto* dst = recordData[(size_t) c].data() + oldLen;
            if (n1 > 0) std::copy (recordFifoBuffer.getReadPointer (c, start1), recordFifoBuffer.getReadPointer (c, start1) + n1, dst);
            if (n2 > 0) std::copy (recordFifoBuffer.getReadPointer (c, start2), recordFifoBuffer.getReadPointer (c, start2) + n2, dst + n1);
            recordPointers[(size_t) c] = recordData[(size_t) c].data();
        }

        haveClip = ! recordTarget->clips.empty();
        if (haveClip)
            recordTarget->clips[0].audio.setDataToReferTo (recordPointers.data(), recordChannels, (int) newLen);
    }
    recordFifo.finishedRead (size1 + size2);
    recordedSamples = newLen;

    if (haveClip)
    {
        recordTarget->clips[0].rebuildPeaks (oldLen);
        recordTarget->updateCachedLength();
        recordTarget->cursor = recordedSamples;
        recordTarget->sendChangeMessage();
    }
}

void AudioEngine::timerCallback()
{
    if (reachedEnd.exchange (false))
        if (onTransportChanged) onTransportChanged();

    drainRecordFifo();
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto rate = device->getCurrentSampleRate();
    deviceRate = rate > 0 ? rate : 44100.0;
    const int block = juce::jmax (device->getCurrentBufferSizeSamples(), 4096);
    mixBuffer.setSize (2, block);
    chunkBuffer.setSize (2, block);
    gainScratch.assign ((size_t) block, 1.0f);
    eqResetPending = true;
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

    const double rate = deviceRate.load();
    if (doc.sampleRate <= 0 || rate <= 0 || numSamples <= 0)
        return;

    const double ratio = doc.sampleRate / rate;
    const double startPos = startTime * doc.sampleRate;

    // Envelope gains per output sample (evaluated every 32 samples and interpolated, like
    // AudioDocument::fillGains() used by export). gainScratch is preallocated.
    const float* gains = nullptr;
    float flatGain = 1.0f;
    if (! doc.envelope.empty())
    {
        if ((int) gainScratch.size() >= numSamples)
        {
            constexpr int step = 32;
            float prev = doc.gainAt (startTime);
            for (int i = 0; i < numSamples; i += step)
            {
                const int n = juce::jmin (step, numSamples - i);
                const float next = doc.gainAt (startTime + (double) (i + n) / rate);
                for (int k = 0; k < n; ++k)
                    gainScratch[(size_t) (i + k)] = prev + (next - prev) * (float) k / (float) n;
                prev = next;
            }
            gains = gainScratch.data();
        }
        else
        {
            flatGain = doc.gainAt (startTime);
        }
    }

    for (auto& clip : doc.clips)
    {
        const auto len = clip.length();
        const int chans = clip.audio.getNumChannels();
        if (len == 0 || chans == 0) continue;

        // output range where this clip is active (computed in double, clamped before casting)
        const double rel0 = startPos - (double) clip.start;           // clip position at output sample 0
        const int i0 = (int) juce::jlimit (0.0, (double) numSamples, std::ceil (-rel0 / ratio));
        const int i1 = (int) juce::jlimit (0.0, (double) numSamples, std::ceil (((double) len - rel0) / ratio));
        if (i1 <= i0) continue;

        for (int oc = 0; oc < dest.getNumChannels(); ++oc)
        {
            const float* src = clip.audio.getReadPointer (juce::jmin (oc, chans - 1));
            float* out = dest.getWritePointer (oc);
            for (int i = i0; i < i1; ++i)
            {
                const double p = rel0 + i * ratio;
                const auto idx = (juce::int64) p;
                if (idx < 0 || idx >= len) continue;
                const float frac = (float) (p - (double) idx);
                const float a = src[idx];
                const float b = idx + 1 < len ? src[idx + 1] : a;
                const float g = gains != nullptr ? gains[i] : flatGain;
                out[i] += (a + (b - a) * frac) * g;
            }
        }
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numIns,
                                                    float* const* outputs, int numOuts, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    callbackActive = true;

    for (int c = 0; c < numOuts; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::clear (outputs[c], numSamples);

    // ---- recording
    if (recording.load())
    {
        float peak = 0.0f;
        if (numIns > 0 && inputs != nullptr)
        {
            int s1, n1, s2, n2;
            recordFifo.prepareToWrite (numSamples, s1, n1, s2, n2);
            if (n1 + n2 < numSamples)
                recordOverflow = true;   // the message thread did not keep up - audio is lost
            for (int c = 0; c < recordChannels; ++c)
            {
                const float* in = inputs[juce::jmin (c, numIns - 1)];
                if (in == nullptr)
                {
                    if (n1 > 0) recordFifoBuffer.clear (c, s1, n1);
                    if (n2 > 0) recordFifoBuffer.clear (c, s2, n2);
                    continue;
                }
                if (n1 > 0) recordFifoBuffer.copyFrom (c, s1, in, n1);
                if (n2 > 0) recordFifoBuffer.copyFrom (c, s2, in + n1, n2);
            }
            recordFifo.finishedWrite (n1 + n2);

            if (inputs[0] != nullptr)
            {
                const auto r = juce::FloatVectorOperations::findMinAndMax (inputs[0], numSamples);
                peak = juce::jmax (-r.getStart(), r.getEnd());
            }
        }
        levels[0] = peak; levels[1] = peak;
        callbackActive = false;
        return;
    }

    // ---- playback, in sub-blocks that fit the preallocated buffers
    float peaks[2] = { 0.0f, 0.0f };
    const int maxBlock = juce::jmin (mixBuffer.getNumSamples(), chunkBuffer.getNumSamples());
    if (maxBlock > 0)
        for (int offset = 0; offset < numSamples; offset += maxBlock)
            processBlock (outputs, numOuts, offset, juce::jmin (maxBlock, numSamples - offset), peaks);

    levels[0] = peaks[0];
    levels[1] = peaks[1];
    callbackActive = false;
}

void AudioEngine::processBlock (float* const* outputs, int numOuts, int offset, int numSamples, float* peaks)
{
    const juce::ScopedTryLock tl (docsLock);
    if (! tl.isLocked())
        return;

    juce::AudioBuffer<float> mix (mixBuffer.getArrayOfWritePointers(), 2, numSamples);
    mix.clear();

    const double rate = deviceRate.load();
    if (rate <= 0)
        return;

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
        previewPos = previewRate > 0 ? pos / previewRate : 0.0;
        if (pos >= (double) len || previewRate <= 0)
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
        const double seek = pendingSeek.exchange (-1.0);
        if (seek >= 0.0)
            t = seek;

        int done = 0;
        const bool loopOn = looping.load() && loopEnd.load() > loopStart.load() + 0.001;

        while (done < numSamples)
        {
            int todo = numSamples - done;
            const double limit = loopOn ? loopEnd.load() : end;
            // computed and clamped in double before converting (no overflow for long audio)
            const double remaining = std::ceil ((limit - t) * rate);
            const int untilLimit = remaining >= (double) todo ? todo : (int) juce::jmax (0.0, remaining);

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
                bool anySolo = false;
                for (auto* d : documents) anySolo = anySolo || d->soloed.load();
                for (auto* d : documents)
                    if (anySolo ? d->soloed.load() : ! d->muted.load())
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
        if (pendingSeek.load() < 0.0)   // a seek that arrived meanwhile wins
            position = t;

        // real-time EQ (no allocation: plain coefficients copied under a try-lock)
        const bool eqOn = eqEnabled.load();
        if (eqOn)
        {
            const bool resetAll = eqResetPending.exchange (false) || ! eqWasEnabled;
            {
                const juce::SpinLock::ScopedTryLockType sl (eqLock);
                if (sl.isLocked() && eqDirty)
                {
                    for (size_t b = 0; b < activeCoeffs.size(); ++b)
                        if (pendingCoeffs[b].active && ! activeCoeffs[b].active)
                            for (auto& ch : eqState) ch[b] = {};    // band switched on: start clean
                    activeCoeffs = pendingCoeffs;
                    eqDirty = false;
                }
            }
            if (resetAll)
                for (auto& ch : eqState) for (auto& st : ch) st = {};

            for (int c = 0; c < 2; ++c)
            {
                auto* d = mix.getWritePointer (c);
                for (size_t b = 0; b < activeCoeffs.size(); ++b)
                {
                    const auto& k = activeCoeffs[b];
                    if (! k.active) continue;
                    auto& st = eqState[(size_t) c][b];
                    float z1 = st.z1, z2 = st.z2;
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float x = d[i];
                        const float y = k.b0 * x + z1;
                        z1 = k.b1 * x - k.a1 * y + z2;
                        z2 = k.b2 * x - k.a2 * y;
                        d[i] = y;
                    }
                    st.z1 = z1; st.z2 = z2;
                }
            }
        }
        eqWasEnabled = eqOn;
    }

    for (int c = 0; c < 2; ++c)
        peaks[c] = juce::jmax (peaks[c], mix.getMagnitude (c, 0, numSamples));

    for (int c = 0; c < numOuts; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::copy (outputs[c] + offset, mix.getReadPointer (juce::jmin (c, 1)), numSamples);
}
