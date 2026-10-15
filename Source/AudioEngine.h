#pragma once

#include <JuceHeader.h>
#include "AudioDocument.h"
#include "DSP.h"

//==============================================================================
// Playback (single track or all tracks mixed), preview, real-time EQ and
// recording. Positions are kept in seconds so tracks with different sample
// rates can be mixed and played on any device rate.
//==============================================================================
class AudioEngine : public juce::AudioIODeviceCallback,
                    private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager()     { return deviceManager; }
    juce::String initialise (const juce::XmlElement* savedState);

    // Documents (tracks). 'current' is the one played unless mix-all is on.
    void setDocuments (const std::vector<AudioDocument*>& all, AudioDocument* current);
    void setMixAll (bool shouldMix)                   { mixAll = shouldMix; }
    bool getMixAll() const                            { return mixAll; }

    // Transport
    void play();
    void stop();
    bool isPlaying() const                            { return playing.load(); }
    void setLooping (bool shouldLoop)                 { looping = shouldLoop; }
    bool isLooping() const                            { return looping.load(); }
    void setLoopRange (double startSeconds, double endSeconds);
    double getPosition() const                        { return position.load(); }
    void setPosition (double seconds)                 { position = juce::jmax (0.0, seconds); }

    // Plays a temporary buffer (used for effect previews). Stops normal playback.
    void playPreview (juce::AudioBuffer<float>&& buffer, double sampleRate);
    bool isPreviewing() const                         { return previewing.load(); }
    double getPreviewPosition() const                 { return previewPos.load(); }

    // Recording into a document that grows while recording.
    bool startRecording (AudioDocument* target, juce::String& error);
    void stopRecording();
    bool isRecording() const                          { return recording.load(); }
    int getNumInputChannels() const;

    // Real-time EQ on normal playback
    void setEQ (const EQSettings& settings, bool enabled);

    double getDeviceSampleRate() const                { return deviceRate.load(); }
    float getLevel (int channel) const                { return levels[(size_t) juce::jlimit (0, 1, channel)].load(); }

    std::function<void()> onTransportChanged;         // called on the message thread

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numIns,
                                           float* const* outputs, int numOuts, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    void timerCallback() override;
    void renderDocument (AudioDocument& doc, double startTime, int numSamples, juce::AudioBuffer<float>& dest);
    double getEndTime() const;
    void rebuildEQ();

    juce::AudioDeviceManager deviceManager;

    juce::CriticalSection docsLock;
    std::vector<AudioDocument*> documents;
    AudioDocument* current = nullptr;

    std::atomic<bool> playing { false }, looping { false }, mixAll { true };   // all tracks play together
    std::atomic<bool> previewing { false }, recording { false };
    std::atomic<bool> reachedEnd { false };
    std::atomic<double> position { 0.0 }, loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<double> deviceRate { 44100.0 };
    std::array<std::atomic<float>, 2> levels {};

    juce::AudioBuffer<float> mixBuffer, chunkBuffer;

    // Preview
    juce::AudioBuffer<float> previewBuffer;
    double previewRate = 44100.0;
    std::atomic<double> previewPos { 0.0 };   // seconds

    // EQ
    juce::SpinLock eqLock;
    EQSettings eqSettings;
    std::atomic<bool> eqEnabled { false };
    bool eqDirty = false;
    juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>> pendingCoeffs, activeCoeffs;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 4>, 2> eqFilters;   // [channel][band]

    // Recording
    AudioDocument* recordTarget = nullptr;
    int recordChannels = 2;
    juce::AbstractFifo recordFifo { 1 << 19 };
    juce::AudioBuffer<float> recordFifoBuffer { 2, 1 << 19 };
    std::vector<std::vector<float>> recordData;
    std::vector<float*> recordPointers;
    juce::int64 recordedSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
