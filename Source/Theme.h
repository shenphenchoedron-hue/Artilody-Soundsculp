#pragma once

#include <JuceHeader.h>

//==============================================================================
// Colours, fonts, translation helper, icons and the custom LookAndFeel.
//==============================================================================
namespace Theme
{
    const juce::Colour background   { 0xfff1f4f9 };
    const juce::Colour panel        { 0xffffffff };
    const juce::Colour border       { 0xffe1e7f0 };
    const juce::Colour text         { 0xff1b2436 };
    const juce::Colour textDim      { 0xff667085 };
    const juce::Colour accent       { 0xff1f6feb };
    const juce::Colour accentSoft   { 0xffdce8fd };
    const juce::Colour waveBg       { 0xff141b2d };
    const juce::Colour waveBg2      { 0xff1a2238 };
    const juce::Colour grid         { 0xff27324c };
    const juce::Colour waveA        { 0xff3b9cff };
    const juce::Colour waveB        { 0xff2fe0e6 };
    const juce::Colour waveSel      { 0xffa883ff };
    const juce::Colour selection    { 0xff4b4fd8 };
    const juce::Colour envelope     { 0xfff7862a };
    const juce::Colour playhead     { 0xffffd84a };

    // Transport colours
    const juce::Colour record       { 0xffe53935 };
    const juce::Colour home         { 0xfff5a524 };
    const juce::Colour play         { 0xff1fbf75 };
    const juce::Colour stop         { 0xff3d5afe };
    const juce::Colour loop         { 0xff9b51e0 };

    juce::Colour markerColour (int index);
    juce::Colour bandColour (int index);
    juce::Font font (float height, bool bold = false);
    float textWidth (const juce::Font& f, const juce::String& text);
}

//==============================================================================
// Simple two-language (English / Danish) support.
bool isDanish();
void setDanish (bool shouldBeDanish);
juce::String T (const char* english, const char* danish);

juce::String formatTime (double seconds);           // 00:03.240
juce::String formatTimeShort (double seconds);      // 0:03

//==============================================================================
namespace Icons
{
    // Draws a named line icon inside the given area.
    void draw (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour);
}

//==============================================================================
// Rounded button with an icon (and optional text). Used for toolbar and transport.
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& iconName, const juce::String& text = {});

    void setColours (juce::Colour background, juce::Colour iconColour);
    void setLitColours (juce::Colour litBackground, juce::Colour litIcon);
    void setShowText (bool shouldShow)       { showText = shouldShow; repaint(); }
    void setRound (bool shouldBeRound)       { round = shouldBeRound; repaint(); }
    void setIcon (const juce::String& n)     { icon = n; repaint(); }
    int getIdealWidth (int height) const;
    juce::String getLabel() const            { return label; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String icon, label;
    juce::Colour bg { Theme::panel }, fg { Theme::text };
    juce::Colour litBg { Theme::accentSoft }, litFg { Theme::accent };
    bool showText = true, round = false;
};

//==============================================================================
class SoundsculpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SoundsculpLookAndFeel();

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getLabelFont (juce::Label&) override;
};

//==============================================================================
// iOS style on/off switch.
class SwitchButton : public juce::ToggleButton
{
public:
    void paintButton (juce::Graphics&, bool, bool) override;
};
