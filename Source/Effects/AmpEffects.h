#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class AmpPedal : public Pedal
{
public:
    AmpPedal() : Pedal ("amp", "Amplifier", "Amp")
    {
        model = addParam ({ "model", "Model", "", 0.0, 7.0, 0.0, 1.0,
            { "Clean", "Crunch", "Classic Rock", "Blues", "Metal", "High Gain", "Bass", "Acoustic" } });
        gain     = addParam ({ "gain", "Gain", "", 0.0, 10.0, 4.0 });
        bass     = addParam ({ "bass", "Bass", "", 0.0, 10.0, 5.0 });
        midP     = addParam ({ "mid", "Mid", "", 0.0, 10.0, 5.0 });
        treble   = addParam ({ "treble", "Treble", "", 0.0, 10.0, 5.0 });
        presence = addParam ({ "presence", "Presence", "", 0.0, 10.0, 5.0 });
        master   = addParam ({ "master", "Master", "dB", -30.0, 6.0, -8.0 });
        sagAmt   = addParam ({ "sag", "Sag", "", 0.0, 10.0, 3.0 });
    }

    struct Voicing
    {
        double preGainDb, hpFreq, midFreq, brightDb, shaperMix, stage2;
    };

    static Voicing voicingFor (int m)
    {
        switch (m)
        {
            case 1:  return { 22.0, 80.0, 650.0,  2.0, 0.8, 0.35 };  // Crunch
            case 2:  return { 30.0, 85.0, 750.0,  2.5, 0.9, 0.55 };  // Classic Rock
            case 3:  return { 18.0, 70.0, 520.0,  1.5, 0.7, 0.30 };  // Blues
            case 4:  return { 48.0, 120.0, 800.0, 3.0, 1.0, 0.85 };  // Metal
            case 5:  return { 54.0, 100.0, 700.0, 3.5, 1.0, 0.95 };  // High Gain
            case 6:  return { 14.0, 35.0, 380.0,  0.0, 0.5, 0.20 };  // Bass
            case 7:  return { 4.0,  60.0, 900.0,  1.0, 0.15, 0.0 };  // Acoustic
            default: return { 8.0,  75.0, 600.0,  1.5, 0.35, 0.10 }; // Clean
        }
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        srate = sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<double>> (
            2, 2, juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR, true);
        oversampler->initProcessing ((size_t) maxBlockSize);
        for (auto* f : { &inHp, &bassF, &midF, &trebF, &presF, &postLp })
            f->prepare (sampleRate, maxBlockSize);
        sagEnv.prepare (sampleRate, 4.0, 220.0);
        dc.prepare (sampleRate);
        lastKey = -1.0;
    }

    void reset() override
    {
        if (oversampler) oversampler->reset();
        for (auto* f : { &inHp, &bassF, &midF, &trebF, &presF, &postLp })
            f->reset();
        sagEnv.reset(); dc.reset();
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const int m = model->getChoice();
        const auto v = voicingFor (m);
        const double g   = gain->blockSmoothed (n);
        const double ba  = bass->blockSmoothed (n);
        const double mi  = midP->blockSmoothed (n);
        const double tr  = treble->blockSmoothed (n);
        const double pr  = presence->blockSmoothed (n);
        const double ms  = juce::Decibels::decibelsToGain (master->blockSmoothed (n));
        const double sag = sagAmt->blockSmoothed (n) / 10.0;

        const double key = m * 1000.0 + ba * 1.1 + mi * 3.3 + tr * 7.7 + pr * 13.0;
        if (std::abs (key - lastKey) > 1.0e-6)
        {
            lastKey = key;
            using C = juce::dsp::IIR::Coefficients<double>;
            inHp.setCoefficients   (C::makeHighPass (srate, v.hpFreq, 0.707));
            bassF.setCoefficients  (C::makeLowShelf (srate, 110.0, 0.707, juce::Decibels::decibelsToGain (juce::jmap (ba, 0.0, 10.0, -10.0, 8.0))));
            midF.setCoefficients   (C::makePeakFilter (srate, v.midFreq, 0.8, juce::Decibels::decibelsToGain (juce::jmap (mi, 0.0, 10.0, -10.0, 6.0))));
            trebF.setCoefficients  (C::makeHighShelf (srate, 2400.0, 0.707, juce::Decibels::decibelsToGain (juce::jmap (tr, 0.0, 10.0, -10.0, 8.0) + v.brightDb)));
            presF.setCoefficients  (C::makeHighShelf (srate, 4800.0, 0.8, juce::Decibels::decibelsToGain (juce::jmap (pr, 0.0, 10.0, -6.0, 7.0))));
            postLp.setCoefficients (C::makeLowPass (srate, 9500.0, 0.707));
        }

        inHp.processBuffer (buf);

        const double pre = juce::Decibels::decibelsToGain (v.preGainDb * (0.25 + 0.75 * g / 10.0));
        const double mix = v.shaperMix;
        const double stage2 = v.stage2;

        // power-supply sag: momentary gain dip driven by signal energy
        double sagGain = 1.0;
        {
            double e = 0.0;
            auto* d0 = buf.getReadPointer (0);
            for (int i = 0; i < n; ++i) e = sagEnv.processSample (d0[i]);
            sagGain = 1.0 / (1.0 + e * sag * 2.4);
        }

        juce::dsp::AudioBlock<double> block (buf.getArrayOfWritePointers(), 2, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                double x = d[i] * pre * sagGain;
                double y = dsp::shape::tube (x);                    // stage 1: triode-ish
                if (stage2 > 0.0)
                    y = y * (1.0 - stage2) + dsp::shape::hardClip (std::tanh (y * 2.2) * 1.3) * stage2; // stage 2
                d[i] = d[i] * (1.0 - mix) + y * mix;
            }
        }
        oversampler->processSamplesDown (block);

        dc.processBuffer (buf);
        bassF.processBuffer (buf);
        midF.processBuffer (buf);
        trebF.processBuffer (buf);
        presF.processBuffer (buf);
        postLp.processBuffer (buf);

        const double comp = 1.0 / juce::jmax (1.0, std::sqrt (pre) * 0.5); // gain staging
        buf.applyGain (ms * comp);
    }

private:
    Param *model, *gain, *bass, *midP, *treble, *presence, *master, *sagAmt;
    std::unique_ptr<juce::dsp::Oversampling<double>> oversampler;
    dsp::StereoBiquad inHp, bassF, midF, trebF, presF, postLp;
    dsp::EnvFollower sagEnv;
    dsp::DCBlocker dc;
    double lastKey = -1.0, srate = 48000.0;
};

//==============================================================================
// Cabinet: IIR speaker voicings for six classic cab types + user IR loader
// (convolution) + mic position/distance controls + blend of both paths.
class CabPedal : public Pedal
{
public:
    CabPedal() : Pedal ("cab", "Cabinet", "Amp")
    {
        model = addParam ({ "model", "Cabinet", "", 0.0, 5.0, 1.0, 1.0,
            { "1x12 American", "2x12 British", "4x12 Vintage", "4x10 Bass", "1x12 Boutique", "Acoustic DI" } });
        micPos   = addParam ({ "mic", "Mic Position", "", 0.0, 10.0, 3.0 });   // 0 = center/bright, 10 = edge/dark
        distance = addParam ({ "dist", "Distance", "", 0.0, 10.0, 2.0 });
        irBlend  = addParam ({ "irblend", "IR Blend", "%", 0.0, 100.0, 0.0 }); // voicing vs loaded IR
        levelP   = addParam ({ "level", "Level", "dB", -12.0, 12.0, 0.0 });
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        srate = sampleRate;
        maxBlockSz = maxBlockSize;
        for (auto* f : { &hp, &bump, &mids, &edge2, &lp, &micTilt })
            f->prepare (sampleRate, maxBlockSize);
        roomLine.prepare (sampleRate, 0.03);
        convReady.store (loadPendingIr());
        floatScratch.setSize (2, maxBlockSize);
        lastKey = -1.0;
    }

    void reset() override
    {
        for (auto* f : { &hp, &bump, &mids, &edge2, &lp, &micTilt })
            f->reset();
        roomLine.reset();
        if (conv) conv->reset();
    }

    // Message thread: request an IR file. Takes effect at next prepare or immediately if possible.
    void setIrFile (const juce::File& f)
    {
        irFile = f;
        convReady.store (loadPendingIr());
    }

    juce::File getIrFile() const { return irFile; }

    juce::var toVar() const override
    {
        auto v = Pedal::toVar();
        if (auto* obj = v.getDynamicObject())
            obj->setProperty ("irPath", irFile.getFullPathName());
        return v;
    }

    void fromVar (const juce::var& v) override
    {
        Pedal::fromVar (v);
        const auto path = v.getProperty ("irPath", "").toString();
        if (path.isNotEmpty() && juce::File (path).existsAsFile())
            setIrFile (juce::File (path));
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const int m = model->getChoice();
        const double mic = micPos->blockSmoothed (n);
        const double dist = distance->blockSmoothed (n);
        const double blend = irBlend->blockSmoothed (n) / 100.0;
        const double lvl = juce::Decibels::decibelsToGain (levelP->blockSmoothed (n));

        const double key = m * 100.0 + mic * 1.3 + dist * 3.7;
        if (std::abs (key - lastKey) > 1.0e-6)
        {
            lastKey = key;
            rebuildVoicing (m, mic, dist);
        }

        // IIR voicing path
        voiced.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            voiced.copyFrom (ch, 0, buf, ch, 0, n);
        hp.processBuffer (voiced);
        bump.processBuffer (voiced);
        mids.processBuffer (voiced);
        edge2.processBuffer (voiced);
        micTilt.processBuffer (voiced);
        lp.processBuffer (voiced);

        // distance: a couple of ms of pre-delay + soft top loss, mixed subtly
        if (dist > 0.2)
        {
            for (int i = 0; i < n; ++i)
            {
                roomLine.push (0, voiced.getSample (0, i));
                roomLine.push (1, voiced.getSample (1, i));
                roomLine.advance();
                const double dl = roomLine.read (0, 1.0 + dist * 0.0018 * srate);
                const double dr = roomLine.read (1, 1.0 + dist * 0.0021 * srate);
                const double a = dist / 10.0 * 0.35;
                voiced.setSample (0, i, voiced.getSample (0, i) * (1.0 - a) + dl * a);
                voiced.setSample (1, i, voiced.getSample (1, i) * (1.0 - a) + dr * a);
            }
        }

        // optional convolution with user IR (float engine, blended in)
        const bool useConv = convReady.load() && blend > 0.001 && conv != nullptr;
        if (useConv)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* dst = floatScratch.getWritePointer (ch);
                auto* src = buf.getReadPointer (ch);
                for (int i = 0; i < n; ++i) dst[i] = (float) src[i];
            }
            juce::dsp::AudioBlock<float> fb (floatScratch.getArrayOfWritePointers(), 2, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctx (fb);
            conv->process (ctx);
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            auto* vsrc = voiced.getReadPointer (ch);
            auto* csrc = useConv ? floatScratch.getReadPointer (ch) : nullptr;
            for (int i = 0; i < n; ++i)
            {
                const double c = csrc != nullptr ? (double) csrc[i] : vsrc[i];
                d[i] = (vsrc[i] * (1.0 - blend) + c * blend) * lvl;
            }
        }
    }

private:
    void rebuildVoicing (int m, double mic, double dist)
    {
        using C = juce::dsp::IIR::Coefficients<double>;
        struct V { double hpF, bumpF, bumpDb, midF, midDb, edgeF, edgeDb, lpF; };
        static const V vs[6] = {
            { 75.0,  95.0, 3.0, 1300.0, -2.0, 3400.0, 3.5, 5200.0 },  // 1x12 US
            { 70.0, 110.0, 3.5,  900.0, -3.0, 2600.0, 4.0, 4800.0 },  // 2x12 UK
            { 78.0, 120.0, 4.5,  800.0, -4.5, 2200.0, 5.0, 4300.0 },  // 4x12 V30
            { 40.0,  80.0, 4.0,  700.0, -2.0, 1800.0, 2.0, 3800.0 },  // 4x10 bass
            { 72.0, 100.0, 2.5, 1500.0, -1.5, 3800.0, 3.0, 5600.0 },  // boutique
            { 60.0,  90.0, 0.5, 1100.0,  0.0, 4500.0, 1.0, 12000.0 } }; // acoustic DI
        const auto& v = vs[juce::jlimit (0, 5, m)];

        hp.setCoefficients   (C::makeHighPass (srate, v.hpF, 0.707));
        bump.setCoefficients (C::makePeakFilter (srate, v.bumpF, 0.9, juce::Decibels::decibelsToGain (v.bumpDb)));
        mids.setCoefficients (C::makePeakFilter (srate, v.midF, 0.7, juce::Decibels::decibelsToGain (v.midDb)));
        edge2.setCoefficients (C::makePeakFilter (srate, v.edgeF, 1.1, juce::Decibels::decibelsToGain (v.edgeDb)));
        // mic position: on-axis bright <-> off-axis dark
        micTilt.setCoefficients (C::makeHighShelf (srate, 2800.0, 0.7,
            juce::Decibels::decibelsToGain (juce::jmap (mic, 0.0, 10.0, 2.5, -7.0))));
        const double lpDrop = juce::jmap (dist, 0.0, 10.0, 1.0, 0.72);
        lp.setCoefficients (C::makeLowPass (srate, v.lpF * lpDrop, 0.75));
    }

    bool loadPendingIr()
    {
        if (! irFile.existsAsFile())
            return false;
        conv = std::make_unique<juce::dsp::Convolution>();
        juce::dsp::ProcessSpec spec { srate, (juce::uint32) juce::jmax (16, maxBlockSz), 2 };
        conv->prepare (spec);
        conv->loadImpulseResponse (irFile,
                                   juce::dsp::Convolution::Stereo::yes,
                                   juce::dsp::Convolution::Trim::yes,
                                   0); // full length
        return true;
    }

    Param *model, *micPos, *distance, *irBlend, *levelP;
    dsp::StereoBiquad hp, bump, mids, edge2, lp, micTilt;
    dsp::StereoDelay roomLine;
    std::unique_ptr<juce::dsp::Convolution> conv;
    juce::AudioBuffer<double> voiced;
    juce::AudioBuffer<float> floatScratch;
    juce::File irFile;
    std::atomic<bool> convReady { false };
    double lastKey = -1.0, srate = 48000.0;
    int maxBlockSz = 512;
};

} // namespace vp
