#pragma once
#include "../Engine/AudioEngine.h"
#include "../Presets/PresetManager.h"
#include "../Hosting/PluginHost.h"
#include "../ToneDna/ToneDnaAnalyzer.h"
#include "Visualizers.h"

namespace vp
{

//==============================================================================
class PresetPanel : public juce::Component, private juce::ListBoxModel
{
public:
    PresetPanel (PresetManager& pm);

    void resized() override;
    void refresh();

    std::function<void()> onRigLoaded;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;

    PresetManager& presets;
    juce::Array<PresetManager::PresetInfo> shown;
    juce::TextEditor search;
    juce::ListBox list;
    juce::TextButton saveBtn { "Save As..." }, importBtn { "Import" }, exportBtn { "Export" };
};

//==============================================================================
class ToneDnaPanel : public juce::Component
{
public:
    ToneDnaPanel (AudioEngine& e, PresetManager& pm);

    void resized() override;
    void paint (juce::Graphics& g) override;

    std::function<void()> onRigApplied;

private:
    void chooseFile();
    void applyResult();

    AudioEngine& engine;
    PresetManager& presets;
    ToneDnaAnalyzer analyzer;
    ToneDnaResult result;
    juce::String statusText { "Import a track to analyse its tone." };
    juce::File loadedFile;

    juce::TextButton importBtn { "Import Audio..." }, applyBtn { "Build This Rig" },
                     abBtn { "A/B: Reference" }, saveBtn { "Save as Preset" };
};

//==============================================================================
class PluginPanel : public juce::Component, private juce::ListBoxModel
{
public:
    PluginPanel (PluginHost& hostToUse, AudioEngine& e);

    void resized() override;
    void refresh();

    std::function<void (const juce::PluginDescription&)> onAddPlugin;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;

    PluginHost& host;
    AudioEngine& engine;
    juce::Array<juce::PluginDescription> shown;
    juce::TextEditor search;
    juce::ListBox list;
    juce::TextButton scanBtn { "Scan VST3 Folders" }, addPathBtn { "Add Folder..." };
    juce::Label scanStatus;
};

//==============================================================================
class SettingsPanel : public juce::Component, private juce::Timer
{
public:
    SettingsPanel (AudioEngine& e);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    AudioEngine& engine;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    juce::Slider inputGain, masterGain;
    juce::Label inputLabel { {}, "Input Gain" }, masterLabel { {}, "Master" };
    juce::ToggleButton limiterToggle { "Output limiter (recommended)" };
    juce::Label statsLabel;
};

//==============================================================================
// Tabbed right-hand dock hosting all panels + visualizers.
class SideDock : public juce::Component
{
public:
    SideDock (AudioEngine& e, PresetManager& pm, PluginHost& host);

    void resized() override;

    PresetPanel presetsPanel;
    ToneDnaPanel toneDnaPanel;
    PluginPanel pluginsPanel;
    SettingsPanel settingsPanel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};

} // namespace vp
