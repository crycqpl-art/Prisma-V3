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
        // rate index: 3 = 1/4, 6 = 1/8, 7 = 1/8T, 8 = 1/16D, 9 = 1/16, 10 = 1/16T, 11 = 1/32, 13 = 1/64
        { "Init", {} },
        { "Chop 1/16", { { "rate", 9 }, { "length", 120 }, { "scan", 1.0f }, { "space", 0.35f } } },
        { "Long Grains", { { "rate", 9 }, { "length", 320 }, { "scan", 0.6f }, { "space", 0.6f }, { "chaos", 0.12f } } },
        { "Dotted Drift", { { "rate", 8 }, { "length", 260 }, { "shape", 1 }, { "scan", 0.5f }, { "space", 0.7f }, { "wash", 0.25f } } },
        { "Triplet Skip", { { "rate", 10 }, { "length", 90 }, { "chaos", 0.35f }, { "space", 0.5f }, { "crunch", 0.15f } } },
        { "Stutter 1/32", { { "rate", 11 }, { "length", 45 }, { "scan", 0.0f }, { "space", 0.2f } } },
        { "Pluck Keys", { { "rate", 6 }, { "length", 220 }, { "shape", 2 }, { "space", 0.45f }, { "highCut", 9000 } } },
        { "Freeze Swell", { { "rate", 3 }, { "length", 700 }, { "shape", 3 }, { "scan", 0.0f }, { "space", 0.8f },
                            { "attack", 80 }, { "release", 1200 }, { "wash", 0.35f } } },
        { "Half Speed", { { "rate", 6 }, { "length", 300 }, { "shape", 1 }, { "scan", 0.5f }, { "pitch", -12 }, { "lowCut", 60 }, { "space", 0.5f } } },
        { "Shimmer", { { "rate", 9 }, { "length", 240 }, { "shape", 1 }, { "pitch", 12 }, { "scan", 0.5f }, { "chaos", 0.25f },
                       { "space", 0.8f }, { "wash", 0.5f } } },
        { "Glitch", { { "rate", 11 }, { "length", 70 }, { "chaos", 0.65f }, { "space", 0.6f }, { "crunch", 0.3f } } },
        { "Free Cloud", { { "sync", 0 }, { "hz", 14 }, { "length", 400 }, { "shape", 1 }, { "scan", 0.3f }, { "chaos", 0.3f },
                          { "space", 0.9f }, { "attack", 150 }, { "release", 900 }, { "wash", 0.4f } } },
        { "Tape Stutter", { { "rate", 7 }, { "length", 150 }, { "crunch", 0.3f }, { "highCut", 7000 }, { "wash", 0.2f }, { "space", 0.3f } } },
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
