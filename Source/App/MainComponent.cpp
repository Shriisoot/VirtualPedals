#include "MainComponent.h"

namespace vp
{

MainComponent::MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    engine.initialise();

    // hosted-plugin restore hook must exist before the session is restored
    PedalChain::hostedPedalRestorer = [this] (const juce::var& v, double sr, int blockSize)
    {
        return pluginHost.restoreFromVar (v, sr, blockSize);
    };

    presets.restoreSession();
    midi.resolve();

    //==========================================================================
    titleLabel.setFont (juce::Font (juce::FontOptions().withHeight (18.0f).withStyle ("Bold")));
    titleLabel.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (titleLabel);

    presetLabel.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (presetLabel);

    undoBtn.onClick = [this] { presets.undo(); board->refresh(); midi.resolve(); };
    redoBtn.onClick = [this] { presets.redo(); board->refresh(); midi.resolve(); };
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);

    statsLabel.setColour (juce::Label::textColourId, colours::textDim);
    statsLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
    statsLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statsLabel);

    powerBtn.setToggleState (true, juce::dontSendNotification);
    powerBtn.setTooltip ("Power: on = rig active, off = silence");
    powerBtn.onClick = [this] { engine.powerOn.store (powerBtn.getToggleState()); };
    addAndMakeVisible (powerBtn);

    recordBtn.setTooltip ("Record the processed output to a 24-bit WAV");
    recordBtn.onClick = [this]
    {
        if (recordBtn.getToggleState())
        {
            if (! engine.startRecording())
            {
                recordBtn.setToggleState (false, juce::dontSendNotification);
                recLabel.setText ("Could not open recording file", juce::dontSendNotification);
                return;
            }
            recLabel.setText ("REC 0:00", juce::dontSendNotification);
        }
        else
        {
            const auto f = engine.stopRecording();
            recLabel.setText ("Saved " + f.getFileName(), juce::dontSendNotification);
            recMessageTicks = 12; // ~6 s at the 2 Hz timer
        }
    };
    addAndMakeVisible (recordBtn);

    recLabel.setColour (juce::Label::textColourId, colours::bad.brighter (0.3f));
    recLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
    addAndMakeVisible (recLabel);

    //==========================================================================
    board = std::make_unique<PedalboardView> (engine, presets, midi, pluginHost);
    board->onChainChanged = [this] { presets.saveSession(); updateHeader(); };
    boardViewport.setViewedComponent (board.get(), false);
    boardViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (boardViewport);

    dock = std::make_unique<SideDock> (engine, presets, pluginHost);
    dock->presetsPanel.onRigLoaded = [this] { board->refresh(); midi.resolve(); updateHeader(); };
    dock->toneDnaPanel.onRigApplied = [this] { board->refresh(); midi.resolve(); updateHeader(); };
    dock->pluginsPanel.onAddPlugin = [this] (const juce::PluginDescription& d)
    {
        auto pedal = pluginHost.createPlugin (d, engine.getSampleRate(), 512);
        if (pedal == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Plugin", "Could not load this plugin.");
            return;
        }
        presets.pushUndoState ("add plugin");
        Pedal* raw = pedal.release();
        engine.editChain ([raw] (PedalChain& c) { c.addPedal (std::unique_ptr<Pedal> (raw)); });
        board->refresh();
        midi.resolve();
    };
    addAndMakeVisible (*dock);

    presets.onPresetChanged = [this] { updateHeader(); };

    //==========================================================================
    inMeterLabel.setColour (juce::Label::textColourId, colours::textDim);
    outMeterLabel.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (inMeterLabel);
    addAndMakeVisible (outMeterLabel);

    spectrum = std::make_unique<SpectrumAnalyzer> (engine);
    addAndMakeVisible (*spectrum);

    visToggle.setClickingTogglesState (true);
    visToggle.onClick = [this]
    {
        const bool showScope = visToggle.getToggleState();
        if (showScope && scope == nullptr)
        {
            spectrum = nullptr; // only one visualizer reads the tap at a time
            scope = std::make_unique<Oscilloscope> (engine);
            addAndMakeVisible (*scope);
        }
        else if (! showScope && spectrum == nullptr)
        {
            scope = nullptr;
            spectrum = std::make_unique<SpectrumAnalyzer> (engine);
            addAndMakeVisible (*spectrum);
        }
        resized();
    };
    addAndMakeVisible (visToggle);

    board->refresh();
    updateHeader();
    startTimerHz (2);
    setSize (1280, 760);
    juce::Logger::writeToLog ("MainComponent constructed, bounds=" + getBounds().toString());
}

MainComponent::~MainComponent()
{
    presets.saveSession();
    PedalChain::hostedPedalRestorer = nullptr;
    engine.shutdown();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::updateHeader()
{
    presetLabel.setText ("Preset: " + presets.getCurrentPresetName(), juce::dontSendNotification);
    undoBtn.setEnabled (presets.canUndo());
    redoBtn.setEnabled (presets.canRedo());
}

void MainComponent::timerCallback()
{
    const double sr = engine.getSampleRate();
    const int lat = engine.getLatencySamples();
    statsLabel.setText ("CPU " + juce::String (engine.getCpuUsage() * 100.0, 1) + "%   "
                        + juce::String (lat * 1000.0 / juce::jmax (1.0, sr), 1) + " ms",
                        juce::dontSendNotification);

    if (engine.isRecording())
    {
        const int secs = (int) engine.getRecordedSeconds();
        recLabel.setText ("REC " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2),
                          juce::dontSendNotification);
    }
    else if (recMessageTicks > 0 && --recMessageTicks == 0)
    {
        recLabel.setText ("", juce::dontSendNotification);
    }

    updateHeader();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        presets.undo(); board->refresh(); midi.resolve();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0))
    {
        presets.redo(); board->refresh(); midi.resolve();
        return true;
    }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        presets.saveSession();
        return true;
    }
    return false;
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    juce::ColourGradient hg (juce::Colour (0xff252b3c), 0.0f, 0.0f,
                             juce::Colour (0xff161a24), 0.0f, 40.0f, false);
    g.setGradientFill (hg);
    g.fillRect (0, 0, getWidth(), 40);
    g.setColour (colours::accent.withAlpha (0.45f));
    g.drawHorizontalLine (40, 0.0f, (float) getWidth());
    g.setColour (colours::accent.withAlpha (0.12f));
    g.drawHorizontalLine (41, 0.0f, (float) getWidth());
}

void MainComponent::resized()
{
    auto b = getLocalBounds();

    // header
    auto header = b.removeFromTop (40).reduced (8, 5);
    powerBtn.setBounds (header.removeFromLeft (30));
    header.removeFromLeft (4);
    titleLabel.setBounds (header.removeFromLeft (170));
    presetLabel.setBounds (header.removeFromLeft (220));
    statsLabel.setBounds (header.removeFromRight (170));
    redoBtn.setBounds (header.removeFromRight (56).reduced (2));
    undoBtn.setBounds (header.removeFromRight (56).reduced (2));
    header.removeFromRight (6);
    recordBtn.setBounds (header.removeFromRight (30));
    recLabel.setBounds (header.removeFromRight (150));

    // right dock
    dock->setBounds (b.removeFromRight (330));

    // bottom strip: meters + visualizer
    auto bottom = b.removeFromBottom (120).reduced (6);
    auto meters = bottom.removeFromLeft (66);
    inMeterLabel.setBounds (meters.removeFromTop (14).removeFromLeft (30));
    auto meterCols = meters;
    inMeter.setBounds (meterCols.removeFromLeft (26).reduced (2));
    outMeterLabel.setBounds (juce::Rectangle<int> (inMeter.getRight() + 4, inMeterLabel.getY(), 34, 14));
    outMeter.setBounds (meterCols.removeFromLeft (26).reduced (2));
    visToggle.setBounds (bottom.removeFromRight (64).withHeight (24));
    if (spectrum) spectrum->setBounds (bottom.reduced (2));
    if (scope)    scope->setBounds (bottom.reduced (2));

    // board
    boardViewport.setBounds (b);
    if (board)
        board->setSize (juce::jmax (board->getWidth(), b.getWidth()), b.getHeight() - 12);
}

} // namespace vp
