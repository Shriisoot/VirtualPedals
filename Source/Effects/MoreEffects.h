#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class UniVibePedal : public Pedal
{
public:
    UniVibePedal() : Pedal ("univibe", "Uni-Vibe", "Modulation", true)
    {
        rate  = addParam ({ "rate", "Speed", "Hz", 0.1, 8.0, 1.2, 0.5 });
        depth = addParam ({ "depth", "Intensity", "%", 0.0, 100.0, 70.0 });
        modeS = addParam ({ "vmode", "Mode", "", 0.0, 1.0, 0.0, 1.0, { "Chorus", "Vibrato" } });
        throb = addParam ({ "throb", "Throb", "%", 0.0, 100.0, 50.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        lfo.prepare (sampleRate);
        std::fill (std::begin (state), std::end (state), 0.0);
    }

    void reset() override { std::fill (std::begin (state), std::end (state), 0.0); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double rt = rate->blockSmoothed (n);
        const double dp = depth->blockSmoothed (n) / 100.0;
        const double th = throb->blockSmoothed (n) / 100.0;
        const bool vibrato = modeS->getBool();
        lfo.setRate (rt);

        // the four photocell stages sit at staggered base frequencies
        static const double baseF[4] = { 320.0, 640.0, 1150.0, 2400.0 };

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double m = lfo.next (dsp::LFO::Sine);
            m = std::pow ((m + 1.0) * 0.5, 1.0 + th * 1.6); // lamp/photocell skewed throb
            double xL = l[i], xR = r[i];
            for (int s = 0; s < 4; ++s)
            {
                const double f = baseF[s] * std::pow (2.0, (m - 0.5) * 2.2 * dp);
                const double t = std::tan (juce::MathConstants<double>::pi * juce::jlimit (40.0, srate * 0.45, f) / srate);
                const double a = (t - 1.0) / (t + 1.0);
                double& s1 = state[s * 2];
                double& s2 = state[s * 2 + 1];
                const double yL = a * xL + s1; s1 = xL - a * yL; xL = yL;
                const double yR = a * xR + s2; s2 = xR - a * yR; xR = yR;
            }
            l[i] = vibrato ? xL : (l[i] + xL) * 0.5;
            r[i] = vibrato ? xR : (r[i] + xR) * 0.5;
        }
    }

private:
    Param *rate, *depth, *modeS, *throb;
    dsp::LFO lfo;
    double state[8] {};
    double srate = 48000.0;
};

//==============================================================================
// Classic crybaby-style wah. "Pedal" is expression-pedal friendly (MIDI learn).
class WahPedal : public Pedal
{
public:
    WahPedal() : Pedal ("wah", "Wah", "Filter")
    {
        pos   = addParam ({ "pedal", "Pedal", "%", 0.0, 100.0, 50.0 });
        range = addParam ({ "range", "Range", "", 0.0, 10.0, 5.0 });
        q     = addParam ({ "q", "Vocal", "", 1.0, 10.0, 5.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        g = 0.0; k = 1.0;
        s1[0] = s1[1] = s2[0] = s2[1] = 0.0;
        posSm.prepare (sampleRate, 18.0);
    }

    void reset() override { s1[0] = s1[1] = s2[0] = s2[1] = 0.0; }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        posSm.setTarget (pos->get() / 100.0);
        const double rg = range->blockSmoothed (n);
        const double qq = q->blockSmoothed (n);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double p = posSm.next();
            const double freq = 350.0 * std::pow (2.0, p * (1.6 + rg * 0.14));
            g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (100.0, srate * 0.45, freq) / srate);
            k = 1.0 / juce::jmax (0.4, qq * 0.55);
            const double a1 = 1.0 / (1.0 + g * (g + k));
            for (int ch = 0; ch < 2; ++ch)
            {
                const double x = ch == 0 ? l[i] : r[i];
                const double hi = (x - s1[ch] * (g + k) - s2[ch]) * a1;
                const double bp = g * hi + s1[ch];
                s1[ch] = g * hi + bp;
                const double lo = g * bp + s2[ch];
                s2[ch] = g * bp + lo;
                (ch == 0 ? l[i] : r[i]) = bp * 2.2 + x * 0.12;
            }
        }
    }

private:
    Param *pos, *range, *q;
    Smoother posSm;
    double g = 0.0, k = 1.0, s1[2] {}, s2[2] {};
    double srate = 48000.0;
};

//==============================================================================
class SlicerPedal : public Pedal
{
public:
    SlicerPedal() : Pedal ("slicer", "Slicer", "Experimental", true)
    {
        rate    = addParam ({ "rate", "Rate", "Hz", 1.0, 16.0, 8.0, 0.5 });
        pattern = addParam ({ "pattern", "Pattern", "", 0.0, 5.0, 0.0, 1.0,
                              { "8ths", "Offbeat", "Gallop", "Trance", "Stutter", "Random" } });
        depth   = addParam ({ "depth", "Depth", "%", 0.0, 100.0, 100.0 });
        smooth  = addParam ({ "smooth", "Smooth", "ms", 1.0, 30.0, 6.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        phase = 0.0; step = 0; gate = 1.0;
        rng.setSeedRandomly();
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        static const int patterns[5][8] = {
            { 1, 1, 1, 1, 1, 1, 1, 1 },
            { 0, 1, 0, 1, 0, 1, 0, 1 },
            { 1, 0, 1, 1, 1, 0, 1, 1 },  // gallop: da-dada
            { 1, 0, 0, 1, 0, 1, 1, 0 },
            { 1, 1, 0, 1, 1, 0, 1, 0 } };

        const int n = buf.getNumSamples();
        const double rt = rate->blockSmoothed (n);
        const int pat = pattern->getChoice();
        const double dp = depth->blockSmoothed (n) / 100.0;
        const double sm = smooth->blockSmoothed (n);
        const double coeff = std::exp (-1.0 / (0.001 * sm * srate));

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            phase += rt / srate;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                step = (step + 1) % 8;
                if (pat == 5) randomOn = rng.nextDouble() < 0.6;
            }
            const bool on = pat == 5 ? randomOn : patterns[pat][step] != 0;
            const double target = on ? 1.0 : 1.0 - dp;
            gate = target + coeff * (gate - target);
            l[i] *= gate;
            r[i] *= gate;
        }
    }

private:
    Param *rate, *pattern, *depth, *smooth;
    juce::Random rng;
    double phase = 0.0, gate = 1.0, srate = 48000.0;
    int step = 0;
    bool randomOn = true;
};

//==============================================================================
// Spring tank: bright, boingy short reverb with characteristic "drip".
class SpringReverbPedal : public Pedal
{
public:
    SpringReverbPedal() : Pedal ("springreverb", "Spring Reverb", "Reverb", true)
    {
        tension = addParam ({ "tension", "Tension", "", 0.0, 10.0, 5.0 });
        drip    = addParam ({ "drip", "Drip", "", 0.0, 10.0, 5.0 });
        toneP   = addParam ({ "tone", "Tone", "", 0.0, 10.0, 6.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        static const double apMs[4] = { 3.5, 5.1, 7.3, 9.7 };
        static const double combMs[3] = { 37.0, 41.3, 46.9 };
        for (int a = 0; a < 4; ++a)
        {
            ap[a].assign ((size_t) juce::jmax (4, (int) (apMs[a] * 0.001 * sampleRate)), 0.0);
            apIdx[a] = 0;
        }
        for (int c = 0; c < 3; ++c)
        {
            comb[c].assign ((size_t) juce::jmax (4, (int) (combMs[c] * 0.001 * sampleRate)), 0.0);
            combIdx[c] = 0;
        }
        dripFilter.prepare (sampleRate, 0);
        outLp.prepare (sampleRate, 0);
        wobble.prepare (sampleRate);
        wobble.setRate (4.1);
        lastKey = -1.0;
    }

    void reset() override
    {
        for (auto& b : ap) std::fill (b.begin(), b.end(), 0.0);
        for (auto& b : comb) std::fill (b.begin(), b.end(), 0.0);
        dripFilter.reset(); outLp.reset();
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double tens = tension->blockSmoothed (n);
        const double dr = drip->blockSmoothed (n);
        const double tn = toneP->blockSmoothed (n);

        const double key = dr * 3.0 + tn * 7.0;
        if (std::abs (key - lastKey) > 0.01)
        {
            lastKey = key;
            dripFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makePeakFilter (
                srate, 2400.0, 3.0, juce::Decibels::decibelsToGain (dr * 1.1)));
            outLp.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (
                srate, juce::jmap (tn, 0.0, 10.0, 2200.0, 6500.0), 0.707));
        }

        const double fb = 0.42 + tens * 0.045;

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double x = (l[i] + r[i]) * 0.5;

            // dispersive allpass chain = the "sproing"
            for (int a = 0; a < 4; ++a)
            {
                auto& b = ap[a];
                int& idx = apIdx[a];
                const double bufout = b[(size_t) idx];
                const double v = x + bufout * 0.62;
                b[(size_t) idx] = v;
                x = bufout - v * 0.62;
                if (++idx >= (int) b.size()) idx = 0;
            }

            const double wob = 1.0 + wobble.next (dsp::LFO::Sine) * 0.0009;
            double wet = 0.0;
            for (int c = 0; c < 3; ++c)
            {
                auto& b = comb[c];
                int& idx = combIdx[c];
                const int len = (int) ((double) b.size() * wob);
                const double y = b[(size_t) (idx % juce::jmax (1, len))];
                b[(size_t) (idx % juce::jmax (1, len))] = x + y * fb;
                idx = (idx + 1) % (int) b.size();
                wet += y;
            }
            wet = dripFilter.processSample (0, wet * 0.5);
            wet = outLp.processSample (0, wet);
            l[i] = wet;
            r[i] = wet;
        }
    }

private:
    Param *tension, *drip, *toneP;
    std::array<std::vector<double>, 4> ap;
    std::array<std::vector<double>, 3> comb;
    int apIdx[4] {}, combIdx[3] {};
    dsp::StereoBiquad dripFilter, outLp;
    dsp::LFO wobble;
    double lastKey = -1.0, srate = 48000.0;
};

//==============================================================================
class TrebleBoosterPedal : public Pedal
{
public:
    TrebleBoosterPedal() : Pedal ("trebleboost", "Treble Booster", "Drive")
    {
        boost = addParam ({ "boost", "Boost", "dB", 0.0, 30.0, 15.0 });
        rangeP = addParam ({ "range", "Range", "", 0.0, 2.0, 0.0, 1.0, { "Treble", "Mid", "Full" } });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        hp.prepare (sampleRate, 0);
        lastRange = -1;
    }

    void reset() override { hp.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const int rg = rangeP->getChoice();
        if (rg != lastRange)
        {
            lastRange = rg;
            const double f = rg == 0 ? 1000.0 : rg == 1 ? 500.0 : 120.0;
            hp.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighPass (srate, f, 0.6));
        }
        hp.processBuffer (buf);
        const double g = juce::Decibels::decibelsToGain (boost->blockSmoothed (n));
        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            // germanium-ish asymmetric soft clip
            l[i] = dsp::shape::tube (l[i] * g * 0.5) * 1.4;
            r[i] = dsp::shape::tube (r[i] * g * 0.5) * 1.4;
        }
    }

private:
    Param *boost, *rangeP;
    dsp::StereoBiquad hp;
    double srate = 48000.0;
    int lastRange = -1;
};

//==============================================================================
// ADT-style doubler: two slightly detuned, delayed copies panned wide.
class DoublerPedal : public Pedal
{
public:
    DoublerPedal() : Pedal ("doubler", "Doubler", "Modulation", true)
    {
        spread = addParam ({ "spread", "Spread", "%", 0.0, 100.0, 80.0 });
        humanise = addParam ({ "human", "Humanise", "%", 0.0, 100.0, 40.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        line.prepare (sampleRate, 0.08);
        drift1.prepare (sampleRate);
        drift2.prepare (sampleRate);
        drift1.setRate (0.31);
        drift2.setRate (0.43);
    }

    void reset() override { line.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double sp = spread->blockSmoothed (n) / 100.0;
        const double hu = humanise->blockSmoothed (n) / 100.0;

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double mono = (l[i] + r[i]) * 0.5;
            line.push (0, mono);
            line.push (1, mono);
            line.advance();

            const double d1 = (0.019 + drift1.next (dsp::LFO::Sine) * 0.0035 * hu) * srate;
            const double d2 = (0.028 + drift2.next (dsp::LFO::Sine) * 0.0042 * hu) * srate;
            const double v1 = line.read (0, d1);
            const double v2 = line.read (1, d2);

            l[i] = mono * (1.0 - sp * 0.5) + v1 * sp;
            r[i] = mono * (1.0 - sp * 0.5) + v2 * sp;
        }
    }

private:
    Param *spread, *humanise;
    dsp::StereoDelay line;
    dsp::LFO drift1, drift2;
    double srate = 48000.0;
};

//==============================================================================
// Volume-swell machine: every picked note fades in ("violining").
class SwellPedal : public Pedal
{
public:
    SwellPedal() : Pedal ("swell", "Swell", "Dynamics")
    {
        attack = addParam ({ "attack", "Swell Time", "ms", 50.0, 2000.0, 400.0, 0.5 });
        sens   = addParam ({ "sens", "Sensitivity", "", 0.0, 10.0, 5.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        det.prepare (sampleRate, 1.0, 120.0);
        ramp = 1.0; lastEnv = 0.0;
    }

    void reset() override { det.reset(); ramp = 1.0; }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double at = attack->blockSmoothed (n);
        const double se = sens->blockSmoothed (n);
        const double step = 1.0 / juce::jmax (1.0, at * 0.001 * srate);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double e = det.processSample ((l[i] + r[i]) * 0.5);
            if (e > lastEnv * (1.5 + (10.0 - se) * 0.4) && e > 0.015)
                ramp = 0.0;                                  // new pick: restart swell
            lastEnv = 0.999 * lastEnv + 0.001 * e;
            ramp = juce::jmin (1.0, ramp + step);
            const double g = ramp * ramp;                    // musical curve
            l[i] *= g;
            r[i] *= g;
        }
    }

private:
    Param *attack, *sens;
    dsp::EnvFollower det;
    double ramp = 1.0, lastEnv = 0.0, srate = 48000.0;
};

} // namespace vp
