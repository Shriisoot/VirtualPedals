#pragma once
#include "../Engine/AudioEngine.h"

namespace vp
{

// JSON presets on disk + factory presets + favourites + autosave + undo/redo.
class PresetManager : private juce::Timer
{
public:
    explicit PresetManager (AudioEngine& engineToUse);
    ~PresetManager() override;

    //==============================================================================
    struct PresetInfo
    {
        juce::String name;
        juce::File file;        // invalid for factory presets
        bool isFactory = false;
        bool isFavourite = false;
        bool isSong = false;    // "Song Tones" library entry
    };

    juce::Array<PresetInfo> getPresets (const juce::String& searchFilter = {}) const;

    void savePreset (const juce::String& name);
    bool loadPreset (const PresetInfo& info);
    bool loadPresetByIndex (int index);                  // for MIDI program change
    void deletePreset (const PresetInfo& info);
    void setFavourite (const juce::String& name, bool fav);

    bool importPreset (const juce::File& file);
    bool exportPreset (const PresetInfo& info, const juce::File& dest);

    juce::String getCurrentPresetName() const { return currentName; }

    //==============================================================================
    // Undo/redo of whole-rig state (chain edits, pedal params at edit boundaries)
    void pushUndoState (const juce::String& actionName);
    bool undo();
    bool redo();
    bool canUndo() const { return undoStack.size() > 0; }
    bool canRedo() const { return redoStack.size() > 0; }

    //==============================================================================
    void saveSession();      // autosave current rig + restore on next launch
    void restoreSession();

    std::function<void()> onPresetChanged;

    static juce::File getPresetsDir();
    static juce::File getSessionFile();

private:
    void timerCallback() override;   // debounced autosave
    juce::var factoryPresetVar (int index) const;

    AudioEngine& engine;
    juce::String currentName { "Init" };
    juce::StringArray favourites;
    juce::Array<juce::var> undoStack, redoStack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace vp
