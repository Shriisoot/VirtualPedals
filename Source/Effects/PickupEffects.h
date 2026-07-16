#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
// Pickup simulator: reshapes the guitar's own pickup into another type.
// Each model = HP tightness + body shelf + mid voice + the pickup's resonant
// peak + top-end character. "Active Metal" adds EMG-style compression + heat.
class PickupPedal : public Pedal
{
public:
    PickupPedal() : Pedal ("pickup", "Pickup Sim", "Pickup")
    {
        typeSel = addParam ({ "type", "Pickup", "", 0.0, 10.0, 9.0, 1.0,
            { "Single Coil Neck", "Single Coil Middle", "Single Coil Bridge",
              "Strat Position 2", "Strat Position 4", "Tele Bridge", "P90",
              "Humbucker Neck", "Humbucker Bridge", "Active Metal (EMG)", "Piezo Acoustic" } });
        level = addParam ({ "level", "Output", "dB", -12.0, 12.0, 0.0 });
        tone  = addParam ({ "tone", "Tone", "", 0.0, 10.0, 8.0 });
        tight = addParam ({ "tight", "Tightness", "Hz", 30.0, 160.0, 45.0 });
    }

    struct Model
    {
        double resFreq, resQ, resDb;      // pickup resonance
        double lowDb, midDb, brightDb;    // 110 Hz shelf, 650 Hz peak, 3.2 kHz shelf
        double levelDb;
        double compress;                  // 0..1 active-pickup squish
        double heat;                      // 0..1 output-stage warmth (soft clip)
    };

    static Model modelFor (int t)
    {
        switch (t)
        {
            case 0:  return { 4200.0, 1.9, 4.5, -1.0,  0.0,  1.0,  0.0, 0.0, 0.0 };  // SC neck
            case 1:  return { 3900.0, 2.0, 4.0, -1.5, -0.5,  1.5, -0.5, 0.0, 0.0 };  // SC mid
            case 2:  return { 3600.0, 2.2, 5.0, -2.0, -1.0,  2.0,  0.0, 0.0, 0.0 };  // SC bridge
            case 3:  return { 3800.0, 1.5, 3.0, -1.5, -3.5,  2.0, -1.0, 0.0, 0.0 };  // Strat pos 2 (quack)
            case 4:  return { 3400.0, 1.5, 3.0, -1.0, -3.0,  1.5, -1.0, 0.0, 0.0 };  // Strat pos 4
            case 5:  return { 3300.0, 2.5, 5.5, -2.0,  0.5,  2.5,  0.5, 0.0, 0.0 };  // Tele bridge
            case 6:  return { 2900.0, 1.6, 4.5,  1.0,  1.5,  0.0,  1.0, 0.1, 0.1 };  // P90
            case 7:  return { 2100.0, 1.2, 3.5,  2.0,  2.0, -2.0,  1.5, 0.0, 0.1 };  // HB neck
            case 8:  return { 2500.0, 1.4, 4.0,  1.5,  1.5, -1.0,  2.5, 0.0, 0.15 }; // HB bridge
            case 9:  return { 2300.0, 1.1, 3.0,  2.5, -1.0, -0.5,  5.5, 0.5, 0.3 };  // Active metal
            default: return { 6500.0, 0.8, 3.0, -3.0, -2.0,  4.0,  0.0, 0.0, 0.0 };  // Piezo
        }
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        srate = sampleRate;
        for (auto* f : { &hp, &low, &mid, &res, &bright, &lp })
            f->prepare (sampleRate, maxBlockSize);
        env.prepare (sampleRate, 2.0, 160.0);
        lastKey = -1.0e9;
    }

    void reset() override
    {
        for (auto* f : { &hp, &low, &mid, &res, &bright, &lp })
            f->reset();
        env.reset();
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const int t = typeSel->getChoice();
        const double tn = tone->blockSmoothed (n);
        const double tg = tight->blockSmoothed (n);
        const double lv = juce::Decibels::decibelsToGain (level->blockSmoothed (n));
        const auto m = modelFor (t);

        const double key = t * 1000.0 + tn * 3.0 + tg * 7.0;
        if (std::abs (key - lastKey) > 1.0e-6)
        {
            lastKey = key;
            using C = juce::dsp::IIR::Coefficients<double>;
            hp.setCoefficients     (C::makeHighPass (srate, t == 9 ? juce::jmax (tg, 60.0) : tg, 0.707));
            low.setCoefficients    (C::makeLowShelf (srate, 110.0, 0.707, juce::Decibels::decibelsToGain (m.lowDb)));
            mid.setCoefficients    (C::makePeakFilter (srate, 650.0, 0.8, juce::Decibels::decibelsToGain (m.midDb)));
            res.setCoefficients    (C::makePeakFilter (srate, m.resFreq, m.resQ, juce::Decibels::decibelsToGain (m.resDb)));
            bright.setCoefficients (C::makeHighShelf (srate, 3200.0, 0.7, juce::Decibels::decibelsToGain (m.brightDb)));
            lp.setCoefficients     (C::makeLowPass (srate, juce::jmap (tn, 0.0, 10.0, 2200.0, 11000.0), 0.707));
        }

        hp.processBuffer (buf);
        low.processBuffer (buf);
        mid.processBuffer (buf);
        res.processBuffer (buf);
        bright.processBuffer (buf);
        lp.processBuffer (buf);

        const double outGain = juce::Decibels::decibelsToGain (m.levelDb) * lv;
        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double L = l[i] * outGain, R = r[i] * outGain;
            if (m.compress > 0.0)
            {
                // active-pickup preamp squish: gentle level-dependent gain reduction
                const double e = env.processSample ((L + R) * 0.5);
                const double g = 1.0 / (1.0 + e * m.compress * 5.0);
                L *= (1.0 - m.compress * 0.5) + g * m.compress * 0.5 + m.compress * 0.55;
                R *= (1.0 - m.compress * 0.5) + g * m.compress * 0.5 + m.compress * 0.55;
            }
            if (m.heat > 0.0)
            {
                L = std::tanh (L * (1.0 + m.heat)) / std::tanh (1.0 + m.heat);
                R = std::tanh (R * (1.0 + m.heat)) / std::tanh (1.0 + m.heat);
            }
            l[i] = L; r[i] = R;
        }
    }

private:
    Param *typeSel, *level, *tone, *tight;
    dsp::StereoBiquad hp, low, mid, res, bright, lp;
    dsp::EnvFollower env;
    double lastKey = -1.0e9, srate = 48000.0;
};

} // namespace vp
