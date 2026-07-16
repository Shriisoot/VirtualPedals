#include "NodeEditor.h"

namespace vp
{

static constexpr float nodeW = 120.0f, nodeH = 54.0f;

NodeEditor::NodeEditor()
{
    nameEdit.setTextToShowWhenEmpty ("My Pedal", colours::textDim);
    addAndMakeVisible (nameEdit);

    saveBtn.onClick = [this] { saveCurrent(); };
    addAndMakeVisible (saveBtn);

    newBtn.onClick = [this]
    {
        graph = NodeGraph();
        graph.addNode ("input", 16.0f, 90.0f);
        graph.addNode ("output", 170.0f, 300.0f);
        selectedNodeId = 0;
        rebuildParamPanel();
        repaint();
    };
    addAndMakeVisible (newBtn);

    loadBtn.onClick = [this] { loadExisting(); };
    addAndMakeVisible (loadBtn);

    // start with a useful template
    graph.addNode ("input", 16.0f, 90.0f);
    graph.addNode ("output", 170.0f, 300.0f);
}

void NodeEditor::resized()
{
    auto top = getLocalBounds().removeFromTop (30).reduced (4, 2);
    newBtn.setBounds (top.removeFromLeft (52));
    top.removeFromLeft (4);
    loadBtn.setBounds (top.removeFromLeft (64));
    top.removeFromLeft (4);
    saveBtn.setBounds (top.removeFromRight (90));
    top.removeFromRight (4);
    nameEdit.setBounds (top);
    rebuildParamPanel();
}

//==============================================================================
juce::Rectangle<float> NodeEditor::nodeBounds (const NodeGraph::Node& n) const
{
    return { n.x, n.y, nodeW, nodeH };
}

juce::Point<float> NodeEditor::portPos (const NodeGraph::Node& n, bool output, int port) const
{
    const auto b = nodeBounds (n);
    if (output)
        return { b.getRight(), b.getCentreY() };
    const int numIns = n.def->numAudioIns + (n.def->hasModIn ? 1 : 0)
                     + (n.type == "mixer" ? 0 : 0);
    const float span = b.getHeight() / (float) (juce::jmax (1, numIns) + 1);
    return { b.getX(), b.getY() + span * (float) (port + 1) };
}

NodeEditor::PortHit NodeEditor::hitTestPort (juce::Point<float> pos) const
{
    for (const auto& n : graph.nodes)
    {
        if (n->type != "output" && pos.getDistanceFrom (portPos (*n, true, 0)) < 9.0f)
            return { n->id, true, 0, true };
        const int numIns = (n->type == "mixer" ? 2 : n->def->numAudioIns + (n->def->hasModIn ? 1 : 0));
        for (int p = 0; p < numIns; ++p)
            if (pos.getDistanceFrom (portPos (*n, false, p)) < 9.0f)
                return { n->id, false, p, true };
    }
    return {};
}

NodeGraph::Node* NodeEditor::hitTestNode (juce::Point<float> pos) const
{
    for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it)
        if (nodeBounds (**it).contains (pos))
            return it->get();
    return nullptr;
}

//==============================================================================
void NodeEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    auto canvas = getLocalBounds().withTrimmedTop (30).withTrimmedBottom (86).toFloat();
    g.setColour (colours::panel.withAlpha (0.5f));
    g.fillRect (canvas);

    // connections
    for (const auto& c : graph.connections)
    {
        auto* src = graph.findNode (c.srcNode);
        auto* dst = graph.findNode (c.dstNode);
        if (src == nullptr || dst == nullptr)
            continue;
        const auto p1 = portPos (*src, true, 0);
        const auto p2 = portPos (*dst, false, c.dstPort);
        juce::Path path;
        path.startNewSubPath (p1);
        const float dx = juce::jmax (30.0f, std::abs (p2.x - p1.x) * 0.5f);
        path.cubicTo (p1.x + dx, p1.y, p2.x - dx, p2.y, p2.x, p2.y);
        g.setColour (c.dstPort == 0 ? colours::accent.withAlpha (0.8f) : colours::accent2.withAlpha (0.8f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    if (draggingWire && wireStart.valid)
    {
        auto* src = graph.findNode (wireStart.nodeId);
        if (src != nullptr)
        {
            const auto p1 = portPos (*src, wireStart.isOutput, wireStart.port);
            g.setColour (colours::warn);
            g.drawLine ({ p1, wireEnd }, 2.0f);
        }
    }

    // nodes
    for (const auto& n : graph.nodes)
    {
        const auto b = nodeBounds (*n);
        const bool selected = n->id == selectedNodeId;
        g.setColour (colours::panelHi);
        g.fillRoundedRectangle (b, 7.0f);
        g.setColour (selected ? colours::accent : colours::outline);
        g.drawRoundedRectangle (b, 7.0f, selected ? 2.0f : 1.0f);

        g.setColour (colours::text);
        g.setFont (13.0f);
        g.drawText (n->def->name, b.reduced (6.0f, 4.0f).removeFromTop (16.0f), juce::Justification::centredLeft);

        g.setFont (10.0f);
        g.setColour (colours::textDim);
        juce::String info;
        if (n->pedal != nullptr)          info = "full DSP block";
        else if (! n->values.empty())     info = juce::String ((int) n->values.size()) + " params";
        g.drawText (info, b.reduced (6.0f, 4.0f).removeFromBottom (14.0f), juce::Justification::centredLeft);

        // ports
        if (n->type != "output")
        {
            g.setColour (colours::accent);
            const auto p = portPos (*n, true, 0);
            g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
        }
        const int numIns = (n->type == "mixer" ? 2 : n->def->numAudioIns + (n->def->hasModIn ? 1 : 0));
        for (int p = 0; p < numIns; ++p)
        {
            const bool isMod = (n->def->hasModIn && p == n->def->numAudioIns) || (n->type == "mixer" && p == 1);
            g.setColour (isMod ? colours::accent2 : colours::accent);
            const auto pp = portPos (*n, false, p);
            g.fillEllipse (pp.x - 5.0f, pp.y - 5.0f, 10.0f, 10.0f);
        }
    }

    g.setColour (colours::textDim);
    g.setFont (11.0f);
    g.drawText ("Right-click canvas: add module. Right-click node: delete. Drag out-port to in-port to wire. "
                "Orange port = mod/second input.",
                getLocalBounds().withTrimmedTop (30).removeFromTop (18).reduced (8, 0),
                juce::Justification::centredLeft);
}

//==============================================================================
void NodeEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;

    if (e.mods.isPopupMenu())
    {
        if (auto* n = hitTestNode (pos))
        {
            if (n->type != "input" && n->type != "output")
            {
                juce::PopupMenu m;
                m.addItem ("Delete " + juce::String (n->def->name), [this, id = n->id]
                {
                    graph.removeNode (id);
                    if (selectedNodeId == id) { selectedNodeId = 0; rebuildParamPanel(); }
                    repaint();
                });
                m.showMenuAsync ({});
            }
        }
        else
        {
            showAddNodeMenu (pos);
        }
        return;
    }

    const auto port = hitTestPort (pos);
    if (port.valid && port.isOutput)
    {
        wireStart = port;
        draggingWire = true;
        wireEnd = pos;
        return;
    }
    if (port.valid && ! port.isOutput)
    {
        // clicking a wired input disconnects it
        graph.disconnectInput (port.nodeId, port.port);
        repaint();
        return;
    }

    if (auto* n = hitTestNode (pos))
    {
        selectedNodeId = n->id;
        draggingNodeId = n->id;
        dragOffset = pos - juce::Point<float> (n->x, n->y);
        rebuildParamPanel();
        repaint();
    }
    else if (selectedNodeId != 0)
    {
        selectedNodeId = 0;
        rebuildParamPanel();
        repaint();
    }
}

void NodeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingWire)
    {
        wireEnd = e.position;
        repaint();
        return;
    }
    if (draggingNodeId != 0)
    {
        if (auto* n = graph.findNode (draggingNodeId))
        {
            n->x = e.position.x - dragOffset.x;
            n->y = juce::jmax (50.0f, e.position.y - dragOffset.y);
            repaint();
        }
    }
}

void NodeEditor::mouseUp (const juce::MouseEvent& e)
{
    if (draggingWire)
    {
        const auto port = hitTestPort (e.position);
        if (port.valid && ! port.isOutput && wireStart.valid)
            graph.connect (wireStart.nodeId, port.nodeId, port.port);
        draggingWire = false;
        wireStart = {};
        repaint();
    }
    draggingNodeId = 0;
}

//==============================================================================
void NodeEditor::showAddNodeMenu (juce::Point<float> pos)
{
    juce::PopupMenu m;
    int id = 1;
    for (const auto& def : NodeGraph::nodeDefs())
    {
        if (juce::String (def.type) == "input" || juce::String (def.type) == "output")
            continue;
        m.addItem (id, def.name);
        ++id;
    }
    m.showMenuAsync ({}, [this, pos] (int result)
    {
        if (result == 0)
            return;
        int id = 1;
        for (const auto& def : NodeGraph::nodeDefs())
        {
            if (juce::String (def.type) == "input" || juce::String (def.type) == "output")
                continue;
            if (id == result)
            {
                graph.addNode (def.type, pos.x, pos.y);
                repaint();
                return;
            }
            ++id;
        }
    });
}

void NodeEditor::rebuildParamPanel()
{
    paramControls.clear();
    auto* node = graph.findNode (selectedNodeId);
    auto area = getLocalBounds().removeFromBottom (82).reduced (6, 4);
    if (node == nullptr)
        return;

    // primitive params as horizontal sliders; pedal-wrapping nodes expose pedal params
    auto addSlider = [&] (const juce::String& label, double min, double max, double value,
                          std::function<void (double)> setter)
    {
        auto* l = new juce::Label ({}, label);
        l->setColour (juce::Label::textColourId, colours::textDim);
        l->setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
        auto* s = new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        s->setRange (min, max, 0.001);
        s->setValue (value, juce::dontSendNotification);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16);
        s->onValueChange = [s, setter] { setter (s->getValue()); };
        paramControls.add (l);
        paramControls.add (s);
        addAndMakeVisible (l);
        addAndMakeVisible (s);
    };

    if (node->pedal != nullptr)
    {
        for (auto* p : node->pedal->getParams())
        {
            Param* param = p;
            addSlider (p->def.name, p->def.min, p->def.max, p->get(), [param] (double v) { param->set (v); });
            if (paramControls.size() >= 12) break; // fits 6 sliders
        }
    }
    else if (node->def != nullptr)
    {
        for (const auto& pd : node->def->params)
        {
            const juce::String pid = pd.id;
            addSlider (pd.name, pd.min, pd.max, node->value (pid, pd.def),
                       [node, pid] (double v) { node->values[pid] = v; });
        }
    }

    // lay out in two columns
    const int cols = 2;
    const int pairs = paramControls.size() / 2;
    const int rows = (pairs + cols - 1) / cols;
    const int cellW = area.getWidth() / cols;
    const int cellH = juce::jmax (18, area.getHeight() / juce::jmax (1, rows));
    for (int i = 0; i < pairs; ++i)
    {
        const int row = i / cols, col = i % cols;
        juce::Rectangle<int> cell (area.getX() + col * cellW, area.getY() + row * cellH, cellW - 8, cellH);
        paramControls[i * 2]->setBounds (cell.removeFromLeft (78));
        paramControls[i * 2 + 1]->setBounds (cell);
    }
}

//==============================================================================
void NodeEditor::saveCurrent()
{
    auto name = nameEdit.getText().trim();
    if (name.isEmpty())
        name = "My Pedal";

    bool hasIn = false, hasOut = false;
    for (const auto& n : graph.nodes)
    {
        hasIn |= n->type == "input";
        hasOut |= n->type == "output";
    }
    if (! hasIn || ! hasOut)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Custom Pedal",
                                                "The graph needs both an Input and an Output node.");
        return;
    }

    NodeGraphLibrary::save (name, graph.toVar());
    if (onLibraryChanged) onLibraryChanged();
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Custom Pedal",
        "Saved \"" + name + "\". Add it to your board from  + Add > Custom.");
}

void NodeEditor::loadExisting()
{
    juce::PopupMenu m;
    const auto files = NodeGraphLibrary::listCustomPedals();
    for (int i = 0; i < files.size(); ++i)
        m.addItem (i + 1, files[i].getFileNameWithoutExtension());
    if (files.isEmpty())
        m.addItem (99999, "(no saved pedals)", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadBtn),
        [this, files] (int result)
        {
            if (result <= 0 || result > files.size())
                return;
            const auto v = juce::JSON::parse (files[result - 1].loadFileAsString());
            if (! v.isVoid())
            {
                graph.fromVar (v);
                nameEdit.setText (files[result - 1].getFileNameWithoutExtension());
                selectedNodeId = 0;
                rebuildParamPanel();
                repaint();
            }
        });
}

} // namespace vp
