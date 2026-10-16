#include "WaveformView.h"
#include "Theme.h"

// Markers under the track only show their number; the text is in the Markers panel.
static juce::String markerLabel (int index)
{
    return juce::String (index + 1);
}

static float markerFlagWidth (int index, const juce::String&)
{
    return juce::jmax (22.0f, Theme::textWidth (Theme::font (13.0f, true), markerLabel (index)) + 14.0f);
}

WaveformView::WaveformView (AudioEngine& e, Timeline& t) : engine (e), timeline (t)
{
    setWantsKeyboardFocus (false);
    setOpaque (true);
}

void WaveformView::setDocument (AudioDocument* d)
{
    doc = d;
    repaint();
}

//==============================================================================
double WaveformView::rate() const
{
    return doc != nullptr ? doc->sampleRate : 44100.0;
}

double WaveformView::viewStartSamples() const
{
    return timeline.startSec * rate();
}

double WaveformView::getTotalViewSamples() const
{
    return timeline.totalSec * rate();
}

double WaveformView::getFitSamplesPerPixel() const
{
    return timeline.fitSecPerPixel() * rate();
}

double WaveformView::getSamplesPerPixel() const
{
    return timeline.effSecPerPixel() * rate();
}

double WaveformView::getVisibleSamples() const
{
    return getSamplesPerPixel() * waveArea.getWidth();
}

void WaveformView::scrollTo (double startSample)
{
    timeline.scrollTo (startSample / rate());
    if (onViewChanged) onViewChanged();
}

void WaveformView::setSamplesPerPixel (double spp, double anchorSample)
{
    timeline.zoomTo (spp / rate(), anchorSample / rate(), (double) (xForSample (anchorSample) - (float) waveArea.getX()));
    if (onViewChanged) onViewChanged();
}

void WaveformView::zoom (double factor)
{
    if (doc == nullptr) return;
    double anchor = doc->hasSelection() ? (double) (doc->selStart + doc->selEnd) * 0.5
                                        : (double) doc->cursor;
    if (anchor < viewStartSamples() || anchor > viewStartSamples() + getVisibleSamples())
        anchor = viewStartSamples() + getVisibleSamples() * 0.5;
    setSamplesPerPixel (getSamplesPerPixel() / factor, anchor);
}

void WaveformView::zoomToFit()
{
    timeline.secPerPixel = 0.0;
    timeline.startSec = 0.0;
    if (onViewChanged) onViewChanged();
}

void WaveformView::ensureVisible (double sample)
{
    auto vis = getVisibleSamples();
    if (sample < viewStartSamples() || sample > viewStartSamples() + vis)
        scrollTo (sample - vis * 0.05);
}

//==============================================================================
float WaveformView::xForSample (double s) const
{
    if (doc == nullptr) return (float) waveArea.getX();
    return (float) waveArea.getX() + (float) ((s - viewStartSamples()) / getSamplesPerPixel());
}

double WaveformView::sampleForX (float x) const
{
    if (doc == nullptr) return 0.0;
    return viewStartSamples() + (double) (x - (float) waveArea.getX()) * getSamplesPerPixel();
}

juce::int64 WaveformView::clampSample (double s) const
{
    return (juce::int64) juce::jlimit (0.0, getTotalViewSamples(), std::round (s));
}

void WaveformView::pushPendingAnnotationUndo()
{
    if (doc == nullptr || annotationUndoPushed) return;
    doc->pushUndo (pendingUndoName, true);       // markers / volume line only
    annotationUndoPushed = true;
}

float WaveformView::yForGain (float gain) const
{
    auto r = waveArea.withTrimmedTop (clipBarHeight).toFloat().reduced (0, 12);
    return r.getBottom() - (gain / AudioDocument::maxEnvelopeGain) * r.getHeight();
}

float WaveformView::gainForY (float y) const
{
    auto r = waveArea.withTrimmedTop (clipBarHeight).toFloat().reduced (0, 12);
    return juce::jlimit (0.0f, AudioDocument::maxEnvelopeGain,
                         (r.getBottom() - y) / r.getHeight() * AudioDocument::maxEnvelopeGain);
}

int WaveformView::findEnvelopePointAt (juce::Point<float> p) const
{
    if (doc == nullptr) return -1;
    for (int i = (int) doc->envelope.size(); --i >= 0;)
    {
        auto& e = doc->envelope[(size_t) i];
        juce::Point<float> c (xForSample (e.time * doc->sampleRate), yForGain (e.gain));
        if (c.getDistanceFrom (p) < 9.0f)
            return i;
    }
    return -1;
}

bool WaveformView::isNearEnvelopeLine (juce::Point<float> p, float tolerance) const
{
    if (doc == nullptr) return false;
    auto g = doc->gainAt (sampleForX (p.x) / doc->sampleRate);
    return std::abs (yForGain (g) - p.y) < tolerance;
}

int WaveformView::findClipBarAt (juce::Point<float> p) const
{
    if (doc == nullptr || p.y < (float) waveArea.getY() || p.y > (float) (waveArea.getY() + clipBarHeight)) return -1;
    for (int i = 0; i < (int) doc->clips.size(); ++i)
    {
        auto& c = doc->clips[(size_t) i];
        if (p.x >= xForSample ((double) c.start) && p.x < xForSample ((double) c.end()))
            return i;
    }
    return -1;
}

int WaveformView::findMarkerAt (juce::Point<float> p) const
{
    if (doc == nullptr) return -1;
    juce::GlyphArrangement ga;
    for (int i = (int) doc->markers.size(); --i >= 0;)
    {
        auto& m = doc->markers[(size_t) i];
        auto x = xForSample (m.time * doc->sampleRate);
        auto w = markerFlagWidth (i, m.name);
        if (p.x >= x - 6 && p.x <= x + w)
            return i;
    }
    return -1;
}

//==============================================================================
void WaveformView::resized()
{
    auto r = getLocalBounds();
    rulerArea = r.removeFromTop (showRuler ? rulerHeight : 0).withTrimmedLeft (gutter);
    laneArea = r.removeFromBottom (markerLaneHeight).withTrimmedLeft (gutter);
    waveArea = r.withTrimmedLeft (gutter);
    if (waveArea.getWidth() > 0 && timeline.width != waveArea.getWidth())
    {
        timeline.width = waveArea.getWidth();
        timeline.scrollTo (timeline.startSec);
        if (onViewChanged) onViewChanged();      // the zoom controls depend on the width
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::waveBg);

    if (doc == nullptr)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (Theme::font (18.0f));
        g.drawText (T ("Open audio files with  +  or drag them here", "Åbn lydfiler med  +  eller træk dem herind"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (showRuler) drawRuler (g);
    drawClips (g);
    drawChannels (g);
    drawEnvelope (g);
    drawMarkerLane (g);
    const bool silenced = isSilenced ? isSilenced() : doc->muted.load();
    if (silenced)
    {
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (waveArea);
    }
    drawPlayhead (g);
}

static double niceStep (double rough)
{
    const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200 };
    for (auto s : steps) if (s >= rough) return s;
    return 3600.0;
}

void WaveformView::drawRuler (juce::Graphics& g)
{
    auto full = rulerArea.withLeft (0);
    g.setColour (Theme::waveBg2);
    g.fillRect (full);
    g.setColour (Theme::grid);
    g.drawHorizontalLine (full.getBottom() - 1, 0.0f, (float) getWidth());

    const double sr = doc->sampleRate;
    const double secsPerPixel = getSamplesPerPixel() / sr;
    const double major = niceStep (secsPerPixel * 80.0);
    const double minor = major / 5.0;
    const double t0 = viewStartSamples() / sr;
    const double t1 = t0 + getVisibleSamples() / sr;

    g.setFont (Theme::font (12.0f));
    for (double t = std::floor (t0 / minor) * minor; t <= t1 + minor; t += minor)
    {
        if (t < 0) continue;
        auto x = xForSample (t * sr);
        if (x < (float) rulerArea.getX() - 1) continue;
        bool isMajor = std::abs (std::fmod (t + major * 0.5 * 1e-6, major)) < minor * 0.5
                       || std::abs (std::fmod (t, major) - major) < minor * 0.5;
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

    // marker triangles
    for (auto& m : doc->markers)
    {
        auto x = xForSample (m.time * sr);
        if (x < (float) rulerArea.getX() - 8 || x > (float) getWidth() + 8) continue;
        juce::Path tri;
        tri.addTriangle (x - 7, (float) full.getBottom() - 14, x + 7, (float) full.getBottom() - 14, x, (float) full.getBottom() - 2);
        g.setColour (m.colour);
        g.fillPath (tri);
    }

    // selection edge triangles (ruler)
    if (doc->hasSelection())
    {
        g.setColour (Theme::waveSel);
        for (auto s : { doc->selStart, doc->selEnd })
        {
            auto x = xForSample ((double) s);
            g.fillRect (juce::Rectangle<float> (x - 0.5f, (float) full.getBottom() - 4, 1.5f, 4.0f));
        }
    }
}

void WaveformView::drawClips (juce::Graphics& g)
{
    // gaps (no audio) are darker than the clips
    g.setColour (juce::Colour (0xff0e1426));
    g.fillRect (waveArea);

    g.setFont (Theme::font (11.5f, true));
    for (int i = 0; i < (int) doc->clips.size(); ++i)
    {
        auto& clip = doc->clips[(size_t) i];
        const float x1 = juce::jmax ((float) waveArea.getX(), xForSample ((double) clip.start));
        const float x2 = juce::jmin ((float) waveArea.getRight(), xForSample ((double) clip.end()));
        if (x2 <= x1) continue;
        auto body = juce::Rectangle<float> (x1, (float) waveArea.getY(), x2 - x1, (float) waveArea.getHeight());
        g.setColour (Theme::waveBg);
        g.fillRect (body);

        const bool clipSelected = doc->hasSelection() && doc->selStart <= clip.start && doc->selEnd >= clip.end();
        const bool dragging = drag == Drag::clip && dragIndex == i;
        auto bar = body.withHeight ((float) clipBarHeight);
        g.setColour (dragging ? Theme::accent : (clipSelected ? Theme::selection.brighter (0.2f) : juce::Colour (0xff2b3b62)));
        g.fillRect (bar);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        if (bar.getWidth() > 24)
            g.drawText (clip.name, bar.reduced (5, 0), juce::Justification::centredLeft, true);

        // clip edges
        g.setColour (juce::Colour (0xff3d5080));
        g.drawVerticalLine ((int) xForSample ((double) clip.start), (float) waveArea.getY(), (float) waveArea.getBottom());
        g.drawVerticalLine ((int) xForSample ((double) clip.end()) - 1, (float) waveArea.getY(), (float) waveArea.getBottom());
    }
}

void WaveformView::drawChannels (juce::Graphics& g)
{
    const int channels = juce::jmax (1, doc->getNumChannels());
    auto area = waveArea.withTrimmedTop (clipBarHeight);
    const int chH = area.getHeight() / channels;
    const double spp = getSamplesPerPixel();
    const auto len = doc->getLength();

    // gutter background
    g.setColour (Theme::waveBg);
    g.fillRect (0, waveArea.getY(), gutter, waveArea.getHeight());

    const float selX1 = doc->hasSelection() ? xForSample ((double) doc->selStart) : -1.0f;
    const float selX2 = doc->hasSelection() ? xForSample ((double) doc->selEnd) : -1.0f;

    // markers as faint vertical lines
    for (auto& m : doc->markers)
    {
        auto x = xForSample (m.time * doc->sampleRate);
        if (x < (float) waveArea.getX() || x > (float) waveArea.getRight()) continue;
        g.setColour (m.colour.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, (float) waveArea.getY(), (float) waveArea.getBottom());
    }

    for (int c = 0; c < channels; ++c)
    {
        auto r = juce::Rectangle<int> (area.getX(), area.getY() + c * chH, area.getWidth(), chH);
        auto inner = r.toFloat().reduced (0, (float) chH * 0.08f);
        const float mid = inner.getCentreY();
        const float half = inner.getHeight() * 0.5f;

        // grid + labels
        g.setFont (Theme::font (11.0f));
        for (float v : { 1.0f, 0.5f, -0.5f, -1.0f })
        {
            const bool isHalf = std::abs (v) < 0.9f;
            if (isHalf && chH < 110) continue;          // avoid crowded labels on low tracks
            auto y = mid - v * half;
            g.setColour (Theme::grid.withAlpha (0.8f));
            g.drawHorizontalLine ((int) y, (float) r.getX(), (float) r.getRight());
            if (chH >= 56)
            {
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawText (juce::String (v, 1), 2, (int) y - 7, gutter - 8, 14, juce::Justification::centredRight);
            }
        }
        g.setColour (Theme::grid.brighter (0.2f));
        g.drawHorizontalLine ((int) mid, (float) r.getX(), (float) r.getRight());
        if (c > 0)
        {
            g.setColour (Theme::grid.brighter (0.4f));
            g.drawHorizontalLine (r.getY(), 0.0f, (float) getWidth());
        }

        // channel badge
        juce::String badge = channels == 1 ? "M" : (channels == 2 ? (c == 0 ? "L" : "R") : juce::String (c + 1));
        auto badgeR = juce::Rectangle<float> (8.0f, mid - 10.0f, 16.0f, 20.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRoundedRectangle (badgeR, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (Theme::font (11.0f, true));
        g.drawText (badge, badgeR, juce::Justification::centred);

        if (c >= doc->getNumChannels() || len == 0)
            continue;
        juce::ignoreUnused (len);

        juce::RectangleList<float> normal, selected;
        const auto viewS0 = viewStartSamples();
        const auto viewS1 = viewS0 + getVisibleSamples();

        for (auto& clip : doc->clips)
        {
            const auto clen = clip.length();
            if (clen == 0 || clip.end() < viewS0 || (double) clip.start > viewS1) continue;
            if (c >= clip.audio.getNumChannels()) continue;
            const float* data = clip.audio.getReadPointer (c);
            if (clip.peaks.size() <= (size_t) c) clip.rebuildPeaks();
            auto& peaks = clip.peaks[(size_t) c];

            if (spp < 1.0)
            {
                juce::Path p;
                bool started = false;
                auto first = (juce::int64) juce::jmax ((double) clip.start, std::floor (viewS0));
                auto last = (juce::int64) juce::jmin ((double) clip.end() - 1, std::ceil (viewS1) + 1);
                for (auto sAbs = first; sAbs <= last; ++sAbs)
                {
                    auto x = xForSample ((double) sAbs);
                    auto y = mid - juce::jlimit (-1.2f, 1.2f, data[sAbs - clip.start]) * half;
                    if (! started) { p.startNewSubPath (x, y); started = true; }
                    else p.lineTo (x, y);
                    if (spp < 0.15)
                        g.setColour (Theme::waveB), g.fillEllipse (x - 2, y - 2, 4, 4);
                }
                g.setColour (Theme::waveB);
                g.strokePath (p, juce::PathStrokeType (1.5f));
                continue;
            }

            const int x0 = juce::jmax (0, (int) std::floor (((double) clip.start - viewS0) / spp));
            const int x1 = juce::jmin (area.getWidth(), (int) std::ceil (((double) clip.end() - viewS0) / spp));
            for (int x = x0; x < x1; ++x)
            {
                const double s0 = viewS0 + x * spp - (double) clip.start;
                const double s1 = s0 + spp;
                if (s1 < 0 || s0 >= (double) clen) continue;
                auto a2 = (juce::int64) juce::jmax (0.0, s0);
                auto b2 = (juce::int64) juce::jmin ((double) clen, std::ceil (s1));
                if (b2 <= a2) b2 = a2 + 1;
                if (b2 > clen) continue;

                float mn = 0, mx = 0;
                if (spp >= AudioDocument::peakBlock && ! peaks.empty())
                {
                    auto pa = (size_t) (a2 / AudioDocument::peakBlock);
                    auto pb = juce::jmin (peaks.size(), (size_t) ((b2 + AudioDocument::peakBlock - 1) / AudioDocument::peakBlock));
                    mn = 1.0f; mx = -1.0f;
                    for (auto i = pa; i < pb; ++i) { mn = juce::jmin (mn, peaks[i].minV); mx = juce::jmax (mx, peaks[i].maxV); }
                    if (mn > mx) mn = mx = 0;
                }
                else
                {
                    auto range = juce::FloatVectorOperations::findMinAndMax (data + a2, (int) (b2 - a2));
                    mn = range.getStart(); mx = range.getEnd();
                }

                auto y1 = mid - juce::jlimit (-1.0f, 1.0f, mx) * half;
                auto y2 = mid - juce::jlimit (-1.0f, 1.0f, mn) * half;
                auto px = (float) (area.getX() + x);
                juce::Rectangle<float> col (px, y1, 1.0f, juce::jmax (1.0f, y2 - y1));
                if (px >= selX1 && px < selX2) selected.add (col);
                else normal.add (col);
            }
        }

        g.setGradientFill (juce::ColourGradient (Theme::waveA, (float) area.getX(), 0, Theme::waveB, (float) area.getRight(), 0, false));
        g.fillRectList (normal);
        g.setGradientFill (juce::ColourGradient (Theme::waveSel, 0, inner.getY(), Theme::waveSel.withRotatedHue (-0.05f), 0, inner.getBottom(), false));
        g.fillRectList (selected);
    }

    // selection overlay
    if (doc->hasSelection())
    {
        auto x1 = juce::jmax ((float) waveArea.getX(), selX1);
        auto x2 = juce::jmin ((float) waveArea.getRight(), selX2);
        if (x2 > x1)
        {
            g.setColour (Theme::selection.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (x1, (float) waveArea.getY(), x2 - x1, (float) waveArea.getHeight()));
        }

        const float dash[] = { 4.0f, 3.0f };
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (auto x : { selX1, selX2 })
        {
            if (x < (float) waveArea.getX() || x > (float) waveArea.getRight()) continue;
            g.drawDashedLine (juce::Line<float> (x, (float) waveArea.getY(), x, (float) waveArea.getBottom()), dash, 2, 1.2f);
            auto handle = juce::Rectangle<float> (10.0f, 32.0f).withCentre ({ x, (float) waveArea.getY() + (float) chH * 0.5f });
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (handle, 4.0f);
            g.setColour (Theme::waveBg);
            g.drawRoundedRectangle (handle, 4.0f, 1.5f);
            g.drawVerticalLine ((int) x, handle.getY() + 8, handle.getBottom() - 8);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
        }
    }
    else if (! engine.isPlaying())
    {
        auto x = xForSample ((double) doc->cursor);
        if (x >= (float) waveArea.getX() && x <= (float) waveArea.getRight())
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawVerticalLine ((int) x, (float) waveArea.getY(), (float) waveArea.getBottom());
        }
    }
}

void WaveformView::drawEnvelope (juce::Graphics& g)
{
    const bool active = ! doc->envelope.empty();
    const float left = (float) waveArea.getX(), right = (float) waveArea.getRight();
    const float lenX = juce::jmin (right, xForSample ((double) doc->getLength()));

    const bool editing = tool == Tool::volume;

    if (! active)
    {
        // The volume line is always visible: flat at 0 dB until points are added
        if (lenX <= left) return;
        const float dash[] = { 6.0f, 4.0f };
        g.setColour (Theme::envelope.withAlpha (editing ? 0.9f : 0.55f));
        g.drawDashedLine (juce::Line<float> (left, yForGain (1.0f), lenX, yForGain (1.0f)), dash, 2, editing ? 2.0f : 1.5f);
        return;
    }

    juce::Path line;
    const double sr = doc->sampleRate;
    for (float x = left; x <= lenX; x += 2.0f)
    {
        auto y = yForGain (doc->gainAt (sampleForX (x) / sr));
        if (x == left) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    // Make sure the corners at the envelope points are exact
    juce::Path exact;
    bool started = false;
    std::vector<juce::Point<float>> pts;
    pts.push_back ({ left, yForGain (doc->gainAt (sampleForX (left) / sr)) });
    for (auto& e : doc->envelope)
    {
        auto x = xForSample (e.time * sr);
        if (x > left && x < lenX) pts.push_back ({ x, yForGain (e.gain) });
    }
    pts.push_back ({ lenX, yForGain (doc->gainAt (sampleForX (lenX) / sr)) });
    for (auto& p : pts) { if (! started) { exact.startNewSubPath (p); started = true; } else exact.lineTo (p); }

    juce::Path fill (exact);
    fill.lineTo (lenX, (float) waveArea.getBottom());
    fill.lineTo (left, (float) waveArea.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Theme::envelope.withAlpha (0.30f), 0, (float) waveArea.getY() + waveArea.getHeight() * 0.3f,
                                             Theme::envelope.withAlpha (0.0f), 0, (float) waveArea.getBottom(), false));
    g.fillPath (fill);

    g.setColour (Theme::envelope.withAlpha (editing ? 1.0f : 0.8f));
    g.strokePath (exact, juce::PathStrokeType (editing ? 2.4f : 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (int i = 0; i < (int) doc->envelope.size(); ++i)
    {
        auto& e = doc->envelope[(size_t) i];
        auto c = juce::Point<float> (xForSample (e.time * sr), yForGain (e.gain));
        if (c.x < left - 8 || c.x > right + 8) continue;
        const float rad = (i == hoverEnvelope || (drag == Drag::envelopePoint && dragIndex == i)) ? 7.5f : 6.0f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (c));
        g.setColour (Theme::envelope);
        g.drawEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (c), 2.5f);

        if (drag == Drag::envelopePoint && dragIndex == i)
        {
            auto db = juce::Decibels::gainToDecibels (e.gain, -60.0f);
            juce::String txt = e.gain <= 0.001f ? juce::String ("-inf dB") : ((db >= 0 ? "+" : "") + juce::String (db, 1) + " dB");
            auto box = juce::Rectangle<float> (70.0f, 22.0f).withCentre ({ c.x, c.y - 22.0f });
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (juce::Colours::white);
            g.setFont (Theme::font (12.0f, true));
            g.drawText (txt, box, juce::Justification::centred);
        }
    }
}

void WaveformView::drawMarkerLane (juce::Graphics& g)
{
    auto full = laneArea.withLeft (0);
    g.setColour (Theme::waveBg2);
    g.fillRect (full);
    g.setColour (Theme::grid);
    g.drawHorizontalLine (full.getY(), 0.0f, (float) getWidth());

    Icons::draw (g, "marker", juce::Rectangle<float> (12.0f, (float) full.getY() + 7.0f, 16.0f, 16.0f),
                 juce::Colours::white.withAlpha (0.5f));

    if (doc->markers.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (Theme::font (12.0f));
        g.drawText (T ("Click here to add a marker", "Klik her for at indsætte en markør"),
                    laneArea.reduced (8, 0), juce::Justification::centredLeft);
    }

    g.saveState();
    g.reduceClipRegion (laneArea);
    for (int i = 0; i < (int) doc->markers.size(); ++i)
    {
        auto& m = doc->markers[(size_t) i];
        auto x = xForSample (m.time * doc->sampleRate);
        auto col = m.colour;
        auto w = markerFlagWidth (i, m.name);
        auto flag = juce::Rectangle<float> (x, (float) laneArea.getY() + 4.0f, w, (float) laneArea.getHeight() - 8.0f);
        g.setColour (col.withAlpha (i == hoverMarker ? 1.0f : 0.88f));
        g.fillRoundedRectangle (flag, 4.0f);
        g.fillRect (flag.withWidth (3.0f));
        g.setColour (col.getPerceivedBrightness() > 0.7f ? juce::Colours::black : juce::Colours::white);
        g.setFont (Theme::font (13.0f, true));
        g.drawText (markerLabel (i), flag.withTrimmedLeft (3.0f), juce::Justification::centred, false);
    }
    g.restoreState();
}

void WaveformView::drawPlayhead (juce::Graphics& g)
{
    const bool playingHere = engine.isPlaying() && (engine.getMixAll() || isCurrent);
    const bool recordingHere = engine.isRecording() && engine.getRecordTarget() == doc && doc->getLength() > 0;
    if (! (playingHere || recordingHere))
        return;

    double sample = recordingHere ? (double) doc->getLength() : engine.getPosition() * doc->sampleRate;
    auto x = xForSample (sample);
    if (x < (float) waveArea.getX() || x > (float) waveArea.getRight()) return;

    g.setColour (recordingHere ? Theme::record : Theme::playhead);
    g.fillRect (juce::Rectangle<float> (x - 1.0f, (float) rulerArea.getY(), 2.0f, (float) (laneArea.getBottom() - rulerArea.getY())));
    juce::Path tri;
    tri.addTriangle (x - 6, (float) rulerArea.getY(), x + 6, (float) rulerArea.getY(), x, (float) rulerArea.getY() + 8);
    g.fillPath (tri);
}

//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (doc == nullptr) return;
    auto p = e.position;
    int newHoverEnv = waveArea.contains (e.getPosition()) ? findEnvelopePointAt (p) : -1;
    int newHoverMarker = laneArea.contains (e.getPosition()) ? findMarkerAt (p) : -1;
    if (newHoverEnv != hoverEnvelope || newHoverMarker != hoverMarker)
    {
        hoverEnvelope = newHoverEnv;
        hoverMarker = newHoverMarker;
        repaint();
    }

    auto cursor = juce::MouseCursor::NormalCursor;
    if (findClipBarAt (p) >= 0)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (T ("Clip: drag the bar to move the clip - also onto another track. Click selects the clip.",
                       "Klip: træk i bjælken for at flytte klippet - også til et andet spor. Klik markerer klippet."));
        return;
    }
    if (waveArea.contains (e.getPosition()))
    {
        if (hoverEnvelope >= 0)
            cursor = juce::MouseCursor::DraggingHandCursor;
        else if (tool == Tool::volume && isNearEnvelopeLine (p, 24.0f))
            cursor = juce::MouseCursor::CrosshairCursor;
        else if (doc->hasSelection() && (std::abs (p.x - xForSample ((double) doc->selStart)) < 6
                                         || std::abs (p.x - xForSample ((double) doc->selEnd)) < 6))
            cursor = juce::MouseCursor::LeftRightResizeCursor;
        else
            cursor = juce::MouseCursor::IBeamCursor;
    }
    else if (laneArea.contains (e.getPosition()))
    {
        cursor = hoverMarker >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor;
    }
    setMouseCursor (cursor);

    if (waveArea.contains (e.getPosition()))
        setTooltip (tool == Tool::volume
                        ? T ("Volume mode: click on or near the orange line to add a point, drag points up/down, right-click a point to delete it. Click the Volume button again (or press Esc) to leave.",
                             "Volumentilstand: klik på eller tæt ved den orange linje for at tilføje et punkt, træk punkter op/ned, højreklik på et punkt for at slette det. Klik på Volumen-knappen igen (eller tryk Esc) for at forlade.")
                        : T ("Drag to select. Double-click between markers to select that part. Double-click the orange line to add a volume point.",
                             "Træk for at markere. Dobbeltklik mellem markører for at markere den del. Dobbeltklik på den orange linje for at tilføje et volumenpunkt."));
    else if (laneArea.contains (e.getPosition()))
    {
        if (hoverMarker >= 0)
            setTooltip (juce::String (hoverMarker + 1) + ": " + doc->markers[(size_t) hoverMarker].name);
        else
            setTooltip (T ("Marker line: click to add a marker, drag to move, right-click for options. Type the text in the Markers panel.",
                           "Markørlinje: klik for at tilføje en markør, træk for at flytte, højreklik for valg. Skriv teksten i Markører-panelet."));
    }
    else
        setTooltip ({});
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (doc == nullptr) return;
    if (onActivate) onActivate();
    drag = Drag::none;
    dragMoved = false;
    annotationUndoPushed = false;
    mouseDownPos = e.getPosition();
    const auto p = e.position;
    const auto sample = clampSample (sampleForX (p.x));

    // ---- marker lane
    if (laneArea.contains (e.getPosition()))
    {
        int idx = findMarkerAt (p);
        if (e.mods.isPopupMenu())
        {
            if (idx >= 0) showMarkerMenu (idx);
            return;
        }
        if (editsBlocked())
            return;
        if (idx >= 0)
        {
            // undo is pushed when the marker actually moves (a plain click changes nothing)
            drag = Drag::marker;
            dragIndex = idx;
            annotationUndoPushed = false;
            pendingUndoName = T ("Move marker", "Flyt markør");
        }
        else
        {
            doc->pushUndo (T ("Add marker", "Tilføj markør"), true);
            int newIdx = doc->addMarker ((double) sample / doc->sampleRate);
            doc->annotationsChanged();
            if (onMarkersChanged) onMarkersChanged (newIdx);
            repaint();
        }
        return;
    }

    // ---- ruler: move cursor / playhead
    if (rulerArea.contains (e.getPosition()) || e.getPosition().x < gutter)
    {
        drag = Drag::scrub;
        doc->selStart = doc->selEnd = doc->cursor = sample;
        if (onSelectionChanged) onSelectionChanged();
        repaint();
        return;
    }

    if (! waveArea.contains (e.getPosition()))
        return;

    // ---- envelope points
    int envIdx = findEnvelopePointAt (p);
    if (envIdx >= 0)
    {
        if (editsBlocked())
            return;
        if (e.mods.isPopupMenu())
        {
            doc->pushUndo (T ("Delete volume point", "Slet volumenpunkt"), true);
            doc->removeEnvelopePoint (envIdx);
            hoverEnvelope = -1;
            doc->annotationsChanged();
            if (onEnvelopeEdited) onEnvelopeEdited();
            repaint();
            return;
        }
        // undo is pushed when the point actually moves (a plain click changes nothing)
        drag = Drag::envelopePoint;
        dragIndex = envIdx;
        annotationUndoPushed = false;
        pendingUndoName = T ("Volume envelope", "Volumenkurve");
        return;
    }

    // ---- clip title bar: drag to move the clip
    int clipIdx = findClipBarAt (p);
    if (clipIdx >= 0 && ! e.mods.isPopupMenu())
    {
        auto& c = doc->clips[(size_t) clipIdx];
        drag = editsBlocked() ? Drag::none : Drag::clip;     // while recording a click only selects the clip
        dragIndex = editsBlocked() ? -1 : clipIdx;
        dragClipOffset = sample - c.start;
        clipUndoPushed = false;
        doc->selStart = c.start;
        doc->selEnd = c.end();
        doc->cursor = c.start;
        if (onSelectionChanged) onSelectionChanged();
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        if (onContextMenu) onContextMenu (sample);
        return;
    }

    if (tool == Tool::volume && e.mods.isLeftButtonDown() && isNearEnvelopeLine (p, 24.0f))
    {
        if (editsBlocked())
            return;
        doc->pushUndo (T ("Add volume point", "Tilføj volumenpunkt"), true);
        dragIndex = doc->addEnvelopePoint ((double) juce::jmin (sample, doc->getLength()) / doc->sampleRate, gainForY (p.y));
        drag = dragIndex >= 0 ? Drag::envelopePoint : Drag::none;
        annotationUndoPushed = true;                 // the drag continues the same undo step
        doc->annotationsChanged();
        if (onEnvelopeEdited) onEnvelopeEdited();
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
        return;

    // ---- selection
    if (doc->hasSelection() && std::abs (p.x - xForSample ((double) doc->selStart)) < 6)
    {
        drag = Drag::selStartEdge;
        return;
    }
    if (doc->hasSelection() && std::abs (p.x - xForSample ((double) doc->selEnd)) < 6)
    {
        drag = Drag::selEndEdge;
        return;
    }

    drag = Drag::newSelection;
    if (e.mods.isShiftDown())
    {
        dragAnchor = doc->hasSelection() ? (std::abs (sample - doc->selStart) > std::abs (sample - doc->selEnd) ? doc->selStart : doc->selEnd)
                                         : doc->cursor;
        dragMoved = true;
        mouseDrag (e);
    }
    else
    {
        dragAnchor = sample;
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (doc == nullptr) return;
    if (e.getDistanceFromDragStart() > 2) dragMoved = true;
    if (! dragMoved) return;

    const auto p = e.position;
    const auto len = doc->getLength();
    auto sample = juce::jmin (clampSample (sampleForX (p.x)), std::max<juce::int64> (len, 0));

    // auto-scroll when dragging outside
    if (drag == Drag::newSelection || drag == Drag::selStartEdge || drag == Drag::selEndEdge)
    {
        if (p.x > (float) waveArea.getRight()) scrollTo (viewStartSamples() + getSamplesPerPixel() * 12);
        if (p.x < (float) waveArea.getX())     scrollTo (viewStartSamples() - getSamplesPerPixel() * 12);
    }

    switch (drag)
    {
        case Drag::newSelection:
            doc->selStart = juce::jmin (dragAnchor, sample);
            doc->selEnd = juce::jmax (dragAnchor, sample);
            doc->cursor = doc->selStart;
            break;
        case Drag::selStartEdge:
            doc->selStart = juce::jmin (sample, doc->selEnd);
            if (sample > doc->selEnd) { doc->selStart = doc->selEnd; doc->selEnd = sample; drag = Drag::selEndEdge; }
            doc->cursor = doc->selStart;
            break;
        case Drag::selEndEdge:
            doc->selEnd = juce::jmax (sample, doc->selStart);
            if (sample < doc->selStart) { doc->selEnd = doc->selStart; doc->selStart = sample; drag = Drag::selStartEdge; }
            break;
        case Drag::envelopePoint:
            // the point may have disappeared (undo during the drag) and edits stop while recording
            if (editsBlocked() || ! juce::isPositiveAndBelow (dragIndex, (int) doc->envelope.size())) return;
            pushPendingAnnotationUndo();
            doc->moveEnvelopePoint (dragIndex, (double) sample / doc->sampleRate, gainForY (p.y));
            if (onEnvelopeEdited) onEnvelopeEdited();
            repaint();
            return;
        case Drag::marker:
            if (! editsBlocked() && juce::isPositiveAndBelow (dragIndex, (int) doc->markers.size()))
            {
                pushPendingAnnotationUndo();
                doc->markers[(size_t) dragIndex].time = juce::jlimit (0.0, doc->getLengthSeconds(), (double) sample / doc->sampleRate);
                if (onMarkersChanged) onMarkersChanged (-1);
            }
            repaint();
            return;
        case Drag::scrub:
            doc->selStart = doc->selEnd = doc->cursor = sample;
            break;
        case Drag::clip:
        {
            if (editsBlocked() || ! juce::isPositiveAndBelow (dragIndex, (int) doc->clips.size())) return;
            if (! clipUndoPushed)
            {
                doc->pushUndo (T ("Move clip", "Flyt klip"));
                clipUndoPushed = true;
            }
            const auto rawSample = (juce::int64) std::llround (sampleForX (p.x));
            doc->moveClip (dragIndex, rawSample - dragClipOffset);
            auto& c = doc->clips[(size_t) dragIndex];
            doc->selStart = c.start;
            doc->selEnd = c.end();
            doc->cursor = c.start;
            if (onClipDragOver) onClipDragOver (e.getScreenPosition());
            if (onSelectionChanged) onSelectionChanged();
            repaint();
            return;
        }
        case Drag::none:
        default:
            return;
    }

    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (doc == nullptr) return;

    if (drag == Drag::newSelection && ! dragMoved)
    {
        // the cursor may sit after the end of the audio (paste there pads with silence)
        auto sample = clampSample (sampleForX (e.position.x));
        doc->selStart = doc->selEnd = doc->cursor = sample;
        if (onSelectionChanged) onSelectionChanged();
    }
    else if (drag == Drag::marker)
    {
        // the marker may be gone if undo happened during the drag
        if (juce::isPositiveAndBelow (dragIndex, (int) doc->markers.size()))
        {
            // keep markers sorted but remember which one was dragged
            auto m = doc->markers[(size_t) dragIndex];
            int idx = dragIndex;
            if (annotationUndoPushed)
            {
                doc->sortMarkers();
                for (int i = 0; i < (int) doc->markers.size(); ++i)
                    if (doc->markers[(size_t) i].time == m.time && doc->markers[(size_t) i].name == m.name) idx = i;
                doc->annotationsChanged();          // only when something actually changed
            }
            if (onMarkersChanged) onMarkersChanged (annotationUndoPushed ? -1 : idx);
        }
    }
    else if (drag == Drag::envelopePoint)
    {
        if (annotationUndoPushed)
            doc->annotationsChanged();
    }
    else if (drag == Drag::clip)
    {
        const int idx = dragIndex;
        drag = Drag::none;
        dragIndex = -1;
        bool movedAway = dragMoved && onClipDropped && onClipDropped (idx, e.getScreenPosition());
        if (! movedAway && clipUndoPushed)
            doc->changed();
        if (onClipDragOver) onClipDragOver ({ -100000, -100000 });   // clears the drop highlight
        repaint();
        return;
    }

    drag = Drag::none;
    dragIndex = -1;
    repaint();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (doc == nullptr) return;
    const auto p = e.position;

    if (laneArea.contains (e.getPosition()))
    {
        int idx = findMarkerAt (p);
        if (idx >= 0 && onMarkersChanged)
            onMarkersChanged (idx);   // focuses the name field in the markers panel
        return;
    }

    if (! waveArea.contains (e.getPosition()))
        return;

    if (isNearEnvelopeLine (p) && findEnvelopePointAt (p) < 0 && ! editsBlocked())
    {
        doc->pushUndo (T ("Add volume point", "Tilføj volumenpunkt"), true);
        doc->addEnvelopePoint (sampleForX (p.x) / doc->sampleRate, doc->gainAt (sampleForX (p.x) / doc->sampleRate));
        doc->annotationsChanged();
        if (onEnvelopeEdited) onEnvelopeEdited();
        repaint();
        return;
    }

    // select the region between surrounding markers (or the whole file)
    const double t = sampleForX (p.x) / doc->sampleRate;
    double a = 0.0, b = doc->getLengthSeconds();
    for (auto& m : doc->markers)
    {
        if (m.time <= t) a = juce::jmax (a, m.time);
        if (m.time > t)  b = juce::jmin (b, m.time);
    }
    doc->selStart = doc->toSamples (a);
    doc->selEnd = juce::jmin (doc->getLength(), doc->toSamples (b));
    doc->cursor = doc->selStart;
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (doc == nullptr) return;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        auto factor = w.deltaY > 0 ? 1.25 : 0.8;
        setSamplesPerPixel (getSamplesPerPixel() / factor, sampleForX (e.position.x));
        return;
    }
    if (e.mods.isAltDown() && onTrackHeightWheel)
    {
        onTrackHeightWheel (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f);
        return;
    }
    const bool horizontal = e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY);
    if (! horizontal && wheelScrollsVertically && wheelScrollsVertically())
    {
        Component::mouseWheelMove (e, w);    // passes the event on to the track list viewport
        return;
    }
    auto delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    scrollTo (viewStartSamples() - delta * getVisibleSamples() * 0.15);
}

void WaveformView::showMarkerMenu (int index)
{
    juce::PopupMenu m;
    m.addItem (1, T ("Rename...", "Omdøb..."));
    m.addItem (2, T ("Select to next marker", "Markér til næste markør"));
    m.addItem (4, T ("Change colour...", "Skift farve..."));
    m.addItem (3, T ("Delete marker", "Slet markør"));
    juce::Component::SafePointer<WaveformView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, index] (int r)
    {
        if (safe == nullptr || safe->doc == nullptr || ! juce::isPositiveAndBelow (index, (int) safe->doc->markers.size()))
            return;
        auto* d = safe->doc;
        if (r == 1 && safe->onMarkersChanged) safe->onMarkersChanged (index);
        else if (r == 2)
        {
            d->selStart = d->toSamples (d->markers[(size_t) index].time);
            d->selEnd = index + 1 < (int) d->markers.size() ? d->toSamples (d->markers[(size_t) index + 1].time) : d->getLength();
            d->cursor = d->selStart;
            if (safe->onSelectionChanged) safe->onSelectionChanged();
        }
        else if ((r == 3 || r == 4) && safe->editsBlocked())
        {
            return;
        }
        else if (r == 4)
        {
            auto flagX = (int) safe->xForSample (d->markers[(size_t) index].time * d->sampleRate);
            auto area = juce::Rectangle<int> (flagX, safe->laneArea.getY(), 24, safe->laneArea.getHeight());
            // undo is pushed on the first real colour change, so a cancelled picker leaves no entry
            auto undoPushed = std::make_shared<bool> (false);
            showColourPicker (safe->localAreaToGlobal (area), d->markers[(size_t) index].colour, [safe, d, index, undoPushed] (juce::Colour c)
            {
                // the view may show another track by now, or the document may be gone
                if (safe == nullptr || safe->doc != d || safe->editsBlocked()
                    || ! juce::isPositiveAndBelow (index, (int) d->markers.size())) return;
                if (d->markers[(size_t) index].colour == c) return;
                if (! *undoPushed)
                {
                    d->pushUndo (T ("Marker colour", "Markørfarve"), true);
                    *undoPushed = true;
                }
                d->markers[(size_t) index].colour = c;
                d->annotationsChanged();
                if (safe->onMarkersChanged) safe->onMarkersChanged (-1);
                safe->repaint();
            });
        }
        else if (r == 3)
        {
            d->pushUndo (T ("Delete marker", "Slet markør"), true);
            d->markers.erase (d->markers.begin() + index);
            d->annotationsChanged();
            if (safe->onMarkersChanged) safe->onMarkersChanged (-1);
        }
        safe->repaint();
    });
}
