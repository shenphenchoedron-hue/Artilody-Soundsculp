#include "MainComponent.h"
#include "DSP.h"
#include "ExportDialog.h"

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

static juce::DialogWindow* launchDialog (juce::Component* content, const juce::String& title, juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (content);
    o.dialogTitle = title;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = parent;
    return o.launchAsync();
}

// Buffers and AudioDocument ranges are limited to 32-bit sample counts.
static bool fitsInt (juce::int64 n)
{
    return n >= 0 && n <= (juce::int64) std::numeric_limits<int>::max();
}

static juce::String tooLongMessage()
{
    return T ("The audio is too long for this operation (more than 2^31 samples).",
              "Lyden er for lang til denne handling (mere end 2^31 samples).");
}

//==============================================================================
// Mixing (thread safe: works on copies, never on documents)
//==============================================================================
struct MixPart
{
    juce::AudioBuffer<float> audio;     // volume line already applied
    double rate = 44100.0;
};

static juce::AudioBuffer<float> mixParts (std::vector<MixPart>& parts, double& rateOut, bool& normalised, juce::String& error)
{
    double rate = 0.0;
    int channels = 1;
    for (auto& p : parts)
    {
        rate = juce::jmax (rate, p.rate);
        channels = juce::jmax (channels, p.audio.getNumChannels());
    }
    if (rate <= 0) rate = 44100.0;
    channels = juce::jmin (channels, 2);
    rateOut = rate;
    normalised = false;

    for (auto& p : parts)
        if (! fitsInt ((juce::int64) std::ceil ((double) p.audio.getNumSamples() * rate / juce::jmax (1.0, p.rate)) + 16))
        {
            error = tooLongMessage();
            return {};
        }

    int length = 0;
    for (auto& p : parts)
    {
        auto r = DSP::resample (p.audio, p.rate / rate);
        p.audio = DSP::matchChannels (r, channels);
        length = juce::jmax (length, p.audio.getNumSamples());
    }

    juce::AudioBuffer<float> mix (channels, length);
    mix.clear();
    for (auto& p : parts)
    {
        for (int c = 0; c < channels; ++c)
            mix.addFrom (c, 0, p.audio, c, 0, p.audio.getNumSamples());
        p.audio.setSize (0, 0);       // free memory early
    }

    if (DSP::getPeak (mix) > 1.0f)
    {
        DSP::normalize (mix, -0.3f);
        normalised = true;
    }
    return mix;
}

//==============================================================================
// Background export: mixing and writing run off the message
// thread. All audio is copied on the message thread before the job starts, so
// documents may be closed while the export runs.
//==============================================================================
class ExportJob : public juce::ThreadWithProgressWindow
{
public:
    // ok, error, normalised - called asynchronously on the message thread
    std::function<void (bool, const juce::String&, bool)> onDone;

    ExportJob (std::vector<MixPart>&& partsToMix, juce::AudioBuffer<float>&& singleBuffer, double singleRate,
               const FileIO::ExportFormat& fmt, const juce::File& dest, juce::Component* centreAround)
        : juce::ThreadWithProgressWindow (T ("Exporting...", "Eksporterer..."), true, false, 60000, {}, centreAround),
          parts (std::move (partsToMix)), data (std::move (singleBuffer)), rate (singleRate), format (fmt), file (dest)
    {
        setStatusMessage (T ("Exporting ", "Eksporterer ") + file.getFileName());
    }

    void run() override
    {
        setProgress (-1.0);      // indeterminate
        if (threadShouldExit()) { ok = false; return; }
        if (! parts.empty())
        {
            setStatusMessage (T ("Mixing tracks...", "Mixer spor..."));
            data = mixParts (parts, rate, normalised, error);
            if (error.isNotEmpty()) { ok = false; return; }
        }
        if (threadShouldExit()) { ok = false; return; }
        setStatusMessage (T ("Writing ", "Skriver ") + file.getFileName());
        ok = FileIO::writeBuffer (data, rate, file, format, error);
        data.setSize (0, 0);
    }

    void threadComplete (bool userPressedCancel) override
    {
        auto cb = onDone;
        const bool success = ok && ! userPressedCancel;
        auto err = error;
        const bool norm = normalised;
        // the owner deletes this job from the callback, so never call it from here directly
        juce::MessageManager::callAsync ([cb, success, err, norm] { if (cb) cb (success, err, norm); });
    }

private:
    std::vector<MixPart> parts;
    juce::AudioBuffer<float> data;
    double rate = 44100.0;
    FileIO::ExportFormat format;
    juce::File file;
    bool ok = false, normalised = false;
    juce::String error;
};

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

    ~PitchDialog() override
    {
        // closed with Escape or the title bar button: the preview must not keep playing
        if (onStopPreview) onStopPreview();
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
    addAndMakeVisible (trackStack);
    addAndMakeVisible (eqPanel);
    addAndMakeVisible (noisePanel);
    addAndMakeVisible (statusBar);

    noiseMaterialTimer.callback = [this] { updateNoiseMaterialNow(); };

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
    header.zoomInButton.onClick = [this] { zoomBy (2.0); };
    header.zoomOutButton.onClick = [this] { zoomBy (0.5); };
    header.openButton.onClick   = [this] { chooseFilesToOpen(); };
    header.tracksButton.onClick = [this] { showTracksMenu(); };
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
    tracksPanel.onOpen = [this] { chooseFilesToOpen(); };
    tracksPanel.onMerge = [this] { showMergeDialog(); };
    filePanel.onMenu = [this] { showFileMenu(); };
    markersPanel.onAdd = [this] { addMarkerAtCursor(); };
    markersPanel.onJump = [this] (int i)
    {
        if (auto* d = current())
            if (juce::isPositiveAndBelow (i, (int) d->markers.size()))
            {
                d->selStart = d->selEnd = d->cursor = d->toSamples (d->markers[(size_t) i].time);
                ensureVisibleSec (d->toSeconds (d->cursor));
                if (engine.isPlaying()) engine.setPosition (d->markers[(size_t) i].time);
                trackStack.repaintAll();
                selectionChanged();
            }
    };
    markersPanel.onChanged = [this] { trackStack.repaintAll(); refreshTrackList(); };

    // ---- stacked tracks
    trackStack.onActivate = [this] (int i)
    {
        if (i != currentIndex) selectDocument (i);
        grabKeyboardFocus();      // take focus away from marker text fields
    };
    trackStack.onCloseTrack = [this] (int i) { closeDocument (i); };
    trackStack.onRenameTrack = [this] (int i) { selectDocument (i); renameTrack(); };
    trackStack.onSelectionChanged = [this] { selectionChanged(); };
    trackStack.onViewChanged = [this] { viewChanged(); };
    trackStack.onMarkersChanged = [this] (int idx)
    {
        markersPanel.refresh();
        if (idx >= 0) markersPanel.focusMarker (idx);
    };
    trackStack.onEnvelopeEdited = [this] { trackStack.repaintAll(); };
    trackStack.onRulerClick = [this] (double t)
    {
        if (auto* d = current())
        {
            d->selStart = d->selEnd = d->cursor = d->toSamples (t);
            if (engine.isPlaying()) engine.setPosition (t);
            trackStack.repaintAll();
            selectionChanged();
        }
    };
    trackStack.onOpen = [this] { chooseFilesToOpen(); };
    trackStack.onMoveClipToTrack = [this] (int from, int clip, int to) { moveClipToTrack (from, clip, to); return true; };
    trackStack.onContextMenu = [this] (int lane, juce::int64 sample) { showWaveContextMenu (lane, sample); };
    trackStack.onNewTrack = [this] { newEmptyTrack(); };
    trackStack.onZoomIn = [this] { zoomBy (2.0); };
    trackStack.onZoomOut = [this] { zoomBy (0.5); };
    trackStack.onZoomSelection = [this] { zoomToSelection(); };
    trackStack.onZoomFit = [this] { zoomFit(); };

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
    statusBar.zoomInButton.onClick = [this] { zoomBy (2.0); };
    statusBar.zoomOutButton.onClick = [this] { zoomBy (0.5); };
    statusBar.zoomBox.onChange = [this]
    {
        auto pct = statusBar.zoomBox.getSelectedId();
        if (pct > 0) zoomTo (timeline.fitSecPerPixel() * 100.0 / pct);
    };

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
    noiseMaterialTimer.stopTimer();

    // The audio settings dialog references the engine's device manager: delete it first.
    if (auto* w = audioSettingsWindow.getComponent())
        delete w;

    // Waits for a running export to finish (it only works on copies of the audio).
    exportJob.reset();

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

    // dark card around the tracks
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
    trackStack.setBounds (w.reduced (4, 4));
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

std::vector<AudioDocument*> MainComponent::audibleTracks() const
{
    // same rule as the playback engine: solo tracks win, otherwise muted tracks are left out
    bool anySolo = false;
    for (auto& d : documents) anySolo = anySolo || d->soloed.load();
    std::vector<AudioDocument*> v;
    for (auto& d : documents)
        if ((anySolo ? d->soloed.load() : ! d->muted.load()) && d->getLength() > 0)
            v.push_back (d.get());
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

    auto* target = documents[(size_t) index].get();
    juce::Component::SafePointer<MainComponent> safe (this);
    auto doClose = [safe, target]
    {
        if (safe == nullptr) return;
        auto* self = safe.getComponent();

        // only stop the recording if this track is the one being recorded
        if (self->engine.isRecording() && self->engine.getRecordTarget() == target)
            self->stopRecordingAndFinish();
        if (! self->engine.isRecording())
            self->engine.stop();

        // the index may have changed while the confirmation was open
        int idx = -1;
        for (int i = 0; i < (int) self->documents.size(); ++i)
            if (self->documents[(size_t) i].get() == target) idx = i;
        if (idx < 0) return;

        auto victim = std::move (self->documents[(size_t) idx]);
        self->documents.erase (self->documents.begin() + idx);
        if (self->currentIndex >= (int) self->documents.size()) self->currentIndex = (int) self->documents.size() - 1;
        else if (self->currentIndex > idx) --self->currentIndex;
        self->syncEngine();                    // engine no longer references the victim
        victim->removeChangeListener (self);
        victim.reset();
        self->refreshAll();
        self->resized();
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
    if (! engine.isRecording())
        updateTimelineTotal();
    if (source == current())
    {
        filePanel.repaint();
        markersPanel.refresh();
        updateStatus();
        updatePanelSampleRates();
        if (! engine.isRecording())
        {
            updateLoopRange();          // the length / selection may have changed (undo, edits)
            updateNoiseMaterial();      // debounced
        }
    }
    trackStack.repaintAll();
    refreshTrackList();
}

bool MainComponent::ownsDocument (const AudioDocument* d) const
{
    if (d == nullptr) return false;
    for (auto& doc : documents)
        if (doc.get() == d) return true;
    return false;
}

void MainComponent::selectionChanged()
{
    if (auto* d = current())
        if (engine.isPlaying() && ! d->hasSelection())
            engine.setPosition (d->toSeconds (d->cursor));
    updateLoopRange();
    updateStatus();
    updateNoiseMaterial();
}

void MainComponent::updatePanelSampleRates()
{
    auto* d = current();
    const double rate = d != nullptr && d->sampleRate > 0 ? d->sampleRate : 48000.0;
    if (std::abs (rate - panelSampleRate) < 0.01) return;
    panelSampleRate = rate;
    eqPanel.setSampleRate (rate);
    noisePanel.setSampleRate (rate);
}

void MainComponent::refreshTrackList()
{
    // While recording the recorded track sends ~30 change messages per second;
    // only rebuild the lists when something visible in them changed.
    juce::String state (currentIndex);
    for (auto& d : documents)
        state << "|" << (d->modified ? "*" : "") << d->name;
    if (engine.isRecording() && state == lastTrackListState)
        return;
    lastTrackListState = state;

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
    updateTimelineTotal();
    trackStack.setDocuments (allDocuments(), currentIndex);
    trackStack.ensureLaneVisible (currentIndex);
    filePanel.setDocument (d);
    markersPanel.setDocument (d);
    updatePanelSampleRates();
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
        if (! fitsInt (temp.getLength())) { showError (T ("Could not open", "Kunne ikke åbne"), tooLongMessage()); return; }
        auto data = temp.copyRange (0, temp.getLength());
        insertBufferAtCursor (std::move (data), temp.sampleRate, T ("Insert file", "Indsæt fil"), f.getFileNameWithoutExtension());
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
    d->sampleRate = src->sampleRate;
    d->numChannels = src->numChannels;
    d->clips = src->clips;
    d->envelope = src->envelope;
    d->markers = src->markers;
    d->nextMarkerColour = src->nextMarkerColour;
    d->changed();               // updates the cached length / peaks and marks it modified
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
    if (engine.isRecording())
    {
        showError (T ("Merge tracks", "Flet spor"), T ("Stop the recording first.", "Stop optagelsen først."));
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

        auto doMerge = [safe, tracks, closeOthers]
        {
            if (safe == nullptr) return;
            auto* self = safe.getComponent();
            if (self->engine.isRecording())
            {
                self->showError (T ("Merge tracks", "Flet spor"), T ("Stop the recording first.", "Stop optagelsen først."));
                return;
            }

            // tracks may have been closed while a confirmation was open
            std::vector<AudioDocument*> valid;
            for (auto* t : tracks)
                if (self->ownsDocument (t)) valid.push_back (t);
            if (valid.empty()) return;

            double rate = 44100.0;
            bool normalised = false;
            juce::String err;
            juce::MouseCursor::showWaitCursor();
            auto mix = self->mixTracks (valid, rate, normalised, &err);
            juce::MouseCursor::hideWaitCursor();
            if (err.isNotEmpty())
            {
                self->showError (T ("Merge tracks", "Flet spor"), err);
                return;
            }

            auto d = std::make_unique<AudioDocument>();
            d->name = T ("Merged track", "Flettet spor");
            d->formatName = T ("Mix", "Mix");
            d->bitDepth = 24;
            // keep markers from all merged tracks
            for (auto* t : valid)
                for (auto& m : t->markers) d->markers.push_back (m);
            d->setBuffer (std::move (mix), rate);
            d->sortMarkers();
            d->nextMarkerColour = (int) d->markers.size();
            d->modified = true;

            if (closeOthers)
            {
                self->engine.stop();
                if (self->engine.isRecording())       // defensive: never free a recording target
                    self->stopRecordingAndFinish();

                for (auto* t : valid)
                {
                    int idx = -1;
                    for (int i = 0; i < (int) self->documents.size(); ++i)
                        if (self->documents[(size_t) i].get() == t) idx = i;
                    if (idx < 0) continue;
                    auto victim = std::move (self->documents[(size_t) idx]);
                    self->documents.erase (self->documents.begin() + idx);
                    self->currentIndex = -1;
                    self->syncEngine();                // engine no longer references the victim
                    victim->removeChangeListener (self);
                }
            }
            self->addDocument (std::move (d));
            self->showMessage (T ("Tracks merged", "Spor flettet") + juce::String (normalised ? T (" (level reduced to avoid clipping)", " (niveau sænket for at undgå forvrængning)") : ""));
        };

        bool discardsChanges = false;
        if (closeOthers)
            for (auto* t : tracks)
                discardsChanges = discardsChanges || (t->modified && t->getLength() > 0);

        if (discardsChanges)
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
                                                T ("Merge tracks", "Flet spor"),
                                                T ("Some of the merged tracks have changes that are not exported. Close them anyway after merging?",
                                                   "Nogle af de flettede spor har ændringer der ikke er eksporteret. Vil du lukke dem alligevel efter fletningen?"),
                                                T ("Merge and close", "Flet og luk"), T ("Cancel", "Annuller"), safe.getComponent(),
                                                juce::ModalCallbackFunction::create ([doMerge] (int r) { if (r == 1) doMerge(); }));
        }
        else
        {
            doMerge();
        }
    };
    launchDialog (dlg, T ("Merge tracks", "Flet spor"), this);
}

juce::AudioBuffer<float> MainComponent::mixTracks (const std::vector<AudioDocument*>& tracks, double& rateOut, bool& normalised,
                                                   juce::String* error)
{
    // copies are made here (message thread); the heavy work happens on the copies
    std::vector<MixPart> parts;
    for (auto* t : tracks)
    {
        if (! fitsInt (t->getLength()))
        {
            if (error != nullptr) *error = tooLongMessage();
            normalised = false;
            rateOut = 44100.0;
            return {};
        }
        MixPart p;
        p.audio = t->copyRange (0, t->getLength());
        DSP::applyEnvelope (p.audio, *t, 0);
        p.rate = t->sampleRate;
        parts.push_back (std::move (p));
    }

    juce::String err;
    auto mix = mixParts (parts, rateOut, normalised, err);
    if (error != nullptr) *error = err;
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

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (r == 0 || safe == nullptr) return;
        switch (r)
        {
            case 1: safe->newEmptyTrack(); break;
            case 2: safe->chooseFilesToOpen(); break;
            case 3: safe->chooseFileToInsert(); break;
            case 4: safe->duplicateTrack(); break;
            case 5: safe->renameTrack(); break;
            case 6: safe->showMergeDialog(); break;
            case 7: safe->closeDocument (safe->currentIndex); break;
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
    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (r == 0 || safe == nullptr) return;
        auto* d = safe->current();
        if (d == nullptr) return;
        if (r == 1) safe->renameTrack();
        if (r == 2) d->file.revealToUser();
        if ((r == 3 || r == 4) && safe->engine.isRecording())
        {
            safe->showMessage (T ("Stop the recording first.", "Stop optagelsen først."));
            return;
        }
        if (r == 3 && ! d->envelope.empty())
        {
            d->pushUndo (T ("Reset volume line", "Nulstil volumenlinje"), true);
            d->resetEnvelope();
            d->annotationsChanged();
        }
        if (r == 4 && ! d->markers.empty())
        {
            d->pushUndo (T ("Remove markers", "Fjern markører"), true);
            d->markers.clear();
            d->annotationsChanged();
            safe->markersPanel.refresh();
        }
        safe->trackStack.repaintAll();
    });
}

void MainComponent::showExportMenu()
{
    if (documents.empty())
    {
        showError (T ("Export", "Eksport"), T ("There is nothing to export yet.", "Der er intet at eksportere endnu."));
        return;
    }

    if (exportJob != nullptr)
    {
        showMessage (T ("An export is already running.", "En eksport kører allerede."));
        return;
    }

    auto* d = current();
    ExportDialog::Options o;
    o.hasTrack = d != nullptr && d->getLength() > 0;
    o.hasSelection = d != nullptr && d->hasSelection();
    o.multipleTracks = documents.size() > 1;
    o.trackSeconds = d != nullptr ? d->getLengthSeconds() : 0.0;
    o.selectionSeconds = o.hasSelection ? d->toSeconds (d->selEnd - d->selStart) : 0.0;

    // per scope format: track / selection use the current track, the mix uses the
    // highest rate / channel count of the tracks that are actually heard
    o.trackRate = o.selectionRate = d != nullptr ? d->sampleRate : 44100.0;
    o.trackChannels = o.selectionChannels = d != nullptr ? juce::jlimit (1, 2, d->getNumChannels()) : 2;
    {
        double mixRate = 0.0;
        int mixChannels = 0;
        for (auto* a : audibleTracks())
        {
            o.mixSeconds = juce::jmax (o.mixSeconds, a->getLengthSeconds());
            mixRate = juce::jmax (mixRate, a->sampleRate);
            mixChannels = juce::jmax (mixChannels, juce::jlimit (1, 2, a->getNumChannels()));
        }
        o.mixRate = mixRate > 0 ? mixRate : 44100.0;
        o.mixChannels = mixChannels > 0 ? mixChannels : 2;
    }
    // legacy fields
    o.sampleRate = o.trackRate;
    o.channels = o.trackChannels;
    o.trackName = d != nullptr ? d->name : juce::String ("export");
    o.lastFormatId = properties.getUserSettings()->getValue ("exportFormat", "wav24");
    o.folder = juce::File (properties.getUserSettings()->getValue ("exportDir",
                           properties.getUserSettings()->getValue ("lastDir",
                               juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName())));
    o.initialScope = o.hasSelection ? 1 : (o.hasTrack ? 0 : 2);

    auto* dlg = new ExportDialog (o);
    juce::Component::SafePointer<MainComponent> safe (this);
    dlg->onExport = [safe] (int scope, int formatIndex, juce::File file)
    {
        auto& fmts = FileIO::getExportFormats();
        if (safe != nullptr && juce::isPositiveAndBelow (formatIndex, (int) fmts.size()))
            safe->exportAudio (scope, fmts[(size_t) formatIndex], file);
    };
    launchDialog (dlg, T ("Export audio", "Eksportér lyd"), this);
}

void MainComponent::exportAudio (int scope, const FileIO::ExportFormat& format, const juce::File& file)
{
    if (exportJob != nullptr)
    {
        showMessage (T ("An export is already running.", "En eksport kører allerede."));
        return;
    }

    // Everything the export needs is copied here, on the message thread.
    std::vector<MixPart> parts;
    juce::AudioBuffer<float> data;
    double rate = 44100.0;
    auto* doc = current();

    if (scope == 2)
    {
        // what you hear is what you get: muted tracks are left out, solo tracks win
        auto audible = audibleTracks();
        if (audible.empty())
        {
            showError (T ("Export failed", "Eksport fejlede"),
                       T ("No tracks are audible (all are muted, empty or not soloed). Unmute a track first.",
                          "Ingen spor kan høres (alle er slået fra, tomme eller ikke solo). Slå et spor til først."));
            return;
        }
        for (auto* t : audible)
        {
            if (! fitsInt (t->getLength()))
            {
                showError (T ("Export failed", "Eksport fejlede"), tooLongMessage());
                return;
            }
            MixPart p;
            p.audio = t->copyRange (0, t->getLength());
            DSP::applyEnvelope (p.audio, *t, 0);
            p.rate = t->sampleRate;
            parts.push_back (std::move (p));
        }
    }
    else if (doc != nullptr)
    {
        juce::int64 s = 0, e = doc->getLength();
        if (scope == 1 && doc->hasSelection()) { s = doc->selStart; e = doc->selEnd; }
        if (! fitsInt (e - s))
        {
            showError (T ("Export failed", "Eksport fejlede"), tooLongMessage());
            return;
        }
        data = doc->copyRange (s, e);
        DSP::applyEnvelope (data, *doc, s);
        rate = doc->sampleRate;
    }
    else
    {
        return;
    }

    FileIO::getFormatManager();     // make sure the shared format manager is created on this thread

    exportJob = std::make_unique<ExportJob> (std::move (parts), std::move (data), rate, format, file, this);
    juce::Component::SafePointer<MainComponent> safe (this);
    const auto formatId = format.id;
    exportJob->onDone = [safe, scope, doc, file, formatId] (bool ok, const juce::String& err, bool normalised)
    {
        if (safe == nullptr) return;
        safe->exportJob.reset();

        if (! ok)
        {
            safe->showError (T ("Export failed", "Eksport fejlede"),
                             err.isNotEmpty() ? err : T ("The export was cancelled.", "Eksporten blev annulleret."));
            return;
        }

        safe->properties.getUserSettings()->setValue ("exportDir", file.getParentDirectory().getFullPathName());
        safe->properties.getUserSettings()->setValue ("exportFormat", formatId);
        if (scope == 0 && safe->ownsDocument (doc))     // the track may have been closed meanwhile
        {
            doc->modified = false;
            safe->refreshTrackList();
            safe->trackStack.repaintAll();
        }
        safe->showMessage (T ("Exported ", "Eksporterede ") + file.getFullPathName()
                           + (normalised ? T (" (level reduced to avoid clipping)", " (niveau sænket for at undgå forvrængning)") : juce::String()));
    };
    exportJob->launchThread();
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

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.settingsButton), [safe] (int r)
    {
        if (r == 0 || safe == nullptr) return;
        if (r == 1) safe->showAudioSettings();
        if (r == 2) safe->showShortcuts();
        if (r == 11 || r == 12)
        {
            safe->properties.getUserSettings()->setValue ("language", r == 12 ? "da" : "en");
            safe->properties.saveIfNeeded();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, r == 12 ? "Sprog" : "Language",
                                                    r == 12 ? juce::String::fromUTF8 ("Genstart programmet for at skifte sprog.")
                                                            : juce::String ("Restart the program to change the language."));
        }
        if (r == 3)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Artilody Soundsculp",
                "Artilody Soundsculp " + juce::String (JUCE_APPLICATION_VERSION_STRING) + "\n\n"
                + T ("Simple multi-track audio editor.", "Simpel lydredigering med flere spor.") + "\n"
                + "https://github.com/shenphenchoedron-hue/Artilody-Soundsculp\n\n"
                + "License: GPL-3.0. Built with JUCE 8, used under the AGPLv3.\n"
                + T ("License texts and third-party notices: THIRD_PARTY_NOTICES and the licenses folder in the installation.",
                     "Licenstekster og tredjepartsnoter: THIRD_PARTY_NOTICES og mappen licenses i installationen."));
        }
    });
}

void MainComponent::showAudioSettings()
{
    if (auto* existing = audioSettingsWindow.getComponent())
    {
        existing->toFront (true);
        return;
    }
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 1, 2, false, false, true, false);
    selector->setSize (520, 420);
    audioSettingsWindow = launchDialog (selector, T ("Audio settings", "Lydindstillinger"), this);
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
        trackStack.setTool (vol ? WaveformView::Tool::volume : WaveformView::Tool::select);
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
    if (id == "split")     doSplit();
    if (id == "join")      doJoin();
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
    // Like Audacity: remove the audio outside the selection, keep the rest where it is
    d->removeRange (e, d->getLength());
    d->removeRange (0, s);
    d->selStart = s; d->selEnd = e; d->cursor = s;
    d->changed();
    showMessage (T ("Trimmed to selection", "Trimmet til markeringen"));
}

void MainComponent::doCopy()
{
    if (! requireSelection (T ("Copy", "Kopiér"))) return;
    auto* d = current();
    if (! fitsInt (d->selEnd - d->selStart)) { showError (T ("Copy", "Kopiér"), tooLongMessage()); return; }
    clipboard = d->copyRange (d->selStart, d->selEnd);
    clipboardRate = d->sampleRate;
    showMessage (T ("Copied ", "Kopierede ") + formatTime (d->toSeconds (d->selEnd - d->selStart)));
}

void MainComponent::doCut()
{
    if (! requireSelection (T ("Cut", "Klip"))) return;
    auto* d = current();
    if (! fitsInt (d->selEnd - d->selStart)) { showError (T ("Cut", "Klip"), tooLongMessage()); return; }
    clipboard = d->copyRange (d->selStart, d->selEnd);
    clipboardRate = d->sampleRate;
    d->pushUndo (T ("Cut", "Klip"));
    d->removeRange (d->selStart, d->selEnd);            // leaves a gap - the pieces stay where they are
    d->selEnd = d->selStart;
    d->changed();
    showMessage (T ("Cut ", "Klippede ") + formatTime ((double) clipboard.getNumSamples() / clipboardRate)
                 + T (" - the gap stays. Drag the clip bars to move the pieces.", " - hullet bliver. Træk i klippenes bjælke for at flytte stykkerne."));
}

void MainComponent::doDelete()
{
    if (! requireSelection (T ("Delete", "Slet"))) return;
    auto* d = current();
    d->pushUndo (T ("Delete", "Slet"));
    d->removeRange (d->selStart, d->selEnd);
    d->selEnd = d->selStart;
    d->changed();
}

void MainComponent::doRippleDelete()
{
    if (! requireSelection (T ("Delete and close gap", "Slet og luk hul"))) return;
    auto* d = current();
    d->pushUndo (T ("Delete and close gap", "Slet og luk hul"));
    d->rippleRemove (d->selStart, d->selEnd);
    d->selEnd = d->selStart;
    d->changed();
}

void MainComponent::doSplit()
{
    if (! requireDocument()) return;
    auto* d = current();
    // only positions strictly inside a clip split anything
    auto splitsSomething = [d] (juce::int64 pos)
    {
        int idx = d->clipAt (pos);
        return idx >= 0 && pos > d->clips[(size_t) idx].start && pos < d->clips[(size_t) idx].end();
    };
    std::vector<juce::int64> positions;
    if (d->hasSelection()) positions = { d->selStart, d->selEnd };
    else                   positions = { d->cursor };
    bool any = false;
    for (auto p : positions) any = any || splitsSomething (p);
    if (! any)
    {
        showMessage (T ("Nothing to split here - place the cursor inside a clip.", "Intet at dele her - placér markøren inde i et klip."));
        return;
    }

    d->pushUndo (T ("Split", "Del"));
    for (auto p : positions)
        d->splitAt (p);
    d->changed();
    showMessage (T ("Split into clips. Drag a clip by its bar to move it.", "Delt i klip. Træk et klip i dets bjælke for at flytte det."));
}

void MainComponent::doJoin()
{
    if (! requireDocument()) return;
    auto* d = current();
    juce::int64 s, e;
    getTargetRange (s, e);

    // same rule as AudioDocument::joinRange: clips touching [s, e)
    int touching = 0;
    juce::int64 a = std::numeric_limits<juce::int64>::max(), b = 0;
    for (auto& c : d->clips)
        if (c.start < e && c.end() > s)
        {
            ++touching;
            a = std::min (a, c.start);
            b = std::max (b, c.end());
        }
    if (touching < 2)
    {
        showMessage (T ("Select at least two clips to join.", "Markér mindst to klip for at samle dem."));
        return;
    }
    if (! fitsInt (b - a + 16))
    {
        showError (T ("Join clips", "Saml klip"), tooLongMessage());
        return;
    }

    d->pushUndo (T ("Join clips", "Saml klip"));
    d->joinRange (s, e);
    d->changed();
    showMessage (T ("Clips joined (gaps became silence).", "Klip samlet (huller blev til stilhed)."));
}

void MainComponent::moveClipToTrack (int fromIndex, int clipIndex, int toIndex)
{
    if (! juce::isPositiveAndBelow (fromIndex, (int) documents.size()) || ! juce::isPositiveAndBelow (toIndex, (int) documents.size()))
        return;
    if (engine.isRecording())
    {
        // the recorded clip refers directly into the recorder's memory
        showMessage (T ("Stop the recording first.", "Stop optagelsen først."));
        return;
    }
    if (fromIndex == toIndex) return;
    auto* from = documents[(size_t) fromIndex].get();
    auto* to = documents[(size_t) toIndex].get();
    if (! juce::isPositiveAndBelow (clipIndex, (int) from->clips.size())) return;

    const double fromRate = from->sampleRate;
    const bool targetEmpty = to->clips.empty() && to->getLength() == 0;
    const double targetRate = targetEmpty ? fromRate : to->sampleRate;
    if (! fitsInt ((juce::int64) std::ceil ((double) from->clips[(size_t) clipIndex].length() * targetRate / fromRate) + 16))
    {
        showError (T ("Move clip", "Flyt klip"), tooLongMessage());
        return;
    }

    // Undo on both tracks. After a drag the source also has the "Move clip" entry of the
    // drag itself; a direct call (e.g. from a test) gets its own entry here.
    from->pushUndo (T ("Move clip to ", "Flyt klip til ") + to->name);

    Clip c = from->takeClip (clipIndex);
    {
        // never keep a buffer that refers to memory owned by someone else
        juce::AudioBuffer<float> owned;
        owned.makeCopyOf (c.audio);
        c.audio = std::move (owned);
    }
    const double startSec = from->toSeconds (c.start);
    from->selEnd = from->selStart;
    from->changed();

    to->pushUndo (T ("Move clip here", "Flyt klip hertil"));     // before the format changes
    if (targetEmpty)
    {
        // empty tracks adopt the format of the moved clip
        const juce::ScopedLock sl (to->lock);
        to->sampleRate = fromRate;
        to->numChannels = juce::jmax (1, c.audio.getNumChannels());
    }
    if (std::abs (to->sampleRate - fromRate) > 0.5)
        c.audio = DSP::resample (c.audio, fromRate / to->sampleRate);
    c.start = to->toSamples (startSec);

    int idx = to->addClip (std::move (c));
    to->selStart = to->clips[(size_t) idx].start;
    to->selEnd = to->clips[(size_t) idx].end();
    to->cursor = to->selStart;
    to->changed();
    selectDocument (toIndex);
    showMessage (T ("Clip moved to ", "Klip flyttet til ") + to->name);
}

void MainComponent::showWaveContextMenu (int laneIndex, juce::int64 sample)
{
    if (laneIndex != currentIndex) selectDocument (laneIndex);
    auto* d = current();
    if (d == nullptr) return;
    if (! d->hasSelection())
        d->cursor = sample;
    const bool sel = d->hasSelection();
    const int clipIdx = d->clipAt (sample);

    juce::PopupMenu m;
    m.addItem (1, T ("Cut", "Klip") + "\tCtrl+X", sel);
    m.addItem (2, T ("Copy", "Kopiér") + "\tCtrl+C", sel);
    m.addItem (3, T ("Paste", "Indsæt") + "\tCtrl+V", clipboard.getNumSamples() > 0);
    m.addItem (4, T ("Delete (leave gap)", "Slet (efterlad hul)") + "\tDelete", sel);
    m.addItem (5, T ("Delete and close gap", "Slet og luk hul") + "\tShift+Delete", sel);
    m.addSeparator();
    m.addItem (6, T ("Split here", "Del her") + "\tCtrl+I", clipIdx >= 0 || sel);
    m.addItem (7, T ("Join clips", "Saml klip") + "\tCtrl+J", d->clips.size() > 1);
    m.addItem (8, T ("Select clip", "Markér klip"), clipIdx >= 0);
    m.addItem (9, T ("Rename clip...", "Omdøb klip..."), clipIdx >= 0);

    juce::Component::SafePointer<MainComponent> safe (this);
    const juce::int64 menuClipStart = clipIdx >= 0 ? d->clips[(size_t) clipIdx].start : -1;
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, d, menuClipStart] (int r)
    {
        if (r == 0 || safe == nullptr || safe->current() == nullptr) return;
        auto* doc = safe->current();
        // clip indexes change when the clip list is re-sorted: find the clip again by position
        int foundClip = -1;
        if (doc == d && menuClipStart >= 0)
            for (int i = 0; i < (int) doc->clips.size(); ++i)
                if (doc->clips[(size_t) i].start == menuClipStart) foundClip = i;
        switch (r)
        {
            case 1: safe->doCut(); break;
            case 2: safe->doCopy(); break;
            case 3: safe->doPaste(); break;
            case 4: safe->doDelete(); break;
            case 5: safe->doRippleDelete(); break;
            case 6: safe->doSplit(); break;
            case 7: safe->doJoin(); break;
            case 8:
                if (juce::isPositiveAndBelow (foundClip, (int) doc->clips.size()))
                {
                    doc->selStart = doc->clips[(size_t) foundClip].start;
                    doc->selEnd = doc->clips[(size_t) foundClip].end();
                    doc->cursor = doc->selStart;
                    safe->trackStack.repaintAll();
                    safe->selectionChanged();
                }
                break;
            case 9:
                if (juce::isPositiveAndBelow (foundClip, (int) doc->clips.size()) && ! safe->engine.isRecording())
                {
                    // the clip list may change while the dialog is open: find the clip again by its position
                    const auto clipStart = doc->clips[(size_t) foundClip].start;
                    const auto oldName = doc->clips[(size_t) foundClip].name;
                    askForValue (T ("Rename clip", "Omdøb klip"), T ("Name:", "Navn:"), oldName,
                                 [safe, doc, clipStart] (const juce::String& v)
                    {
                        if (safe == nullptr || ! safe->ownsDocument (doc) || safe->engine.isRecording()) return;
                        int idx = -1;
                        for (int i = 0; i < (int) doc->clips.size(); ++i)
                            if (doc->clips[(size_t) i].start == clipStart) idx = i;
                        if (idx < 0 || doc->clips[(size_t) idx].name == v.trim()) return;
                        doc->pushUndo (T ("Rename clip", "Omdøb klip"));
                        doc->clips[(size_t) idx].name = v.trim();
                        doc->annotationsChanged();
                        safe->trackStack.repaintAll();
                    });
                }
                break;
            default: break;
        }
        safe->trackStack.repaintAll();
    });
}

void MainComponent::insertBufferAtCursor (juce::AudioBuffer<float> data, double rate, const juce::String& undoName,
                                          const juce::String& clipName)
{
    auto* d = current();
    if (d == nullptr || data.getNumSamples() == 0 || engine.isRecording() || rate <= 0) return;

    const double targetRate = d->clips.empty() ? rate : d->sampleRate;
    if (! fitsInt ((juce::int64) std::ceil ((double) data.getNumSamples() * targetRate / rate) + 16))
    {
        showError (undoName, tooLongMessage());
        return;
    }

    d->pushUndo (undoName);

    // Empty tracks adopt the inserted material's format
    if (d->clips.empty())
    {
        // cursor / selection are in samples: keep their time position across the rate change
        const double cursorSec = d->toSeconds (d->cursor);
        const double selStartSec = d->toSeconds (d->selStart);
        const double selEndSec = d->toSeconds (d->selEnd);
        {
            const juce::ScopedLock sl (d->lock);
            d->sampleRate = rate;
            d->numChannels = juce::jmax (1, data.getNumChannels());
        }
        d->cursor = d->toSamples (cursorSec);
        d->selStart = d->toSamples (selStartSec);
        d->selEnd = d->toSamples (selEndSec);
    }
    if (std::abs (rate - d->sampleRate) > 0.5)
        data = DSP::resample (data, rate / d->sampleRate);

    juce::int64 pos = d->cursor;
    if (d->hasSelection())
    {
        pos = d->selStart;
        d->rippleRemove (d->selStart, d->selEnd);     // paste replaces the selection
    }
    const auto len = (juce::int64) data.getNumSamples();
    d->insertClip (pos, std::move (data), clipName);
    d->selStart = pos;
    d->selEnd = pos + len;
    d->cursor = pos;
    d->modified = true;
    d->changed();
    ensureVisibleSec (d->toSeconds (pos));
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
    showMessage (T ("Pasted as a new clip ", "Indsat som nyt klip ") + formatTime ((double) clipboard.getNumSamples() / clipboardRate));
}

void MainComponent::applyToRange (const juce::String& undoName, juce::int64 s, juce::int64 e,
                                  const std::function<void (juce::AudioBuffer<float>&, juce::int64)>& fn)
{
    auto* d = current();
    if (d == nullptr) return;
    if (e <= s || ! d->hasAudioIn (s, e))
    {
        showMessage (undoName + ": " + T ("there is no audio here.", "der er ingen lyd her."));
        return;
    }
    d->pushUndo (undoName);
    d->processRange (s, e, fn);
    d->changed();
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
    const double len = (double) std::max<juce::int64> (1, e - s - 1);
    // the fade curve follows the time position, so it also works across several clips
    applyToRange (in ? T ("Fade in", "Fade ind") : T ("Fade out", "Fade ud"), s, e,
                  [in, s, len] (juce::AudioBuffer<float>& b, juce::int64 absStart)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            auto t = (float) ((double) (absStart + i - s) / len);
            if (! in) t = 1.0f - t;
            t = juce::jlimit (0.0f, 1.0f, t);
            const float g = t * t * (3.0f - 2.0f * t) * 0.5f + t * 0.5f;
            for (int c = 0; c < b.getNumChannels(); ++c)
                b.getWritePointer (c)[i] *= g;
        }
    });
}

void MainComponent::doNormalize()
{
    if (! requireDocument()) return;
    auto* d = current();
    juce::int64 s, e;
    getTargetRange (s, e);
    if (! fitsInt (e - s)) { showError (T ("Normalize", "Normalisér"), tooLongMessage()); return; }
    auto before = DSP::getPeak (d->copyRange (s, e));
    if (before <= 1.0e-6f) { showMessage (T ("Nothing to normalize (silence).", "Intet at normalisere (stilhed).")); return; }
    const float gain = juce::Decibels::decibelsToGain (-0.1f) / before;
    applyToRange (T ("Normalize", "Normalisér"), s, e, [gain] (juce::AudioBuffer<float>& b, juce::int64) { b.applyGain (gain); });
    auto gainDb = -0.1f - juce::Decibels::gainToDecibels (before);
    showMessage (T ("Normalized: ", "Normaliseret: ") + (gainDb >= 0 ? "+" : "") + juce::String (gainDb, 1) + " dB");
}

void MainComponent::doSilenceTrim()
{
    if (! requireDocument()) return;
    juce::Component::SafePointer<MainComponent> safe (this);
    askForValue (T ("Silence trim", "Stilhedstrim"),
                 T ("Removes silence at the start and end of each clip (in the selection, or all clips).\nThreshold in dB (e.g. -50):",
                    "Fjerner stilhed i starten og slutningen af hvert klip (i markeringen, eller alle klip).\nTærskel i dB (fx -50):"),
                 "-50", [safe] (const juce::String& v)
    {
        if (safe == nullptr || safe->current() == nullptr || safe->engine.isRecording()) return;
        auto* d = safe->current();

        // accept only plain numbers between -120 and -6 dB, otherwise use the default -50 dB
        float thr = -50.0f;
        bool thresholdOk = false;
        {
            auto text = v.trim().replaceCharacter (',', '.');
            if (text.endsWithIgnoreCase ("db")) text = text.dropLastCharacters (2).trim();
            if (text.isNotEmpty() && text.containsOnly ("-+0123456789.") && text.containsAnyOf ("0123456789"))
            {
                auto value = text.getFloatValue();
                if (value <= -6.0f && value >= -120.0f) { thr = value; thresholdOk = true; }
            }
        }
        juce::String thresholdNote;
        if (! thresholdOk)
            thresholdNote = T ("Invalid threshold (use a number from -120 to -6 dB) - used -50 dB. ",
                               "Ugyldig tærskel (brug et tal fra -120 til -6 dB) - brugte -50 dB. ");

        juce::int64 s, e;
        safe->getTargetRange (s, e);
        auto margin = d->toSamples (0.005);

        // collect the ranges to remove first, then remove them (removing changes the clip list)
        std::vector<std::pair<juce::int64, juce::int64>> cuts;
        for (auto& c : d->clips)
        {
            if (c.end() <= s || c.start >= e) continue;
            juce::int64 first, last;
            if (! DSP::findNonSilentRange (c.audio, thr, first, last))
            {
                cuts.push_back ({ c.start, c.end() });          // whole clip is silent
                continue;
            }
            first = std::max<juce::int64> (0, first - margin);
            last = std::min<juce::int64> (c.length() - 1, last + margin);
            if (first > 0) cuts.push_back ({ c.start, c.start + first });
            if (last + 1 < c.length()) cuts.push_back ({ c.start + last + 1, c.end() });
        }
        double removed = 0;
        for (auto& r : cuts) removed += d->toSeconds (r.second - r.first);
        if (removed <= 0.0005) { safe->showMessage (thresholdNote + T ("No silence found.", "Ingen stilhed fundet.")); return; }

        d->pushUndo (T ("Silence trim", "Stilhedstrim"));
        for (auto& r : cuts) d->removeRange (r.first, r.second);
        d->selEnd = d->selStart;
        d->changed();
        safe->showMessage (thresholdNote + T ("Removed ", "Fjernede ") + juce::String (removed, 2) + T (" s of silence", " s stilhed"));
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
        if (safe == nullptr || safe->current() == nullptr || safe->engine.isRecording()) return;
        auto* d = safe->current();
        if (! d->hasSelection()) return;
        int n = juce::jlimit (2, 100, v.getIntValue());
        if ((d->selEnd - d->selStart) * n > (juce::int64) std::numeric_limits<int>::max() / 2)
        {
            safe->showError (T ("Loop", "Loop"), tooLongMessage());
            return;
        }
        auto seg = d->copyRange (d->selStart, d->selEnd);
        const int L = seg.getNumSamples();
        const int chans = seg.getNumChannels();

        // Equal-power crossfade at every joint: the tail of repeat i overlaps the head of
        // repeat i+1 (no level dip, no click). Each joint shortens the result by xf samples.
        const int xf = juce::jmin (L / 4, (int) (d->sampleRate * 0.01));
        juce::AudioBuffer<float> joint (chans, juce::jmax (1, xf));     // tail(i) x head(i+1)
        joint.clear();
        for (int c = 0; c < chans; ++c)
            for (int k = 0; k < xf; ++k)
            {
                const double a = ((double) k + 0.5) / (double) xf * juce::MathConstants<double>::halfPi;
                joint.setSample (c, k, seg.getSample (c, L - xf + k) * (float) std::cos (a)
                                         + seg.getSample (c, k) * (float) std::sin (a));
            }

        // repeats 2..n: each starts after the crossfaded head and (except the last) ends with the next joint
        const int step = L - xf;
        juce::AudioBuffer<float> out (chans, step * (n - 1));
        for (int r = 0; r < n - 1; ++r)
            for (int c = 0; c < chans; ++c)
            {
                out.copyFrom (c, r * step, seg, c, xf, step);
                if (xf > 0 && r < n - 2)
                    out.copyFrom (c, r * step + step - xf, joint, c, 0, xf);
            }

        const auto selS = d->selStart, selE = d->selEnd;
        const auto added = (juce::int64) out.getNumSamples();
        d->pushUndo (T ("Loop", "Loop"));
        d->insertClip (selE, std::move (out), {});        // the repeats follow the selection, later audio moves right
        d->joinRange (selS, selE + added);
        if (xf > 0)
        {
            // first joint: the end of the original selection fades into the first repeat
            const auto js = selE - xf;
            d->processRange (js, selE, [&joint, js] (juce::AudioBuffer<float>& b, juce::int64 absStart)
            {
                const int off = (int) (absStart - js);
                for (int c = 0; c < b.getNumChannels(); ++c)
                    b.copyFrom (c, 0, joint, juce::jmin (c, joint.getNumChannels() - 1), off, b.getNumSamples());
            });
        }
        d->selStart = selS;
        d->selEnd = selE + added;
        d->cursor = selS;
        d->changed();
        safe->showMessage (T ("Selection repeated ", "Markeringen gentaget ") + juce::String (n) + T (" times", " gange"));
    });
}

void MainComponent::doReverse()
{
    if (! requireDocument()) return;
    juce::int64 s, e;
    getTargetRange (s, e);
    applyToRange (T ("Reverse", "Baglæns"), s, e, [] (juce::AudioBuffer<float>& b, juce::int64) { DSP::reverse (b); });
}

void MainComponent::doPitch()
{
    if (! requireDocument()) return;
    auto* dlg = new PitchDialog();
    juce::Component::SafePointer<MainComponent> safe (this);
    dlg->onPreview = [safe] (double st) { if (safe != nullptr) safe->previewPitch (st); };
    dlg->onApply = [safe] (double st) { if (safe != nullptr) safe->applyPitch (st); };
    // also called from the dialog's destructor: only stop a running preview, never normal playback
    dlg->onStopPreview = [safe] { if (safe != nullptr && safe->engine.isPreviewing()) safe->engine.stop(); };
    launchDialog (dlg, T ("Pitch", "Tone"), this);
}

void MainComponent::previewPitch (double semitones)
{
    auto* d = current();
    if (d == nullptr || engine.isRecording()) return;
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
    if (d == nullptr || std::abs (semitones) < 0.01 || engine.isRecording()) return;
    juce::int64 s, e;
    getTargetRange (s, e);
    juce::MouseCursor::showWaitCursor();
    applyToRange (T ("Pitch", "Tone"), s, e, [semitones] (juce::AudioBuffer<float>& b, juce::int64) { b = DSP::pitchShift (b, semitones); });
    juce::MouseCursor::hideWaitCursor();
    showMessage (T ("Pitch changed ", "Tone ændret ") + (semitones > 0 ? "+" : "") + juce::String (semitones, 1) + T (" semitones", " halvtoner"));
}

void MainComponent::applyEQ()
{
    if (! requireDocument()) return;
    auto* d = current();
    juce::int64 s, e;
    getTargetRange (s, e);
    if (e <= s || ! d->hasAudioIn (s, e))
    {
        showMessage ("EQ: " + T ("there is no audio here.", "der er ingen lyd her."));
        return;
    }
    if (! fitsInt (e - s)) { showError ("EQ", tooLongMessage()); return; }

    // Filter the whole range in one go (continuous across clips and gaps), then apply
    // one common anti-clip gain so the clips keep their relative levels.
    auto settings = eqPanel.getSettings();
    juce::MouseCursor::showWaitCursor();
    auto processed = d->copyRange (s, e);           // plain audio (the volume line is not included)
    DSP::applyEQ (processed, d->sampleRate, settings);
    const float peak = DSP::getPeak (processed);
    const bool reduced = peak > 1.0f;
    if (reduced)
        processed.applyGain (0.999f / peak);

    d->pushUndo ("EQ");
    d->processRange (s, e, [&processed, s] (juce::AudioBuffer<float>& b, juce::int64 absStart)
    {
        const int offset = (int) (absStart - s);
        const int n = juce::jmin (b.getNumSamples(), processed.getNumSamples() - offset);
        if (n <= 0) return;
        for (int c = 0; c < b.getNumChannels(); ++c)
            b.copyFrom (c, 0, processed, juce::jmin (c, processed.getNumChannels() - 1), offset, n);
    });
    d->changed();
    juce::MouseCursor::hideWaitCursor();
    showMessage (T ("EQ applied. Turn the EQ switch off to avoid hearing it twice.",
                    "EQ anvendt. Slå EQ-kontakten fra for ikke at høre den to gange.")
                 + (reduced ? T (" (level reduced to avoid clipping)", " (niveau sænket for at undgå forvrængning)") : juce::String()));
}

void MainComponent::learnNoise()
{
    if (! requireSelection (T ("Learn noise profile", "Lær støjprofil")))
    {
        noisePanel.setLearnedProfile ({}, current() != nullptr ? current()->sampleRate : 48000.0);
        return;
    }
    auto* d = current();
    const auto e = juce::jmin (d->selEnd, d->selStart + d->toSamples (60.0));
    noisePanel.setLearnedProfile (DSP::learnNoiseProfile (d->copyRange (d->selStart, e)), d->sampleRate);
    showMessage (T ("Noise profile learned from the selection. Now select what to clean (or nothing for the whole track) and press Apply.",
                    "Støjprofil lært fra markeringen. Markér nu det der skal renses (eller intet for hele sporet) og tryk Anvend."));
}

std::vector<float> MainComponent::getNoiseProfile()
{
    // a learned profile is only used when it is switched on and matches this track's rate
    if (noisePanel.isUsingLearnedProfile())
        return noisePanel.getLearnedProfile();
    auto* d = current();
    if (d == nullptr) return {};
    // otherwise estimate from the material the display analyses: the selection, or the start of the track
    juce::int64 s, e;
    getNoiseMaterialRange (s, e, 60.0);
    return DSP::estimateNoiseProfile (d->copyRange (s, e));
}

void MainComponent::getNoiseMaterialRange (juce::int64& s, juce::int64& e, double maxSeconds) const
{
    auto* d = current();
    s = e = 0;
    if (d == nullptr) return;
    if (d->hasSelection()) { s = d->selStart; e = d->selEnd; }
    else                   { s = 0; e = d->getLength(); }
    e = juce::jmin (e, s + d->toSamples (maxSeconds));
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
    engine.stop();
    juce::int64 s, e;
    getTargetRange (s, e);
    auto params = noisePanel.getParams();
    params.outputNoiseOnly = false;
    juce::MouseCursor::showWaitCursor();
    auto profile = getNoiseProfile();
    applyToRange (T ("Noise removal", "Støjfjernelse"), s, e,
                  [profile, params] (juce::AudioBuffer<float>& b, juce::int64) { b = DSP::reduceNoise (b, profile, params); });
    juce::MouseCursor::hideWaitCursor();
    showMessage (T ("Noise removed", "Støj fjernet"));
}

void MainComponent::updateNoiseMaterial()
{
    // called on every selection change (mouse drags): coalesce, copy once things settle
    if (! noisePanel.isVisible()) return;
    noiseMaterialTimer.startTimer (200);
}

void MainComponent::updateNoiseMaterialNow()
{
    if (! noisePanel.isVisible()) return;
    auto* d = current();
    if (d == nullptr || d->getLength() == 0 || engine.isRecording())
    {
        noisePanel.setAnalysisMaterial ({});
        return;
    }
    juce::int64 s, e;
    getNoiseMaterialRange (s, e, 8.0);
    noisePanel.setAnalysisMaterial (d->copyRange (s, e));
}

void MainComponent::addMarkerAtCursor()
{
    if (! requireDocument()) return;
    auto* d = current();
    d->pushUndo (T ("Add marker", "Tilføj markør"), true);
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
        stopRecordingAndFinish();
        return;
    }
    engine.stop();
    trackStack.repaintAll();
}

void MainComponent::stopRecordingAndFinish()
{
    if (! engine.isRecording()) return;
    finishRecording (engine.stopRecording());
}

void MainComponent::finishRecording (AudioDocument* recorded)
{
    // The bookkeeping belongs to the recorded track, which need not be the current one.
    if (ownsDocument (recorded))
    {
        recorded->modified = recorded->getLength() > 0;
        recorded->clearHistory();
        recorded->cursor = recorded->selStart = recorded->selEnd = 0;
    }
    zoomFit();
    refreshAll();

    if (engine.hadRecordingOverflow())
        showError (T ("Recording", "Optagelse"),
                   T ("Some audio could not be recorded in time (buffer overflow), so the recording may contain gaps.",
                      "Noget lyd kunne ikke optages i tide (buffer-overløb), så optagelsen kan have huller."));
    showMessage (T ("Recording finished", "Optagelse afsluttet"));
}

void MainComponent::goToStart()
{
    if (auto* d = current())
    {
        d->cursor = 0;
        if (! d->hasSelection()) d->selStart = d->selEnd = 0;
        engine.setPosition (d->hasSelection() && engine.isLooping() ? d->toSeconds (d->selStart) : 0.0);
        timeline.scrollTo (0.0);
        viewChanged();
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

    // No edits/undo while a mouse drag is in progress: they would change the clip/marker/envelope
    // lists under the drag (stale indexes, lost undo entries).
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return true;

    if (key == juce::KeyPress::homeKey)                            { goToStart(); return true; }
    if ((key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey)) && mods.isShiftDown()) { doRippleDelete(); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { doDelete(); return true; }
    if (key == juce::KeyPress ('i', cmd, 0))                       { doSplit(); return true; }
    if (key == juce::KeyPress ('j', cmd, 0))                       { doJoin(); return true; }
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
        if (auto* d = current())
        {
            d->selStart = 0;
            d->selEnd = d->getLength();
            d->cursor = 0;
            trackStack.repaintAll();
            selectionChanged();
        }
        return true;
    }
    if (! mods.isCommandDown() && ! mods.isAltDown())
    {
        if (code == 'R' || code == 'r') { toggleRecord(); return true; }
        if (code == 'L' || code == 'l') { toggleLoop(); return true; }
        if (code == 'M' || code == 'm') { addMarkerAtCursor(); return true; }
        if (code == '+' || code == '=') { zoomBy (2.0); return true; }
        if (code == '-')                { zoomBy (0.5); return true; }
        if (code == '0')                { zoomFit(); return true; }
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
        auto t = engine.getPosition();
        if (t > timeline.startSec + timeline.visibleSec() || t < timeline.startSec)
        {
            timeline.scrollTo (t - timeline.visibleSec() * 0.02);
            viewChanged();
        }
        trackStack.repaintAll();
    }
    else if (engine.isRecording())
    {
        if (auto* rec = engine.getRecordTarget())     // follow the recorded track, not the selected one
        {
            updateTimelineTotal();
            if (rec->getLengthSeconds() > timeline.startSec + timeline.visibleSec() * 0.95)
                timeline.scrollTo (rec->getLengthSeconds() - timeline.visibleSec() * 0.7);
        }
        trackStack.repaintAll();
    }

    if (wasPlaying && ! playing) trackStack.repaintAll();
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
    if (engine.isRecording() && engine.getRecordTarget() == d) cursorT = d->getLengthSeconds();
    statusBar.setInfo (fmt, formatTime (cursorT), d->hasSelection() ? sel : juce::String ("-"), selLen, transientMessage);
}

void MainComponent::updateZoomControls()
{
    const double fit = timeline.fitSecPerPixel();
    const double spp = timeline.effSecPerPixel();
    const double minSpp = juce::jmin (Timeline::minSecPerPixel, fit);
    double v = (fit <= minSpp) ? 0.0 : std::log (fit / spp) / std::log (fit / minSpp);
    v = juce::jlimit (0.0, 1.0, v);
    header.zoomSlider.setValue (v, juce::dontSendNotification);
    statusBar.zoomSlider.setValue (v, juce::dontSendNotification);
    statusBar.zoomBox.setText (juce::String ((int) std::round (fit / spp * 100.0)) + "%", juce::dontSendNotification);
    trackStack.updateZoomLabel();
}

double MainComponent::zoomAnchor() const
{
    auto* d = current();
    double anchor = timeline.startSec + timeline.visibleSec() * 0.5;
    if (d != nullptr)
    {
        double a = d->hasSelection() ? d->toSeconds ((d->selStart + d->selEnd) / 2) : d->toSeconds (d->cursor);
        if (a >= timeline.startSec && a <= timeline.startSec + timeline.visibleSec())
            anchor = a;
    }
    return anchor;
}

void MainComponent::zoomTo (double secPerPixel)
{
    const double anchor = zoomAnchor();
    timeline.zoomTo (secPerPixel, anchor, (anchor - timeline.startSec) / timeline.effSecPerPixel());
    viewChanged();
}

void MainComponent::zoomBy (double factor)
{
    zoomTo (timeline.effSecPerPixel() / factor);
}

void MainComponent::zoomFit()
{
    timeline.secPerPixel = 0.0;
    timeline.startSec = 0.0;
    viewChanged();
}

void MainComponent::zoomToSelection()
{
    auto* d = current();
    if (d == nullptr || ! d->hasSelection())
    {
        showMessage (T ("Select an area first, then zoom to it.", "Markér først et område og zoom så til det."));
        return;
    }
    const double a = d->toSeconds (d->selStart), b = d->toSeconds (d->selEnd);
    const double len = (b - a) * 1.06;
    timeline.zoomTo (len / timeline.width, a - (b - a) * 0.03, 0.0);
    timeline.scrollTo (a - (b - a) * 0.03);
    viewChanged();
}

void MainComponent::ensureVisibleSec (double t)
{
    if (t < timeline.startSec || t > timeline.startSec + timeline.visibleSec())
    {
        timeline.scrollTo (t - timeline.visibleSec() * 0.05);
        viewChanged();
    }
}

void MainComponent::viewChanged()
{
    trackStack.repaintAll();
    updateZoomControls();
}

void MainComponent::updateTimelineTotal()
{
    double total = 0.0;
    for (auto& d : documents)
        total = juce::jmax (total, d->getLengthSeconds());
    if (total <= 0.0) total = 30.0;
    if (std::abs (total - timeline.totalSec) > 1.0e-9)
    {
        timeline.setTotal (total);
        viewChanged();
    }
}

void MainComponent::setZoomFromSlider (double v)
{
    const double fit = timeline.fitSecPerPixel();
    const double minSpp = juce::jmin (Timeline::minSecPerPixel, fit);
    zoomTo (fit * std::pow (minSpp / fit, v));
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
    d->removeRange (d->toSamples (7.48), d->toSamples (8.3));      // show that pieces can have gaps
    d->clips.back().name = "electric_piano_phrase #2";
    d->rebuildPeaks();
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
    d2->addMarker (0.5, T ("Vocal start", "Vokal start"));
    d2->addMarker (4.5, T ("Second phrase", "Anden frase"));
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
    // Document editing (clips)
    {
        AudioDocument doc;
        doc.setBuffer (sine (100.0, 10.0, 0.5f), sr);
        doc.addMarker (8.0, "B");
        doc.addEnvelopePoint (9.0, 0.5f);

        doc.pushUndo ("cut");
        doc.removeRange (doc.toSamples (2.0), doc.toSamples (4.0));
        check (doc.clips.size() == 2 && std::abs (doc.getLengthSeconds() - 10.0) < 1e-6, "cut leaves a gap (2 clips, length unchanged)");
        check (std::abs (doc.markers[0].time - 8.0) < 1e-6, "markers stay after cut with gap");
        auto gap = doc.copyRange (doc.toSamples (2.5), doc.toSamples (3.5));
        check (DSP::getPeak (gap) == 0.0f, "gap is silent");

        // move second clip left to close the gap manually
        int second = doc.clips[0].start > doc.clips[1].start ? 0 : 1;
        doc.moveClip (second, doc.toSamples (2.0));
        check (std::abs (doc.getLengthSeconds() - 8.0) < 1e-3, "clip can be moved");
        // moving onto another clip is prevented (snaps next to it)
        doc.moveClip (second, doc.toSamples (1.0));
        check (doc.clips[0].end() <= doc.clips[1].start || doc.clips[1].end() <= doc.clips[0].start, "clips never overlap");

        check (doc.undo() && doc.clips.size() == 1 && std::abs (doc.getLengthSeconds() - 10.0) < 1e-6, "undo restores");
        check (doc.redo() && doc.clips.size() == 2, "redo re-applies");

        AudioDocument d2;
        d2.setBuffer (sine (100.0, 10.0, 0.5f), sr);
        d2.addMarker (8.0, "B");
        d2.rippleRemove (d2.toSamples (2.0), d2.toSamples (4.0));
        check (std::abs (d2.getLengthSeconds() - 8.0) < 1e-6 && std::abs (d2.markers[0].time - 6.0) < 1e-6, "delete-and-close-gap shifts audio and markers");

        d2.splitAt (d2.toSamples (5.0));
        check (d2.clips.size() == 3, "split creates clips");
        d2.joinRange (0, d2.getLength());
        check (d2.clips.size() == 1 && std::abs (d2.getLengthSeconds() - 8.0) < 1e-6, "join makes one clip");

        juce::AudioBuffer<float> ins (1, (int) sr);
        ins.clear();
        d2.insertClip (d2.toSamples (1.0), std::move (ins), "x");
        check (std::abs (d2.getLengthSeconds() - 9.0) < 1e-6 && d2.clips.size() == 3, "paste inside a clip inserts and pushes later audio");
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
            auto file = dir.getChildFile ("t_" + f.id + "." + f.extension);
            juce::String err;
            bool ok = FileIO::writeBuffer (b, sr, file, f, err);
            AudioDocument doc;
            bool loaded = ok && FileIO::loadIntoDocument (file, doc, err);
            double len = doc.getLengthSeconds();
            check (ok && loaded && std::abs (len - 1.0) < 0.1, "export+import " + f.name + " (" + juce::String (len, 3) + " s) " + err);
        }
        dir.deleteRecursively();

        // MP3 files are opened with JUCE's built-in decoder (JUCE_USE_MP3AUDIOFORMAT)
        check (FileIO::getWildcardForOpen().contains ("*.mp3"), "MP3 files can be opened");
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

    // cut leaves a gap / undo
    empty->selStart = 0; empty->selEnd = empty->toSamples (1.0);
    doCut();
    check (std::abs (empty->getLengthSeconds() - 4.0) < 0.001 && ! empty->hasAudioIn (0, empty->toSamples (1.0)), "cut 1 s leaves a gap");
    undo();
    check (empty->hasAudioIn (0, empty->toSamples (1.0)), "undo cut");

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
    check (voc->clips.size() == 1 && voc->clips[0].start == voc->toSamples (2.0) && std::abs (voc->getLengthSeconds() - 5.0) < 0.001,
           "trim keeps the selection in place");
    voc->moveClip (0, 0);
    check (std::abs (voc->getLengthSeconds() - 3.0) < 0.001, "move trimmed clip to start");

    // insert from another track with different sample rate (resampled)
    {
        juce::AudioBuffer<float> b (1, 48000);
        b.clear();
        voc->selStart = voc->selEnd = voc->cursor = 0;
        insertBufferAtCursor (std::move (b), 48000.0, "insert");
        check (std::abs (voc->getLengthSeconds() - 4.0) < 0.002, "insert 1 s @48k into 44.1k track");

        // move a clip to another track
        auto clipsBefore = empty->clips.size();
        moveClipToTrack (1, 0, 2);
        check (empty->clips.size() == clipsBefore + 1, "clip moved to another track");
    }

    // merge
    double rate = 0; bool norm = false;
    auto mix = mixTracks (allDocuments(), rate, norm);
    check (std::abs (mix.getNumSamples() / rate - piano->getLengthSeconds()) < 0.001, "mix length = longest track");
    check (DSP::getPeak (mix) <= 1.0f, "mix does not clip");

    // volume line is baked into a merge
    {
        AudioDocument a, b;
        juce::AudioBuffer<float> ones (1, 44100);
        for (int i = 0; i < ones.getNumSamples(); ++i) ones.setSample (0, i, 0.25f);
        juce::AudioBuffer<float> ones2; ones2.makeCopyOf (ones);
        a.setBuffer (std::move (ones), 44100.0);
        b.setBuffer (std::move (ones2), 44100.0);
        a.envelope = { { 0.0, 0.0f }, { 1.0, 0.0f } };   // track A fully faded out by its volume line
        double r = 0; bool n = false;
        auto m = mixTracks ({ &a, &b }, r, n);
        check (std::abs (m.getSample (0, 22050) - 0.25f) < 1.0e-4f, "merge includes volume line (A silent -> only B heard)");
        a.envelope = { { 0.0, 2.0f }, { 1.0, 2.0f } };   // +6 dB
        m = mixTracks ({ &a, &b }, r, n);
        check (std::abs (m.getSample (0, 22050) - 0.75f) < 1.0e-4f, "merge includes volume line (+6 dB)");
    }

    // envelope
    piano->addEnvelopePoint (1.0, 0.0f);
    check (piano->gainAt (1.0) < 0.001f, "envelope point sets gain");

    std::cout << (failures == 0 ? "ALL UI TESTS PASSED" : juce::String (failures) + " UI TEST(S) FAILED") << std::endl;
    for (auto& d : documents) d->modified = false;
    return failures;
}
