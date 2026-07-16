#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace vp
{

// One-pole smoother advanced on the audio thread only; target set atomically from any thread.
class Smoother
{
public:
    void prepare (double sampleRate, double timeMs = 20.0)
    {
        setTime (sampleRate, timeMs);
        snap();
    }

    // change smoothing time without snapping to the target
    void setTime (double sampleRate, double timeMs)
    {
        coeff = std::exp (-1.0 / (0.001 * juce::jmax (0.01, timeMs) * sampleRate));
    }

    void setTarget (double t) noexcept       { target.store (t, std::memory_order_relaxed); }
    void snap() noexcept                     { y = target.load (std::memory_order_relaxed); }

    double next() noexcept
    {
        const double t = target.load (std::memory_order_relaxed);
        y = t + coeff * (y - t);
        return y;
    }

    // Advance as if n samples elapsed, return end value (block-rate smoothing).
    double skip (int n) noexcept
    {
        const double t = target.load (std::memory_order_relaxed);
        y = t + std::pow (coeff, (double) n) * (y - t);
        return y;
    }

    double current() const noexcept          { return y; }

private:
    std::atomic<double> target { 0.0 };
    double coeff = 0.0, y = 0.0;
};

struct ParamDef
{
    juce::String id, name, unit;
    double min = 0.0, max = 1.0, def = 0.5;
    double skew = 1.0;              // knob response curve (1 = linear)
    juce::StringArray choices;      // non-empty => discrete selector
    bool isToggle = false;

    bool isDiscrete() const { return isToggle || ! choices.isEmpty(); }
};

class Param
{
public:
    explicit Param (ParamDef d) : def (std::move (d))
    {
        raw.store (def.def);
        smoother.setTarget (def.def);
    }

    void prepare (double sampleRate, double smoothMs = 20.0)
    {
        smoother.prepare (sampleRate, def.isDiscrete() ? 0.0001 : smoothMs);
        smoother.snap();
    }

    void set (double v)
    {
        v = juce::jlimit (def.min, def.max, v);
        raw.store (v, std::memory_order_relaxed);
        smoother.setTarget (v);
        if (def.isDiscrete())
            smoother.snap();
    }

    double get() const noexcept              { return raw.load (std::memory_order_relaxed); }
    int    getChoice() const noexcept        { return (int) std::lround (get()); }
    bool   getBool() const noexcept          { return get() >= 0.5; }

    // Audio-thread accessors
    double nextSmoothed() noexcept           { return smoother.next(); }
    double blockSmoothed (int numSamples) noexcept { return smoother.skip (numSamples); }

    // 0..1 normalised mapping for UI / MIDI
    double toNormalised (double v) const
    {
        const double p = (v - def.min) / (def.max - def.min);
        return def.skew == 1.0 ? p : std::pow (juce::jlimit (0.0, 1.0, p), 1.0 / def.skew);
    }
    double fromNormalised (double n) const
    {
        n = juce::jlimit (0.0, 1.0, n);
        if (def.skew != 1.0) n = std::pow (n, def.skew);
        return def.min + n * (def.max - def.min);
    }

    juce::String getText() const
    {
        if (def.isToggle)            return getBool() ? "On" : "Off";
        if (! def.choices.isEmpty()) return def.choices[juce::jlimit (0, def.choices.size() - 1, getChoice())];
        const double v = get();
        juce::String s = std::abs (v) >= 100.0 ? juce::String (v, 0)
                       : std::abs (v) >= 10.0  ? juce::String (v, 1)
                                               : juce::String (v, 2);
        return def.unit.isNotEmpty() ? s + " " + def.unit : s;
    }

    const ParamDef def;

private:
    std::atomic<double> raw { 0.0 };
    Smoother smoother;
};

} // namespace vp
