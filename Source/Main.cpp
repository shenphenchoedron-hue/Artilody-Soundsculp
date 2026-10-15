#include <JuceHeader.h>
#include "MainComponent.h"
#include "ExportDialog.h"
#include <BinaryData.h>

//==============================================================================
class SoundsculpApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Artilody Soundsculp"; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "ArtilodySoundsculp";
        opts.filenameSuffix = ".settings";
        opts.folderName = "ArtilodySoundsculp";
        opts.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (opts);

        auto lang = properties.getUserSettings()->getValue ("language", {});
        if (lang.isEmpty())
            lang = "en";                       // the app is English by default
        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.trim();
        for (auto& a : args) a = a.unquoted();
        if (args.contains ("--da")) lang = "da";
        if (args.contains ("--en")) lang = "en";
        setDanish (lang == "da");

        if (args.contains ("--selftest"))
        {
            auto result = MainComponent::runSelfTest();
            setApplicationReturnValue (result);
            quit();
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (properties);

        if (args.contains ("--uitest"))
        {
            auto failures = mainWindow->getMain()->runUiTest();
            setApplicationReturnValue (failures == 0 ? 0 : 1);
            juce::MessageManager::callAsync ([] { quit(); });
            return;
        }

        auto exportShot = args.indexOf ("--screenshot-export");
        if (exportShot >= 0 && exportShot + 1 < args.size())
        {
            ExportDialog::Options o;
            o.hasTrack = o.hasSelection = o.multipleTracks = true;
            o.trackSeconds = 12.68; o.selectionSeconds = 4.24; o.mixSeconds = 12.68;
            o.trackName = "electric_piano_phrase.wav";
            o.folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
            o.lastFormatId = "wav24";
            ExportDialog dlg (o);
            auto img = dlg.createComponentSnapshot (dlg.getLocalBounds(), true, 1.0f);
            auto target = juce::File::getCurrentWorkingDirectory().getChildFile (args[exportShot + 1]);
            target.deleteFile();
            juce::FileOutputStream os (target);
            juce::PNGImageFormat().writeImageToStream (img, os);
            os.flush();
            juce::MessageManager::callAsync ([] { quit(); });
            return;
        }

        auto shotIndex = args.indexOf ("--screenshot");
        if (shotIndex >= 0 && shotIndex + 1 < args.size())
        {
            auto target = juce::File::getCurrentWorkingDirectory().getChildFile (args[shotIndex + 1]);
            auto* mc = mainWindow->getMain();
            mc->loadDemoContent();
            juce::Timer::callAfterDelay (1500, [this, target]
            {
                auto* mc2 = mainWindow->getMain();
                auto img = mc2->createComponentSnapshot (mc2->getLocalBounds(), true, 1.0f);
                target.deleteFile();
                juce::FileOutputStream os (target);
                juce::PNGImageFormat png;
                png.writeImageToStream (img, os);
                os.flush();
                std::cout << "Screenshot written: " << target.getFullPathName() << std::endl;
                quit();
            });
            return;
        }

        juce::Array<juce::File> files;
        for (auto& a : args)
            if (! a.startsWith ("--") && juce::File::isAbsolutePath (a) && juce::File (a).existsAsFile())
                files.add (juce::File (a));
            else if (! a.startsWith ("--") && juce::File::getCurrentWorkingDirectory().getChildFile (a).existsAsFile())
                files.add (juce::File::getCurrentWorkingDirectory().getChildFile (a));
        if (! files.isEmpty())
            mainWindow->getMain()->openFiles (files);
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            mainWindow->getMain()->saveSettings();
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr && mainWindow->getMain()->hasUnsavedChanges())
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Artilody Soundsculp",
                                                T ("Some tracks have changes that are not exported. Quit anyway?",
                                                   "Nogle spor har ændringer der ikke er eksporteret. Vil du afslutte alligevel?"),
                                                T ("Quit", "Afslut"), T ("Cancel", "Annuller"), nullptr,
                                                juce::ModalCallbackFunction::create ([] (int r) { if (r == 1) juce::JUCEApplication::quit(); }));
            return;
        }
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr) return;
        juce::Array<juce::File> files;
        for (auto& a : juce::StringArray::fromTokens (commandLine, true))
            if (juce::File (a.unquoted()).existsAsFile()) files.add (juce::File (a.unquoted()));
        mainWindow->getMain()->openFiles (files);
    }

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::ApplicationProperties& props)
            : DocumentWindow ("Artilody Soundsculp", Theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setIcon (juce::ImageCache::getFromMemory (BinaryData::icon_256_png, BinaryData::icon_256_pngSize));
            auto* mc = new MainComponent (props);
            mc->onTitleChanged = [this] (const juce::String& t) { setName (t); };
            setContentOwned (mc, true);
            setResizable (true, true);
            setResizeLimits (1100, 700, 10000, 10000);

            auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            setSize (juce::jmin (1600, area.getWidth() - 40), juce::jmin (940, area.getHeight() - 40));
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            mc->grabKeyboardFocus();
        }

        MainComponent* getMain()    { return dynamic_cast<MainComponent*> (getContentComponent()); }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    juce::ApplicationProperties properties;
    SoundsculpLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (SoundsculpApplication)
