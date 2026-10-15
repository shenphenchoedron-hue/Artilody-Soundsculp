#include "Theme.h"

//==============================================================================
juce::Colour Theme::markerColour (int index)
{
    static const juce::uint32 colours[] = { 0xff3b82f6, 0xfff97316, 0xff14c8a0, 0xffa855f7,
                                            0xffef4444, 0xffeab308, 0xffec4899, 0xff22c55e,
                                            0xff06b6d4, 0xff8b5a2b, 0xff94a3b8, 0xffffffff };
    return juce::Colour (colours[(size_t) (std::abs (index) % numMarkerColours)]);
}

juce::Colour Theme::bandColour (int index)
{
    static const juce::uint32 colours[] = { 0xff3b9cff, 0xfff97316, 0xffa855f7, 0xff14c8a0 };
    return juce::Colour (colours[(size_t) (std::abs (index) % 4)]);
}

juce::Font Theme::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

float Theme::textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, t, 0, 0);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

//==============================================================================
static bool danishLanguage = false;

bool isDanish()                    { return danishLanguage; }
void setDanish (bool d)            { danishLanguage = d; }

juce::String T (const char* english, const char* danish)
{
    return danishLanguage ? juce::String::fromUTF8 (danish) : juce::String::fromUTF8 (english);
}

juce::String formatTime (double seconds)
{
    if (seconds < 0) seconds = 0;
    auto totalMs = (juce::int64) std::llround (seconds * 1000.0);
    auto ms  = (int) (totalMs % 1000);
    auto s   = (int) ((totalMs / 1000) % 60);
    auto m   = (int) (totalMs / 60000);
    return juce::String::formatted ("%02d:%02d.%03d", m, s, ms);
}

juce::String formatTimeShort (double seconds)
{
    if (seconds < 0) seconds = 0;
    auto total = (int) std::floor (seconds + 1e-6);
    return juce::String::formatted ("%d:%02d", total / 60, total % 60);
}

//==============================================================================
// Icons are drawn in a 24x24 design grid and scaled into the target area.
void Icons::draw (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour)
{
    using namespace juce;
    auto size = jmin (area.getWidth(), area.getHeight());
    auto r = area.withSizeKeepingCentre (size, size);
    auto tr = AffineTransform::scale (size / 24.0f).translated (r.getX(), r.getY());
    // The transform is applied before stroking, so the width is in screen pixels.
    PathStrokeType stroke (jmax (1.4f, size / 11.0f), PathStrokeType::curved, PathStrokeType::rounded);

    Path p;     // stroked
    Path f;     // filled

    auto line = [&p] (float x1, float y1, float x2, float y2) { p.startNewSubPath (x1, y1); p.lineTo (x2, y2); };

    if (name == "play")            { f.addTriangle (7, 4.5f, 7, 19.5f, 19.5f, 12); }
    else if (name == "stop")       { f.addRoundedRectangle (6.0f, 6.0f, 12.0f, 12.0f, 1.5f); }
    else if (name == "record")     { f.addEllipse (5, 5, 14, 14); }
    else if (name == "home")       { f.addRectangle (5.0f, 5.0f, 2.6f, 14.0f); f.addTriangle (19, 5, 19, 19, 8.5f, 12); }
    else if (name == "loop" || name == "looptool")
    {
        p.addCentredArc (12, 12, 7, 7, 0, 0.6f, MathConstants<float>::twoPi - 0.2f, true);
        f.addTriangle (12.5f, 2.0f, 12.5f, 9.0f, 17.5f, 5.5f);
        f.applyTransform (AffineTransform::translation (-0.5f, 0.0f));
    }
    else if (name == "select")     { f.startNewSubPath (6, 3); f.lineTo (6, 19); f.lineTo (10, 15); f.lineTo (13, 21);
                                     f.lineTo (15.5f, 20); f.lineTo (12.5f, 14); f.lineTo (18, 14); f.closeSubPath(); }
    else if (name == "volume")     { p.startNewSubPath (3, 17); p.lineTo (8, 12); p.lineTo (13, 15); p.lineTo (21, 6);
                                     f.addEllipse (6, 10, 4, 4); f.addEllipse (11, 13, 4, 4); f.addEllipse (19, 4, 4, 4); }
    else if (name == "trim" || name == "cut")
    {
        p.addEllipse (3.5f, 14.5f, 5.5f, 5.5f); p.addEllipse (3.5f, 4.0f, 5.5f, 5.5f);
        line (8.3f, 8.3f, 20, 18.5f); line (8.3f, 15.7f, 20, 5.5f);
    }
    else if (name == "copy")       { p.addRoundedRectangle (8.0f, 8.0f, 12.0f, 12.0f, 2.0f);
                                     p.startNewSubPath (16, 5); p.lineTo (16, 4); p.lineTo (4, 4); p.lineTo (4, 16); p.lineTo (5, 16); }
    else if (name == "paste")      { p.addRoundedRectangle (5.0f, 5.0f, 14.0f, 16.0f, 2.0f);
                                     f.addRoundedRectangle (8.5f, 3.0f, 7.0f, 4.0f, 1.2f); line (8.5f, 12, 15.5f, 12); line (8.5f, 16, 13.5f, 16); }
    else if (name == "fadein")     { line (3, 20, 21, 20); f.startNewSubPath (4, 19); f.lineTo (20, 5); f.lineTo (20, 19); f.closeSubPath();
                                     for (int i = 0; i < 4; ++i) { /* bars rendered by fill */ } }
    else if (name == "fadeout")    { line (3, 20, 21, 20); f.startNewSubPath (4, 5); f.lineTo (20, 19); f.lineTo (4, 19); f.closeSubPath(); }
    else if (name == "normalize")
    {
        const float h[] = { 4, 9, 14, 9, 16, 11, 6 };
        for (int i = 0; i < 7; ++i) { auto x = 3.5f + i * 2.85f; line (x, 12 - h[i] / 2, x, 12 + h[i] / 2); }
    }
    else if (name == "silence")
    {
        line (3, 12, 9, 12); line (15, 12, 21, 12);
        line (12, 4, 12, 20); line (9.5f, 6, 9.5f, 18); line (14.5f, 6, 14.5f, 18);
        p.applyTransform (AffineTransform::rotation (0.35f, 12, 12));
    }
    else if (name == "reverse")    { f.addTriangle (11, 6, 11, 18, 3, 12); f.addTriangle (20, 6, 20, 18, 12, 12); }
    else if (name == "pitch")
    {
        p.addEllipse (4, 15, 6, 4.5f); line (10, 17, 10, 4); p.startNewSubPath (10, 4); p.quadraticTo (14, 4, 15, 8);
        f.addTriangle (18, 4, 15.5f, 8, 20.5f, 8); f.addTriangle (18, 20, 15.5f, 16, 20.5f, 16); line (18, 8, 18, 16);
    }
    else if (name == "eq")
    {
        p.startNewSubPath (2, 12); p.lineTo (5, 12); p.lineTo (7, 6); p.lineTo (9.5f, 18);
        p.lineTo (12, 4); p.lineTo (14.5f, 20); p.lineTo (17, 9); p.lineTo (19, 12); p.lineTo (22, 12);
    }
    else if (name == "noise" || name == "sparkle")
    {
        auto star = [&f] (float cx, float cy, float s)
        {
            f.startNewSubPath (cx, cy - s);
            f.quadraticTo (cx + s * 0.15f, cy - s * 0.15f, cx + s, cy);
            f.quadraticTo (cx + s * 0.15f, cy + s * 0.15f, cx, cy + s);
            f.quadraticTo (cx - s * 0.15f, cy + s * 0.15f, cx - s, cy);
            f.quadraticTo (cx - s * 0.15f, cy - s * 0.15f, cx, cy - s);
            f.closeSubPath();
        };
        star (10, 13, 8); star (18.5f, 5.5f, 3.5f); star (19, 18, 2.5f);
    }
    else if (name == "undo")       { p.startNewSubPath (8, 9); p.lineTo (15, 9); p.quadraticTo (20, 9, 20, 14); p.quadraticTo (20, 19, 15, 19); p.lineTo (9, 19);
                                     f.addTriangle (3.5f, 9, 9, 4.5f, 9, 13.5f); }
    else if (name == "redo")       { p.startNewSubPath (16, 9); p.lineTo (9, 9); p.quadraticTo (4, 9, 4, 14); p.quadraticTo (4, 19, 9, 19); p.lineTo (15, 19);
                                     f.addTriangle (20.5f, 9, 15, 4.5f, 15, 13.5f); }
    else if (name == "zoomin" || name == "zoomout")
    {
        p.addEllipse (4, 4, 12, 12); line (14.5f, 14.5f, 20, 20); line (7, 10, 13, 10);
        if (name == "zoomin") line (10, 7, 10, 13);
    }
    else if (name == "fit")
    {
        line (3, 12, 21, 12);
        p.startNewSubPath (6.5f, 8.5f); p.lineTo (3, 12); p.lineTo (6.5f, 15.5f);
        p.startNewSubPath (17.5f, 8.5f); p.lineTo (21, 12); p.lineTo (17.5f, 15.5f);
        line (3, 4, 3, 20); line (21, 4, 21, 20);
        p.applyTransform (AffineTransform::scale (0.9f, 1.0f, 12, 12));
    }
    else if (name == "zoomsel")
    {
        p.addEllipse (6, 6, 11, 11); line (15.5f, 15.5f, 21, 21);
        p.startNewSubPath (5, 3); p.lineTo (3, 3); p.lineTo (3, 9);
        p.startNewSubPath (13, 3); p.lineTo (15, 3);
        f.addRectangle (9.0f, 9.0f, 5.0f, 5.0f);
    }
    else if (name == "taller" || name == "shorter")
    {
        line (4, 4, 20, 4); line (4, 20, 20, 20); line (12, 7, 12, 17);
        if (name == "taller") { f.addTriangle (12, 6.5f, 8.5f, 10.5f, 15.5f, 10.5f); f.addTriangle (12, 17.5f, 8.5f, 13.5f, 15.5f, 13.5f); }
        else                  { f.addTriangle (12, 11, 8.5f, 7, 15.5f, 7); f.addTriangle (12, 13, 8.5f, 17, 15.5f, 17); }
    }
    else if (name == "split")
    {
        p.addRoundedRectangle (2.0f, 7.0f, 8.0f, 10.0f, 1.5f);
        p.addRoundedRectangle (14.0f, 7.0f, 8.0f, 10.0f, 1.5f);
        line (12, 3, 12, 5.5f); line (12, 9, 12, 11); line (12, 13, 12, 15); line (12, 18.5f, 12, 21);
    }
    else if (name == "join")
    {
        p.addRoundedRectangle (2.0f, 7.0f, 20.0f, 10.0f, 1.5f);
        line (8, 12, 11, 12); line (13, 12, 16, 12);
        f.addTriangle (11.5f, 12, 9.5f, 10, 9.5f, 14); f.addTriangle (12.5f, 12, 14.5f, 10, 14.5f, 14);
    }
    else if (name == "merge")
    {
        p.startNewSubPath (4, 4); p.lineTo (4, 8); p.quadraticTo (4, 12, 12, 13);
        p.startNewSubPath (20, 4); p.lineTo (20, 8); p.quadraticTo (20, 12, 12, 13);
        line (12, 13, 12, 18);
        f.addTriangle (12, 22, 8, 17, 16, 17);
    }
    else if (name == "plus")       { line (12, 4.5f, 12, 19.5f); line (4.5f, 12, 19.5f, 12); }
    else if (name == "close")      { line (6, 6, 18, 18); line (18, 6, 6, 18); }
    else if (name == "export")     { p.startNewSubPath (8, 9); p.lineTo (5, 9); p.lineTo (5, 20); p.lineTo (19, 20); p.lineTo (19, 9); p.lineTo (16, 9);
                                     line (12, 3, 12, 14); p.startNewSubPath (8, 7); p.lineTo (12, 3); p.lineTo (16, 7); }
    else if (name == "open")       { p.startNewSubPath (3, 19); p.lineTo (3, 5); p.lineTo (9, 5); p.lineTo (11, 7.5f); p.lineTo (20, 7.5f); p.lineTo (20, 10);
                                     p.startNewSubPath (3, 19); p.lineTo (6, 11); p.lineTo (22, 11); p.lineTo (19, 19); p.closeSubPath(); }
    else if (name == "settings")
    {
        for (int i = 0; i < 8; ++i)
        {
            auto a = i * MathConstants<float>::pi / 4.0f;
            Path tooth; tooth.addRoundedRectangle (10.5f, 1.8f, 3.0f, 5.0f, 1.0f);
            tooth.applyTransform (AffineTransform::rotation (a, 12, 12));
            f.addPath (tooth);
        }
        p.addEllipse (5.5f, 5.5f, 13, 13);
        p.addEllipse (9.3f, 9.3f, 5.4f, 5.4f);
    }
    else if (name == "more")       { f.addEllipse (4, 10.5f, 3, 3); f.addEllipse (10.5f, 10.5f, 3, 3); f.addEllipse (17, 10.5f, 3, 3); }
    else if (name == "headphones") { p.startNewSubPath (4, 17); p.lineTo (4, 13); p.addCentredArc (12, 13, 8, 8, 0, -MathConstants<float>::halfPi, MathConstants<float>::halfPi);
                                     p.lineTo (20, 17); f.addRoundedRectangle (3.0f, 13.5f, 4.5f, 7.0f, 1.5f); f.addRoundedRectangle (16.5f, 13.5f, 4.5f, 7.0f, 1.5f); }
    else if (name == "power")      { p.addCentredArc (12, 13, 7, 7, 0, 0.7f, MathConstants<float>::twoPi - 0.7f, true); line (12, 3.5f, 12, 11); }
    else if (name == "learn")      { p.addEllipse (3, 3, 18, 18); line (12, 7, 12, 13); f.addEllipse (10.8f, 15.5f, 2.4f, 2.4f); }
    else if (name == "marker")     { f.addTriangle (5, 4, 19, 4, 12, 13); line (12, 13, 12, 21); }
    else if (name == "trash")      { line (4, 6, 20, 6); p.startNewSubPath (6, 6); p.lineTo (7, 20); p.lineTo (17, 20); p.lineTo (18, 6);
                                     p.startNewSubPath (9, 6); p.lineTo (9, 3.5f); p.lineTo (15, 3.5f); p.lineTo (15, 6); }

    g.setColour (colour);
    if (! f.isEmpty())  g.fillPath (f, tr);
    if (! p.isEmpty())  g.strokePath (p, stroke, tr);
}

//==============================================================================
IconButton::IconButton (const juce::String& iconName, const juce::String& text)
    : juce::Button (text), icon (iconName), label (text)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::setColours (juce::Colour background, juce::Colour iconColour)
{
    bg = background; fg = iconColour; repaint();
}

void IconButton::setLitColours (juce::Colour litBackground, juce::Colour litIcon)
{
    litBg = litBackground; litFg = litIcon; repaint();
}

int IconButton::getIdealWidth (int height) const
{
    if (! showText || label.isEmpty())
        return height;

    juce::GlyphArrangement ga;
    ga.addLineOfText (Theme::font (13.5f), label, 0, 0);
    // padding + icon + gap + text + padding (matches paintButton)
    return (int) (height * 0.4f + height * 0.6f * 0.85f + 7.0f + ga.getBoundingBox (0, -1, true).getWidth() + 6.0f);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = getToggleState();
    auto back = lit ? litBg : bg;
    auto ink  = lit ? litFg : fg;

    if (! isEnabled())
        ink = ink.withMultipliedAlpha (0.35f);

    if (highlighted && ! lit) back = back.overlaidWith (juce::Colours::black.withAlpha (0.04f));
    if (down)                 back = back.darker (0.08f);

    const float radius = round ? b.getHeight() * 0.5f : 8.0f;

    if (lit && back != Theme::accentSoft)
    {
        // glow for lit transport buttons
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (back.withAlpha (0.12f * (float) (4 - i)));
            g.fillRoundedRectangle (b.expanded ((float) i * 1.0f), radius + (float) i);
        }
    }

    g.setColour (back);
    g.fillRoundedRectangle (b, radius);

    if (! lit && back == Theme::panel)
    {
        g.setColour (Theme::border);
        g.drawRoundedRectangle (b, radius, 1.0f);
    }
    else if (lit && back == Theme::accentSoft)
    {
        g.setColour (Theme::accent.withAlpha (0.25f));
        g.drawRoundedRectangle (b, radius, 1.0f);
    }

    auto content = b.reduced (b.getHeight() * 0.2f);
    if (showText && label.isNotEmpty())
    {
        auto iconArea = content.removeFromLeft (content.getHeight() * 0.85f);
        Icons::draw (g, icon, iconArea, ink);
        content.removeFromLeft (7.0f);
        g.setColour (ink);
        g.setFont (Theme::font (13.5f));
        g.drawText (label, content, juce::Justification::centredLeft, false);
    }
    else
    {
        Icons::draw (g, icon, content, ink);
    }
}

//==============================================================================
SoundsculpLookAndFeel::SoundsculpLookAndFeel()
{
    using namespace juce;
    setColour (ResizableWindow::backgroundColourId, Theme::background);
    setColour (Label::textColourId, Theme::text);
    setColour (ComboBox::backgroundColourId, Theme::panel);
    setColour (ComboBox::textColourId, Theme::text);
    setColour (ComboBox::outlineColourId, Theme::border);
    setColour (ComboBox::arrowColourId, Theme::textDim);
    setColour (PopupMenu::backgroundColourId, Theme::panel);
    setColour (PopupMenu::textColourId, Theme::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::accentSoft);
    setColour (PopupMenu::highlightedTextColourId, Theme::text);
    setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    setColour (TextEditor::textColourId, Theme::text);
    setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    setColour (TextEditor::focusedOutlineColourId, Theme::accent.withAlpha (0.5f));
    setColour (TextEditor::highlightColourId, Theme::accentSoft);
    setColour (CaretComponent::caretColourId, Theme::accent);
    setColour (TextButton::buttonColourId, Theme::panel);
    setColour (TextButton::textColourOffId, Theme::text);
    setColour (TextButton::buttonOnColourId, Theme::accent);
    setColour (TextButton::textColourOnId, Colours::white);
    setColour (Slider::textBoxTextColourId, Theme::text);
    setColour (Slider::textBoxOutlineColourId, Theme::border);
    setColour (Slider::textBoxBackgroundColourId, Theme::panel);
    setColour (Slider::trackColourId, Theme::accent);
    setColour (Slider::thumbColourId, Theme::accent);
    setColour (AlertWindow::backgroundColourId, Theme::panel);
    setColour (AlertWindow::textColourId, Theme::text);
    setColour (AlertWindow::outlineColourId, Theme::border);
    setColour (ToggleButton::textColourId, Theme::text);
    setColour (ToggleButton::tickColourId, Theme::accent);
    setColour (ToggleButton::tickDisabledColourId, Theme::textDim);
    setColour (ListBox::backgroundColourId, Theme::panel);
    setColour (ScrollBar::thumbColourId, Theme::textDim.withAlpha (0.5f));
    setColour (TooltipWindow::backgroundColourId, Theme::text);
    setColour (TooltipWindow::textColourId, Colours::white);
    setColour (TooltipWindow::outlineColourId, Colours::transparentBlack);
    setColour (DocumentWindow::backgroundColourId, Theme::background);
}

void SoundsculpLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                              juce::Slider::SliderStyle style, juce::Slider& slider)
{
    using namespace juce;
    if (style == Slider::LinearBar || style == Slider::LinearBarVertical)
    {
        auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.5f);
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (b, 6.0f, 1.0f);
        return;
    }

    auto track = Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f, (float) w, 6.0f);
    g.setColour (Theme::border);
    g.fillRoundedRectangle (track, 3.0f);

    auto filled = track.withRight (pos);
    ColourGradient grad (Theme::waveB, track.getX(), 0, Theme::accent, track.getRight(), 0, false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (filled, 3.0f);

    auto thumbCol = slider.findColour (Slider::thumbColourId);
    g.setColour (Colours::white);
    g.fillEllipse (Rectangle<float> (16.0f, 16.0f).withCentre ({ pos, track.getCentreY() }));
    g.setColour (thumbCol);
    g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, track.getCentreY() }));
}

void SoundsculpLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace juce;
    auto b = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (b, 6.0f, 1.0f);

    Path arrow;
    auto cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    arrow.startNewSubPath (cx - 4, cy - 2);
    arrow.lineTo (cx, cy + 2);
    arrow.lineTo (cx + 4, cy - 2);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.strokePath (arrow, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

juce::Font SoundsculpLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Theme::font (juce::jmin (15.0f, (float) box.getHeight() * 0.5f));
}

juce::Font SoundsculpLookAndFeel::getPopupMenuFont()                       { return Theme::font (15.0f); }
juce::Font SoundsculpLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return Theme::font (juce::jmin (15.0f, (float) h * 0.5f)); }
juce::Font SoundsculpLookAndFeel::getLabelFont (juce::Label& l)            { return l.getFont(); }

void SoundsculpLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour, bool hl, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : colour;
    if (hl)   c = c.overlaidWith (juce::Colours::black.withAlpha (0.05f));
    if (down) c = c.darker (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 7.0f);
    if (c == Theme::panel || c.getBrightness() > 0.95f)
    {
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }
}

void SoundsculpLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool down)
{
    LookAndFeel_V4::drawToggleButton (g, b, hl, down);
}

//==============================================================================
void SwitchButton::paintButton (juce::Graphics& g, bool, bool)
{
    auto b = getLocalBounds().toFloat().withSizeKeepingCentre (38.0f, 22.0f);
    const bool on = getToggleState();
    g.setColour (on ? Theme::accent : Theme::border.darker (0.15f));
    g.fillRoundedRectangle (b, 11.0f);
    auto knob = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ on ? b.getRight() - 11.0f : b.getX() + 11.0f, b.getCentreY() });
    g.setColour (juce::Colours::white);
    g.fillEllipse (knob);
}

//==============================================================================
namespace
{
    class PaletteColourSelector : public juce::ColourSelector,
                                  private juce::ChangeListener
    {
    public:
        PaletteColourSelector (juce::Colour initial, std::function<void (juce::Colour)> cb)
            : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showColourAtTop),
              callback (std::move (cb))
        {
            setCurrentColour (initial, juce::dontSendNotification);
            addChangeListener (this);
            setSize (300, 330);
        }

        int getNumSwatches() const override                       { return Theme::numMarkerColours; }
        juce::Colour getSwatchColour (int index) const override   { return Theme::markerColour (index); }
        void setSwatchColour (int, const juce::Colour&) override  {}

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (callback) callback (getCurrentColour().withAlpha (1.0f));
        }
        std::function<void (juce::Colour)> callback;
    };
}

void showColourPicker (juce::Rectangle<int> screenArea, juce::Colour initial, std::function<void (juce::Colour)> onChange)
{
    auto picker = std::make_unique<PaletteColourSelector> (initial, std::move (onChange));
    juce::CallOutBox::launchAsynchronously (std::move (picker), screenArea, nullptr);
}
