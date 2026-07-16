#include "PedalFactory.h"
#include "../Effects/DriveEffects.h"
#include "../Effects/DynamicsEffects.h"
#include "../Effects/EqEffects.h"
#include "../Effects/DelayEffects.h"
#include "../Effects/ReverbEffects.h"
#include "../Effects/ModEffects.h"
#include "../Effects/PitchEffects.h"
#include "../Effects/FilterEffects.h"
#include "../Effects/TextureEffects.h"
#include "../Effects/SustainEffect.h"
#include "../Effects/AmpEffects.h"
#include "../Effects/PickupEffects.h"
#include "../Effects/MoreEffects.h"

namespace vp
{

PedalFactory& PedalFactory::instance()
{
    static PedalFactory f;
    return f;
}

template <typename T>
void PedalFactory::reg()
{
    T probe; // read identity from a throwaway instance
    entries.push_back ({ probe.typeId, probe.displayName, probe.category,
                         [] { return std::make_unique<T>(); } });
}

PedalFactory::PedalFactory()
{
    // Pickup (first in chain by convention)
    reg<PickupPedal>();

    // Drive
    reg<BoostPedal>();
    reg<OverdrivePedal>();
    reg<DistortionPedal>();
    reg<FuzzPedal>();
    reg<TrebleBoosterPedal>();

    // Dynamics
    reg<CompressorPedal>();
    reg<NoiseGatePedal>();
    reg<LimiterPedal>();
    reg<NoiseReductionPedal>();
    reg<SwellPedal>();

    // EQ
    reg<ParametricEQPedal>();
    reg<GraphicEQPedal>();

    // Delay
    reg<DelayPedal>();
    reg<ReverseDelayPedal>();

    // Reverb / Ambient
    reg<ReverbPedal>();
    reg<SpringReverbPedal>();
    reg<FreezePedal>();
    reg<SpectralPedal>();

    // Modulation
    reg<ChorusPedal>();
    reg<FlangerPedal>();
    reg<PhaserPedal>();
    reg<UniVibePedal>();
    reg<TremoloPedal>();
    reg<VibratoPedal>();
    reg<RotaryPedal>();
    reg<DoublerPedal>();

    // Pitch
    reg<PitchShiftPedal>();
    reg<OctaverPedal>();
    reg<HarmonyPedal>();
    reg<WhammyPedal>();

    // Filter
    reg<WahPedal>();
    reg<AutoWahPedal>();
    reg<EnvelopeFilterPedal>();
    reg<TalkBoxPedal>();

    // Experimental
    reg<RingModPedal>();
    reg<SynthPedal>();
    reg<BitCrusherPedal>();
    reg<GranularPedal>();
    reg<TapePedal>();
    reg<GlitchPedal>();
    reg<SlicerPedal>();

    // Sustain
    reg<SustainPedal>();

    // Amp & Cab
    reg<AmpPedal>();
    reg<CabPedal>();

    // Utility
    reg<LooperPedal>();
    reg<StereoImagerPedal>();
    reg<UtilityPedal>();
    reg<HumFilterPedal>();
}

juce::StringArray PedalFactory::getCategories() const
{
    juce::StringArray cats;
    for (const auto& e : entries)
        cats.addIfNotAlreadyThere (e.category);
    return cats;
}

std::unique_ptr<Pedal> PedalFactory::create (const juce::String& typeId) const
{
    for (const auto& e : entries)
        if (e.typeId == typeId)
            return e.create();
    return nullptr;
}

} // namespace vp
