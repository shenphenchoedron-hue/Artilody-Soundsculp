#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "FileIO.h"

//==============================================================================
// "Export audio" dialog: what to export, format, file name and folder.
//==============================================================================
class ExportDialog : public juce::Component
{
public:
    struct Options
    {
        bool hasTrack = false, hasSelection = false, multipleTracks = false;
        double trackSeconds = 0, selectionSeconds = 0, mixSeconds = 0;
        double sampleRate = 44100.0;       // kept for compatibility (not used by the dialog)
        int channels = 2;                  // kept for compatibility (not used by the dialog)
        // per-scope output format (used for the info text / size estimate)
        double trackRate = 44100, selectionRate = 44100, mixRate = 44100;
        int trackChannels = 2, selectionChannels = 2, mixChannels = 2;
        juce::String trackName, lastFormatId;
        juce::File folder;
        int initialScope = 0;
    };

    explicit ExportDialog (const Options& o);

    // scope: 0 = current track, 1 = selection, 2 = all tracks mixed
    std::function<void (int scope, int formatIndex, juce::File file)> onExport;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct ScopeCard : public juce::Button
    {
        ScopeCard (const juce::String& icon, const juce::String& title);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        juce::String icon, title, detail;
    };

    Options options;
    int scope = 0;

    ScopeCard trackCard { "normalize", T ("Current track", "Aktuelt spor") };
    ScopeCard selectionCard { "select", T ("Selection only", "Kun markering") };
    ScopeCard mixCard { "merge", T ("All tracks mixed", "Alle spor mixet") };

    juce::ComboBox formatBox;
    juce::TextEditor nameEditor, folderEditor;
    juce::TextButton browseButton, cancelButton, exportButton;
    juce::String infoText;

    juce::Rectangle<int> scopeLabelArea, formatLabelArea, nameLabelArea, folderLabelArea, infoArea;

    void setScope (int s);
    bool isScopeAvailable (int s) const;
    void updateExportEnabled();
    void formatChanged();
    void updateInfo();
    const FileIO::ExportFormat* selectedFormat() const;
    juce::String defaultBaseName() const;
    void doExport();

    std::unique_ptr<juce::FileChooser> chooser;
};
