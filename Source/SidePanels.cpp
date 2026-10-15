#include "SidePanels.h"

Card::Card (const juce::String& t) : title (t) {}

void Card::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (Theme::text);
    g.setFont (Theme::font (17.0f, true));
    g.drawText (title, 16, 10, getWidth() - 60, 28, juce::Justification::centredLeft);
    g.setColour (Theme::border);
    g.drawHorizontalLine (44, 14.0f, (float) getWidth() - 14.0f);
}

//==============================================================================
TracksPanel::TracksPanel() : Card (T ("Tracks", "Spor"))
{
    addButton.setColours (juce::Colours::transparentBlack, Theme::text);
    addButton.setTooltip (T ("New empty track, open files, insert file, merge tracks...",
                             "Nyt tomt spor, åbn filer, indsæt fil, flet spor..."));
    addButton.onClick = [this] { if (onAddMenu) onAddMenu(); };
    addAndMakeVisible (addButton);
    openButton.setColours (Theme::accentSoft, Theme::accent);
    openButton.setTooltip (T ("Open audio files (Ctrl+O)", "Åbn lydfiler (Ctrl+O)"));
    openButton.onClick = [this] { if (onOpen) onOpen(); };
    addAndMakeVisible (openButton);
    mergeButton.setColours (Theme::accentSoft, Theme::accent);
    mergeButton.setTooltip (T ("Merge several tracks into one new track", "Flet flere spor sammen til ét nyt spor"));
    mergeButton.onClick = [this] { if (onMerge) onMerge(); };
    addAndMakeVisible (mergeButton);

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
}

void TracksPanel::setTracks (const std::vector<AudioDocument*>& tracks, int currentIndex)
{
    // Rebuild rows only when the count changes; otherwise update in place.
    if ((int) tracks.size() != rows.size())
    {
        rows.clear();
        for (int i = 0; i < (int) tracks.size(); ++i)
            list.addAndMakeVisible (rows.add (new Row (*this, i)));
    }

    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        auto* r = rows[i];
        auto* d = tracks[(size_t) i];
        r->name = d->name;
        r->info = formatTime (d->getLengthSeconds()) + "  " + (d->getNumChannels() == 1 ? "Mono" : "Stereo");
        r->current = i == currentIndex;
        r->modified = d->modified;
        r->repaint();
    }
    resized();
}

void TracksPanel::resized()
{
    addButton.setBounds (getHeaderButtonArea());
    auto hb = getHeaderButtonArea();
    auto x = hb.getX() - 6;
    auto mw = mergeButton.getIdealWidth (28);
    mergeButton.setBounds (x - mw, hb.getY(), mw, 28);
    x -= mw + 6;
    auto ow = openButton.getIdealWidth (28);
    openButton.setBounds (x - ow, hb.getY(), ow, 28);
    viewport.setBounds (getContentArea().withTrimmedTop (2));
    const int rowH = 40;
    list.setSize (viewport.getMaximumVisibleWidth(), rows.size() * rowH);
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, list.getWidth(), rowH - 2);

    if (rows.isEmpty())
        list.setSize (viewport.getWidth(), 0);
}

void TracksPanel::Row::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (current)
    {
        g.setColour (Theme::accentSoft);
        g.fillRoundedRectangle (b, 6.0f);
    }
    else if (isMouseOver (true))
    {
        g.setColour (Theme::background);
        g.fillRoundedRectangle (b, 6.0f);
    }

    Icons::draw (g, "normalize", { 6.0f, b.getCentreY() - 9.0f, 18.0f, 18.0f }, current ? Theme::accent : Theme::textDim);
    g.setColour (Theme::text);
    g.setFont (Theme::font (14.0f, current));
    g.drawText ((modified ? "* " : "") + name, 30, 2, getWidth() - 58, 18, juce::Justification::centredLeft, true);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f));
    g.drawText (info, 30, 19, getWidth() - 58, 16, juce::Justification::centredLeft, true);
}

//==============================================================================
FilePanel::FilePanel() : Card (T ("File", "Fil"))
{
    moreButton.setColours (juce::Colours::transparentBlack, Theme::textDim);
    moreButton.onClick = [this] { if (onMenu) onMenu(); };
    addAndMakeVisible (moreButton);
    moreButton.setBounds (0, 0, 28, 28);
}

void FilePanel::paint (juce::Graphics& g)
{
    Card::paint (g);
    moreButton.setBounds (getHeaderButtonArea());

    auto r = getContentArea().toFloat();
    if (doc == nullptr)
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (14.0f));
        g.drawText (T ("No file open", "Ingen fil åben"), r.withHeight (24), juce::Justification::centredLeft);
        return;
    }

    auto sizeText = [] (juce::int64 bytes)
    {
        if (bytes <= 0) return juce::String ("-");
        return bytes > 1024 * 1024 ? juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB"
                                   : juce::String ((double) bytes / 1024.0, 0) + " KB";
    };

    const std::pair<juce::String, juce::String> rows[] = {
        { T ("Name", "Navn"), doc->name },
        { T ("Format", "Format"), doc->formatName },
        { T ("Sample Rate", "Samplerate"), juce::String (doc->sampleRate / 1000.0, 1) + " kHz" },
        { T ("Bit Depth", "Bitdybde"), juce::String (doc->bitDepth) + " bit" },
        { T ("Channels", "Kanaler"), doc->getNumChannels() == 1 ? "Mono" : (doc->getNumChannels() == 2 ? "Stereo" : juce::String (doc->getNumChannels())) },
        { T ("Length", "Længde"), formatTime (doc->getLengthSeconds()) },
        { T ("File Size", "Filstørrelse"), sizeText (doc->fileSize) },
    };

    const bool compact = r.getHeight() < 7 * 40;
    for (auto& row : rows)
    {
        if (compact)
        {
            auto line = r.removeFromTop (21);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (13.0f));
            g.drawText (row.first, line.removeFromLeft (88), juce::Justification::centredLeft);
            g.setColour (Theme::text);
            g.setFont (Theme::font (13.5f));
            g.drawText (row.second, line, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (13.0f));
            g.drawText (row.first, r.removeFromTop (18), juce::Justification::centredLeft);
            g.setColour (Theme::text);
            g.setFont (Theme::font (15.0f));
            g.drawText (row.second, r.removeFromTop (22), juce::Justification::centredLeft, true);
        }
    }
}

//==============================================================================
MarkersPanel::MarkersPanel() : Card (T ("Markers", "Markører"))
{
    addButton.setColours (juce::Colours::transparentBlack, Theme::text);
    addButton.setTooltip (T ("Add a marker at the cursor (M)", "Tilføj en markør ved positionen (M)"));
    addButton.onClick = [this] { if (onAdd) onAdd(); };
    addAndMakeVisible (addButton);

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
}

void MarkersPanel::setDocument (AudioDocument* d)
{
    doc = d;
    refresh();
    repaint();
}

void MarkersPanel::paint (juce::Graphics& g)
{
    Card::paint (g);
    // show which track the markers belong to
    if (doc != nullptr)
    {
        auto x = 16 + (int) Theme::textWidth (Theme::font (17.0f, true), title) + 8;
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (13.0f));
        g.drawText ("- " + doc->name, x, 10, getWidth() - x - 46, 28, juce::Justification::centredLeft, true);
    }
}

void MarkersPanel::refresh()
{
    const int count = doc != nullptr ? (int) doc->markers.size() : 0;
    if (count != rows.size())
    {
        rows.clear();
        for (int i = 0; i < count; ++i)
            list.addAndMakeVisible (rows.add (new Row (*this, i)));
    }
    for (auto* r : rows)
    {
        if (! r->nameEditor.hasKeyboardFocus (true))
            r->nameEditor.setText (doc->markers[(size_t) r->index].name, false);
        r->colourButton.colour = doc->markers[(size_t) r->index].colour;
        r->colourButton.repaint();
        r->repaint();
    }
    resized();
}

void MarkersPanel::focusMarker (int index)
{
    refresh();
    if (juce::isPositiveAndBelow (index, rows.size()))
    {
        auto* r = rows[index];
        viewport.setViewPosition (0, juce::jmax (0, r->getY() - 10));
        r->nameEditor.grabKeyboardFocus();
        r->nameEditor.selectAll();
    }
}

void MarkersPanel::resized()
{
    addButton.setBounds (getHeaderButtonArea());
    viewport.setBounds (getContentArea().withTrimmedTop (2));
    const int rowH = 32;
    list.setSize (viewport.getMaximumVisibleWidth(), rows.size() * rowH);
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, list.getWidth(), rowH);
}

MarkersPanel::Row::Row (MarkersPanel& o, int i) : owner (o), index (i)
{
    nameEditor.setFont (Theme::font (14.0f));
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (4, 6);
    nameEditor.setSelectAllWhenFocused (false);
    nameEditor.setTooltip (T ("Type the marker text here", "Skriv markørens tekst her"));
    nameEditor.onTextChange = [this]
    {
        if (owner.doc != nullptr && juce::isPositiveAndBelow (index, (int) owner.doc->markers.size()))
        {
            owner.doc->markers[(size_t) index].name = nameEditor.getText();
            owner.doc->modified = true;
            if (owner.onChanged) owner.onChanged();
        }
    };
    nameEditor.onReturnKey = [this] { nameEditor.giveAwayKeyboardFocus(); };
    nameEditor.onEscapeKey = [this] { nameEditor.giveAwayKeyboardFocus(); };
    addAndMakeVisible (nameEditor);

    deleteButton.setColours (juce::Colours::transparentBlack, Theme::textDim);
    deleteButton.setTooltip (T ("Delete marker", "Slet markør"));
    deleteButton.onClick = [this]
    {
        auto* d = owner.doc;
        if (d == nullptr || ! juce::isPositiveAndBelow (index, (int) d->markers.size())) return;
        d->pushUndo (T ("Delete marker", "Slet markør"));
        d->markers.erase (d->markers.begin() + index);
        d->annotationsChanged();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MarkersPanel> (&owner)]
        {
            if (safe != nullptr) { safe->refresh(); if (safe->onChanged) safe->onChanged(); }
        });
    };
    addAndMakeVisible (deleteButton);

    colourButton.setTooltip (T ("Change the marker colour", "Skift markørens farve"));
    colourButton.onClick = [this] { owner.chooseColour (index, colourButton.getScreenBounds()); };
    addAndMakeVisible (colourButton);
}

void MarkersPanel::Row::ColourButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto h = (float) getHeight();
    if (highlighted)
    {
        g.setColour (Theme::border);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }
    juce::Path tri;
    tri.addTriangle (3.0f, h * 0.5f - 7, 3.0f, h * 0.5f + 7, 15.0f, h * 0.5f);
    g.setColour (colour);
    g.fillPath (tri);
    if (colour.getBrightness() > 0.9f)
    {
        g.setColour (Theme::textDim);
        g.strokePath (tri, juce::PathStrokeType (1.0f));
    }
}

void MarkersPanel::chooseColour (int index, juce::Rectangle<int> screenArea)
{
    if (doc == nullptr || ! juce::isPositiveAndBelow (index, (int) doc->markers.size())) return;
    doc->pushUndo (T ("Marker colour", "Markørfarve"));
    juce::Component::SafePointer<MarkersPanel> safe (this);
    auto* d = doc;
    showColourPicker (screenArea, doc->markers[(size_t) index].colour, [safe, d, index] (juce::Colour c)
    {
        if (safe == nullptr || safe->doc != d || ! juce::isPositiveAndBelow (index, (int) d->markers.size())) return;
        d->markers[(size_t) index].colour = c;
        d->annotationsChanged();
        safe->refresh();
        if (safe->onChanged) safe->onChanged();
    });
}

void MarkersPanel::Row::paint (juce::Graphics& g)
{
    if (owner.doc == nullptr || ! juce::isPositiveAndBelow (index, (int) owner.doc->markers.size())) return;
    auto& m = owner.doc->markers[(size_t) index];
    auto h = (float) getHeight();

    juce::ignoreUnused (h);

    g.setColour (Theme::text);
    g.setFont (Theme::font (13.5f, true));
    g.drawText (juce::String (index + 1), 20, 0, 24, getHeight(), juce::Justification::centredLeft);

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.5f));
    g.drawText (formatTime (m.time), getWidth() - 106, 0, 80, getHeight(), juce::Justification::centredRight);
}

void MarkersPanel::Row::resized()
{
    colourButton.setBounds (0, 4, 20, getHeight() - 8);
    nameEditor.setBounds (44, 3, getWidth() - 44 - 108, getHeight() - 6);
    deleteButton.setBounds (getWidth() - 22, (getHeight() - 18) / 2, 18, 18);
}

void MarkersPanel::Row::mouseUp (const juce::MouseEvent&)
{
    if (owner.onJump) owner.onJump (index);
}
