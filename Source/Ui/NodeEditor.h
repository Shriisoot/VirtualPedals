#pragma once
#include "../Engine/NodeGraph.h"
#include "LookAndFeel.h"

namespace vp
{

//==============================================================================
// Canvas UI for building custom pedals: drag nodes, wire ports, edit params,
// save to the custom-pedal library.
class NodeEditor : public juce::Component
{
public:
    NodeEditor();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    std::function<void()> onLibraryChanged; // after save, so add-menus refresh

private:
    struct PortHit { int nodeId = 0; bool isOutput = false; int port = 0; bool valid = false; };

    juce::Rectangle<float> nodeBounds (const NodeGraph::Node& n) const;
    juce::Point<float> portPos (const NodeGraph::Node& n, bool output, int port) const;
    PortHit hitTestPort (juce::Point<float> pos) const;
    NodeGraph::Node* hitTestNode (juce::Point<float> pos) const;
    void showAddNodeMenu (juce::Point<float> pos);
    void rebuildParamPanel();
    void saveCurrent();
    void loadExisting();

    NodeGraph graph;
    int selectedNodeId = 0;
    int draggingNodeId = 0;
    juce::Point<float> dragOffset;
    PortHit wireStart;
    juce::Point<float> wireEnd;
    bool draggingWire = false;

    juce::TextEditor nameEdit;
    juce::TextButton saveBtn { "Save Pedal" }, newBtn { "New" }, loadBtn { "Load..." };
    juce::OwnedArray<juce::Component> paramControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeEditor)
};

} // namespace vp
