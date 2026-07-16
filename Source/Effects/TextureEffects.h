#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class BitCrusherPedal : public Pedal
{
public:
    BitCrusherPedal() : Pedal ("bitcrusher", "Bit Crusher", "Experimental", true)
    {
        bits = addParam ({ "bits", "Bits", "", 1.0, 16.0, 12.0 });
        rateDiv = addParam ({ "ratediv", "Rate Divide", "x", 1.0, 50.0, 1.0, 0.5 });
    }

    void prepare (double, int) override { holdL = holdR = 0.0; counter = 0.0; }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double b = bits->blockSmoothed (n);
        const double div = rateDiv->blockSmoothed (n);
        const double steps = std::pow (2.0, b - 1.0);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            counter += 1.0;
            if (counter >= div)
            {
                counter -= div;
                holdL = dsp::shape::crush (l[i], steps);
                holdR = dsp::shape::crush (r[i], steps);
            }
            l[i] = holdL;
            r[i] = holdR;
        }
    }

private:
    Param *bits, *rateDiv;
    double holdL = 0.0, holdR = 0.0, counter = 0.0;
};

//==============================================================================
// Granular cloud from a rolling capture buffer.
class GranularPedal : public Pedal
{
public:
    static constexpr int numGrains = 8;

    GranularPedal() : Pedal ("granular", "Granular", "Experimental", true)
    {
        grainSize = addParam ({ "size", "Grain Size", "ms", 20.0, 500.0, 120.0, 0.5 });
        density   = addParam ({ "density", "Density", "", 1.0, 8.0, 4.0 });
        jitter    = addParam ({ "jitter", "Position Jitter", "%", 0.0, 100.0, 40.0 });
        pitchJit  = addParam ({ "pjit", "Pitch Spray", "st", 0.0, 12.0, 0.0 });
        reverseP  = addParam ({ "grev", "Reverse Grains", "%", 0.0, 100.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        const int len = juce::nextPowerOfTwo ((int) (sampleRate * 2.0));
        mask = len - 1;
        for (auto& b : buf2) b.assign ((size_t) len, 0.0);
        writeIdx = 0;
        for (auto& g : grains) g.active = false;
        rng.setSeedRandomly();
    }

    void reset() override
    {
        for (auto& b : buf2) std::fill (b.begin(), b.end(), 0.0);
        for (auto& g : grains) g.active = false;
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double sizeMs = grainSize->blockSmoothed (n);
        const double dens = density->blockSmoothed (n);
        const double jit = jitter->blockSmoothed (n) / 100.0;
        const double pj = pitchJit->blockSmoothed (n);
        const double revProb = reverseP->blockSmoothed (n) / 100.0;

        const int grainSamps = juce::jmax (64, (int) (sizeMs * 0.001 * srate));
        const double spawnInterval = (double) grainSamps / juce::jmax (1.0, dens);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            buf2[0][(size_t) (writeIdx & mask)] = l[i];
            buf2[1][(size_t) (writeIdx & mask)] = r[i];

            spawnTimer += 1.0;
            if (spawnTimer >= spawnInterval)
            {
                spawnTimer -= spawnInterval;
                for (auto& g : grains)
                {
                    if (! g.active)
                    {
                        g.active = true;
                        g.age = 0;
                        g.length = grainSamps;
                        const double back = 800.0 + rng.nextDouble() * jit * srate * 1.2;
                        g.pos = (double) writeIdx - back;
                        g.rate = std::pow (2.0, (rng.nextDouble() * 2.0 - 1.0) * pj / 12.0);
                        if (rng.nextDouble() < revProb) g.rate = -g.rate;
                        g.pan = rng.nextDouble();
                        break;
                    }
                }
            }

            double outL = 0.0, outR = 0.0;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const double w = std::sin (juce::MathConstants<double>::pi * (double) g.age / (double) g.length);
                const int idx = ((int) g.pos) & mask;
                const double sL = buf2[0][(size_t) idx];
                const double sR = buf2[1][(size_t) idx];
                outL += (sL * (1.0 - g.pan) + sL * 0.3) * w;
                outR += (sR * g.pan + sR * 0.3) * w;
                g.pos += g.rate;
                if (++g.age >= g.length) g.active = false;
            }
            const double norm = 0.55;
            l[i] = outL * norm;
            r[i] = outR * norm;
            ++writeIdx;
        }
    }

private:
    struct Grain { bool active = false; int age = 0, length = 0; double pos = 0.0, rate = 1.0, pan = 0.5; };
    Param *grainSize, *density, *jitter, *pitchJit, *reverseP;
    std::vector<double> buf2[2];
    Grain grains[numGrains];
    juce::Random rng;
    int writeIdx = 0, mask = 0;
    double spawnTimer = 0.0, srate = 48000.0;
};

//==============================================================================
class TapePedal : public Pedal
{
public:
    TapePedal() : Pedal ("tape", "Tape Machine", "Experimental")
    {
        satAmt  = addParam ({ "sat", "Saturation", "", 0.0, 10.0, 4.0 });
        wow     = addParam ({ "wow", "Wow", "", 0.0, 10.0, 2.0 });
        flutter = addParam ({ "flutter", "Flutter", "", 0.0, 10.0, 2.0 });
        ageP    = addParam ({ "age", "Age", "", 0.0, 10.0, 3.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        line.prepare (sampleRate, 0.05);
        wowLfo.prepare (sampleRate);
        flutLfo.prepare (sampleRate);
        headBump.prepare (sampleRate, 0);
        rolloff.prepare (sampleRate, 0);
        lastAge = -1.0;
    }

    void reset() override { line.reset(); headBump.reset(); rolloff.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double sat = satAmt->blockSmoothed (n);
        const double w = wow->blockSmoothed (n) / 10.0;
        const double f = flutter->blockSmoothed (n) / 10.0;
        const double age = ageP->blockSmoothed (n);

        if (std::abs (age - lastAge) > 0.05)
        {
            lastAge = age;
            headBump.setCoefficients (juce::dsp::IIR::Coefficients<double>::makePeakFilter (srate, 90.0, 0.8,
                juce::Decibels::decibelsToGain (1.0 + age * 0.35)));
            rolloff.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (srate,
                juce::jmap (age, 0.0, 10.0, 16000.0, 5500.0), 0.707));
        }

        wowLfo.setRate (0.6);
        flutLfo.setRate (8.7);

        const double drive = 1.0 + sat * 0.5;
        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double mod = wowLfo.next (dsp::LFO::Sine) * 0.0022 * w
                             + flutLfo.next (dsp::LFO::Sine) * 0.0004 * f;
            const double dly = (0.012 + mod) * srate;
            line.push (0, l[i]);
            line.push (1, r[i]);
            line.advance();
            l[i] = std::tanh (line.read (0, dly) * drive) / std::tanh (drive) * 0.9;
            r[i] = std::tanh (line.read (1, dly) * drive) / std::tanh (drive) * 0.9;
        }
        headBump.processBuffer (buf);
        rolloff.processBuffer (buf);
    }

private:
    Param *satAmt, *wow, *flutter, *ageP;
    dsp::StereoDelay line;
    dsp::LFO wowLfo, flutLfo;
    dsp::StereoBiquad headBump, rolloff;
    double lastAge = -1.0, srate = 48000.0;
};

//==============================================================================
class GlitchPedal : public Pedal
{
public:
    GlitchPedal() : Pedal ("glitch", "Glitch", "Experimental", true)
    {
        prob    = addParam ({ "prob", "Trigger Chance", "%", 0.0, 100.0, 35.0 });
        sliceMs = addParam ({ "slice", "Slice", "ms", 20.0, 400.0, 90.0, 0.5 });
        repeats = addParam ({ "repeats", "Max Repeats", "", 1.0, 8.0, 3.0 });
        stutterPitch = addParam ({ "spitch", "Pitch Drop", "", 0.0, 1.0, 0.0, 1.0, {}, true });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        const int len = juce::nextPowerOfTwo ((int) (sampleRate * 1.0));
        mask = len - 1;
        for (auto& b : cap) b.assign ((size_t) len, 0.0);
        writeIdx = 0; state = Passing; rng.setSeedRandomly();
    }

    void reset() override { state = Passing; }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double p = prob->blockSmoothed (n) / 100.0;
        const int slice = juce::jmax (64, (int) (sliceMs->blockSmoothed (n) * 0.001 * srate));
        const int maxRep = (int) repeats->blockSmoothed (n);
        const bool drop = stutterPitch->getBool();

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            cap[0][(size_t) (writeIdx & mask)] = l[i];
            cap[1][(size_t) (writeIdx & mask)] = r[i];
            ++writeIdx;

            if (state == Passing)
            {
                if (--decisionTimer <= 0)
                {
                    decisionTimer = slice;
                    if (rng.nextDouble() < p)
                    {
                        state = Repeating;
                        repCount = 1 + rng.nextInt (juce::jmax (1, maxRep));
                        repStart = writeIdx - slice;
                        repPos = 0.0;
                        repRate = 1.0;
                    }
                }
            }
            else
            {
                const int idx = (repStart + (int) repPos) & mask;
                l[i] = cap[0][(size_t) idx];
                r[i] = cap[1][(size_t) idx];
                repPos += repRate;
                if (repPos >= (double) slice)
                {
                    repPos = 0.0;
                    if (drop) repRate *= 0.5; // each repeat an octave lower
                    if (--repCount <= 0) { state = Passing; decisionTimer = slice * 2; }
                }
            }
        }
    }

private:
    enum State { Passing, Repeating };
    Param *prob, *sliceMs, *repeats, *stutterPitch;
    std::vector<double> cap[2];
    juce::Random rng;
    State state = Passing;
    int writeIdx = 0, mask = 0, decisionTimer = 0, repCount = 0, repStart = 0;
    double repPos = 0.0, repRate = 1.0, srate = 48000.0;
};

//==============================================================================
// Spectral pad: continuously re-frozen spectrum, blurred into an evolving wash.
class SpectralPedal : public Pedal
{
public:
    SpectralPedal() : Pedal ("spectral", "Spectral Pad", "Ambient", true)
    {
        smear  = addParam ({ "smear", "Smear", "", 0.0, 10.0, 5.0 });
        shimmerP = addParam ({ "shimmer", "Air", "", 0.0, 10.0, 2.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        fz.prepare (sampleRate);
        recaptureCounter = 0;
        shelf.prepare (sampleRate, 0);
        lastShim = -1.0;
    }

    void reset() override { fz.reset(); shelf.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double sm = smear->blockSmoothed (n);
        const double sh = shimmerP->blockSmoothed (n);

        if (std::abs (sh - lastShim) > 0.05)
        {
            lastShim = sh;
            shelf.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighShelf (srate, 3000.0, 0.7,
                juce::Decibels::decibelsToGain (juce::jmap (sh, 0.0, 10.0, -9.0, 6.0))));
        }

        // recapture interval: high smear = rare recapture = long blurred washes
        const int interval = (int) juce::jmap (sm, 0.0, 10.0, srate * 0.08, srate * 1.5);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            if (--recaptureCounter <= 0)
            {
                recaptureCounter = juce::jmax (1024, interval);
                fz.requestCapture();
            }
            double wl, wr;
            fz.processSample (l[i], r[i], wl, wr);
            l[i] = shelf.processSample (0, wl);
            r[i] = shelf.processSample (1, wr);
        }
    }

private:
    Param *smear, *shimmerP;
    dsp::SpectralFreeze fz;
    dsp::StereoBiquad shelf;
    int recaptureCounter = 0;
    double lastShim = -1.0, srate = 48000.0;
};

//==============================================================================
class StereoImagerPedal : public Pedal
{
public:
    StereoImagerPedal() : Pedal ("stereoimager", "Stereo Imager", "Utility")
    {
        widthP = addParam ({ "width", "Width", "%", 0.0, 200.0, 100.0 });
        haas   = addParam ({ "haas", "Haas Delay", "ms", 0.0, 30.0, 0.0 });
        panP   = addParam ({ "pan", "Pan", "", -1.0, 1.0, 0.0 });
        monoBass = addParam ({ "monobass", "Mono Bass", "Hz", 0.0, 500.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        line.prepare (sampleRate, 0.05);
        bassLp.prepare (sampleRate, 0);
        lastMb = -1.0;
    }

    void reset() override { line.reset(); bassLp.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double w = widthP->blockSmoothed (n) / 100.0;
        const double h = haas->blockSmoothed (n) * 0.001 * srate;
        const double pan = panP->blockSmoothed (n);
        const double mb = monoBass->blockSmoothed (n);

        if (std::abs (mb - lastMb) > 1.0 && mb > 1.0)
        {
            lastMb = mb;
            bassLp.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (srate, juce::jmax (30.0, mb), 0.707));
        }

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            line.push (0, l[i]);
            line.push (1, r[i]);
            line.advance();
            double L = l[i];
            double R = h > 1.0 ? line.read (1, h) : r[i];

            double mid = (L + R) * 0.5;
            double side = (L - R) * 0.5 * w;

            if (mb > 1.0)
            {
                const double bass = bassLp.processSample (0, side);
                side -= bass; // keep lows centred
            }

            L = mid + side;
            R = mid - side;
            l[i] = L * (pan > 0.0 ? 1.0 - pan : 1.0);
            r[i] = R * (pan < 0.0 ? 1.0 + pan : 1.0);
        }
    }

private:
    Param *widthP, *haas, *panP, *monoBass;
    dsp::StereoDelay line;
    dsp::StereoBiquad bassLp;
    double lastMb = -1.0, srate = 48000.0;
};

//==============================================================================
class UtilityPedal : public Pedal
{
public:
    UtilityPedal() : Pedal ("utility", "Utility", "Utility")
    {
        gain  = addParam ({ "gain", "Gain", "dB", -60.0, 24.0, 0.0 });
        phaseL = addParam ({ "phasel", "Invert L", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        phaseR = addParam ({ "phaser2", "Invert R", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        monoP = addParam ({ "mono", "Mono Sum", "", 0.0, 1.0, 0.0, 1.0, {}, true });
    }

    void prepare (double, int) override {}

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double g = juce::Decibels::decibelsToGain (gain->blockSmoothed (n));
        const double gl = g * (phaseL->getBool() ? -1.0 : 1.0);
        const double gr = g * (phaseR->getBool() ? -1.0 : 1.0);
        const bool mono = monoP->getBool();

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double L = l[i] * gl, R = r[i] * gr;
            if (mono) { const double m = (L + R) * 0.5; L = R = m; }
            l[i] = L; r[i] = R;
        }
    }

private:
    Param *gain, *phaseL, *phaseR, *monoP;
};

} // namespace vp
