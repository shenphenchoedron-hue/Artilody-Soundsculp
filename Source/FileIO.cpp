#include "FileIO.h"
#include "AudioDocument.h"
#include "Theme.h"

juce::AudioFormatManager& FileIO::getFormatManager()
{
    static juce::AudioFormatManager fm;
    static bool initialised = false;
    if (! initialised)
    {
        fm.registerBasicFormats();
        initialised = true;
    }
    return fm;
}

juce::String FileIO::getWildcardForOpen()
{
    // Only what the registered JUCE formats can read on this platform (e.g. M4A/AAC/CAF via Core Audio on macOS).
    return getFormatManager().getWildcardForAllFormats();
}

static juce::String describeFormat (const juce::File& file, bool isFloat)
{
    auto ext = file.getFileExtension().toLowerCase().trimCharactersAtStart (".");
    if (ext == "wav" || ext == "wave" || ext == "bwf") return isFloat ? "WAV (Float)" : "WAV (PCM)";
    if (ext == "flac")                return "FLAC";
    if (ext == "mp3")                 return "MP3";
    if (ext == "ogg" || ext == "oga") return "Ogg Vorbis";
    if (ext == "aif" || ext == "aiff") return "AIFF";
    if (ext == "m4a" || ext == "aac" || ext == "mp4") return "AAC / M4A";
    return ext.toUpperCase();
}

bool FileIO::loadIntoDocument (const juce::File& file, AudioDocument& doc, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = T ("File not found: ", "Filen findes ikke: ") + file.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (getFormatManager().createReaderFor (file));

    if (reader == nullptr)
    {
        error = T ("This file format is not supported: ", "Filformatet understøttes ikke: ") + file.getFileName();
        return false;
    }

    const auto length = reader->lengthInSamples;
    const auto rate = reader->sampleRate;
    if (! std::isfinite (rate) || rate <= 0.0 || rate > 10000000.0 || reader->numChannels == 0 || reader->numChannels > 64)
    {
        error = T ("The file has an invalid sample rate or channel count: ", "Filen har en ugyldig samplerate eller kanalantal: ")
                  + file.getFileName();
        return false;
    }
    if (length <= 0)
    {
        error = T ("The file contains no audio: ", "Filen indeholder ingen lyd: ") + file.getFileName();
        return false;
    }
    if (length > (juce::int64) std::numeric_limits<int>::max() - 16)
    {
        error = T ("The file is too long.", "Filen er for lang.");
        return false;
    }

    juce::AudioBuffer<float> buf;
    try
    {
        buf.setSize ((int) reader->numChannels, (int) length);
    }
    catch (const std::bad_alloc&)
    {
        error = T ("Not enough memory to open the file.", "Der er ikke nok hukommelse til at åbne filen.");
        return false;
    }
    if (! reader->read (&buf, 0, (int) length, 0, true, true))
    {
        error = T ("Could not read the audio data: ", "Kunne ikke læse lyddata: ") + file.getFileName();
        return false;
    }

    doc.name = file.getFileName();
    doc.file = file;
    doc.formatName = describeFormat (file, reader->usesFloatingPointData && file.hasFileExtension ("wav"));
    doc.bitDepth = (int) reader->bitsPerSample;
    doc.fileSize = file.getSize();
    doc.setBuffer (std::move (buf), rate);
    doc.modified = false;
    doc.clearHistory();
    return true;
}

//==============================================================================
const std::vector<FileIO::ExportFormat>& FileIO::getExportFormats()
{
    static const std::vector<ExportFormat> formats {
        { "wav16",  "WAV 16-bit PCM",            "wav",  16 },
        { "wav24",  "WAV 24-bit PCM",            "wav",  24 },
        { "wav32",  "WAV 32-bit float",          "wav",  32 },
        { "aiff24", "AIFF 24-bit",               "aiff", 24 },
        { "flac16", "FLAC 16-bit (lossless)",    "flac", 16 },
        { "flac24", "FLAC 24-bit (lossless)",    "flac", 24 },
        { "ogg",    "OGG Vorbis (192 kbps)",     "ogg",  16 },
    };
    return formats;
}

// Writes directly into 'file' (which should be a fresh temporary file).
static bool writeWithJuceToFile (juce::AudioFormat& fmt, const juce::AudioBuffer<float>& buffer, double sampleRate,
                                 const juce::File& file, int bits, int quality, juce::String& error)
{
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
    if (os == nullptr || os->failedToOpen())
    {
        error = T ("Cannot write to ", "Kan ikke skrive til ") + file.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (fmt.createWriterFor (os.get(), sampleRate,
                                                                           (unsigned int) buffer.getNumChannels(),
                                                                           bits, {}, quality));
    if (writer == nullptr)
    {
        error = T ("The format does not support these settings.", "Formatet understøtter ikke disse indstillinger.");
        return false;
    }
    os.release();   // owned by the writer now

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
    {
        error = T ("Error while writing the file.", "Fejl under skrivning af filen.");
        return false;
    }
    writer.reset();   // flushes and closes the file (writes the header)

    if (! file.existsAsFile() || file.getSize() <= 0)
    {
        error = T ("Error while writing the file.", "Fejl under skrivning af filen.");
        return false;
    }
    return true;
}

// Writes to a temporary file next to 'dest' and only replaces 'dest' when everything succeeded.
static bool writeWithJuce (juce::AudioFormat& fmt, const juce::AudioBuffer<float>& buffer, double sampleRate,
                           const juce::File& dest, int bits, int quality, juce::String& error)
{
    juce::TemporaryFile temp (dest);
    if (! writeWithJuceToFile (fmt, buffer, sampleRate, temp.getFile(), bits, quality, error))
        return false;
    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = T ("Cannot write to ", "Kan ikke skrive til ") + dest.getFullPathName();
        return false;
    }
    return true;
}

bool FileIO::writeBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& dest,
                          const ExportFormat& f, juce::String& error)
{
    if (buffer.getNumSamples() == 0)
    {
        error = T ("There is no audio to export.", "Der er ingen lyd at eksportere.");
        return false;
    }

    // Integer formats (and Ogg Vorbis) cannot hold values beyond full scale
    juce::AudioBuffer<float> safe;
    safe.makeCopyOf (buffer);
    if (f.bitDepth < 32)
        for (int c = 0; c < safe.getNumChannels(); ++c)
        {
            auto* d = safe.getWritePointer (c);
            for (int i = 0; i < safe.getNumSamples(); ++i)
                d[i] = juce::jlimit (-1.0f, 1.0f, d[i]);
        }

    if (f.id.startsWith ("wav"))
    {
        juce::WavAudioFormat wav;
        return writeWithJuce (wav, safe, sampleRate, dest, f.bitDepth, 0, error);
    }
    if (f.id.startsWith ("aiff"))
    {
        juce::AiffAudioFormat aiff;
        return writeWithJuce (aiff, safe, sampleRate, dest, f.bitDepth, 0, error);
    }
    if (f.id.startsWith ("flac"))
    {
        juce::FlacAudioFormat flac;
        return writeWithJuce (flac, safe, sampleRate, dest, f.bitDepth, 5, error);
    }
    if (f.id == "ogg")
    {
        juce::OggVorbisAudioFormat ogg;
        auto opts = ogg.getQualityOptions();
        auto q = juce::jmax (0, opts.indexOf ("192 kbps"));
        return writeWithJuce (ogg, safe, sampleRate, dest, 16, q, error);
    }

    error = T ("Unknown export format.", "Ukendt eksportformat.");
    return false;
}
