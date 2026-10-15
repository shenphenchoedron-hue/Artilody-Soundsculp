#include "HeaderBar.h"

//==============================================================================
static void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Stylised gradient "A" with a waveform, followed by the product name.
    auto iconArea = area.removeFromLeft (area.getHeight()).reduced (2.0f);
    auto s = iconArea.getHeight() / 24.0f;
    auto x0 = iconArea.getX(), y0 = iconArea.getY();
    auto pt = [&] (float x, float y) { return juce::Point<float> (x0 + x * s, y0 + y * s); };

    juce::ColourGradient grad (juce::Colour (0xffff8c28), pt (2, 20).x, pt (2, 20).y,
                               juce::Colour (0xff00c8e6), pt (22, 4).x, pt (22, 4).y, false);
    grad.addColour (0.45, juce::Colour (0xffd63ca0));
    grad.addColour (0.75, juce::Colour (0xff7c4dff));
    g.setGradientFill (grad);

    juce::Path a;
    a.startNewSubPath (pt (3.5f, 21));
    a.lineTo (pt (12, 2.5f));
    a.lineTo (pt (20.5f, 21));
    g.strokePath (a, juce::PathStrokeType (2.6f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float h[] = { 1.5f, 3.5f, 5.5f, 3.0f, 6.0f, 4.0f, 2.0f };
    for (int i = 0; i < 7; ++i)
    {
        auto x = 7.6f + (float) i * 1.45f;
        g.fillRoundedRectangle (juce::Rectangle<float> (pt (x - 0.45f, 14.5f - h[i] * 0.5f), pt (x + 0.45f, 14.5f + h[i] * 0.5f)), 0.4f * s);
    }

    area.removeFromLeft (10.0f);
    // shrink the font until the full name fits
    float fh = area.getHeight() * 0.62f;
    while (fh > 10.0f && Theme::textWidth (Theme::font (fh, true), "Artilody ") + Theme::textWidth (Theme::font (fh), "Soundsculp") > area.getWidth())
        fh -= 0.5f;
    g.setColour (Theme::text);
    g.setFont (Theme::font (fh, true));
    g.drawText ("Artilody", area, juce::Justification::centredLeft);
    auto w = Theme::textWidth (Theme::font (fh, true), "Artilody ");
    g.setFont (Theme::font (fh));
    g.drawText ("Soundsculp", area.withTrimmedLeft (w), juce::Justification::centredLeft);
}

//==============================================================================
HeaderBar::HeaderBar()
{
    auto setupTransport = [this] (IconButton& b, juce::Colour c, const juce::String& tip)
    {
        b.setColours (c.withAlpha (0.14f).overlaidWith (juce::Colours::white.withAlpha (0.0f)), c);
        b.setLitColours (c, juce::Colours::white);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    setupTransport (recordButton, Theme::record, T ("Record (R)", "Optag (R)"));
    setupTransport (homeButton, Theme::home, T ("Go to start (Home)", "Gå til start (Home)"));
    setupTransport (playButton, Theme::play, T ("Play (Space)", "Afspil (Mellemrum)"));
    setupTransport (stopButton, Theme::stop, T ("Stop (Space)", "Stop (Mellemrum)"));
    setupTransport (loopButton, Theme::loop, T ("Loop selection on/off (L)", "Loop markering til/fra (L)"));
    recordButton.setRound (true);
    loopButton.setClickingTogglesState (true);

    trackBox.setTooltip (T ("Choose the track to edit", "Vælg sporet der skal redigeres"));
    trackBox.setTextWhenNothingSelected (T ("No tracks", "Ingen spor"));
    addAndMakeVisible (trackBox);

    for (auto* b : { &undoButton, &redoButton, &zoomOutButton, &zoomInButton, &addButton, &settingsButton })
    {
        b->setColours (Theme::panel, Theme::text);
        addAndMakeVisible (b);
    }
    undoButton.setTooltip (T ("Undo (Ctrl+Z)", "Fortryd (Ctrl+Z)"));
    redoButton.setTooltip (T ("Redo (Ctrl+Y)", "Gentag (Ctrl+Y)"));
    zoomOutButton.setTooltip (T ("Zoom out (-)", "Zoom ud (-)"));
    zoomInButton.setTooltip (T ("Zoom in (+)", "Zoom ind (+)"));
    addButton.setTooltip (T ("Tracks: new empty track, open files, insert file, merge...",
                             "Spor: nyt tomt spor, åbn filer, indsæt fil, flet..."));
    settingsButton.setTooltip (T ("Settings", "Indstillinger"));

    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.setRange (0.0, 1.0);
    zoomSlider.setTooltip (T ("Zoom", "Zoom"));
    addAndMakeVisible (zoomSlider);

    exportButton.setColours (Theme::accent, juce::Colours::white);
    exportButton.setTooltip (T ("Export track, selection or all tracks mixed (Ctrl+E)",
                                "Eksportér spor, markering eller alle spor mixet (Ctrl+E)"));
    addAndMakeVisible (exportButton);
}

void HeaderBar::setSelectionInfo (const juce::String& sel, const juce::String& len)
{
    if (sel != selectionText || len != lengthText)
    {
        selectionText = sel;
        lengthText = len;
        repaint (infoArea);
    }
}

void HeaderBar::paint (juce::Graphics& g)
{
    drawLogo (g, logoArea.toFloat());

    auto drawGroup = [&g] (juce::Rectangle<int> r)
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    };
    drawGroup (transportGroup);
    drawGroup (editGroup);
    drawGroup (zoomGroup);
    drawGroup (infoArea);

    auto info = infoArea.reduced (12, 6);
    auto lenArea = info.removeFromRight (juce::jmin (90, info.getWidth() / 3));
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f));
    g.drawText (T ("Selection", "Markering"), info.removeFromTop (16), juce::Justification::centredLeft);
    g.drawText (T ("Length", "Længde"), lenArea.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (Theme::text);
    g.setFont (Theme::font (14.5f));
    g.drawText (selectionText, info, juce::Justification::centredLeft, true);
    g.drawText (lengthText, lenArea, juce::Justification::centredLeft, true);
    g.setColour (Theme::border);
    g.drawVerticalLine (lenArea.getX() - 8, (float) infoArea.getY() + 8, (float) infoArea.getBottom() - 8);
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    const int h = r.getHeight();
    const int btn = h - 16;
    const bool narrow = getWidth() < 1500;

    logoArea = r.removeFromLeft (narrow ? 220 : 290).withSizeKeepingCentre (narrow ? 220 : 290, juce::jmin (h, 46));
    r.removeFromLeft (8);

    transportGroup = r.removeFromLeft (5 * (btn + 6) + 10);
    {
        auto t = transportGroup.reduced (8, 8);
        for (auto* b : { &recordButton, &homeButton, &playButton, &stopButton, &loopButton })
        {
            b->setBounds (t.removeFromLeft (btn).withSizeKeepingCentre (btn, btn));
            t.removeFromLeft (6);
        }
    }
    r.removeFromLeft (10);

    settingsButton.setBounds (r.removeFromRight (h).reduced (4));
    r.removeFromRight (8);
    exportButton.setBounds (r.removeFromRight (narrow ? 104 : 124).reduced (0, 2));
    r.removeFromRight (10);
    infoArea = r.removeFromRight (narrow ? 220 : 260);
    r.removeFromRight (10);

    zoomGroup = r.removeFromRight (narrow ? 2 * btn + 120 : 3 * btn + 140);
    {
        auto z = zoomGroup.reduced (8, 8);
        zoomOutButton.setBounds (z.removeFromLeft (btn));
        z.removeFromLeft (4);
        zoomInButton.setBounds (z.removeFromLeft (btn));
        z.removeFromLeft (6);
        addButton.setVisible (! narrow);
        if (! narrow) addButton.setBounds (z.removeFromRight (btn));
        zoomSlider.setBounds (z.reduced (4, 0));
    }
    r.removeFromRight (10);

    editGroup = r.removeFromRight (2 * btn + 22);
    {
        auto e = editGroup.reduced (8, 8);
        undoButton.setBounds (e.removeFromLeft (btn));
        e.removeFromLeft (6);
        redoButton.setBounds (e.removeFromLeft (btn));
    }
    r.removeFromRight (10);
    trackBox.setBounds (r.withSizeKeepingCentre (r.getWidth(), btn + 6));
}

//==============================================================================
ToolBar::ToolBar()
{
    struct Def { const char* id; const char* icon; juce::String name; juce::String tip; bool right; bool toggle; };
    const Def defs[] = {
        { "select",    "select",    T ("Select", "Markér"),        T ("Select with the mouse", "Markér med musen"), false, true },
        { "volume",    "volume",    T ("Volume", "Volumen"),       T ("Draw volume points on the orange volume line", "Tegn volumenpunkter på den orange volumenlinje"), false, true },
        { "trim",      "trim",      T ("Trim", "Trim"),            T ("Keep only the selection (Ctrl+T)", "Behold kun markeringen (Ctrl+T)"), false, false },
        { "cut",       "cut",       T ("Cut", "Klip"),             T ("Cut selection (Ctrl+X)", "Klip markering (Ctrl+X)"), false, false },
        { "copy",      "copy",      T ("Copy", "Kopiér"),          T ("Copy selection (Ctrl+C)", "Kopiér markering (Ctrl+C)"), false, false },
        { "paste",     "paste",     T ("Paste", "Indsæt"),         T ("Paste at cursor / replace selection (Ctrl+V)", "Indsæt ved positionen / erstat markering (Ctrl+V)"), false, false },
        { "fadein",    "fadein",    T ("Fade In", "Fade ind"),     T ("Fade in the selection", "Fade markeringen ind"), false, false },
        { "fadeout",   "fadeout",   T ("Fade Out", "Fade ud"),     T ("Fade out the selection", "Fade markeringen ud"), false, false },
        { "normalize", "normalize", T ("Normalize", "Normalisér"), T ("Normalize to -0.1 dB peak", "Normalisér til -0,1 dB peak"), false, false },
        { "silence",   "silence",   T ("Silence Trim", "Stilhedstrim"), T ("Remove silence at start and end", "Fjern stilhed i start og slut"), false, false },
        { "looptool",  "looptool",  T ("Loop", "Loop"),            T ("Repeat the selection a number of times", "Gentag markeringen et antal gange"), false, false },
        { "reverse",   "reverse",   T ("Reverse", "Baglæns"),      T ("Reverse the selection", "Vend markeringen baglæns"), false, false },
        { "pitch",     "pitch",     T ("Pitch", "Tone"),           T ("Raise or lower the pitch without changing the speed", "Hæv eller sænk tonen uden at ændre hastigheden"), false, false },
        { "eq",        "eq",        T ("EQ", "EQ"),                T ("Show/hide the parametric EQ", "Vis/skjul den parametriske EQ"), true, true },
        { "noise",     "noise",     T ("Noise Remover", "Støjfjerner"), T ("Show/hide the noise remover", "Vis/skjul støjfjerneren"), true, true },
    };

    for (auto& d : defs)
    {
        Entry e;
        e.id = d.id;
        e.rightAligned = d.right;
        e.button = std::make_unique<IconButton> (d.icon, d.name);
        e.button->setTooltip (d.tip);
        e.button->setColours (Theme::panel, Theme::text);
        e.button->setLitColours (Theme::accentSoft, Theme::accent);
        if (d.toggle) e.button->setToggleState (false, juce::dontSendNotification);
        auto id = e.id;
        e.button->onClick = [this, id] { if (onTool) onTool (id); };
        addAndMakeVisible (*e.button);
        entries.push_back (std::move (e));
    }
}

IconButton* ToolBar::get (const juce::String& id)
{
    for (auto& e : entries)
        if (e.id == id) return e.button.get();
    return nullptr;
}

void ToolBar::paint (juce::Graphics&) {}

void ToolBar::resized()
{
    auto r = getLocalBounds().reduced (16, 6);
    const int h = r.getHeight();

    // Decide which buttons can show their text: try all, then drop text from the
    // least important ones, finally show icons only.
    auto iconOnlyAlways = [] (const juce::String& id) { return id == "cut" || id == "copy" || id == "paste"; };
    auto measure = [&]
    {
        int total = 0;
        for (auto& e : entries) total += e.button->getIdealWidth (h) + 8;
        return total + 30;
    };
    for (auto& e : entries) e.button->setShowText (! iconOnlyAlways (e.id));
    const char* dropOrder[] = { "noise", "eq", "select", "volume", "looptool", "pitch", "reverse", "trim" };
    for (auto* id : dropOrder)
    {
        if (measure() <= r.getWidth()) break;
        get (id)->setShowText (false);
    }
    if (measure() > r.getWidth())
        for (auto& e : entries) e.button->setShowText (false);

    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        if (it->rightAligned)
        {
            it->button->setBounds (r.removeFromRight (it->button->getIdealWidth (h)));
            r.removeFromRight (8);
        }

    for (auto& e : entries)
        if (! e.rightAligned)
        {
            e.button->setBounds (r.removeFromLeft (e.button->getIdealWidth (h)));
            r.removeFromLeft (e.id == "volume" || e.id == "paste" ? 18 : 8);
        }
}

//==============================================================================
StatusBar::StatusBar()
{
    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.setRange (0.0, 1.0);
    addAndMakeVisible (zoomSlider);

    for (auto* b : { &zoomOutButton, &zoomInButton })
    {
        b->setColours (juce::Colours::transparentBlack, Theme::text);
        addAndMakeVisible (b);
    }

    zoomBox.setEditableText (false);
    for (auto p : { 100, 200, 400, 800, 1600, 3200 })
        zoomBox.addItem (juce::String (p) + "%", p);
    addAndMakeVisible (zoomBox);

    mixToggle.setButtonText (T ("Play all tracks", "Afspil alle spor"));
    mixToggle.setTooltip (T ("Play all tracks mixed together instead of only the current track",
                             "Afspil alle spor mixet sammen i stedet for kun det aktuelle spor"));
    addAndMakeVisible (mixToggle);
}

void StatusBar::setInfo (const juce::String& f, const juce::String& c, const juce::String& s, const juce::String& sl,
                         const juce::String& msg)
{
    if (f == formatText && c == cursorText && s == selectionText && sl == selLengthText && msg == messageText)
        return;
    formatText = f; cursorText = c; selectionText = s; selLengthText = sl; messageText = msg;
    repaint();
}

void StatusBar::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds());
    g.setColour (Theme::border);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto r = getLocalBounds().reduced (16, 0);
    g.setFont (Theme::font (13.5f));
    g.setColour (Theme::text);
    g.drawText (formatText, r.removeFromLeft (230), juce::Justification::centredLeft);

    r.removeFromLeft (160);   // mix toggle lives here
    auto mid = r.withTrimmedRight (330);
    g.setColour (Theme::textDim);
    juce::String txt;
    auto x = mid.getX() + 20;
    auto drawPair = [&] (const juce::String& label, const juce::String& value, juce::Colour vc)
    {
        g.setColour (Theme::textDim);
        auto lw = (int) Theme::textWidth (Theme::font (13.5f), label) + 8;
        g.drawText (label, x, 0, lw, getHeight(), juce::Justification::centredLeft);
        x += lw;
        g.setColour (vc);
        auto vw = (int) Theme::textWidth (Theme::font (13.5f), value) + 18;
        g.drawText (value, x, 0, vw, getHeight(), juce::Justification::centredLeft);
        x += vw;
    };
    drawPair (T ("Cursor", "Position"), cursorText, Theme::text);
    drawPair (T ("Selection", "Markering"), selectionText, Theme::accent);
    if (selLengthText.isNotEmpty())
        drawPair ({}, "(" + selLengthText + ")", Theme::accent);
    if (messageText.isNotEmpty())
    {
        g.setColour (Theme::textDim);
        g.drawText (messageText, x + 10, 0, juce::jmax (0, mid.getRight() - x - 10), getHeight(), juce::Justification::centredLeft, true);
    }
}

void StatusBar::resized()
{
    auto r = getLocalBounds().reduced (16, 6);
    mixToggle.setBounds (r.getX() + 230, r.getY(), 150, r.getHeight());
    zoomBox.setBounds (r.removeFromRight (90));
    r.removeFromRight (8);
    zoomInButton.setBounds (r.removeFromRight (r.getHeight()));
    zoomSlider.setBounds (r.removeFromRight (150));
    zoomOutButton.setBounds (r.removeFromRight (r.getHeight()));
}
