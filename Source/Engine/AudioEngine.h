#pragma once
#include "PedalChain.h"
#include "DspUtil.h"

namespace vp
{

// Owns the device, the master signal path and the pedal chain.
// Signal flow: input gain -> DC block -> [chain] -> master gain -> limiter -> clip guard -> out
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    void initialise();
    void shutdown();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    //==============================================================================
    // All structural chain edits must go through this lock.
    template <typename Fn>
    void editChain (Fn&& fn)
    {
        const juce::ScopedLock sl (processLock);
        fn (chain);
    }

    PedalChain& getChainUnsafe() { return chain; }
    const juce::CriticalSection& getProcessLock() const { return processLock; }

    //==============================================================================
    // Master parameters (thread-safe)
    std::atomic<double> inputGainDb  { 0.0 };
    std::atomic<double> masterGainDb { 0.0 };
    std::atomic<bool>   limiterOn    { true };
    std::atomic<bool>   muteInput    { false };
    std::atomic<bool>   powerOn      { true };   // master on/off: off = silence + zero DSP cost

    //==============================================================================
    // Recording (24-bit WAV of the processed output)
    bool startRecording();
    juce::File stopRecording();
    bool isRecording() const { return recording.load(); }
    double getRecordedSeconds() const { return (double) recordedSamples.load() / juce::jmax (1.0, currentSampleRate); }
    static juce::File getRecordingsDir();

    // Meters (written by audio thread, read by UI)
    std::atomic<float> inPeak { 0.0f }, inRms { 0.0f }, outPeak { 0.0f }, outRms { 0.0f };
    std::atomic<float> clipCount { 0.0f };

    double getCpuUsage() const { return deviceManager.getCpuUsage(); }
    int getLatencySamples() const;
    double getSampleRate() const { return currentSampleRate; }

    //==============================================================================
    // Visualisation taps: UI thread pulls mono samples for spectrum/scope.
    int readVisualisationSamples (float* dest, int maxSamples);

    //==============================================================================
    // Reference player for Tone DNA A/B comparison
    void loadReference (juce::AudioBuffer<float>&& data, double fileSampleRate);
    void setReferencePlaying (bool shouldPlay);
    bool isReferencePlaying() const { return referencePlaying.load(); }
    bool hasReference() const       { return referenceLoaded.load(); }

    //==============================================================================
    // MIDI
    std::function<void (int cc, float value01)> onMidiCC;              // called on midi thread
    std::function<void (int program)> onMidiProgramChange;

    //==============================================================================
    // Session state
    juce::var toVar() const;
    void fromVar (const juce::var& v);

    //==============================================================================
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    void refreshMidiInputs();

private:
    juce::AudioDeviceManager deviceManager;
    juce::CriticalSection processLock;
    PedalChain chain;

    juce::AudioBuffer<double> workBuf;
    dsp::DCBlocker inputDc;
    dsp::Limiter outLimiter;

    // visualisation fifo (single-writer audio thread, single-reader UI thread)
    std::vector<float> visBuf;
    std::atomic<int> visWrite { 0 };
    int visRead = 0;

    // reference playback
    juce::AudioBuffer<float> reference;
    std::atomic<bool> referencePlaying { false }, referenceLoaded { false };
    int referencePos = 0;

    // recording
    juce::TimeSliceThread writeThread { "vp-recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::SpinLock writerLock;
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> recordedSamples { 0 };
    juce::File recordingFile;
    juce::AudioBuffer<float> recScratch;

    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;
    juce::StringArray openedMidiInputs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace vp
