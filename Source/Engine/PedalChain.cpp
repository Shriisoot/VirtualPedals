#include "PedalChain.h"
#include "PedalFactory.h"
#include "NodeGraph.h"

namespace vp
{

std::function<std::unique_ptr<Pedal> (const juce::var&, double, int)> PedalChain::hostedPedalRestorer;

void PedalChain::prepareToPlay (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = maxBlockSize;
    laneBuf.setSize (2, maxBlockSize, false, false, true);
    sumBuf.setSize (2, maxBlockSize, false, false, true);

    for (auto& item : items)
    {
        if (item->pedal)
            item->pedal->prepareToPlay (sampleRate, maxBlockSize);
        for (auto& lane : item->lanes)
            lane->prepareToPlay (sampleRate, maxBlockSize);
    }
}

void PedalChain::process (juce::AudioBuffer<double>& buf)
{
    const int n = buf.getNumSamples();

    for (auto& item : items)
    {
        if (item->pedal != nullptr)
        {
            item->pedal->processBlock (buf);
        }
        else if (! item->lanes.empty())
        {
            sumBuf.clear (0, n);
            sumBuf.clear (1, n);
            for (size_t li = 0; li < item->lanes.size(); ++li)
            {
                for (int ch = 0; ch < 2; ++ch)
                    laneBuf.copyFrom (ch, 0, buf, ch, 0, n);

                juce::AudioBuffer<double> laneView (laneBuf.getArrayOfWritePointers(), 2, n);
                item->lanes[li]->process (laneView);

                const double g = li < item->laneGains.size() ? item->laneGains[li] : 1.0;
                for (int ch = 0; ch < 2; ++ch)
                    sumBuf.addFrom (ch, 0, laneBuf, ch, 0, n, g);
            }
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, sumBuf, ch, 0, n);
        }
    }
}

void PedalChain::reset()
{
    visitPedals ([] (Pedal& p) { p.reset(); });
}

//==============================================================================
Pedal* PedalChain::addPedal (std::unique_ptr<Pedal> p, int index)
{
    if (p == nullptr)
        return nullptr;
    p->prepareToPlay (sr, maxBlock);
    auto item = std::make_unique<ChainItem>();
    item->pedal = std::move (p);
    Pedal* raw = item->pedal.get();
    if (index < 0 || index >= (int) items.size())
        items.push_back (std::move (item));
    else
        items.insert (items.begin() + index, std::move (item));
    return raw;
}

ChainItem* PedalChain::addParallel (int index, int numLanes)
{
    auto item = std::make_unique<ChainItem>();
    for (int i = 0; i < juce::jmax (2, numLanes); ++i)
    {
        auto lane = std::make_unique<PedalChain>();
        lane->prepareToPlay (sr, maxBlock);
        item->lanes.push_back (std::move (lane));
        item->laneGains.push_back (1.0 / std::sqrt ((double) juce::jmax (2, numLanes)));
    }
    ChainItem* raw = item.get();
    if (index < 0 || index >= (int) items.size())
        items.push_back (std::move (item));
    else
        items.insert (items.begin() + index, std::move (item));
    return raw;
}

void PedalChain::removeItem (int index)
{
    if (juce::isPositiveAndBelow (index, (int) items.size()))
        items.erase (items.begin() + index);
}

void PedalChain::moveItem (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, (int) items.size()))
        return;
    to = juce::jlimit (0, (int) items.size() - 1, to);
    if (from == to)
        return;
    auto item = std::move (items[(size_t) from]);
    items.erase (items.begin() + from);
    items.insert (items.begin() + to, std::move (item));
}

void PedalChain::visitPedals (const std::function<void (Pedal&)>& fn)
{
    for (auto& item : items)
    {
        if (item->pedal)
            fn (*item->pedal);
        for (auto& lane : item->lanes)
            lane->visitPedals (fn);
    }
}

int PedalChain::getTotalLatency() const
{
    int total = 0;
    for (const auto& item : items)
    {
        if (item->pedal)
            total += item->pedal->getLatencySamples();
        else
        {
            int maxLane = 0;
            for (const auto& lane : item->lanes)
                maxLane = juce::jmax (maxLane, lane->getTotalLatency());
            total += maxLane;
        }
    }
    return total;
}

//==============================================================================
juce::var PedalChain::toVar() const
{
    juce::Array<juce::var> arr;
    for (const auto& item : items)
    {
        if (item->pedal)
        {
            arr.add (item->pedal->toVar());
        }
        else
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("type", "__parallel__");
            juce::Array<juce::var> lanesVar, gainsVar;
            for (const auto& lane : item->lanes)
                lanesVar.add (lane->toVar());
            for (double g : item->laneGains)
                gainsVar.add (g);
            obj->setProperty ("lanes", lanesVar);
            obj->setProperty ("gains", gainsVar);
            arr.add (juce::var (obj));
        }
    }
    return arr;
}

void PedalChain::fromVar (const juce::var& v)
{
    items.clear();
    if (auto* arr = v.getArray())
    {
        for (const auto& iv : *arr)
        {
            const auto type = iv.getProperty ("type", "").toString();
            if (type == "__parallel__")
            {
                auto item = std::make_unique<ChainItem>();
                if (auto* lanes = iv.getProperty ("lanes", juce::var()).getArray())
                {
                    for (const auto& lv : *lanes)
                    {
                        auto lane = std::make_unique<PedalChain>();
                        lane->prepareToPlay (sr, maxBlock);
                        lane->fromVar (lv);
                        item->lanes.push_back (std::move (lane));
                    }
                }
                if (auto* gains = iv.getProperty ("gains", juce::var()).getArray())
                    for (const auto& gv : *gains)
                        item->laneGains.push_back ((double) gv);
                while (item->laneGains.size() < item->lanes.size())
                    item->laneGains.push_back (0.7);
                if (! item->lanes.empty())
                    items.push_back (std::move (item));
            }
            else if (type == "custom")
            {
                auto pedal = std::make_unique<CustomPedal> (iv.getProperty ("customName", "Custom").toString(),
                                                            iv.getProperty ("graph", juce::var()));
                pedal->fromVar (iv);
                addPedal (std::move (pedal));
            }
            else if (type == "vst3" && hostedPedalRestorer != nullptr)
            {
                if (auto pedal = hostedPedalRestorer (iv, sr, maxBlock))
                    addPedal (std::move (pedal));
            }
            else if (auto pedal = PedalFactory::instance().create (type))
            {
                pedal->fromVar (iv);
                addPedal (std::move (pedal));
            }
        }
    }
}

} // namespace vp
