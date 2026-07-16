#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class CompressorPedal : public Pedal
{
public:
    CompressorPedal() : Pedal ("compressor", "Compressor", "Dynamics", true)
    {
        threshold = addParam ({ "threshold", "Threshold", "dB", -60.0, 0.0, -24.0 });
        ratio     = addParam ({ "ratio", "Ratio", ":1", 1.0, 20.0, 4.0, 0.5 });
        attack    = addParam ({ "attack", "Attack", "ms", 0.1, 100.0, 5.0, 0.4 });
        release   = addParam ({ "release", "Release", "ms", 10.0, 1000.0, 120.0, 0.4 });
        makeup    = addParam ({ "makeup", "Makeup", "dB", 0.0, 24.0, 4.0 });
        knee      = addParam ({ "knee", "Knee", "dB", 0.0, 18.0, 6.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        env.prepare (sampleRate, 5.0, 120.0);
    }

    void reset() override { env.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double th = threshold->blockSmoothed (n);
        const double ra = juce::jmax (1.0, ratio->blockSmoothed (n));
        const double at = attack->blockSmoothed (n);
        const double re = release->blockSmoothed (n);
        const double mk = juce::Decibels::decibelsToGain (makeup->blockSmoothed (n));
        const double kn = knee->blockSmoothed (n);

        if (std::abs (at - lastAt) > 0.05 || std::abs (re - lastRe) > 0.5)
        {
            lastAt = at; lastRe = re;
            env.prepare (srate, at, re);
        }

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double peak = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            const double e = env.processSample (peak);
            const double eDb = juce::Decibels::gainToDecibels (e, -90.0);

            // soft-knee gain computer
            double overDb;
            const double half = kn * 0.5;
            if (eDb < th - half)       overDb = 0.0;
            else if (eDb > th + half)  overDb = eDb - th;
            else { const double d = eDb - th + half; overDb = (d * d) / (2.0 * juce::jmax (0.001, kn)); }

            const double grDb = overDb * (1.0 / ra - 1.0);
            const double g = juce::Decibels::decibelsToGain (grDb) * mk;
            currentGrDb.store ((float) -grDb, std::memory_order_relaxed);
            l[i] *= g;
            r[i] *= g;
        }
    }

    std::atomic<float> currentGrDb { 0.0f }; // for UI metering

private:
    Param *threshold, *ratio, *attack, *release, *makeup, *knee;
    dsp::EnvFollower env;
    double lastAt = -1.0, lastRe = -1.0, srate = 48000.0;
};

//==============================================================================
class NoiseGatePedal : public Pedal
{
public:
    NoiseGatePedal() : Pedal ("noisegate", "Noise Gate", "Dynamics")
    {
        threshold = addParam ({ "threshold", "Threshold", "dB", -90.0, -20.0, -60.0 });
        attack    = addParam ({ "attack", "Attack", "ms", 0.1, 50.0, 1.0, 0.4 });
        hold      = addParam ({ "hold", "Hold", "ms", 0.0, 500.0, 40.0 });
        release   = addParam ({ "release", "Release", "ms", 5.0, 1000.0, 90.0, 0.4 });
        range     = addParam ({ "range", "Range", "dB", -90.0, 0.0, -80.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        det.prepare (sampleRate, 0.2, 30.0);
        sideHp.prepare (sampleRate, 0);
        sideHp.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighPass (sampleRate, 55.0, 0.707));
        gateGain = 0.0;
        holdCounter = 0;
    }

    void reset() override { det.reset(); sideHp.reset(); gateGain = 0.0; holdCounter = 0; }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double thOpen  = juce::Decibels::decibelsToGain (threshold->blockSmoothed (n));
        const double thClose = thOpen * 0.5; // 6 dB hysteresis
        const double atSamps = juce::jmax (1.0, attack->get() * 0.001 * srate);
        const double reSamps = juce::jmax (1.0, release->get() * 0.001 * srate);
        const int holdSamps  = (int) (hold->get() * 0.001 * srate);
        const double floorGain = juce::Decibels::decibelsToGain (range->blockSmoothed (n));
        const double atStep = 1.0 / atSamps, reStep = 1.0 / reSamps;

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double side = sideHp.processSample (0, (l[i] + r[i]) * 0.5);
            const double e = det.processSample (side);

            if (e > thOpen) { open = true; holdCounter = holdSamps; }
            else if (e < thClose)
            {
                if (holdCounter > 0) --holdCounter;
                else open = false;
            }

            gateGain = open ? juce::jmin (1.0, gateGain + atStep)
                            : juce::jmax (0.0, gateGain - reStep);
            const double g = floorGain + (1.0 - floorGain) * gateGain;
            l[i] *= g;
            r[i] *= g;
        }
    }

private:
    Param *threshold, *attack, *hold, *release, *range;
    dsp::EnvFollower det;
    dsp::StereoBiquad sideHp;
    double gateGain = 0.0, srate = 48000.0;
    int holdCounter = 0;
    bool open = false;
};

//==============================================================================
class LimiterPedal : public Pedal
{
public:
    LimiterPedal() : Pedal ("limiter", "Limiter", "Dynamics")
    {
        ceiling = addParam ({ "ceiling", "Ceiling", "dB", -24.0, 0.0, -1.0 });
        release = addParam ({ "release", "Release", "ms", 10.0, 500.0, 80.0, 0.4 });
        inGain  = addParam ({ "ingain", "Input", "dB", -12.0, 24.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        lim.prepare (sampleRate);
    }

    void reset() override { lim.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        buf.applyGain (juce::Decibels::decibelsToGain (inGain->blockSmoothed (n)));
        const double re = release->blockSmoothed (n);
        if (std::abs (re - lastRe) > 0.5) { lastRe = re; lim.env.prepare (srate, 0.05, re); }
        lim.ceiling = juce::Decibels::decibelsToGain (ceiling->blockSmoothed (n));
        lim.processBuffer (buf);
    }

private:
    Param *ceiling, *release, *inGain;
    dsp::Limiter lim;
    double lastRe = -1.0, srate = 48000.0;
};

//==============================================================================
// Two-band adaptive noise reduction with a noise-floor "Learn" function.
class NoiseReductionPedal : public Pedal
{
public:
    NoiseReductionPedal() : Pedal ("noisereduction", "Noise Reduction", "Dynamics")
    {
        amount = addParam ({ "amount", "Reduction", "dB", 0.0, 30.0, 12.0 });
        learn  = addParam ({ "learn", "Learn Floor", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        humSel = addParam ({ "hum", "Hum Filter", "", 0.0, 2.0, 0.0, 1.0, { "Off", "50 Hz", "60 Hz" } });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        for (auto& e : bandEnv) e.prepare (sampleRate, 2.0, 60.0);
        split.prepare (sampleRate, 0);
        split.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, 2800.0, 0.707));
        rebuildHum (0);
        learnCounter = 0;
    }

    void reset() override
    {
        for (auto& e : bandEnv) e.reset();
        split.reset();
        for (auto& f : humNotch) f.reset();
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double redGain = juce::Decibels::decibelsToGain (-amount->blockSmoothed (n));

        const int hum = humSel->getChoice();
        if (hum != lastHum) rebuildHum (hum);
        if (hum > 0)
            for (auto& f : humNotch)
                f.processBuffer (buf);

        const bool learning = learn->getBool();

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double mono = (l[i] + r[i]) * 0.5;
            const double lo = split.processSample (0, mono);
            const double hi = mono - lo;
            const double eLo = bandEnv[0].processSample (lo);
            const double eHi = bandEnv[1].processSample (hi);

            if (learning)
            {
                floorLo = juce::jmax (floorLo * 0.9995, eLo);
                floorHi = juce::jmax (floorHi * 0.9995, eHi);
                ++learnCounter;
                if (learnCounter > (int) srate) // auto-finish after 1 s
                    learn->set (0.0);
            }

            // downward expansion per band: if band energy is near the learned floor, duck it
            const double margin = 2.2;
            const double gLo = eLo < floorLo * margin ? redGain : 1.0;
            const double gHi = eHi < floorHi * margin ? redGain : 1.0;
            gainLoSm = 0.999 * gainLoSm + 0.001 * gLo;
            gainHiSm = 0.999 * gainHiSm + 0.001 * gHi;

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = ch == 0 ? l : r;
                const double loB = splitStereo.processSample (ch, d[i]);
                const double hiB = d[i] - loB;
                d[i] = loB * gainLoSm + hiB * gainHiSm;
            }
        }
    }

private:
    void rebuildHum (int sel)
    {
        lastHum = sel;
        const double base = sel == 1 ? 50.0 : 60.0;
        for (int h = 0; h < (int) humNotch.size(); ++h)
        {
            humNotch[(size_t) h].prepare (srate, 0);
            humNotch[(size_t) h].setCoefficients (
                juce::dsp::IIR::Coefficients<double>::makeNotch (srate, base * (h + 1), 30.0));
        }
        splitStereo.prepare (srate, 0);
        splitStereo.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (srate, 2800.0, 0.707));
    }

    Param *amount, *learn, *humSel;
    dsp::EnvFollower bandEnv[2];
    dsp::StereoBiquad split, splitStereo;
    std::array<dsp::StereoBiquad, 4> humNotch;
    double floorLo = 1.0e-5, floorHi = 1.0e-5;
    double gainLoSm = 1.0, gainHiSm = 1.0;
    double srate = 48000.0;
    int lastHum = -1, learnCounter = 0;
};

//==============================================================================
// Dedicated mains-hum eliminator: deep notches at the fundamental + 5 harmonics.
class HumFilterPedal : public Pedal
{
public:
    HumFilterPedal() : Pedal ("humfilter", "Hum Eliminator", "Utility")
    {
        freqSel = addParam ({ "freq", "Mains", "", 0.0, 1.0, 1.0, 1.0, { "50 Hz", "60 Hz" } });
        depth   = addParam ({ "depth", "Depth", "", 1.0, 10.0, 8.0 });
        width   = addParam ({ "width", "Width", "", 1.0, 10.0, 3.0 });
    }

    void prepare (double sampleRate, int) override { srate = sampleRate; lastKey = -1.0; }
    void reset() override { for (auto& f : notches) f.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const double key = freqSel->get() * 1000.0 + depth->get() * 10.0 + width->get();
        if (std::abs (key - lastKey) > 0.01)
        {
            lastKey = key;
            const double base = freqSel->getChoice() == 0 ? 50.0 : 60.0;
            const double q = juce::jmap (width->get(), 1.0, 10.0, 60.0, 8.0);
            for (int h = 0; h < (int) notches.size(); ++h)
            {
                notches[(size_t) h].prepare (srate, 0);
                notches[(size_t) h].setCoefficients (
                    juce::dsp::IIR::Coefficients<double>::makeNotch (srate, base * (h + 1), q));
            }
        }
        const int passes = juce::jlimit (1, 2, (int) (depth->get() / 5.0) + 1);
        for (int p = 0; p < passes; ++p)
            for (auto& f : notches)
                f.processBuffer (buf);
    }

private:
    Param *freqSel, *depth, *width;
    std::array<dsp::StereoBiquad, 6> notches;
    double lastKey = -1.0, srate = 48000.0;
};

} // namespace vp
