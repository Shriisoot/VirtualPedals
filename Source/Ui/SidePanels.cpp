#include "SidePanels.h"
#include "NodeEditor.h"

namespace vp
{

//==============================================================================
PresetPanel::PresetPanel (PresetManager& pm) : presets (pm)
{
    search.setTextToShowWhenEmpty ("Search presets...", colours::textDim);
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);

    list.setModel (this);
    list.setRowHeight (26);
    addAndMakeVisible (list);

    saveBtn.onClick = [this]
    {
        auto* alert = new juce::AlertWindow ("Save Preset", "Preset name:", juce::MessageBoxIconType::NoIcon);
        alert->addTextEditor ("name", presets.getCurrentPresetName());
        alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert] (int r)
        {
            if (r == 1)
            {
                const auto name = alert->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                {
                    presets.savePreset (name);
                    refresh();
                }
            }
            delete alert;
        }), false);
    };
    addAndMakeVisible (saveBtn);

    importBtn.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Import preset", juce::File(), "*.vpreset");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile())
                {
                    presets.importPreset (fc.getResult());
                    refresh();
                }
            });
    };
    addAndMakeVisible (importBtn);

    exportBtn.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (! juce::isPositiveAndBelow (row, shown.size()))
            return;
        const auto info = shown[row];
        auto chooser = std::make_shared<juce::FileChooser> ("Export preset",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (info.name + ".vpreset"),
            "*.vpreset");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser, info] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File())
                    presets.exportPreset (info, fc.getResult());
            });
    };
    addAndMakeVisible (exportBtn);

    refresh();
}

void PresetPanel::refresh()
{
    shown = presets.getPresets (search.getText());
    list.updateContent();
    list.repaint();
}

void PresetPanel::resized()
{
    auto b = getLocalBounds().reduced (6);
    search.setBounds (b.removeFromTop (26));
    b.removeFromTop (4);
    auto buttons = b.removeFromBottom (28);
    saveBtn.setBounds (buttons.removeFromLeft (buttons.getWidth() / 3).reduced (2, 0));
    importBtn.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
    exportBtn.setBounds (buttons.reduced (2, 0));
    b.removeFromBottom (4);
    list.setBounds (b);
}

int PresetPanel::getNumRows() { return shown.size(); }

void PresetPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, shown.size()))
        return;
    const auto& info = shown.getReference (row);
    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillRect (0, 0, w, h);
    }
    g.setColour (info.isFavourite ? colours::warn : colours::outline);
    g.setFont (14.0f);
    g.drawText (info.isFavourite ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85"))
                                 : info.isSong ? juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xaa")) : "-",
                6, 0, 18, h, juce::Justification::centred);
    g.setColour (colours::text);
    g.drawText (info.name, 26, 0, w - 90, h, juce::Justification::centredLeft);
    g.setColour (info.isSong ? colours::accent2.withAlpha (0.8f) : colours::textDim);
    g.setFont (11.0f);
    g.drawText (info.isSong ? "song" : info.isFactory ? "factory" : "user",
                w - 60, 0, 54, h, juce::Justification::centredRight);
}

void PresetPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, shown.size()))
    {
        presets.loadPreset (shown.getReference (row));
        if (onRigLoaded) onRigLoaded();
    }
}

void PresetPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || ! juce::isPositiveAndBelow (row, shown.size()))
        return;
    const auto info = shown[row];
    juce::PopupMenu m;
    m.addItem ("Load", [this, info] { presets.loadPreset (info); if (onRigLoaded) onRigLoaded(); });
    m.addItem (info.isFavourite ? "Remove favourite" : "Mark favourite",
               [this, info] { presets.setFavourite (info.name, ! info.isFavourite); refresh(); });
    if (! info.isFactory)
        m.addItem ("Delete", [this, info] { presets.deletePreset (info); refresh(); });
    m.showMenuAsync ({});
}

//==============================================================================
ToneDnaPanel::ToneDnaPanel (AudioEngine& e, PresetManager& pm) : engine (e), presets (pm)
{
    importBtn.onClick = [this] { chooseFile(); };
    addAndMakeVisible (importBtn);

    applyBtn.setEnabled (false);
    applyBtn.onClick = [this] { applyResult(); };
    addAndMakeVisible (applyBtn);

    abBtn.setClickingTogglesState (true);
    abBtn.setEnabled (false);
    abBtn.setTooltip ("Toggle between the reference track and your live rig");
    abBtn.onClick = [this] { engine.setReferencePlaying (abBtn.getToggleState()); };
    addAndMakeVisible (abBtn);

    saveBtn.setEnabled (false);
    saveBtn.onClick = [this]
    {
        presets.savePreset ("ToneDNA " + loadedFile.getFileNameWithoutExtension());
    };
    addAndMakeVisible (saveBtn);

    analyzer.onComplete = [this] (const ToneDnaResult& r)
    {
        result = r;
        statusText = r.valid ? "Analysis complete - confidence per block below."
                             : "Analysis failed: " + r.error;
        applyBtn.setEnabled (r.valid);
        saveBtn.setEnabled (false);
        repaint();
    };
}

void ToneDnaPanel::chooseFile()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Import reference audio",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile())
                return;
            loadedFile = f;
            statusText = "Analysing " + f.getFileName() + " ...";
            applyBtn.setEnabled (false);
            abBtn.setEnabled (false);
            repaint();
            if (ToneDnaAnalyzer::loadReferenceIntoEngine (f, engine))
                abBtn.setEnabled (true);
            analyzer.analyzeFile (f);
        });
}

void ToneDnaPanel::applyResult()
{
    presets.pushUndoState ("Tone DNA rig");
    ToneDnaAnalyzer::applyRig (result, engine);
    saveBtn.setEnabled (true);
    if (onRigApplied) onRigApplied();
}

void ToneDnaPanel::resized()
{
    auto b = getLocalBounds().reduced (6);
    importBtn.setBounds (b.removeFromTop (28));
    b.removeFromTop (4);
    auto row = b.removeFromTop (26);
    applyBtn.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
    abBtn.setBounds (row.reduced (2, 0));
    b.removeFromTop (2);
    saveBtn.setBounds (b.removeFromTop (24).reduced (40, 0));
}

void ToneDnaPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().reduced (8);
    b.removeFromTop (92);

    g.setColour (colours::text);
    g.setFont (13.0f);
    g.drawFittedText (statusText, b.removeFromTop (32), juce::Justification::topLeft, 2);
    b.removeFromTop (4);

    if (! result.valid)
        return;

    // measured summary
    g.setFont (11.0f);
    g.setColour (colours::textDim);
    juce::String summary;
    summary << "crest " << juce::String (result.crestDb, 1) << " dB   "
            << "range " << juce::String (result.dynamicRangeDb, 1) << " dB   "
            << "tilt " << juce::String (result.tiltDb, 1) << " dB\n"
            << "drive " << juce::String (result.saturation * 100.0, 0) << "%   "
            << "width " << juce::String (result.stereoWidth * 100.0, 0) << "%   "
            << "tail " << juce::String (result.reverbDecayS, 1) << " s";
    g.drawFittedText (summary, b.removeFromTop (34), juce::Justification::topLeft, 3);
    b.removeFromTop (6);

    // detected rig with confidence bars
    for (const auto& comp : result.rig)
    {
        auto row = b.removeFromTop (40);
        if (row.getHeight() < 36)
            break;
        g.setColour (colours::text);
        g.setFont (12.0f);
        g.drawText (comp.typeId + "   " + juce::String ((int) (comp.confidence * 100.0)) + "%",
                    row.removeFromTop (14), juce::Justification::left);
        auto barArea = row.removeFromTop (8).reduced (0, 1);
        g.setColour (colours::panelHi);
        g.fillRoundedRectangle (barArea.toFloat(), 3.0f);
        g.setColour (comp.confidence > 0.6 ? colours::good : comp.confidence > 0.35 ? colours::warn : colours::bad);
        g.fillRoundedRectangle (barArea.removeFromLeft ((int) (barArea.getWidth() * comp.confidence)).toFloat(), 3.0f);
        g.setColour (colours::textDim);
        g.setFont (10.0f);
        g.drawText (comp.description, row.removeFromTop (12), juce::Justification::left);
        b.removeFromTop (2);
    }
}

//==============================================================================
PluginPanel::PluginPanel (PluginHost& hostToUse, AudioEngine& e) : host (hostToUse), engine (e)
{
    search.setTextToShowWhenEmpty ("Search plugins...", colours::textDim);
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);

    list.setModel (this);
    list.setRowHeight (26);
    addAndMakeVisible (list);

    scanBtn.onClick = [this]
    {
        scanBtn.setEnabled (false);
        scanStatus.setText ("Scanning...", juce::dontSendNotification);
        host.startScan (
            [this] (float progress, const juce::String& name)
            {
                scanStatus.setText (juce::String ((int) (progress * 100.0f)) + "%  " + name, juce::dontSendNotification);
            },
            [this]
            {
                scanBtn.setEnabled (true);
                scanStatus.setText ("Scan complete.", juce::dontSendNotification);
                refresh();
            });
    };
    addAndMakeVisible (scanBtn);

    addPathBtn.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Add a VST3 folder or pick a .vst3 file",
                                                            juce::File(), "*.vst3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories
                              | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                const auto result = fc.getResult();
                if (result == juce::File())
                    return;

                if (result.hasFileExtension ("vst3"))
                {
                    // a .vst3 can be a single file or a bundle folder - scan it directly
                    const int found = host.scanSingleFile (result);
                    scanStatus.setText (found > 0 ? juce::String (found) + " plugin(s) added from " + result.getFileName()
                                                  : "No loadable VST3 found in " + result.getFileName(),
                                        juce::dontSendNotification);
                    refresh();
                }
                else if (result.isDirectory())
                {
                    host.addSearchPath (result);
                    scanStatus.setText ("Folder added - scanning...", juce::dontSendNotification);
                    scanBtn.onClick(); // rescan immediately so new plugins appear
                }
            });
    };
    addAndMakeVisible (addPathBtn);

    scanStatus.setColour (juce::Label::textColourId, colours::textDim);
    scanStatus.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    scanStatus.setText ("Plugins never scanned yet.", juce::dontSendNotification);
    addAndMakeVisible (scanStatus);

    refresh();
}

void PluginPanel::refresh()
{
    shown = host.getPlugins (search.getText());
    list.updateContent();
    list.repaint();
}

void PluginPanel::resized()
{
    auto b = getLocalBounds().reduced (6);
    auto row = b.removeFromTop (28);
    scanBtn.setBounds (row.removeFromLeft (row.getWidth() * 2 / 3).reduced (2, 0));
    addPathBtn.setBounds (row.reduced (2, 0));
    b.removeFromTop (2);
    scanStatus.setBounds (b.removeFromTop (16));
    b.removeFromTop (2);
    search.setBounds (b.removeFromTop (26));
    b.removeFromTop (4);
    list.setBounds (b);
}

int PluginPanel::getNumRows() { return shown.size(); }

void PluginPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, shown.size()))
        return;
    const auto& d = shown.getReference (row);
    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillRect (0, 0, w, h);
    }
    const bool fav = host.isFavourite (d.createIdentifierString());
    g.setColour (fav ? colours::warn : colours::outline);
    g.setFont (13.0f);
    g.drawText (fav ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) : "-", 6, 0, 16, h, juce::Justification::centred);
    g.setColour (colours::text);
    g.drawText (d.name, 26, 0, w - 130, h, juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (11.0f);
    g.drawText (d.manufacturerName, w - 104, 0, 98, h, juce::Justification::centredRight);
}

void PluginPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, shown.size()) && onAddPlugin)
        onAddPlugin (shown.getReference (row));
}

void PluginPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || ! juce::isPositiveAndBelow (row, shown.size()))
        return;
    const auto d = shown[row];
    const auto id = d.createIdentifierString();
    juce::PopupMenu m;
    m.addItem ("Add to board", [this, d] { if (onAddPlugin) onAddPlugin (d); });
    m.addItem (host.isFavourite (id) ? "Remove favourite" : "Mark favourite",
               [this, id] { host.setFavourite (id, ! host.isFavourite (id)); refresh(); });
    m.showMenuAsync ({});
}

//==============================================================================
SettingsPanel::SettingsPanel (AudioEngine& e) : engine (e)
{
    deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(), 1, 2, 2, 2, true, false, true, false);
    addAndMakeVisible (*deviceSelector);

    auto setupGain = [this] (juce::Slider& s, juce::Label& l, std::atomic<double>& target)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setRange (-24.0, 24.0, 0.1);
        s.setValue (target.load(), juce::dontSendNotification);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
        s.setTextValueSuffix (" dB");
        s.onValueChange = [&s, &target] { target.store (s.getValue()); };
        l.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    };
    setupGain (inputGain, inputLabel, engine.inputGainDb);
    setupGain (masterGain, masterLabel, engine.masterGainDb);

    limiterToggle.setToggleState (engine.limiterOn.load(), juce::dontSendNotification);
    limiterToggle.onClick = [this] { engine.limiterOn.store (limiterToggle.getToggleState()); };
    addAndMakeVisible (limiterToggle);

    statsLabel.setColour (juce::Label::textColourId, colours::textDim);
    statsLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
    addAndMakeVisible (statsLabel);

    startTimerHz (2);
}

void SettingsPanel::timerCallback()
{
    const double sr = engine.getSampleRate();
    const int lat = engine.getLatencySamples();
    statsLabel.setText (juce::String ("CPU ") + juce::String (engine.getCpuUsage() * 100.0, 1) + "%    "
                        + "Latency " + juce::String (lat * 1000.0 / juce::jmax (1.0, sr), 1) + " ms ("
                        + juce::String (lat) + " smp)    "
                        + juce::String (sr / 1000.0, 1) + " kHz",
                        juce::dontSendNotification);
}

void SettingsPanel::resized()
{
    auto b = getLocalBounds().reduced (6);
    statsLabel.setBounds (b.removeFromTop (20));
    auto row = b.removeFromTop (24);
    inputLabel.setBounds (row.removeFromLeft (74));
    inputGain.setBounds (row);
    row = b.removeFromTop (24);
    masterLabel.setBounds (row.removeFromLeft (74));
    masterGain.setBounds (row);
    limiterToggle.setBounds (b.removeFromTop (24));
    b.removeFromTop (4);
    deviceSelector->setBounds (b);
}

void SettingsPanel::paint (juce::Graphics&) {}

//==============================================================================
SideDock::SideDock (AudioEngine& e, PresetManager& pm, PluginHost& host)
    : presetsPanel (pm), toneDnaPanel (e, pm), pluginsPanel (host, e), settingsPanel (e)
{
    tabs.setTabBarDepth (28);
    tabs.addTab ("Presets", colours::panel, &presetsPanel, false);
    tabs.addTab ("Tone DNA", colours::panel, &toneDnaPanel, false);
    tabs.addTab ("Plugins", colours::panel, &pluginsPanel, false);
    tabs.addTab ("Builder", colours::panel, new NodeEditor(), true);
    tabs.addTab ("Audio", colours::panel, &settingsPanel, false);
    addAndMakeVisible (tabs);
}

void SideDock::resized()
{
    tabs.setBounds (getLocalBounds());
}

} // namespace vp
