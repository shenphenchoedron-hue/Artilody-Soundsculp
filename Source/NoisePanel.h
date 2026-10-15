#pragma once

#include <JuceHeader.h>
#include "DSP.h"
#include "Theme.h"

//==============================================================================
class SpectrumView : public juce::Component
{
public:
    void setSpectra (std::vector<float> original, std::vector<float> filtered);
    void paint (juce::Graphics&) override;

private:
    std::vector<float> orig, filt;
};

//==============================================================================
class NoisePanel : public juce::Component,
                   private juce::Timer
{
public:
    NoisePanel();

    DSP::NoiseParams getParams() const;
    bool hasLearnedProfile() const                       { return ! learnedProfile.empty(); }
    const std::vector<float>& getLearnedProfile() const  { return learnedProfile; }
    void setLearnedProfile (std::vector<float> p);
    void setNoiseOnlyPreview (bool b)                    { listenNoise.setToggleState (b, juce::dontSendNotification); }

    // Called by the owner with the material to analyse for the spectrum display.
    void setAnalysisMaterial (juce::AudioBuffer<float>&& material);

    std::function<void()> onPreview, onApply, onLearn, onClose, onStopPreview;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void scheduleUpdate()  { startTimer (250); }

    SpectrumView spectrum;
    SwitchButton enableSwitch;
    IconButton moreButton { "more" };
    juce::Slider reduction, sensitivity, artifact;
    juce::Label reductionLabel, sensitivityLabel, artifactLabel;
    IconButton listenNoise { "headphones" };
    IconButton previewButton { "play", T ("Preview", "Forhør") };
    IconButton applyButton { "sparkle", T ("Apply", "Anvend") };

    std::vector<float> learnedProfile;
    juce::AudioBuffer<float> material;
};
