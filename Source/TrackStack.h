#pragma once

#include <JuceHeader.h>
#include "WaveformView.h"
#include "Theme.h"

//==============================================================================
// Time ruler shared by all stacked tracks.
//==============================================================================
class TimeRuler : public juce::Component
{
public:
    explicit TimeRuler (Timeline& t) : timeline (t) {}
    void setDocument (AudioDocument* d)        { doc = d; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    std::function<void (double seconds)> onClick;

private:
    Timeline& timeline;
    AudioDocument* doc = nullptr;
    float xForTime (double t) const;
};

//==============================================================================
// One track: a header strip + its waveform view.
//==============================================================================
class TrackLane : public juce::Component
{
public:
    TrackLane (AudioEngine& engine, Timeline& timeline, AudioDocument* doc, int index);

    void setIndex (int i)                       { index = i; repaint(); }
    void setCurrent (bool c);
    void setDropHighlight (bool h)              { if (dropHighlight != h) { dropHighlight = h; repaint(); } }
    AudioDocument* getDocument() const          { return doc; }
    WaveformView& getView()                     { return view; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;

    std::function<void()> onSelect, onClose, onRename;
    std::function<void()> onMixChanged;           // mute / solo toggled (other lanes may change dimming)
    static constexpr int headerHeight = 28;

private:
    AudioDocument* doc;
    int index;
    bool current = false, dropHighlight = false;
    WaveformView view;
    IconButton closeButton { "close" };
    juce::TextButton muteButton { "M" }, soloButton { "S" };
};

//==============================================================================
// Vertical list of tracks with a zoom bar and a shared time ruler.
//==============================================================================
class TrackStack : public juce::Component
{
public:
    TrackStack (AudioEngine& engine, Timeline& timeline);

    void setDocuments (const std::vector<AudioDocument*>& docs, int currentIndex);
    WaveformView* getView (int index);
    int getNumLanes() const                     { return lanes.size(); }
    void repaintAll();
    void setTool (WaveformView::Tool t);
    void changeLaneHeight (float factor);
    void fitLaneHeights()                       { laneHeight = 0; layoutLanes(); }
    void ensureLaneVisible (int index);
    int laneIndexAt (juce::Point<int> screenPos) const;
    void setDropTarget (int index);

    void paint (juce::Graphics&) override;
    void resized() override;

    // view callbacks (index = lane / track index)
    std::function<void (int)> onActivate, onCloseTrack, onRenameTrack;
    std::function<void()> onSelectionChanged, onViewChanged, onEnvelopeEdited;
    std::function<void (int)> onMarkersChanged;
    std::function<void (double)> onRulerClick;
    std::function<bool (int fromLane, int clipIndex, int toLane)> onMoveClipToTrack;
    std::function<void (int lane, juce::int64 sample)> onContextMenu;
    std::function<void()> onOpen, onNewTrack, onZoomIn, onZoomOut, onZoomSelection, onZoomFit;

    TimeRuler ruler;

private:
    AudioEngine& engine;
    Timeline& timeline;
    WaveformView::Tool tool = WaveformView::Tool::select;
    int laneHeight = 0;           // 0 == automatic

    juce::Viewport viewport;
    juce::Component container;
    juce::OwnedArray<TrackLane> lanes;

    // zoom bar
    IconButton zoomOutButton { "zoomout", T ("Zoom out", "Zoom ud") };
    IconButton zoomInButton { "zoomin", T ("Zoom in", "Zoom ind") };
    IconButton zoomSelButton { "zoomsel", T ("Zoom to selection", "Zoom til markering") };
    IconButton zoomFitButton { "fit", T ("Show all", "Vis alt") };
    IconButton shorterButton { "shorter" }, tallerButton { "taller" };
    juce::Label zoomLabel, heightLabel;


    void layoutLanes();
    void wireView (TrackLane& lane);
    int indexOf (const TrackLane* lane) const;
    bool isSilenced (const AudioDocument* doc) const;  // same rule as the playback engine
    juce::Rectangle<int> barArea, rulerArea;

public:
    void updateZoomLabel();
};
