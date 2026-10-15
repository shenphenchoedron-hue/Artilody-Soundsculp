#include "MainComponent.h"
#include "DSP.h"

//==============================================================================
// Small dialog helpers
//==============================================================================
static void askForValue (const juce::String& title, const juce::String& message, const juce::String& defaultValue,
                         std::function<void (const juce::String&)> callback)
{
    auto* aw = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor ("value", defaultValue);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton (T ("Cancel", "Annuller"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, callback] (int result)
    {
        if (result == 1)
            callback (aw->getTextEditorContents ("value"));
    }), true);
}

static void closeDialog (juce::Component* c)
{
    if (auto* dw = c->findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

static void launchDialog (juce::Component* content, const juce::String& title, juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (content);
    o.dialogTitle = title;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = parent;
    o.launchAsync();
}

//==============================================================================
// Pitch dialog: semitone slider with preview / apply
//==============================================================================
class PitchDialog : public juce::Component
{
public:
    std::function<void (double)> onPreview, onApply;
    std::function<void()> onStopPreview;

    PitchDialog()
    {
        info.setText (T ("Raise or lower the pitch without changing the speed or length.",
                         "Hæv eller sænk tonen uden at ændre hastighed eller længde."), juce::dontSendNotification);
        info.setFont (Theme::font (14.0f));
        addAndMakeVisible (info);

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setRange (-12.0, 12.0, 0.1);
        slider.setValue (0.0);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 110, 28);
        slider.textFromValueFunction = [] (double v)
        {
            return (v > 0 ? "+" : "") + juce::String (v, 1) + T (" semitones", " halvtoner");
        };
        slider.valueFromTextFunction = [] (const juce::String& t)
        {
            return t.retainCharacters ("-+0123456789.,").replaceCharacter (',', '.').getDoubleValue();
        };
        addAndMakeVisible (slider);

        for (auto* b : { &minusOct, &minusOne, &plusOne, &plusOct })
            addAndMakeVisible (b);
        minusOct.setButtonText ("-12"); minusOne.setButtonText ("-1"); plusOne.setButtonText ("+1"); plusOct.setButtonText ("+12");
        minusOct.onClick = [this] { slider.setValue (-12.0); };
        plusOct.onClick  = [this] { slider.setValue (12.0); };
        minusOne.onClick = [this] { slider.setValue (slider.getValue() - 1.0); };
        plusOne.onClick  = [this] { slider.setValue (slider.getValue() + 1.0); };

        preview.setButtonText (T ("Preview", "Forhør"));
        apply.setButtonText (T ("Apply", "Anvend"));
        cancel.setButtonText (T ("Cancel", "Annuller"));
        apply.setColour (juce::TextButton::buttonColourId, Theme::accent);
        apply.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        preview.onClick = [this] { if (onPreview) onPreview (slider.getValue()); };
        apply.onClick = [this] { auto v = slider.getValue(); if (onStopPreview) onStopPreview(); closeDialog (this); if (onApply) onApply (v); };
        cancel.onClick = [this] { if (onStopPreview) onStopPreview(); closeDialog (this); };
        addAndMakeVisible (preview); addAndMakeVisible (apply); addAndMakeVisible (cancel);
        setSize (520, 190);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18);
        info.setBounds (r.removeFromTop (26));
        r.removeFromTop (8);
        slider.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        auto quick = r.removeFromTop (30);
        for (auto* b : { &minusOct, &minusOne, &plusOne, &plusOct })
        {
            b->setBounds (quick.removeFromLeft (60));
            quick.removeFromLeft (8);
        }
        auto buttons = r.removeFromBottom (32);
        cancel.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        apply.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        preview.setBounds (buttons.removeFromRight (100));
    }

private:
    juce::Label info;
    juce::Slider slider;
    juce::TextButton minusOct, minusOne, plusOne, plusOct, preview, apply, cancel;
};

//==============================================================================
// Merge dialog: choose tracks to mix into a new track
//==============================================================================
class MergeDialog : public juce::Component
{
public:
    std::function<void (std::vector<int>, bool closeOthers)> onMerge;

    MergeDialog (const std::vector<AudioDocument*>& docs)
    {
        info.setText (T ("Choose the tracks to merge (mix) into one new track. Volume lines are included.",
                         "Vælg de spor der skal flettes (mixes) til ét nyt spor. Volumenlinjer medregnes."),
                      juce::dontSendNotification);
        info.setFont (Theme::font (14.0f));
        addAndMakeVisible (info);

        for (auto* d : docs)
        {
            auto* t = toggles.add (new juce::ToggleButton (d->name + "   (" + formatTime (d->getLengthSeconds()) + ")"));
            t->setToggleState (d->getLength() > 0, juce::dontSendNotification);
            t->setEnabled (d->getLength() > 0);
            addAndMakeVisible (t);
        }
        closeOthers.setButtonText (T ("Close the merged tracks afterwards", "Luk de flettede spor bagefter"));
        addAndMakeVisible (closeOthers);

        merge.setButtonText (T ("Merge", "Flet"));
        merge.setColour (juce::TextButton::buttonColourId, Theme::accent);
        merge.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        cancel.setButtonText (T ("Cancel", "Annuller"));
        merge.onClick = [this]
        {
            std::vector<int> chosen;
            for (int i = 0; i < toggles.size(); ++i)
                if (toggles[i]->getToggleState()) chosen.push_back (i);
            auto close = closeOthers.getToggleState();
            auto cb = onMerge;
            closeDialog (this);
            if (cb) cb (chosen, close);
        };
        cancel.onClick = [this] { closeDialog (this); };
        addAndMakeVisible (merge);
        addAndMakeVisible (cancel);
        setSize (480, 150 + toggles.size() * 30);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18);
        info.setBounds (r.removeFromTop (40));
        for (auto* t : toggles) t->setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        closeOthers.setBounds (r.removeFromTop (28));
        auto buttons = r.removeFromBottom (32);
        cancel.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        merge.setBounds (buttons.removeFromRight (100));
    }

private:
    juce::Label info;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::ToggleButton closeOthers;
    juce::TextButton merge, cancel;
};

//==============================================================================
MainComponent::MainComponent (juce::ApplicationProperties& props) : properties (props)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    addAndMakeVisible (header);
    addAndMakeVisible (toolbar);
    addAndMakeVisible (tracksPanel);
    addAndMakeVisible (filePanel);
    addAndMakeVisible (markersPanel);
    addAndMakeVisible (waveform);
    addAndMakeVisible (overview);
    addAndMakeVisible (eqPanel);
    addAndMakeVisible (noisePanel);
    addAndMakeVisible (statusBar);

    // ---- audio
    auto* settings = properties.getUserSettings();
    std::unique_ptr<juce::XmlElement> audioState (settings->getXmlValue ("audioDeviceState"));
    auto err = engine.initialise (audioState.get());
    engine.onTransportChanged = [this] { timerCallback(); };

    // ---- header
    header.recordButton.onClick = [this] { toggleRecord(); };
    header.homeButton.onClick   = [this] { goToStart(); };
    header.playButton.onClick   = [this] { play(); };
    header.stopButton.onClick   = [this] { stop(); };
    header.loopButton.onClick   = [this] { engine.setLooping (header.loopButton.getToggleState()); updateLoopRange(); };
    header.undoButton.onClick   = [this] { undo(); };
    header.redoButton.onClick   = [this] { redo(); };
    header.zoomInButton.onClick = [this] { waveform.zoom (2.0); };
    header.zoomOutButton.onClick = [this] { waveform.zoom (0.5); };
    header.addButton.onClick    = [this] { showTracksMenu(); };
    header.exportButton.onClick = [this] { showExportMenu(); };
    header.settingsButton.onClick = [this] { showSettingsMenu(); };
    header.trackBox.onChange    = [this] { selectDocument (header.trackBox.getSelectedItemIndex()); };
    header.zoomSlider.onValueChange = [this] { setZoomFromSlider (header.zoomSlider.getValue()); };

    // ---- toolbar
    toolbar.onTool = [this] (const juce::String& id) { handleTool (id); };
    toolbar.get ("select")->setToggleState (true, juce::dontSendNotification);
    toolbar.get ("eq")->setToggleState (true, juce::dontSendNotification);
    toolbar.get ("noise")->setToggleState (true, juce::dontSendNotification);

    // ---- side panels
    tracksPanel.onSelect = [this] (int i) { selectDocument (i); };
    tracksPanel.onClose = [this] (int i) { closeDocument (i); };
    tracksPanel.onAddMenu = [this] { showTracksMenu(); };
    filePanel.onMenu = [this] { showFileMenu(); };
    markersPanel.onAdd = [this] { addMarkerAtCursor(); };
    markersPanel.onJump = [this] (int i)
    {
        if (auto* d = current())
            if (juce::isPositiveAndBelow (i, (int) d->markers.size()))
            {
                d->selStart = d->selEnd = d->cursor = d->toSamples (d->markers[(size_t) i].time);
                waveform.ensureVisible ((double) d->cursor);
                if (engine.isPlaying()) engine.setPosition (d->markers[(size_t) i].time);
                waveform.repaint();
                updateStatus();
            }
    };
    markersPanel.onChanged = [this] { waveform.repaint(); overview.repaint(); refreshTrackList(); };

    // ---- waveform
    waveform.onSelectionChanged = [this]
    {
        if (auto* d = current())
            if (engine.isPlaying() && ! d->hasSelection() && ! engine.getMixAll())
                engine.setPosition (d->toSeconds (d->cursor));
        updateLoopRange();
        updateStatus();
        overview.repaint();
        updateNoiseMaterial();
    };
    waveform.onViewChanged = [this] { overview.repaint(); updateZoomControls(); };
    waveform.onMarkersChanged = [this] (int idx)
    {
        markersPanel.refresh();
        if (idx >= 0) markersPanel.focusMarker (idx);
        overview.repaint();
    };
    waveform.onEnvelopeEdited = [this] { overview.repaint(); };

    // ---- effect panels
    eqPanel.onChange = [this] { engine.setEQ (eqPanel.getSettings(), eqPanel.isEQEnabled()); };
    eqPanel.onApply = [this] { applyEQ(); };
    eqPanel.onClose = [this] { toolbar.get ("eq")->setToggleState (false, juce::dontSendNotification); setPanelsVisible (false, noisePanel.isVisible()); };
    noisePanel.onLearn = [this] { learnNoise(); };
    noisePanel.onPreview = [this] { previewNoise(); };
    noisePanel.onApply = [this] { applyNoise(); };
    noisePanel.onClose = [this] { toolbar.get ("noise")->setToggleState (false, juce::dontSendNotification); setPanelsVisible (eqPanel.isVisible(), false); };
    engine.setEQ (eqPanel.getSettings(), false);

    // ---- status bar
    statusBar.zoomSlider.onValueChange = [this] { setZoomFromSlider (statusBar.zoomSlider.getValue()); };
    statusBar.zoomInButton.onClick = [this] { waveform.zoom (2.0); };
    statusBar.zoomOutButton.onClick = [this] { waveform.zoom (0.5); };
    statusBar.zoomBox.onChange = [this]
    {
        auto pct = statusBar.zoomBox.getSelectedId();
        if (pct > 0 && current() != nullptr)
        {
            auto* d = current();
            waveform.setSamplesPerPixel (waveform.getFitSamplesPerPixel() * 100.0 / pct,
                                         d->hasSelection() ? (double) d->selStart : (double) d->cursor);
        }
    };
    statusBar.mixToggle.onClick = [this] { engine.setMixAll (statusBar.mixToggle.getToggleState()); updateLoopRange(); };

    setPanelsVisible (settings->getBoolValue ("showEQ", true), settings->getBoolValue ("showNoise", true));
    toolbar.get ("eq")->setToggleState (eqPanel.isVisible(), juce::dontSendNotification);
    toolbar.get ("noise")->setToggleState (noisePanel.isVisible(), juce::dontSendNotification);

    refreshAll();
    startTimerHz (30);

    if (err.isNotEmpty())
        showMessage (T ("Audio device problem: ", "Problem med lydenhed: ") + err);

    setSize (1600, 940);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.stop();
    engine.stopRecording();
    engine.setDocuments ({}, nullptr);
    for (auto& d : documents) d->removeChangeListener (this);
}

void MainComponent::saveSettings()
{
    auto* s = properties.getUserSettings();
    if (auto xml = engine.getDeviceManager().createStateXml())
        s->setValue ("audioDeviceState", xml.get());
    s->setValue ("showEQ", eqPanel.isVisible());
    s->setValue ("showNoise", noisePanel.isVisible());
    properties.saveIfNeeded();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // dark card around waveform + overview
    auto card = waveCard.toFloat();
    g.setColour (Theme::waveBg);
    g.fillRoundedRectangle (card, 10.0f);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (80));
    toolbar.setBounds (r.removeFromTop (56));
    statusBar.setBounds (r.removeFromBottom (40));

    r.reduce (12, 10);
    leftColumn = r.removeFromLeft (getWidth() < 1400 ? 230 : 260);
    r.removeFromLeft (12);

    const bool showRight = eqPanel.isVisible() || noisePanel.isVisible();
    if (showRight)
    {
        rightColumn = r.removeFromRight (getWidth() < 1400 ? 340 : 400);
        r.removeFromRight (12);
        auto rc = rightColumn;
        if (eqPanel.isVisible() && noisePanel.isVisible())
        {
            eqPanel.setBounds (rc.removeFromTop (rc.getHeight() * 47 / 100));
            rc.removeFromTop (12);
            noisePanel.setBounds (rc);
        }
        else if (eqPanel.isVisible())  eqPanel.setBounds (rc.removeFromTop (juce::jmin (rc.getHeight(), 480)));
        else                           noisePanel.setBounds (rc.removeFromTop (juce::jmin (rc.getHeight(), 420)));
    }

    {
        auto lc = leftColumn;
        tracksPanel.setBounds (lc.removeFromTop (juce::jlimit (130, 260, 60 + (int) documents.size() * 40)));
        lc.removeFromTop (12);
        filePanel.setBounds (lc.removeFromTop (lc.getHeight() > 560 ? 236 : 210));
        lc.removeFromTop (12);
        markersPanel.setBounds (lc);
    }

    waveCard = r;
    auto w = r.reduced (0, 4);
    overview.setBounds (w.removeFromBottom (96).reduced (4, 0));
    waveform.setBounds (w.reduced (4, 4));
    updateZoomControls();
}

void MainComponent::setPanelsVisible (bool eq, bool noise)
{
    eqPanel.setVisible (eq);
    noisePanel.setVisible (noise);
    resized();
    repaint();
    if (noise) updateNoiseMaterial();
}

//==============================================================================
// Documents
//==============================================================================
AudioDocument* MainComponent::current() const
{
    return juce::isPositiveAndBelow (currentIndex, (int) documents.size()) ? documents[(size_t) currentIndex].get() : nullptr;
}

std::vector<AudioDocument*> MainComponent::allDocuments() const
{
    std::vector<AudioDocument*> v;
    for (auto& d : documents) v.push_back (d.get());
    return v;
}

AudioDocument* MainComponent::addDocument (std::unique_ptr<AudioDocument> d)
{
    d->addChangeListener (this);
    documents.push_back (std::move (d));
    selectDocument ((int) documents.size() - 1);
    resized();
    return documents.back().get();
}

void MainComponent::selectDocument (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) documents.size()))
        index = documents.empty() ? -1 : juce::jlimit (0, (int) documents.size() - 1, index);

    if (index != currentIndex && engine.isPlaying() && ! engine.getMixAll())
        engine.stop();

    currentIndex = index;
    syncEngine();
    refreshAll();
}

void MainComponent::closeDocument (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) documents.size()))
        return;

    auto doClose = [this, index]
    {
        if (! juce::isPositiveAndBelow (index, (int) documents.size())) return;
        if (engine.isRecording()) engine.stopRecording();
        engine.stop();

        auto victim = std::move (documents[(size_t) index]);
        documents.erase (documents.begin() + index);
        if (currentIndex >= (int) documents.size()) currentIndex = (int) documents.size() - 1;
        else if (currentIndex > index) --currentIndex;
        syncEngine();                          // engine no longer references the victim
        victim->removeChangeListener (this);
        victim.reset();
        refreshAll();
        resized();
    };

    if (documents[(size_t) index]->modified && documents[(size_t) index]->getLength() > 0)
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
                                            T ("Close track", "Luk spor"),
                                            T ("The track has changes that are not exported. Close it anyway?",
                                               "Sporet har ændringer der ikke er eksporteret. Vil du lukke det alligevel?"),
                                            T ("Close", "Luk"), T ("Cancel", "Annuller"), this,
                                            juce::ModalCallbackFunction::create ([doClose] (int r) { if (r == 1) doClose(); }));
    }
    else
    {
        doClose();
    }
}

void MainComponent::syncEngine()
{
    engine.setDocuments (allDocuments(), current());
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == current())
    {
        filePanel.repaint();
        markersPanel.refresh();
        waveform.repaint();
        overview.repaint();
        updateStatus();
    }
    refreshTrackList();
}

void MainComponent::refreshTrackList()
{
    header.trackBox.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) documents.size(); ++i)
        header.trackBox.addItem ((documents[(size_t) i]->modified ? "* " : "") + documents[(size_t) i]->name, i + 1);
    header.trackBox.setSelectedItemIndex (currentIndex, juce::dontSendNotification);
    tracksPanel.setTracks (allDocuments(), currentIndex);

    if (onTitleChanged)
        onTitleChanged (current() != nullptr ? current()->name + " - Artilody Soundsculp" : juce::String ("Artilody Soundsculp"));
}

void MainComponent::refreshAll()
{
    auto* d = current();
    waveform.setDocument (d);
    overview.setDocument (d);
    filePanel.setDocument (d);
    markersPanel.setDocument (d);
    refreshTrackList();
    updateLoopRange();
    updateStatus();
    updateZoomControls();
    updateNoiseMaterial();
    repaint();
}

//==============================================================================
// Files
//==============================================================================
void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> list;
    for (auto& f : files) list.add (juce::File (f));
    openFiles (list);
}

void MainComponent::openFiles (const juce::Array<juce::File>& files)
{
    juce::StringArray errors;
    juce::MouseCursor::showWaitCursor();
    for (auto& f : files)
    {
        auto doc = std::make_unique<AudioDocument>();
        juce::String err;
        if (FileIO::loadIntoDocument (f, *doc, err))
        {
            addDocument (std::move (doc));
            properties.getUserSettings()->setValue ("lastDir", f.getParentDirectory().getFullPathName());
        }
        else
            errors.add (err);
    }
    juce::MouseCursor::hideWaitCursor();

    if (! errors.isEmpty())
        showError (T ("Could not open", "Kunne ikke åbne"), errors.joinIntoString ("\n"));
}

void MainComponent::chooseFilesToOpen()
{
    auto dir = juce::File (properties.getUserSettings()->getValue ("lastDir",
                           juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));
    chooser = std::make_unique<juce::FileChooser> (T ("Open audio files", "Åbn lydfiler"), dir, FileIO::getWildcardForOpen());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc) { openFiles (fc.getResults()); });
}

void MainComponent::chooseFileToInsert()
{
    if (! requireDocument()) return;
    auto dir = juce::File (properties.getUserSettings()->getValue ("lastDir",
                           juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));
    chooser = std::make_unique<juce::FileChooser> (T ("Insert audio file into track", "Indsæt lydfil i spor"), dir,
                                                   FileIO::getWildcardForOpen());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        AudioDocument temp;
        juce::String err;
        juce::MouseCursor::showWaitCursor();
        bool ok = FileIO::loadIntoDocument (f, temp, err);
        juce::MouseCursor::hideWaitCursor();
        if (! ok) { showError (T ("Could not open", "Kunne ikke åbne"), err); return; }
        juce::AudioBuffer<float> data;
        data.makeCopyOf (temp.buffer);
        insertBufferAtCursor (std::move (data), temp.sampleRate, T ("Insert file", "Indsæt fil"));
        showMessage (T ("Inserted ", "Indsatte ") + f.getFileName());
    });
}

void MainComponent::newEmptyTrack()
{
    auto d = std::make_unique<AudioDocument>();
    d->name = T ("Track ", "Spor ") + juce::String (emptyTrackCounter++);
    d->formatName = T ("New track", "Nyt spor");
    d->bitDepth = 24;
    auto rate = current() != nullptr ? current()->sampleRate : 44100.0;
    juce::AudioBuffer<float> empty (2, 0);
    d->setBuffer (std::move (empty), rate);
    d->modified = false;
    addDocument (std::move (d));
    showMessage (T ("Empty track created. Click a position and paste (Ctrl+V) or insert a file.",
                    "Tomt spor oprettet. Klik en position og indsæt (Ctrl+V) eller indsæt en fil."));
}

void MainComponent::duplicateTrack()
{
    if (! requireDocument()) return;
    auto* src = current();
    auto d = std::make_unique<AudioDocument>();
    d->name = src->name + T (" (copy)", " (kopi)");
    d->formatName = src->formatName;
    d->bitDepth = src->bitDepth;
    juce::AudioBuffer<float> b;
    b.makeCopyOf (src->buffer);
    d->setBuffer (std::move (b), src->sampleRate);
    d->envelope = src->envelope;
    d->markers = src->markers;
    d->modified = true;
    addDocument (std::move (d));
}

void MainComponent::renameTrack()
{
    if (! requireDocument()) return;
    juce::Component::SafePointer<MainComponent> safe (this);
    auto* d = current();
    askForValue (T ("Rename track", "Omdøb spor"), T ("New name:", "Nyt navn:"), d->name, [safe, d] (const juce::String& v)
    {
        if (safe == nullptr || v.trim().isEmpty()) return;
        for (auto& doc : safe->documents)
            if (doc.get() == d) { d->name = v.trim(); safe->refreshAll(); }
    });
}

void MainComponent::showMergeDialog()
{
    if (documents.size() < 2)
    {
        showError (T ("Merge tracks", "Flet spor"), T ("Open or create at least two tracks first.", "Åbn eller opret mindst to spor først."));
        return;
    }
    auto* dlg = new MergeDialog (allDocuments());
    juce::Component::SafePointer<MainComponent> safe (this);
    dlg->onMerge = [safe] (std::vector<int> chosen, bool closeOthers)
    {
        if (safe == nullptr || chosen.size() < 1) return;
        std::vector<AudioDocument*> tracks;
        for (auto i : chosen)
            if (juce::isPositiveAndBelow (i, (int) safe->documents.size()))
                tracks.push_back (safe->documents[(size_t) i].get());

        double rate = 44100.0;
        bool normalised = false;
        juce::MouseCursor::showWaitCursor();
        auto mix = safe->mixTracks (tracks, rate, normalised);
        juce::MouseCursor::hideWaitCursor();

        auto d = std::make_unique<AudioDocument>();
        d->name = T ("Merged track", "Flettet spor");
        d->formatName = T ("Mix", "Mix");
        d->bitDepth = 24;
        // keep markers from all merged tracks
        for (auto* t : tracks)
            for (auto& m : t->markers) d->markers.push_back (m);
        d->setBuffer (std::move (mix), rate);
        d->sortMarkers();
        d->nextMarkerColour = (int) d->markers.size();
        d->modified = true;

        if (closeOthers)
        {
            safe->engine.stop();
            for (int i = (int) chosen.size(); --i >= 0;)
            {
                auto idx = chosen[(size_t) i];
                auto victim = std::move (safe->documents[(size_t) idx]);
                safe->documents.erase (safe->documents.begin() + idx);
                safe->currentIndex = -1;
                safe->syncEngine();
                victim->removeChangeListener (safe.getComponent());
            }
        }
        safe->addDocument (std::move (d));
        safe->showMessage (T ("Tracks merged", "Spor flettet") + juce::String (normalised ? T (" (level reduced to avoid clipping)", " (niveau sænket for at undgå forvrængning)") : ""));
    };
    launchDialog (dlg, T ("Merge tracks", "Flet spor"), this);
}

juce::AudioBuffer<float> MainComponent::mixTracks (const std::vector<AudioDocument*>& tracks, double& rateOut, bool& normalised)
{
    double rate = 0.0;
    int channels = 1;
    for (auto* t : tracks)
    {
        rate = juce::jmax (rate, t->sampleRate);
        channels = juce::jmax (channels, t->getNumChannels());
    }
    if (rate <= 0) rate = 44100.0;
    channels = juce::jmin (channels, 2);

    std::vector<juce::AudioBuffer<float>> parts;
    int length = 0;
    for (auto* t : tracks)
    {
        juce::AudioBuffer<float> b;
        b.makeCopyOf (t->buffer);
        DSP::applyEnvelope (b, *t, 0);
        auto r = DSP::resample (b, t->sampleRate / rate);
        auto m = DSP::matchChannels (r, channels);
        length = juce::jmax (length, m.getNumSamples());
        parts.push_back (std::move (m));
    }

    juce::AudioBuffer<float> mix (channels, length);
    mix.clear();
    for (auto& p : parts)
        for (int c = 0; c < channels; ++c)
            mix.addFrom (c, 0, p, c, 0, p.getNumSamples());

    normalised = false;
    if (DSP::getPeak (mix) > 1.0f)
    {
        DSP::normalize (mix, -0.3f);
        normalised = true;
    }
    rateOut = rate;
    return mix;
}

//==============================================================================
// Menus
//==============================================================================
void MainComponent::showTracksMenu()
{
    juce::PopupMenu m;
    m.addItem (1, T ("New empty track", "Nyt tomt spor"));
    m.addItem (2, T ("Open audio files...", "Åbn lydfiler...") + "\tCtrl+O");
    m.addItem (3, T ("Insert audio file at cursor...", "Indsæt lydfil ved positionen..."), current() != nullptr);
    m.addSeparator();
    m.addItem (4, T ("Duplicate track", "Duplikér spor"), current() != nullptr);
    m.addItem (5, T ("Rename track...", "Omdøb spor..."), current() != nullptr);
    m.addItem (6, T ("Merge tracks into one track...", "Flet spor til ét spor..."), documents.size() > 1);
    m.addSeparator();
    m.addItem (7, T ("Close track", "Luk spor"), current() != nullptr);

    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
    {
        switch (r)
        {
            case 1: newEmptyTrack(); break;
            case 2: chooseFilesToOpen(); break;
            case 3: chooseFileToInsert(); break;
            case 4: duplicateTrack(); break;
            case 5: renameTrack(); break;
            case 6: showMergeDialog(); break;
            case 7: closeDocument (currentIndex); break;
            default: break;
        }
    });
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu m;
    m.addItem (1, T ("Rename track...", "Omdøb spor..."), current() != nullptr);
    m.addItem (2, T ("Show file in folder", "Vis filen i mappe"), current() != nullptr && current()->file.existsAsFile());
    m.addItem (3, T ("Reset volume line", "Nulstil volumenlinje"), current() != nullptr && ! current()->envelope.empty());
    m.addItem (4, T ("Remove all markers", "Fjern alle markører"), current() != nullptr && ! current()->markers.empty());
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
    {
        auto* d = current();
        if (d == nullptr) return;
        if (r == 1) renameTrack();
        if (r == 2) d->file.revealToUser();
        if (r == 3) { d->pushUndo (T ("Reset volume line", "Nulstil volumenlinje")); d->resetEnvelope(); d->annotationsChanged(); }
        if (r == 4) { d->pushUndo (T ("Remove markers", "Fjern markører")); d->markers.clear(); d->annotationsChanged(); }
    });
}

void MainComponent::showExportMenu()
{
    if (documents.empty())
    {
        showError (T ("Export", "Eksport"), T ("There is nothing to export yet.", "Der er intet at eksportere endnu."));
        return;
    }

    auto& formats = FileIO::getExportFormats();
    const bool hasFFmpeg = FileIO::findFFmpeg() != juce::File();
    auto makeFormatMenu = [&] (int base)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) formats.size(); ++i)
        {
            auto& f = formats[(size_t) i];
            sub.addItem (base + i, f.name + (f.needsFFmpeg && ! hasFFmpeg ? T ("  (needs ffmpeg)", "  (kræver ffmpeg)") : juce::String()),
                         ! f.needsFFmpeg || hasFFmpeg);
        }
        return sub;
    };

    juce::PopupMenu m;
    m.addSectionHeader (T ("Export", "Eksportér"));
    m.addSubMenu (T ("Current track", "Aktuelt spor"), makeFormatMenu (100), current() != nullptr);
    m.addSubMenu (T ("Selection only", "Kun markering"), makeFormatMenu (200), current() != nullptr && current()->hasSelection());
    m.addSubMenu (T ("All tracks mixed into one file", "Alle spor mixet til én fil"), makeFormatMenu (300), documents.size() > 1);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.exportButton), [this] (int r)
    {
        auto& fmts = FileIO::getExportFormats();
        if (r < 100) return;
        int scope = r / 100 - 1;
        int idx = r % 100;
        if (juce::isPositiveAndBelow (idx, (int) fmts.size()))
            exportAudio (scope, fmts[(size_t) idx]);
    });
}

void MainComponent::exportAudio (int scope, const FileIO::ExportFormat& format)
{
    auto* d = current();
    juce::String baseName = scope == 2 ? T ("Mix", "Mix") : (d != nullptr ? juce::File::createLegalFileName (d->name) : juce::String ("export"));
    baseName = juce::File (baseName).getFileNameWithoutExtension();
    if (scope == 1) baseName << T ("_selection", "_markering");

    auto dir = juce::File (properties.getUserSettings()->getValue ("exportDir",
                           properties.getUserSettings()->getValue ("lastDir",
                               juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName())));
    auto target = dir.getChildFile (baseName + "." + format.extension);

    chooser = std::make_unique<juce::FileChooser> (T ("Export as ", "Eksportér som ") + format.name, target, "*." + format.extension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this, scope, format] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (! file.hasFileExtension (format.extension))
            file = file.withFileExtension (format.extension);

        juce::AudioBuffer<float> data;
        double rate = 44100.0;
        bool normalised = false;
        auto* doc = current();

        juce::MouseCursor::showWaitCursor();
        if (scope == 2)
        {
            data = mixTracks (allDocuments(), rate, normalised);
        }
        else if (doc != nullptr)
        {
            juce::int64 s = 0, e = doc->getLength();
            if (scope == 1 && doc->hasSelection()) { s = doc->selStart; e = doc->selEnd; }
            data = doc->copyRange (s, e);
            DSP::applyEnvelope (data, *doc, s);
            rate = doc->sampleRate;
        }

        juce::String err;
        bool ok = FileIO::writeBuffer (data, rate, file, format, err);
        juce::MouseCursor::hideWaitCursor();

        if (! ok)
        {
            showError (T ("Export failed", "Eksport fejlede"), err);
            return;
        }

        properties.getUserSettings()->setValue ("exportDir", file.getParentDirectory().getFullPathName());
        if (scope == 0 && doc != nullptr)
        {
            doc->modified = false;
            refreshTrackList();
        }
        showMessage (T ("Exported ", "Eksporterede ") + file.getFileName()
                     + (normalised ? T (" (level reduced to avoid clipping)", " (niveau sænket for at undgå forvrængning)") : juce::String()));
    });
}

void MainComponent::showSettingsMenu()
{
    juce::PopupMenu lang;
    lang.addItem (11, "English", true, ! isDanish());
    lang.addItem (12, "Dansk", true, isDanish());

    juce::PopupMenu m;
    m.addItem (1, T ("Audio settings...", "Lydindstillinger..."));
    m.addSubMenu (T ("Language", "Sprog"), lang);
    m.addItem (2, T ("Keyboard shortcuts", "Tastaturgenveje"));
    m.addSeparator();
    m.addItem (3, T ("About Artilody Soundsculp", "Om Artilody Soundsculp"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.settingsButton), [this] (int r)
    {
        if (r == 1) showAudioSettings();
        if (r == 2) showShortcuts();
        if (r == 11 || r == 12)
        {
            properties.getUserSettings()->setValue ("language", r == 12 ? "da" : "en");
            properties.saveIfNeeded();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, r == 12 ? "Sprog" : "Language",
                                                    r == 12 ? juce::String::fromUTF8 ("Genstart programmet for at skifte sprog.")
                                                            : juce::String ("Restart the program to change the language."));
        }
        if (r == 3)
        {
            auto ff = FileIO::findFFmpeg();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Artilody Soundsculp",
                "Artilody Soundsculp " + juce::String (JUCE_APPLICATION_VERSION_STRING) + "\n\n"
                + T ("Simple multi-track audio editor.", "Simpel lydredigering med flere spor.") + "\n"
                + "https://github.com/shenphenchoedron-hue/Artilody-Soundsculp\n\n"
                + "ffmpeg: " + (ff == juce::File() ? T ("not found (MP3/M4A/Opus export disabled)", "ikke fundet (MP3/M4A/Opus-eksport slået fra)")
                                                   : ff.getFullPathName())
                + "\n\nLicense: GPL-3.0. Built with JUCE.");
        }
    });
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 1, 2, false, false, true, false);
    selector->setSize (520, 420);
    launchDialog (selector, T ("Audio settings", "Lydindstillinger"), this);
}

void MainComponent::showShortcuts()
{
    juce::String text = T (
        "Space\tPlay / stop\nR\tRecord\nHome\tGo to start\nL\tLoop on/off\nM\tAdd marker at cursor\n"
        "Ctrl+A\tSelect all\nCtrl+X / C / V\tCut / copy / paste\nDelete\tDelete selection\nCtrl+T\tTrim to selection\n"
        "Ctrl+Z / Ctrl+Y\tUndo / redo\nCtrl+O\tOpen files\nCtrl+N\tNew empty track\nCtrl+E\tExport\n+ / -\tZoom in / out\n0\tZoom to fit\n"
        "Ctrl+mouse wheel\tZoom at mouse\nDouble-click\tSelect between markers",
        "Mellemrum\tAfspil / stop\nR\tOptag\nHome\tGå til start\nL\tLoop til/fra\nM\tTilføj markør ved positionen\n"
        "Ctrl+A\tMarkér alt\nCtrl+X / C / V\tKlip / kopiér / indsæt\nDelete\tSlet markering\nCtrl+T\tTrim til markering\n"
        "Ctrl+Z / Ctrl+Y\tFortryd / gentag\nCtrl+O\tÅbn filer\nCtrl+N\tNyt tomt spor\nCtrl+E\tEksportér\n+ / -\tZoom ind / ud\n0\tZoom til hele sporet\n"
        "Ctrl+musehjul\tZoom ved musen\nDobbeltklik\tMarkér mellem markører");
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, T ("Keyboard shortcuts", "Tastaturgenveje"),
                                            text.replace ("\t", "  -  "));
}

//==============================================================================
// Editing
//==============================================================================
bool MainComponent::requireDocument()
{
    if (current() != nullptr && ! engine.isRecording())
        return true;
    if (engine.isRecording())
        showMessage (T ("Stop the recording first.", "Stop optagelsen først."));
    else
        showMessage (T ("Open a file or create a track first (+).", "Åbn en fil eller opret et spor først (+)."));
    return false;
}

bool MainComponent::requireSelection (const juce::String& what)
{
    if (! requireDocument()) return false;
    if (current()->hasSelection()) return true;
    showMessage (what + ": " + T ("select an area with the mouse first.", "markér først et område med musen."));
    return false;
}

void MainComponent::getTargetRange (juce::int64& s, juce::int64& e) const
{
    auto* d = current();
    if (d->hasSelection()) { s = d->selStart; e = d->selEnd; }
    else                   { s = 0; e = d->getLength(); }
}

void MainComponent::handleTool (const juce::String& id)
{
    if (id == "select" || id == "volume")
    {
        bool vol = id == "volume";
        waveform.setTool (vol ? WaveformView::Tool::volume : WaveformView::Tool::select);
        toolbar.get ("select")->setToggleState (! vol, juce::dontSendNotification);
        toolbar.get ("volume")->setToggleState (vol, juce::dontSendNotification);
        if (vol) showMessage (T ("Volume tool: click on the waveform to add points, drag them up/down, right-click to delete.",
                                 "Volumenværktøj: klik på bølgeformen for at tilføje punkter, træk dem op/ned, højreklik for at slette."));
        return;
    }
    if (id == "eq" || id == "noise")
    {
        auto* b = toolbar.get (id);
        b->setToggleState (! b->getToggleState(), juce::dontSendNotification);
        setPanelsVisible (toolbar.get ("eq")->getToggleState(), toolbar.get ("noise")->getToggleState());
        return;
    }
    if (id == "trim")      doTrim();
    if (id == "cut")       doCut();
    if (id == "copy")      doCopy();
    if (id == "paste")     doPaste();
    if (id == "fadein")    doFade (true);
    if (id == "fadeout")   doFade (false);
    if (id == "normalize") doNormalize();
    if (id == "silence")   doSilenceTrim();
    if (id == "looptool")  doLoopTool();
    if (id == "reverse")   doReverse();
    if (id == "pitch")     doPitch();
}

void MainComponent::doTrim()
{
    if (! requireSelection (T ("Trim", "Trim"))) return;
    auto* d = current();
    auto s = d->selStart, e = d->selEnd;
    d->pushUndo (T ("Trim", "Trim"));
    auto keep = d->copyRange (s, e);
    // remove after, then before (keeps markers/envelope consistent)
    d->replaceRange (e, d->getLength(), juce::AudioBuffer<float> (d->getNumChannels(), 0), {});
    d->replaceRange (0, s, juce::AudioBuffer<float> (d->getNumChannels(), 0), {});
    d->selStart = 0; d->selEnd = d->getLength(); d->cursor = 0;
    waveform.zoomToFit();
    showMessage (T ("Trimmed to selection", "Trimmet til markeringen"));
}

void MainComponent::doCopy()
{
    if (! requireSelection (T ("Copy", "Kopiér"))) return;
    auto* d = current();
    clipboard = d->copyRange (d->selStart, d->selEnd);
    clipboardRate = d->sampleRate;
    showMessage (T ("Copied ", "Kopierede ") + formatTime (d->toSeconds (d->selEnd - d->selStart)));
}

void MainComponent::doCut()
{
    if (! requireSelection (T ("Cut", "Klip"))) return;
    auto* d = current();
    clipboard = d->copyRange (d->selStart, d->selEnd);
    clipboardRate = d->sampleRate;
    d->replaceRange (d->selStart, d->selEnd, juce::AudioBuffer<float> (d->getNumChannels(), 0), T ("Cut", "Klip"));
    showMessage (T ("Cut ", "Klippede ") + formatTime ((double) clipboard.getNumSamples() / clipboardRate));
}

void MainComponent::doDelete()
{
    if (! requireSelection (T ("Delete", "Slet"))) return;
    auto* d = current();
    d->replaceRange (d->selStart, d->selEnd, juce::AudioBuffer<float> (d->getNumChannels(), 0), T ("Delete", "Slet"));
}

void MainComponent::insertBufferAtCursor (juce::AudioBuffer<float> data, double rate, const juce::String& undoName)
{
    auto* d = current();
    if (d == nullptr) return;

    // Empty tracks adopt the inserted material's format
    if (d->getLength() == 0 && d->cursor == 0)
    {
        d->pushUndo (undoName);
        d->setBuffer (std::move (data), rate);
        d->selStart = 0; d->selEnd = d->getLength();
        d->modified = true;
        waveform.zoomToFit();
        refreshAll();
        return;
    }

    if (std::abs (rate - d->sampleRate) > 0.5)
        data = DSP::resample (data, rate / d->sampleRate);
    data = DSP::matchChannels (data, juce::jmax (1, d->getNumChannels()));

    d->pushUndo (undoName);
    juce::int64 s = d->hasSelection() ? d->selStart : d->cursor;
    juce::int64 e = d->hasSelection() ? d->selEnd : d->cursor;

    if (s > d->getLength())
    {
        // Pad with silence up to the cursor position
        const auto pad = s - d->getLength();
        juce::AudioBuffer<float> silence (juce::jmax (1, d->getNumChannels()), (int) pad);
        silence.clear();
        if (d->getNumChannels() == 0)
        {
            juce::AudioBuffer<float> b (data.getNumChannels(), 0);
            d->setBuffer (std::move (b), d->sampleRate);
        }
        d->replaceRange (d->getLength(), d->getLength(), silence, {});
        e = s;
    }
    d->replaceRange (s, e, data, {});
    waveform.ensureVisible ((double) s);
}

void MainComponent::doPaste()
{
    if (! requireDocument()) return;
    if (clipboard.getNumSamples() == 0)
    {
        showMessage (T ("The clipboard is empty - copy or cut something first.", "Udklipsholderen er tom - kopiér eller klip noget først."));
        return;
    }
    juce::AudioBuffer<float> data;
    data.makeCopyOf (clipboard);
    insertBufferAtCursor (std::move (data), clipboardRate, T ("Paste", "Indsæt"));
    showMessage (T ("Pasted ", "Indsatte ") + formatTime ((double) clipboard.getNumSamples() / clipboardRate));
}

void MainComponent::doFade (bool in)
{
    if (! requireDocument()) return;
    auto* d = current();
    juce::int64 s, e;
    if (d->hasSelection()) { s = d->selStart; e = d->selEnd; }
    else
    {
        // No selection: fade the first / last 3 seconds
        auto n = juce::jmin (d->getLength(), d->toSamples (3.0));
        s = in ? 0 : d->getLength() - n;
        e = in ? n : d->getLength();
    }
    auto seg = d->copyRange (s, e);
    if (in) DSP::fadeIn (seg); else DSP::fadeOut (seg);
    d->replaceRange (s, e, seg, in ? T ("Fade in", "Fade ind") : T ("Fade out", "Fade ud"));
}

void MainComponent::doNormalize()
{
    if (! requireDocument()) return;
    auto* d = current();
    juce::int64 s, e;
    getTargetRange (s, e);
    auto seg = d->copyRange (s, e);
    auto before = DSP::getPeak (seg);
    if (before <= 1.0e-6f) { showMessage (T ("Nothing to normalize (silence).", "Intet at normalisere (stilhed).")); return; }
    DSP::normalize (seg, -0.1f);
    d->replaceRange (s, e, seg, T ("Normalize", "Normalisér"));
    if (! (s == 0 && e == d->getLength())) { d->selStart = s; d->selEnd = e; }
    auto gainDb = -0.1f - juce::Decibels::gainToDecibels (before);
    showMessage (T ("Normalized: ", "Normaliseret: ") + (gainDb >= 0 ? "+" : "") + juce::String (gainDb, 1) + " dB");
}

void MainComponent::doSilenceTrim()
{
    if (! requireDocument()) return;
    juce::Component::SafePointer<MainComponent> safe (this);
    askForValue (T ("Silence trim", "Stilhedstrim"),
                 T ("Removes silence at the start and end of the track (or selection).\nThreshold in dB (e.g. -50):",
                    "Fjerner stilhed i starten og slutningen af sporet (eller markeringen).\nTærskel i dB (fx -50):"),
                 "-50", [safe] (const juce::String& v)
    {
        if (safe == nullptr || safe->current() == nullptr) return;
        auto* d = safe->current();
        auto thr = juce::jlimit (-120.0f, 0.0f, v.replaceCharacter (',', '.').getFloatValue());
        juce::int64 s, e;
        safe->getTargetRange (s, e);
        auto seg = d->copyRange (s, e);
        juce::int64 first, last;
        if (! DSP::findNonSilentRange (seg, thr, first, last))
        {
            safe->showMessage (T ("Everything is below the threshold - nothing removed.", "Alt er under tærsklen - intet fjernet."));
            return;
        }
        // keep a few ms around the sound
        auto margin = d->toSamples (0.005);
        first = std::max<juce::int64> (0, first - margin);
        last = std::min<juce::int64> (seg.getNumSamples() - 1, last + margin);
        auto removed = (double) (seg.getNumSamples() - (last - first + 1)) / d->sampleRate;
        if (removed <= 0.0005) { safe->showMessage (T ("No silence found.", "Ingen stilhed fundet.")); return; }

        d->pushUndo (T ("Silence trim", "Stilhedstrim"));
        d->replaceRange (s + last + 1, e, juce::AudioBuffer<float> (d->getNumChannels(), 0), {});
        d->replaceRange (s, s + first, juce::AudioBuffer<float> (d->getNumChannels(), 0), {});
        d->selStart = d->selEnd = d->cursor = s;
        safe->showMessage (T ("Removed ", "Fjernede ") + juce::String (removed, 2) + T (" s of silence", " s stilhed"));
    });
}

void MainComponent::doLoopTool()
{
    if (! requireSelection (T ("Loop", "Loop"))) return;
    juce::Component::SafePointer<MainComponent> safe (this);
    askForValue (T ("Loop", "Loop"), T ("How many times should the selection be played in total? (2-100)",
                                        "Hvor mange gange skal markeringen spilles i alt? (2-100)"),
                 "4", [safe] (const juce::String& v)
    {
        if (safe == nullptr || safe->current() == nullptr) return;
        auto* d = safe->current();
        int n = juce::jlimit (2, 100, v.getIntValue());
        auto seg = d->copyRange (d->selStart, d->selEnd);
        if ((juce::int64) seg.getNumSamples() * n > std::numeric_limits<int>::max() / 2) return;

        // tiny crossfade at the joints to avoid clicks
        const int xf = juce::jmin (seg.getNumSamples() / 4, (int) (d->sampleRate * 0.003));
        juce::AudioBuffer<float> out (seg.getNumChannels(), seg.getNumSamples() * n);
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < seg.getNumChannels(); ++c)
            {
                out.copyFrom (c, i * seg.getNumSamples(), seg, c, 0, seg.getNumSamples());
                if (i > 0 && xf > 0)
                {
                    auto* p = out.getWritePointer (c, i * seg.getNumSamples());
                    for (int k = 0; k < xf; ++k) p[k] *= (float) k / (float) xf;
                    auto* q = out.getWritePointer (c, i * seg.getNumSamples() - xf);
                    for (int k = 0; k < xf; ++k) q[k] *= 1.0f - (float) k / (float) xf;
                }
            }
        d->replaceRange (d->selStart, d->selEnd, out, T ("Loop", "Loop"));
        safe->showMessage (T ("Selection repeated ", "Markeringen gentaget ") + juce::String (n) + T (" times", " gange"));
    });
}

void MainComponent::doReverse()
{
    if (! requireDocument()) return;
    auto* d = current();
    juce::int64 s, e;
    getTargetRange (s, e);
    auto seg = d->copyRange (s, e);
    DSP::reverse (seg);
    d->replaceRange (s, e, seg, T ("Reverse", "Baglæns"));
    if (! (s == 0 && e == d->getLength())) { d->selStart = s; d->selEnd = e; }
}

void MainComponent::doPitch()
{
    if (! requireDocument()) return;
    auto* dlg = new PitchDialog();
    juce::Component::SafePointer<MainComponent> safe (this);
    dlg->onPreview = [safe] (double st) { if (safe != nullptr) safe->previewPitch (st); };
    dlg->onApply = [safe] (double st) { if (safe != nullptr) safe->applyPitch (st); };
    dlg->onStopPreview = [safe] { if (safe != nullptr) safe->engine.stop(); };
    launchDialog (dlg, T ("Pitch", "Tone"), this);
}

void MainComponent::previewPitch (double semitones)
{
    auto* d = current();
    if (d == nullptr) return;
    juce::int64 s, e;
    getTargetRange (s, e);
    e = juce::jmin (e, s + d->toSamples (10.0));
    juce::MouseCursor::showWaitCursor();
    auto seg = DSP::pitchShift (d->copyRange (s, e), semitones);
    juce::MouseCursor::hideWaitCursor();
    engine.playPreview (std::move (seg), d->sampleRate);
}

void MainComponent::applyPitch (double semitones)
{
    auto* d = current();
    if (d == nullptr || std::abs (semitones) < 0.01) return;
    juce::int64 s, e;
    getTargetRange (s, e);
    juce::MouseCursor::showWaitCursor();
    auto seg = DSP::pitchShift (d->copyRange (s, e), semitones);
    juce::MouseCursor::hideWaitCursor();
    d->replaceRange (s, e, seg, T ("Pitch", "Tone"));
    if (! (s == 0 && e == d->getLength())) { d->selStart = s; d->selEnd = e; }
    showMessage (T ("Pitch changed ", "Tone ændret ") + (semitones > 0 ? "+" : "") + juce::String (semitones, 1) + T (" semitones", " halvtoner"));
}

void MainComponent::applyEQ()
{
    if (! requireDocument()) return;
    auto* d = current();
    juce::int64 s, e;
    getTargetRange (s, e);
    auto seg = d->copyRange (s, e);
    DSP::applyEQ (seg, d->sampleRate, eqPanel.getSettings());
    d->replaceRange (s, e, seg, "EQ");
    if (! (s == 0 && e == d->getLength())) { d->selStart = s; d->selEnd = e; }
    showMessage (T ("EQ applied. Turn the EQ switch off to avoid hearing it twice.",
                    "EQ anvendt. Slå EQ-kontakten fra for ikke at høre den to gange."));
}

void MainComponent::learnNoise()
{
    if (! requireSelection (T ("Learn noise profile", "Lær støjprofil")))
    {
        noisePanel.setLearnedProfile ({});
        return;
    }
    auto* d = current();
    noisePanel.setLearnedProfile (DSP::learnNoiseProfile (d->copyRange (d->selStart, d->selEnd)));
    showMessage (T ("Noise profile learned from the selection. Now select what to clean (or nothing for the whole track) and press Apply.",
                    "Støjprofil lært fra markeringen. Markér nu det der skal renses (eller intet for hele sporet) og tryk Anvend."));
}

std::vector<float> MainComponent::getNoiseProfile()
{
    if (noisePanel.hasLearnedProfile())
        return noisePanel.getLearnedProfile();
    return DSP::estimateNoiseProfile (current()->buffer);
}

void MainComponent::previewNoise()
{
    if (! requireDocument()) return;
    auto* d = current();
    if (engine.isPreviewing()) { engine.stop(); return; }
    juce::int64 s, e;
    getTargetRange (s, e);
    e = juce::jmin (e, s + d->toSamples (15.0));
    juce::MouseCursor::showWaitCursor();
    auto out = DSP::reduceNoise (d->copyRange (s, e), getNoiseProfile(), noisePanel.getParams());
    juce::MouseCursor::hideWaitCursor();
    engine.playPreview (std::move (out), d->sampleRate);
}

void MainComponent::applyNoise()
{
    if (! requireDocument()) return;
    auto* d = current();
    engine.stop();
    juce::int64 s, e;
    getTargetRange (s, e);
    auto params = noisePanel.getParams();
    params.outputNoiseOnly = false;
    juce::MouseCursor::showWaitCursor();
    auto out = DSP::reduceNoise (d->copyRange (s, e), getNoiseProfile(), params);
    juce::MouseCursor::hideWaitCursor();
    d->replaceRange (s, e, out, T ("Noise removal", "Støjfjernelse"));
    if (! (s == 0 && e == d->getLength())) { d->selStart = s; d->selEnd = e; }
    showMessage (T ("Noise removed", "Støj fjernet"));
}

void MainComponent::updateNoiseMaterial()
{
    if (! noisePanel.isVisible()) return;
    auto* d = current();
    if (d == nullptr || d->getLength() == 0)
    {
        noisePanel.setAnalysisMaterial ({});
        return;
    }
    juce::int64 s, e;
    getTargetRange (s, e);
    e = juce::jmin (e, s + d->toSamples (8.0));
    noisePanel.setAnalysisMaterial (d->copyRange (s, e));
}

void MainComponent::addMarkerAtCursor()
{
    if (! requireDocument()) return;
    auto* d = current();
    d->pushUndo (T ("Add marker", "Tilføj markør"));
    double t = engine.isPlaying() ? engine.getPosition() : d->toSeconds (d->cursor);
    int idx = d->addMarker (t);
    d->annotationsChanged();
    markersPanel.focusMarker (idx);
}

void MainComponent::undo()
{
    if (auto* d = current())
    {
        if (engine.isRecording()) return;
        auto desc = d->getUndoDescription();
        if (d->undo()) showMessage (T ("Undo: ", "Fortryd: ") + desc);
    }
}

void MainComponent::redo()
{
    if (auto* d = current())
    {
        if (engine.isRecording()) return;
        auto desc = d->getRedoDescription();
        if (d->redo()) showMessage (T ("Redo: ", "Gentag: ") + desc);
    }
}

//==============================================================================
// Transport
//==============================================================================
void MainComponent::togglePlay()
{
    if (engine.isPlaying() || engine.isPreviewing() || engine.isRecording()) stop();
    else play();
}

void MainComponent::play()
{
    auto* d = current();
    if (d == nullptr || engine.isRecording()) return;
    if (engine.isPlaying()) return;
    syncEngine();
    updateLoopRange();
    double start = d->toSeconds (d->hasSelection() ? d->selStart : d->cursor);
    double end = engine.getMixAll() ? 0.0 : d->getLengthSeconds();
    if (engine.getMixAll())
        for (auto& doc : documents) end = juce::jmax (end, doc->getLengthSeconds());
    if (start >= end - 0.01) start = 0.0;
    engine.setPosition (start);
    engine.play();
}

void MainComponent::stop()
{
    if (engine.isRecording())
    {
        engine.stopRecording();
        if (auto* d = current())
        {
            d->modified = true;
            d->clearHistory();
            waveform.zoomToFit();
        }
        refreshAll();
        showMessage (T ("Recording finished", "Optagelse afsluttet"));
        return;
    }
    engine.stop();
    waveform.repaint();
}

void MainComponent::goToStart()
{
    if (auto* d = current())
    {
        d->cursor = 0;
        if (! d->hasSelection()) d->selStart = d->selEnd = 0;
        engine.setPosition (d->hasSelection() && engine.isLooping() ? d->toSeconds (d->selStart) : 0.0);
        waveform.scrollTo (0.0);
        waveform.repaint();
        updateStatus();
    }
}

void MainComponent::toggleLoop()
{
    header.loopButton.setToggleState (! header.loopButton.getToggleState(), juce::dontSendNotification);
    engine.setLooping (header.loopButton.getToggleState());
    updateLoopRange();
}

void MainComponent::updateLoopRange()
{
    auto* d = current();
    if (d == nullptr) return;
    if (d->hasSelection())
        engine.setLoopRange (d->toSeconds (d->selStart), d->toSeconds (d->selEnd));
    else
    {
        double end = d->getLengthSeconds();
        if (engine.getMixAll())
            for (auto& doc : documents) end = juce::jmax (end, doc->getLengthSeconds());
        engine.setLoopRange (0.0, end);
    }
}

void MainComponent::toggleRecord()
{
    if (engine.isRecording()) { stop(); return; }
    engine.stop();

    auto d = std::make_unique<AudioDocument>();
    d->name = T ("Recording ", "Optagelse ") + juce::String (recordingCounter);
    d->formatName = T ("Recording", "Optagelse");
    d->bitDepth = 24;
    juce::AudioBuffer<float> empty (2, 0);
    d->setBuffer (std::move (empty), engine.getDeviceSampleRate());
    auto* doc = addDocument (std::move (d));

    juce::String err;
    if (! engine.startRecording (doc, err))
    {
        closeDocument (currentIndex);
        showError (T ("Recording", "Optagelse"), err);
        return;
    }
    ++recordingCounter;
    showMessage (T ("Recording... press Stop or R to finish.", "Optager... tryk Stop eller R for at afslutte."));
}

//==============================================================================
// Keyboard
//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const int code = key.getKeyCode();
    const auto mods = key.getModifiers();

    if (key == juce::KeyPress::spaceKey)                           { togglePlay(); return true; }
    if (key == juce::KeyPress::homeKey)                            { goToStart(); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { doDelete(); return true; }
    if (key == juce::KeyPress ('z', cmd, 0))                       { undo(); return true; }
    if (key == juce::KeyPress ('y', cmd, 0) || key == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)) { redo(); return true; }
    if (key == juce::KeyPress ('x', cmd, 0))                       { doCut(); return true; }
    if (key == juce::KeyPress ('c', cmd, 0))                       { doCopy(); return true; }
    if (key == juce::KeyPress ('v', cmd, 0))                       { doPaste(); return true; }
    if (key == juce::KeyPress ('t', cmd, 0))                       { doTrim(); return true; }
    if (key == juce::KeyPress ('o', cmd, 0))                       { chooseFilesToOpen(); return true; }
    if (key == juce::KeyPress ('n', cmd, 0))                       { newEmptyTrack(); return true; }
    if (key == juce::KeyPress ('e', cmd, 0))                       { showExportMenu(); return true; }
    if (key == juce::KeyPress ('a', cmd, 0))
    {
        if (auto* d = current()) { d->selStart = 0; d->selEnd = d->getLength(); waveform.repaint(); overview.repaint(); updateStatus(); updateLoopRange(); }
        return true;
    }
    if (! mods.isCommandDown() && ! mods.isAltDown())
    {
        if (code == 'R' || code == 'r') { toggleRecord(); return true; }
        if (code == 'L' || code == 'l') { toggleLoop(); return true; }
        if (code == 'M' || code == 'm') { addMarkerAtCursor(); return true; }
        if (code == '+' || code == '=') { waveform.zoom (2.0); return true; }
        if (code == '-')                { waveform.zoom (0.5); return true; }
        if (code == '0')                { waveform.zoomToFit(); return true; }
    }
    return false;
}

//==============================================================================
// Periodic UI updates
//==============================================================================
void MainComponent::timerCallback()
{
    const bool playing = engine.isPlaying() || engine.isPreviewing();
    header.playButton.setToggleState (playing, juce::dontSendNotification);
    header.recordButton.setToggleState (engine.isRecording(), juce::dontSendNotification);
    header.loopButton.setToggleState (engine.isLooping(), juce::dontSendNotification);

    auto* d = current();
    header.undoButton.setEnabled (d != nullptr && d->canUndo());
    header.redoButton.setEnabled (d != nullptr && d->canRedo());

    if (d != nullptr && engine.isPlaying())
    {
        auto sample = engine.getPosition() * d->sampleRate;
        auto vis = waveform.getVisibleSamples();
        if (sample > d->viewStart + vis || sample < d->viewStart)
            waveform.scrollTo (sample - vis * 0.02);
        waveform.repaint();
    }
    else if (engine.isRecording())
    {
        if (d != nullptr)
        {
            auto vis = waveform.getVisibleSamples();
            if ((double) d->getLength() > d->viewStart + vis * 0.95)
                waveform.scrollTo ((double) d->getLength() - vis * 0.7);
        }
        waveform.repaint();
    }

    static bool wasPlaying = false;
    if (wasPlaying && ! playing) waveform.repaint();
    wasPlaying = playing;

    if (transientMessage.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 7000)
        transientMessage.clear();

    updateStatus();
}

void MainComponent::updateStatus()
{
    auto* d = current();
    if (d == nullptr)
    {
        header.setSelectionInfo ("--:--.--- - --:--.---", "--:--.---");
        statusBar.setInfo ("", "--:--.---", "--:--.---", "", transientMessage);
        return;
    }

    juce::String sel = d->hasSelection() ? formatTime (d->toSeconds (d->selStart)) + " - " + formatTime (d->toSeconds (d->selEnd))
                                         : formatTime (d->toSeconds (d->cursor));
    juce::String selLen = d->hasSelection() ? formatTime (d->toSeconds (d->selEnd - d->selStart)) : juce::String();
    header.setSelectionInfo (sel, d->hasSelection() ? selLen : formatTime (d->getLengthSeconds()));

    juce::String fmt = juce::String (d->sampleRate / 1000.0, 1) + " kHz   |   " + juce::String (d->bitDepth) + " bit   |   "
                       + (d->getNumChannels() == 1 ? "Mono" : (d->getNumChannels() == 2 ? "Stereo" : juce::String (d->getNumChannels()) + " ch"));
    double cursorT = engine.isPlaying() ? engine.getPosition() : d->toSeconds (d->cursor);
    if (engine.isRecording()) cursorT = d->getLengthSeconds();
    statusBar.setInfo (fmt, formatTime (cursorT), d->hasSelection() ? sel : juce::String ("-"), selLen, transientMessage);
}

void MainComponent::updateZoomControls()
{
    auto* d = current();
    if (d == nullptr) return;
    const double fit = waveform.getFitSamplesPerPixel();
    const double spp = waveform.getSamplesPerPixel();
    const double minSpp = 0.05;
    double v = (fit <= minSpp) ? 0.0 : std::log (fit / spp) / std::log (fit / minSpp);
    v = juce::jlimit (0.0, 1.0, v);
    header.zoomSlider.setValue (v, juce::dontSendNotification);
    statusBar.zoomSlider.setValue (v, juce::dontSendNotification);
    auto pct = (int) std::round (fit / spp * 100.0);
    statusBar.zoomBox.setText (juce::String (pct) + "%", juce::dontSendNotification);
}

void MainComponent::setZoomFromSlider (double v)
{
    auto* d = current();
    if (d == nullptr) return;
    const double fit = waveform.getFitSamplesPerPixel();
    const double minSpp = 0.05;
    double spp = fit * std::pow (minSpp / fit, v);
    double anchor = d->hasSelection() ? (double) (d->selStart + d->selEnd) * 0.5 : (double) d->cursor;
    if (anchor < d->viewStart || anchor > d->viewStart + waveform.getVisibleSamples())
        anchor = d->viewStart + waveform.getVisibleSamples() * 0.5;
    waveform.setSamplesPerPixel (spp, anchor);
}

void MainComponent::showMessage (const juce::String& m)
{
    transientMessage = m;
    messageTime = juce::Time::getMillisecondCounter();
    updateStatus();
}

void MainComponent::showError (const juce::String& title, const juce::String& m)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, m);
}

bool MainComponent::hasUnsavedChanges() const
{
    for (auto& d : documents)
        if (d->modified && d->getLength() > 0) return true;
    return false;
}

//==============================================================================
// Demo content (used for screenshots) and self test
//==============================================================================
static juce::AudioBuffer<float> synthPiano (double sr, double seconds, const std::vector<std::pair<double, int>>& notes, juce::uint32 seed)
{
    juce::AudioBuffer<float> b (2, (int) (sr * seconds));
    b.clear();
    juce::Random rng ((juce::int64) seed);
    for (auto& [start, midi] : notes)
    {
        const double f = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        const int s0 = (int) (start * sr);
        const int len = juce::jmin (b.getNumSamples() - s0, (int) (sr * 2.2));
        const float pan = 0.5f + 0.35f * (float) std::sin (midi * 1.3);
        for (int i = 0; i < len; ++i)
        {
            const double t = i / sr;
            const float env = (float) (std::exp (-t * 2.2) * (1.0 - std::exp (-t * 300.0)));
            float v = 0.0f;
            for (int h = 1; h <= 5; ++h)
                v += (float) (std::sin (juce::MathConstants<double>::twoPi * f * h * t * (1.0 + 0.0004 * h)) / (h * h));
            v *= env * 0.28f;
            b.addSample (0, s0 + i, v * (1.2f - pan));
            b.addSample (1, s0 + i, v * (0.2f + pan));
        }
    }
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.addSample (c, i, (rng.nextFloat() - 0.5f) * 0.004f);
    DSP::normalize (b, -1.0f);
    return b;
}

void MainComponent::loadDemoContent()
{
    const double sr = 44100.0;
    std::vector<std::pair<double, int>> notes;
    const int chords[][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 65, 69, 72 }, { 67, 71, 74 } };
    for (int bar = 0; bar < 12; ++bar)
    {
        auto& ch = chords[bar % 4];
        for (int k = 0; k < 4; ++k)
            notes.push_back ({ bar * 1.0 + k * 0.25, ch[k % 3] + (k == 3 ? 12 : 0) });
    }

    auto d = std::make_unique<AudioDocument>();
    d->name = "electric_piano_phrase.wav";
    d->formatName = "WAV (PCM)";
    d->bitDepth = 24;
    d->fileSize = 21800000 / 10;
    d->setBuffer (synthPiano (sr, 12.68, notes, 1), sr);
    d->addMarker (0.0, "Intro");
    d->addMarker (3.24, T ("Verse", "Vers"));
    d->addMarker (7.48, T ("Chorus", "Omkvæd"));
    d->addMarker (11.2, "Outro");
    d->envelope = { { 0.0, 0.75f }, { 3.4, 0.95f }, { 5.4, 1.45f }, { 7.6, 1.0f }, { 10.5, 0.95f }, { 12.68, 0.95f } };
    d->selStart = d->toSamples (3.24);
    d->selEnd = d->toSamples (7.48);
    d->cursor = d->selStart;
    d->modified = false;
    d->clearHistory();
    addDocument (std::move (d));

    auto d2 = std::make_unique<AudioDocument>();
    d2->name = "vocal_take.wav";
    d2->formatName = "WAV (PCM)";
    std::vector<std::pair<double, int>> mel;
    for (int i = 0; i < 16; ++i) mel.push_back ({ 0.5 + i * 0.5, 72 + (i * 5) % 9 });
    d2->setBuffer (synthPiano (sr, 9.0, mel, 2), sr);
    d2->modified = false;
    d2->clearHistory();
    d2->addChangeListener (this);
    documents.push_back (std::move (d2));

    newEmptyTrack();
    selectDocument (0);
    transientMessage.clear();
    resized();
}

int MainComponent::runSelfTest()
{
    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << std::endl;
        if (! ok) ++failures;
    };

    const double sr = 44100.0;
    auto sine = [sr] (double f, double secs, float amp)
    {
        juce::AudioBuffer<float> b (1, (int) (sr * secs));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * f * i / sr));
        return b;
    };
    auto freqOf = [sr] (const juce::AudioBuffer<float>& b)
    {
        int crossings = 0;
        const int start = b.getNumSamples() / 4, end = b.getNumSamples() * 3 / 4;
        for (int i = start + 1; i < end; ++i)
            if (b.getSample (0, i - 1) < 0 && b.getSample (0, i) >= 0) ++crossings;
        return crossings / ((end - start) / sr);
    };

    // Pitch
    {
        auto in = sine (440.0, 2.0, 0.5f);
        auto up = DSP::pitchShift (in, 12.0);
        auto down = DSP::pitchShift (in, -5.0);
        check (up.getNumSamples() == in.getNumSamples(), "pitch shift keeps length");
        auto fu = freqOf (up), fd = freqOf (down);
        check (std::abs (fu - 880.0) < 10.0, "pitch +12 -> 880 Hz (got " + juce::String (fu, 1) + ")");
        check (std::abs (fd - 329.6) < 8.0, "pitch -5 -> 329.6 Hz (got " + juce::String (fd, 1) + ")");
    }
    // Normalize
    {
        auto b = sine (100.0, 0.5, 0.25f);
        DSP::normalize (b, -0.1f);
        check (std::abs (juce::Decibels::gainToDecibels (DSP::getPeak (b)) + 0.1f) < 0.05f, "normalize to -0.1 dB");
    }
    // Noise reduction
    {
        auto clean = sine (1000.0, 3.0, 0.3f);
        auto noisy = clean;
        noisy.makeCopyOf (clean);
        juce::Random r (5);
        for (int i = 0; i < noisy.getNumSamples(); ++i)
            noisy.addSample (0, i, (r.nextFloat() - 0.5f) * 0.05f);
        auto noiseOnly = juce::AudioBuffer<float> (1, (int) sr);
        for (int i = 0; i < noiseOnly.getNumSamples(); ++i) noiseOnly.setSample (0, i, (r.nextFloat() - 0.5f) * 0.05f);
        auto out = DSP::reduceNoise (noisy, DSP::learnNoiseProfile (noiseOnly), {});
        auto err = [&] (const juce::AudioBuffer<float>& x)
        {
            double e = 0;
            for (int i = 4096; i < x.getNumSamples() - 4096; ++i) { auto d = x.getSample (0, i) - clean.getSample (0, i); e += d * d; }
            return e;
        };
        check (err (out) < err (noisy) * 0.5, "noise reduction lowers noise (" + juce::String (err (noisy)) + " -> " + juce::String (err (out)) + ")");
    }
    // Document editing
    {
        AudioDocument doc;
        doc.setBuffer (sine (100.0, 10.0, 0.5f), sr);
        doc.addMarker (8.0, "B");
        doc.addEnvelopePoint (9.0, 0.5f);
        doc.replaceRange (doc.toSamples (2.0), doc.toSamples (4.0), juce::AudioBuffer<float> (1, 0), "cut");
        check (std::abs (doc.getLengthSeconds() - 8.0) < 1e-6, "cut shortens track");
        check (std::abs (doc.markers[0].time - 6.0) < 1e-6, "markers shift after cut");
        check (doc.undo() && std::abs (doc.getLengthSeconds() - 10.0) < 1e-6, "undo restores");
        check (doc.redo() && std::abs (doc.getLengthSeconds() - 8.0) < 1e-6, "redo re-applies");
    }
    // EQ
    {
        EQSettings s;
        s.bands[1].gainDb = 6.0f; s.bands[1].frequency = 1000.0f;
        check (std::abs (DSP::eqResponseDb (s, 48000.0, 1000.0f) - 6.0f) < 0.3f, "EQ bell +6 dB at 1 kHz");
    }
    // File round trips
    {
        auto b = sine (440.0, 1.0, 0.5f);
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("soundsculp_test");
        dir.createDirectory();
        for (auto& f : FileIO::getExportFormats())
        {
            if (f.needsFFmpeg && FileIO::findFFmpeg() == juce::File()) continue;
            auto file = dir.getChildFile ("t_" + f.id + "." + f.extension);
            juce::String err;
            bool ok = FileIO::writeBuffer (b, sr, file, f, err);
            AudioDocument doc;
            bool loaded = ok && FileIO::loadIntoDocument (file, doc, err);
            double len = doc.getLengthSeconds();
            check (ok && loaded && std::abs (len - 1.0) < 0.1, "export+import " + f.name + " (" + juce::String (len, 3) + " s) " + err);
        }
        dir.deleteRecursively();
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

//==============================================================================
int MainComponent::runUiTest()
{
    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << std::endl;
        if (! ok) ++failures;
    };

    loadDemoContent();
    check (documents.size() == 3, "demo has 3 tracks");

    // copy 1 s from the piano track
    selectDocument (0);
    auto* piano = current();
    piano->selStart = piano->toSamples (1.0);
    piano->selEnd = piano->toSamples (2.0);
    doCopy();
    check (clipboard.getNumSamples() == piano->toSamples (1.0), "copy 1 s");

    // paste into the empty track at 2 s -> pads with silence
    selectDocument (2);
    auto* empty = current();
    check (empty->getLength() == 0, "track 3 is empty");
    empty->cursor = empty->selStart = empty->selEnd = 0;
    doPaste();
    check (std::abs (empty->getLengthSeconds() - 1.0) < 0.001, "paste into empty track at 0 s");
    empty->selStart = empty->selEnd = 0;
    empty->cursor = empty->toSamples (3.0);
    doPaste();
    check (std::abs (empty->getLengthSeconds() - 4.0) < 0.001, "paste after end pads with silence (len " + juce::String (empty->getLengthSeconds(), 3) + ")");

    // cut / undo
    empty->selStart = 0; empty->selEnd = empty->toSamples (1.0);
    doCut();
    check (std::abs (empty->getLengthSeconds() - 3.0) < 0.001, "cut 1 s");
    undo();
    check (std::abs (empty->getLengthSeconds() - 4.0) < 0.001, "undo cut");

    // effects on selection
    empty->selStart = 0; empty->selEnd = empty->toSamples (1.0);
    doFade (true); doFade (false); doReverse(); doNormalize();
    check (std::abs (DSP::getPeak (empty->copyRange (0, empty->toSamples (1.0))) - juce::Decibels::decibelsToGain (-0.1f)) < 0.01f, "fade/reverse/normalize on selection");
    applyPitch (3.0);
    check (std::abs (empty->getLengthSeconds() - 4.0) < 0.001, "pitch keeps length");
    applyEQ();
    applyNoise();
    check (std::abs (empty->getLengthSeconds() - 4.0) < 0.001, "EQ + noise keep length");

    // trim
    selectDocument (1);
    auto* voc = current();
    voc->selStart = voc->toSamples (2.0); voc->selEnd = voc->toSamples (5.0);
    doTrim();
    check (std::abs (voc->getLengthSeconds() - 3.0) < 0.001, "trim to 3 s");

    // insert from another track with different sample rate (resampled)
    {
        juce::AudioBuffer<float> b (1, 48000);
        b.clear();
        voc->selStart = voc->selEnd = voc->cursor = 0;
        insertBufferAtCursor (std::move (b), 48000.0, "insert");
        check (std::abs (voc->getLengthSeconds() - 4.0) < 0.002, "insert 1 s @48k into 44.1k track");
    }

    // merge
    double rate = 0; bool norm = false;
    auto mix = mixTracks (allDocuments(), rate, norm);
    check (std::abs (mix.getNumSamples() / rate - piano->getLengthSeconds()) < 0.001, "mix length = longest track");
    check (DSP::getPeak (mix) <= 1.0f, "mix does not clip");

    // envelope
    piano->addEnvelopePoint (1.0, 0.0f);
    check (piano->gainAt (1.0) < 0.001f, "envelope point sets gain");

    std::cout << (failures == 0 ? "ALL UI TESTS PASSED" : juce::String (failures) + " UI TEST(S) FAILED") << std::endl;
    for (auto& d : documents) d->modified = false;
    return failures;
}
