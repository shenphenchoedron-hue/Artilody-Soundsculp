#pragma once

#include <JuceHeader.h>

class AudioDocument;

//==============================================================================
// Parametric EQ settings
//==============================================================================
struct EQBand
{
    enum Type { LowShelf = 0, Bell, HighShelf, LowCut, HighCut, Notch, numTypes };

    int type = Bell;
    float frequency = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
    bool enabled = true;

    static juce::StringArray getTypeNames();
    bool usesGain() const { return type == LowShelf || type == Bell || type == HighShelf; }
};

struct EQSettings
{
    std::array<EQBand, 4> bands;

    EQSettings();
    static EQSettings preset (int index);
    static juce::StringArray presetNames();
};

//==============================================================================
namespace DSP
{
    using Buffer = juce::AudioBuffer<float>;

    void fadeIn (Buffer& b);
    void fadeOut (Buffer& b);
    float getPeak (const Buffer& b);
    void normalize (Buffer& b, float targetDb = -0.1f);
    void reverse (Buffer& b);

    // Finds the first/last sample louder than threshold. Returns false if all silent.
    bool findNonSilentRange (const Buffer& b, float thresholdDb, juce::int64& first, juce::int64& last);

    // Pitch shift without changing length (phase vocoder + resampling).
    Buffer pitchShift (const Buffer& in, double semitones);

    // Resample by ratio (output length = input length / ratio).
    Buffer resample (const Buffer& in, double ratio);

    // Converts channel count (mono <-> stereo etc.).
    Buffer matchChannels (const Buffer& in, int channels);

    juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>> makeCoefficients (const EQSettings&, double sampleRate);
    void applyEQ (Buffer& b, double sampleRate, const EQSettings& settings);
    // Combined magnitude response in dB at a frequency.
    float eqResponseDb (const EQSettings&, double sampleRate, float frequency);
    float bandResponseDb (const EQBand&, double sampleRate, float frequency);

    // ---- Noise reduction (spectral gating)
    constexpr int noiseFftOrder = 11;              // 2048
    std::vector<float> learnNoiseProfile (const Buffer& noiseOnly);
    std::vector<float> estimateNoiseProfile (const Buffer& material);
    struct NoiseParams { float reduction = 0.72f, sensitivity = 0.5f, artifactControl = 0.25f; bool outputNoiseOnly = false; };
    Buffer reduceNoise (const Buffer& in, const std::vector<float>& profile, const NoiseParams&);

    // Average magnitude spectrum in dB (numBins = fftSize/2), for display.
    std::vector<float> averageSpectrumDb (const Buffer& b, int maxFrames = 400);

    // Applies the document's volume envelope (offset = position of b[0] in the document).
    void applyEnvelope (Buffer& b, const AudioDocument& doc, juce::int64 offset);
}
