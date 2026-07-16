#pragma once
#include <JuceHeader.h>

namespace vp
{

namespace colours
{
    const juce::Colour bg        { 0xff14161c };
    const juce::Colour panel     { 0xff1c1f28 };
    const juce::Colour panelHi   { 0xff262a36 };
    const juce::Colour outline   { 0xff333845 };
    const juce::Colour accent    { 0xff4fc3f7 };
    const juce::Colour accent2   { 0xffff9838 };
    const juce::Colour good      { 0xff66d97e };
    const juce::Colour warn      { 0xffffcb47 };
    const juce::Colour bad       { 0xffff5a5a };
    const juce::Colour text      { 0xffe8eaf0 };
    const juce::Colour textDim   { 0xff9aa0ae };

    inline juce::Colour categoryColour (const juce::String& cat)
    {
        if (cat == "Drive")        return juce::Colour (0xffe0703a);
        if (cat == "Dynamics")     return juce::Colour (0xff4fa3e0);
        if (cat == "EQ")           return juce::Colour (0xff58c7b0);
        if (cat == "Delay")        return juce::Colour (0xff9d7bd8);
        if (cat == "Reverb")       return juce::Colour (0xff5f8fd9);
        if (cat == "Ambient")      return juce::Colour (0xff7fb2e8);
        if (cat == "Modulation")   return juce::Colour (0xffd45fb0);
        if (cat == "Pitch")        return juce::Colour (0xffd9c04f);
        if (cat == "Filter")       return juce::Colour (0xff8fce5a);
        if (cat == "Experimental") return juce::Colour (0xffe05a7c);
        if (cat == "Sustain")      return juce::Colour (0xff5ad9c0);
        if (cat == "Amp")          return juce::Colour (0xffc98a4b);
        if (cat == "Plugin")       return juce::Colour (0xff8a93a8);
        if (cat == "Custom")       return juce::Colour (0xffb08ae0);
        if (cat == "Pickup")       return juce::Colour (0xffd0d4dc);
        return juce::Colour (0xff7a8296);
    }
}

//==============================================================================
// Procedural "pedal art": each category gets its own decorative graphic drawn
// straight onto the pedal face (no image assets, always crisp).
namespace art
{
    inline void draw (juce::Graphics& g, juce::Rectangle<float> area,
                      const juce::String& cat, juce::Colour c)
    {
        g.setColour (c.withAlpha (0.16f));
        const auto cx = area.getCentreX();
        const auto cy = area.getCentreY();
        const float s = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        juce::Path p;

        if (cat == "Drive")
        {
            // flame
            p.startNewSubPath (cx, cy - s);
            p.cubicTo (cx + s * 0.7f, cy - s * 0.3f, cx + s * 0.35f, cy + s * 0.15f, cx + s * 0.55f, cy + s * 0.75f);
            p.cubicTo (cx + s * 0.2f, cy + s * 0.55f, cx - s * 0.2f, cy + s * 0.9f, cx - s * 0.45f, cy + s * 0.6f);
            p.cubicTo (cx - s * 0.75f, cy + s * 0.2f, cx - s * 0.3f, cy - s * 0.25f, cx, cy - s);
            p.closeSubPath();
            g.fillPath (p);
            g.setColour (c.withAlpha (0.10f));
            p.clear();
            p.startNewSubPath (cx, cy - s * 0.4f);
            p.cubicTo (cx + s * 0.3f, cy, cx + s * 0.1f, cy + s * 0.3f, cx, cy + s * 0.6f);
            p.cubicTo (cx - s * 0.15f, cy + s * 0.3f, cx - s * 0.25f, cy, cx, cy - s * 0.4f);
            g.fillPath (p);
        }
        else if (cat == "Modulation")
        {
            for (int w = 0; w < 3; ++w)
            {
                p.clear();
                const float yy = cy + (w - 1) * s * 0.42f;
                p.startNewSubPath (cx - s, yy);
                for (float x = -s; x <= s; x += 2.0f)
                    p.lineTo (cx + x, yy + std::sin ((x / s + w * 0.7f) * juce::MathConstants<float>::pi * 2.0f) * s * 0.16f);
                g.strokePath (p, juce::PathStrokeType (2.2f));
            }
        }
        else if (cat == "Delay")
        {
            for (int ring = 0; ring < 4; ++ring)
            {
                const float rr = s * (0.25f + ring * 0.25f);
                g.setColour (c.withAlpha (0.20f - ring * 0.04f));
                g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 2.0f);
            }
        }
        else if (cat == "Reverb" || cat == "Ambient")
        {
            // starfield + mountain
            juce::Random seeded (1234);
            for (int i = 0; i < 14; ++i)
            {
                const float sx = area.getX() + seeded.nextFloat() * area.getWidth();
                const float sy = area.getY() + seeded.nextFloat() * area.getHeight() * 0.7f;
                const float sr = 1.0f + seeded.nextFloat() * 1.8f;
                g.fillEllipse (sx, sy, sr, sr);
            }
            p.startNewSubPath (cx - s, cy + s * 0.9f);
            p.lineTo (cx - s * 0.3f, cy + s * 0.1f);
            p.lineTo (cx + s * 0.1f, cy + s * 0.55f);
            p.lineTo (cx + s * 0.5f, cy - s * 0.05f);
            p.lineTo (cx + s, cy + s * 0.9f);
            p.closeSubPath();
            g.fillPath (p);
        }
        else if (cat == "Pitch")
        {
            // musical note
            g.fillEllipse (cx - s * 0.55f, cy + s * 0.25f, s * 0.55f, s * 0.42f);
            g.fillRect (cx - s * 0.05f, cy - s * 0.7f, s * 0.12f, s * 1.15f);
            p.startNewSubPath (cx + 0.06f * s, cy - s * 0.7f);
            p.cubicTo (cx + s * 0.45f, cy - s * 0.6f, cx + s * 0.5f, cy - s * 0.25f, cx + s * 0.3f, cy - s * 0.1f);
            p.cubicTo (cx + s * 0.42f, cy - s * 0.35f, cx + s * 0.3f, cy - s * 0.5f, cx + 0.06f * s, cy - s * 0.55f);
            p.closeSubPath();
            g.fillPath (p);
        }
        else if (cat == "Experimental")
        {
            // lightning bolt
            p.startNewSubPath (cx + s * 0.25f, cy - s);
            p.lineTo (cx - s * 0.4f, cy + s * 0.15f);
            p.lineTo (cx - s * 0.02f, cy + s * 0.15f);
            p.lineTo (cx - s * 0.3f, cy + s);
            p.lineTo (cx + s * 0.45f, cy - s * 0.1f);
            p.lineTo (cx + s * 0.05f, cy - s * 0.1f);
            p.closeSubPath();
            g.fillPath (p);
        }
        else if (cat == "Sustain")
        {
            // infinity
            p.addCentredArc (cx - s * 0.42f, cy, s * 0.4f, s * 0.28f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi + 0.2f, true);
            g.strokePath (p, juce::PathStrokeType (s * 0.14f, juce::PathStrokeType::curved));
            p.clear();
            p.addCentredArc (cx + s * 0.42f, cy, s * 0.4f, s * 0.28f, 0.0f, -0.6f + juce::MathConstants<float>::pi,
                             juce::MathConstants<float>::pi * 3.0f - 0.4f, true);
            g.strokePath (p, juce::PathStrokeType (s * 0.14f, juce::PathStrokeType::curved));
        }
        else if (cat == "Dynamics")
        {
            // VU needle gauge
            g.drawEllipse (cx - s * 0.8f, cy - s * 0.45f, s * 1.6f, s * 1.6f, 2.0f);
            for (int t = 0; t < 5; ++t)
            {
                const float ang = -2.3f + t * 0.28f;
                const float x1 = cx + std::sin (ang) * s * 0.62f;
                const float y1 = cy + s * 0.35f - std::cos (ang) * s * 0.62f;
                const float x2 = cx + std::sin (ang) * s * 0.75f;
                const float y2 = cy + s * 0.35f - std::cos (ang) * s * 0.75f;
                g.drawLine (x1, y1, x2, y2, 2.0f);
            }
            g.drawLine (cx, cy + s * 0.35f, cx + std::sin (-1.7f) * s * 0.7f, cy + s * 0.35f - std::cos (-1.7f) * s * 0.7f, 2.5f);
        }
        else if (cat == "EQ")
        {
            for (int b = 0; b < 5; ++b)
            {
                const float h = s * (0.35f + 0.5f * std::sin (b * 1.7f + 0.8f) * 0.5f + 0.35f);
                g.fillRoundedRectangle (cx - s * 0.8f + b * s * 0.38f, cy + s * 0.6f - h, s * 0.22f, h, 2.0f);
            }
        }
        else if (cat == "Filter")
        {
            p.startNewSubPath (cx - s, cy + s * 0.5f);
            p.cubicTo (cx - s * 0.2f, cy + s * 0.45f, cx - s * 0.1f, cy - s * 0.7f, cx + s * 0.15f, cy - s * 0.7f);
            p.cubicTo (cx + s * 0.4f, cy - s * 0.7f, cx + s * 0.3f, cy + s * 0.5f, cx + s, cy + s * 0.6f);
            g.strokePath (p, juce::PathStrokeType (2.6f));
        }
        else if (cat == "Amp")
        {
            // glowing tubes
            for (int t = 0; t < 2; ++t)
            {
                const float tx = cx - s * 0.45f + t * s * 0.65f;
                g.setColour (c.withAlpha (0.18f));
                g.fillRoundedRectangle (tx, cy - s * 0.6f, s * 0.34f, s * 1.2f, s * 0.17f);
                g.setColour (juce::Colour (0xffff8830).withAlpha (0.25f));
                g.fillEllipse (tx + s * 0.06f, cy + s * 0.15f, s * 0.22f, s * 0.3f);
            }
        }
        else if (cat == "Pickup")
        {
            // humbucker: two coil rows of pole pieces
            g.setColour (c.withAlpha (0.20f));
            g.fillRoundedRectangle (cx - s * 0.85f, cy - s * 0.45f, s * 1.7f, s * 0.9f, s * 0.12f);
            g.setColour (colours::bg.withAlpha (0.5f));
            for (int row = 0; row < 2; ++row)
                for (int pole = 0; pole < 6; ++pole)
                    g.fillEllipse (cx - s * 0.72f + pole * s * 0.27f, cy - s * 0.26f + row * s * 0.36f, s * 0.14f, s * 0.14f);
        }
        else if (cat == "Plugin")
        {
            g.setFont (juce::Font (juce::FontOptions().withHeight (s * 0.8f).withStyle ("Bold")));
            g.drawText ("VST3", area, juce::Justification::centred);
        }
        else if (cat == "Custom")
        {
            const juce::Point<float> nodes[4] = { { cx - s * 0.6f, cy - s * 0.4f }, { cx + s * 0.5f, cy - s * 0.55f },
                                                  { cx - s * 0.3f, cy + s * 0.5f }, { cx + s * 0.6f, cy + s * 0.35f } };
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b)
                    g.drawLine ({ nodes[a], nodes[b] }, 1.6f);
            for (const auto& nd : nodes)
                g.fillEllipse (nd.x - s * 0.12f, nd.y - s * 0.12f, s * 0.24f, s * 0.24f);
        }
        else // Utility & fallback: gear
        {
            for (int tooth = 0; tooth < 8; ++tooth)
            {
                const float ang = tooth * juce::MathConstants<float>::twoPi / 8.0f;
                g.fillRoundedRectangle (cx + std::cos (ang) * s * 0.55f - s * 0.09f,
                                        cy + std::sin (ang) * s * 0.55f - s * 0.09f, s * 0.18f, s * 0.18f, 2.0f);
            }
            g.drawEllipse (cx - s * 0.45f, cy - s * 0.45f, s * 0.9f, s * 0.9f, s * 0.16f);
        }
    }
}

//==============================================================================
class VpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VpLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::bg);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::TextButton::buttonColourId, colours::panelHi);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.35f));
        setColour (juce::ComboBox::backgroundColourId, colours::panelHi);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::outlineColourId, colours::outline);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.3f));
        setColour (juce::TextEditor::backgroundColourId, colours::panelHi);
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::TextEditor::outlineColourId, colours::outline);
        setColour (juce::ListBox::backgroundColourId, colours::panel);
        setColour (juce::ScrollBar::thumbColourId, colours::outline);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ToggleButton::textColourId, colours::text);
        setColour (juce::ToggleButton::tickColourId, colours::accent);
        setColour (juce::AlertWindow::backgroundColourId, colours::panel);
        setColour (juce::AlertWindow::textColourId, colours::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                                .reduced (4.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float radius = size * 0.5f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float arcThickness = juce::jmax (2.0f, radius * 0.14f);

        // tick marks around the sweep
        g.setColour (colours::text.withAlpha (0.18f));
        for (int t = 0; t <= 10; ++t)
        {
            const float a = rotaryStartAngle + t / 10.0f * (rotaryEndAngle - rotaryStartAngle);
            const float x1 = centre.x + std::sin (a) * (radius + 1.0f);
            const float y1 = centre.y - std::cos (a) * (radius + 1.0f);
            const float x2 = centre.x + std::sin (a) * (radius + 3.5f);
            const float y2 = centre.y - std::cos (a) * (radius + 3.5f);
            g.drawLine (x1, y1, x2, y2, 1.0f);
        }

        // metallic body: radial highlight over dark cap
        {
            const auto capR = radius * 0.78f;
            juce::ColourGradient grad (juce::Colour (0xff3a4050), centre.x - capR * 0.5f, centre.y - capR * 0.6f,
                                       juce::Colour (0xff181b22), centre.x + capR * 0.7f, centre.y + capR * 0.9f, true);
            g.setGradientFill (grad);
            g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (centre.x - capR * 0.82f, centre.y - capR * 0.82f, capR * 1.64f, capR * 1.64f, 1.2f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.2f);
        }

        // track arc
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - arcThickness * 0.5f, radius - arcThickness * 0.5f,
                             0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (colours::outline.withAlpha (0.7f));
        g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - arcThickness * 0.5f, radius - arcThickness * 0.5f,
                             0.0f, rotaryStartAngle, angle, true);
        auto arcColour = slider.isEnabled() ? colours::accent : colours::textDim;
        if (auto* parent = slider.getParentComponent())
            if (parent->getProperties().contains ("catColour"))
                arcColour = juce::Colour ((juce::uint32) (juce::int64) parent->getProperties()["catColour"]);
        g.setColour (arcColour);
        g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // pointer
        juce::Path p;
        p.addRoundedRectangle (-1.5f, -radius * 0.72f, 3.0f, radius * 0.34f, 1.5f);
        g.setColour (colours::text);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return withHeight (13.0f); }
    juce::Font getPopupMenuFont() override                { return withHeight (14.0f); }
    juce::Font getLabelFont (juce::Label& l) override     { return withHeight (juce::jmin (13.0f, (float) l.getHeight() - 2.0f)); }

private:
    static juce::Font withHeight (float h)
    {
        return juce::Font (juce::FontOptions().withHeight (h));
    }
};

} // namespace vp
