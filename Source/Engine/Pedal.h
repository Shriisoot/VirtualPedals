#pragma once
#include "Params.h"

namespace vp
{

// Base class for every effect. Lightweight (not an AudioProcessor): double-precision
// stereo processing, built-in silent-bypass crossfade and optional wet/dry mix.
class Pedal
{
public:
    Pedal (juce::String typeIdIn, juce::String nameIn, juce::String categoryIn, bool withMix = false)
        : typeId (std::move (typeIdIn)), displayName (std::move (nameIn)), category (std::move (categoryIn))
    {
        if (withMix)
            mixParam = addParam ({ "mix", "Mix", "%", 0.0, 100.0, 100.0 });
    }

    virtual ~Pedal() = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        maxBlock = maxBlockSize;
        dryBuf.setSize (2, maxBlockSize, false, false, true);
        gainScratch.allocate ((size_t) maxBlockSize, true);
        bypassGain.prepare (sampleRate, 12.0);
        bypassGain.setTarget (bypassed.load() ? 0.0 : 1.0);
        bypassGain.snap();
        for (auto* p : parameters)
            p->prepare (sampleRate);
        prepare (sampleRate, maxBlockSize);
        reset();
    }

    // Called from the audio thread. Handles bypass crossfade + wet/dry around process().
    void processBlock (juce::AudioBuffer<double>& buf)
    {
        const int n = buf.getNumSamples();
        const double bypassTarget = bypassed.load (std::memory_order_relaxed) ? 0.0 : 1.0;
        bypassGain.setTarget (bypassTarget);

        const bool fullyBypassed = bypassTarget == 0.0 && bypassGain.current() < 1.0e-4;
        if (fullyBypassed)
        {
            bypassGain.snap();
            return; // true silent bypass: zero processing cost
        }

        for (int ch = 0; ch < 2; ++ch)
            dryBuf.copyFrom (ch, 0, buf, ch, 0, n);

        process (buf);

        double mixTargetPct = mixParam != nullptr ? mixParam->blockSmoothed (n) : 100.0;
        const double wetMix = juce::jlimit (0.0, 1.0, mixTargetPct / 100.0);

        for (int i = 0; i < n; ++i)
            gainScratch[i] = bypassGain.next();

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* wet = buf.getWritePointer (ch);
            auto* dry = dryBuf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const double bg = gainScratch[i];
                const double mixed = wet[i] * wetMix + dry[i] * (1.0 - wetMix);
                wet[i] = mixed * bg + dry[i] * (1.0 - bg);
            }
        }
    }

    virtual void reset() {}

    //==============================================================================
    Param* addParam (ParamDef d)
    {
        auto* p = new Param (std::move (d));
        parameters.add (p);
        return p;
    }

    Param* param (const juce::String& id) const
    {
        for (auto* p : parameters)
            if (p->def.id == id)
                return p;
        return nullptr;
    }

    const juce::OwnedArray<Param>& getParams() const { return parameters; }

    //==============================================================================
    void setBypassed (bool b)        { bypassed.store (b); }
    bool isBypassed() const          { return bypassed.load(); }

    virtual int getLatencySamples() const { return 0; }

    //==============================================================================
    virtual juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("type", typeId);
        obj->setProperty ("bypassed", isBypassed());
        auto* ps = new juce::DynamicObject();
        for (auto* p : parameters)
            ps->setProperty (juce::Identifier (p->def.id), p->get());
        obj->setProperty ("params", juce::var (ps));
        return juce::var (obj);
    }

    virtual void fromVar (const juce::var& v)
    {
        setBypassed ((bool) v.getProperty ("bypassed", false));
        if (auto* ps = v.getProperty ("params", juce::var()).getDynamicObject())
            for (auto& kv : ps->getProperties())
                if (auto* p = param (kv.name.toString()))
                    p->set ((double) kv.value);
    }

    const juce::String typeId, displayName, category;

protected:
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void process (juce::AudioBuffer<double>& buf) = 0;

    double sr = 48000.0;
    int maxBlock = 512;
    Param* mixParam = nullptr;

private:
    juce::OwnedArray<Param> parameters;
    juce::AudioBuffer<double> dryBuf;
    juce::HeapBlock<double> gainScratch;
    std::atomic<bool> bypassed { false };
    Smoother bypassGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pedal)
};

} // namespace vp
