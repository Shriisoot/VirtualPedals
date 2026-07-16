#include <JuceHeader.h>
#include "App/MainComponent.h"

//==============================================================================
class VirtualPedalsApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Virtual Pedals"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise (const juce::String&) override
    {
        fileLogger = juce::FileLogger::createDefaultAppLogger ("VirtualPedals", "log.txt",
                                                               "Virtual Pedals session log");
        juce::Logger::setCurrentLogger (fileLogger);
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::Logger::setCurrentLogger (nullptr);
        delete fileLogger;
    }

    void systemRequestedQuit() override { quit(); }

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Colour (0xff14161c),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new vp::MainComponent(), true);
            juce::Logger::writeToLog ("After setContentOwned, window bounds=" + getBounds().toString());
            setResizable (true, true);
            setResizeLimits (960, 600, 10000, 10000);
            centreWithSize (1280, 800);
            juce::Logger::writeToLog ("After centreWithSize, window bounds=" + getBounds().toString());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    juce::FileLogger* fileLogger = nullptr;
};

START_JUCE_APPLICATION (VirtualPedalsApplication)
