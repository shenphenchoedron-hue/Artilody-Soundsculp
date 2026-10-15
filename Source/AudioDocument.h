#pragma once

#include <JuceHeader.h>

struct Marker
{
    juce::String name;
    double time = 0.0;          // seconds
    int colour = 0;             // index into Theme::markerColour
};

struct EnvelopePoint
{
    double time = 0.0;          // seconds
    float gain = 1.0f;          // linear gain, 0 .. 2 (+6 dB)
};

//==============================================================================
// One opened (or recorded) audio file with its edit state, envelope, markers
// and undo history.
//==============================================================================
class AudioDocument : public juce::ChangeBroadcaster
{
public:
    AudioDocument() = default;

    static constexpr float maxEnvelopeGain = 2.0f;
    static constexpr int peakBlock = 256;

    // ----- file info
    juce::String name { "Untitled" };
    juce::File file;
    juce::String formatName { "WAV (PCM)" };
    int bitDepth = 24;
    juce::int64 fileSize = 0;
    bool modified = false;

    // ----- audio
    double sampleRate = 44100.0;
    juce::AudioBuffer<float> buffer;

    // ----- annotations (seconds based)
    std::vector<EnvelopePoint> envelope;      // sorted by time; empty == flat 1.0
    std::vector<Marker> markers;               // sorted by time

    // ----- selection / cursor (samples)
    juce::int64 selStart = 0, selEnd = 0;
    juce::int64 cursor = 0;

    // ----- view (samples)
    double viewStart = 0.0;
    double samplesPerPixel = 0.0;              // 0 == fit to width

    // Guards buffer + envelope against the audio thread.
    juce::CriticalSection lock;

    // ----- queries
    juce::int64 getLength() const              { return buffer.getNumSamples(); }
    int getNumChannels() const                 { return buffer.getNumChannels(); }
    double getLengthSeconds() const            { return sampleRate > 0 ? (double) getLength() / sampleRate : 0.0; }
    bool hasSelection() const                  { return selEnd > selStart; }
    double toSeconds (juce::int64 s) const     { return (double) s / sampleRate; }
    juce::int64 toSamples (double t) const     { return (juce::int64) std::llround (t * sampleRate); }

    float gainAt (double seconds) const;       // envelope value (no locking)
    // Fills gains for consecutive samples starting at startSample (no locking).
    void fillGains (juce::int64 startSample, int num, float* dest) const;

    // ----- editing (all call pushUndo themselves when desc is non-empty)
    juce::AudioBuffer<float> copyRange (juce::int64 start, juce::int64 end) const;
    // Replaces [start, end) with newData, shifting markers/envelope after the range.
    void replaceRange (juce::int64 start, juce::int64 end, const juce::AudioBuffer<float>& newData, const juce::String& undoName);
    void setBuffer (juce::AudioBuffer<float>&& newBuffer, double newRate);

    // Envelope editing (locks)
    int addEnvelopePoint (double time, float gain);
    void moveEnvelopePoint (int index, double time, float gain);
    void removeEnvelopePoint (int index);
    void resetEnvelope();

    // Markers
    int addMarker (double time, const juce::String& name = {});
    void sortMarkers();

    // ----- undo: call pushUndo() *before* changing the document
    void pushUndo (const juce::String& description);
    bool canUndo() const                       { return ! undoStack.empty(); }
    bool canRedo() const                       { return ! redoStack.empty(); }
    juce::String getUndoDescription() const    { return undoStack.empty() ? juce::String() : undoStack.back().description; }
    juce::String getRedoDescription() const    { return redoStack.empty() ? juce::String() : redoStack.back().description; }
    bool undo();
    bool redo();
    void clearHistory();

    // ----- peaks for fast waveform drawing
    struct Peak { float minV = 0, maxV = 0; };
    std::vector<std::vector<Peak>> peaks;
    void rebuildPeaks (juce::int64 fromSample = 0);

    void changed (bool markModified = true);   // rebuilds peaks + notifies listeners
    void annotationsChanged();                 // notifies listeners (envelope/markers)

    int nextMarkerColour = 0;

private:
    struct State
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        std::vector<EnvelopePoint> envelope;
        std::vector<Marker> markers;
        juce::int64 selStart = 0, selEnd = 0, cursor = 0;
        juce::String description;
    };

    std::vector<State> undoStack, redoStack;
    static constexpr size_t maxUndo = 40;

    State captureState (const juce::String& desc) const;
    void restoreState (const State&);
    void sortEnvelope();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDocument)
};
