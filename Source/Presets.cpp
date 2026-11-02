#include "PluginProcessor.h"
#include "Presets.h"
#include <utility>

namespace
{
using Setting = std::pair<const char*, float>;
struct Preset { const char* name; std::vector<Setting> values; };

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        { "Init", {} },
        { "Chop 1/16", { { "rate", 10 }, { "length", 0.8f }, { "grainSize", 60 }, { "density", 45 } } },
        { "Stutter 1/32", { { "rate", 12 }, { "length", 0.6f }, { "grainSize", 40 }, { "density", 60 } } },
        { "Triplet Skip", { { "rate", 8 }, { "length", 0.55f }, { "grainSize", 70 }, { "density", 40 }, { "crunch", 0.15f } } },
        { "Cloud", { { "grainSize", 250 }, { "density", 30 }, { "scan", 0.25f }, { "spread", 0.3f }, { "chaos", 0.2f },
                     { "attack", 120 }, { "release", 900 }, { "wash", 0.4f } } },
        { "Freeze", { { "grainSize", 300 }, { "density", 40 }, { "scan", 0.0f }, { "spread", 0.05f }, { "attack", 60 }, { "release", 1200 } } },
        { "Half Speed", { { "scan", 0.5f }, { "pitch", -12 }, { "grainSize", 120 }, { "density", 35 }, { "lowCut", 60 } } },
        { "Shimmer", { { "pitch", 12 }, { "scan", 0.5f }, { "grainSize", 180 }, { "density", 40 }, { "chaos", 0.2f }, { "wash", 0.6f } } },
        { "Glitch", { { "rate", 12 }, { "length", 0.7f }, { "chaos", 0.6f }, { "grainSize", 30 }, { "density", 70 }, { "crunch", 0.3f } } },
        { "Tape Stutter", { { "rate", 7 }, { "length", 0.5f }, { "grainSize", 90 }, { "density", 40 }, { "crunch", 0.3f },
                            { "highCut", 7000 }, { "wash", 0.2f } } },
    };
    return p;
}
} // namespace

juce::StringArray prisma::presetNames()
{
    juce::StringArray n;
    for (auto& p : presets()) n.add (p.name);
    return n;
}

void PrismaProcessor::applyPreset (int index)
{
    const auto& all = presets();
    if (index < 0 || index >= (int) all.size()) return;
    static const juce::StringArray keep { "divide", "gridDiv", "gridType", "sampleBpm", "count", "sensitivity", "snap" };
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (keep.contains (rp->getParameterID())) continue;   // never re-slice the sample from a preset
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    for (auto& [id, v] : all[(size_t) index].values)
        setParam (id, v);
}
