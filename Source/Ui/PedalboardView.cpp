#include "PedalboardView.h"
#include "../Engine/PedalFactory.h"
#include "../Engine/NodeGraph.h"

namespace vp
{

//==============================================================================
// Frame drawn around a parallel group with stacked lanes inside.
struct PedalboardView::Row : public juce::Component
{
    ChainItem* item = nullptr;
    juce::OwnedArray<PedalComponent> lanePedals;
    juce::OwnedArray<juce::TextButton> laneAddBtns;
    juce::TextButton removeBtn { "x" };
    std::vector<int> laneOfPedal;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (colours::panelHi.withAlpha (0.4f));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (colours::accent2.withAlpha (0.7f));
        g.drawRoundedRectangle (b, 10.0f, 1.2f);
        g.setColour (colours::textDim);
        g.setFont (11.0f);
        g.drawText ("PARALLEL", 8, 2, 80, 14, juce::Justification::left);
    }
};

//==============================================================================
PedalboardView::PedalboardView (AudioEngine& e, PresetManager& p, MidiLearnManager& m, PluginHost& host)
    : engine (e), presets (p), midi (m), pluginHost (host)
{
    addBtn = std::make_unique<juce::TextButton> ("+ Add");
    addBtn->onClick = [this] { showAddMenu (-1); };
    addAndMakeVisible (*addBtn);
    refresh();
}

void PedalboardView::performEdit (const juce::String&, std::function<void (PedalChain&)> edit)
{
    presets.pushUndoState ("edit");
    engine.editChain (edit);
    midi.resolve();
    refresh();
    if (onChainChanged) onChainChanged();
}

//==============================================================================
void PedalboardView::refresh()
{
    itemComps.clear();

    engine.editChain ([this] (PedalChain& chain)
    {
        for (int i = 0; i < chain.getNumItems(); ++i)
        {
            auto* item = chain.getItem (i);
            if (item == nullptr)
                continue;

            if (item->pedal != nullptr)
            {
                auto* pc = new PedalComponent (*item->pedal, midi);
                const int index = i;
                pc->onRemove = [this, index]
                {
                    performEdit ("remove", [index] (PedalChain& c) { c.removeItem (index); });
                };
                pc->onEdited = [this] { presets.saveSession(); };
                pc->onDragStart = [this, index] (PedalComponent& comp, const juce::MouseEvent&)
                {
                    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (&comp))
                        dnd->startDragging ("pedal:" + juce::String (index), &comp);
                };
                itemComps.add (pc);
                addAndMakeVisible (pc);
            }
            else
            {
                auto* row = new Row();
                row->item = item;
                const int index = i;
                row->removeBtn.setTooltip ("Remove parallel split");
                row->removeBtn.onClick = [this, index]
                {
                    performEdit ("remove split", [index] (PedalChain& c) { c.removeItem (index); });
                };
                row->addAndMakeVisible (row->removeBtn);

                for (size_t li = 0; li < item->lanes.size(); ++li)
                {
                    auto* lane = item->lanes[li].get();
                    for (int pi = 0; pi < lane->getNumItems(); ++pi)
                    {
                        if (auto* laneItem = lane->getItem (pi); laneItem != nullptr && laneItem->pedal != nullptr)
                        {
                            auto* pc = new PedalComponent (*laneItem->pedal, midi);
                            const int pedalIdx = pi;
                            pc->onRemove = [this, lane, pedalIdx]
                            {
                                performEdit ("remove", [lane, pedalIdx] (PedalChain&) { lane->removeItem (pedalIdx); });
                            };
                            pc->onEdited = [this] { presets.saveSession(); };
                            row->lanePedals.add (pc);
                            row->laneOfPedal.push_back ((int) li);
                            row->addAndMakeVisible (pc);
                        }
                    }
                    auto* laneAdd = new juce::TextButton ("+");
                    laneAdd->onClick = [this, lane] { showAddMenu (-1, lane); };
                    row->laneAddBtns.add (laneAdd);
                    row->addAndMakeVisible (laneAdd);
                }
                itemComps.add (row);
                addAndMakeVisible (row);
            }
        }
    });

    rebuildLayout();
}

void PedalboardView::rebuildLayout()
{
    // total width: pedals side by side; parallel rows sized by widest lane
    int x = 12;
    const int topY = 14;
    const int pedalH = 190;

    for (auto* comp : itemComps)
    {
        if (auto* pc = dynamic_cast<PedalComponent*> (comp))
        {
            pc->setBounds (x, topY, pc->getIdealWidth(), pedalH);
            x += pc->getIdealWidth() + 10;
        }
        else if (auto* row = dynamic_cast<Row*> (comp))
        {
            const int lanes = (int) row->item->lanes.size();
            int maxLaneW = 90;
            {
                std::vector<int> laneW ((size_t) lanes, 8);
                for (int i = 0; i < row->lanePedals.size(); ++i)
                    laneW[(size_t) row->laneOfPedal[(size_t) i]] += row->lanePedals[i]->getIdealWidth() + 8;
                for (int w : laneW) maxLaneW = juce::jmax (maxLaneW, w + 40);
            }
            const int laneH = 170;
            const int rowH = juce::jmax (1, lanes) * (laneH + 6) + 24;
            row->setBounds (x, 6, maxLaneW, juce::jmax (rowH, pedalH + 8));

            // lay out inside the row
            row->removeBtn.setBounds (maxLaneW - 24, 4, 20, 20);
            std::vector<int> laneX ((size_t) lanes, 8);
            for (int i = 0; i < row->lanePedals.size(); ++i)
            {
                const int li = row->laneOfPedal[(size_t) i];
                auto* pc = row->lanePedals[i];
                pc->setBounds (laneX[(size_t) li], 22 + li * (laneH + 6), pc->getIdealWidth(), laneH - 10);
                laneX[(size_t) li] += pc->getIdealWidth() + 8;
            }
            for (int li = 0; li < row->laneAddBtns.size(); ++li)
                row->laneAddBtns[li]->setBounds (laneX[(size_t) li], 22 + li * (laneH + 6) + laneH / 2 - 22, 28, 28);

            x += maxLaneW + 10;
        }
    }

    addBtn->setBounds (x, topY + pedalH / 2 - 18, 76, 36);
    setSize (juce::jmax (x + 96, getParentWidth()), juce::jmax (getParentHeight(), 240));
}

void PedalboardView::resized() {}

void PedalboardView::paint (juce::Graphics& g)
{
    // tolex-style board: dark gradient + woven texture
    {
        juce::ColourGradient bgGrad (juce::Colour (0xff181b22), 0.0f, 0.0f,
                                     juce::Colour (0xff101318), 0.0f, (float) getHeight(), false);
        g.setGradientFill (bgGrad);
        g.fillAll();

        juce::Random seeded (77);
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        for (int i = 0; i < getWidth() / 6; ++i)
        {
            const float x = seeded.nextFloat() * (float) getWidth();
            const float y2 = seeded.nextFloat() * (float) getHeight();
            g.fillEllipse (x, y2, 2.2f, 2.2f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.018f));
        for (int x = -getHeight(); x < getWidth(); x += 14)
            g.drawLine ((float) x, 0.0f, (float) (x + getHeight()), (float) getHeight(), 1.0f);
    }

    // patch cables between pedals (sagging bezier + jack plugs)
    {
        const float jackY = 14.0f + 95.0f;
        float prevX = 2.0f;
        float prevY = jackY;

        auto drawCable = [&g] (float x1, float y1, float x2, float y2)
        {
            juce::Path cable;
            const float sag = 26.0f + std::abs (x2 - x1) * 0.10f;
            cable.startNewSubPath (x1, y1);
            cable.cubicTo (x1 + (x2 - x1) * 0.3f, y1 + sag, x1 + (x2 - x1) * 0.7f, y2 + sag, x2, y2);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.strokePath (cable, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colour (0xff30353f));
            g.strokePath (cable, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (cable, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // plug sleeves
            g.setColour (juce::Colour (0xff8a93a4));
            g.fillRoundedRectangle (x1 - 2.0f, y1 - 3.5f, 7.0f, 7.0f, 2.0f);
            g.fillRoundedRectangle (x2 - 5.0f, y2 - 3.5f, 7.0f, 7.0f, 2.0f);
        };

        for (auto* comp : itemComps)
        {
            const float midY = (float) comp->getY() + 95.0f;
            drawCable (prevX, prevY, (float) comp->getX() + 2.0f, midY);
            prevX = (float) comp->getRight() - 2.0f;
            prevY = midY;
        }
        drawCable (prevX, prevY, (float) juce::jmax (getWidth() - 4, (int) prevX + 60), jackY);

        g.setColour (colours::textDim.withAlpha (0.7f));
        g.setFont (10.0f);
        g.drawText ("GUITAR IN", 4, (int) jackY - 26, 70, 12, juce::Justification::left);
    }

    if (dropIndicatorIndex >= 0)
    {
        int x = 8;
        int count = 0;
        for (auto* comp : itemComps)
        {
            if (count == dropIndicatorIndex)
                break;
            x = comp->getRight() + 4;
            ++count;
        }
        if (dropIndicatorIndex == 0 && itemComps.size() > 0)
            x = itemComps[0]->getX() - 6;
        g.setColour (colours::accent);
        g.fillRoundedRectangle ((float) x, 12.0f, 4.0f, 196.0f, 2.0f);
    }

    if (itemComps.isEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (16.0f);
        g.drawText ("Empty board - click \"+ Add\" to build your rig, or load a preset",
                    getLocalBounds(), juce::Justification::centred);
    }
}

//==============================================================================
bool PedalboardView::isInterestedInDragSource (const SourceDetails& s)
{
    return s.description.toString().startsWith ("pedal:");
}

void PedalboardView::itemDragMove (const SourceDetails& s)
{
    int idx = 0;
    for (auto* comp : itemComps)
    {
        if (s.localPosition.x < comp->getX() + comp->getWidth() / 2)
            break;
        ++idx;
    }
    if (idx != dropIndicatorIndex)
    {
        dropIndicatorIndex = idx;
        repaint();
    }
}

void PedalboardView::itemDragExit (const SourceDetails&)
{
    dropIndicatorIndex = -1;
    repaint();
}

void PedalboardView::itemDropped (const SourceDetails& s)
{
    const int from = s.description.toString().fromFirstOccurrenceOf ("pedal:", false, false).getIntValue();
    int to = dropIndicatorIndex;
    dropIndicatorIndex = -1;
    if (to < 0)
        return;
    if (to > from)
        --to; // account for removal shifting indices
    performEdit ("reorder", [from, to] (PedalChain& c) { c.moveItem (from, to); });
}

//==============================================================================
void PedalboardView::showAddMenu (int insertIndex, PedalChain* targetChain)
{
    juce::PopupMenu menu;
    auto& factory = PedalFactory::instance();

    int id = 1;
    std::map<int, juce::String> idToType;
    for (const auto& cat : factory.getCategories())
    {
        juce::PopupMenu sub;
        for (const auto& e : factory.getEntries())
        {
            if (e.category == cat)
            {
                sub.addItem (id, e.name);
                idToType[id] = e.typeId;
                ++id;
            }
        }
        menu.addSubMenu (cat, sub);
    }

    // custom pedals built in the node editor
    juce::PopupMenu customSub;
    const auto customs = NodeGraphLibrary::listCustomPedals();
    std::map<int, juce::File> idToCustom;
    for (const auto& f : customs)
    {
        customSub.addItem (id, f.getFileNameWithoutExtension());
        idToCustom[id] = f;
        ++id;
    }
    if (customs.isEmpty())
        customSub.addItem (999999, "(create pedals in the Builder panel)", false);
    menu.addSubMenu ("Custom", customSub);

    menu.addSeparator();
    const int parallelId = id++;
    menu.addItem (parallelId, "Parallel Split (A/B lanes)");

    // hosted plugins
    juce::PopupMenu vstSub;
    std::map<int, juce::PluginDescription> idToPlugin;
    for (const auto& d : pluginHost.getPlugins())
    {
        vstSub.addItem (id, d.name + "  (" + d.manufacturerName + ")");
        idToPlugin[id] = d;
        ++id;
    }
    if (idToPlugin.empty())
        vstSub.addItem (999998, "(scan for plugins in the Plugins panel)", false);
    menu.addSubMenu ("VST3 Plugins", vstSub);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addBtn.get()),
        [this, insertIndex, targetChain, idToType, idToCustom, idToPlugin, parallelId] (int result)
        {
            if (result == 0)
                return;

            auto addToTarget = [&] (std::unique_ptr<Pedal> pedal)
            {
                if (pedal == nullptr)
                    return;
                Pedal* raw = pedal.release();
                performEdit ("add", [raw, insertIndex, targetChain] (PedalChain& c)
                {
                    (targetChain != nullptr ? *targetChain : c).addPedal (std::unique_ptr<Pedal> (raw), insertIndex);
                });
            };

            if (result == parallelId)
            {
                performEdit ("add split", [insertIndex, targetChain] (PedalChain& c)
                {
                    (targetChain != nullptr ? *targetChain : c).addParallel (insertIndex, 2);
                });
            }
            else if (auto it = idToType.find (result); it != idToType.end())
            {
                addToTarget (PedalFactory::instance().create (it->second));
            }
            else if (auto ic = idToCustom.find (result); ic != idToCustom.end())
            {
                addToTarget (NodeGraphLibrary::createCustomPedal (ic->second));
            }
            else if (auto ip = idToPlugin.find (result); ip != idToPlugin.end())
            {
                auto pedal = pluginHost.createPlugin (ip->second, engine.getSampleRate(), 512);
                if (pedal == nullptr)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                        "Plugin", "Could not load this plugin. It has been noted; if it crashed the scanner "
                                  "previously it is blacklisted automatically.");
                    return;
                }
                addToTarget (std::move (pedal));
            }
        });
}

} // namespace vp
