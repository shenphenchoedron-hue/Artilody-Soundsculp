#pragma once

#include <JuceHeader.h>
#include "DSP.h"
#include "Theme.h"

//==============================================================================
class SpectrumView : public juce::Component
{
public:
    void setSpectra (std::vector<float> original, std::vector<float> filtered);
    void setSampleRate (double sr)            { if (sr > 0.0) { sampleRate = sr; repaint(); } }
    void setHint (const juce::String& h)      { if (h != hint) { hint = h; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    std::vector<float> orig, filt;
    double sampleRate = 44100.0;
    juce::String hint;
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
    double getLearnedProfileRate() const                 { return learnedRate; }
    // True only if the switch is on, a profile exists and it was learned at the
    // current sample rate. Use this (not hasLearnedProfile) to pick the profile.
    bool isUsingLearnedProfile() const;
    // sampleRate <= 0 means "the current sample rate".
    void setLearnedProfile (std::vector<float> p, double sampleRate = 0.0);

    // Sample rate of the current track (spectrum axis + learned profile check).
    void setSampleRate (double sr);
    double getSampleRate() const                         { return sampleRate; }
    void setNoiseOnlyPreview (bool b)                    { listenNoise.setToggleState (b, juce::dontSendNotification); }

    // Called by the owner with the material to analyse for the spectrum display.
    void setAnalysisMaterial (juce::AudioBuffer<float>&& material);

    std::function<void()> onPreview, onApply, onLearn, onClose, onStopPreview;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void scheduleUpdate()  { startTimer (250); }
    bool learnedRateMatches() const;
    void updateHint();

    SpectrumView spectrum;
    SwitchButton enableSwitch;
    IconButton moreButton { "more" };
    juce::Slider reduction, sensitivity, artifact;
    juce::Label reductionLabel, sensitivityLabel, artifactLabel;
    IconButton listenNoise { "headphones" };
    IconButton previewButton { "play", T ("Preview", "Forhør") };
    IconButton applyButton { "sparkle", T ("Apply", "Anvend") };

    std::vector<float> learnedProfile;
    double learnedRate = 0.0;
    double sampleRate = 44100.0;
    juce::AudioBuffer<float> material;
};
