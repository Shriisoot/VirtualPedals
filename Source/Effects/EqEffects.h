#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class ParametricEQPedal : public Pedal
{
public:
    ParametricEQPedal() : Pedal ("parametriceq", "Parametric EQ", "EQ")
    {
        lowCut   = addParam ({ "lowcut", "Low Cut", "Hz", 20.0, 800.0, 20.0, 0.4 });
        lowGain  = addParam ({ "low", "Low", "dB", -15.0, 15.0, 0.0 });
        mid1Freq = addParam ({ "mid1f", "Mid 1 Freq", "Hz", 100.0, 2000.0, 400.0, 0.4 });
        mid1Gain = addParam ({ "mid1g", "Mid 1", "dB", -15.0, 15.0, 0.0 });
        mid2Freq = addParam ({ "mid2f", "Mid 2 Freq", "Hz", 500.0, 8000.0, 2000.0, 0.4 });
        mid2Gain = addParam ({ "mid2g", "Mid 2", "dB", -15.0, 15.0, 0.0 });
        highGain = addParam ({ "high", "High", "dB", -15.0, 15.0, 0.0 });
        highCut  = addParam ({ "highcut", "High Cut", "kHz", 2.0, 20.0, 20.0, 0.6 });
        qParam   = addParam ({ "q", "Q", "", 0.3, 4.0, 0.9, 0.6 });
        level    = addParam ({ "level", "Level", "dB", -12.0, 12.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        for (auto* f : { &fLowCut, &fLow, &fMid1, &fMid2, &fHigh, &fHighCut })
            f->prepare (sampleRate, 0);
        lastKey = -1.0;
    }

    void reset() override
    {
        for (auto* f : { &fLowCut, &fLow, &fMid1, &fMid2, &fHigh, &fHighCut })
            f->reset();
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double key = lowCut->get() + lowGain->get() * 3.1 + mid1Freq->get() * 1.7 + mid1Gain->get() * 5.3
                         + mid2Freq->get() * 0.9 + mid2Gain->get() * 7.9 + highGain->get() * 11.0
                         + highCut->get() * 13.0 + qParam->get() * 17.0;
        if (std::abs (key - lastKey) > 1.0e-6)
        {
            lastKey = key;
            const double q = qParam->get();
            using C = juce::dsp::IIR::Coefficients<double>;
            fLowCut.setCoefficients (C::makeHighPass (srate, lowCut->get(), 0.707));
            fLow.setCoefficients (C::makeLowShelf (srate, 120.0, 0.707, juce::Decibels::decibelsToGain (lowGain->get())));
            fMid1.setCoefficients (C::makePeakFilter (srate, mid1Freq->get(), q, juce::Decibels::decibelsToGain (mid1Gain->get())));
            fMid2.setCoefficients (C::makePeakFilter (srate, mid2Freq->get(), q, juce::Decibels::decibelsToGain (mid2Gain->get())));
            fHigh.setCoefficients (C::makeHighShelf (srate, 3200.0, 0.707, juce::Decibels::decibelsToGain (highGain->get())));
            fHighCut.setCoefficients (C::makeLowPass (srate, juce::jmin (highCut->get() * 1000.0, srate * 0.49), 0.707));
        }

        if (lowCut->get() > 21.0)   fLowCut.processBuffer (buf);
        fLow.processBuffer (buf);
        fMid1.processBuffer (buf);
        fMid2.processBuffer (buf);
        fHigh.processBuffer (buf);
        if (highCut->get() < 19.9)  fHighCut.processBuffer (buf);

        buf.applyGain (juce::Decibels::decibelsToGain (level->blockSmoothed (n)));
    }

private:
    Param *lowCut, *lowGain, *mid1Freq, *mid1Gain, *mid2Freq, *mid2Gain, *highGain, *highCut, *qParam, *level;
    dsp::StereoBiquad fLowCut, fLow, fMid1, fMid2, fHigh, fHighCut;
    double lastKey = -1.0, srate = 48000.0;
};

//==============================================================================
class GraphicEQPedal : public Pedal
{
public:
    GraphicEQPedal() : Pedal ("graphiceq", "Graphic EQ", "EQ")
    {
        static const double freqs[7] = { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };
        for (int i = 0; i < 7; ++i)
            bands[i] = addParam ({ "b" + juce::String (i), juce::String ((int) freqs[i]) + (freqs[i] >= 1000 ? "" : "") + " Hz",
                                   "dB", -12.0, 12.0, 0.0 });
        level = addParam ({ "level", "Level", "dB", -12.0, 12.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        for (auto& f : filters) f.prepare (sampleRate, 0);
        lastKey = -1.0;
    }

    void reset() override { for (auto& f : filters) f.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        static const double freqs[7] = { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };
        double key = 0.0;
        for (int i = 0; i < 7; ++i) key += bands[i]->get() * (i * 3.7 + 1.0);
        if (std::abs (key - lastKey) > 1.0e-6)
        {
            lastKey = key;
            for (int i = 0; i < 7; ++i)
                filters[(size_t) i].setCoefficients (juce::dsp::IIR::Coefficients<double>::makePeakFilter (
                    srate, freqs[i], 1.1, juce::Decibels::decibelsToGain (bands[i]->get())));
        }
        for (auto& f : filters) f.processBuffer (buf);
        buf.applyGain (juce::Decibels::decibelsToGain (level->blockSmoothed (buf.getNumSamples())));
    }

private:
    Param* bands[7] {};
    Param* level {};
    std::array<dsp::StereoBiquad, 7> filters;
    double lastKey = -1.0, srate = 48000.0;
};

} // namespace vp
