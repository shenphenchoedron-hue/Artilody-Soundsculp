#include "SidePanels.h"

static juce::String channelsText (int n)
{
    return n == 1 ? juce::String ("Mono") : (n == 2 ? juce::String ("Stereo") : juce::String (n) + T (" ch", " kan."));
}

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
        r->info = formatTime (d->getLengthSeconds()) + "  " + channelsText (d->getNumChannels());
        r->current = i == currentIndex;
        r->modified = d->modified;
        r->repaint();
    }
    resized();
}

void TracksPanel::postCallback (std::function<void (int)> TracksPanel::* callback, int index)
{
    juce::Component::SafePointer<TracksPanel> safe (this);
    juce::MessageManager::callAsync ([safe, callback, index]
    {
        if (safe != nullptr && (safe.getComponent()->*callback))
            (safe.getComponent()->*callback) (index);
    });
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

void FilePanel::resized()
{
    moreButton.setBounds (getHeaderButtonArea());
}

void FilePanel::paint (juce::Graphics& g)
{
    Card::paint (g);

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
        { T ("Channels", "Kanaler"), channelsText (doc->getNumChannels()) },
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
        {
            r->editing = false;
            r->nameEditor.setText (doc->markers[(size_t) r->index].name, false);
        }
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
    nameEditor.onTextChange = [this] { nameEdited(); };
    nameEditor.onReturnKey = [this] { nameEditor.giveAwayKeyboardFocus(); };
    nameEditor.onEscapeKey = [this] { nameEditor.giveAwayKeyboardFocus(); };
    // the edit session (one undo step) ends when the field loses focus
    nameEditor.onFocusLost = [this] { editing = false; };
    addAndMakeVisible (nameEditor);

    deleteButton.setColours (juce::Colours::transparentBlack, Theme::textDim);
    deleteButton.setTooltip (T ("Delete marker", "Slet markør"));
    deleteButton.onClick = [this]
    {
        auto* d = owner.doc;
        const int mi = findMarker();
        if (d == nullptr || mi < 0) return;
        d->pushUndo (T ("Delete marker", "Slet markør"), true);
        d->markers.erase (d->markers.begin() + mi);
        d->annotationsChanged();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MarkersPanel> (&owner)]
        {
            if (safe != nullptr) { safe->refresh(); if (safe->onChanged) safe->onChanged(); }
        });
    };
    addAndMakeVisible (deleteButton);

    colourButton.setTooltip (T ("Change the marker colour", "Skift markørens farve"));
    colourButton.onClick = [this]
    {
        const int mi = findMarker();
        if (mi >= 0) owner.chooseColour (mi, colourButton.getScreenBounds());
    };
    addAndMakeVisible (colourButton);
}

int MarkersPanel::Row::findMarker() const
{
    auto* d = owner.doc;
    if (d == nullptr) return -1;
    const int n = (int) d->markers.size();
    if (! editing)
        return juce::isPositiveAndBelow (index, n) ? index : -1;

    auto matches = [d, this] (int i)
    {
        auto& m = d->markers[(size_t) i];
        return m.time == editTime && m.name == editName;
    };
    if (juce::isPositiveAndBelow (index, n) && matches (index)) return index;
    for (int i = 0; i < n; ++i)
        if (matches (i)) return i;
    return -1;
}

void MarkersPanel::Row::nameEdited()
{
    auto* d = owner.doc;
    if (d == nullptr) return;

    int mi = findMarker();
    if (mi < 0 && editing)
    {
        // lost track of the marker (e.g. undo while typing): start a new session on this row
        editing = false;
        mi = findMarker();
    }
    if (mi < 0) return;

    auto& m = d->markers[(size_t) mi];
    const auto newName = nameEditor.getText();
    if (m.name == newName) return;

    if (! editing)
    {
        d->pushUndo (T ("Rename marker", "Omdøb markør"), true);
        editing = true;
    }
    m.name = newName;
    editTime = m.time;
    editName = newName;
    d->modified = true;
    if (owner.onChanged) owner.onChanged();
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
    juce::Component::SafePointer<MarkersPanel> safe (this);
    auto* d = doc;
    // identify the marker by time + name in case the list changes while the picker is open
    const double markerTime = doc->markers[(size_t) index].time;
    const juce::String markerName = doc->markers[(size_t) index].name;
    auto undoPushed = std::make_shared<bool> (false);   // pushed lazily on the first real change

    showColourPicker (screenArea, doc->markers[(size_t) index].colour, [safe, d, index, markerTime, markerName, undoPushed] (juce::Colour c)
    {
        if (safe == nullptr || safe->doc != d) return;
        auto matches = [&] (int i) { return d->markers[(size_t) i].time == markerTime && d->markers[(size_t) i].name == markerName; };
        int mi = -1;
        if (juce::isPositiveAndBelow (index, (int) d->markers.size()) && matches (index)) mi = index;
        for (int i = 0; mi < 0 && i < (int) d->markers.size(); ++i)
            if (matches (i)) mi = i;
        if (mi < 0 || d->markers[(size_t) mi].colour == c) return;

        if (! *undoPushed)
        {
            d->pushUndo (T ("Marker colour", "Markørfarve"), true);
            *undoPushed = true;
        }
        d->markers[(size_t) mi].colour = c;
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
