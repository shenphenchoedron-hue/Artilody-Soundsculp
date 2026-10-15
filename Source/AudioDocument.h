#pragma once

#include <JuceHeader.h>

struct Marker
{
    juce::String name;
    double time = 0.0;          // seconds
    juce::Colour colour { 0xff3b82f6 };
};

struct EnvelopePoint
{
    double time = 0.0;          // seconds
    float gain = 1.0f;          // linear gain, 0 .. 2 (+6 dB)
};

struct Peak { float minV = 0, maxV = 0; };

//==============================================================================
// A piece of audio placed somewhere on a track (like an Audacity clip).
//==============================================================================
struct Clip
{
    juce::int64 start = 0;                     // position on the track (samples)
    juce::AudioBuffer<float> audio;
    juce::String name;
    std::vector<std::vector<Peak>> peaks;      // per channel, one per peakBlock samples

    juce::int64 length() const                 { return audio.getNumSamples(); }
    juce::int64 end() const                    { return start + length(); }
    void rebuildPeaks (juce::int64 fromSample = 0);

    Clip() = default;
    Clip (const Clip& other)                   { *this = other; }
    Clip& operator= (const Clip& other)
    {
        if (this != &other)
        {
            start = other.start;
            audio.makeCopyOf (other.audio);
            name = other.name;
            peaks = other.peaks;
        }
        return *this;
    }
    Clip (Clip&&) = default;
    Clip& operator= (Clip&&) = default;
};

//==============================================================================
// One track: clips (with gaps between them), volume envelope, markers and
// undo history.
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

    // ----- mixer state (read by the audio thread)
    std::atomic<bool> muted { false }, soloed { false };

    // ----- audio
    double sampleRate = 44100.0;
    int numChannels = 2;
    // Never overlapping. Sorted by start after every structural edit (split, insert, join, addClip,
    // changed(), undo/redo). moveClip() keeps indexes stable during a drag, so the order may be
    // temporarily unsorted until the next changed().
    std::vector<Clip> clips;

    // ----- annotations (seconds based, belong to the track timeline)
    std::vector<EnvelopePoint> envelope;      // sorted by time; empty == flat 1.0
    std::vector<Marker> markers;               // sorted by time

    // ----- selection / cursor (samples)
    juce::int64 selStart = 0, selEnd = 0;
    juce::int64 cursor = 0;

    // Guards clips + envelope against the audio thread.
    juce::CriticalSection lock;

    // ----- queries
    juce::int64 getLength() const;             // end of the last clip
    int getNumChannels() const                 { return numChannels; }
    double getLengthSeconds() const            { return sampleRate > 0 ? (double) getLength() / sampleRate : 0.0; }
    // Thread safe (audio thread): length cached by the message thread after every change.
    double getCachedLengthSeconds() const noexcept { return cachedLengthSeconds.load(); }
    void updateCachedLength();                 // message thread; called by all primitives and changed()
    bool hasSelection() const                  { return selEnd > selStart; }
    double toSeconds (juce::int64 s) const     { return (double) s / sampleRate; }
    juce::int64 toSamples (double t) const     { return (juce::int64) std::llround (t * sampleRate); }
    int clipAt (juce::int64 pos) const;        // index of the clip containing pos, or -1
    bool hasAudioIn (juce::int64 s, juce::int64 e) const;

    float gainAt (double seconds) const;       // envelope value (no locking)
    void fillGains (juce::int64 startSample, int num, float* dest) const;

    // ----- rendering (gaps are silent)
    void renderInto (juce::int64 start, int numSamples, juce::AudioBuffer<float>& dest, int destStart) const;
    juce::AudioBuffer<float> copyRange (juce::int64 start, juce::int64 end) const;

    // ----- editing primitives (they do NOT push undo - call pushUndo() first)
    void setBuffer (juce::AudioBuffer<float>&& newBuffer, double newRate);   // replace everything by one clip at 0
    void removeRange (juce::int64 s, juce::int64 e);                         // leaves a gap
    void rippleRemove (juce::int64 s, juce::int64 e);                        // removes and closes the gap
    void insertClip (juce::int64 pos, juce::AudioBuffer<float> data, const juce::String& clipName);  // pushes later audio right
    void splitAt (juce::int64 pos);
    void joinRange (juce::int64 s, juce::int64 e);                           // joins clips touching [s,e), gaps -> silence
    // Calls fn on every clip portion inside [s,e) (same length in/out); absStart = track position of portion[0].
    void processRange (juce::int64 s, juce::int64 e, const std::function<void (juce::AudioBuffer<float>&, juce::int64 absStart)>& fn);
    juce::int64 findFreePosition (juce::int64 desired, juce::int64 length, int excludeIndex) const;
    void moveClip (int index, juce::int64 newStart);                         // keeps clips sorted
    Clip takeClip (int index);
    int addClip (Clip c);                                                    // placed at a free position near c.start
    void setChannelCount (int channels);                                     // converts all clips

    // Envelope editing (locks)
    int addEnvelopePoint (double time, float gain);
    void moveEnvelopePoint (int index, double time, float gain);
    void removeEnvelopePoint (int index);
    void resetEnvelope();

    // Markers
    int addMarker (double time, const juce::String& name = {});
    void sortMarkers();

    // ----- undo: call pushUndo() *before* changing the document
    // annotationsOnly = true stores only envelope, markers and selection/cursor (no clip audio).
    void pushUndo (const juce::String& description, bool annotationsOnly = false);
    bool canUndo() const                       { return ! undoStack.empty(); }
    bool canRedo() const                       { return ! redoStack.empty(); }
    juce::String getUndoDescription() const    { return undoStack.empty() ? juce::String() : undoStack.back().description; }
    juce::String getRedoDescription() const    { return redoStack.empty() ? juce::String() : redoStack.back().description; }
    bool undo();
    bool redo();
    void clearHistory();

    void rebuildPeaks();
    void changed (bool markModified = true);   // rebuilds peaks + notifies listeners
    void annotationsChanged();                 // notifies listeners (envelope/markers)

    int nextMarkerColour = 0;
    int nextClipNumber = 1;

private:
    struct State
    {
        std::vector<Clip> clips;
        double sampleRate = 44100.0;
        int numChannels = 2;
        std::vector<EnvelopePoint> envelope;
        std::vector<Marker> markers;
        juce::int64 selStart = 0, selEnd = 0, cursor = 0;
        juce::String description;
        bool annotationsOnly = false;
        size_t bytes = 0;                       // approximate memory used by the clip audio
    };

    std::vector<State> undoStack, redoStack;
    static constexpr size_t maxUndo = 40;
    static constexpr size_t maxUndoBytes = (size_t) 1536 * 1024 * 1024;   // budget for full states (both stacks)

    std::atomic<double> cachedLengthSeconds { 0.0 };

    State captureState (const juce::String& desc, bool annotationsOnly) const;
    void applyState (State&);                   // swaps s with the current document (cheap); s receives the old data
    bool stepHistory (std::vector<State>& from, std::vector<State>& to);
    void trimHistory();
    static size_t clipBytes (const std::vector<Clip>&);
    void sortEnvelope();
    void sortClips();
    void shiftAnnotations (double fromSec, double removedUntilSec, double deltaSec);
    juce::String newClipName();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDocument)
};
