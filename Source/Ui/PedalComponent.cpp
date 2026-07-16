#include "PedalComponent.h"
#include "../Hosting/PluginHost.h"
#include "../Effects/AmpEffects.h"

namespace vp
{

PedalComponent::PedalComponent (Pedal& pedalToShow, MidiLearnManager& midiMgr)
    : pedal (pedalToShow), midi (midiMgr)
{
    const auto catColour = colours::categoryColour (pedal.category);
    getProperties().set ("catColour", (juce::int64) catColour.getARGB());

    nameLabel.setText (pedal.displayName, juce::dontSendNotification);
    nameLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f).withStyle ("Bold")));
    nameLabel.setColour (juce::Label::textColourId, colours::text);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    bypassBtn.setClickingTogglesState (true);
    bypassBtn.setToggleState (! pedal.isBypassed(), juce::dontSendNotification);
    bypassBtn.setTooltip ("Enable / bypass (silent, click-free)");
    bypassBtn.onClick = [this]
    {
        pedal.setBypassed (! bypassBtn.getToggleState());
        if (onEdited) onEdited();
        repaint();
    };
    addAndMakeVisible (bypassBtn);

    removeBtn.setTooltip ("Remove pedal");
    removeBtn.onClick = [this] { if (onRemove) onRemove(); };
    addAndMakeVisible (removeBtn);

    // special buttons for hosted plugins / cab IR loading
    if (auto* vst = dynamic_cast<VstPedal*> (&pedal))
    {
        extraBtn.setButtonText ("Editor");
        extraBtn.onClick = [vst] { vst->openEditor(); };
        addAndMakeVisible (extraBtn);
    }
    else if (auto* cab = dynamic_cast<CabPedal*> (&pedal))
    {
        extraBtn.setButtonText ("IR...");
        extraBtn.onClick = [this, cab]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Load impulse response",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [chooser, cab, this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().existsAsFile())
                    {
                        cab->setIrFile (fc.getResult());
                        if (auto* p = cab->param ("irblend"); p != nullptr && p->get() < 1.0)
                            p->set (100.0);
                        if (onEdited) onEdited();
                    }
                });
        };
        addAndMakeVisible (extraBtn);
    }

    //==========================================================================
    for (auto* p : pedal.getParams())
    {
        auto c = std::make_unique<Control>();
        c->param = p;

        c->label = std::make_unique<juce::Label>();
        c->label->setText (p->def.name, juce::dontSendNotification);
        c->label->setJustificationType (juce::Justification::centred);
        c->label->setColour (juce::Label::textColourId, colours::textDim);
        c->label->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*c->label);

        if (p->def.isToggle)
        {
            c->toggle = std::make_unique<juce::TextButton> (p->def.name);
            c->toggle->setClickingTogglesState (true);
            c->toggle->setToggleState (p->getBool(), juce::dontSendNotification);
            c->toggle->onClick = [this, p, btn = c->toggle.get()]
            {
                p->set (btn->getToggleState() ? 1.0 : 0.0);
                if (onEdited) onEdited();
            };
            c->label->setVisible (false);
            addAndMakeVisible (*c->toggle);
        }
        else if (! p->def.choices.isEmpty())
        {
            c->combo = std::make_unique<juce::ComboBox>();
            for (int i = 0; i < p->def.choices.size(); ++i)
                c->combo->addItem (p->def.choices[i], i + 1);
            c->combo->setSelectedItemIndex (p->getChoice(), juce::dontSendNotification);
            c->combo->onChange = [this, p, combo = c->combo.get()]
            {
                p->set ((double) combo->getSelectedItemIndex());
                if (onEdited) onEdited();
            };
            addAndMakeVisible (*c->combo);
        }
        else
        {
            c->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                        juce::Slider::NoTextBox);
            c->slider->setRange (0.0, 1.0, 0.0001);
            c->slider->setValue (p->toNormalised (p->get()), juce::dontSendNotification);
            c->slider->setDoubleClickReturnValue (true, p->toNormalised (p->def.def));
            c->slider->setTooltip (p->def.name + ": " + p->getText());
            c->slider->onValueChange = [this, p, s = c->slider.get()]
            {
                p->set (p->fromNormalised (s->getValue()));
                s->setTooltip (p->def.name + ": " + p->getText());
            };
            c->slider->onDragEnd = [this] { if (onEdited) onEdited(); };
            c->slider->setPopupDisplayEnabled (true, true, this);
            // right-click => MIDI learn menu
            c->slider->setMouseClickGrabsKeyboardFocus (false);
            c->slider->addMouseListener (this, false);
            addAndMakeVisible (*c->slider);
        }
        controls.push_back (std::move (c));
    }

    const int perRow = juce::jmax (2, juce::jmin (4, ((int) controls.size() + 1) / 2));
    idealWidth = juce::jmax (150, perRow * 62 + 16);

    startTimerHz (10); // reflect MIDI-driven changes
}

PedalComponent::~PedalComponent() = default;

void PedalComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const auto cat = colours::categoryColour (pedal.category);
    const bool off = pedal.isBypassed();

    // enclosure: vertical metal gradient tinted by category
    {
        juce::ColourGradient grad (cat.darker (off ? 0.92f : 0.78f).withMultipliedSaturation (0.55f),
                                   b.getX(), b.getY(),
                                   cat.darker (off ? 0.97f : 0.9f).withMultipliedSaturation (0.5f),
                                   b.getX(), b.getBottom(), false);
        grad.addColour (0.12, cat.darker (off ? 0.88f : 0.68f).withMultipliedSaturation (0.55f));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 9.0f);
    }

    // brushed-metal streaks (deterministic per pedal)
    {
        juce::Random seeded ((juce::int64) pedal.displayName.hashCode());
        g.setColour (juce::Colours::white.withAlpha (0.028f));
        for (int i = 0; i < 26; ++i)
        {
            const float y = b.getY() + seeded.nextFloat() * b.getHeight();
            g.drawHorizontalLine ((int) y, b.getX() + 3.0f, b.getRight() - 3.0f);
        }
    }

    // category watermark art behind the controls
    art::draw (g, b.reduced (b.getWidth() * 0.16f, b.getHeight() * 0.2f).translated (0.0f, 8.0f),
               pedal.category, off ? cat.withMultipliedSaturation (0.2f) : cat.brighter (0.4f));

    // bevel + outline
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (b.reduced (1.2f), 8.0f, 1.0f);
    g.setColour (off ? colours::outline : cat.withAlpha (0.85f));
    g.drawRoundedRectangle (b, 9.0f, 1.4f);

    // corner screws
    {
        const float sr = 3.4f;
        const juce::Point<float> corners[4] = {
            { b.getX() + 8.0f, b.getY() + 34.0f }, { b.getRight() - 8.0f, b.getY() + 34.0f },
            { b.getX() + 8.0f, b.getBottom() - 8.0f }, { b.getRight() - 8.0f, b.getBottom() - 8.0f } };
        int slot = 0;
        for (const auto& cpt : corners)
        {
            juce::ColourGradient sg (juce::Colour (0xff9aa2b0), cpt.x - sr * 0.5f, cpt.y - sr * 0.5f,
                                     juce::Colour (0xff30343e), cpt.x + sr, cpt.y + sr, true);
            g.setGradientFill (sg);
            g.fillEllipse (cpt.x - sr, cpt.y - sr, sr * 2.0f, sr * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            const float ang = 0.5f + slot++ * 0.9f;
            g.drawLine (cpt.x - std::cos (ang) * sr * 0.8f, cpt.y - std::sin (ang) * sr * 0.8f,
                        cpt.x + std::cos (ang) * sr * 0.8f, cpt.y + std::sin (ang) * sr * 0.8f, 1.1f);
        }
    }

    // header nameplate
    auto header = b.removeFromTop (26.0f);
    {
        auto plate = header.reduced (2.0f);
        juce::ColourGradient pg (cat.withAlpha (off ? 0.22f : 0.6f), plate.getX(), plate.getY(),
                                 cat.darker (0.4f).withAlpha (off ? 0.18f : 0.5f), plate.getX(), plate.getBottom(), false);
        g.setGradientFill (pg);
        g.fillRoundedRectangle (plate, 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) plate.getY() + 1, plate.getX() + 4.0f, plate.getRight() - 4.0f);
    }

    // jewel bypass LED with glow
    {
        const juce::Rectangle<float> led (7.0f, 8.0f, 11.0f, 11.0f);
        if (! off)
        {
            g.setColour (colours::good.withAlpha (0.35f));
            g.fillEllipse (led.expanded (4.0f));
        }
        juce::ColourGradient lg (off ? juce::Colour (0xff3c4450) : colours::good.brighter (0.6f),
                                 led.getCentreX() - 2.0f, led.getCentreY() - 2.0f,
                                 off ? juce::Colour (0xff222830) : colours::good.darker (0.4f),
                                 led.getRight(), led.getBottom(), true);
        g.setGradientFill (lg);
        g.fillEllipse (led);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (led, 1.0f);
    }
}

void PedalComponent::resized()
{
    auto b = getLocalBounds().reduced (4);
    auto header = b.removeFromTop (24);
    header.removeFromLeft (18); // LED space
    removeBtn.setBounds (header.removeFromRight (20).reduced (1));
    bypassBtn.setBounds (juce::Rectangle<int> (4, 4, 18, 18)); // over the LED
    bypassBtn.setAlpha (0.01f);                                 // invisible hit target
    nameLabel.setBounds (header);

    if (extraBtn.isVisible() && extraBtn.getButtonText().isNotEmpty())
        extraBtn.setBounds (b.removeFromBottom (22).reduced (18, 1));

    const int count = (int) controls.size();
    if (count == 0)
        return;
    const int perRow = juce::jmax (1, (getWidth() - 12) / 60);
    const int rows = (count + perRow - 1) / perRow;
    const int cellW = (getWidth() - 12) / juce::jmax (1, juce::jmin (perRow, count));
    const int cellH = juce::jmax (48, b.getHeight() / juce::jmax (1, rows));

    int idx = 0;
    for (auto& c : controls)
    {
        const int row = idx / perRow;
        const int col = idx % perRow;
        juce::Rectangle<int> cell (6 + col * cellW, b.getY() + row * cellH, cellW, cellH);
        auto labelArea = cell.removeFromBottom (14);

        if (c->slider)  c->slider->setBounds (cell.reduced (2));
        if (c->combo)   c->combo->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() - 4, 92), 22));
        if (c->toggle)  c->toggle->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() - 4, 84), 24));
        if (c->label)   c->label->setBounds (labelArea);
        ++idx;
    }
}

void PedalComponent::mouseDrag (const juce::MouseEvent& e)
{
    // dragging the header initiates chain reordering
    if (e.getMouseDownY() < 28 && e.getDistanceFromDragStart() > 8 && onDragStart)
        onDragStart (*this, e);
}

void PedalComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        for (auto& c : controls)
            if (c->slider != nullptr && e.eventComponent == c->slider.get())
            {
                showKnobMenu (*c->param);
                return;
            }
}

void PedalComponent::timerCallback()
{
    // keep UI in sync with MIDI-driven parameter changes
    for (auto& c : controls)
    {
        if (c->slider != nullptr && ! c->slider->isMouseButtonDown())
        {
            const double norm = c->param->toNormalised (c->param->get());
            if (std::abs (norm - c->slider->getValue()) > 1.0e-4)
                c->slider->setValue (norm, juce::dontSendNotification);
        }
        else if (c->combo != nullptr && c->combo->getSelectedItemIndex() != c->param->getChoice())
            c->combo->setSelectedItemIndex (c->param->getChoice(), juce::dontSendNotification);
        else if (c->toggle != nullptr && c->toggle->getToggleState() != c->param->getBool())
            c->toggle->setToggleState (c->param->getBool(), juce::dontSendNotification);
    }
    const bool on = ! pedal.isBypassed();
    if (bypassBtn.getToggleState() != on)
    {
        bypassBtn.setToggleState (on, juce::dontSendNotification);
        repaint();
    }
}

void PedalComponent::showKnobMenu (Param& p)
{
    juce::PopupMenu m;
    m.addItem ("MIDI Learn (" + p.def.name + ")", [this, &p] { midi.armLearn (&pedal, p.def.id); });
    if (midi.hasMappingFor (&pedal, p.def.id))
        m.addItem ("Clear MIDI mapping", [this, &p] { midi.clearMappingFor (&pedal, p.def.id); });
    m.addSeparator();
    m.addItem ("Reset to default", [this, &p] { p.set (p.def.def); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

} // namespace vp
