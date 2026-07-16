#include "ToneDnaAnalyzer.h"
#include "../Engine/PedalFactory.h"

namespace vp
{

//==============================================================================
static std::unique_ptr<juce::AudioFormatReader> openAudioFile (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats(); // WAV, AIFF, FLAC, OGG, MP3 (via bundled/WMF readers)
    return std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (file));
}

void ToneDnaAnalyzer::analyzeFile (const juce::File& file)
{
    pendingFile = file;
    startThread();
}

void ToneDnaAnalyzer::run()
{
    ToneDnaResult result;
    auto reader = openAudioFile (pendingFile);

    if (reader == nullptr)
    {
        result.error = "Could not open audio file (supported: WAV, AIFF, FLAC, MP3, OGG).";
    }
    else
    {
        const double sr = reader->sampleRate;
        const juce::int64 maxSamples = juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (sr * 90.0));
        juce::AudioBuffer<float> audio ((int) juce::jmin (2u, reader->numChannels), (int) maxSamples);
        reader->read (&audio, 0, (int) maxSamples, 0, true, reader->numChannels > 1);
        if (onProgress) onProgress (0.3f);
        result = analyzeBuffer (audio, sr);
        if (onProgress) onProgress (0.9f);
    }

    ToneDnaResult toSend = result;
    auto cb = onComplete;
    juce::MessageManager::callAsync ([cb, toSend] { if (cb) cb (toSend); });
}

bool ToneDnaAnalyzer::loadReferenceIntoEngine (const juce::File& file, AudioEngine& engine)
{
    auto reader = openAudioFile (file);
    if (reader == nullptr)
        return false;
    const juce::int64 maxSamples = juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 120.0));
    juce::AudioBuffer<float> audio ((int) juce::jmin (2u, reader->numChannels), (int) maxSamples);
    reader->read (&audio, 0, (int) maxSamples, 0, true, reader->numChannels > 1);
    engine.loadReference (std::move (audio), reader->sampleRate);
    return true;
}

//==============================================================================
ToneDnaResult ToneDnaAnalyzer::analyzeBuffer (const juce::AudioBuffer<float>& audio, double sr)
{
    ToneDnaResult r;
    const int numSamples = audio.getNumSamples();
    if (numSamples < (int) sr / 2 || sr <= 0.0)
    {
        r.error = "Audio too short to analyse (need at least 0.5 s).";
        return r;
    }
    r.lengthSeconds = numSamples / sr;

    const auto* ch0 = audio.getReadPointer (0);
    const auto* ch1 = audio.getNumChannels() > 1 ? audio.getReadPointer (1) : ch0;

    //=== long-term average spectrum ==========================================
    constexpr int order = 12, fftSize = 1 << order, hop = fftSize / 2;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize,
        juce::dsp::WindowingFunction<float>::hann, false);

    std::vector<double> avgMag ((size_t) fftSize / 2 + 1, 0.0);
    std::vector<float> frame ((size_t) fftSize * 2, 0.0f);
    std::vector<double> centroidTrack;
    std::vector<double> fluxTrack;
    std::vector<double> prevMag ((size_t) fftSize / 2 + 1, 0.0);
    int frames = 0;

    for (int pos = 0; pos + fftSize <= numSamples; pos += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            frame[(size_t) i] = 0.5f * (ch0[pos + i] + ch1[pos + i]) * window[(size_t) i];
        fft.performRealOnlyForwardTransform (frame.data());

        double centroidNum = 0.0, centroidDen = 0.0, flux = 0.0;
        for (int k = 1; k <= fftSize / 2; ++k)
        {
            const double re = frame[(size_t) (2 * k)];
            const double im = frame[(size_t) (2 * k + 1)];
            const double m = std::sqrt (re * re + im * im);
            avgMag[(size_t) k] += m;
            const double f = k * sr / fftSize;
            centroidNum += f * m;
            centroidDen += m;
            flux += juce::jmax (0.0, m - prevMag[(size_t) k]);
            prevMag[(size_t) k] = m;
        }
        centroidTrack.push_back (centroidDen > 1.0e-9 ? centroidNum / centroidDen : 0.0);
        fluxTrack.push_back (flux);
        ++frames;
    }
    if (frames == 0) { r.error = "Analysis failed."; return r; }
    for (auto& m : avgMag) m /= frames;

    //=== band energies + tilt + rolloff ======================================
    static const double bandEdges[9] = { 60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    double totalE = 1.0e-12;
    std::array<double, 8> bandE {};
    for (int k = 1; k <= fftSize / 2; ++k)
    {
        const double f = k * sr / fftSize;
        const double e = avgMag[(size_t) k] * avgMag[(size_t) k];
        totalE += e;
        for (int b = 0; b < 8; ++b)
            if (f >= bandEdges[b] && f < bandEdges[b + 1])
                { bandE[(size_t) b] += e; break; }
    }
    for (int b = 0; b < 8; ++b)
        r.bandEnergyDb[(size_t) b] = 10.0 * std::log10 (bandE[(size_t) b] / totalE + 1.0e-12);

    const double lowE  = bandE[1] + bandE[2];
    const double highE = bandE[5] + bandE[6];
    r.tiltDb = 10.0 * std::log10 ((highE + 1.0e-12) / (lowE + 1.0e-12));

    // spectral rolloff: highest freq still within 30 dB of the strongest band
    {
        double peakDb = -300.0;
        for (int b = 0; b < 8; ++b) peakDb = juce::jmax (peakDb, r.bandEnergyDb[(size_t) b]);
        r.rolloffHz = 20000.0;
        for (int b = 7; b >= 0; --b)
        {
            if (r.bandEnergyDb[(size_t) b] > peakDb - 30.0)
            {
                r.rolloffHz = bandEdges[b + 1];
                break;
            }
        }
    }

    //=== dynamics =============================================================
    {
        const int win = (int) (sr * 0.05);
        std::vector<double> rmsDb;
        double globalPeak = 1.0e-9, globalRms = 0.0;
        for (int pos = 0; pos + win <= numSamples; pos += win)
        {
            double acc = 0.0;
            for (int i = 0; i < win; ++i)
            {
                const double s = 0.5 * (ch0[pos + i] + ch1[pos + i]);
                acc += s * s;
                globalPeak = juce::jmax (globalPeak, std::abs (s));
            }
            const double rms = std::sqrt (acc / win);
            globalRms += acc;
            if (rms > 1.0e-6)
                rmsDb.push_back (juce::Decibels::gainToDecibels (rms));
        }
        globalRms = std::sqrt (globalRms / juce::jmax (1, (numSamples / win) * win));
        r.crestDb = juce::Decibels::gainToDecibels (globalPeak / juce::jmax (1.0e-9, globalRms));

        if (rmsDb.size() > 4)
        {
            std::sort (rmsDb.begin(), rmsDb.end());
            const double p10 = rmsDb[(size_t) (rmsDb.size() * 0.10)];
            const double p95 = rmsDb[(size_t) (rmsDb.size() * 0.95)];
            r.dynamicRangeDb = p95 - p10;
            r.noiseFloorDb = rmsDb.front();
        }
    }

    //=== saturation / clipping ===============================================
    {
        // sample-value histogram concentration near the extremes = clipping
        int nearMax = 0, total = 0;
        double maxAbs = 1.0e-9;
        for (int i = 0; i < numSamples; i += 4)
            maxAbs = juce::jmax (maxAbs, (double) std::abs (ch0[i]));
        for (int i = 0; i < numSamples; i += 4)
        {
            const double a = std::abs (ch0[i]) / maxAbs;
            if (a > 0.93) ++nearMax;
            if (a > 0.05) ++total;
        }
        r.clipping = total > 0 ? juce::jlimit (0.0, 1.0, (double) nearMax / total * 6.0) : 0.0;

        // drive estimate: energy density in 2-6 kHz relative to 200-800 Hz plus low crest
        const double presence = (bandE[5] + bandE[6] * 0.5 + 1.0e-12) / (bandE[2] + bandE[3] + 1.0e-12);
        const double crestFactor = juce::jlimit (0.0, 1.0, (14.0 - r.crestDb) / 10.0);
        r.saturation = juce::jlimit (0.0, 1.0, presence * 0.55 + crestFactor * 0.55 + r.clipping * 0.4);
    }

    //=== stereo width =========================================================
    if (audio.getNumChannels() > 1)
    {
        double mid = 1.0e-12, side = 0.0;
        for (int i = 0; i < numSamples; i += 2)
        {
            mid  += std::abs (ch0[i] + ch1[i]) * 0.5;
            side += std::abs (ch0[i] - ch1[i]) * 0.5;
        }
        r.stereoWidth = juce::jlimit (0.0, 1.0, side / mid * 2.0);
    }

    //=== reverb tail ==========================================================
    {
        // measure decay after energy peaks: average slope of the envelope in dB/s
        const int win = (int) (sr * 0.05);
        std::vector<double> env;
        for (int pos = 0; pos + win <= numSamples; pos += win)
        {
            double acc = 0.0;
            for (int i = 0; i < win; ++i) { const double s = 0.5 * (ch0[pos + i] + ch1[pos + i]); acc += s * s; }
            env.push_back (std::sqrt (acc / win));
        }
        double tailAcc = 0.0; int tailCount = 0;
        double decayAcc = 0.0; int decayCount = 0;
        for (size_t i = 2; i < env.size(); ++i)
        {
            const double prev = env[i - 1], cur = env[i];
            if (prev > 1.0e-5 && cur < prev && cur > 1.0e-6)
            {
                const double slopeDbPerS = juce::Decibels::gainToDecibels (cur / prev) / 0.05;
                if (slopeDbPerS > -120.0 && slopeDbPerS < -1.0)
                {
                    decayAcc += slopeDbPerS;
                    ++decayCount;
                }
            }
            // "wash": how much level persists between transients
            if (env[i] > 1.0e-6)
            {
                tailAcc += juce::jmin (1.0, env[i] / (env[i - 2] + 1.0e-9));
                ++tailCount;
            }
        }
        const double avgSlope = decayCount > 0 ? decayAcc / decayCount : -120.0;
        r.reverbDecayS = juce::jlimit (0.0, 12.0, 60.0 / juce::jmax (5.0, -avgSlope));
        r.tailRatio = tailCount > 0 ? juce::jlimit (0.0, 1.0, tailAcc / tailCount) : 0.0;
    }

    //=== delay detection via onset-envelope autocorrelation ==================
    {
        // fluxTrack sampled at sr/hop Hz
        const double frameRate = sr / hop;
        const int minLag = (int) (0.08 * frameRate);
        const int maxLag = juce::jmin ((int) fluxTrack.size() / 2, (int) (1.2 * frameRate));
        double mean = 0.0;
        for (double f : fluxTrack) mean += f;
        mean /= juce::jmax ((size_t) 1, fluxTrack.size());

        double best = 0.0; int bestLag = 0; double norm = 1.0e-12;
        for (size_t i = 0; i < fluxTrack.size(); ++i)
            norm += (fluxTrack[i] - mean) * (fluxTrack[i] - mean);
        for (int lag = minLag; lag < maxLag; ++lag)
        {
            double acc = 0.0;
            for (size_t i = 0; i + (size_t) lag < fluxTrack.size(); ++i)
                acc += (fluxTrack[i] - mean) * (fluxTrack[i + (size_t) lag] - mean);
            acc /= norm;
            if (acc > best) { best = acc; bestLag = lag; }
        }
        if (best > 0.25 && bestLag > 0)
        {
            r.delayMs = bestLag / frameRate * 1000.0;
            r.delayStrength = juce::jlimit (0.0, 1.0, best);
        }
    }

    //=== modulation via spectral-centroid wobble =============================
    {
        // remove slow trend, then find dominant 0.2-8 Hz periodicity
        const double frameRate = sr / hop;
        std::vector<double> c = centroidTrack;
        double mean = 0.0;
        for (double x : c) mean += x;
        mean /= juce::jmax ((size_t) 1, c.size());
        for (auto& x : c) x -= mean;

        double bestPower = 0.0, bestHz = 0.0, sigPower = 1.0e-12;
        for (double x : c) sigPower += x * x;
        for (double hz = 0.2; hz <= 8.0; hz += 0.1)
        {
            double re = 0.0, im = 0.0;
            for (size_t i = 0; i < c.size(); ++i)
            {
                const double ph = juce::MathConstants<double>::twoPi * hz * (double) i / frameRate;
                re += c[i] * std::cos (ph);
                im += c[i] * std::sin (ph);
            }
            const double p = (re * re + im * im) / c.size();
            if (p > bestPower) { bestPower = p; bestHz = hz; }
        }
        const double depth = juce::jlimit (0.0, 1.0, bestPower / (sigPower / c.size()) * 0.02);
        if (depth > 0.12)
        {
            r.modulationHz = bestHz;
            r.modulationDepth = depth;
        }
    }

    r.valid = true;
    buildRig (r);
    return r;
}

//==============================================================================
void ToneDnaAnalyzer::buildRig (ToneDnaResult& r)
{
    using C = ToneDnaResult::Component;
    r.rig.clear();

    // --- noise gate ----------------------------------------------------------
    if (r.noiseFloorDb > -70.0)
    {
        C c; c.typeId = "noisegate";
        c.description = "Noise floor detected around " + juce::String (r.noiseFloorDb, 0) + " dB";
        c.confidence = juce::jlimit (0.3, 0.9, (r.noiseFloorDb + 70.0) / 25.0 + 0.3);
        c.params["threshold"] = juce::jlimit (-80.0, -35.0, r.noiseFloorDb + 8.0);
        r.rig.push_back (c);
    }

    // --- compressor ----------------------------------------------------------
    {
        const double comp = juce::jlimit (0.0, 1.0, (18.0 - r.dynamicRangeDb) / 14.0);
        if (comp > 0.25)
        {
            C c; c.typeId = "compressor";
            c.description = "Dynamic range " + juce::String (r.dynamicRangeDb, 1) + " dB suggests compression";
            c.confidence = juce::jlimit (0.2, 0.9, comp);
            c.params["threshold"] = -20.0 - comp * 14.0;
            c.params["ratio"] = 2.0 + comp * 4.0;
            c.params["makeup"] = 3.0 + comp * 4.0;
            r.rig.push_back (c);
        }
    }

    // --- drive stage ----------------------------------------------------------
    if (r.saturation > 0.18)
    {
        C c;
        if (r.saturation > 0.75 && r.clipping > 0.35)
        {
            c.typeId = "fuzz";
            c.params["fuzz"] = 4.0 + r.saturation * 5.0;
            c.description = "Heavy asymmetric clipping: fuzz-like saturation";
        }
        else if (r.saturation > 0.5)
        {
            c.typeId = "distortion";
            c.params["drive"] = 3.0 + r.saturation * 6.0;
            c.params["modeSel"] = r.tiltDb > 2.0 ? 2.0 : 1.0;
            c.description = "Strong harmonic density: distortion stage";
        }
        else
        {
            c.typeId = "overdrive";
            c.params["drive"] = 2.0 + r.saturation * 7.0;
            c.description = "Moderate soft clipping: overdrive stage";
        }
        c.confidence = juce::jlimit (0.25, 0.95, r.saturation + 0.15);
        r.rig.push_back (c);
    }

    // --- EQ to match tilt ------------------------------------------------------
    {
        C c; c.typeId = "parametriceq";
        c.description = "Match frequency balance (tilt " + juce::String (r.tiltDb, 1) + " dB)";
        c.confidence = 0.7;
        c.params["low"]  = juce::jlimit (-8.0, 8.0, (r.bandEnergyDb[1] - r.bandEnergyDb[3]) * 0.4);
        c.params["mid1g"] = juce::jlimit (-6.0, 6.0, (r.bandEnergyDb[3] - (r.bandEnergyDb[1] + r.bandEnergyDb[5]) * 0.5) * 0.5);
        c.params["high"] = juce::jlimit (-8.0, 8.0, r.tiltDb * 0.5);
        r.rig.push_back (c);
    }

    // --- modulation -------------------------------------------------------------
    if (r.modulationDepth > 0.12)
    {
        C c;
        c.typeId = r.modulationHz < 1.5 ? "chorus" : "phaser";
        c.description = "Spectral movement at " + juce::String (r.modulationHz, 1) + " Hz";
        c.confidence = juce::jlimit (0.2, 0.8, r.modulationDepth);
        c.params["rate"] = r.modulationHz;
        c.params["depth"] = 30.0 + r.modulationDepth * 50.0;
        c.params["mix"] = 35.0;
        r.rig.push_back (c);
    }

    // --- delay --------------------------------------------------------------------
    if (r.delayStrength > 0.25 && r.delayMs > 60.0)
    {
        C c; c.typeId = "delay";
        c.description = "Echo periodicity at " + juce::String (r.delayMs, 0) + " ms";
        c.confidence = juce::jlimit (0.25, 0.85, r.delayStrength);
        c.params["time"] = r.delayMs;
        c.params["feedback"] = 20.0 + r.delayStrength * 30.0;
        c.params["mix"] = 15.0 + r.delayStrength * 20.0;
        r.rig.push_back (c);
    }

    // --- reverb ----------------------------------------------------------------------
    if (r.tailRatio > 0.35 || r.reverbDecayS > 0.8)
    {
        C c; c.typeId = "reverb";
        c.description = "Reverberant tail ~" + juce::String (r.reverbDecayS, 1) + " s";
        c.confidence = juce::jlimit (0.25, 0.85, r.tailRatio);
        c.params["modeSel"] = r.reverbDecayS > 2.5 ? 1.0 : 0.0;
        c.params["size"] = juce::jlimit (1.0, 10.0, r.reverbDecayS * 2.5);
        c.params["mix"] = juce::jlimit (10.0, 60.0, r.tailRatio * 70.0);
        r.rig.push_back (c);
    }

    // --- amp model -----------------------------------------------------------------------
    {
        C c; c.typeId = "amp";
        int model = 0;
        double conf = 0.5;
        if (r.saturation > 0.75)      { model = 5; conf = 0.6; }  // High gain
        else if (r.saturation > 0.55) { model = 4; conf = 0.55; } // Metal-ish
        else if (r.saturation > 0.38) { model = 2; conf = 0.55; } // Classic rock
        else if (r.saturation > 0.25) { model = 1; conf = 0.5;  } // Crunch
        else if (r.tiltDb > 1.0)      { model = 7; conf = 0.4;  } // Acoustic-bright
        else                          { model = 0; conf = 0.6;  } // Clean
        if (r.bandEnergyDb[0] > r.bandEnergyDb[4] + 6.0) { model = 6; conf = 0.5; } // Bass-heavy
        c.params["model"] = (double) model;
        c.params["gain"] = 2.0 + r.saturation * 6.0;
        c.params["treble"] = juce::jlimit (2.0, 8.0, 5.0 + r.tiltDb * 0.4);
        c.params["bass"] = juce::jlimit (2.0, 8.0, 5.0 - r.tiltDb * 0.3);
        c.description = "Closest amp voicing by gain + spectral balance";
        c.confidence = conf;
        r.rig.push_back (c);
    }

    // --- cabinet ---------------------------------------------------------------------------
    {
        C c; c.typeId = "cab";
        int model = 1; double conf = 0.4;
        if (r.rolloffHz <= 4000.0)      { model = 2; conf = 0.65; }
        else if (r.rolloffHz <= 8000.0) { model = 1; conf = 0.6; }
        else                            { model = 5; conf = 0.45; } // open/DI-like top end
        if (r.bandEnergyDb[0] > r.bandEnergyDb[4] + 6.0) model = 3;
        c.params["model"] = (double) model;
        c.params["mic"] = juce::jlimit (0.0, 10.0, (12000.0 - r.rolloffHz) / 1500.0);
        c.description = "High-frequency rolloff near " + juce::String (r.rolloffHz / 1000.0, 1) + " kHz";
        c.confidence = conf;
        r.rig.push_back (c);
    }
}

//==============================================================================
void ToneDnaAnalyzer::applyRig (const ToneDnaResult& result, AudioEngine& engine)
{
    engine.editChain ([&] (PedalChain& chain)
    {
        chain.clear();
        for (const auto& comp : result.rig)
        {
            if (auto pedal = PedalFactory::instance().create (comp.typeId))
            {
                for (const auto& [id, value] : comp.params)
                    if (auto* p = pedal->param (id))
                        p->set (value);
                chain.addPedal (std::move (pedal));
            }
        }
    });
}

} // namespace vp
