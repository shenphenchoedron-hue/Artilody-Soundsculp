#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
class HeaderBar : public juce::Component
{
public:
    HeaderBar();
    void paint (juce::Graphics&) override;
    void resized() override;

    void setSelectionInfo (const juce::String& selection, const juce::String& length);

    // Transport (in the requested order)
    IconButton recordButton { "record" }, homeButton { "home" }, playButton { "play" },
               stopButton { "stop" }, loopButton { "loop" };

    juce::ComboBox trackBox;
    IconButton undoButton { "undo" }, redoButton { "redo" };
    IconButton zoomOutButton { "zoomout" }, zoomInButton { "zoomin" };
    IconButton openButton { "open", T ("Open file", "Åbn fil") };
    IconButton tracksButton { "plus" };
    juce::Slider zoomSlider;
    IconButton exportButton { "export", T ("Export", "Eksport") };
    IconButton settingsButton { "settings" };

private:
    juce::String selectionText { "--:--.--- - --:--.---" }, lengthText { "00:00.000" };
    juce::Rectangle<int> transportGroup, editGroup, zoomGroup, infoArea, logoArea;
};

//==============================================================================
class ToolBar : public juce::Component
{
public:
    ToolBar();
    void resized() override;
    void paint (juce::Graphics&) override;

    IconButton* get (const juce::String& id);
    std::function<void (const juce::String& id)> onTool;

private:
    struct Entry { juce::String id; std::unique_ptr<IconButton> button; bool rightAligned; };
    std::vector<Entry> entries;
};

//==============================================================================
class StatusBar : public juce::Component
{
public:
    StatusBar();
    void paint (juce::Graphics&) override;
    void resized() override;

    void setInfo (const juce::String& format, const juce::String& cursor, const juce::String& selection,
                  const juce::String& selLength, const juce::String& message);

    juce::Slider zoomSlider;
    juce::ComboBox zoomBox;
    IconButton zoomOutButton { "zoomout" }, zoomInButton { "zoomin" };

private:
    juce::String formatText, cursorText, selectionText, selLengthText, messageText;
};
