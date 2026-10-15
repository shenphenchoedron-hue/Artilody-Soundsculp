#include "ExportDialog.h"

static juce::String sizeText (double bytes)
{
    if (bytes >= 1024.0 * 1024.0) return juce::String (bytes / (1024.0 * 1024.0), 1) + " MB";
    return juce::String (juce::jmax (1.0, bytes / 1024.0), 0) + " KB";
}

static juce::String channelsText (int n)
{
    return n == 1 ? juce::String ("Mono") : (n == 2 ? juce::String ("Stereo") : juce::String (n) + T (" ch", " kan."));
}

// Removes a trailing audio file extension ("song.wav" -> "song") but keeps other
// dots ("Mr. Smith" stays "Mr. Smith").
static juce::String stripAudioExtension (const juce::String& name)
{
    const int dot = name.lastIndexOfChar ('.');
    if (dot <= 0) return name;
    const auto ext = name.substring (dot + 1).trim().toLowerCase();

    static const char* known[] = { "wav", "wave", "flac", "ogg", "oga", "mp3", "aiff", "aif", "aifc", "m4a", "opus", "aac", "wma", "caf" };
    bool isAudio = false;
    for (auto* k : known)
        if (ext == k) isAudio = true;
    for (auto& f : FileIO::getExportFormats())
        if (ext == f.extension.toLowerCase().trimCharactersAtStart (".")) isAudio = true;

    return isAudio ? name.substring (0, dot).trimEnd() : name;
}

//==============================================================================
ExportDialog::ScopeCard::ScopeCard (const juce::String& i, const juce::String& t)
    : juce::Button (t), icon (i), title (t)
{
    setClickingTogglesState (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ExportDialog::ScopeCard::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    const bool enabled = isEnabled();

    g.setColour (on ? Theme::accentSoft : (highlighted && enabled ? Theme::background : Theme::panel));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (on ? Theme::accent : Theme::border);
    g.drawRoundedRectangle (b, 10.0f, on ? 2.0f : 1.0f);

    auto alpha = enabled ? 1.0f : 0.4f;
    auto r = b.reduced (14.0f, 10.0f);
    Icons::draw (g, icon, r.removeFromTop (26.0f).removeFromLeft (26.0f), (on ? Theme::accent : Theme::textDim).withMultipliedAlpha (alpha));
    r.removeFromTop (6.0f);
    g.setColour (Theme::text.withMultipliedAlpha (alpha));
    g.setFont (Theme::font (15.0f, true));
    g.drawText (title, r.removeFromTop (20.0f), juce::Justification::centredLeft, true);
    g.setColour (Theme::textDim.withMultipliedAlpha (alpha));
    g.setFont (Theme::font (12.5f));
    g.drawText (detail, r.removeFromTop (18.0f), juce::Justification::centredLeft, true);

    if (on)
    {
        auto tick = juce::Rectangle<float> (18.0f, 18.0f).withPosition (b.getRight() - 26.0f, b.getY() + 8.0f);
        g.setColour (Theme::accent);
        g.fillEllipse (tick);
        juce::Path p;
        p.startNewSubPath (tick.getX() + 4.5f, tick.getCentreY());
        p.lineTo (tick.getX() + 8.0f, tick.getBottom() - 5.0f);
        p.lineTo (tick.getRight() - 4.0f, tick.getY() + 5.0f);
        g.setColour (juce::Colours::white);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
ExportDialog::ExportDialog (const Options& o) : options (o)
{
    trackCard.detail = o.hasTrack ? formatTime (o.trackSeconds) + "  " + o.trackName : T ("No track", "Intet spor");
    selectionCard.detail = o.hasSelection ? formatTime (o.selectionSeconds) : T ("Nothing selected", "Intet markeret");
    mixCard.detail = o.multipleTracks ? formatTime (o.mixSeconds) + "  " + T ("one file", "én fil") : T ("Needs 2+ tracks", "Kræver 2+ spor");
    trackCard.setEnabled (o.hasTrack);
    selectionCard.setEnabled (o.hasSelection);
    mixCard.setEnabled (o.multipleTracks);
    trackCard.onClick = [this] { setScope (0); };
    selectionCard.onClick = [this] { setScope (1); };
    mixCard.onClick = [this] { setScope (2); };
    addAndMakeVisible (trackCard);
    addAndMakeVisible (selectionCard);
    addAndMakeVisible (mixCard);

    // formats grouped by kind
    auto& formats = FileIO::getExportFormats();
    auto addGroup = [&] (const juce::String& heading, std::initializer_list<const char*> ids)
    {
        formatBox.addSectionHeading (heading);
        for (auto* id : ids)
            for (int i = 0; i < (int) formats.size(); ++i)
                if (formats[(size_t) i].id == id)
                    formatBox.addItem (formats[(size_t) i].name, i + 1);
    };
    addGroup (T ("Uncompressed", "Ukomprimeret"), { "wav16", "wav24", "wav32", "aiff24" });
    addGroup (T ("Lossless (smaller, same quality)", "Tabsfri (mindre, samme kvalitet)"), { "flac16", "flac24" });
    addGroup (T ("Compressed (smallest files)", "Komprimeret (mindste filer)"), { "ogg" });

    int initialId = 2;   // WAV 24-bit
    for (int i = 0; i < (int) formats.size(); ++i)
        if (formats[(size_t) i].id == o.lastFormatId)
            initialId = i + 1;
    formatBox.setSelectedId (initialId, juce::dontSendNotification);
    formatBox.onChange = [this] { formatChanged(); };
    addAndMakeVisible (formatBox);

    for (auto* ed : { &nameEditor, &folderEditor })
    {
        ed->setFont (Theme::font (14.5f));
        ed->setIndents (10, 0);
        ed->setJustification (juce::Justification::centredLeft);
        ed->setColour (juce::TextEditor::backgroundColourId, Theme::panel);
        ed->setColour (juce::TextEditor::outlineColourId, Theme::border);
        ed->setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
        addAndMakeVisible (*ed);
    }
    folderEditor.setText (o.folder.getFullPathName(), false);

    browseButton.setButtonText (T ("Choose...", "Vælg..."));
    browseButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (T ("Choose export folder", "Vælg eksportmappe"),
                                                       juce::File (folderEditor.getText()));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File())
                folderEditor.setText (fc.getResult().getFullPathName(), false);
        });
    };
    addAndMakeVisible (browseButton);

    cancelButton.setButtonText (T ("Cancel", "Annuller"));
    cancelButton.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0);
    };
    exportButton.setButtonText (T ("Export", "Eksportér"));
    exportButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    exportButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    exportButton.onClick = [this] { doExport(); };
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (exportButton);

    // start on the requested scope if it is available, otherwise the first enabled one
    int s = juce::jlimit (0, 2, o.initialScope);
    if (! isScopeAvailable (s))
        for (int i = 0; i < 3; ++i)
            if (isScopeAvailable (i)) { s = i; break; }
    setScope (s);
    setSize (640, 410);
}

const FileIO::ExportFormat* ExportDialog::selectedFormat() const
{
    auto& formats = FileIO::getExportFormats();
    int idx = formatBox.getSelectedId() - 1;
    return juce::isPositiveAndBelow (idx, (int) formats.size()) ? &formats[(size_t) idx] : nullptr;
}

juce::String ExportDialog::defaultBaseName() const
{
    if (scope == 2) return "Mix";
    auto base = stripAudioExtension (juce::File::createLegalFileName (options.trackName).trim());
    if (base.isEmpty()) base = "export";
    return scope == 1 ? base + "_selection" : base;
}

bool ExportDialog::isScopeAvailable (int s) const
{
    return s == 0 ? options.hasTrack : (s == 1 ? options.hasSelection : (s == 2 && options.multipleTracks));
}

void ExportDialog::updateExportEnabled()
{
    exportButton.setEnabled (isScopeAvailable (scope) && selectedFormat() != nullptr);
}

void ExportDialog::setScope (int s)
{
    scope = s;
    trackCard.setToggleState (s == 0, juce::dontSendNotification);
    selectionCard.setToggleState (s == 1, juce::dontSendNotification);
    mixCard.setToggleState (s == 2, juce::dontSendNotification);
    if (auto* f = selectedFormat())
        nameEditor.setText (defaultBaseName() + "." + f->extension, false);
    updateExportEnabled();
    updateInfo();
}

void ExportDialog::formatChanged()
{
    if (auto* f = selectedFormat())
    {
        auto base = stripAudioExtension (nameEditor.getText().trim());
        if (base.isEmpty()) base = defaultBaseName();
        nameEditor.setText (base + "." + f->extension, false);
    }
    updateExportEnabled();
    updateInfo();
}

void ExportDialog::updateInfo()
{
    auto* f = selectedFormat();
    if (f == nullptr) { infoText.clear(); repaint(); return; }

    const double secs = scope == 0 ? options.trackSeconds : (scope == 1 ? options.selectionSeconds : options.mixSeconds);
    const double rate = scope == 0 ? options.trackRate : (scope == 1 ? options.selectionRate : options.mixRate);
    const int chans = juce::jmax (1, scope == 0 ? options.trackChannels : (scope == 1 ? options.selectionChannels : options.mixChannels));
    const double pcm = secs * rate * chans * f->bitDepth / 8.0;
    double bytes = pcm;
    if (f->id.startsWith ("flac"))  bytes = pcm * 0.6;
    else if (f->id == "ogg")        bytes = secs * 192000.0 / 8.0;

    infoText = formatTime (secs) + "   |   " + juce::String (rate / 1000.0, 1) + " kHz   |   "
               + channelsText (chans) + "   |   "
               + T ("approx. ", "ca. ") + sizeText (bytes)
               + T ("   |   Volume lines are included", "   |   Volumenlinjer medregnes");
    repaint();
}

void ExportDialog::doExport()
{
    auto* f = selectedFormat();
    if (f == nullptr || ! isScopeAvailable (scope)) return;

    auto name = stripAudioExtension (juce::File::createLegalFileName (nameEditor.getText().trim()));
    if (name.isEmpty()) name = defaultBaseName();
    auto folder = juce::File (folderEditor.getText().trim());
    if (! juce::File::isAbsolutePath (folderEditor.getText().trim()) || ! (folder.isDirectory() || folder.createDirectory()))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, T ("Export", "Eksport"),
                                                T ("Please choose a valid folder.", "Vælg en gyldig mappe."));
        return;
    }
    // append (never replace) the extension, so dots in the name are kept
    auto file = folder.getChildFile (name + "." + f->extension.trimCharactersAtStart ("."));

    const int formatIndex = formatBox.getSelectedId() - 1;
    const int sc = scope;
    auto cb = onExport;
    juce::Component::SafePointer<ExportDialog> safe (this);

    auto go = [safe, cb, sc, formatIndex, file]
    {
        if (safe != nullptr)
            if (auto* dw = safe->findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (1);
        if (cb) cb (sc, formatIndex, file);
    };

    if (file.existsAsFile())
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, T ("Export", "Eksport"),
                                            file.getFileName() + T (" already exists. Replace it?", " findes allerede. Vil du erstatte den?"),
                                            T ("Replace", "Erstat"), T ("Cancel", "Annuller"), this,
                                            juce::ModalCallbackFunction::create ([go] (int r) { if (r == 1) go(); }));
        return;
    }
    go();
}

void ExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);

    auto label = [&g] (juce::Rectangle<int> r, const juce::String& t)
    {
        g.setColour (Theme::text);
        g.setFont (Theme::font (14.0f, true));
        g.drawText (t, r, juce::Justification::centredLeft);
    };
    label (scopeLabelArea, T ("What to export", "Hvad skal eksporteres"));
    label (formatLabelArea, T ("Format", "Format"));
    label (nameLabelArea, T ("File name", "Filnavn"));
    label (folderLabelArea, T ("Folder", "Mappe"));

    g.setColour (Theme::background);
    g.fillRoundedRectangle (infoArea.toFloat(), 8.0f);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (infoText, infoArea.reduced (12, 0), juce::Justification::centredLeft, true);

    g.setColour (Theme::border);
    g.drawHorizontalLine (getHeight() - 64, 0.0f, (float) getWidth());
}

void ExportDialog::resized()
{
    auto r = getLocalBounds().reduced (22, 18);

    scopeLabelArea = r.removeFromTop (22);
    r.removeFromTop (6);
    auto cards = r.removeFromTop (100);
    const int cw = (cards.getWidth() - 20) / 3;
    trackCard.setBounds (cards.removeFromLeft (cw));
    cards.removeFromLeft (10);
    selectionCard.setBounds (cards.removeFromLeft (cw));
    cards.removeFromLeft (10);
    mixCard.setBounds (cards);

    r.removeFromTop (16);
    auto row = [&r] (juce::Rectangle<int>& labelArea) -> juce::Rectangle<int>
    {
        auto line = r.removeFromTop (34);
        labelArea = line.removeFromLeft (100);
        r.removeFromTop (10);
        return line;
    };
    formatBox.setBounds (row (formatLabelArea));
    nameEditor.setBounds (row (nameLabelArea));
    auto folderRow = row (folderLabelArea);
    browseButton.setBounds (folderRow.removeFromRight (100));
    folderRow.removeFromRight (8);
    folderEditor.setBounds (folderRow);

    infoArea = r.removeFromTop (34);

    auto buttons = getLocalBounds().removeFromBottom (64).reduced (22, 14);
    exportButton.setBounds (buttons.removeFromRight (130));
    buttons.removeFromRight (10);
    cancelButton.setBounds (buttons.removeFromRight (110));
}
