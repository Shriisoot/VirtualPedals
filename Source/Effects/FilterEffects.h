#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
// State-variable filter core used by the wah family (per-channel, double).
struct SVF
{
    double g = 0.0, k = 1.0, s1[2] {}, s2[2] {};
    double srate = 48000.0;

    void prepare (double sampleRate) { srate = sampleRate; reset(); }
    void reset() { s1[0] = s1[1] = s2[0] = s2[1] = 0.0; }

    void set (double freq, double q)
    {
        g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, srate * 0.45, freq) / srate);
        k = 1.0 / juce::jmax (0.1, q);
    }

    // returns {low, band, high}
    void processSample (int ch, double x, double& lo, double& bp, double& hi)
    {
        const double a1 = 1.0 / (1.0 + g * (g + k));
        hi = (x - s1[ch] * (g + k) - s2[ch]) * a1;
        bp = g * hi + s1[ch];
        s1[ch] = g * hi + bp;
        lo = g * bp + s2[ch];
        s2[ch] = g * bp + lo;
    }
};

//==============================================================================
class AutoWahPedal : public Pedal
{
public:
    AutoWahPedal() : Pedal ("autowah", "Auto Wah", "Filter", true)
    {
        sens  = addParam ({ "sens", "Sensitivity", "", 0.0, 10.0, 5.0 });
        range = addParam ({ "range", "Range", "", 0.0, 10.0, 5.0 });
        q     = addParam ({ "q", "Resonance", "", 0.5, 8.0, 3.0, 0.6 });
        dir   = addParam ({ "dir", "Direction", "", 0.0, 1.0, 0.0, 1.0, { "Up", "Down" } });
    }

    void prepare (double sampleRate, int) override
    {
        svf.prepare (sampleRate);
        env.prepare (sampleRate, 4.0, 120.0);
    }

    void reset() override { svf.reset(); env.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double se = sens->blockSmoothed (n);
        const double rg = range->blockSmoothed (n);
        const double qq = q->blockSmoothed (n);
        const bool down = dir->getBool();

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double e = env.processSample ((l[i] + r[i]) * 0.5);
            double sweep = juce::jlimit (0.0, 1.0, e * (1.0 + se * 4.0));
            if (down) sweep = 1.0 - sweep;
            const double freq = 250.0 * std::pow (2.0, sweep * (1.5 + rg * 0.35));
            svf.set (freq, qq);

            double lo, bp, hi;
            svf.processSample (0, l[i], lo, bp, hi);
            l[i] = bp * 1.4;
            svf.processSample (1, r[i], lo, bp, hi);
            r[i] = bp * 1.4;
        }
    }

private:
    Param *sens, *range, *q, *dir;
    SVF svf;
    dsp::EnvFollower env;
};

//==============================================================================
class EnvelopeFilterPedal : public Pedal
{
public:
    EnvelopeFilterPedal() : Pedal ("envfilter", "Envelope Filter", "Filter", true)
    {
        sens   = addParam ({ "sens", "Sensitivity", "", 0.0, 10.0, 5.0 });
        attack = addParam ({ "attack", "Attack", "ms", 0.5, 100.0, 8.0, 0.4 });
        decay  = addParam ({ "decay", "Decay", "ms", 20.0, 800.0, 200.0, 0.4 });
        q      = addParam ({ "q", "Resonance", "", 0.5, 10.0, 4.0, 0.6 });
        typeSel = addParam ({ "type", "Type", "", 0.0, 2.0, 0.0, 1.0, { "Low-pass", "Band-pass", "High-pass" } });
        baseF  = addParam ({ "base", "Base Freq", "Hz", 80.0, 1200.0, 220.0, 0.5 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        svf.prepare (sampleRate);
        env.prepare (sampleRate, 8.0, 200.0);
    }

    void reset() override { svf.reset(); env.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double se = sens->blockSmoothed (n);
        const double at = attack->blockSmoothed (n);
        const double de = decay->blockSmoothed (n);
        const double qq = q->blockSmoothed (n);
        const double bf = baseF->blockSmoothed (n);
        const int type = typeSel->getChoice();

        if (std::abs (at - lastAt) > 0.1 || std::abs (de - lastDe) > 1.0)
        {
            lastAt = at; lastDe = de;
            env.prepare (srate, at, de);
        }

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double e = env.processSample ((l[i] + r[i]) * 0.5);
            const double sweep = juce::jlimit (0.0, 1.0, e * (1.0 + se * 5.0));
            svf.set (bf * std::pow (2.0, sweep * 3.2), qq);

            double lo, bp, hi;
            svf.processSample (0, l[i], lo, bp, hi);
            l[i] = (type == 0 ? lo : type == 1 ? bp * 1.4 : hi);
            svf.processSample (1, r[i], lo, bp, hi);
            r[i] = (type == 0 ? lo : type == 1 ? bp * 1.4 : hi);
        }
    }

private:
    Param *sens, *attack, *decay, *q, *typeSel, *baseF;
    SVF svf;
    dsp::EnvFollower env;
    double lastAt = -1.0, lastDe = -1.0, srate = 48000.0;
};

//==============================================================================
// Vowel-formant filter bank ("talk box" style): morphs A-E-I-O-U.
class TalkBoxPedal : public Pedal
{
public:
    TalkBoxPedal() : Pedal ("talkbox", "Talk Filter", "Filter", true)
    {
        vowel = addParam ({ "vowel", "Vowel", "", 0.0, 4.0, 0.0 });   // continuous morph A..U
        auto1 = addParam ({ "auto", "Auto Mode", "", 0.0, 2.0, 0.0, 1.0, { "Manual", "Envelope", "LFO" } });
        rate  = addParam ({ "rate", "LFO Rate", "Hz", 0.1, 5.0, 0.6, 0.5 });
        reso  = addParam ({ "reso", "Intensity", "", 1.0, 10.0, 6.0 });
    }

    void prepare (double sampleRate, int) override
    {
        for (auto& f : formants) f.prepare (sampleRate);
        env.prepare (sampleRate, 5.0, 150.0);
        lfo.prepare (sampleRate);
    }

    void reset() override { for (auto& f : formants) f.reset(); env.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        // classic vowel formant tables (F1, F2, F3)
        static const double table[5][3] = {
            { 800.0, 1150.0, 2900.0 },   // A
            { 400.0, 1600.0, 2700.0 },   // E
            { 350.0, 1700.0, 2700.0 },   // I
            { 450.0,  800.0, 2830.0 },   // O
            { 325.0,  700.0, 2700.0 } }; // U

        const int n = buf.getNumSamples();
        const double vw = vowel->blockSmoothed (n);
        const int mode = auto1->getChoice();
        const double rs = reso->blockSmoothed (n);
        lfo.setRate (rate->blockSmoothed (n));

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double pos = vw;
            if (mode == 1)      pos = juce::jlimit (0.0, 4.0, env.processSample ((l[i] + r[i]) * 0.5) * 24.0);
            else if (mode == 2) pos = (lfo.next (dsp::LFO::Sine) * 0.5 + 0.5) * 4.0;

            const int v0 = juce::jlimit (0, 3, (int) pos);
            const double frac = juce::jlimit (0.0, 1.0, pos - v0);

            double outL = 0.0, outR = 0.0;
            for (int f = 0; f < 3; ++f)
            {
                const double freq = table[v0][f] + frac * (table[v0 + 1][f] - table[v0][f]);
                formants[(size_t) f].set (freq, rs);
                double lo, bp, hi;
                formants[(size_t) f].processSample (0, l[i], lo, bp, hi);
                outL += bp * (f == 0 ? 1.0 : f == 1 ? 0.7 : 0.35);
                formants[(size_t) f].processSample (1, r[i], lo, bp, hi);
                outR += bp * (f == 0 ? 1.0 : f == 1 ? 0.7 : 0.35);
            }
            l[i] = outL * 1.4;
            r[i] = outR * 1.4;
        }
    }

private:
    Param *vowel, *auto1, *rate, *reso;
    std::array<SVF, 3> formants;
    dsp::EnvFollower env;
    dsp::LFO lfo;
};

//==============================================================================
// Guitar-synth voicing: tracked sub-octave square + resonant filter sweep.
class SynthPedal : public Pedal
{
public:
    SynthPedal() : Pedal ("synth", "Synth", "Experimental", true)
    {
        subLevel = addParam ({ "sub", "Sub Osc", "%", 0.0, 100.0, 60.0 });
        square   = addParam ({ "square", "Square Fuzz", "%", 0.0, 100.0, 50.0 });
        cutoff   = addParam ({ "cutoff", "Cutoff", "Hz", 100.0, 8000.0, 1200.0, 0.4 });
        resoP    = addParam ({ "reso", "Resonance", "", 0.5, 10.0, 4.0 });
        envAmt   = addParam ({ "envamt", "Env Amount", "%", 0.0, 100.0, 60.0 });
    }

    void prepare (double sampleRate, int) override
    {
        svf.prepare (sampleRate);
        env.prepare (sampleRate, 3.0, 180.0);
        flip = 1.0; lastSign = 1.0;
    }

    void reset() override { svf.reset(); env.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double sub = subLevel->blockSmoothed (n) / 100.0;
        const double sq  = square->blockSmoothed (n) / 100.0;
        const double co  = cutoff->blockSmoothed (n);
        const double rs  = resoP->blockSmoothed (n);
        const double ea  = envAmt->blockSmoothed (n) / 100.0;

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double mono = (l[i] + r[i]) * 0.5;
            const double e = env.processSample (mono);

            // octave divider: flip a square on positive-going zero crossings
            const double sign = mono >= 0.0 ? 1.0 : -1.0;
            if (sign > 0.0 && lastSign < 0.0)
                flip = -flip;
            lastSign = sign;
            const double subOsc = flip * juce::jmin (e * 3.0, 0.6);

            const double squared = (mono >= 0.0 ? 1.0 : -1.0) * juce::jmin (e * 3.0, 0.7);
            double x = mono * (1.0 - sq) + squared * sq + subOsc * sub;

            svf.set (co * (1.0 + ea * e * 12.0), rs);
            double lo, bp, hi;
            svf.processSample (0, x, lo, bp, hi);
            l[i] = lo;
            svf.processSample (1, x, lo, bp, hi);
            r[i] = lo;
        }
    }

private:
    Param *subLevel, *square, *cutoff, *resoP, *envAmt;
    SVF svf;
    dsp::EnvFollower env;
    double flip = 1.0, lastSign = 1.0;
};

} // namespace vp
