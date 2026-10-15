#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "AudioDocument.h"
#include "AudioEngine.h"
#include "WaveformView.h"
#include "EQPanel.h"
#include "NoisePanel.h"
#include "SidePanels.h"
#include "HeaderBar.h"
#include "FileIO.h"

//==============================================================================
class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit MainComponent (juce::ApplicationProperties& props);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override  { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

    void openFiles (const juce::Array<juce::File>& files);
    bool hasUnsavedChanges() const;
    void saveSettings();

    // Used by --screenshot / --selftest
    void loadDemoContent();
    static int runSelfTest();
    int runUiTest();

    std::function<void (const juce::String&)> onTitleChanged;

private:
    juce::ApplicationProperties& properties;
    AudioEngine engine;

    HeaderBar header;
    ToolBar toolbar;
    TracksPanel tracksPanel;
    FilePanel filePanel;
    MarkersPanel markersPanel;
    WaveformView waveform { engine };
    OverviewView overview { waveform };
    EQPanel eqPanel;
    NoisePanel noisePanel;
    StatusBar statusBar;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<std::unique_ptr<AudioDocument>> documents;
    int currentIndex = -1;
    int recordingCounter = 1, emptyTrackCounter = 1;

    juce::AudioBuffer<float> clipboard;
    double clipboardRate = 44100.0;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String transientMessage;
    juce::uint32 messageTime = 0;
    juce::Rectangle<int> waveCard, leftColumn, rightColumn;

    // ----- documents
    AudioDocument* current() const;
    std::vector<AudioDocument*> allDocuments() const;
    AudioDocument* addDocument (std::unique_ptr<AudioDocument> d);
    void selectDocument (int index);
    void closeDocument (int index);
    void syncEngine();
    void refreshTrackList();
    void refreshAll();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // ----- menus / dialogs
    void showTracksMenu();
    void showExportMenu();
    void showSettingsMenu();
    void showFileMenu();
    void chooseFilesToOpen();
    void chooseFileToInsert();
    void newEmptyTrack();
    void duplicateTrack();
    void renameTrack();
    void showMergeDialog();
    void exportAudio (int scope, const FileIO::ExportFormat& format);
    void showAudioSettings();
    void showShortcuts();

    // ----- editing
    bool requireDocument();
    bool requireSelection (const juce::String& what);
    void getTargetRange (juce::int64& start, juce::int64& end) const;   // selection or whole track
    void handleTool (const juce::String& id);
    void doTrim();
    void doCut();
    void doCopy();
    void doPaste();
    void doDelete();
    void doFade (bool in);
    void doNormalize();
    void doSilenceTrim();
    void doLoopTool();
    void doReverse();
    void doPitch();
    void applyPitch (double semitones);
    void previewPitch (double semitones);
    void applyEQ();
    void learnNoise();
    void previewNoise();
    void applyNoise();
    std::vector<float> getNoiseProfile();
    void updateNoiseMaterial();
    void insertBufferAtCursor (juce::AudioBuffer<float> data, double rate, const juce::String& undoName);
    juce::AudioBuffer<float> mixTracks (const std::vector<AudioDocument*>& tracks, double& rateOut, bool& normalised);
    void addMarkerAtCursor();
    void undo();
    void redo();

    // ----- transport
    void togglePlay();
    void play();
    void stop();
    void goToStart();
    void toggleLoop();
    void toggleRecord();
    void updateLoopRange();

    // ----- view
    void timerCallback() override;
    void updateStatus();
    void updateZoomControls();
    void setZoomFromSlider (double v);
    void setPanelsVisible (bool eq, bool noise);
    void showMessage (const juce::String& m);
    void showError (const juce::String& title, const juce::String& m);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
