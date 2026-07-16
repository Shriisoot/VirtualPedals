#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class PitchShiftPedal : public Pedal
{
public:
    PitchShiftPedal() : Pedal ("pitchshift", "Pitch Shift", "Pitch", true)
    {
        semis = addParam ({ "semis", "Pitch", "st", -24.0, 24.0, 0.0 });
        fine  = addParam ({ "fine", "Fine", "ct", -100.0, 100.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        shifterL.prepare (sampleRate);
        shifterR.prepare (sampleRate);
    }

    void reset() override { shifterL.reset(); shifterR.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double st = std::round (semis->blockSmoothed (n)) + fine->blockSmoothed (n) / 100.0;
        shifterL.setSemitones (st);
        shifterR.setSemitones (st);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            l[i] = shifterL.processMono (l[i]);
            r[i] = shifterR.processMono (r[i]);
        }
    }

private:
    Param *semis, *fine;
    dsp::PitchShifter shifterL, shifterR;
};

//==============================================================================
class OctaverPedal : public Pedal
{
public:
    OctaverPedal() : Pedal ("octaver", "Octaver", "Pitch")
    {
        dry  = addParam ({ "dry", "Dry", "%", 0.0, 100.0, 100.0 });
        sub1 = addParam ({ "sub1", "-1 Oct", "%", 0.0, 100.0, 50.0 });
        sub2 = addParam ({ "sub2", "-2 Oct", "%", 0.0, 100.0, 0.0 });
        up1  = addParam ({ "up1", "+1 Oct", "%", 0.0, 100.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        for (auto* s : { &shSub1, &shSub2, &shUp1 })
            s->prepare (sampleRate);
        shSub1.setSemitones (-12.0);
        shSub2.setSemitones (-24.0);
        shUp1.setSemitones (12.0);
        lp.prepare (sampleRate, 0);
        lp.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, 2200.0, 0.707));
    }

    void reset() override { shSub1.reset(); shSub2.reset(); shUp1.reset(); lp.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double gDry = dry->blockSmoothed (n) / 100.0;
        const double g1 = sub1->blockSmoothed (n) / 100.0;
        const double g2 = sub2->blockSmoothed (n) / 100.0;
        const double gu = up1->blockSmoothed (n) / 100.0;

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double mono = (l[i] + r[i]) * 0.5;
            const double smoothed = lp.processSample (0, mono); // sub tracks better from LP'd input
            const double s1 = shSub1.processMono (smoothed);
            const double s2 = shSub2.processMono (smoothed);
            const double u1 = shUp1.processMono (mono);
            const double subs = s1 * g1 + s2 * g2 + u1 * gu;
            l[i] = l[i] * gDry + subs;
            r[i] = r[i] * gDry + subs;
        }
    }

private:
    Param *dry, *sub1, *sub2, *up1;
    dsp::PitchShifter shSub1, shSub2, shUp1;
    dsp::StereoBiquad lp;
};

//==============================================================================
class HarmonyPedal : public Pedal
{
public:
    HarmonyPedal() : Pedal ("harmony", "Harmony", "Pitch", true)
    {
        interval1 = addParam ({ "int1", "Voice 1", "", 0.0, 8.0, 4.0, 1.0,
                                { "-Oct", "-5th", "-4th", "-3rd", "Unison", "+3rd", "+4th", "+5th", "+Oct" } });
        interval2 = addParam ({ "int2", "Voice 2", "", 0.0, 8.0, 4.0, 1.0,
                                { "-Oct", "-5th", "-4th", "-3rd", "Unison", "+3rd", "+4th", "+5th", "+Oct" } });
        minorMode = addParam ({ "minor", "Minor 3rds", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        detune    = addParam ({ "detune", "Detune", "ct", 0.0, 30.0, 6.0 });
        levelP    = addParam ({ "vlevel", "Voices", "%", 0.0, 100.0, 60.0 });
    }

    void prepare (double sampleRate, int) override
    {
        v1.prepare (sampleRate);
        v2.prepare (sampleRate);
    }

    void reset() override { v1.reset(); v2.reset(); }

    static double intervalToSemis (int idx, bool minor)
    {
        switch (idx)
        {
            case 0: return -12.0;
            case 1: return -7.0;                // perfect 5th down
            case 2: return -5.0;                // perfect 4th down
            case 3: return minor ? -9.0 : -8.0; // down a 3rd
            case 4: return 0.0;
            case 5: return minor ? 3.0 : 4.0;   // up a 3rd
            case 6: return 5.0;
            case 7: return 7.0;
            case 8: return 12.0;
            default: return 0.0;
        }
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const bool minor = minorMode->getBool();
        const double det = detune->blockSmoothed (n) / 100.0;
        const double lvl = levelP->blockSmoothed (n) / 100.0;
        v1.setSemitones (intervalToSemis (interval1->getChoice(), minor) + det * 0.01);
        v2.setSemitones (intervalToSemis (interval2->getChoice(), minor) - det * 0.01);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double mono = (l[i] + r[i]) * 0.5;
            const double h1 = v1.processMono (mono);
            const double h2 = v2.processMono (mono);
            l[i] = l[i] + (h1 * 0.8 + h2 * 0.4) * lvl; // voice 1 leans left
            r[i] = r[i] + (h2 * 0.8 + h1 * 0.4) * lvl; // voice 2 leans right
        }
    }

private:
    Param *interval1, *interval2, *minorMode, *detune, *levelP;
    dsp::PitchShifter v1, v2;
};

//==============================================================================
// Expression-pedal-friendly pitch bender (classic whammy behaviour).
class WhammyPedal : public Pedal
{
public:
    WhammyPedal() : Pedal ("whammy", "Whammy", "Pitch")
    {
        pedalPos = addParam ({ "pedal", "Pedal", "%", 0.0, 100.0, 0.0 });
        rangeSel = addParam ({ "range", "Range", "", 0.0, 4.0, 1.0, 1.0,
                               { "+1 Oct", "+2 Oct", "-1 Oct", "-2 Oct", "Dive Bomb" } });
        blend    = addParam ({ "blend", "Blend", "%", 0.0, 100.0, 100.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        shL.prepare (sampleRate);
        shR.prepare (sampleRate);
        posSm.prepare (sampleRate, 25.0);
    }

    void reset() override { shL.reset(); shR.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        posSm.setTarget (pedalPos->get() / 100.0);
        const double bl = blend->blockSmoothed (n) / 100.0;
        const int range = rangeSel->getChoice();

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double p = posSm.next();
            double st = 0.0;
            switch (range)
            {
                case 0: st = 12.0 * p; break;
                case 1: st = 24.0 * p; break;
                case 2: st = -12.0 * p; break;
                case 3: st = -24.0 * p; break;
                case 4: st = -36.0 * p; break;
                default: break;
            }
            shL.setSemitones (st);
            shR.setSemitones (st);
            const double wl = shL.processMono (l[i]);
            const double wr = shR.processMono (r[i]);
            l[i] = wl * bl + l[i] * (1.0 - bl);
            r[i] = wr * bl + r[i] * (1.0 - bl);
        }
    }

private:
    Param *pedalPos, *rangeSel, *blend;
    dsp::PitchShifter shL, shR;
    Smoother posSm;
    double srate = 48000.0;
};

} // namespace vp
