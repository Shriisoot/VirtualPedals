#pragma once
#include "../Engine/Pedal.h"
#include "../Engine/DspUtil.h"

namespace vp
{

//==============================================================================
class DelayPedal : public Pedal
{
public:
    DelayPedal() : Pedal ("delay", "Delay", "Delay", true)
    {
        time     = addParam ({ "time", "Time", "ms", 20.0, 2000.0, 380.0, 0.5 });
        feedback = addParam ({ "feedback", "Feedback", "%", 0.0, 95.0, 35.0 });
        tone     = addParam ({ "tone", "Tone", "", 0.0, 10.0, 6.0 });
        modeSel  = addParam ({ "modeSel", "Mode", "", 0.0, 3.0, 0.0, 1.0, { "Digital", "Analog", "Tape", "Ping-Pong" } });
        wowAmt   = addParam ({ "wow", "Wow/Flutter", "", 0.0, 10.0, 2.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        line.prepare (sampleRate, 2.5);
        fbFilter.prepare (sampleRate, 0);
        fbHp.prepare (sampleRate, 0);
        fbHp.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeHighPass (sampleRate, 90.0, 0.707));
        lfo.prepare (sampleRate);
        timeSm.prepare (sampleRate, 180.0); // slow glide => tape-style repitching, no clicks
        timeSm.setTarget (time->get() * 0.001 * sampleRate);
        timeSm.snap();
        lastTone = -1.0;
    }

    void reset() override { line.reset(); fbFilter.reset(); fbHp.reset(); }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double fb = feedback->blockSmoothed (n) / 100.0;
        const double t  = tone->blockSmoothed (n);
        const int mode  = modeSel->getChoice();
        const double wow = wowAmt->blockSmoothed (n) / 10.0;

        timeSm.setTarget (time->get() * 0.001 * srate);

        if (std::abs (t - lastTone) > 0.02 || mode != lastMode)
        {
            lastTone = t; lastMode = mode;
            const double base = mode == 1 ? 3400.0 : mode == 2 ? 5200.0 : 12000.0;
            fbFilter.setCoefficients (juce::dsp::IIR::Coefficients<double>::makeLowPass (
                srate, juce::jmap (t, 0.0, 10.0, base * 0.3, juce::jmin (base * 2.0, srate * 0.45)), 0.707));
        }

        lfo.setRate (mode == 2 ? 0.9 : 0.4);

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            double delaySamps = timeSm.next();
            if (mode == 2 && wow > 0.0)
                delaySamps *= 1.0 + lfo.next (dsp::LFO::Sine) * 0.0025 * wow
                                  + std::sin (flutterPhase += 7.3 / srate * juce::MathConstants<double>::twoPi) * 0.0006 * wow;
            delaySamps = juce::jlimit (4.0, srate * 2.4, delaySamps);

            double dl = line.read (0, delaySamps);
            double dr = line.read (1, delaySamps);

            // feedback conditioning
            dl = fbFilter.processSample (0, dl);
            dr = fbFilter.processSample (1, dr);
            dl = fbHp.processSample (0, dl);
            dr = fbHp.processSample (1, dr);
            if (mode == 1 || mode == 2) // analog/tape soft saturation in the loop
            {
                dl = std::tanh (dl * 1.1);
                dr = std::tanh (dr * 1.1);
            }

            if (mode == 3) // ping-pong: cross-feedback
            {
                line.push (0, l[i] + dr * fb);
                line.push (1, dl * fb); // right lane fed only from left tap => bouncing
            }
            else
            {
                line.push (0, l[i] + dl * fb);
                line.push (1, r[i] + dr * fb);
            }
            line.advance();

            l[i] = dl;
            r[i] = mode == 3 ? dr : dr;
        }
    }

private:
    Param *time, *feedback, *tone, *modeSel, *wowAmt;
    dsp::StereoDelay line;
    dsp::StereoBiquad fbFilter, fbHp;
    dsp::LFO lfo;
    Smoother timeSm;
    double lastTone = -1.0, flutterPhase = 0.0, srate = 48000.0;
    int lastMode = -1;
};

//==============================================================================
class ReverseDelayPedal : public Pedal
{
public:
    ReverseDelayPedal() : Pedal ("reversedelay", "Reverse", "Delay", true)
    {
        window   = addParam ({ "window", "Window", "ms", 100.0, 2000.0, 600.0, 0.5 });
        feedback = addParam ({ "feedback", "Feedback", "%", 0.0, 80.0, 20.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        const int maxLen = (int) (sampleRate * 2.2);
        for (auto& b : rec) b.assign ((size_t) maxLen, 0.0);
        writeIdx = 0;
    }

    void reset() override
    {
        for (auto& b : rec) std::fill (b.begin(), b.end(), 0.0);
        writeIdx = 0;
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const int win = juce::jlimit (32, (int) rec[0].size() - 1, (int) (window->blockSmoothed (n) * 0.001 * srate));
        const double fb = feedback->blockSmoothed (n) / 100.0;

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const int pos = writeIdx % win;                 // forward write cursor within window
            const int rdPos = win - 1 - pos;                // mirrored read cursor => reversed audio
            const int base = (writeIdx / win) % 2;          // double-buffer halves to avoid overlap
            const int wOff = base * win;
            const int rOff = (1 - base) * win;

            const size_t wl = (size_t) ((wOff + pos) % (int) rec[0].size());
            const size_t rl = (size_t) ((rOff + rdPos) % (int) rec[0].size());

            const double outL = rec[0][rl];
            const double outR = rec[1][rl];

            // crossfade the seams to avoid clicks
            const double edge = juce::jmin (pos, win - 1 - pos) / juce::jmax (1.0, win * 0.05);
            const double fade = juce::jlimit (0.0, 1.0, edge);

            rec[0][wl] = l[i] + outL * fb;
            rec[1][wl] = r[i] + outR * fb;

            l[i] = outL * fade;
            r[i] = outR * fade;
            ++writeIdx;
        }
    }

private:
    Param *window, *feedback;
    std::vector<double> rec[2];
    int writeIdx = 0;
    double srate = 48000.0;
};

//==============================================================================
class LooperPedal : public Pedal
{
public:
    static constexpr double maxSeconds = 60.0;

    LooperPedal() : Pedal ("looper", "Looper", "Utility")
    {
        recordP  = addParam ({ "record", "Record", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        playP    = addParam ({ "play", "Play", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        overdubP = addParam ({ "overdub", "Overdub", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        clearP   = addParam ({ "clear", "Clear", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        reverseP = addParam ({ "reverse", "Reverse", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        halfP    = addParam ({ "half", "Half Speed", "", 0.0, 1.0, 0.0, 1.0, {}, true });
        volumeP  = addParam ({ "volume", "Loop Level", "dB", -30.0, 6.0, 0.0 });
    }

    void prepare (double sampleRate, int) override
    {
        srate = sampleRate;
        const auto len = (size_t) (sampleRate * maxSeconds);
        for (auto& b : loop) b.assign (len, 0.0);
        loopLen = 0; playPos = 0.0; recording = false;
    }

    void reset() override {}

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        const double vol = juce::Decibels::decibelsToGain (volumeP->blockSmoothed (n));

        if (clearP->getBool())
        {
            clearP->set (0.0);
            recordP->set (0.0); playP->set (0.0); overdubP->set (0.0);
            loopLen = 0; playPos = 0.0; recording = false;
        }

        const bool wantRecord = recordP->getBool();
        const bool wantPlay   = playP->getBool();
        const bool wantDub    = overdubP->getBool();
        const bool rev        = reverseP->getBool();
        const double speed    = halfP->getBool() ? 0.5 : 1.0;

        if (wantRecord && ! recording) { recording = true; loopLen = 0; playPos = 0.0; }
        if (! wantRecord && recording)
        {
            recording = false;
            if (loopLen > (int) (srate * 0.1))
                playP->set (1.0); // auto-start playback when recording stops
        }

        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        const int maxLen = (int) loop[0].size();

        for (int i = 0; i < n; ++i)
        {
            if (recording && loopLen < maxLen)
            {
                loop[0][(size_t) loopLen] = l[i];
                loop[1][(size_t) loopLen] = r[i];
                ++loopLen;
            }
            else if (wantPlay && loopLen > 0)
            {
                double pos = rev ? (double) loopLen - 1.0 - playPos : playPos;
                pos = juce::jlimit (0.0, (double) loopLen - 1.0, pos);
                const int i0 = (int) pos;
                const int i1 = juce::jmin (i0 + 1, loopLen - 1);
                const double frac = pos - i0;

                const double pl = loop[0][(size_t) i0] + frac * (loop[0][(size_t) i1] - loop[0][(size_t) i0]);
                const double pr = loop[1][(size_t) i0] + frac * (loop[1][(size_t) i1] - loop[1][(size_t) i0]);

                if (wantDub)
                {
                    const size_t di = (size_t) (rev ? loopLen - 1 - i0 : i0);
                    loop[0][di] += l[i];
                    loop[1][di] += r[i];
                }

                l[i] += pl * vol;
                r[i] += pr * vol;

                playPos += speed;
                if (playPos >= (double) loopLen)
                    playPos -= (double) loopLen;
            }
        }
    }

    float getLoopSeconds() const { return (float) (loopLen / srate); }
    bool  isRecording() const   { return recording; }

private:
    Param *recordP, *playP, *overdubP, *clearP, *reverseP, *halfP, *volumeP;
    std::vector<double> loop[2];
    int loopLen = 0;
    double playPos = 0.0, srate = 48000.0;
    bool recording = false;
};

} // namespace vp
