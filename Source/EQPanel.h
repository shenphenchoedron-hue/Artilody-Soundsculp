#pragma once

#include <JuceHeader.h>
#include "DSP.h"
#include "Theme.h"

//==============================================================================
class EQCurve : public juce::Component
{
public:
    EQCurve (EQSettings& s) : settings (s) {}

    std::function<void()> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override  { dragBand = -1; }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr float minDb = -15.0f, maxDb = 15.0f;

    // Sample rate used to draw the response; frequencies are limited to 0.49 * rate.
    void setSampleRate (double sr)     { sampleRate = sr; repaint(); }
    float getMaxFrequency() const;

    // Highest Q allowed for a band (shelves are limited to avoid large overshoot).
    static float maxQFor (const EQBand& b);

private:
    EQSettings& settings;
    int dragBand = -1;
    double sampleRate = 48000.0;

    juce::Rectangle<float> plotArea() const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;
    juce::Point<float> nodePos (int band) const;
    int hitBand (juce::Point<float>) const;
};

//==============================================================================
class EQPanel : public juce::Component
{
public:
    EQPanel();

    EQSettings& getSettings()          { return settings; }
    // Live EQ is only active while the switch is on AND the panel is shown, so a
    // hidden panel never leaves an invisible EQ running. onChange is called when
    // the panel is shown or hidden.
    bool isEQEnabled() const           { return enableSwitch.getToggleState() && isVisible(); }
    bool isEQSwitchOn() const          { return enableSwitch.getToggleState(); }

    // Sample rate of the current track: the curve is drawn at this rate and the
    // frequency controls are limited to 0.49 * sr. Default 48000.
    void setSampleRate (double sr);
    double getSampleRate() const       { return sampleRate; }

    std::function<void()> onChange;    // settings or enabled state changed
    std::function<void()> onApply;     // apply EQ destructively to selection / track
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void refreshRows();

private:
    struct BandRow : public juce::Component
    {
        BandRow (EQPanel& owner, int index);
        void resized() override;
        void paint (juce::Graphics&) override;
        void refresh();
        void updateFrequencyRange();

        EQPanel& owner;
        int index;
        juce::ComboBox type;
        juce::Slider freq, gain, q;
        IconButton power { "power" };
    };

    EQSettings settings;
    double sampleRate = 48000.0;
    EQCurve curve { settings };
    SwitchButton enableSwitch;
    IconButton moreButton { "more" };
    juce::ComboBox presets;
    juce::TextButton applyButton;
    juce::OwnedArray<BandRow> rows;

    void changed();
};
