#pragma once
#include "Pedal.h"

namespace vp
{

// Central registry of every built-in pedal type.
class PedalFactory
{
public:
    struct Entry
    {
        juce::String typeId, name, category;
        std::function<std::unique_ptr<Pedal>()> create;
    };

    static PedalFactory& instance();

    const std::vector<Entry>& getEntries() const { return entries; }
    juce::StringArray getCategories() const;
    std::unique_ptr<Pedal> create (const juce::String& typeId) const;

private:
    PedalFactory();
    template <typename T>
    void reg();

    std::vector<Entry> entries;
};

} // namespace vp
