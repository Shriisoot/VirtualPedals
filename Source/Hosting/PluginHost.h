#pragma once
#include "../Engine/Pedal.h"

namespace vp
{

//==============================================================================
// A chain pedal wrapping a hosted VST3 plugin instance.
class VstPedal : public Pedal
{
public:
    VstPedal (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& d)
        : Pedal ("vst3", d.name, "Plugin", true),
          instance (std::move (inst)), desc (d)
    {
    }

    ~VstPedal() override
    {
        closeEditor();
        if (instance)
            instance->releaseResources();
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        if (instance == nullptr)
            return;
        instance->setPlayConfigDetails (2, 2, sampleRate, maxBlockSize);
        instance->prepareToPlay (sampleRate, maxBlockSize);
        floatBuf.setSize (2, maxBlockSize);
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        if (instance == nullptr)
            return;
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* dst = floatBuf.getWritePointer (ch);
            auto* src = buf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                dst[i] = (float) src[i];
        }
        juce::AudioBuffer<float> view (floatBuf.getArrayOfWritePointers(), 2, n);
        juce::MidiBuffer midi;
        instance->processBlock (view, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* dst = buf.getWritePointer (ch);
            auto* src = floatBuf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                dst[i] = (double) src[i];
        }
    }

    int getLatencySamples() const override
    {
        return instance != nullptr ? instance->getLatencySamples() : 0;
    }

    //==============================================================================
    void openEditor();
    void closeEditor();
    bool hasEditor() const { return instance != nullptr && instance->hasEditor(); }

    juce::var toVar() const override
    {
        auto v = Pedal::toVar();
        if (auto* obj = v.getDynamicObject())
        {
            obj->setProperty ("pluginDesc", desc.createXml()->toString());
            if (instance != nullptr)
            {
                juce::MemoryBlock state;
                instance->getStateInformation (state);
                obj->setProperty ("pluginState", state.toBase64Encoding());
            }
        }
        return v;
    }

    void fromVar (const juce::var& v) override
    {
        Pedal::fromVar (v);
        const auto stateStr = v.getProperty ("pluginState", "").toString();
        if (stateStr.isNotEmpty() && instance != nullptr)
        {
            juce::MemoryBlock state;
            if (state.fromBase64Encoding (stateStr))
                instance->setStateInformation (state.getData(), (int) state.getSize());
        }
    }

    const juce::PluginDescription& getDescription() const { return desc; }

private:
    class EditorWindow;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription desc;
    juce::AudioBuffer<float> floatBuf;
    std::unique_ptr<juce::DocumentWindow> editorWindow;
};

//==============================================================================
// VST3 discovery, browsing and instantiation. Uses JUCE's dead-man's-pedal file:
// if a plugin crashes the scanner, it is blacklisted on the next run instead of
// killing the app again.
class PluginHost : private juce::Timer
{
public:
    PluginHost();
    ~PluginHost() override;

    // Async scan of the standard VST3 folders (+ user-added paths).
    void startScan (std::function<void (float, juce::String)> progress,
                    std::function<void()> done);
    bool isScanning() const { return scanning.load(); }

    juce::Array<juce::PluginDescription> getPlugins (const juce::String& searchFilter = {}) const;

    void setFavourite (const juce::String& pluginId, bool fav);
    bool isFavourite (const juce::String& pluginId) const;

    std::unique_ptr<VstPedal> createPlugin (const juce::PluginDescription& desc, double sr, int blockSize);

    // Scan one specific .vst3 file/bundle; returns how many plugins were found in it.
    int scanSingleFile (const juce::File& file);

    // Restore a hosted plugin from saved chain state (installed as PedalChain hook).
    std::unique_ptr<Pedal> restoreFromVar (const juce::var& v, double sr, int blockSize);

    juce::FileSearchPath getSearchPath() const;
    void addSearchPath (const juce::File& dir);

private:
    void timerCallback() override;
    juce::File listFile() const;
    juce::File deadMansFile() const;
    juce::File favFile() const;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;
    juce::StringArray favourites, extraPaths;
    std::atomic<bool> scanning { false };

    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    std::function<void (float, juce::String)> progressCb;
    std::function<void()> doneCb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHost)
};

} // namespace vp
