#pragma once
#include "PedalComponent.h"
#include "../Engine/AudioEngine.h"
#include "../Presets/PresetManager.h"
#include "../Hosting/PluginHost.h"

namespace vp
{

//==============================================================================
// The signal-chain canvas: pedals left->right, parallel splits as stacked lanes,
// drag-and-drop reordering, "+" menus for adding pedals anywhere.
class PedalboardView : public juce::Component,
                       public juce::DragAndDropTarget
{
public:
    PedalboardView (AudioEngine& e, PresetManager& p, MidiLearnManager& m, PluginHost& host);

    void refresh();                       // rebuild from engine chain state
    void resized() override;
    void paint (juce::Graphics& g) override;

    //==============================================================================
    bool isInterestedInDragSource (const SourceDetails& s) override;
    void itemDropped (const SourceDetails& s) override;
    void itemDragMove (const SourceDetails& s) override;
    void itemDragExit (const SourceDetails& s) override;

    std::function<void()> onChainChanged;
    std::function<void (const juce::PluginDescription&)> onAddPluginRequest; // handled upstream

    void showAddMenu (int insertIndex, PedalChain* targetChain = nullptr);

private:
    struct Row;
    void rebuildLayout();
    void performEdit (const juce::String& name, std::function<void (PedalChain&)> edit);
    juce::Component* makeItemComponent (ChainItem& item, int index);

    AudioEngine& engine;
    PresetManager& presets;
    MidiLearnManager& midi;
    PluginHost& pluginHost;

    juce::OwnedArray<juce::Component> itemComps;    // pedal faces + group frames
    std::unique_ptr<juce::TextButton> addBtn;
    juce::Viewport* viewportIfAny = nullptr;
    int dropIndicatorIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardView)
};

} // namespace vp
