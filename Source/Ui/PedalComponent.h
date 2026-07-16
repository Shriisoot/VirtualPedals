#pragma once
#include "../Engine/Pedal.h"
#include "../Midi/MidiLearnManager.h"
#include "LookAndFeel.h"

namespace vp
{

// Generic pedal face: header bar + auto-generated controls from the param list.
class PedalComponent : public juce::Component, private juce::Timer
{
public:
    PedalComponent (Pedal& pedalToShow, MidiLearnManager& midiMgr);
    ~PedalComponent() override;

    Pedal& getPedal() { return pedal; }

    // computed from number of controls
    int getIdealWidth() const { return idealWidth; }

    std::function<void()> onRemove;
    std::function<void()> onEdited;              // any param/bypass change (for autosave)
    std::function<void (PedalComponent&, const juce::MouseEvent&)> onDragStart;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void showKnobMenu (Param& p);

    Pedal& pedal;
    MidiLearnManager& midi;

    struct Control
    {
        Param* param = nullptr;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::TextButton> toggle;
        std::unique_ptr<juce::Label> label;
    };

    std::vector<std::unique_ptr<Control>> controls;
    juce::TextButton bypassBtn { "" }, removeBtn { "x" }, extraBtn;
    juce::Label nameLabel;
    int idealWidth = 150;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalComponent)
};

} // namespace vp
