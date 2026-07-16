#pragma once
#include "../Engine/AudioEngine.h"

namespace vp
{

//==============================================================================
struct ToneDnaResult
{
    bool valid = false;
    juce::String error;

    // Measured features (all local DSP analysis, no network)
    std::array<double, 8> bandEnergyDb {};   // 60-120,120-250,250-500,500-1k,1-2k,2-4k,4-8k,8-16k
    double tiltDb = 0.0;                     // high vs low balance
    double crestDb = 0.0;                    // peak/rms
    double dynamicRangeDb = 0.0;             // window-RMS spread
    double saturation = 0.0;                 // 0..1 drive estimate
    double clipping = 0.0;                   // 0..1 hard-clip evidence
    double stereoWidth = 0.0;                // 0..1 side/mid
    double tailRatio = 0.0;                  // 0..1 reverb wash evidence
    double reverbDecayS = 0.0;
    double delayMs = 0.0;
    double delayStrength = 0.0;              // 0..1
    double modulationHz = 0.0;
    double modulationDepth = 0.0;            // 0..1
    double noiseFloorDb = -120.0;
    double rolloffHz = 20000.0;              // cab-style high rolloff
    double lengthSeconds = 0.0;

    struct Component
    {
        juce::String typeId;                 // pedal factory type
        juce::String description;
        double confidence = 0.0;             // 0..1
        std::map<juce::String, double> params;
    };
    std::vector<Component> rig;
};

//==============================================================================
// Offline analysis of an imported audio file -> closest-matching virtual rig.
class ToneDnaAnalyzer : public juce::Thread
{
public:
    ToneDnaAnalyzer() : juce::Thread ("ToneDNA") {}
    ~ToneDnaAnalyzer() override { stopThread (4000); }

    // Async: reads + analyses on a worker thread, fires onComplete on the message thread.
    void analyzeFile (const juce::File& file);
    std::function<void (const ToneDnaResult&)> onComplete;
    std::function<void (float)> onProgress;

    // Loads the file used for A/B into the engine's reference player.
    static bool loadReferenceIntoEngine (const juce::File& file, AudioEngine& engine);

    // Build the suggested rig in the live engine.
    static void applyRig (const ToneDnaResult& result, AudioEngine& engine);

    static ToneDnaResult analyzeBuffer (const juce::AudioBuffer<float>& audio, double sampleRate);

    void run() override;

private:
    static void buildRig (ToneDnaResult& r);
    juce::File pendingFile;
};

} // namespace vp
