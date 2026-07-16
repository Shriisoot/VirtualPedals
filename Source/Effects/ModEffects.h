#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class ChorusPedal : public Pedal
{
public:
    ChorusPedal() : Pedal ("chorus", "Chorus", "Modulation", true)
    {
        rate   = addParam ({ "rate", "Rate", "Hz", 0.05, 8.0, 0.8, 0.5 });
        depth  = addParam ({ "depth", "Depth", "%", 0.0, 100.0, 40.0 });
        voices = addParam ({ "voices", "Voices", "", 1.0, 3.0, 2.0, 1.0, { "1", "2", "3" } });
        widthP = addParam ({ "width", "Width", "%", 0.0, 100.0, 80.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        line.prepare (sampleRate, 0.06);
        for (int v = 0; v < 3; ++v) { lfos[v].prepare (sampleRate); lfos[v].resetPhase (v * 0.33); }
    }

    void reset() override { line.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double rt = rate->blockSmoothed (n);
        const double dp = depth->blockSmoothed (n) / 100.0;
        const double wd = widthP->blockSmoothed (n) / 100.0;
        const int nv = voices->getChoice() + 1;

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            line.push (0, l[i]);
            line.push (1, r[i]);
            line.advance();

            double wetL = 0.0, wetR = 0.0;
            for (int v = 0; v < nv; ++v)
            {
                lfos[v].setRate (rt * (1.0 + v * 0.13));
                const double m = lfos[v].next (dsp::LFO::Sine);
                const double base = 0.012 + v * 0.004;
                const double dly = (base + m * 0.005 * dp) * srate;
                const double pan = nv == 1 ? 0.0 : ((double) v / (nv - 1) * 2.0 - 1.0) * wd;
                const double tapL = line.read (0, dly);
                const double tapR = line.read (1, dly * 1.01);
                wetL += tapL * (1.0 - juce::jmax (0.0, pan));
                wetR += tapR * (1.0 + juce::jmin (0.0, pan));
            }
            const double norm = 1.0 / std::sqrt ((double) nv);
            l[i] = wetL * norm;
            r[i] = wetR * norm;
        }
    }

private:
    Param *rate, *depth, *voices, *widthP;
    dsp::StereoDelay line;
    dsp::LFO lfos[3];
    double srate = 48000.0;
};

//==============================================================================
class FlangerPedal : public Pedal
{
public:
    FlangerPedal() : Pedal ("flanger", "Flanger", "Modulation", true)
    {
        rate     = addParam ({ "rate", "Rate", "Hz", 0.05, 5.0, 0.3, 0.5 });
        depth    = addParam ({ "depth", "Depth", "%", 0.0, 100.0, 70.0 });
        feedback = addParam ({ "feedback", "Feedback", "%", -95.0, 95.0, 40.0 });
        manual   = addParam ({ "manual", "Manual", "ms", 0.5, 8.0, 2.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        line.prepare (sampleRate, 0.03);
        lfo.prepare (sampleRate);
        fbL = fbR = 0.0;
    }

    void reset() override { line.reset(); fbL = fbR = 0.0; }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double rt = rate->blockSmoothed (n);
        const double dp = depth->blockSmoothed (n) / 100.0;
        const double fb = feedback->blockSmoothed (n) / 100.0;
        const double man = manual->blockSmoothed (n) * 0.001 * srate;
        lfo.setRate (rt);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double m = (lfo.next (dsp::LFO::Sine) + 1.0) * 0.5; // 0..1
            const double dly = juce::jmax (4.0, man + m * dp * 0.004 * srate);

            line.push (0, l[i] + fbL * fb);
            line.push (1, r[i] + fbR * fb);
            line.advance();

            fbL = line.read (0, dly);
            fbR = line.read (1, dly * 1.005);
            l[i] = (l[i] + fbL) * 0.7;
            r[i] = (r[i] + fbR) * 0.7;
        }
    }

private:
    Param *rate, *depth, *feedback, *manual;
    dsp::StereoDelay line;
    dsp::LFO lfo;
    double fbL = 0.0, fbR = 0.0, srate = 48000.0;
};

//==============================================================================
class PhaserPedal : public Pedal
{
public:
    static constexpr int maxStages = 12;

    PhaserPedal() : Pedal ("phaser", "Phaser", "Modulation", true)
    {
        rate     = addParam ({ "rate", "Rate", "Hz", 0.05, 8.0, 0.5, 0.5 });
        depth    = addParam ({ "depth", "Depth", "%", 0.0, 100.0, 80.0 });
        feedback = addParam ({ "feedback", "Feedback", "%", 0.0, 90.0, 30.0 });
        stages   = addParam ({ "stages", "Stages", "", 0.0, 2.0, 1.0, 1.0, { "4", "8", "12" } });
        center   = addParam ({ "center", "Center", "Hz", 200.0, 2000.0, 700.0, 0.5 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        lfo.prepare (sampleRate);
        std::fill (std::begin (apState), std::end (apState), 0.0);
        fbL = fbR = 0.0;
    }

    void reset() override
    {
        std::fill (std::begin (apState), std::end (apState), 0.0);
        fbL = fbR = 0.0;
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double rt = rate->blockSmoothed (n);
        const double dp = depth->blockSmoothed (n) / 100.0;
        const double fb = feedback->blockSmoothed (n) / 100.0;
        const double ct = center->blockSmoothed (n);
        const int ns = (stages->getChoice() + 1) * 4;
        lfo.setRate (rt);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double m = (lfo.next (dsp::LFO::Sine) + 1.0) * 0.5;
            const double freq = ct * std::pow (4.0, (m - 0.5) * dp);
            // first-order allpass coefficient
            const double t = std::tan (juce::MathConstants<double>::pi * juce::jlimit (30.0, srate * 0.45, freq) / srate);
            const double a = (t - 1.0) / (t + 1.0);

            double xL = l[i] + fbL * fb;
            double xR = r[i] + fbR * fb;
            for (int s = 0; s < ns; ++s)
            {
                // allpass: y = a*x + x1 - a*y1  (state form)
                double& s1 = apState[s * 2];
                double& s2 = apState[s * 2 + 1];
                const double yL = a * xL + s1; s1 = xL - a * yL; xL = yL;
                const double yR = a * xR + s2; s2 = xR - a * yR; xR = yR;
            }
            fbL = xL; fbR = xR;
            l[i] = (l[i] + xL) * 0.5;
            r[i] = (r[i] + xR) * 0.5;
        }
    }

private:
    Param *rate, *depth, *feedback, *stages, *center;
    dsp::LFO lfo;
    double apState[maxStages * 2] {};
    double fbL = 0.0, fbR = 0.0, srate = 48000.0;
};

//==============================================================================
class TremoloPedal : public Pedal
{
public:
    TremoloPedal() : Pedal ("tremolo", "Tremolo", "Modulation")
    {
        rate   = addParam ({ "rate", "Rate", "Hz", 0.5, 16.0, 5.0, 0.5 });
        depth  = addParam ({ "depth", "Depth", "%", 0.0, 100.0, 60.0 });
        shapeP = addParam ({ "shape", "Shape", "", 0.0, 2.0, 0.0, 1.0, { "Sine", "Triangle", "Square" } });
        stereo = addParam ({ "stereo", "Stereo Phase", "°", 0.0, 180.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        lfoL.prepare (sampleRate);
        lfoR.prepare (sampleRate);
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double rt = rate->blockSmoothed (n);
        const double dp = depth->blockSmoothed (n) / 100.0;
        const double ph = stereo->blockSmoothed (n) / 360.0;
        const int sh = shapeP->getChoice() == 2 ? dsp::LFO::Square : shapeP->getChoice() == 1 ? dsp::LFO::Triangle : dsp::LFO::Sine;

        lfoL.setRate (rt);
        lfoR.setRate (rt);
        lfoR.resetPhase (lfoL.phase + ph >= 1.0 ? lfoL.phase + ph - 1.0 : lfoL.phase + ph);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double gl = 1.0 - dp * (0.5 + 0.5 * lfoL.next (sh));
            double gr = 1.0 - dp * (0.5 + 0.5 * lfoR.next (sh));
            // smooth square edges slightly to avoid clicks
            smoothL = 0.995 * smoothL + 0.005 * gl;
            smoothR = 0.995 * smoothR + 0.005 * gr;
            l[i] *= smoothL;
            r[i] *= smoothR;
        }
    }

private:
    Param *rate, *depth, *shapeP, *stereo;
    dsp::LFO lfoL, lfoR;
    double smoothL = 1.0, smoothR = 1.0;
};

//==============================================================================
class VibratoPedal : public Pedal
{
public:
    VibratoPedal() : Pedal ("vibrato", "Vibrato", "Modulation")
    {
        rate  = addParam ({ "rate", "Rate", "Hz", 0.5, 12.0, 5.0, 0.5 });
        depth = addParam ({ "depth", "Depth", "%", 0.0, 100.0, 35.0 });
        rise  = addParam ({ "rise", "Rise", "ms", 0.0, 2000.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        line.prepare (sampleRate, 0.03);
        lfo.prepare (sampleRate);
        riseEnv = 0.0;
    }

    void reset() override { line.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double rt = rate->blockSmoothed (n);
        const double dp = depth->blockSmoothed (n) / 100.0;
        const double riseMs = rise->blockSmoothed (n);
        const double riseStep = riseMs <= 1.0 ? 1.0 : 1.0 / (riseMs * 0.001 * srate);
        lfo.setRate (rt);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            riseEnv = juce::jmin (1.0, riseEnv + riseStep);
            const double m = lfo.next (dsp::LFO::Sine);
            const double dly = (0.008 + m * 0.0035 * dp * riseEnv) * srate;
            line.push (0, l[i]);
            line.push (1, r[i]);
            line.advance();
            l[i] = line.read (0, dly);
            r[i] = line.read (1, dly);
        }
    }

private:
    Param *rate, *depth, *rise;
    dsp::StereoDelay line;
    dsp::LFO lfo;
    double riseEnv = 0.0, srate = 48000.0;
};

//==============================================================================
class RotaryPedal : public Pedal
{
public:
    RotaryPedal() : Pedal ("rotary", "Rotary Speaker", "Modulation", true)
    {
        speed  = addParam ({ "speed", "Speed", "", 0.0, 1.0, 0.0, 1.0, { "Slow", "Fast" } });
        accel  = addParam ({ "accel", "Ramp", "s", 0.2, 5.0, 1.2 });
        balanceP = addParam ({ "balance", "Horn/Drum", "%", 0.0, 100.0, 55.0 });
        drive  = addParam ({ "drive", "Drive", "", 0.0, 10.0, 1.5 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        xover.prepare (sampleRate, 0);
        xover.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, 800.0, 0.707));
        hornLine.prepare (sampleRate, 0.01);
        drumLine.prepare (sampleRate, 0.01);
        hornRate = 0.8; drumRate = 0.7;
        hornPhase = 0.0; drumPhase = 0.31;
    }

    void reset() override { xover.reset(); hornLine.reset(); drumLine.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const bool fast = speed->getBool();
        const double ramp = accel->blockSmoothed (n);
        const double bal = balanceP->blockSmoothed (n) / 100.0;
        const double drv = 1.0 + drive->blockSmoothed (n) * 0.35;

        const double hornTarget = fast ? 6.8 : 0.8;
        const double drumTarget = fast ? 5.9 : 0.7;
        const double rampCoeff = std::exp (-1.0 / (ramp * srate));

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            hornRate = hornTarget + rampCoeff * (hornRate - hornTarget); // inertia
            drumRate = drumTarget + rampCoeff * (drumRate - drumTarget);
            hornPhase += hornRate / srate; if (hornPhase >= 1.0) hornPhase -= 1.0;
            drumPhase += drumRate / srate; if (drumPhase >= 1.0) drumPhase -= 1.0;

            const double mono = std::tanh ((l[i] + r[i]) * 0.5 * drv);
            const double low = xover.processSample (0, mono);
            const double high = mono - low;

            const double hs = std::sin (hornPhase * juce::MathConstants<double>::twoPi);
            const double hc = std::cos (hornPhase * juce::MathConstants<double>::twoPi);
            const double ds = std::sin (drumPhase * juce::MathConstants<double>::twoPi);

            // doppler (FM) + level (AM) + pan per rotor
            hornLine.push (0, high); hornLine.push (1, high); hornLine.advance();
            drumLine.push (0, low);  drumLine.push (1, low);  drumLine.advance();
            const double hTap = hornLine.read (0, 4.0 + (hs + 1.0) * 0.0011 * srate);
            const double dTap = drumLine.read (0, 4.0 + (ds + 1.0) * 0.0006 * srate);

            const double hornAm = 0.72 + 0.28 * hc;
            const double drumAm = 0.85 + 0.15 * ds;

            const double hornL = hTap * hornAm * (0.5 + 0.45 * hc);
            const double hornR = hTap * hornAm * (0.5 - 0.45 * hc);
            const double drumL = dTap * drumAm * (0.5 + 0.25 * ds);
            const double drumR = dTap * drumAm * (0.5 - 0.25 * ds);

            l[i] = hornL * bal + drumL * (1.0 - bal);
            r[i] = hornR * bal + drumR * (1.0 - bal);
        }
    }

private:
    Param *speed, *accel, *balanceP, *drive;
    dsp::StereoBiquad xover;
    dsp::StereoDelay hornLine, drumLine;
    double hornRate = 0.8, drumRate = 0.7, hornPhase = 0.0, drumPhase = 0.31, srate = 48000.0;
};

//==============================================================================
class RingModPedal : public Pedal
{
public:
    RingModPedal() : Pedal ("ringmod", "Ring Modulator", "Experimental", true)
    {
        freq   = addParam ({ "freq", "Frequency", "Hz", 20.0, 4000.0, 440.0, 0.4 });
        shapeP = addParam ({ "shape", "Carrier", "", 0.0, 1.0, 0.0, 1.0, { "Sine", "Square" } });
        track  = addParam ({ "track", "Env Track", "%", 0.0, 100.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        phase = 0.0;
        env.prepare (sampleRate, 5.0, 80.0);
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double f = freq->blockSmoothed (n);
        const double tr = track->blockSmoothed (n) / 100.0;
        const bool square = shapeP->getBool();

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double e = env.processSample ((l[i] + r[i]) * 0.5);
            const double fm = f * (1.0 + tr * e * 8.0);
            phase += fm / srate;
            if (phase >= 1.0) phase -= std::floor (phase);
            double c = std::sin (phase * juce::MathConstants<double>::twoPi);
            if (square) c = c >= 0.0 ? 1.0 : -1.0;
            l[i] *= c;
            r[i] *= c;
        }
    }

private:
    Param *freq, *shapeP, *track;
    dsp::EnvFollower env;
    double phase = 0.0, srate = 48000.0;
};

} // namespace vp
