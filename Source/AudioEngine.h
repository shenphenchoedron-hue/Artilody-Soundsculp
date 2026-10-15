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
    void setPosition (double seconds)
    {
        seconds = std::isfinite (seconds) ? juce::jmax (0.0, seconds) : 0.0;
        position = seconds;
        pendingSeek = seconds;     // consumed by the audio callback (works during playback)
    }

    // Plays a temporary buffer (used for effect previews). Stops normal playback.
    void playPreview (juce::AudioBuffer<float>&& buffer, double sampleRate);
    bool isPreviewing() const                         { return previewing.load(); }
    double getPreviewPosition() const                 { return previewPos.load(); }

    // Recording into a document that grows while recording.
    bool startRecording (AudioDocument* target, juce::String& error);
    AudioDocument* stopRecording();                   // returns the document that was recorded (nullptr if none)
    AudioDocument* getRecordTarget() const            { return recordTarget; }   // message thread
    bool isRecording() const                          { return recording.load(); }
    // True if input audio was lost (FIFO overflow or length limit) since recording started.
    bool hadRecordingOverflow() const                 { return recordOverflow.load(); }
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
    void processBlock (float* const* outputs, int numOuts, int offset, int numSamples, float* peaks);
    void drainRecordFifo();
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
    std::atomic<double> pendingSeek { -1.0 };   // < 0 == none
    std::atomic<bool> callbackActive { false };
    std::atomic<double> deviceRate { 44100.0 };
    std::array<std::atomic<float>, 2> levels {};

    juce::AudioBuffer<float> mixBuffer, chunkBuffer;
    std::vector<float> gainScratch;             // envelope gains, sized like mixBuffer

    // Preview
    juce::AudioBuffer<float> previewBuffer;
    double previewRate = 44100.0;
    std::atomic<double> previewPos { 0.0 };   // seconds

    // EQ: plain biquad coefficients (computed on the message thread, copied without allocation)
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        bool active = false;   // false == pass-through
    };
    struct BiquadState { float z1 = 0.0f, z2 = 0.0f; };

    juce::SpinLock eqLock;
    EQSettings eqSettings;
    std::atomic<bool> eqEnabled { false };
    std::atomic<bool> eqResetPending { true };
    bool eqDirty = false;                       // guarded by eqLock
    bool eqWasEnabled = false;                  // audio thread only
    std::array<Biquad, 4> pendingCoeffs, activeCoeffs;
    std::array<std::array<BiquadState, 4>, 2> eqState {};   // [channel][band]

    // Recording
    AudioDocument* recordTarget = nullptr;
    int recordChannels = 2;
    juce::AbstractFifo recordFifo { 1 << 19 };
    juce::AudioBuffer<float> recordFifoBuffer { 2, 1 << 19 };
    std::vector<std::vector<float>> recordData;
    std::vector<float*> recordPointers;
    juce::int64 recordedSamples = 0;
    std::atomic<bool> recordOverflow { false };
    static constexpr juce::int64 maxRecordSamples = (juce::int64) std::numeric_limits<int>::max() - 65536;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
