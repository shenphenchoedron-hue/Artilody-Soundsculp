#pragma once

#include <JuceHeader.h>
#include "AudioDocument.h"
#include "AudioEngine.h"

//==============================================================================
// Main editor view: time ruler, waveform per channel, selection, volume
// envelope, playhead, and the marker lane underneath the waveform.
//==============================================================================
class WaveformView : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    enum class Tool { select, volume };

    explicit WaveformView (AudioEngine& engine);

    void setDocument (AudioDocument* d);
    AudioDocument* getDocument() const                 { return doc; }
    void setTool (Tool t)                              { tool = t; repaint(); }
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

private:
    AudioEngine& engine;
    AudioDocument* doc = nullptr;
    Tool tool = Tool::select;

    juce::Rectangle<int> rulerArea, waveArea, laneArea;

    // interaction state
    enum class Drag { none, newSelection, selStartEdge, selEndEdge, envelopePoint, marker, scrub };
    Drag drag = Drag::none;
    juce::int64 dragAnchor = 0;
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
    void drawChannels (juce::Graphics&);
    void drawEnvelope (juce::Graphics&);
    void drawMarkerLane (juce::Graphics&);
    void drawPlayhead (juce::Graphics&);
    void showMarkerMenu (int index);
};

//==============================================================================
// Overview strip showing the whole track with the visible range and selection.
//==============================================================================
class OverviewView : public juce::Component
{
public:
    explicit OverviewView (WaveformView& main) : mainView (main) {}

    void setDocument (AudioDocument* d)    { doc = d; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    WaveformView& mainView;
    AudioDocument* doc = nullptr;
    void centreAt (float x);
};
