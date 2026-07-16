#pragma once
#include <JuceHeader.h>

namespace vp::dsp
{

//==============================================================================
// Stereo biquad built on juce::dsp::IIR (double precision, transposed direct form II)
struct StereoBiquad
{
    juce::dsp::IIR::Filter<double> f[2];

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        juce::dsp::ProcessSpec spec { sampleRate, 512, 1 };
        for (auto& x : f) { x.prepare (spec); x.reset(); }
    }

    void setCoefficients (juce::dsp::IIR::Coefficients<double>::Ptr c)
    {
        f[0].coefficients = c;
        f[1].coefficients = c;
    }

    void reset() { f[0].reset(); f[1].reset(); }

    double processSample (int ch, double x) { return f[ch].processSample (x); }

    void processBuffer (juce::AudioBuffer<double>& buf)
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                d[i] = f[ch].processSample (d[i]);
        }
    }
};

//==============================================================================
struct DCBlocker
{
    double x1[2] {}, y1[2] {};
    double R = 0.9995;

    void prepare (double sampleRate) { R = 1.0 - (2.0 * juce::MathConstants<double>::pi * 8.0 / sampleRate); reset(); }
    void reset() { x1[0] = x1[1] = y1[0] = y1[1] = 0.0; }

    double processSample (int ch, double x)
    {
        const double y = x - x1[ch] + R * y1[ch];
        x1[ch] = x; y1[ch] = y;
        return y;
    }

    void processBuffer (juce::AudioBuffer<double>& buf)
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                d[i] = processSample (ch, d[i]);
        }
    }
};

//==============================================================================
struct EnvFollower
{
    double attCoeff = 0.0, relCoeff = 0.0, env = 0.0;

    void prepare (double sampleRate, double attackMs, double releaseMs)
    {
        attCoeff = std::exp (-1.0 / (0.001 * juce::jmax (0.01, attackMs) * sampleRate));
        relCoeff = std::exp (-1.0 / (0.001 * juce::jmax (0.01, releaseMs) * sampleRate));
    }

    void reset() { env = 0.0; }

    double processSample (double x)
    {
        const double a = std::abs (x);
        const double c = a > env ? attCoeff : relCoeff;
        env = a + c * (env - a);
        return env;
    }
};

//==============================================================================
struct LFO
{
    enum Shape { Sine = 0, Triangle, Saw, Square, SampleHold };

    double phase = 0.0, inc = 0.0, sh = 0.0;
    juce::Random rng;

    void prepare (double sampleRate) { srate = sampleRate; }
    void setRate (double hz)         { inc = hz / srate; }
    void resetPhase (double p = 0.0) { phase = p; }

    // returns -1..1
    double next (int shape)
    {
        double v = 0.0;
        switch (shape)
        {
            case Sine:      v = std::sin (phase * juce::MathConstants<double>::twoPi); break;
            case Triangle:  v = 4.0 * std::abs (phase - 0.5) - 1.0; break;
            case Saw:       v = 2.0 * phase - 1.0; break;
            case Square:    v = phase < 0.5 ? 1.0 : -1.0; break;
            case SampleHold: v = sh; break;
            default: break;
        }
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            sh = rng.nextDouble() * 2.0 - 1.0;
        }
        return v;
    }

private:
    double srate = 48000.0;
};

//==============================================================================
// Waveshaping curves (input roughly -1..1 after drive gain)
namespace shape
{
    inline double softClip (double x)   { return std::tanh (x); }
    inline double hardClip (double x)   { return juce::jlimit (-1.0, 1.0, x); }
    inline double diode (double x)      { return x >= 0.0 ? 1.0 - std::exp (-x) : -0.72 * (1.0 - std::exp (1.4 * x)); }
    inline double fuzz (double x)       { const double y = x >= 0.0 ? 1.0 - std::exp (-1.8 * x) : -1.0 + std::exp (2.6 * x); return juce::jlimit (-1.1, 1.1, y * 1.15); }
    inline double foldback (double x)
    {
        while (x > 1.0 || x < -1.0)
            x = x > 1.0 ? 2.0 - x : (x < -1.0 ? -2.0 - x : x);
        return x;
    }
    inline double tube (double x)
    {
        const double b = 0.18; // bias => asymmetry => even harmonics
        return std::tanh (x + b * x * x) - std::tanh (b * x * x * 0.5);
    }
    inline double crush (double x, double steps) { return steps <= 1.0 ? x : std::round (x * steps) / steps; }
}

//==============================================================================
// Simple program-adaptive peak limiter (per-buffer stereo-linked), plus hard safety clip.
struct Limiter
{
    EnvFollower env;
    double ceiling = 0.98;

    void prepare (double sampleRate) { env.prepare (sampleRate, 0.05, 80.0); env.reset(); }
    void reset() { env.reset(); }

    void processBuffer (juce::AudioBuffer<double>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer (0);
        auto* r = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : l;
        for (int i = 0; i < n; ++i)
        {
            const double peak = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            const double e = env.processSample (peak);
            double g = e > ceiling ? ceiling / e : 1.0;
            l[i] = juce::jlimit (-1.0, 1.0, l[i] * g);
            if (r != l) r[i] = juce::jlimit (-1.0, 1.0, r[i] * g);
        }
    }
};

//==============================================================================
// Freeverb-style reverb, stereo, double precision.
struct Reverb
{
    static constexpr int numCombs = 8, numAllpasses = 4;

    void prepare (double sampleRate)
    {
        srate = sampleRate;
        const double scale = sampleRate / 44100.0;
        static const int combTuning[numCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apTuning[numAllpasses] = { 556, 441, 341, 225 };
        for (int ch = 0; ch < 2; ++ch)
        {
            const int spread = ch == 0 ? 0 : 23;
            for (int c = 0; c < numCombs; ++c)
            {
                combs[ch][c].resize ((size_t) juce::jmax (4, (int) ((combTuning[c] + spread) * scale)));
                std::fill (combs[ch][c].begin(), combs[ch][c].end(), 0.0);
                combIdx[ch][c] = 0; combLp[ch][c] = 0.0;
            }
            for (int a = 0; a < numAllpasses; ++a)
            {
                aps[ch][a].resize ((size_t) juce::jmax (4, (int) ((apTuning[a] + spread) * scale)));
                std::fill (aps[ch][a].begin(), aps[ch][a].end(), 0.0);
                apIdx[ch][a] = 0;
            }
        }
        preDelayBuf.resize ((size_t) (sampleRate * 0.25) + 8, 0.0);
        preIdx = 0;
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& c : combs[ch]) std::fill (c.begin(), c.end(), 0.0);
            for (auto& a : aps[ch])   std::fill (a.begin(), a.end(), 0.0);
            for (auto& l : combLp[ch]) l = 0.0;
        }
        std::fill (preDelayBuf.begin(), preDelayBuf.end(), 0.0);
    }

    // room 0..1, damp 0..1, preDelayMs 0..250. Input mono mix, writes stereo wet.
    void processSample (double inL, double inR, double room, double damp, double preDelayMs,
                        double& outL, double& outR)
    {
        double input = (inL + inR) * 0.5 * 0.03;

        // pre-delay
        const int preSamps = juce::jlimit (0, (int) preDelayBuf.size() - 1, (int) (preDelayMs * 0.001 * srate));
        preDelayBuf[(size_t) preIdx] = input;
        int rd = preIdx - preSamps; if (rd < 0) rd += (int) preDelayBuf.size();
        input = preDelayBuf[(size_t) rd];
        if (++preIdx >= (int) preDelayBuf.size()) preIdx = 0;

        const double feedback = 0.7 + 0.28 * room;
        const double d = damp * 0.4 + 0.05;

        double out[2] = { 0.0, 0.0 };
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int c = 0; c < numCombs; ++c)
            {
                auto& buf = combs[ch][c];
                int& idx = combIdx[ch][c];
                const double y = buf[(size_t) idx];
                combLp[ch][c] = y * (1.0 - d) + combLp[ch][c] * d;
                buf[(size_t) idx] = input + combLp[ch][c] * feedback;
                if (++idx >= (int) buf.size()) idx = 0;
                out[ch] += y;
            }
            for (int a = 0; a < numAllpasses; ++a)
            {
                auto& buf = aps[ch][a];
                int& idx = apIdx[ch][a];
                const double bufout = buf[(size_t) idx];
                const double v = out[ch] + bufout * 0.5;
                buf[(size_t) idx] = v;
                out[ch] = bufout - v * 0.5;
                if (++idx >= (int) buf.size()) idx = 0;
            }
        }
        outL = out[0];
        outR = out[1];
    }

private:
    std::vector<double> combs[2][numCombs], aps[2][numAllpasses];
    double combLp[2][numCombs] {};
    int combIdx[2][numCombs] {}, apIdx[2][numAllpasses] {};
    std::vector<double> preDelayBuf;
    int preIdx = 0;
    double srate = 48000.0;
};

//==============================================================================
// Classic dual-tap crossfaded delay-line pitch shifter (per channel pair).
struct PitchShifter
{
    void prepare (double sampleRate)
    {
        srate = sampleRate;
        windowSamps = (int) (0.045 * sampleRate); // 45 ms grains
        const int size = juce::nextPowerOfTwo (windowSamps * 4);
        mask = size - 1;
        for (auto& b : buf) { b.assign ((size_t) size, 0.0); }
        writeIdx = 0;
        phase = 0.0;
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0);
        phase = 0.0;
    }

    void setSemitones (double st) { ratio = std::pow (2.0, st / 12.0); }

    double processSample (int ch, double x)
    {
        auto& b = buf[ch];
        b[(size_t) (writeIdx & mask)] = x;

        // two read taps half a window apart, crossfaded by a triangular window
        const double rate = 1.0 - ratio;
        if (ch == 0)
        {
            phase += rate;
            const double w = (double) windowSamps;
            if (phase >= w) phase -= w;
            else if (phase < 0.0) phase += w;
        }

        auto tap = [&] (double offset) -> double
        {
            double idx = (double) writeIdx - 2.0 - offset;
            const int i0 = (int) std::floor (idx);
            const double frac = idx - (double) i0;
            const double a = b[(size_t) (i0 & mask)];
            const double c = b[(size_t) ((i0 + 1) & mask)];
            return a + frac * (c - a);
        };

        const double w = (double) windowSamps;
        const double p1 = phase;
        double p2 = phase + w * 0.5; if (p2 >= w) p2 -= w;
        const double g1 = std::sin (juce::MathConstants<double>::pi * p1 / w);
        const double g2 = std::sin (juce::MathConstants<double>::pi * p2 / w);
        const double out = tap (p1) * g1 + tap (p2) * g2;

        if (ch == 1)
            ++writeIdx;
        return out;
    }

    // convenience for mono use (advances write index itself)
    double processMono (double x)
    {
        const double l = processSample (0, x);
        const double r = processSample (1, x);
        return (l + r) * 0.5;
    }

    double ratio = 1.0;

private:
    std::vector<double> buf[2];
    int writeIdx = 0, mask = 0, windowSamps = 2048;
    double phase = 0.0, srate = 48000.0;
};

//==============================================================================
// FFT spectral freeze, Paulstretch-style: the captured magnitude spectrum is
// resynthesised every hop with fully RANDOM phases (Hann, 75% overlap-add).
// Random-phase resynthesis is what keeps the pad smooth and organ-like —
// accumulating phases frame-to-frame produces the metallic "trrrr" buzz.
// Capture averages several analysis frames so plucks/pinch harmonics settle.
struct SpectralFreeze
{
    static constexpr int order = 12, size = 1 << order, hop = size / 4;
    static constexpr int captureFrames = 4;

    void prepare (double sampleRate)
    {
        juce::ignoreUnused (sampleRate);
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize (size);
        juce::dsp::WindowingFunction<double>::fillWindowingTables (window.data(), (size_t) size,
            juce::dsp::WindowingFunction<double>::hann, false);
        for (int ch = 0; ch < 2; ++ch)
        {
            outBuf[ch].assign (size * 2, 0.0);
            mag[ch].assign (size / 2 + 1, 0.0f);
            magAccum[ch].assign (size / 2 + 1, 0.0f);
            history[ch].assign (size, 0.0);
        }
        scratch.assign (size * 2, 0.0f);
        hopCounter = 0; outRead = 0; synthWrite = 0; histPos = 0; accumCount = 0;
        frozen = false; capturing = false;
        rng.setSeedRandomly();
    }

    void reset() { if (fft != nullptr) prepare (0); }

    void requestCapture()
    {
        capturing = true;
        accumCount = 0;
        for (int ch = 0; ch < 2; ++ch)
            std::fill (magAccum[ch].begin(), magAccum[ch].end(), 0.0f);
    }

    void unfreeze()       { frozen = false; capturing = false; }
    bool isFrozen() const { return frozen; }

    // Feed dry input every sample; outputs the frozen texture (0 until frozen).
    void processSample (double inL, double inR, double& outL, double& outR)
    {
        history[0][(size_t) histPos] = inL;
        history[1][(size_t) histPos] = inR;
        histPos = (histPos + 1) % size;

        if (++hopCounter >= hop)
        {
            hopCounter = 0;
            if (capturing)
            {
                accumulateFrame();
                if (++accumCount >= captureFrames)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (size_t k = 0; k < mag[ch].size(); ++k)
                            mag[ch][k] = magAccum[ch][k] / (float) captureFrames;
                    capturing = false;
                    frozen = true;
                }
            }
            if (frozen)
                synthesiseFrame();
        }

        outL = outBuf[0][(size_t) outRead];
        outR = outBuf[1][(size_t) outRead];
        outBuf[0][(size_t) outRead] = 0.0;
        outBuf[1][(size_t) outRead] = 0.0;
        if (! frozen)
            synthWrite = outRead; // keep cursors aligned so a new freeze starts immediately
        outRead = (outRead + 1) % (size * 2);
    }

private:
    void accumulateFrame()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (scratch.begin(), scratch.end(), 0.0f);
            for (int i = 0; i < size; ++i) // oldest -> newest, windowed
                scratch[(size_t) i] = (float) (history[ch][(size_t) ((histPos + i) % size)] * window[(size_t) i]);
            fft->performRealOnlyForwardTransform (scratch.data());
            for (int k = 0; k <= size / 2; ++k)
            {
                const float re = scratch[(size_t) (2 * k)];
                const float im = scratch[(size_t) (2 * k + 1)];
                magAccum[ch][(size_t) k] += std::sqrt (re * re + im * im);
            }
        }
    }

    void synthesiseFrame()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (scratch.begin(), scratch.end(), 0.0f);
            for (int k = 1; k <= size / 2; ++k) // bin 0 (DC) stays silent
            {
                const float theta = rng.nextFloat() * juce::MathConstants<float>::twoPi;
                const float m = mag[ch][(size_t) k];
                scratch[(size_t) (2 * k)]     = m * std::cos (theta);
                scratch[(size_t) (2 * k + 1)] = m * std::sin (theta);
            }
            fft->performRealOnlyInverseTransform (scratch.data());
            const double norm = 1.0 / 1.5; // Hann^2 sum at 75% overlap
            for (int i = 0; i < size; ++i)
            {
                const int w = (synthWrite + i) % (size * 2);
                outBuf[ch][(size_t) w] += (double) scratch[(size_t) i] * window[(size_t) i] * norm;
            }
        }
        synthWrite = (synthWrite + hop) % (size * 2);
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<double> window;
    std::vector<double> outBuf[2], history[2];
    std::vector<float> mag[2], magAccum[2], scratch;
    int hopCounter = 0, outRead = 0, synthWrite = 0, histPos = 0, accumCount = 0;
    bool frozen = false, capturing = false;
    juce::Random rng;
};

//==============================================================================
// Fractional delay line (stereo), cubic interpolation, up to maxSeconds.
struct StereoDelay
{
    void prepare (double sampleRate, double maxSeconds)
    {
        srate = sampleRate;
        const int len = juce::nextPowerOfTwo ((int) (sampleRate * maxSeconds) + 4);
        mask = len - 1;
        for (auto& b : buf) b.assign ((size_t) len, 0.0);
        writeIdx = 0;
    }

    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0); }

    void push (int ch, double x) { buf[ch][(size_t) (writeIdx & mask)] = x; }
    void advance() { ++writeIdx; }

    double read (int ch, double delaySamples) const
    {
        const double idx = (double) writeIdx - delaySamples;
        const int i1 = (int) std::floor (idx);
        const double frac = idx - (double) i1;
        const auto& b = buf[ch];
        const double y0 = b[(size_t) ((i1 - 1) & mask)];
        const double y1 = b[(size_t) (i1 & mask)];
        const double y2 = b[(size_t) ((i1 + 1) & mask)];
        const double y3 = b[(size_t) ((i1 + 2) & mask)];
        const double a = frac;
        return y1 + 0.5 * a * (y2 - y0 + a * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + a * (3.0 * (y1 - y2) + y3 - y0)));
    }

    double srate = 48000.0;

private:
    std::vector<double> buf[2];
    int writeIdx = 0, mask = 0;
};

} // namespace vp::dsp
