#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace prisma
{

constexpr int maxPads = 64;

// The loaded sample (always 2 channels) plus drawing data.
struct Sample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::String path, name;
    std::vector<float> overview;     // peak per bucket 0..1
    std::vector<float> brightness;   // rough high-frequency share per bucket 0..1
    std::vector<int> transients;     // sample positions of detected hits (always starts with 0)
    int length() const { return audio.getNumSamples(); }
    double seconds() const { return audio.getNumSamples() / sampleRate; }
};

using SamplePtr = std::shared_ptr<const Sample>;

enum DivideMode { DivideGrid = 0, DivideEqual, DivideTransients };

struct SliceSettings
{
    int mode = DivideGrid;
    double gridBeats = 0.5;   // length of one slice in quarter-note beats (already includes triplet/dotted)
    double bpm = 120.0;       // tempo of the sample
    int equalCount = 16;
    float sensitivity = 0.5f;
};

struct Region { float start = 0.0f, end = 1.0f; };   // normalised 0..1 of the sample

SamplePtr loadSample (const juce::File& file, juce::String& error);
std::vector<Region> slice (const Sample& s, const SliceSettings& settings);

// positions of grid lines (normalised) for snapping and drawing
std::vector<float> gridLines (const Sample& s, double gridBeats, double bpm);

} // namespace prisma
