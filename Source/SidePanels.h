#pragma once

#include <JuceHeader.h>
#include "AudioDocument.h"
#include "Theme.h"

//==============================================================================
// White rounded card with a title and an optional header button.
class Card : public juce::Component
{
public:
    explicit Card (const juce::String& title);
    void paint (juce::Graphics&) override;
    juce::Rectangle<int> getContentArea() const    { return getLocalBounds().reduced (14, 0).withTrimmedTop (46).withTrimmedBottom (10); }
    juce::Rectangle<int> getHeaderButtonArea() const { return { getWidth() - 42, 10, 28, 28 }; }

protected:
    juce::String title;
};

//==============================================================================
class TracksPanel : public Card
{
public:
    TracksPanel();
    void setTracks (const std::vector<AudioDocument*>& tracks, int currentIndex);
    void resized() override;

    std::function<void (int)> onSelect, onClose;
    std::function<void()> onAddMenu, onOpen, onMerge;

private:
    struct Row : public juce::Component
    {
        Row (TracksPanel& o, int i) : owner (o), index (i)
        {
            close.setColours (juce::Colours::transparentBlack, Theme::textDim);
            close.setTooltip (T ("Close track", "Luk spor"));
            close.onClick = [this] { if (owner.onClose) owner.onClose (index); };
            addAndMakeVisible (close);
        }
        void paint (juce::Graphics&) override;
        void resized() override { close.setBounds (getWidth() - 24, (getHeight() - 20) / 2, 20, 20); }
        void mouseUp (const juce::MouseEvent&) override { if (owner.onSelect) owner.onSelect (index); }

        TracksPanel& owner;
        int index;
        juce::String name, info;
        bool current = false, modified = false;
        IconButton close { "close" };
    };

    IconButton addButton { "plus" };
    IconButton openButton { "open", T ("Open", "Åbn") };
    IconButton mergeButton { "merge", T ("Merge", "Flet") };
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;
};

//==============================================================================
class FilePanel : public Card
{
public:
    FilePanel();
    void setDocument (AudioDocument* d)   { doc = d; repaint(); }
    void paint (juce::Graphics&) override;

    std::function<void()> onMenu;

private:
    AudioDocument* doc = nullptr;
    IconButton moreButton { "more" };
};

//==============================================================================
class MarkersPanel : public Card
{
public:
    void paint (juce::Graphics&) override;
    MarkersPanel();
    void setDocument (AudioDocument* d);
    void refresh();
    void focusMarker (int index);
    void chooseColour (int index, juce::Rectangle<int> screenArea);
    void resized() override;

    std::function<void()> onAdd;
    std::function<void (int)> onJump;
    std::function<void()> onChanged;

private:
    struct Row : public juce::Component
    {
        Row (MarkersPanel& owner, int index);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;

        struct ColourButton : public juce::Button
        {
            ColourButton() : juce::Button ("colour") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
            juce::Colour colour;
            void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        };

        MarkersPanel& owner;
        int index;
        ColourButton colourButton;
        juce::TextEditor nameEditor;
        IconButton deleteButton { "close" };
    };

    AudioDocument* doc = nullptr;
    IconButton addButton { "plus" };
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;
};
