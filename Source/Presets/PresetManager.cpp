#include "PresetManager.h"
#include "../Engine/PedalFactory.h"

namespace vp
{

//==============================================================================
// Factory presets are built programmatically: name + list of {type, params, bypass}.
struct FactoryDef
{
    const char* name;
    const char* json; // chain description
};

static const FactoryDef factoryDefs[] =
{
    { "Clean Sparkle",
      R"([{"type":"compressor","params":{"threshold":-28,"ratio":3,"makeup":4}},
          {"type":"chorus","params":{"rate":0.7,"depth":35,"mix":35}},
          {"type":"reverb","params":{"modeSel":0,"size":4,"mix":22}},
          {"type":"amp","params":{"model":0,"gain":3,"master":-8}},
          {"type":"cab","params":{"model":0}}])" },

    { "Blues Breaker",
      R"([{"type":"overdrive","params":{"drive":3.5,"voice":2,"tone":6,"mix":100}},
          {"type":"reverb","params":{"modeSel":0,"size":3,"mix":15}},
          {"type":"amp","params":{"model":3,"gain":4.5,"master":-8}},
          {"type":"cab","params":{"model":0,"mic":4}}])" },

    { "Classic Rock Crunch",
      R"([{"type":"overdrive","params":{"drive":5,"voice":1,"mix":100}},
          {"type":"amp","params":{"model":2,"gain":6,"master":-9}},
          {"type":"cab","params":{"model":2,"mic":3}},
          {"type":"delay","params":{"time":330,"feedback":22,"mix":18}}])" },

    { "Metal Rhythm",
      R"([{"type":"noisegate","params":{"threshold":-52}},
          {"type":"distortion","params":{"drive":7,"modeSel":2,"body":3,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":7,"master":-10}},
          {"type":"cab","params":{"model":2,"mic":2}},
          {"type":"parametriceq","params":{"mid1f":550,"mid1g":-4}}])" },

    { "Ambient Wash",
      R"([{"type":"compressor","params":{"threshold":-30,"ratio":3}},
          {"type":"sustain","params":{"modeSel":6,"amount":7,"mix":60}},
          {"type":"delay","params":{"time":540,"feedback":45,"modeSel":0,"mix":35}},
          {"type":"reverb","params":{"modeSel":4,"size":8,"shimmer":40,"mix":55}},
          {"type":"amp","params":{"model":0,"gain":2.5,"master":-8}},
          {"type":"cab","params":{"model":4}}])" },

    { "Funk Envelope",
      R"([{"type":"compressor","params":{"threshold":-26,"ratio":4,"attack":2}},
          {"type":"envfilter","params":{"sens":6,"q":5,"type":1,"mix":100}},
          {"type":"amp","params":{"model":0,"gain":3.5,"master":-8}},
          {"type":"cab","params":{"model":0}}])" },

    { "Octave Fuzz Lead",
      R"([{"type":"fuzz","params":{"fuzz":7,"octave":1,"mix":100}},
          {"type":"amp","params":{"model":2,"gain":6,"master":-10}},
          {"type":"cab","params":{"model":2}},
          {"type":"delay","params":{"time":420,"feedback":30,"modeSel":1,"mix":25}}])" },

    { "Infinite Pad",
      R"([{"type":"sustain","params":{"modeSel":2,"amount":8,"swell":900,"mix":70}},
          {"type":"spectral","params":{"smear":7,"mix":40}},
          {"type":"reverb","params":{"modeSel":3,"size":9,"mix":60}},
          {"type":"amp","params":{"model":0,"gain":2,"master":-9}}])" },

    { "Rotary Drive",
      R"([{"type":"overdrive","params":{"drive":3,"voice":0,"mix":100}},
          {"type":"rotary","params":{"speed":1,"mix":100}},
          {"type":"amp","params":{"model":1,"gain":4,"master":-8}},
          {"type":"cab","params":{"model":1}}])" },

    { "Acoustic Studio",
      R"([{"type":"noisereduction","params":{"amount":10}},
          {"type":"compressor","params":{"threshold":-24,"ratio":2.5,"attack":10}},
          {"type":"parametriceq","params":{"lowcut":80,"mid1f":250,"mid1g":-2,"high":2}},
          {"type":"reverb","params":{"modeSel":1,"size":4,"mix":18}},
          {"type":"amp","params":{"model":7,"gain":3,"master":-6}},
          {"type":"cab","params":{"model":5}}])" },
};

//==============================================================================
// Song Tones: rigs voiced after classic (mostly Metallica) guitar tones.
// Every metal rig starts with the Active Metal (EMG-style) pickup sim into a
// gate, a screamer-style boost, and a tight high-gain amp into a 4x12.
static const FactoryDef songDefs[] =
{
    { "Master of Puppets",
      R"([{"type":"pickup","params":{"type":9,"tight":75}},
          {"type":"noisegate","params":{"threshold":-48,"release":60}},
          {"type":"overdrive","params":{"drive":1.5,"voice":1,"tone":6.5,"level":5,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":7,"bass":6.5,"mid":2,"treble":7,"presence":6.5,"master":-10,"sag":1}},
          {"type":"cab","params":{"model":2,"mic":2}},
          {"type":"parametriceq","params":{"mid1f":500,"mid1g":-3,"high":1.5}}])" },

    { "Battery",
      R"([{"type":"pickup","params":{"type":9,"tight":80}},
          {"type":"noisegate","params":{"threshold":-46,"release":50}},
          {"type":"overdrive","params":{"drive":2,"voice":1,"tone":7,"level":5,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":7.5,"bass":6.5,"mid":1.8,"treble":7,"presence":7,"master":-10,"sag":0.5}},
          {"type":"cab","params":{"model":2,"mic":2}},
          {"type":"parametriceq","params":{"mid1f":550,"mid1g":-3.5,"high":2}}])" },

    { "Ride the Lightning",
      R"([{"type":"pickup","params":{"type":8,"tight":60}},
          {"type":"noisegate","params":{"threshold":-50}},
          {"type":"overdrive","params":{"drive":2.5,"voice":1,"tone":6,"level":4,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":6,"bass":6,"mid":3.5,"treble":6.5,"presence":6,"master":-10,"sag":2}},
          {"type":"cab","params":{"model":2,"mic":3}},
          {"type":"reverb","params":{"modeSel":0,"size":2.5,"mix":8}}])" },

    { "Creeping Death",
      R"([{"type":"pickup","params":{"type":9,"tight":70}},
          {"type":"noisegate","params":{"threshold":-48}},
          {"type":"overdrive","params":{"drive":2,"voice":1,"tone":6.5,"level":4.5,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":6.8,"bass":6.5,"mid":2.8,"treble":6.8,"presence":6.2,"master":-10,"sag":1.5}},
          {"type":"cab","params":{"model":2,"mic":2.5}}])" },

    { "Seek & Destroy",
      R"([{"type":"pickup","params":{"type":8,"tight":55}},
          {"type":"noisegate","params":{"threshold":-52}},
          {"type":"distortion","params":{"drive":5.5,"modeSel":0,"edge":6,"body":5,"level":-4,"mix":100}},
          {"type":"amp","params":{"model":2,"gain":6,"bass":5.5,"mid":4.5,"treble":6.5,"presence":5.5,"master":-9,"sag":3}},
          {"type":"cab","params":{"model":2,"mic":4}},
          {"type":"reverb","params":{"modeSel":0,"size":3,"mix":10}}])" },

    { "For Whom the Bell Tolls",
      R"([{"type":"pickup","params":{"type":9,"tight":60}},
          {"type":"noisegate","params":{"threshold":-48}},
          {"type":"overdrive","params":{"drive":2,"voice":1,"tone":6,"level":4,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":6.5,"bass":7,"mid":3.2,"treble":6.2,"presence":5.8,"master":-10,"sag":2}},
          {"type":"cab","params":{"model":2,"mic":3}}])" },

    { "Fade to Black (Lead)",
      R"([{"type":"pickup","params":{"type":9,"tight":55}},
          {"type":"noisegate","params":{"threshold":-54}},
          {"type":"overdrive","params":{"drive":3,"voice":1,"tone":6,"level":4,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":6.5,"bass":5.5,"mid":5,"treble":6,"presence":6,"master":-10,"sag":2}},
          {"type":"cab","params":{"model":2,"mic":3.5}},
          {"type":"delay","params":{"time":380,"feedback":30,"modeSel":1,"mix":22}},
          {"type":"reverb","params":{"modeSel":1,"size":5,"mix":18}}])" },

    { "One (Clean Intro)",
      R"([{"type":"pickup","params":{"type":7}},
          {"type":"compressor","params":{"threshold":-26,"ratio":3,"makeup":3}},
          {"type":"chorus","params":{"rate":0.6,"depth":38,"width":90,"mix":40}},
          {"type":"amp","params":{"model":0,"gain":2.5,"bass":5.5,"mid":4.5,"treble":5.5,"presence":4,"master":-8}},
          {"type":"cab","params":{"model":0,"mic":4}},
          {"type":"delay","params":{"time":440,"feedback":26,"modeSel":0,"mix":18}},
          {"type":"reverb","params":{"modeSel":1,"size":5.5,"mix":22}}])" },

    { "One (Heavy)",
      R"([{"type":"pickup","params":{"type":9,"tight":85}},
          {"type":"noisegate","params":{"threshold":-45,"release":45}},
          {"type":"overdrive","params":{"drive":1.5,"voice":1,"tone":7,"level":5.5,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":7.8,"bass":6.5,"mid":1.5,"treble":7.2,"presence":7,"master":-11,"sag":0.5}},
          {"type":"cab","params":{"model":2,"mic":1.5}},
          {"type":"parametriceq","params":{"mid1f":600,"mid1g":-4,"high":2}}])" },

    { "Blackened",
      R"([{"type":"pickup","params":{"type":9,"tight":90}},
          {"type":"noisegate","params":{"threshold":-44,"release":40}},
          {"type":"overdrive","params":{"drive":1.5,"voice":1,"tone":7,"level":6,"mix":100}},
          {"type":"amp","params":{"model":4,"gain":7.5,"bass":6,"mid":1,"treble":7.5,"presence":7.5,"master":-11,"sag":0}},
          {"type":"cab","params":{"model":2,"mic":1.5}},
          {"type":"parametriceq","params":{"mid1f":650,"mid1g":-5,"high":2.5,"lowcut":70}}])" },

    { "Enter Sandman",
      R"([{"type":"pickup","params":{"type":9,"tight":65}},
          {"type":"noisegate","params":{"threshold":-50}},
          {"type":"overdrive","params":{"drive":2,"voice":1,"tone":6,"level":4.5,"mix":100}},
          {"type":"amp","params":{"model":5,"gain":6.5,"bass":6.5,"mid":3,"treble":6.5,"presence":6.5,"master":-10,"sag":1.5}},
          {"type":"cab","params":{"model":2,"mic":3}},
          {"type":"reverb","params":{"modeSel":0,"size":3,"mix":9}}])" },

    { "Sad But True",
      R"([{"type":"pickup","params":{"type":9,"tight":50}},
          {"type":"noisegate","params":{"threshold":-48}},
          {"type":"overdrive","params":{"drive":2,"voice":1,"tone":5.5,"level":4.5,"mix":100}},
          {"type":"amp","params":{"model":5,"gain":7,"bass":7.5,"mid":3.5,"treble":6,"presence":6,"master":-10,"sag":2}},
          {"type":"cab","params":{"model":2,"mic":3.5}},
          {"type":"parametriceq","params":{"low":1.5,"mid1f":800,"mid1g":-1.5}}])" },

    { "Wherever I May Roam",
      R"([{"type":"pickup","params":{"type":9,"tight":60}},
          {"type":"noisegate","params":{"threshold":-50}},
          {"type":"overdrive","params":{"drive":2,"voice":1,"tone":6,"level":4.5,"mix":100}},
          {"type":"amp","params":{"model":5,"gain":6.8,"bass":6.5,"mid":3.2,"treble":6.5,"presence":6.5,"master":-10,"sag":1.5}},
          {"type":"cab","params":{"model":2,"mic":3}},
          {"type":"delay","params":{"time":330,"feedback":18,"modeSel":1,"mix":12}}])" },

    { "Fuel",
      R"([{"type":"pickup","params":{"type":9,"tight":80}},
          {"type":"noisegate","params":{"threshold":-46,"release":50}},
          {"type":"overdrive","params":{"drive":2.5,"voice":1,"tone":7,"level":5,"mix":100}},
          {"type":"amp","params":{"model":5,"gain":7.5,"bass":6.5,"mid":2.8,"treble":7,"presence":7.2,"master":-10,"sag":0.5}},
          {"type":"cab","params":{"model":2,"mic":2}},
          {"type":"parametriceq","params":{"mid2f":4000,"mid2g":2,"mid1f":500,"mid1g":-2}}])" },

    { "Hardwired",
      R"([{"type":"pickup","params":{"type":9,"tight":95}},
          {"type":"noisegate","params":{"threshold":-42,"release":35}},
          {"type":"overdrive","params":{"drive":2,"voice":1,"tone":7,"level":5.5,"mix":100}},
          {"type":"amp","params":{"model":5,"gain":8,"bass":6,"mid":3,"treble":7,"presence":7,"master":-11,"sag":0}},
          {"type":"cab","params":{"model":2,"mic":1.5}}])" },

    { "Nothing Else Matters (Clean)",
      R"([{"type":"pickup","params":{"type":7}},
          {"type":"compressor","params":{"threshold":-28,"ratio":2.5,"makeup":4,"attack":8}},
          {"type":"amp","params":{"model":0,"gain":2,"bass":5.5,"mid":5,"treble":5,"presence":4,"master":-7}},
          {"type":"cab","params":{"model":0,"mic":4.5}},
          {"type":"reverb","params":{"modeSel":1,"size":5,"predelay":25,"mix":24}}])" },

    { "Welcome Home Sanitarium (Clean)",
      R"([{"type":"pickup","params":{"type":7}},
          {"type":"compressor","params":{"threshold":-26,"ratio":3,"makeup":3}},
          {"type":"chorus","params":{"rate":0.5,"depth":45,"width":100,"mix":45}},
          {"type":"amp","params":{"model":0,"gain":2.8,"bass":5,"mid":4.5,"treble":5.5,"presence":4.5,"master":-8}},
          {"type":"cab","params":{"model":0,"mic":4}},
          {"type":"delay","params":{"time":420,"feedback":22,"modeSel":0,"mix":15}},
          {"type":"reverb","params":{"modeSel":1,"size":4.5,"mix":16}}])" },

    { "The Unforgiven (Clean)",
      R"([{"type":"pickup","params":{"type":0}},
          {"type":"compressor","params":{"threshold":-27,"ratio":2.8,"makeup":3.5}},
          {"type":"amp","params":{"model":0,"gain":2.2,"bass":5,"mid":4.8,"treble":5.2,"presence":4,"master":-7}},
          {"type":"cab","params":{"model":4,"mic":4}},
          {"type":"reverb","params":{"modeSel":1,"size":5.5,"predelay":30,"mix":26}}])" },
};

//==============================================================================
PresetManager::PresetManager (AudioEngine& engineToUse) : engine (engineToUse)
{
    getPresetsDir().createDirectory();
    const auto favFile = getPresetsDir().getChildFile ("favourites.txt");
    if (favFile.existsAsFile())
        favourites.addTokens (favFile.loadFileAsString(), "\n", "");
}

PresetManager::~PresetManager()
{
    saveSession();
}

juce::File PresetManager::getPresetsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VirtualPedals").getChildFile ("Presets");
}

juce::File PresetManager::getSessionFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VirtualPedals").getChildFile ("session.json");
}

//==============================================================================
juce::Array<PresetManager::PresetInfo> PresetManager::getPresets (const juce::String& filter) const
{
    juce::Array<PresetInfo> out;

    for (const auto& def : factoryDefs)
    {
        PresetInfo info;
        info.name = def.name;
        info.isFactory = true;
        info.isFavourite = favourites.contains (info.name);
        if (filter.isEmpty() || info.name.containsIgnoreCase (filter))
            out.add (info);
    }

    for (const auto& def : songDefs)
    {
        PresetInfo info;
        info.name = def.name;
        info.isFactory = true;
        info.isSong = true;
        info.isFavourite = favourites.contains (info.name);
        if (filter.isEmpty() || info.name.containsIgnoreCase (filter)
            || juce::String ("metallica song tones").containsIgnoreCase (filter))
            out.add (info);
    }

    for (const auto& f : getPresetsDir().findChildFiles (juce::File::findFiles, false, "*.vpreset"))
    {
        PresetInfo info;
        info.name = f.getFileNameWithoutExtension();
        info.file = f;
        info.isFavourite = favourites.contains (info.name);
        if (filter.isEmpty() || info.name.containsIgnoreCase (filter))
            out.add (info);
    }

    // favourites first, then alphabetical
    std::sort (out.begin(), out.end(), [] (const PresetInfo& a, const PresetInfo& b)
    {
        if (a.isFavourite != b.isFavourite) return a.isFavourite;
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return out;
}

static juce::var presetVarFromDef (const FactoryDef& def)
{
    auto chainVar = juce::JSON::parse (juce::String (def.json));
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("chain", chainVar);
    obj->setProperty ("inputGainDb", 0.0);
    obj->setProperty ("masterGainDb", 0.0);
    obj->setProperty ("limiterOn", true);
    return juce::var (obj);
}

static const FactoryDef* findBuiltInPreset (const juce::String& name)
{
    for (const auto& def : factoryDefs)
        if (name == def.name)
            return &def;
    for (const auto& def : songDefs)
        if (name == def.name)
            return &def;
    return nullptr;
}

juce::var PresetManager::factoryPresetVar (int index) const
{
    if (! juce::isPositiveAndBelow (index, (int) std::size (factoryDefs)))
        return {};
    return presetVarFromDef (factoryDefs[index]);
}

//==============================================================================
void PresetManager::savePreset (const juce::String& name)
{
    const auto file = getPresetsDir().getChildFile (juce::File::createLegalFileName (name) + ".vpreset");
    file.replaceWithText (juce::JSON::toString (engine.toVar(), false));
    currentName = name;
    if (onPresetChanged) onPresetChanged();
}

bool PresetManager::loadPreset (const PresetInfo& info)
{
    juce::var v;
    if (info.isFactory)
    {
        if (const auto* def = findBuiltInPreset (info.name))
            v = presetVarFromDef (*def);
    }
    else if (info.file.existsAsFile())
    {
        v = juce::JSON::parse (info.file.loadFileAsString());
    }

    if (v.isVoid())
        return false;

    pushUndoState ("Load preset");
    engine.fromVar (v);
    currentName = info.name;
    if (onPresetChanged) onPresetChanged();
    return true;
}

bool PresetManager::loadPresetByIndex (int index)
{
    const auto all = getPresets();
    if (! juce::isPositiveAndBelow (index, all.size()))
        return false;
    return loadPreset (all.getReference (index));
}

void PresetManager::deletePreset (const PresetInfo& info)
{
    if (! info.isFactory && info.file.existsAsFile())
        info.file.moveToTrash();
    if (onPresetChanged) onPresetChanged();
}

void PresetManager::setFavourite (const juce::String& name, bool fav)
{
    if (fav) favourites.addIfNotAlreadyThere (name);
    else     favourites.removeString (name);
    getPresetsDir().getChildFile ("favourites.txt").replaceWithText (favourites.joinIntoString ("\n"));
    if (onPresetChanged) onPresetChanged();
}

bool PresetManager::importPreset (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;
    const auto v = juce::JSON::parse (file.loadFileAsString());
    if (v.isVoid() || ! v.hasProperty ("chain"))
        return false;
    file.copyFileTo (getPresetsDir().getChildFile (file.getFileName()));
    if (onPresetChanged) onPresetChanged();
    return true;
}

bool PresetManager::exportPreset (const PresetInfo& info, const juce::File& dest)
{
    if (info.isFactory)
    {
        if (const auto* def = findBuiltInPreset (info.name))
            return dest.replaceWithText (juce::JSON::toString (presetVarFromDef (*def), false));
        return false;
    }
    return info.file.copyFileTo (dest);
}

//==============================================================================
void PresetManager::pushUndoState (const juce::String&)
{
    undoStack.add (engine.toVar());
    if (undoStack.size() > 64)
        undoStack.remove (0);
    redoStack.clear();
    startTimer (3000); // debounce autosave after edits
}

bool PresetManager::undo()
{
    if (undoStack.isEmpty())
        return false;
    redoStack.add (engine.toVar());
    engine.fromVar (undoStack.getLast());
    undoStack.removeLast();
    if (onPresetChanged) onPresetChanged();
    return true;
}

bool PresetManager::redo()
{
    if (redoStack.isEmpty())
        return false;
    undoStack.add (engine.toVar());
    engine.fromVar (redoStack.getLast());
    redoStack.removeLast();
    if (onPresetChanged) onPresetChanged();
    return true;
}

//==============================================================================
void PresetManager::saveSession()
{
    getSessionFile().getParentDirectory().createDirectory();
    auto v = engine.toVar();
    if (auto* obj = v.getDynamicObject())
        obj->setProperty ("presetName", currentName);
    getSessionFile().replaceWithText (juce::JSON::toString (v, false));
}

void PresetManager::restoreSession()
{
    const auto f = getSessionFile();
    if (! f.existsAsFile())
        return;
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (v.isVoid())
        return;
    engine.fromVar (v);
    currentName = v.getProperty ("presetName", "Init").toString();
    if (onPresetChanged) onPresetChanged();
}

void PresetManager::timerCallback()
{
    stopTimer();
    saveSession();
}

} // namespace vp
