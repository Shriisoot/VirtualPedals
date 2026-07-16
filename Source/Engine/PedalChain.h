#pragma once
#include "Pedal.h"

namespace vp
{

class PedalChain;

// One position in a chain: either a single pedal or a parallel split of sub-chains.
struct ChainItem
{
    std::unique_ptr<Pedal> pedal;                       // leaf
    std::vector<std::unique_ptr<PedalChain>> lanes;     // parallel group (>= 2 lanes)
    std::vector<double> laneGains;                      // linear gain per lane

    bool isParallel() const { return ! lanes.empty(); }
};

// Recursive serial chain with parallel groups: unlimited serial + parallel routing.
class PedalChain
{
public:
    PedalChain() = default;

    void prepareToPlay (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<double>& buf);
    void reset();

    //==============================================================================
    // Structure edits (call with the engine's process lock held)
    Pedal* addPedal (std::unique_ptr<Pedal> p, int index = -1);
    ChainItem* addParallel (int index = -1, int numLanes = 2);
    void removeItem (int index);
    void moveItem (int from, int to);

    int getNumItems() const { return (int) items.size(); }
    ChainItem* getItem (int i) { return juce::isPositiveAndBelow (i, (int) items.size()) ? items[(size_t) i].get() : nullptr; }

    // Depth-first list of every pedal in this chain (including inside parallel lanes)
    void visitPedals (const std::function<void (Pedal&)>& fn);

    int getTotalLatency() const;

    //==============================================================================
    juce::var toVar() const;
    void fromVar (const juce::var& v);

    void clear() { items.clear(); }

    // Installed by the plugin host so saved rigs can restore hosted VST3 pedals.
    static std::function<std::unique_ptr<Pedal> (const juce::var&, double sr, int blockSize)> hostedPedalRestorer;

private:
    std::vector<std::unique_ptr<ChainItem>> items;
    juce::AudioBuffer<double> laneBuf, sumBuf;
    double sr = 48000.0;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE (PedalChain)
};

} // namespace vp
