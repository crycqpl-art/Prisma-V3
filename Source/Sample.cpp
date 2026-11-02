#include "Sample.h"
#include <cmath>

namespace prisma
{

static std::vector<float> energyEnvelope (const Sample& s, int hop)
{
    const auto* L = s.audio.getReadPointer (0);
    const auto* R = s.audio.getReadPointer (1);
    const int frames = s.length() / hop;
    std::vector<float> env ((size_t) juce::jmax (1, frames), -10.0f);
    for (int f = 0; f < frames; ++f)
    {
        double sum = 0.0;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float d = i > 0 ? m - 0.5f * (L[i - 1] + R[i - 1]) : 0.0f;
            sum += (double) m * m + 4.0 * (double) d * d;
        }
        env[(size_t) f] = (float) std::log10 (1.0e-7 + sum / hop);
    }
    return env;
}

static std::vector<int> findTransients (const Sample& s, float sensitivity)
{
    const int hop = juce::jmax (64, (int) (s.sampleRate * 0.01));
    const auto env = energyEnvelope (s, hop);
    float top = -10.0f;
    for (auto v : env) top = juce::jmax (top, v);
    const float riseNeeded = 0.9f - 0.7f * juce::jlimit (0.0f, 1.0f, sensitivity);   // 0.9 .. 0.2 (log10 units)
    const float floorDb = 2.0f + 2.0f * sensitivity;                                  // how far below the peak still counts
    const int minGap = juce::jmax (1, (int) (0.06 * s.sampleRate / hop));
    std::vector<int> out { 0 };
    int last = 0;
    for (int f = 2; f + 1 < (int) env.size(); ++f)
    {
        const float rise = env[(size_t) f] - juce::jmin (env[(size_t) f - 1], env[(size_t) f - 2]);
        // must also be a new local high, so noisy decays do not count as hits
        float recentMax = -10.0f;
        for (int k = juce::jmax (0, f - 6); k < f; ++k) recentMax = juce::jmax (recentMax, env[(size_t) k]);
        const bool newHigh = env[(size_t) f] > recentMax + 0.15f;
        if (rise > riseNeeded && newHigh && env[(size_t) f] > top - floorDb && env[(size_t) f] >= env[(size_t) f + 1] && f - last >= minGap)
        {
            out.push_back ((f - 1) * hop);
            last = f;
        }
    }
    return out;
}

SamplePtr loadSample (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { error = "unsupported or unreadable file"; return nullptr; }

    const double sr = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (sr * 300.0));   // 5 minutes max
    if (len < 256) { error = "file is too short"; return nullptr; }

    juce::AudioBuffer<float> tmp ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&tmp, 0, len, 0, true, true);

    auto s = std::make_shared<Sample>();
    s->sampleRate = sr;
    s->path = file.getFullPathName();
    s->name = file.getFileNameWithoutExtension();
    s->audio.setSize (2, len);
    s->audio.copyFrom (0, 0, tmp, 0, 0, len);
    s->audio.copyFrom (1, 0, tmp, tmp.getNumChannels() > 1 ? 1 : 0, 0, len);
    const float peak = s->audio.getMagnitude (0, len);
    if (peak < 1.0e-5f) { error = "file is silent"; return nullptr; }
    s->audio.applyGain (0.89f / peak);

    const int buckets = 2048;
    s->overview.assign ((size_t) buckets, 0.0f);
    s->brightness.assign ((size_t) buckets, 0.0f);
    const auto* L = s->audio.getReadPointer (0);
    const auto* R = s->audio.getReadPointer (1);
    for (int b = 0; b < buckets; ++b)
    {
        const int a = (int) ((juce::int64) len * b / buckets);
        const int e = juce::jmax (a + 1, (int) ((juce::int64) len * (b + 1) / buckets));
        float pk = 0.0f; int zc = 0; float prev = 0.0f;
        for (int i = a; i < e && i < len; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            pk = juce::jmax (pk, std::abs (m));
            if ((m > 0.0f) != (prev > 0.0f)) ++zc;
            prev = m;
        }
        s->overview[(size_t) b] = juce::jmin (1.0f, pk);
        s->brightness[(size_t) b] = juce::jlimit (0.0f, 1.0f, (float) zc / (float) juce::jmax (1, e - a) * 8.0f);
    }
    s->transients = findTransients (*s, 0.5f);
    return s;
}

std::vector<float> gridLines (const Sample& s, double gridBeats, double bpm)
{
    std::vector<float> lines;
    if (gridBeats <= 0.0 || bpm <= 0.0) return lines;
    const double sliceSec = gridBeats * 60.0 / bpm;
    const double total = s.seconds();
    for (double t = 0.0; t < total - 1.0e-6 && (int) lines.size() < 4096; t += sliceSec)
        lines.push_back ((float) (t / total));
    return lines;
}

std::vector<Region> slice (const Sample& s, const SliceSettings& st)
{
    std::vector<float> cuts;
    const double total = s.seconds();

    if (st.mode == DivideGrid)
    {
        const double sliceSec = st.gridBeats * 60.0 / juce::jmax (20.0, st.bpm);
        for (double t = 0.0; t < total - 1.0e-6; t += sliceSec)
        {
            // drop a leftover that is shorter than half a slice
            if (total - t < sliceSec * 0.5 && ! cuts.empty()) break;
            cuts.push_back ((float) (t / total));
            if ((int) cuts.size() >= maxPads) break;
        }
    }
    else if (st.mode == DivideEqual)
    {
        const int n = juce::jlimit (1, maxPads, st.equalCount);
        for (int i = 0; i < n; ++i) cuts.push_back ((float) i / (float) n);
    }
    else
    {
        for (auto t : findTransients (s, st.sensitivity))
        {
            cuts.push_back ((float) t / (float) s.length());
            if ((int) cuts.size() >= maxPads) break;
        }
    }

    if (cuts.empty()) cuts.push_back (0.0f);
    std::vector<Region> regions;
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        Region r;
        r.start = cuts[i];
        r.end = i + 1 < cuts.size() ? cuts[i + 1] : 1.0f;
        if (st.mode == DivideGrid && i + 1 == cuts.size())
        {
            const double sliceNorm = st.gridBeats * 60.0 / juce::jmax (20.0, st.bpm) / total;
            r.end = (float) juce::jmin (1.0, (double) r.start + sliceNorm);
        }
        if (r.end - r.start > 1.0e-5f) regions.push_back (r);
    }
    return regions;
}

} // namespace prisma
