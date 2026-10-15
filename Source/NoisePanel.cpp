#include "NoisePanel.h"

void SpectrumView::setSpectra (std::vector<float> o, std::vector<float> f)
{
    orig = std::move (o);
    filt = std::move (f);
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Theme::waveBg);
    g.fillRoundedRectangle (b, 8.0f);

    auto r = b.withTrimmedLeft (30).withTrimmedBottom (18).withTrimmedTop (22).withTrimmedRight (8);
    if (r.getWidth() <= 1.0f || r.getHeight() <= 1.0f) return;
    const float minDb = -90.0f, maxDb = 0.0f;
    auto xForF = [&] (float f) { return r.getX() + r.getWidth() * std::log10 (juce::jmax (20.0f, f) / 20.0f) / 3.0f; };
    auto yForDb = [&] (float db) { return juce::jmap (juce::jlimit (minDb, maxDb, db), maxDb, minDb, r.getY(), r.getBottom()); };

    g.setFont (Theme::font (10.5f));
    for (float db : { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f })
    {
        g.setColour (Theme::grid);
        g.drawHorizontalLine ((int) yForDb (db), r.getX(), r.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText (juce::String ((int) db), 0, (int) yForDb (db) - 7, 26, 14, juce::Justification::centredRight);
    }
    for (float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        juce::String label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
        g.drawText (label, (int) xForF (f) - 16, (int) r.getBottom() + 3, 32, 13, juce::Justification::centred);
    }

    // legend
    g.setFont (Theme::font (12.0f));
    auto legend = b.removeFromTop (20).reduced (8, 0);
    auto drawLegend = [&] (juce::Colour c, const juce::String& t, float x)
    {
        g.setColour (c);
        g.fillEllipse (x, legend.getCentreY() - 4, 8, 8);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawText (t, (int) x + 12, (int) legend.getY(), 90, (int) legend.getHeight(), juce::Justification::centredLeft);
    };
    drawLegend (juce::Colours::grey, T ("Original", "Original"), b.getCentreX() - 90);
    drawLegend (Theme::waveB, T ("Filtered", "Filtreret"), b.getCentreX() + 10);

    // nothing exists above Nyquist at this sample rate
    const float nyquist = (float) (sampleRate * 0.5);
    const float specRight = nyquist < 20000.0f ? juce::jmax (r.getX(), xForF (nyquist)) : r.getRight();
    if (specRight < r.getRight())
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (specRight, r.getY(), r.getRight() - specRight, r.getHeight()));
    }

    auto drawSpec = [&] (const std::vector<float>& s, juce::Colour c, bool fill)
    {
        if (s.size() < 4) return;
        const float binHz = (float) sampleRate / (float) ((s.size() - 1) * 2);
        juce::Path p;
        bool started = false;
        float lastX = r.getX();
        for (float x = r.getX(); x <= specRight; x += 1.5f)
        {
            float f = 20.0f * std::pow (10.0f, 3.0f * (x - r.getX()) / r.getWidth());
            auto bin = juce::jlimit<size_t> (1, s.size() - 1, (size_t) (f / binHz));
            // take max over the bins covered by this pixel for a readable display
            float f2 = 20.0f * std::pow (10.0f, 3.0f * (x + 1.5f - r.getX()) / r.getWidth());
            auto bin2 = juce::jlimit<size_t> (bin, s.size() - 1, (size_t) (f2 / binHz));
            float v = s[bin];
            for (auto k = bin; k <= bin2; ++k) v = juce::jmax (v, s[k]);
            auto y = yForDb (v);
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
            lastX = x;
        }
        if (! started) return;
        if (fill)
        {
            juce::Path f (p);
            f.lineTo (lastX, r.getBottom());
            f.lineTo (r.getX(), r.getBottom());
            f.closeSubPath();
            g.setColour (c.withAlpha (0.25f));
            g.fillPath (f);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    };

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    drawSpec (orig, juce::Colours::grey.brighter (0.2f), true);
    drawSpec (filt, Theme::waveB, false);
    g.restoreState();

    if (orig.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawText (T ("No audio", "Ingen lyd"), r, juce::Justification::centred);
    }

    if (hint.isNotEmpty())
    {
        g.setColour (Theme::envelope);
        g.setFont (Theme::font (12.0f));
        g.drawFittedText (hint, r.reduced (6.0f, 4.0f).toNearestInt().removeFromTop (32), juce::Justification::topLeft, 2);
    }
}

//==============================================================================
NoisePanel::NoisePanel()
{
    addAndMakeVisible (spectrum);

    enableSwitch.setTooltip (T ("On: use the noise profile learned from a selection (menu ... > Learn noise profile). Off: the noise is estimated automatically.",
                                "Til: brug støjprofilen lært fra en markering (menu ... > Lær støjprofil). Fra: støjen estimeres automatisk."));
    enableSwitch.onClick = [this]
    {
        if (enableSwitch.getToggleState() && (learnedProfile.empty() || ! learnedRateMatches()) && onLearn)
            onLearn();
        scheduleUpdate();
    };
    addAndMakeVisible (enableSwitch);

    moreButton.setColours (Theme::panel, Theme::textDim);
    moreButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, T ("Learn noise profile from selection", "Lær støjprofil fra markering"));
        m.addItem (2, T ("Forget learned profile", "Glem lært profil"), ! learnedProfile.empty());
        m.addItem (3, T ("Reset sliders", "Nulstil skydere"));
        m.addSeparator();
        m.addItem (4, T ("Hide noise remover", "Skjul støjfjerner"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreButton), [this] (int r)
        {
            if (r == 1 && onLearn) onLearn();
            if (r == 2) { setLearnedProfile ({}); }
            if (r == 3) { reduction.setValue (72); sensitivity.setValue (50); artifact.setValue (25); }
            if (r == 4 && onClose) onClose();
        });
    };
    addAndMakeVisible (moreButton);

    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, double v)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 24);
        s.setRange (0.0, 100.0, 1.0);
        s.setTextValueSuffix (" %");
        s.setValue (v, juce::dontSendNotification);
        s.onValueChange = [this] { scheduleUpdate(); };
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setFont (Theme::font (14.0f));
        l.setColour (juce::Label::textColourId, Theme::text);
        addAndMakeVisible (l);
    };
    setup (reduction, reductionLabel, T ("Reduction", "Reduktion"), 72);
    setup (sensitivity, sensitivityLabel, T ("Sensitivity", "Følsomhed"), 50);
    setup (artifact, artifactLabel, T ("Artifact Control", "Artefaktkontrol"), 25);
    reduction.setTooltip (T ("How much the noise is turned down", "Hvor meget støjen dæmpes"));
    sensitivity.setTooltip (T ("How much of the signal is treated as noise", "Hvor meget af signalet der regnes som støj"));
    artifact.setTooltip (T ("Smooths the result to avoid 'watery' artifacts", "Udglatter resultatet for at undgå 'vandede' artefakter"));

    listenNoise.setClickingTogglesState (true);
    listenNoise.setColours (Theme::panel, Theme::text);
    listenNoise.setTooltip (T ("Preview only the noise that will be removed", "Forhør kun den støj der fjernes"));
    addAndMakeVisible (listenNoise);

    previewButton.setColours (Theme::panel, Theme::text);
    previewButton.setTooltip (T ("Listen to the result before applying (selection or first 15 s)",
                                 "Lyt til resultatet før det anvendes (markering eller første 15 s)"));
    previewButton.onClick = [this] { if (onPreview) onPreview(); };
    addAndMakeVisible (previewButton);

    applyButton.setColours (Theme::accent, juce::Colours::white);
    applyButton.setTooltip (T ("Remove noise from the selection (or the whole track)", "Fjern støj fra markeringen (eller hele sporet)"));
    applyButton.onClick = [this] { if (onApply) onApply(); };
    addAndMakeVisible (applyButton);
}

DSP::NoiseParams NoisePanel::getParams() const
{
    DSP::NoiseParams p;
    p.reduction = (float) reduction.getValue() / 100.0f;
    p.sensitivity = (float) sensitivity.getValue() / 100.0f;
    p.artifactControl = (float) artifact.getValue() / 100.0f;
    p.outputNoiseOnly = listenNoise.getToggleState();
    return p;
}

void NoisePanel::setLearnedProfile (std::vector<float> p, double rate)
{
    learnedProfile = std::move (p);
    learnedRate = learnedProfile.empty() ? 0.0 : (rate > 0.0 ? rate : sampleRate);
    enableSwitch.setToggleState (! learnedProfile.empty(), juce::dontSendNotification);
    updateHint();
    scheduleUpdate();
}

bool NoisePanel::learnedRateMatches() const
{
    return std::abs (learnedRate - sampleRate) < 0.5;
}

bool NoisePanel::isUsingLearnedProfile() const
{
    return enableSwitch.getToggleState() && ! learnedProfile.empty() && learnedRateMatches();
}

void NoisePanel::setSampleRate (double sr)
{
    if (! (sr > 0.0) || sr == sampleRate) return;
    sampleRate = sr;
    spectrum.setSampleRate (sr);
    updateHint();
    scheduleUpdate();
}

void NoisePanel::updateHint()
{
    if (enableSwitch.getToggleState() && ! learnedProfile.empty() && ! learnedRateMatches())
        spectrum.setHint (T ("The learned noise profile was made at ", "Den lærte støjprofil blev lavet ved ")
                          + juce::String (learnedRate / 1000.0, 1) + " kHz"
                          + T (" - this track uses the automatic estimate. Learn again to use a profile.",
                               " - dette spor bruger det automatiske estimat. Lær igen for at bruge en profil."));
    else
        spectrum.setHint ({});
}

void NoisePanel::setAnalysisMaterial (juce::AudioBuffer<float>&& m)
{
    material = std::move (m);
    scheduleUpdate();
}

void NoisePanel::timerCallback()
{
    stopTimer();
    updateHint();
    if (material.getNumSamples() < 4096)
    {
        spectrum.setSpectra ({}, {});
        return;
    }

    // same choice as the owner makes for Preview / Apply
    auto profile = isUsingLearnedProfile() ? learnedProfile
                                           : DSP::estimateNoiseProfile (material);
    auto params = getParams();
    params.outputNoiseOnly = false;
    auto filtered = DSP::reduceNoise (material, profile, params);
    spectrum.setSpectra (DSP::averageSpectrumDb (material, 150), DSP::averageSpectrumDb (filtered, 150));
}

void NoisePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    Icons::draw (g, "sparkle", { 14.0f, 12.0f, 24.0f, 24.0f }, Theme::accent);
    g.setColour (Theme::text);
    g.setFont (Theme::font (18.0f, true));
    g.drawText (T ("Noise Remover", "Støjfjerner"), 46, 10, 200, 28, juce::Justification::centredLeft);
}

void NoisePanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto header = r.removeFromTop (28);
    moreButton.setBounds (header.removeFromRight (28));
    header.removeFromRight (6);
    enableSwitch.setBounds (header.removeFromRight (44));

    r.removeFromTop (8);
    auto buttons = r.removeFromBottom (38);
    listenNoise.setBounds (buttons.removeFromLeft (44));
    buttons.removeFromLeft (8);
    previewButton.setBounds (buttons.removeFromLeft ((buttons.getWidth() - 8) * 2 / 5));
    buttons.removeFromLeft (8);
    applyButton.setBounds (buttons);

    r.removeFromBottom (8);
    for (auto* pair : { &artifact, &sensitivity, &reduction })
    {
        auto row = r.removeFromBottom (28);
        auto* label = pair == &artifact ? &artifactLabel : (pair == &sensitivity ? &sensitivityLabel : &reductionLabel);
        label->setBounds (row.removeFromLeft (110));
        pair->setBounds (row);
    }
    r.removeFromBottom (6);
    spectrum.setBounds (r);
}
