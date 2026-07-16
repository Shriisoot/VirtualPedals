#include "AudioEngine.h"

namespace vp
{

AudioEngine::AudioEngine()
{
    visBuf.resize (1 << 15, 0.0f);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

void AudioEngine::initialise()
{
    // stereo out, up to 2 ins (guitar interfaces are usually mono in)
    const auto err = deviceManager.initialiseWithDefaultDevices (2, 2);
    if (err.isNotEmpty())
        juce::Logger::writeToLog ("Audio device init: " + err);
    deviceManager.addAudioCallback (this);
    refreshMidiInputs();
}

void AudioEngine::shutdown()
{
    deviceManager.removeAudioCallback (this);
    for (const auto& id : openedMidiInputs)
        deviceManager.removeMidiInputDeviceCallback (id, this);
    openedMidiInputs.clear();
    deviceManager.closeAudioDevice();
}

void AudioEngine::refreshMidiInputs()
{
    for (const auto& id : openedMidiInputs)
        deviceManager.removeMidiInputDeviceCallback (id, this);
    openedMidiInputs.clear();

    for (const auto& dev : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled (dev.identifier, true);
        deviceManager.addMidiInputDeviceCallback (dev.identifier, this);
        openedMidiInputs.add (dev.identifier);
    }
}

int AudioEngine::getLatencySamples() const
{
    int total = currentBlockSize;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        total = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples();
    return total + const_cast<PedalChain&> (chain).getTotalLatency();
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();
    currentBlockSize = device->getCurrentBufferSizeSamples();

    const juce::ScopedLock sl (processLock);
    workBuf.setSize (2, juce::jmax (currentBlockSize, 16), false, false, true);
    recScratch.setSize (2, juce::jmax (currentBlockSize, 8192), false, false, true);
    inputDc.prepare (currentSampleRate);
    outLimiter.prepare (currentSampleRate);
    outLimiter.ceiling = 0.98;
    chain.prepareToPlay (currentSampleRate, juce::jmax (currentBlockSize, 16));
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    if (! powerOn.load (std::memory_order_relaxed))
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        inPeak.store (inPeak.load() * 0.8f);  inRms.store (0.0f);
        outPeak.store (outPeak.load() * 0.8f); outRms.store (0.0f);
        return;
    }

    if (numSamples > workBuf.getNumSamples())
        workBuf.setSize (2, numSamples, false, false, true); // defensive; normally preallocated

    auto* wl = workBuf.getWritePointer (0);
    auto* wr = workBuf.getWritePointer (1);

    // ---- input stage: float -> double, mono sources duplicated to stereo
    const bool haveIn0 = numInputChannels > 0 && inputChannelData[0] != nullptr;
    const bool haveIn1 = numInputChannels > 1 && inputChannelData[1] != nullptr;
    const double inGain = juce::Decibels::decibelsToGain (inputGainDb.load());
    const bool mute = muteInput.load();

    float ipk = 0.0f; double irms = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        double L = haveIn0 && ! mute ? (double) inputChannelData[0][i] : 0.0;
        double R = haveIn1 && ! mute ? (double) inputChannelData[1][i] : L;
        L *= inGain; R *= inGain;
        const float a = (float) juce::jmax (std::abs (L), std::abs (R));
        ipk = juce::jmax (ipk, a);
        irms += a * a;
        wl[i] = L; wr[i] = R;
    }
    inPeak.store (juce::jmax (ipk, inPeak.load() * 0.86f));
    inRms.store ((float) std::sqrt (irms / juce::jmax (1, numSamples)));

    inputDc.processBuffer (workBuf);

    // ---- reference playback (Tone DNA A/B): replaces the live signal
    if (referencePlaying.load() && referenceLoaded.load())
    {
        const int len = reference.getNumSamples();
        for (int i = 0; i < numSamples; ++i)
        {
            const int p = referencePos;
            wl[i] = (double) reference.getSample (0, p);
            wr[i] = (double) reference.getSample (juce::jmin (1, reference.getNumChannels() - 1), p);
            referencePos = (referencePos + 1) % juce::jmax (1, len);
        }
    }
    else
    {
        // ---- pedal chain
        juce::AudioBuffer<double> view (workBuf.getArrayOfWritePointers(), 2, numSamples);
        const juce::ScopedLock sl (processLock);
        chain.process (view);
    }

    // ---- output stage
    const double outGain = juce::Decibels::decibelsToGain (masterGainDb.load());
    workBuf.applyGain (0, 0, numSamples, outGain);
    workBuf.applyGain (1, 0, numSamples, outGain);

    if (limiterOn.load())
    {
        juce::AudioBuffer<double> view (workBuf.getArrayOfWritePointers(), 2, numSamples);
        outLimiter.processBuffer (view);
    }

    float opk = 0.0f; double orms = 0.0; int clips = 0;
    const int wmask = (int) visBuf.size() - 1;
    int w = visWrite.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        double L = wl[i], R = wr[i];
        if (std::abs (L) >= 1.0 || std::abs (R) >= 1.0) ++clips;
        L = juce::jlimit (-1.0, 1.0, L); // final hard clip protection
        R = juce::jlimit (-1.0, 1.0, R);
        wl[i] = L; wr[i] = R;

        const float mono = (float) ((L + R) * 0.5);
        visBuf[(size_t) (w & wmask)] = mono;
        ++w;

        const float a = (float) juce::jmax (std::abs (L), std::abs (R));
        opk = juce::jmax (opk, a);
        orms += a * a;
    }
    visWrite.store (w, std::memory_order_release);

    outPeak.store (juce::jmax (opk, outPeak.load() * 0.86f));
    outRms.store ((float) std::sqrt (orms / juce::jmax (1, numSamples)));
    if (clips > 0)
        clipCount.store (clipCount.load() + (float) clips);

    // ---- recording tap (post limiter/clip guard = exactly what you hear)
    if (recording.load (std::memory_order_relaxed) && numSamples <= recScratch.getNumSamples())
    {
        const juce::SpinLock::ScopedTryLockType tl (writerLock);
        if (tl.isLocked() && threadedWriter != nullptr)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* dst = recScratch.getWritePointer (ch);
                const auto* src = ch == 0 ? wl : wr;
                for (int i = 0; i < numSamples; ++i)
                    dst[i] = (float) src[i];
            }
            const float* chans[2] = { recScratch.getReadPointer (0), recScratch.getReadPointer (1) };
            if (threadedWriter->write (chans, numSamples))
                recordedSamples.fetch_add (numSamples);
        }
    }

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;
        const auto* src = ch == 0 ? wl : wr;
        for (int i = 0; i < numSamples; ++i)
            outputChannelData[ch][i] = (float) src[i];
    }
}

//==============================================================================
int AudioEngine::readVisualisationSamples (float* dest, int maxSamples)
{
    const int w = visWrite.load (std::memory_order_acquire);
    const int mask = (int) visBuf.size() - 1;
    int available = w - visRead;
    if (available < 0 || available > (int) visBuf.size())
    {
        visRead = w - juce::jmin ((int) visBuf.size(), 4096);
        available = w - visRead;
    }
    const int toRead = juce::jmin (available, maxSamples);
    for (int i = 0; i < toRead; ++i)
        dest[i] = visBuf[(size_t) ((visRead + i) & mask)];
    visRead += toRead;
    return toRead;
}

//==============================================================================
void AudioEngine::loadReference (juce::AudioBuffer<float>&& data, double fileSampleRate)
{
    // resample to engine rate if needed (linear — analysis quality is unaffected)
    juce::AudioBuffer<float> resampled;
    if (std::abs (fileSampleRate - currentSampleRate) > 1.0 && fileSampleRate > 1000.0)
    {
        const double ratio = fileSampleRate / currentSampleRate;
        const int outLen = (int) (data.getNumSamples() / ratio);
        resampled.setSize (data.getNumChannels(), outLen);
        for (int ch = 0; ch < data.getNumChannels(); ++ch)
        {
            auto* dst = resampled.getWritePointer (ch);
            auto* src = data.getReadPointer (ch);
            for (int i = 0; i < outLen; ++i)
            {
                const double pos = i * ratio;
                const int i0 = (int) pos;
                const int i1 = juce::jmin (i0 + 1, data.getNumSamples() - 1);
                dst[i] = src[i0] + (float) (pos - i0) * (src[i1] - src[i0]);
            }
        }
    }

    const juce::ScopedLock sl (processLock);
    reference = resampled.getNumSamples() > 0 ? std::move (resampled) : std::move (data);
    referencePos = 0;
    referenceLoaded.store (reference.getNumSamples() > 0);
}

void AudioEngine::setReferencePlaying (bool shouldPlay)
{
    referencePos = 0;
    referencePlaying.store (shouldPlay && referenceLoaded.load());
}

//==============================================================================
juce::var AudioEngine::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("inputGainDb", inputGainDb.load());
    obj->setProperty ("masterGainDb", masterGainDb.load());
    obj->setProperty ("limiterOn", limiterOn.load());
    obj->setProperty ("chain", const_cast<PedalChain&> (chain).toVar());
    return juce::var (obj);
}

void AudioEngine::fromVar (const juce::var& v)
{
    inputGainDb.store ((double) v.getProperty ("inputGainDb", 0.0));
    masterGainDb.store ((double) v.getProperty ("masterGainDb", 0.0));
    limiterOn.store ((bool) v.getProperty ("limiterOn", true));
    const juce::ScopedLock sl (processLock);
    chain.fromVar (v.getProperty ("chain", juce::var()));
}

//==============================================================================
juce::File AudioEngine::getRecordingsDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Virtual Pedals Recordings");
}

bool AudioEngine::startRecording()
{
    if (recording.load())
        return true;

    auto dir = getRecordingsDir();
    dir.createDirectory();
    recordingFile = dir.getChildFile ("VP-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav");

    std::unique_ptr<juce::FileOutputStream> stream (recordingFile.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto* writer = wav.createWriterFor (stream.get(), currentSampleRate, 2, 24, {}, 0);
    if (writer == nullptr)
        return false;
    stream.release(); // writer took ownership

    writeThread.startThread();
    auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, writeThread, 1 << 17);
    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        threadedWriter = std::move (tw);
    }
    recordedSamples.store (0);
    recording.store (true);
    return true;
}

juce::File AudioEngine::stopRecording()
{
    recording.store (false);
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        old = std::move (threadedWriter);
    }
    old = nullptr; // flush + close the WAV
    return recordingFile;
}

//==============================================================================
void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isController() && onMidiCC != nullptr)
        onMidiCC (message.getControllerNumber(), (float) message.getControllerValue() / 127.0f);
    else if (message.isProgramChange() && onMidiProgramChange != nullptr)
        onMidiProgramChange (message.getProgramChangeNumber());
}

} // namespace vp
