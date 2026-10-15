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
    return "*.wav;*.wave;*.flac;*.ogg;*.oga;*.mp3;*.aif;*.aiff;*.m4a;*.aac;*.opus;*.wma;*.mp4;*.caf;*.bwf";
}

//==============================================================================
juce::File FileIO::findFFmpeg()
{
   #if JUCE_WINDOWS
    const juce::String exe = "ffmpeg.exe";
    const char separator = ';';
   #else
    const juce::String exe = "ffmpeg";
    const char separator = ':';
   #endif

    auto appDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    juce::Array<juce::File> candidates { appDir.getChildFile (exe),
                                         appDir.getChildFile ("ffmpeg").getChildFile (exe),
                                         appDir.getParentDirectory().getChildFile ("Resources").getChildFile (exe) };

    auto path = juce::SystemStats::getEnvironmentVariable ("PATH", {});
    for (auto& dir : juce::StringArray::fromTokens (path, juce::String::charToString (separator), {}))
        if (dir.isNotEmpty())
            candidates.add (juce::File (dir).getChildFile (exe));

   #if JUCE_MAC
    candidates.add (juce::File ("/opt/homebrew/bin/ffmpeg"));
    candidates.add (juce::File ("/usr/local/bin/ffmpeg"));
   #elif JUCE_LINUX
    candidates.add (juce::File ("/usr/bin/ffmpeg"));
   #endif

    for (auto& f : candidates)
        if (f.existsAsFile())
            return f;
    return {};
}

static bool runProcess (const juce::StringArray& args, juce::String& error)
{
    juce::ChildProcess proc;
    if (! proc.start (args, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
    {
        error = "Could not start " + args[0];
        return false;
    }
    auto output = proc.readAllProcessOutput();
    proc.waitForProcessToFinish (10 * 60 * 1000);
    if (proc.getExitCode() != 0)
    {
        auto lines = juce::StringArray::fromLines (output.trim());
        lines.removeEmptyStrings();
        error = "ffmpeg: " + (lines.isEmpty() ? juce::String ("unknown error") : lines[lines.size() - 1]);
        return false;
    }
    return true;
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
    if (ext == "opus")                return "Opus";
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
    juce::TemporaryFile temp (".wav");

    if (reader == nullptr)
    {
        // Try decoding with ffmpeg (m4a, opus, wma ...)
        auto ffmpeg = findFFmpeg();
        if (ffmpeg == juce::File())
        {
            error = T ("This file format is not supported (install ffmpeg for more formats): ",
                       "Filformatet understøttes ikke (installér ffmpeg for flere formater): ") + file.getFileName();
            return false;
        }
        if (! runProcess ({ ffmpeg.getFullPathName(), "-y", "-v", "error", "-i", file.getFullPathName(),
                            "-vn", "-acodec", "pcm_f32le", temp.getFile().getFullPathName() }, error))
            return false;

        reader.reset (getFormatManager().createReaderFor (temp.getFile()));
        if (reader == nullptr)
        {
            error = T ("Could not decode: ", "Kunne ikke afkode: ") + file.getFileName();
            return false;
        }
    }

    const auto length = reader->lengthInSamples;
    if (length > (juce::int64) std::numeric_limits<int>::max() - 16)
    {
        error = T ("The file is too long.", "Filen er for lang.");
        return false;
    }

    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), (int) length);
    reader->read (&buf, 0, (int) length, 0, true, true);

    doc.name = file.getFileName();
    doc.file = file;
    doc.formatName = describeFormat (file, reader->usesFloatingPointData && file.hasFileExtension ("wav"));
    doc.bitDepth = (int) reader->bitsPerSample;
    doc.fileSize = file.getSize();
    doc.setBuffer (std::move (buf), reader->sampleRate);
    doc.modified = false;
    doc.clearHistory();
    return true;
}

//==============================================================================
const std::vector<FileIO::ExportFormat>& FileIO::getExportFormats()
{
    static const std::vector<ExportFormat> formats {
        { "wav16",  "WAV 16-bit PCM",            "wav",  16, false },
        { "wav24",  "WAV 24-bit PCM",            "wav",  24, false },
        { "wav32",  "WAV 32-bit float",          "wav",  32, false },
        { "aiff24", "AIFF 24-bit",               "aiff", 24, false },
        { "flac16", "FLAC 16-bit (lossless)",    "flac", 16, false },
        { "flac24", "FLAC 24-bit (lossless)",    "flac", 24, false },
        { "ogg",    "OGG Vorbis (192 kbps)",     "ogg",  16, false },
        { "mp3",    "MP3 (320 kbps)",            "mp3",  16, true  },
        { "mp3v",   "MP3 (192 kbps)",            "mp3",  16, true  },
        { "m4a",    "M4A / AAC (256 kbps)",      "m4a",  16, true  },
        { "alac",   "M4A / Apple Lossless",      "m4a",  24, true  },
        { "opus",   "Opus (160 kbps)",           "opus", 16, true  },
    };
    return formats;
}

static bool writeWithJuce (juce::AudioFormat& fmt, const juce::AudioBuffer<float>& buffer, double sampleRate,
                           const juce::File& dest, int bits, int quality, juce::String& error)
{
    dest.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (dest.createOutputStream());
    if (os == nullptr || os->failedToOpen())
    {
        error = T ("Cannot write to ", "Kan ikke skrive til ") + dest.getFullPathName();
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

    // Lossy formats need headroom against clipping
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

    // ffmpeg based formats
    auto ffmpeg = findFFmpeg();
    if (ffmpeg == juce::File())
    {
        error = T ("This format needs ffmpeg, which was not found. Install ffmpeg (Linux: sudo apt install ffmpeg) or export as WAV/FLAC/OGG.",
                   "Dette format kræver ffmpeg, som ikke blev fundet. Installér ffmpeg (Linux: sudo apt install ffmpeg) eller eksportér som WAV/FLAC/OGG.");
        return false;
    }

    juce::TemporaryFile temp (".wav");
    juce::WavAudioFormat wav;
    if (! writeWithJuce (wav, safe, sampleRate, temp.getFile(), 24, 0, error))
        return false;

    juce::StringArray args { ffmpeg.getFullPathName(), "-y", "-v", "error", "-i", temp.getFile().getFullPathName() };
    if (f.id == "mp3")       args.addArray ({ "-codec:a", "libmp3lame", "-b:a", "320k" });
    else if (f.id == "mp3v") args.addArray ({ "-codec:a", "libmp3lame", "-b:a", "192k" });
    else if (f.id == "m4a")  args.addArray ({ "-c:a", "aac", "-b:a", "256k" });
    else if (f.id == "alac") args.addArray ({ "-c:a", "alac", "-sample_fmt", "s32p" });
    else if (f.id == "opus") args.addArray ({ "-c:a", "libopus", "-b:a", "160k", "-ar", "48000" });
    dest.deleteFile();
    args.add (dest.getFullPathName());

    return runProcess (args, error);
}
