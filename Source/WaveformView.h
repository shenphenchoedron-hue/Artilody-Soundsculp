#pragma once

#include <JuceHeader.h>
#include "AudioDocument.h"
#include "AudioEngine.h"

//==============================================================================
// Shared horizontal view state (in seconds) so all stacked tracks line up.
//==============================================================================
struct Timeline
{
    double startSec = 0.0;
    double secPerPixel = 0.0;      // 0 == fit everything
    double totalSec = 30.0;        // length of the longest track (or 30 s)
    int width = 1000;              // pixel width of the waveform area

    static constexpr double minSecPerPixel = 1.0 / 48000.0 * 0.05;

    double fitSecPerPixel() const  { return totalSec / (double) juce::jmax (1, width); }
    double effSecPerPixel() const  { return secPerPixel > 0.0 ? secPerPixel : fitSecPerPixel(); }
    double visibleSec() const      { return effSecPerPixel() * width; }
    double zoomFactor() const      { return fitSecPerPixel() / effSecPerPixel(); }   // 1 == fit

    void scrollTo (double start)
    {
        startSec = juce::jlimit (0.0, juce::jmax (0.0, totalSec - visibleSec()), start);
    }

    // Zooms so that 'anchorSec' stays at pixel offset 'anchorX'.
    void zoomTo (double spp, double anchorSec, double anchorX)
    {
        const double fit = fitSecPerPixel();
        spp = juce::jlimit (juce::jmin (minSecPerPixel, fit), fit, spp);
        secPerPixel = spp >= fit * 0.999 ? 0.0 : spp;
        scrollTo (anchorSec - anchorX * effSecPerPixel());
    }

    void setTotal (double t)
    {
        totalSec = juce::jmax (0.01, t);
        scrollTo (startSec);
    }
};

//==============================================================================
// Main editor view: time ruler, waveform per channel, selection, volume
// envelope, playhead, and the marker lane underneath the waveform.
//==============================================================================
class WaveformView : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    enum class Tool { select, volume };

    WaveformView (AudioEngine& engine, Timeline& timeline);

    void setDocument (AudioDocument* d);
    AudioDocument* getDocument() const                 { return doc; }
    void setTool (Tool t)                              { tool = t; repaint(); }
    void setShowRuler (bool b)                         { showRuler = b; resized(); }
    void setIsCurrent (bool b)                         { if (isCurrent != b) { isCurrent = b; repaint(); } }
    double viewStartSamples() const;
    Tool getTool() const                               { return tool; }

    // view helpers
    double getFitSamplesPerPixel() const;
    double getSamplesPerPixel() const;
    void setSamplesPerPixel (double spp, double anchorSample);
    void zoom (double factor);                         // >1 zooms in
    void zoomToFit();
    void ensureVisible (double sample);
    double getVisibleSamples() const;
    double getTotalViewSamples() const;                // max(length, 30 s) so empty tracks can be clicked
    void scrollTo (double startSample);

    std::function<void()> onSelectionChanged;
    std::function<void()> onViewChanged;
    std::function<void (int markerIndex)> onMarkersChanged;   // index of new/edited marker or -1
    std::function<void()> onEnvelopeEdited;
    std::function<void()> onActivate;                  // mouse pressed in this view
    std::function<bool()> wheelScrollsVertically;      // let the parent viewport scroll
    std::function<void (float)> onTrackHeightWheel;    // Alt + wheel
    std::function<void (juce::Point<int> screenPos)> onClipDragOver;           // clip dragged (for drop highlight)
    std::function<bool (int clipIndex, juce::Point<int> screenPos)> onClipDropped; // true if moved to another track
    std::function<void (juce::int64 sample)> onContextMenu;                    // right-click in the waveform

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int rulerHeight = 34;
    static constexpr int markerLaneHeight = 30;
    static constexpr int gutter = 40;
    static constexpr int clipBarHeight = 18;

private:
    AudioEngine& engine;
    Timeline& timeline;
    AudioDocument* doc = nullptr;
    Tool tool = Tool::select;
    bool showRuler = true, isCurrent = true;
    double rate() const;

    juce::Rectangle<int> rulerArea, waveArea, laneArea;

    // interaction state
    enum class Drag { none, newSelection, selStartEdge, selEndEdge, envelopePoint, marker, scrub, clip };
    Drag drag = Drag::none;
    juce::int64 dragAnchor = 0, dragClipOffset = 0;
    bool clipUndoPushed = false;
    int dragIndex = -1;
    bool dragMoved = false;
    juce::Point<int> mouseDownPos;
    int hoverEnvelope = -1, hoverMarker = -1;

    float xForSample (double sample) const;
    double sampleForX (float x) const;
    float yForGain (float gain) const;
    float gainForY (float y) const;
    int findEnvelopePointAt (juce::Point<float> p) const;
    bool isNearEnvelopeLine (juce::Point<float> p) const;
    int findMarkerAt (juce::Point<float> p) const;
    juce::int64 clampSample (double s) const;

    void drawRuler (juce::Graphics&);
    void drawClips (juce::Graphics&);
    void drawChannels (juce::Graphics&);
    int findClipBarAt (juce::Point<float> p) const;
    void drawEnvelope (juce::Graphics&);
    void drawMarkerLane (juce::Graphics&);
    void drawPlayhead (juce::Graphics&);
    void showMarkerMenu (int index);
};
