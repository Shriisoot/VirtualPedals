#pragma once
#include "../Engine/AudioEngine.h"
#include "../Presets/PresetManager.h"

namespace vp
{

// Maps MIDI CCs to pedal parameters. Click a knob while in learn mode, move a
// controller: bound. Program changes load presets. Expression pedals are CCs.
class MidiLearnManager
{
public:
    MidiLearnManager (AudioEngine& e, PresetManager& p) : engine (e), presets (p)
    {
        engine.onMidiCC = [this] (int cc, float v) { handleCC (cc, v); };
        engine.onMidiProgramChange = [this] (int prog)
        {
            juce::MessageManager::callAsync ([this, prog] { presets.loadPresetByIndex (prog); });
        };
        load();
    }

    ~MidiLearnManager()
    {
        engine.onMidiCC = nullptr;
        engine.onMidiProgramChange = nullptr;
        save();
    }

    //==============================================================================
    struct Mapping
    {
        int cc = -1;
        Pedal* pedal = nullptr;          // resolved at runtime
        juce::String pedalType, paramId; // persisted identity (type + occurrence index)
        int pedalOccurrence = 0;
    };

    // Arm learn: the next incoming CC gets bound to this parameter.
    void armLearn (Pedal* pedal, const juce::String& paramId)
    {
        armedPedal = pedal;
        armedParam = paramId;
        learning.store (true);
    }

    void cancelLearn() { learning.store (false); armedPedal = nullptr; }
    bool isLearning() const { return learning.load(); }

    void clearMappingFor (Pedal* pedal, const juce::String& paramId)
    {
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
            [&] (const Mapping& m) { return m.pedal == pedal && m.paramId == paramId; }), mappings.end());
        save();
    }

    bool hasMappingFor (Pedal* pedal, const juce::String& paramId) const
    {
        for (const auto& m : mappings)
            if (m.pedal == pedal && m.paramId == paramId)
                return true;
        return false;
    }

    // Re-resolve pedal pointers after any chain change.
    void resolve()
    {
        std::map<juce::String, int> seen;
        std::vector<std::pair<Pedal*, int>> pedalsByType; // pedal + occurrence

        engine.editChain ([&] (PedalChain& chain)
        {
            chain.visitPedals ([&] (Pedal& p)
            {
                const int occ = seen[p.typeId]++;
                pedalsByType.push_back ({ &p, occ });
            });
        });

        for (auto& m : mappings)
        {
            m.pedal = nullptr;
            for (auto& [pedal, occ] : pedalsByType)
                if (pedal->typeId == m.pedalType && occ == m.pedalOccurrence)
                    m.pedal = pedal;
        }
    }

private:
    void handleCC (int cc, float value01)
    {
        if (learning.load() && armedPedal != nullptr)
        {
            Mapping m;
            m.cc = cc;
            m.pedal = armedPedal;
            m.paramId = armedParam;
            m.pedalType = armedPedal->typeId;
            m.pedalOccurrence = occurrenceOf (armedPedal);
            // replace any existing binding for the same target
            mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                [&] (const Mapping& x) { return x.pedal == m.pedal && x.paramId == m.paramId; }), mappings.end());
            mappings.push_back (m);
            learning.store (false);
            armedPedal = nullptr;
            juce::MessageManager::callAsync ([this] { save(); });
            return;
        }

        for (const auto& m : mappings)
        {
            if (m.cc == cc && m.pedal != nullptr)
            {
                if (auto* p = m.pedal->param (m.paramId))
                    p->set (p->fromNormalised ((double) value01));
                else if (m.paramId == "__bypass__")
                    m.pedal->setBypassed (value01 < 0.5f);
            }
        }
    }

    int occurrenceOf (Pedal* target)
    {
        int occ = 0, result = 0;
        std::map<juce::String, int> seen;
        engine.editChain ([&] (PedalChain& chain)
        {
            chain.visitPedals ([&] (Pedal& p)
            {
                const int o = seen[p.typeId]++;
                if (&p == target) result = o;
            });
        });
        juce::ignoreUnused (occ);
        return result;
    }

    juce::File mapFile() const
    {
        return PresetManager::getPresetsDir().getParentDirectory().getChildFile ("midimap.json");
    }

    void save()
    {
        juce::Array<juce::var> arr;
        for (const auto& m : mappings)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("cc", m.cc);
            o->setProperty ("type", m.pedalType);
            o->setProperty ("occ", m.pedalOccurrence);
            o->setProperty ("param", m.paramId);
            arr.add (juce::var (o));
        }
        mapFile().replaceWithText (juce::JSON::toString (juce::var (arr), false));
    }

    void load()
    {
        const auto v = juce::JSON::parse (mapFile().loadFileAsString());
        if (auto* arr = v.getArray())
        {
            for (const auto& mv : *arr)
            {
                Mapping m;
                m.cc = (int) mv.getProperty ("cc", -1);
                m.pedalType = mv.getProperty ("type", "").toString();
                m.pedalOccurrence = (int) mv.getProperty ("occ", 0);
                m.paramId = mv.getProperty ("param", "").toString();
                if (m.cc >= 0)
                    mappings.push_back (m);
            }
        }
        resolve();
    }

    AudioEngine& engine;
    PresetManager& presets;
    std::vector<Mapping> mappings;
    Pedal* armedPedal = nullptr;
    juce::String armedParam;
    std::atomic<bool> learning { false };
};

} // namespace vp
