#pragma once
#include "../Engine/AudioEngine.h"
#include "../Presets/PresetManager.h"
#include "../Midi/MidiLearnManager.h"
#include "../Hosting/PluginHost.h"
#include "../Ui/PedalboardView.h"
#include "../Ui/SidePanels.h"
#include "../Ui/Visualizers.h"

namespace vp
{

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::ApplicationCommandTarget,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    // ApplicationCommandTarget (for future menu wiring; keyboard handled in keyPressed)
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override {}
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override {}
    bool perform (const InvocationInfo&) override { return false; }

private:
    void timerCallback() override;
    void updateHeader();

    AudioEngine engine;
    PresetManager presets { engine };
    MidiLearnManager midi { engine, presets };
    PluginHost pluginHost;

    VpLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    //==============================================================================
    class PowerButton : public juce::Button
    {
    public:
        PowerButton() : juce::Button ("power") { setClickingTogglesState (true); }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto b = getLocalBounds().toFloat().reduced (3.0f);
            const bool on = getToggleState();
            const auto c = on ? colours::good : colours::bad;
            if (on || over)
            {
                g.setColour (c.withAlpha (0.28f));
                g.fillEllipse (b.expanded (2.0f));
            }
            g.setColour (juce::Colour (0xff232833));
            g.fillEllipse (b);
            g.setColour (c);
            const float r = b.getWidth() * 0.26f;
            juce::Path p;
            p.addCentredArc (b.getCentreX(), b.getCentreY() + 1.0f, r, r, 0.0f, 0.7f,
                             juce::MathConstants<float>::twoPi - 0.7f, true);
            g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (b.getCentreX(), b.getY() + 4.0f, b.getCentreX(), b.getCentreY(), 2.4f);
        }
    };

    class RecordButton : public juce::Button
    {
    public:
        RecordButton() : juce::Button ("record") { setClickingTogglesState (true); }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto b = getLocalBounds().toFloat().reduced (3.0f);
            const bool on = getToggleState();
            if (on || over)
            {
                g.setColour (colours::bad.withAlpha (0.3f));
                g.fillEllipse (b.expanded (2.0f));
            }
            g.setColour (juce::Colour (0xff232833));
            g.fillEllipse (b);
            g.setColour (on ? colours::bad : colours::bad.withAlpha (0.75f));
            if (on)
                g.fillRoundedRectangle (b.reduced (b.getWidth() * 0.3f), 2.0f);   // stop square
            else
                g.fillEllipse (b.reduced (b.getWidth() * 0.28f));                 // record dot
        }
    };

    // header
    juce::Label titleLabel { {}, "VIRTUAL PEDALS" };
    juce::Label presetLabel;
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" };
    juce::Label statsLabel;
    PowerButton powerBtn;
    RecordButton recordBtn;
    juce::Label recLabel;
    int recMessageTicks = 0;

    // board
    juce::Viewport boardViewport;
    std::unique_ptr<PedalboardView> board;

    // right dock
    std::unique_ptr<SideDock> dock;

    // bottom strip
    LevelMeter inMeter { engine.inPeak, engine.inRms };
    LevelMeter outMeter { engine.outPeak, engine.outRms };
    juce::Label inMeterLabel { {}, "IN" }, outMeterLabel { {}, "OUT" };
    std::unique_ptr<SpectrumAnalyzer> spectrum;
    std::unique_ptr<Oscilloscope> scope;
    juce::TextButton visToggle { "Scope" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace vp
