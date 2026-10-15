#pragma once

#include <JuceHeader.h>

class AudioDocument;

namespace FileIO
{
    juce::AudioFormatManager& getFormatManager();
    juce::String getWildcardForOpen();

    bool loadIntoDocument (const juce::File& file, AudioDocument& doc, juce::String& error);

    struct ExportFormat
    {
        juce::String id, name, extension;
        int bitDepth = 24;
    };
    const std::vector<ExportFormat>& getExportFormats();

    // Writes a buffer to disk in the given format.
    bool writeBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& dest,
                      const ExportFormat& format, juce::String& error);
}
