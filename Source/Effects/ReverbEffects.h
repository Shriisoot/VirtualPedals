#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class ReverbPedal : public Pedal
{
public:
    ReverbPedal() : Pedal ("reverb", "Reverb", "Reverb", true)
    {
        size    = addParam ({ "size", "Size", "", 0.0, 10.0, 5.0 });
        damp    = addParam ({ "damp", "Damping", "", 0.0, 10.0, 5.0 });
        preDly  = addParam ({ "predelay", "Pre-Delay", "ms", 0.0, 200.0, 20.0 });
        widthP  = addParam ({ "width", "Width", "%", 0.0, 100.0, 100.0 });
        shimmer = addParam ({ "shimmer", "Shimmer", "%", 0.0, 100.0, 0.0 });
        modeSel = addParam ({ "modeSel", "Type", "", 0.0, 4.0, 1.0, 1.0, { "Room", "Hall", "Plate", "Ambient", "Shimmer" } });
        freeze  = addParam ({ "freeze", "Freeze", "", 0.0, 1.0, 0.0, 1.0, {}, true });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        rev.prepare (sampleRate);
        shifter.prepare (sampleRate);
        shifter.setSemitones (12.0);
        shimLp.prepare (sampleRate, 0);
        shimLp.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, 6000.0, 0.707));
    }

    void reset() override { rev.reset(); shifter.reset(); shimLp.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const int mode = modeSel->getChoice();
        double room = size->blockSmoothed (n) / 10.0;
        double dmp  = damp->blockSmoothed (n) / 10.0;
        double pre  = preDly->blockSmoothed (n);
        const double width = widthP->blockSmoothed (n) / 100.0;
        double shim = shimmer->blockSmoothed (n) / 100.0;
        const bool frozen = freeze->getBool();

        switch (mode)
        {
            case 0:  room *= 0.55; break;                       // Room
            case 1:  room = 0.6 + room * 0.38; break;           // Hall
            case 2:  room = 0.5 + room * 0.4; dmp *= 0.35; break; // Plate: bright + dense
            case 3:  room = 0.75 + room * 0.24; pre += 30.0; break; // Ambient
            case 4:  room = 0.7 + room * 0.29; shim = juce::jmax (shim, 0.45); break; // Shimmer
            default: break;
        }
        if (frozen) { room = 1.0; dmp = 0.0; }

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double inL = frozen ? 0.0 : l[i];
            double inR = frozen ? 0.0 : r[i];

            if (shim > 0.001)
            {
                // pitch-shifted (+12) tail regeneration
                const double s = shimLp.processSample (0, shifter.processSample (0, (lastWetL + lastWetR) * 0.5));
                shifter.processSample (1, (lastWetL + lastWetR) * 0.5);
                inL += s * shim * 0.6;
                inR += s * shim * 0.6;
            }

            double wl, wr;
            rev.processSample (inL, inR, room, dmp, pre, wl, wr);
            lastWetL = wl; lastWetR = wr;

            const double mid  = (wl + wr) * 0.5;
            const double side = (wl - wr) * 0.5 * width;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

private:
    Param *size, *damp, *preDly, *widthP, *shimmer, *modeSel, *freeze;
    dsp::Reverb rev;
    dsp::PitchShifter shifter;
    dsp::StereoBiquad shimLp;
    double lastWetL = 0.0, lastWetR = 0.0, srate = 48000.0;
};

//==============================================================================
class FreezePedal : public Pedal
{
public:
    FreezePedal() : Pedal ("freezepedal", "Freeze", "Ambient", true)
    {
        engage = addParam ({ "engage", "Freeze", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        glow   = addParam ({ "glow", "Glow", "", 0.0, 10.0, 3.0 });      // soft high shelf on the pad
        swell  = addParam ({ "swell", "Swell", "ms", 5.0, 2000.0, 250.0, 0.5 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        fz.prepare (sampleRate);
        padGain.prepare (sampleRate, 120.0);
        padGain.setTarget (0.0);
        padGain.snap();
        shelf.prepare (sampleRate, 0);
        lastGlow = -1.0;
    }

    void reset() override { fz.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const bool on = engage->getBool();
        const double g = glow->blockSmoothed (n);

        if (on && ! wasOn) fz.requestCapture();
        if (! on && wasOn) fz.unfreeze();
        wasOn = on;

        const double sw = juce::jmax (5.0, swell->get());
        if (std::abs (sw - lastSwell) > 0.5)
        {
            lastSwell = sw;
            padGain.setTime (srate, sw);
        }
        padGain.setTarget (on ? 1.0 : 0.0);

        if (std::abs (g - lastGlow) > 0.02)
        {
            lastGlow = g;
            shelf.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighShelf (srate, 2500.0, 0.7,
                juce::Decibels::decibelsToGain (juce::jmap (g, 0.0, 10.0, -6.0, 6.0))));
        }

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double padL, padR;
            fz.processSample (l[i], r[i], padL, padR);
            const double pg = padGain.next();
            padL = shelf.processSample (0, padL) * pg;
            padR = shelf.processSample (1, padR) * pg;
            l[i] += padL;
            r[i] += padR;
        }
    }

private:
    Param *engage, *glow, *swell;
    dsp::SpectralFreeze fz;
    dsp::StereoBiquad shelf;
    Smoother padGain;
    double lastGlow = -1.0, lastSwell = -1.0, srate = 48000.0;
    bool wasOn = false;
};

} // namespace vp
