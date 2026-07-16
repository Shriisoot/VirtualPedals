#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
// Shared base for waveshaping pedals: 4x oversampled nonlinearity with pre/post filters.
class DriveBase : public Pedal
{
public:
    using Pedal::Pedal;

protected:
    void prepare (double sampleRate, int maxBlockSize) override
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<double>> (
            2, 2, juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR, true);
        oversampler->initProcessing ((size_t) maxBlockSize);
        preFilter.prepare (sampleRate, maxBlockSize);
        postFilter.prepare (sampleRate, maxBlockSize);
        toneFilter.prepare (sampleRate, maxBlockSize);
        dc.prepare (sampleRate);
    }

    void reset() override
    {
        if (oversampler) oversampler->reset();
        preFilter.reset(); postFilter.reset(); toneFilter.reset(); dc.reset();
    }

    // drive: linear gain, shaperFn: waveshape, level: linear out gain
    template <typename Fn>
    void processShaped (juce::AudioBuffer<double>& buf, double driveGain, double levelGain, Fn&& fn)
    {
        const int n = buf.getNumSamples();
        preFilter.processBuffer (buf);

        juce::dsp::AudioBlock<double> block (buf.getArrayOfWritePointers(), 2, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = fn (d[i] * driveGain);
        }
        oversampler->processSamplesDown (block);

        dc.processBuffer (buf);
        toneFilter.processBuffer (buf);
        postFilter.processBuffer (buf);
        buf.applyGain (levelGain);
    }

    std::unique_ptr<juce::dsp::Oversampling<double>> oversampler;
    dsp::StereoBiquad preFilter, postFilter, toneFilter;
    dsp::DCBlocker dc;
};

//==============================================================================
class BoostPedal : public Pedal
{
public:
    BoostPedal() : Pedal ("boost", "Clean Boost", "Drive")
    {
        gain = addParam ({ "gain", "Boost", "dB", 0.0, 24.0, 6.0 });
        tilt = addParam ({ "tilt", "Tilt EQ", "", -5.0, 5.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        lowShelf.prepare (sampleRate, 0);
        highShelf.prepare (sampleRate, 0);
        srate = sampleRate;
    }

    void reset() override { lowShelf.reset(); highShelf.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double t = tilt->blockSmoothed (n);
        if (std::abs (t - lastTilt) > 0.01)
        {
            lastTilt = t;
            lowShelf.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowShelf (srate, 400.0, 0.6, juce::Decibels::decibelsToGain (-t)));
            highShelf.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighShelf (srate, 1200.0, 0.6, juce::Decibels::decibelsToGain (t)));
        }
        lowShelf.processBuffer (buf);
        highShelf.processBuffer (buf);

        const double g = juce::Decibels::decibelsToGain (gain->blockSmoothed (n));
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = 1.5 * std::tanh (d[i] * g / 1.5); // transparent until pushed hard
        }
    }

private:
    Param *gain, *tilt;
    dsp::StereoBiquad lowShelf, highShelf;
    double lastTilt = 1.0e9, srate = 48000.0;
};

//==============================================================================
class OverdrivePedal : public DriveBase
{
public:
    OverdrivePedal() : DriveBase ("overdrive", "Overdrive", "Drive", true)
    {
        drive = addParam ({ "drive", "Drive", "", 0.0, 10.0, 4.0 });
        tone  = addParam ({ "tone",  "Tone",  "", 0.0, 10.0, 5.5 });
        level = addParam ({ "level", "Level", "dB", -24.0, 12.0, 0.0 });
        voice = addParam ({ "voice", "Voice", "", 0.0, 2.0, 0.0, 1.0, { "Smooth", "Screamer", "Blues" } });
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        DriveBase::prepare (sampleRate, maxBlockSize);
        srate = sampleRate;
        preFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighPass (sampleRate, 90.0, 0.707));
        postFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, 8500.0, 0.707));
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double d  = drive->blockSmoothed (n);
        const double t  = tone->blockSmoothed (n);
        const double lv = juce::Decibels::decibelsToGain (level->blockSmoothed (n));
        const int v = voice->getChoice();

        if (std::abs (t - lastTone) > 0.02)
        {
            lastTone = t;
            const double freq = juce::jmap (t, 0.0, 10.0, 900.0, 9000.0);
            toneFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (srate, freq, 0.6));
        }

        const double g = juce::Decibels::decibelsToGain (juce::jmap (d, 0.0, 10.0, 0.0, 38.0));
        const double comp = 1.0 / juce::jmax (1.0, std::sqrt (g) * 0.55); // rough auto gain staging

        switch (v)
        {
            case 1:  processShaped (buf, g, lv * comp, [] (double x) { return dsp::shape::diode (x); }); break;
            case 2:  processShaped (buf, g, lv * comp, [] (double x) { return dsp::shape::tube (x); }); break;
            default: processShaped (buf, g, lv * comp, [] (double x) { return dsp::shape::softClip (x); }); break;
        }
    }

private:
    Param *drive, *tone, *level, *voice;
    double lastTone = -1.0, srate = 48000.0;
};

//==============================================================================
class DistortionPedal : public DriveBase
{
public:
    DistortionPedal() : DriveBase ("distortion", "Distortion", "Drive", true)
    {
        drive = addParam ({ "drive", "Gain", "", 0.0, 10.0, 5.0 });
        edge  = addParam ({ "edge",  "Edge", "", 0.0, 10.0, 5.0 });
        body  = addParam ({ "body",  "Body", "", 0.0, 10.0, 5.0 });
        level = addParam ({ "level", "Level", "dB", -30.0, 12.0, -6.0 });
        mode  = addParam ({ "modeSel", "Mode", "", 0.0, 2.0, 0.0, 1.0, { "Classic", "Modern", "Metal" } });
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        DriveBase::prepare (sampleRate, maxBlockSize);
        srate = sampleRate;
        scoop.prepare (sampleRate, maxBlockSize);
    }

    void reset() override { DriveBase::reset(); scoop.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double d  = drive->blockSmoothed (n);
        const double e  = edge->blockSmoothed (n);
        const double b  = body->blockSmoothed (n);
        const double lv = juce::Decibels::decibelsToGain (level->blockSmoothed (n));
        const int m = mode->getChoice();

        if (std::abs (e - lastEdge) > 0.02 || std::abs (b - lastBody) > 0.02 || m != lastMode)
        {
            lastEdge = e; lastBody = b; lastMode = m;
            preFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighPass (srate,
                m == 2 ? 140.0 : 70.0, 0.707)); // metal: tighter low end
            toneFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (srate,
                juce::jmap (e, 0.0, 10.0, 2500.0, 11000.0), 0.65));
            // body: mid scoop/boost around 700 Hz
            scoop.setCoefficients (juce::dsp::IIR::Coefficients<double>::makePeakFilter (srate, 700.0, 0.8,
                juce::Decibels::decibelsToGain (juce::jmap (b, 0.0, 10.0, -9.0, 6.0))));
            postFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (srate, 9500.0, 0.707));
        }

        const double gainDb = m == 2 ? 52.0 : m == 1 ? 44.0 : 36.0;
        const double g = juce::Decibels::decibelsToGain (juce::jmap (d, 0.0, 10.0, 6.0, gainDb));
        const double comp = 1.0 / juce::jmax (1.0, std::sqrt (g) * 0.7);

        processShaped (buf, g, lv * comp, [m] (double x)
        {
            return m == 0 ? dsp::shape::softClip (x * 0.8) * 1.1
                          : dsp::shape::hardClip (std::tanh (x * 0.9) * 1.4);
        });
        scoop.processBuffer (buf);
    }

private:
    Param *drive, *edge, *body, *level, *mode;
    dsp::StereoBiquad scoop;
    double lastEdge = -1.0, lastBody = -1.0, srate = 48000.0;
    int lastMode = -1;
};

//==============================================================================
class FuzzPedal : public DriveBase
{
public:
    FuzzPedal() : DriveBase ("fuzz", "Fuzz", "Drive", true)
    {
        fuzzAmt = addParam ({ "fuzz",  "Fuzz",  "", 0.0, 10.0, 6.0 });
        bias    = addParam ({ "bias",  "Bias",  "", 0.0, 10.0, 8.0 });   // low = dying battery sputter
        toneP   = addParam ({ "tone",  "Tone",  "", 0.0, 10.0, 5.0 });
        level   = addParam ({ "level", "Level", "dB", -30.0, 12.0, -4.0 });
        octave  = addParam ({ "octave", "Octave Up", "", 0.0, 1.0, 0.0, 1.0, {}, true });
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        DriveBase::prepare (sampleRate, maxBlockSize);
        srate = sampleRate;
        preFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighPass (sampleRate, 60.0, 0.707));
        postFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, 7500.0, 0.707));
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double fz = fuzzAmt->blockSmoothed (n);
        const double bi = bias->blockSmoothed (n);
        const double t  = toneP->blockSmoothed (n);
        const double lv = juce::Decibels::decibelsToGain (level->blockSmoothed (n));
        const bool oct = octave->getBool();

        if (std::abs (t - lastTone) > 0.02)
        {
            lastTone = t;
            toneFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (srate,
                juce::jmap (t, 0.0, 10.0, 1200.0, 8000.0), 0.6));
        }

        const double g = juce::Decibels::decibelsToGain (juce::jmap (fz, 0.0, 10.0, 10.0, 46.0));
        const double gateBias = juce::jmap (bi, 0.0, 10.0, 0.22, 0.0); // starves the input
        const double comp = 1.0 / juce::jmax (1.0, std::sqrt (g) * 0.6);

        processShaped (buf, g, lv * comp, [gateBias, oct] (double x)
        {
            if (oct) x = std::abs (x) * 2.0 - 0.5;                    // full-wave rectify => octave up
            const double sign = x >= 0.0 ? 1.0 : -1.0;
            const double mag = juce::jmax (0.0, std::abs (x) - gateBias); // sputtery gating
            return dsp::shape::fuzz (sign * mag);
        });
    }

private:
    Param *fuzzAmt, *bias, *toneP, *level, *octave;
    double lastTone = -1.0, srate = 48000.0;
};

} // namespace vp
