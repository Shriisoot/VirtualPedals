#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
// The Sustain Engine: nine sustain strategies with hard safeguards against
// runaway output (dedicated limiter + feedback-energy watchdog).
class SustainPedal : public Pedal
{
public:
    SustainPedal() : Pedal ("sustain", "Sustain Engine", "Sustain", true)
    {
        mode = addParam ({ "modeSel", "Mode", "", 0.0, 8.0, 0.0, 1.0,
            { "Natural", "Freeze", "Infinite Hold", "Controlled Feedback", "Artificial Feedback",
              "Drone", "Ambient", "Harmonic", "String Resonance" } });
        amount   = addParam ({ "amount", "Sustain", "", 0.0, 10.0, 6.0 });
        swell    = addParam ({ "swell", "Swell", "ms", 10.0, 3000.0, 400.0, 0.5 });
        bright   = addParam ({ "bright", "Brightness", "", 0.0, 10.0, 5.0 });
        levelP   = addParam ({ "level", "Level", "dB", -24.0, 6.0, -3.0 });
        trigger  = addParam ({ "trigger", "Hold", "", 0.0, 1.0, 0.0, 1.0, {}, true });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        comp.prepare (sampleRate, 2.0, 350.0);
        noteDetect.prepare (sampleRate, 1.5, 60.0);
        freeze.prepare (sampleRate);
        shifter.prepare (sampleRate);
        shifter.setSemitones (12.0);
        fifth.prepare (sampleRate);
        fifth.setSemitones (7.0);
        subShift.prepare (sampleRate);
        subShift.setSemitones (-12.0);
        fbLine.prepare (sampleRate, 0.1);
        toneShelf.prepare (sampleRate, 0);
        guard.prepare (sampleRate);
        guard.ceiling = 0.95;
        padGain.prepare (sampleRate, 400.0);
        padGain.setTarget (0.0);
        padGain.snap();
        for (auto& c : combState) std::fill (c.begin(), c.end(), 0.0);
        for (auto& c : combState) c.assign ((size_t) (sampleRate * 0.02) + 8, 0.0);
        combIdx.fill (0);
        lastBright = -1.0; wasHeld = false; lastEnv = 0.0;
    }

    void reset() override
    {
        freeze.reset(); shifter.reset(); fifth.reset(); subShift.reset(); fbLine.reset();
        toneShelf.reset(); guard.reset();
        for (auto& c : combState) std::fill (c.begin(), c.end(), 0.0);
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const int m = mode->getChoice();
        const double amt = amount->blockSmoothed (n) / 10.0;
        const double sw = juce::jmax (10.0, swell->get());
        const double br = bright->blockSmoothed (n);
        const double lvl = juce::Decibels::decibelsToGain (levelP->blockSmoothed (n));
        const bool held = trigger->getBool();

        if (std::abs (br - lastBright) > 0.05)
        {
            lastBright = br;
            toneShelf.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighShelf (srate, 2200.0, 0.7,
                juce::Decibels::decibelsToGain (juce::jmap (br, 0.0, 10.0, -9.0, 6.0))));
            padGain.setTime (srate, sw);
        }

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);

        switch (m)
        {
            case 0: processNatural (l, r, n, amt); break;
            case 1: case 2: processFreeze (l, r, n, amt, m == 2, held); break;
            case 3: processFeedback (l, r, n, amt, false); break;
            case 4: processFeedback (l, r, n, amt, true); break;
            case 5: processDrone (l, r, n, amt, held); break;
            case 6: processAmbient (l, r, n, amt, held); break;
            case 7: processHarmonic (l, r, n, amt, held); break;
            case 8: processResonance (l, r, n, amt); break;
            default: break;
        }

        toneShelf.processBuffer (buf);
        buf.applyGain (lvl);
        guard.processBuffer (buf); // absolute safeguard against runaway levels
    }

private:
    //==========================================================================
    void processNatural (double* l, double* r, int n, double amt)
    {
        // heavy optical-style compression with slow release + gentle harmonic push
        const double ratioInv = 1.0 - amt * 0.85;   // amt 1 => ~7:1
        const double thresh = 0.08;
        for (int i = 0; i < n; ++i)
        {
            const double e = comp.processSample ((l[i] + r[i]) * 0.5);
            double g = 1.0;
            if (e > thresh)
                g = std::pow (e / thresh, ratioInv - 1.0);
            g = juce::jmin (g * (1.0 + amt * 2.2), 12.0); // makeup, clamped
            l[i] = std::tanh (l[i] * g);
            r[i] = std::tanh (r[i] * g);
        }
    }

    // Sustainiac-style hold: a new note (or pinch harmonic) arms a short settle
    // window, then freezes and holds INDEFINITELY. Touching/muting the strings
    // (sharp level drop vs. the slow envelope) releases it. Natural string decay
    // does NOT release — the pad keeps ringing, like feedback in front of an amp.
    void processFreeze (double* l, double* r, int n, double amt, bool infinite, bool held)
    {
        for (int i = 0; i < n; ++i)
        {
            const double e = noteDetect.processSample ((l[i] + r[i]) * 0.5);

            if (infinite)
            {
                slowEnv = 0.99995 * slowEnv + 0.00005 * e;   // ~0.4 s reference envelope

                switch (susState)
                {
                    case Idle:
                        if (e > 0.02 && e > lastEnv * 1.8)
                        {
                            susState = Arming;
                            armCounter = (int) (0.05 * srate);  // let the attack settle into harmonics
                        }
                        break;

                    case Arming:
                        if (--armCounter <= 0)
                        {
                            freeze.requestCapture();
                            susState = Holding;
                            quietCounter = 0;
                        }
                        break;

                    case Holding:
                        // clearly louder new attack replaces the held note
                        if (e > 0.03 && e > lastEnv * 3.0)
                            freeze.requestCapture();

                        // string touch = level collapses far below the slow envelope
                        if ((e < slowEnv * 0.12 && e < 0.01) || e < 0.0012)
                        {
                            if (++quietCounter > (int) (0.12 * srate))
                            {
                                freeze.unfreeze();
                                susState = Idle;
                                quietCounter = 0;
                            }
                        }
                        else
                            quietCounter = 0;
                        break;
                }
                lastEnv = 0.9995 * lastEnv + 0.0005 * e;
            }
            else
            {
                if (held && ! wasHeld) freeze.requestCapture();
                if (! held && wasHeld) freeze.unfreeze();
                wasHeld = held;
            }

            padGain.setTarget ((infinite ? susState == Holding : held) && freeze.isFrozen() ? amt : 0.0);
            double wl, wr;
            freeze.processSample (l[i], r[i], wl, wr);
            const double pg = padGain.next();
            l[i] += wl * pg;
            r[i] += wr * pg;
        }
    }

    void processFeedback (double* l, double* r, int n, double amt, bool artificial)
    {
        // regenerative feedback loop; artificial mode shifts +12 like amp feedback squeal
        const double fbAmt = amt * 0.72; // watchdog-safe range
        for (int i = 0; i < n; ++i)
        {
            const double e = comp.processSample ((l[i] + r[i]) * 0.5);
            double fb = fbLine.read (0, 0.03 * srate);
            if (artificial)
                fb = shifter.processMono (fb);
            fb = std::tanh (fb * 1.4) * fbAmt * juce::jmin (1.0, e * 14.0); // only feeds back while a note rings

            fbLine.push (0, (l[i] + r[i]) * 0.5 + fb * 0.9);
            fbLine.push (1, 0.0);
            fbLine.advance();

            l[i] += fb;
            r[i] += fb;
        }
    }

    void processDrone (double* l, double* r, int n, double amt, bool held)
    {
        for (int i = 0; i < n; ++i)
        {
            if (held && ! wasHeld) freeze.requestCapture();
            if (! held && wasHeld) freeze.unfreeze();
            wasHeld = held;

            double wl, wr;
            freeze.processSample (l[i], r[i], wl, wr);
            const double sub = subShift.processMono ((wl + wr) * 0.5);
            padGain.setTarget (held && freeze.isFrozen() ? amt : 0.0);
            const double pg = padGain.next();
            l[i] += (wl * 0.6 + sub * 0.9) * pg;
            r[i] += (wr * 0.6 + sub * 0.9) * pg;
        }
    }

    void processAmbient (double* l, double* r, int n, double amt, bool held)
    {
        juce::ignoreUnused (held);
        for (int i = 0; i < n; ++i)
        {
            // auto-freeze softly on sustained notes, swell in slowly: an ambient bed
            const double e = noteDetect.processSample ((l[i] + r[i]) * 0.5);
            if (e > 0.03 && ++holdCounter > (int) (srate * 0.25))
            {
                holdCounter = 0;
                freeze.requestCapture();
            }
            double wl, wr;
            freeze.processSample (l[i], r[i], wl, wr);
            padGain.setTarget (freeze.isFrozen() ? amt * 0.8 : 0.0);
            const double pg = padGain.next();
            l[i] += wl * pg;
            r[i] += wr * pg;
        }
    }

    void processHarmonic (double* l, double* r, int n, double amt, bool held)
    {
        for (int i = 0; i < n; ++i)
        {
            if (held && ! wasHeld) freeze.requestCapture();
            if (! held && wasHeld) freeze.unfreeze();
            wasHeld = held;

            double wl, wr;
            freeze.processSample (l[i], r[i], wl, wr);
            const double mono = (wl + wr) * 0.5;
            const double oct = shifter.processMono (mono);
            const double fth = fifth.processMono (mono);
            padGain.setTarget (held && freeze.isFrozen() ? amt : 0.0);
            const double pg = padGain.next();
            l[i] += (wl * 0.5 + oct * 0.55 + fth * 0.4) * pg;
            r[i] += (wr * 0.5 + oct * 0.55 + fth * 0.35) * pg;
        }
    }

    void processResonance (double* l, double* r, int n, double amt)
    {
        // sympathetic-string comb resonators tuned to open-string frequencies
        static const double freqs[4] = { 82.41, 110.0, 146.83, 196.0 }; // E A D G
        const double fb = 0.55 + amt * 0.42;
        for (int i = 0; i < n; ++i)
        {
            const double x = (l[i] + r[i]) * 0.5;
            double res = 0.0;
            for (int c = 0; c < 4; ++c)
            {
                auto& buf = combState[(size_t) c];
                int& idx = combIdx[(size_t) c];
                const int len = juce::jmax (4, (int) (srate / freqs[c]));
                const double y = buf[(size_t) (idx % len)];
                buf[(size_t) (idx % len)] = x * 0.25 + y * fb;
                idx = (idx + 1) % len;
                res += y;
            }
            res *= 0.3 * amt;
            l[i] += res;
            r[i] += res;
        }
    }

    //==========================================================================
    Param *mode, *amount, *swell, *bright, *levelP, *trigger;
    dsp::EnvFollower comp, noteDetect;
    dsp::SpectralFreeze freeze;
    dsp::PitchShifter shifter, fifth, subShift;
    dsp::StereoDelay fbLine;
    dsp::StereoBiquad toneShelf;
    dsp::Limiter guard;
    Smoother padGain;
    std::array<std::vector<double>, 4> combState;
    std::array<int, 4> combIdx {};
    enum SusState { Idle, Arming, Holding };
    SusState susState = Idle;
    double lastBright = -1.0, lastEnv = 0.0, slowEnv = 0.0, srate = 48000.0;
    int holdCounter = 0, armCounter = 0, quietCounter = 0;
    bool wasHeld = false;
};

} // namespace vp
