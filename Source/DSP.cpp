#include "DSP.h"
#include "AudioDocument.h"
#include "Theme.h"

using Coeffs = juce::dsp::IIR::Coefficients<float>;

//==============================================================================
juce::StringArray EQBand::getTypeNames()
{
    return { T ("Low Shelf", "Lav shelf"), T ("Bell", "Klokke"), T ("High Shelf", "Høj shelf"),
             T ("Low Cut", "Lav-cut"), T ("High Cut", "Høj-cut"), T ("Notch", "Notch") };
}

EQSettings::EQSettings()
{
    bands[0] = { EQBand::LowShelf, 120.0f, 0.0f, 0.7f, true };
    bands[1] = { EQBand::Bell, 400.0f, 0.0f, 1.0f, true };
    bands[2] = { EQBand::Bell, 2800.0f, 0.0f, 1.0f, true };
    bands[3] = { EQBand::HighShelf, 8000.0f, 0.0f, 0.7f, true };
}

juce::StringArray EQSettings::presetNames()
{
    return { T ("Parametric EQ (flat)", "Parametrisk EQ (flad)"), T ("Bass boost", "Bas-boost"),
             T ("Vocal presence", "Vokal-tydelighed"), T ("Warmth", "Varme"), T ("Brightness", "Lysere klang"),
             T ("Remove rumble", "Fjern rumlen"), T ("De-mud", "Fjern mudder"), T ("Telephone", "Telefon") };
}

EQSettings EQSettings::preset (int index)
{
    EQSettings s;
    auto& b = s.bands;
    switch (index)
    {
        case 1: b[0].gainDb = 6.0f; b[0].frequency = 100.0f; b[1].gainDb = 2.0f; b[1].frequency = 200.0f; break;
        case 2: b[0] = { EQBand::LowCut, 90.0f, 0.0f, 0.7f, true }; b[1].gainDb = -2.5f; b[1].frequency = 300.0f;
                b[2].gainDb = 4.0f; b[2].frequency = 3500.0f; b[2].q = 0.9f; b[3].gainDb = 2.0f; b[3].frequency = 10000.0f; break;
        case 3: b[0].gainDb = 3.0f; b[0].frequency = 180.0f; b[1].gainDb = 1.5f; b[1].frequency = 350.0f; b[3].gainDb = -2.5f; break;
        case 4: b[2].gainDb = 2.5f; b[2].frequency = 4500.0f; b[3].gainDb = 4.5f; b[3].frequency = 9000.0f; break;
        case 5: b[0] = { EQBand::LowCut, 80.0f, 0.0f, 0.71f, true }; break;
        case 6: b[1].gainDb = -4.5f; b[1].frequency = 280.0f; b[1].q = 1.4f; break;
        case 7: b[0] = { EQBand::LowCut, 400.0f, 0.0f, 0.9f, true }; b[3] = { EQBand::HighCut, 3200.0f, 0.0f, 0.9f, true };
                b[2].gainDb = 5.0f; b[2].frequency = 1500.0f; break;
        default: break;
    }
    return s;
}

//==============================================================================
namespace DSP
{

void fadeIn (Buffer& b)
{
    const int n = b.getNumSamples();
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            auto t = (float) i / (float) juce::jmax (1, n - 1);
            d[i] *= t * t * (3.0f - 2.0f * t) * 0.5f + t * 0.5f;     // gentle S-curve
        }
    }
}

void fadeOut (Buffer& b)
{
    const int n = b.getNumSamples();
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            auto t = 1.0f - (float) i / (float) juce::jmax (1, n - 1);
            d[i] *= t * t * (3.0f - 2.0f * t) * 0.5f + t * 0.5f;
        }
    }
}

float getPeak (const Buffer& b)
{
    float peak = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        peak = juce::jmax (peak, b.getMagnitude (c, 0, b.getNumSamples()));
    return peak;
}

void normalize (Buffer& b, float targetDb)
{
    auto peak = getPeak (b);
    if (peak > 1.0e-6f)
        b.applyGain (juce::Decibels::decibelsToGain (targetDb) / peak);
}

void reverse (Buffer& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        b.reverse (c, 0, b.getNumSamples());
}

bool findNonSilentRange (const Buffer& b, float thresholdDb, juce::int64& first, juce::int64& last)
{
    const float thr = juce::Decibels::decibelsToGain (thresholdDb);
    const int n = b.getNumSamples();
    first = -1; last = -1;

    for (int i = 0; i < n && first < 0; ++i)
        for (int c = 0; c < b.getNumChannels(); ++c)
            if (std::abs (b.getSample (c, i)) > thr) { first = i; break; }

    for (int i = n - 1; i >= 0 && last < 0; --i)
        for (int c = 0; c < b.getNumChannels(); ++c)
            if (std::abs (b.getSample (c, i)) > thr) { last = i; break; }

    return first >= 0 && last >= first;
}

//==============================================================================
static inline float cubicAt (const std::vector<float>& d, double pos)
{
    auto i = (juce::int64) std::floor (pos);
    auto t = (float) (pos - (double) i);
    auto at = [&d] (juce::int64 k) { return (k >= 0 && k < (juce::int64) d.size()) ? d[(size_t) k] : 0.0f; };
    float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

Buffer pitchShift (const Buffer& in, double semitones)
{
    Buffer out;
    out.makeCopyOf (in);
    if (std::abs (semitones) < 1.0e-3 || in.getNumSamples() == 0)
        return out;

    const double ratio = std::pow (2.0, semitones / 12.0);
    constexpr int order = 11, N = 1 << order, Hs = N / 4;
    const double Ha = (double) Hs / ratio;
    const int inLen = in.getNumSamples();
    const int paddedLen = inLen + 2 * N;
    const int numFrames = (int) ((double) (paddedLen - N) / Ha) + 1;
    const int stretchedLen = numFrames * Hs + N;

    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);

    std::vector<juce::dsp::Complex<float>> time ((size_t) N), freq ((size_t) N);
    const int bins = N / 2 + 1;

    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        std::vector<float> padded ((size_t) paddedLen, 0.0f);
        std::copy (in.getReadPointer (c), in.getReadPointer (c) + inLen, padded.begin() + N);

        std::vector<float> stretched ((size_t) stretchedLen, 0.0f), wsum ((size_t) stretchedLen, 0.0f);
        std::vector<double> lastPhase ((size_t) bins, 0.0), sumPhase ((size_t) bins, 0.0);
        int prevPos = 0;

        for (int k = 0; k < numFrames; ++k)
        {
            const int pos = (int) std::llround (k * Ha);
            if (pos + N > paddedLen) break;
            const double actualHa = k == 0 ? Ha : (double) juce::jmax (1, pos - prevPos);
            prevPos = pos;

            for (int i = 0; i < N; ++i)
                time[(size_t) i] = { padded[(size_t) (pos + i)] * window[(size_t) i], 0.0f };

            fft.perform (time.data(), freq.data(), false);

            for (int b = 0; b < bins; ++b)
            {
                auto& z = freq[(size_t) b];
                const double mag = std::abs (z);
                const double ph = std::arg (z);
                const double omega = juce::MathConstants<double>::twoPi * b / N;

                if (k == 0)
                    sumPhase[(size_t) b] = ph;
                else
                {
                    double delta = ph - lastPhase[(size_t) b] - omega * actualHa;
                    delta -= juce::MathConstants<double>::twoPi * std::round (delta / juce::MathConstants<double>::twoPi);
                    const double trueFreq = omega + delta / actualHa;
                    sumPhase[(size_t) b] += trueFreq * Hs;
                }
                lastPhase[(size_t) b] = ph;
                z = std::polar ((float) mag, (float) sumPhase[(size_t) b]);
            }

            for (int b = 1; b < N / 2; ++b)
                freq[(size_t) (N - b)] = std::conj (freq[(size_t) b]);

            fft.perform (freq.data(), time.data(), true);

            const int outPos = k * Hs;
            for (int i = 0; i < N; ++i)
            {
                stretched[(size_t) (outPos + i)] += time[(size_t) i].real() * window[(size_t) i];
                wsum[(size_t) (outPos + i)] += window[(size_t) i] * window[(size_t) i];
            }
        }

        for (size_t i = 0; i < stretched.size(); ++i)
            stretched[i] = wsum[i] > 1.0e-3f ? stretched[i] / wsum[i] : 0.0f;

        auto* dst = out.getWritePointer (c);
        for (int j = 0; j < inLen; ++j)
            dst[j] = cubicAt (stretched, (double) (j + N) * ratio);
    }

    return out;
}

Buffer resample (const Buffer& in, double ratio)
{
    if (std::abs (ratio - 1.0) < 1.0e-9)
    {
        Buffer copy;
        copy.makeCopyOf (in);
        return copy;
    }

    const int outLen = (int) std::floor ((double) in.getNumSamples() / ratio);
    Buffer out (in.getNumChannels(), juce::jmax (0, outLen));

    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        std::vector<float> src ((size_t) in.getNumSamples() + 8, 0.0f);
        std::copy (in.getReadPointer (c), in.getReadPointer (c) + in.getNumSamples(), src.begin());
        juce::LagrangeInterpolator interp;
        interp.process (ratio, src.data(), out.getWritePointer (c), outLen);
    }
    return out;
}

Buffer matchChannels (const Buffer& in, int channels)
{
    if (in.getNumChannels() == channels)
    {
        Buffer copy;
        copy.makeCopyOf (in);
        return copy;
    }

    Buffer out (channels, in.getNumSamples());
    out.clear();
    if (in.getNumChannels() == 0) return out;

    if (channels == 1)
    {
        for (int c = 0; c < in.getNumChannels(); ++c)
            out.addFrom (0, 0, in, c, 0, in.getNumSamples(), 1.0f / (float) in.getNumChannels());
    }
    else
    {
        for (int c = 0; c < channels; ++c)
            out.copyFrom (c, 0, in, c % in.getNumChannels(), 0, in.getNumSamples());
    }
    return out;
}

//==============================================================================
static Coeffs::Ptr makeBandCoefficients (const EQBand& band, double sr)
{
    auto f = juce::jlimit (10.0f, (float) (sr * 0.49), band.frequency);
    auto q = juce::jlimit (0.1f, 18.0f, band.q);
    auto g = juce::Decibels::decibelsToGain (band.gainDb);
    switch (band.type)
    {
        case EQBand::LowShelf:  return Coeffs::makeLowShelf (sr, f, q, g);
        case EQBand::HighShelf: return Coeffs::makeHighShelf (sr, f, q, g);
        case EQBand::LowCut:    return Coeffs::makeHighPass (sr, f, q);
        case EQBand::HighCut:   return Coeffs::makeLowPass (sr, f, q);
        case EQBand::Notch:     return Coeffs::makeNotch (sr, f, q);
        case EQBand::Bell:
        default:                return Coeffs::makePeakFilter (sr, f, q, g);
    }
}

juce::ReferenceCountedArray<Coeffs> makeCoefficients (const EQSettings& s, double sr)
{
    juce::ReferenceCountedArray<Coeffs> result;
    for (auto& b : s.bands)
        if (b.enabled)
            result.add (makeBandCoefficients (b, sr));
    return result;
}

void applyEQ (Buffer& b, double sr, const EQSettings& s)
{
    auto coeffs = makeCoefficients (s, sr);
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* d = b.getWritePointer (c);
        for (auto* co : coeffs)
        {
            juce::dsp::IIR::Filter<float> filter (co);
            filter.reset();
            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] = filter.processSample (d[i]);
        }
    }

    // keep things from clipping hard
    auto peak = getPeak (b);
    if (peak > 1.0f)
        b.applyGain (0.999f / peak);
}

float bandResponseDb (const EQBand& band, double sr, float frequency)
{
    if (! band.enabled) return 0.0f;
    auto c = makeBandCoefficients (band, sr);
    auto mag = c->getMagnitudeForFrequency (frequency, sr);
    return (float) juce::Decibels::gainToDecibels (mag, -60.0);
}

float eqResponseDb (const EQSettings& s, double sr, float frequency)
{
    float total = 0.0f;
    for (auto& b : s.bands)
        total += bandResponseDb (b, sr, frequency);
    return total;
}

//==============================================================================
// Noise reduction
//==============================================================================
namespace
{
    constexpr int nN = 1 << noiseFftOrder;
    constexpr int nHop = nN / 4;
    constexpr int nBins = nN / 2 + 1;

    std::vector<float> hann()
    {
        std::vector<float> w ((size_t) nN);
        for (int i = 0; i < nN; ++i)
            w[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) nN);
        return w;
    }

    // Mixes the buffer down to mono.
    std::vector<float> mono (const Buffer& b)
    {
        std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
        if (b.getNumChannels() == 0) return m;
        for (int c = 0; c < b.getNumChannels(); ++c)
            juce::FloatVectorOperations::add (m.data(), b.getReadPointer (c), b.getNumSamples());
        juce::FloatVectorOperations::multiply (m.data(), 1.0f / (float) b.getNumChannels(), b.getNumSamples());
        return m;
    }

    // Calls fn(frameMagnitudes) for frames of the mono signal (with optional stride).
    template <typename Fn>
    void forEachFrame (const std::vector<float>& sig, int maxFrames, Fn&& fn)
    {
        juce::dsp::FFT fft (noiseFftOrder);
        auto w = hann();
        std::vector<juce::dsp::Complex<float>> t ((size_t) nN), f ((size_t) nN);
        std::vector<float> mags ((size_t) nBins);

        const int len = (int) sig.size();
        int frames = juce::jmax (1, (len - nN) / nHop + 1);
        int stride = juce::jmax (1, frames / juce::jmax (1, maxFrames));

        for (int k = 0; k < frames; k += stride)
        {
            int pos = k * nHop;
            for (int i = 0; i < nN; ++i)
            {
                int idx = pos + i;
                t[(size_t) i] = { idx < len ? sig[(size_t) idx] * w[(size_t) i] : 0.0f, 0.0f };
            }
            fft.perform (t.data(), f.data(), false);
            for (int b = 0; b < nBins; ++b)
                mags[(size_t) b] = std::abs (f[(size_t) b]);
            fn (mags);
        }
    }
}

std::vector<float> learnNoiseProfile (const Buffer& noiseOnly)
{
    std::vector<float> profile ((size_t) nBins, 0.0f);
    int count = 0;
    forEachFrame (mono (noiseOnly), 2000, [&] (const std::vector<float>& m)
    {
        for (int b = 0; b < nBins; ++b) profile[(size_t) b] += m[(size_t) b];
        ++count;
    });
    for (auto& v : profile) v /= (float) juce::jmax (1, count);
    return profile;
}

std::vector<float> estimateNoiseProfile (const Buffer& material)
{
    // Use a low percentile of each bin's magnitude as the noise floor estimate.
    std::vector<std::vector<float>> perBin ((size_t) nBins);
    forEachFrame (mono (material), 600, [&] (const std::vector<float>& m)
    {
        for (int b = 0; b < nBins; ++b) perBin[(size_t) b].push_back (m[(size_t) b]);
    });

    std::vector<float> profile ((size_t) nBins, 0.0f);
    for (int b = 0; b < nBins; ++b)
    {
        auto& v = perBin[(size_t) b];
        if (v.empty()) continue;
        auto idx = (size_t) ((double) (v.size() - 1) * 0.12);
        std::nth_element (v.begin(), v.begin() + (long) idx, v.end());
        profile[(size_t) b] = v[idx];
    }
    return profile;
}

Buffer reduceNoise (const Buffer& in, const std::vector<float>& profile, const NoiseParams& p)
{
    Buffer out (in.getNumChannels(), in.getNumSamples());
    out.clear();
    if (in.getNumSamples() == 0 || (int) profile.size() != nBins)
    {
        out.makeCopyOf (in);
        return out;
    }

    juce::dsp::FFT fft (noiseFftOrder);
    auto w = hann();
    const float floorGain = juce::jlimit (0.003f, 1.0f, 1.0f - p.reduction);
    const float thrMul = 1.0f + p.sensitivity * 5.0f;
    const float release = juce::jlimit (0.0f, 0.95f, p.artifactControl * 0.9f);
    const int freqSmooth = (int) std::round (p.artifactControl * 6.0f);

    const int len = in.getNumSamples();
    const int padded = len + 2 * nN;
    const int frames = (padded - nN) / nHop + 1;

    std::vector<juce::dsp::Complex<float>> t ((size_t) nN), f ((size_t) nN);
    std::vector<float> gains ((size_t) nBins), smoothed ((size_t) nBins), prevGains ((size_t) nBins, 1.0f);

    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        std::vector<float> src ((size_t) padded, 0.0f);
        std::copy (in.getReadPointer (c), in.getReadPointer (c) + len, src.begin() + nN);
        std::vector<float> acc ((size_t) padded, 0.0f), wsum ((size_t) padded, 0.0f);
        std::fill (prevGains.begin(), prevGains.end(), 1.0f);

        for (int k = 0; k < frames; ++k)
        {
            const int pos = k * nHop;
            if (pos + nN > padded) break;

            for (int i = 0; i < nN; ++i)
                t[(size_t) i] = { src[(size_t) (pos + i)] * w[(size_t) i], 0.0f };
            fft.perform (t.data(), f.data(), false);

            for (int b = 0; b < nBins; ++b)
            {
                const float mag = std::abs (f[(size_t) b]);
                const float thr = profile[(size_t) b] * thrMul + 1.0e-9f;
                const float r = mag / thr;
                // smooth transition between 0.5x and 1x threshold
                const float x = juce::jlimit (0.0f, 1.0f, (r - 0.5f) * 2.0f);
                gains[(size_t) b] = floorGain + (1.0f - floorGain) * (x * x * (3.0f - 2.0f * x));
            }

            // frequency smoothing
            for (int b = 0; b < nBins; ++b)
            {
                float sum = 0.0f; int n = 0;
                for (int d = -freqSmooth; d <= freqSmooth; ++d)
                {
                    int bb = b + d;
                    if (bb >= 0 && bb < nBins) { sum += gains[(size_t) bb]; ++n; }
                }
                smoothed[(size_t) b] = sum / (float) n;
            }

            // time smoothing: fast attack, smoothed release
            for (int b = 0; b < nBins; ++b)
            {
                float g = smoothed[(size_t) b];
                if (g < prevGains[(size_t) b])
                    g = prevGains[(size_t) b] * release + g * (1.0f - release);
                prevGains[(size_t) b] = g;
                if (p.outputNoiseOnly) g = 1.0f - g;
                f[(size_t) b] *= g;
            }
            for (int b = 1; b < nN / 2; ++b)
                f[(size_t) (nN - b)] = std::conj (f[(size_t) b]);

            fft.perform (f.data(), t.data(), true);
            for (int i = 0; i < nN; ++i)
            {
                acc[(size_t) (pos + i)] += t[(size_t) i].real() * w[(size_t) i];
                wsum[(size_t) (pos + i)] += w[(size_t) i] * w[(size_t) i];
            }
        }

        auto* d = out.getWritePointer (c);
        for (int i = 0; i < len; ++i)
        {
            auto ws = wsum[(size_t) (i + nN)];
            d[i] = ws > 1.0e-3f ? acc[(size_t) (i + nN)] / ws : 0.0f;
        }
    }
    return out;
}

std::vector<float> averageSpectrumDb (const Buffer& b, int maxFrames)
{
    std::vector<float> avg ((size_t) nBins, 0.0f);
    int count = 0;
    forEachFrame (mono (b), maxFrames, [&] (const std::vector<float>& m)
    {
        for (int i = 0; i < nBins; ++i) avg[(size_t) i] += m[(size_t) i];
        ++count;
    });
    const float norm = 2.0f / (float) (nN / 2);   // full-scale sine -> ~0 dB
    for (auto& v : avg)
        v = juce::Decibels::gainToDecibels (v / (float) juce::jmax (1, count) * norm, -120.0f);
    return avg;
}

//==============================================================================
void applyEnvelope (Buffer& b, const AudioDocument& doc, juce::int64 offset)
{
    if (doc.envelope.empty()) return;
    constexpr int block = 4096;
    std::vector<float> gains (block);
    for (int pos = 0; pos < b.getNumSamples(); pos += block)
    {
        int n = juce::jmin (block, b.getNumSamples() - pos);
        doc.fillGains (offset + pos, n, gains.data());
        for (int c = 0; c < b.getNumChannels(); ++c)
            juce::FloatVectorOperations::multiply (b.getWritePointer (c, pos), gains.data(), n);
    }
}

} // namespace DSP
