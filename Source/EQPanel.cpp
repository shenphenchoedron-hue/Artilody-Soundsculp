#include "EQPanel.h"

static constexpr double displayRate = 48000.0;

static juce::String freqText (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz"
                       : juce::String ((int) std::round (f)) + " Hz";
}

//==============================================================================
juce::Rectangle<float> EQCurve::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedLeft (28.0f).withTrimmedBottom (18.0f).withTrimmedTop (6.0f).withTrimmedRight (6.0f);
}

float EQCurve::xForFreq (float f) const
{
    auto r = plotArea();
    return r.getX() + r.getWidth() * (std::log10 (f / 20.0f) / 3.0f);
}

float EQCurve::freqForX (float x) const
{
    auto r = plotArea();
    return 20.0f * std::pow (10.0f, 3.0f * juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

float EQCurve::yForDb (float db) const
{
    auto r = plotArea();
    return juce::jmap (db, maxDb, minDb, r.getY(), r.getBottom());
}

float EQCurve::dbForY (float y) const
{
    auto r = plotArea();
    return juce::jlimit (minDb, maxDb, juce::jmap (y, r.getY(), r.getBottom(), maxDb, minDb));
}

juce::Point<float> EQCurve::nodePos (int band) const
{
    auto& b = settings.bands[(size_t) band];
    float db = b.usesGain() ? b.gainDb : 0.0f;
    return { xForFreq (b.frequency), yForDb (db) };
}

int EQCurve::hitBand (juce::Point<float> p) const
{
    for (int i = 3; i >= 0; --i)
        if (nodePos (i).getDistanceFrom (p) < 10.0f)
            return i;
    return -1;
}

void EQCurve::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Theme::waveBg);
    g.fillRoundedRectangle (bounds, 8.0f);

    auto r = plotArea();
    g.setFont (Theme::font (10.5f));

    for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f })
    {
        auto y = yForDb (db);
        g.setColour (db == 0.0f ? Theme::grid.brighter (0.5f) : Theme::grid);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), 2, (int) y - 7, 24, 14, juce::Justification::centredRight);
    }

    for (float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        auto x = xForFreq (f);
        g.setColour (Theme::grid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        juce::String label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
        g.drawText (label, (int) x - 16, (int) r.getBottom() + 3, 32, 13, juce::Justification::centred);
    }

    // individual band curves (subtle)
    for (int b = 0; b < 4; ++b)
    {
        if (! settings.bands[(size_t) b].enabled) continue;
        juce::Path p;
        for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
        {
            auto db = juce::jlimit (minDb - 3, maxDb + 3, DSP::bandResponseDb (settings.bands[(size_t) b], displayRate, freqForX (x)));
            if (x == r.getX()) p.startNewSubPath (x, yForDb (db)); else p.lineTo (x, yForDb (db));
        }
        g.setColour (Theme::bandColour (b).withAlpha (0.35f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // combined curve
    juce::Path curve;
    std::vector<juce::Point<float>> pts;
    for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
    {
        auto db = juce::jlimit (minDb - 3, maxDb + 3, DSP::eqResponseDb (settings, displayRate, freqForX (x)));
        pts.push_back ({ x, yForDb (db) });
    }
    for (size_t i = 0; i < pts.size(); ++i)
        if (i == 0) curve.startNewSubPath (pts[i]); else curve.lineTo (pts[i]);

    juce::Path fill (curve);
    fill.lineTo (r.getRight(), yForDb (0));
    fill.lineTo (r.getX(), yForDb (0));
    fill.closeSubPath();

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    g.setGradientFill (juce::ColourGradient (Theme::waveB.withAlpha (0.30f), r.getX(), 0, Theme::waveSel.withAlpha (0.30f), r.getRight(), 0, false));
    g.fillPath (fill);
    g.setGradientFill (juce::ColourGradient (Theme::waveB, r.getX(), 0, Theme::waveSel, r.getRight(), 0, false));
    g.strokePath (curve, juce::PathStrokeType (2.2f));
    g.restoreState();

    for (int b = 0; b < 4; ++b)
    {
        auto c = nodePos (b);
        auto col = Theme::bandColour (b);
        if (! settings.bands[(size_t) b].enabled) col = col.withSaturation (0.0f).withAlpha (0.5f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre (c));
        g.setColour (col);
        g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (c));
    }
}

void EQCurve::mouseDown (const juce::MouseEvent& e)
{
    dragBand = hitBand (e.position);
}

void EQCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    auto& b = settings.bands[(size_t) dragBand];
    b.frequency = juce::jlimit (20.0f, 20000.0f, freqForX (e.position.x));
    if (b.usesGain())
        b.gainDb = std::round (dbForY (e.position.y) * 10.0f) / 10.0f;
    repaint();
    if (onChange) onChange();
}

void EQCurve::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int band = hitBand (e.position);
    if (band < 0) return;
    auto& b = settings.bands[(size_t) band];
    b.q = juce::jlimit (0.1f, 18.0f, b.q * (w.deltaY > 0 ? 1.1f : 1.0f / 1.1f));
    repaint();
    if (onChange) onChange();
}

void EQCurve::mouseDoubleClick (const juce::MouseEvent& e)
{
    int band = hitBand (e.position);
    if (band < 0) return;
    settings.bands[(size_t) band].gainDb = 0.0f;
    repaint();
    if (onChange) onChange();
}

//==============================================================================
EQPanel::BandRow::BandRow (EQPanel& o, int i) : owner (o), index (i)
{
    type.addItemList (EQBand::getTypeNames(), 1);
    type.onChange = [this]
    {
        owner.settings.bands[(size_t) index].type = type.getSelectedItemIndex();
        owner.changed();
    };
    addAndMakeVisible (type);

    auto setup = [this] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 0, 0);
        s.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId, Theme::text);
        s.setVelocityBasedMode (false);
        addAndMakeVisible (s);
    };
    setup (freq); setup (gain); setup (q);

    freq.setRange (20.0, 20000.0, 1.0);
    freq.setSkewFactorFromMidPoint (1000.0);
    freq.textFromValueFunction = [] (double v) { return freqText (v); };
    freq.valueFromTextFunction = [] (const juce::String& t)
    {
        auto v = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue();
        return t.containsIgnoreCase ("k") ? v * 1000.0 : v;
    };
    freq.onValueChange = [this] { owner.settings.bands[(size_t) index].frequency = (float) freq.getValue(); owner.changed(); };

    gain.setRange (-15.0, 15.0, 0.1);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    gain.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
    gain.onValueChange = [this] { owner.settings.bands[(size_t) index].gainDb = (float) gain.getValue(); owner.changed(); };

    q.setRange (0.1, 18.0, 0.01);
    q.setSkewFactorFromMidPoint (1.0);
    q.textFromValueFunction = [] (double v) { return "Q " + juce::String (v, v < 10 ? 1 : 0); };
    q.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
    q.onValueChange = [this] { owner.settings.bands[(size_t) index].q = (float) q.getValue(); owner.changed(); };

    power.setClickingTogglesState (true);
    power.setColours (Theme::panel, Theme::textDim);
    power.setLitColours (Theme::panel, Theme::bandColour (index));
    power.setTooltip (T ("Band on/off", "Bånd til/fra"));
    power.onClick = [this] { owner.settings.bands[(size_t) index].enabled = power.getToggleState(); owner.changed(); };
    addAndMakeVisible (power);

    refresh();
}

void EQPanel::BandRow::refresh()
{
    auto& b = owner.settings.bands[(size_t) index];
    type.setSelectedItemIndex (b.type, juce::dontSendNotification);
    freq.setValue (b.frequency, juce::dontSendNotification);
    gain.setValue (b.gainDb, juce::dontSendNotification);
    gain.setEnabled (b.usesGain());
    q.setValue (b.q, juce::dontSendNotification);
    power.setToggleState (b.enabled, juce::dontSendNotification);
    freq.updateText(); gain.updateText(); q.updateText();
}

void EQPanel::BandRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto col = Theme::bandColour (index);
    g.setColour (col);
    g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, r.getCentreY() - 9.0f, 12.0f, 18.0f), 3.0f);
    g.setColour (col.darker (0.1f));
    g.setFont (Theme::font (16.0f, true));
    g.drawText (juce::String (index + 1), 18, 0, 20, getHeight(), juce::Justification::centred);
}

void EQPanel::BandRow::resized()
{
    auto r = getLocalBounds().reduced (0, 3);
    r.removeFromLeft (40);
    power.setBounds (r.removeFromRight (r.getHeight()));
    r.removeFromRight (4);
    auto w = r.getWidth();
    type.setBounds (r.removeFromLeft ((int) (w * 0.31f)));
    r.removeFromLeft (4);
    auto rest = r.getWidth() - 8;
    freq.setBounds (r.removeFromLeft (rest * 36 / 100)); r.removeFromLeft (4);
    gain.setBounds (r.removeFromLeft (rest * 34 / 100)); r.removeFromLeft (4);
    q.setBounds (r);
}

//==============================================================================
EQPanel::EQPanel()
{
    addAndMakeVisible (curve);
    curve.onChange = [this] { refreshRows(); if (onChange) onChange(); };

    enableSwitch.setToggleState (false, juce::dontSendNotification);
    enableSwitch.setTooltip (T ("Hear the EQ live while playing", "Hør EQ'en live under afspilning"));
    enableSwitch.onClick = [this] { if (onChange) onChange(); };
    addAndMakeVisible (enableSwitch);

    moreButton.setColours (Theme::panel, Theme::textDim);
    moreButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, T ("Reset all bands", "Nulstil alle bånd"));
        m.addItem (2, T ("Apply EQ to selection (or whole track)", "Anvend EQ på markering (eller hele sporet)"));
        m.addSeparator();
        m.addItem (3, T ("Hide EQ panel", "Skjul EQ-panel"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreButton), [this] (int r)
        {
            if (r == 1) { settings = EQSettings(); presets.setSelectedItemIndex (0, juce::dontSendNotification); changed(); }
            if (r == 2 && onApply) onApply();
            if (r == 3 && onClose) onClose();
        });
    };
    addAndMakeVisible (moreButton);

    presets.addItemList (EQSettings::presetNames(), 1);
    presets.setSelectedItemIndex (0, juce::dontSendNotification);
    presets.onChange = [this] { settings = EQSettings::preset (presets.getSelectedItemIndex()); changed(); };
    addAndMakeVisible (presets);

    applyButton.setButtonText (T ("Apply", "Anvend"));
    applyButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    applyButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    applyButton.setTooltip (T ("Apply the EQ permanently to the selection (or the whole track)",
                               "Anvend EQ'en permanent på markeringen (eller hele sporet)"));
    applyButton.onClick = [this] { if (onApply) onApply(); };
    addAndMakeVisible (applyButton);

    for (int i = 0; i < 4; ++i)
        addAndMakeVisible (rows.add (new BandRow (*this, i)));
}

void EQPanel::changed()
{
    refreshRows();
    curve.repaint();
    if (onChange) onChange();
}

void EQPanel::refreshRows()
{
    for (auto* r : rows) r->refresh();
}

void EQPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    Icons::draw (g, "sparkle", { 14.0f, 12.0f, 24.0f, 24.0f }, Theme::accent);
    g.setColour (Theme::text);
    g.setFont (Theme::font (18.0f, true));
    g.drawText ("EQ", 46, 10, 100, 28, juce::Justification::centredLeft);
}

void EQPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto header = r.removeFromTop (28);
    moreButton.setBounds (header.removeFromRight (28));
    header.removeFromRight (6);
    enableSwitch.setBounds (header.removeFromRight (44));

    r.removeFromTop (8);
    auto presetRow = r.removeFromTop (28);
    applyButton.setBounds (presetRow.removeFromRight (80));
    presetRow.removeFromRight (6);
    presets.setBounds (presetRow);

    r.removeFromTop (8);
    const int rowH = 34;
    auto rowsArea = r.removeFromBottom (rowH * 4);
    r.removeFromBottom (8);
    curve.setBounds (r);
    for (auto* row : rows)
        row->setBounds (rowsArea.removeFromTop (rowH));
}
