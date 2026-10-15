#include "TrackStack.h"

static double niceRulerStep (double rough)
{
    const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200 };
    for (auto s : steps) if (s >= rough) return s;
    return 3600.0;
}

//==============================================================================
float TimeRuler::xForTime (double t) const
{
    return (float) WaveformView::gutter + (float) ((t - timeline.startSec) / timeline.effSecPerPixel());
}

void TimeRuler::paint (juce::Graphics& g)
{
    auto full = getLocalBounds();
    g.setColour (Theme::waveBg2);
    g.fillRect (full);
    g.setColour (Theme::grid);
    g.drawHorizontalLine (full.getBottom() - 1, 0.0f, (float) getWidth());

    const double spp = timeline.effSecPerPixel();
    const double major = niceRulerStep (spp * 80.0);
    const double minor = major / 5.0;
    const double t0 = timeline.startSec;
    const double t1 = t0 + timeline.visibleSec();

    g.setFont (Theme::font (12.0f));
    long k0 = (long) std::floor (t0 / minor);
    for (long k = k0; (double) k * minor <= t1 + minor; ++k)
    {
        const double t = (double) k * minor;
        if (t < 0) continue;
        auto x = xForTime (t);
        if (x < (float) WaveformView::gutter - 1) continue;
        const bool isMajor = (k % 5) == 0;
        g.setColour (juce::Colours::white.withAlpha (isMajor ? 0.55f : 0.22f));
        g.drawVerticalLine ((int) x, (float) full.getBottom() - (isMajor ? 10.0f : 5.0f), (float) full.getBottom());
        if (isMajor)
        {
            juce::String label = major < 1.0 ? juce::String (t, major < 0.01 ? 3 : (major < 0.1 ? 2 : 1)) + "s"
                                             : formatTimeShort (t);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawText (label, (int) x - 40, full.getY() + 3, 80, 16, juce::Justification::centred);
        }
    }

    if (doc != nullptr)
    {
        if (doc->hasSelection())
        {
            g.setColour (Theme::waveSel);
            for (auto s : { doc->selStart, doc->selEnd })
                g.fillRect (juce::Rectangle<float> (xForTime (doc->toSeconds (s)) - 0.5f, (float) full.getBottom() - 5, 1.5f, 5.0f));
        }
    }
}

void TimeRuler::mouseDown (const juce::MouseEvent& e)
{
    if (onClick && e.position.x >= (float) WaveformView::gutter)
        onClick (juce::jmax (0.0, timeline.startSec + (e.position.x - (float) WaveformView::gutter) * timeline.effSecPerPixel()));
}

void TimeRuler::mouseDrag (const juce::MouseEvent& e)  { mouseDown (e); }

//==============================================================================
TrackLane::TrackLane (AudioEngine& engine, Timeline& timeline, AudioDocument* d, int i)
    : doc (d), index (i), view (engine, timeline)
{
    view.setShowRuler (false);
    view.setDocument (d);
    addAndMakeVisible (view);

    closeButton.setColours (juce::Colours::transparentBlack, juce::Colours::white.withAlpha (0.7f));
    closeButton.setTooltip (T ("Close track", "Luk spor"));
    // closing deletes this lane, so do it after the click has finished
    closeButton.onClick = [this]
    {
        juce::Component::SafePointer<TrackLane> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onClose) safe->onClose(); });
    };
    addAndMakeVisible (closeButton);

    auto setupToggle = [this] (juce::TextButton& b, juce::Colour on, const juce::String& tip)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, Theme::waveBg2.brighter (0.25f));
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.8f));
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    setupToggle (muteButton, juce::Colour (0xfff97316), T ("Mute: silence this track during playback", "Mute: gør sporet lydløst under afspilning"));
    setupToggle (soloButton, juce::Colour (0xffffd84a), T ("Solo: hear only this track (and other solo tracks)", "Solo: hør kun dette spor (og andre solo-spor)"));
    muteButton.setToggleState (doc->muted.load(), juce::dontSendNotification);
    soloButton.setToggleState (doc->soloed.load(), juce::dontSendNotification);
    muteButton.onClick = [this] { doc->muted = muteButton.getToggleState(); repaint(); view.repaint(); };
    soloButton.onClick = [this] { doc->soloed = soloButton.getToggleState(); repaint(); view.repaint(); };
}

void TrackLane::setCurrent (bool c)
{
    if (current != c)
    {
        current = c;
        view.setIsCurrent (c);
        repaint();
    }
}

void TrackLane::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (current ? Theme::accent.withAlpha (0.9f) : Theme::waveBg2.brighter (0.06f));
    g.fillRect (header);

    auto r = header.reduced (10, 0);
    g.setColour (juce::Colours::white);
    g.setFont (Theme::font (13.5f, true));
    auto num = juce::String (index + 1);
    g.drawText (num, r.removeFromLeft (20), juce::Justification::centredLeft);
    g.setFont (Theme::font (13.5f, current));
    auto nameW = juce::jmin ((int) Theme::textWidth (Theme::font (13.5f, current), doc->name) + 12, r.getWidth() - 60);
    g.drawText ((doc->modified ? "* " : "") + doc->name, r.removeFromLeft (nameW + 12), juce::Justification::centredLeft, true);
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.setFont (Theme::font (12.0f));
    juce::String info = formatTime (doc->getLengthSeconds()) + "   " + juce::String (doc->sampleRate / 1000.0, 1) + " kHz   "
                        + (doc->getNumChannels() == 1 ? "Mono" : "Stereo");
    if (doc->getLength() == 0)
        info = T ("Empty track - click a position and paste or insert a file", "Tomt spor - klik en position og indsæt eller indsæt en fil");
    g.drawText (info, r.withTrimmedRight (100), juce::Justification::centredLeft, true);

    // separator below the lane
    g.setColour (Theme::grid.brighter (0.5f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    if (current)
    {
        g.setColour (Theme::accent);
        g.drawRect (getLocalBounds(), 2);
    }
}

void TrackLane::paintOverChildren (juce::Graphics& g)
{
    if (dropHighlight)
    {
        g.setColour (juce::Colour (0xff1fbf75).withAlpha (0.18f));
        g.fillRect (getLocalBounds().withTrimmedTop (headerHeight));
        g.setColour (juce::Colour (0xff1fbf75));
        g.drawRect (getLocalBounds(), 3);
    }
}

void TrackLane::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);
    closeButton.setBounds (header.removeFromRight (headerHeight).reduced (5));
    header.removeFromRight (6);
    soloButton.setBounds (header.removeFromRight (26).reduced (1, 4));
    header.removeFromRight (4);
    muteButton.setBounds (header.removeFromRight (26).reduced (1, 4));
    view.setBounds (r.withTrimmedBottom (1));
}

void TrackLane::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect();
}

void TrackLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.y < (float) headerHeight && onRename) onRename();
}

//==============================================================================
TrackStack::TrackStack (AudioEngine& e, Timeline& t) : ruler (t), engine (e), timeline (t)
{
    addAndMakeVisible (ruler);
    ruler.onClick = [this] (double s) { if (onRulerClick) onRulerClick (s); };

    viewport.setViewedComponent (&container, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    auto setupBar = [this] (IconButton& b, const juce::String& tip)
    {
        b.setColours (Theme::waveBg2.brighter (0.08f), juce::Colours::white);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    setupBar (zoomOutButton, T ("Zoom out (-)", "Zoom ud (-)"));
    setupBar (zoomInButton, T ("Zoom in (+). Ctrl + mouse wheel zooms at the mouse.", "Zoom ind (+). Ctrl + musehjul zoomer ved musen."));
    setupBar (zoomSelButton, T ("Zoom so the selection fills the window", "Zoom så markeringen fylder vinduet"));
    setupBar (zoomFitButton, T ("Show the whole length (0)", "Vis hele længden (0)"));
    setupBar (shorterButton, T ("Lower track height (Alt + mouse wheel)", "Lavere spor (Alt + musehjul)"));
    setupBar (tallerButton, T ("Taller tracks (Alt + mouse wheel)", "Højere spor (Alt + musehjul)"));
    zoomOutButton.onClick = [this] { if (onZoomOut) onZoomOut(); };
    zoomInButton.onClick = [this] { if (onZoomIn) onZoomIn(); };
    zoomSelButton.onClick = [this] { if (onZoomSelection) onZoomSelection(); };
    zoomFitButton.onClick = [this] { if (onZoomFit) onZoomFit(); };
    shorterButton.onClick = [this] { changeLaneHeight (1.0f / 1.25f); };
    tallerButton.onClick = [this] { changeLaneHeight (1.25f); };

    for (auto* l : { &zoomLabel, &heightLabel })
    {
        l->setFont (Theme::font (13.0f));
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
        addAndMakeVisible (l);
    }
    heightLabel.setText (T ("Track height", "Sporhøjde"), juce::dontSendNotification);
    heightLabel.setJustificationType (juce::Justification::centredRight);


    updateZoomLabel();
}

int TrackStack::indexOf (const TrackLane* lane) const
{
    return lanes.indexOf (lane);
}

void TrackStack::wireView (TrackLane& lane)
{
    auto* lp = &lane;
    auto& v = lane.getView();
    v.setTool (tool);
    v.onActivate = [this, lp] { if (onActivate) onActivate (indexOf (lp)); };
    v.onSelectionChanged = [this] { ruler.repaint(); if (onSelectionChanged) onSelectionChanged(); };
    v.onViewChanged = [this] { if (onViewChanged) onViewChanged(); };
    v.onMarkersChanged = [this] (int i) { ruler.repaint(); if (onMarkersChanged) onMarkersChanged (i); };
    v.onEnvelopeEdited = [this] { if (onEnvelopeEdited) onEnvelopeEdited(); };
    v.wheelScrollsVertically = [this] { return container.getHeight() > viewport.getHeight() + 2; };
    v.onTrackHeightWheel = [this] (float f) { changeLaneHeight (f); };
    v.onClipDragOver = [this, lp] (juce::Point<int> pos)
    {
        int target = laneIndexAt (pos);
        setDropTarget (target != indexOf (lp) ? target : -1);
    };
    v.onClipDropped = [this, lp] (int clipIndex, juce::Point<int> pos)
    {
        setDropTarget (-1);
        int target = laneIndexAt (pos);
        int from = indexOf (lp);
        if (target < 0 || target == from || ! onMoveClipToTrack) return false;
        // the lane may be rebuilt, so do the move after this mouse event
        juce::MessageManager::callAsync ([this, from, clipIndex, target] { onMoveClipToTrack (from, clipIndex, target); });
        return true;
    };
    v.onContextMenu = [this, lp] (juce::int64 sample) { if (onContextMenu) onContextMenu (indexOf (lp), sample); };
    lane.onSelect = [this, lp] { if (onActivate) onActivate (indexOf (lp)); };
    lane.onClose = [this, lp] { if (onCloseTrack) onCloseTrack (indexOf (lp)); };
    lane.onRename = [this, lp] { if (onRenameTrack) onRenameTrack (indexOf (lp)); };
}

void TrackStack::setDocuments (const std::vector<AudioDocument*>& docs, int currentIndex)
{
    bool same = (int) docs.size() == lanes.size();
    for (int i = 0; same && i < lanes.size(); ++i)
        same = lanes[i]->getDocument() == docs[(size_t) i];

    if (! same)
    {
        // keep lanes for documents that still exist (preserves their state)
        juce::OwnedArray<TrackLane> old;
        old.swapWith (lanes);
        for (int i = 0; i < (int) docs.size(); ++i)
        {
            TrackLane* lane = nullptr;
            for (int k = 0; k < old.size(); ++k)
                if (old[k]->getDocument() == docs[(size_t) i]) { lane = old.removeAndReturn (k); break; }
            if (lane == nullptr)
            {
                lane = new TrackLane (engine, timeline, docs[(size_t) i], i);
                wireView (*lane);
            }
            lane->setIndex (i);
            lanes.add (lane);
            container.addAndMakeVisible (lane);
        }
        old.clear();
        layoutLanes();
    }

    for (int i = 0; i < lanes.size(); ++i)
        lanes[i]->setCurrent (i == currentIndex);

    ruler.setDocument (juce::isPositiveAndBelow (currentIndex, (int) docs.size()) ? docs[(size_t) currentIndex] : nullptr);
    repaintAll();
}

WaveformView* TrackStack::getView (int index)
{
    return juce::isPositiveAndBelow (index, lanes.size()) ? &lanes[index]->getView() : nullptr;
}

void TrackStack::repaintAll()
{
    for (auto* l : lanes) { l->repaint(); l->getView().repaint(); }
    ruler.repaint();
    updateZoomLabel();
}

void TrackStack::setTool (WaveformView::Tool t)
{
    tool = t;
    for (auto* l : lanes) l->getView().setTool (t);
}

void TrackStack::changeLaneHeight (float factor)
{
    int current = laneHeight > 0 ? laneHeight
                                 : (lanes.isEmpty() ? 200 : lanes[0]->getHeight());
    laneHeight = juce::jlimit (110, 1200, (int) std::round ((float) current * factor));
    layoutLanes();
}

void TrackStack::ensureLaneVisible (int index)
{
    if (! juce::isPositiveAndBelow (index, lanes.size())) return;
    auto b = lanes[index]->getBounds();
    auto pos = viewport.getViewPosition();
    if (b.getY() < pos.y)
        viewport.setViewPosition (0, b.getY());
    else if (b.getBottom() > pos.y + viewport.getHeight())
        viewport.setViewPosition (0, b.getBottom() - viewport.getHeight());
}

int TrackStack::laneIndexAt (juce::Point<int> screenPos) const
{
    for (int i = 0; i < lanes.size(); ++i)
        if (lanes[i]->getScreenBounds().contains (screenPos))
            return i;
    return -1;
}

void TrackStack::setDropTarget (int index)
{
    for (int i = 0; i < lanes.size(); ++i)
        lanes[i]->setDropHighlight (i == index);
}

void TrackStack::layoutLanes()
{
    const int avail = viewport.getHeight();
    const int n = lanes.size();
    int h = laneHeight;
    if (h <= 0)
        h = n <= 1 ? avail : juce::jmax (190, avail / juce::jmax (1, n));

    const bool needsScroll = h * n > avail;
    const int width = viewport.getWidth() - (needsScroll ? viewport.getScrollBarThickness() : 0);
    container.setSize (width, juce::jmax (avail, h * n));
    for (int i = 0; i < n; ++i)
        lanes[i]->setBounds (0, i * h, width, h);


    ruler.setBounds (rulerArea.withWidth (width));
    updateZoomLabel();
}

void TrackStack::updateZoomLabel()
{
    zoomLabel.setText (T ("Zoom ", "Zoom ") + juce::String ((int) std::round (timeline.zoomFactor() * 100.0)) + "%",
                       juce::dontSendNotification);
}

void TrackStack::paint (juce::Graphics& g)
{
    g.setColour (Theme::waveBg);
    g.fillRect (getLocalBounds());
    g.setColour (Theme::waveBg2);
    g.fillRect (barArea);

    if (lanes.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (Theme::font (14.0f));
        g.drawText (T ("No audio files open", "Ingen lydfiler åbne"), viewport.getBounds(), juce::Justification::centred);
    }
}

void TrackStack::resized()
{
    auto r = getLocalBounds();
    barArea = r.removeFromTop (40);
    {
        auto b = barArea.reduced (8, 5);
        auto place = [&b] (IconButton& btn)
        {
            btn.setBounds (b.removeFromLeft (btn.getIdealWidth (b.getHeight())));
            b.removeFromLeft (6);
        };
        place (zoomOutButton);
        place (zoomInButton);
        zoomLabel.setBounds (b.removeFromLeft (90));
        place (zoomSelButton);
        place (zoomFitButton);
        tallerButton.setBounds (b.removeFromRight (b.getHeight()));
        b.removeFromRight (6);
        shorterButton.setBounds (b.removeFromRight (b.getHeight()));
        heightLabel.setBounds (b.removeFromRight (100));
    }
    rulerArea = r.removeFromTop (WaveformView::rulerHeight);
    viewport.setBounds (r);
    layoutLanes();
}
