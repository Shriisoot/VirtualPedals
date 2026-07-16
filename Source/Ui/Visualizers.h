#pragma once
#include "../Engine/AudioEngine.h"
#include "LookAndFeel.h"

namespace vp
{

//==============================================================================
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter (std::atomic<float>& peakSource, std::atomic<float>& rmsSource)
        : peak (peakSource), rms (rmsSource)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (colours::panelHi);
        g.fillRoundedRectangle (b, 3.0f);

        const float pk = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (peak.load() + 1.0e-6f, -60.0f) / 60.0f + 1.0f);
        const float rm = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (rms.load() + 1.0e-6f, -60.0f) / 60.0f + 1.0f);

        auto fill = b.reduced (2.0f);
        const bool horizontal = getWidth() > getHeight();

        auto draw = [&] (float v, juce::Colour c, float alpha)
        {
            g.setColour (c.withAlpha (alpha));
            if (horizontal)
                g.fillRoundedRectangle (fill.withWidth (fill.getWidth() * v), 2.0f);
            else
                g.fillRoundedRectangle (fill.withTop (fill.getBottom() - fill.getHeight() * v), 2.0f);
        };

        const auto colFor = [] (float v) { return v > 0.97f ? colours::bad : v > 0.85f ? colours::warn : colours::good; };
        draw (pk, colFor (pk), 0.45f);
        draw (rm, colFor (rm), 0.95f);
    }

private:
    void timerCallback() override { repaint(); }
    std::atomic<float>& peak;
    std::atomic<float>& rms;
};

//==============================================================================
class SpectrumAnalyzer : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumAnalyzer (AudioEngine& e) : engine (e), fft (11)
    {
        window.resize (fftSize);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize,
            juce::dsp::WindowingFunction<float>::hann, false);
        fifo.resize (fftSize, 0.0f);
        fftData.resize (fftSize * 2, 0.0f);
        smoothed.resize (numBins, -100.0f);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (b, 4.0f);

        juce::Path path;
        const float sr = (float) engine.getSampleRate();
        bool started = false;
        for (int i = 0; i < numBins; ++i)
        {
            const float freq = (float) i * sr / (float) fftSize;
            if (freq < 25.0f || freq > 16000.0f)
                continue;
            const float xNorm = std::log (freq / 25.0f) / std::log (16000.0f / 25.0f);
            const float yNorm = juce::jlimit (0.0f, 1.0f, (smoothed[(size_t) i] + 90.0f) / 90.0f);
            const float px = b.getX() + xNorm * b.getWidth();
            const float py = b.getBottom() - yNorm * b.getHeight();
            if (! started) { path.startNewSubPath (px, py); started = true; }
            else             path.lineTo (px, py);
        }
        g.setColour (colours::accent.withAlpha (0.9f));
        g.strokePath (path, juce::PathStrokeType (1.5f));

        g.setColour (colours::textDim);
        g.setFont (10.0f);
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float xNorm = std::log (f / 25.0f) / std::log (16000.0f / 25.0f);
            const float px = b.getX() + xNorm * b.getWidth();
            g.drawVerticalLine ((int) px, b.getY() + 2.0f, b.getBottom() - 2.0f);
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                        (int) px + 2, (int) b.getY(), 34, 12, juce::Justification::left);
        }
    }

private:
    void timerCallback() override
    {
        float tmp[2048];
        int got;
        while ((got = engine.readVisualisationSamples (tmp, 2048)) > 0)
        {
            for (int i = 0; i < got; ++i)
            {
                fifo[(size_t) fifoPos] = tmp[i];
                if (++fifoPos >= fftSize)
                {
                    fifoPos = 0;
                    computeFrame();
                }
            }
            if (got < 2048)
                break;
        }
        repaint();
    }

    void computeFrame()
    {
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = fifo[(size_t) i] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performRealOnlyForwardTransform (fftData.data());
        for (int i = 0; i < numBins; ++i)
        {
            const float re = fftData[(size_t) (2 * i)];
            const float im = fftData[(size_t) (2 * i + 1)];
            const float db = juce::Decibels::gainToDecibels (std::sqrt (re * re + im * im) / (float) fftSize * 4.0f, -100.0f);
            smoothed[(size_t) i] = juce::jmax (db, smoothed[(size_t) i] - 2.2f); // fast attack, slow release
        }
    }

    static constexpr int fftSize = 1 << 11;
    static constexpr int numBins = fftSize / 2;
    AudioEngine& engine;
    juce::dsp::FFT fft;
    std::vector<float> window, fifo, fftData, smoothed;
    int fifoPos = 0;
};

//==============================================================================
class Oscilloscope : public juce::Component, private juce::Timer
{
public:
    explicit Oscilloscope (AudioEngine& e) : engine (e)
    {
        display.resize (2048, 0.0f);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (colours::outline);
        g.drawHorizontalLine (getHeight() / 2, b.getX(), b.getRight());

        juce::Path p;
        const int n = (int) display.size();
        for (int i = 0; i < n; ++i)
        {
            const float px = b.getX() + (float) i / (float) (n - 1) * b.getWidth();
            const float py = b.getCentreY() - juce::jlimit (-1.0f, 1.0f, display[(size_t) i]) * b.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath (px, py);
            else        p.lineTo (px, py);
        }
        g.setColour (colours::good.withAlpha (0.9f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

private:
    void timerCallback() override
    {
        float tmp[2048];
        const int got = engine.readVisualisationSamples (tmp, 2048);
        if (got > 0)
        {
            const int n = (int) display.size();
            if (got >= n)
                std::copy (tmp + (got - n), tmp + got, display.begin());
            else
            {
                std::rotate (display.begin(), display.begin() + got, display.end());
                std::copy (tmp, tmp + got, display.end() - got);
            }
        }
        repaint();
    }

    AudioEngine& engine;
    std::vector<float> display;
};

} // namespace vp
