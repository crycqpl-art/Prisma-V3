#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Sample.h"
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace prisma
{

constexpr int numVoices = 8;
constexpr int maxGrains = 32;      // per voice
constexpr int firstPadNote = 36;   // C1 in Ableton

// Grain trigger rate (synced to the project grid)
inline juce::StringArray rateNames()  { return { "1/1", "1/2", "1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32", "1/32T", "1/64" }; }
constexpr int numRates = 14;
constexpr int defaultRate = 9;     // 1/16
inline double rateBeats (int r)
{
    static const double b[] = { 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
    return b[juce::jlimit (0, numRates - 1, r)];
}
inline juce::StringArray shapeNames()    { return { "Square", "Soft", "Pluck", "Swell" }; }
enum Shape { ShapeSquare = 0, ShapeSoft, ShapePluck, ShapeSwell };

inline juce::StringArray divideNames()   { return { "Grid", "Equal", "Transients" }; }
inline juce::StringArray gridDivNames()  { return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }; }
inline double gridDivBeats (int i)       { static const double b[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 }; return b[juce::jlimit (0, 5, i)]; }
inline juce::StringArray gridTypeNames() { return { "Straight", "Triplet", "Dotted" }; }
inline double gridTypeFactor (int i)     { static const double f[] = { 1.0, 2.0 / 3.0, 1.5 }; return f[juce::jlimit (0, 2, i)]; }
inline juce::StringArray countNames()    { return { "4", "8", "12", "16", "24", "32", "48", "64" }; }
inline int countValue (int i)            { static const int c[] = { 4, 8, 12, 16, 24, 32, 48, 64 }; return c[juce::jlimit (0, 7, i)]; }

inline juce::String noteName (int note)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (n[((note % 12) + 12) % 12]) + juce::String (note / 12 - 2);   // Ableton: 60 = C3
}

// Global (automatable) settings, read once per block.
struct Params
{
    bool sync = true; int rate = defaultRate; float hz = 8.0f;
    float lengthMs = 160.0f; int shape = ShapeSquare;
    float position = 0.0f, scan = 1.0f, chaos = 0.0f, space = 0.3f, pitch = 0.0f;
    float attack = 0.003f, decayTime = 0.3f, sustain = 1.0f, release = 0.2f;
};

constexpr float minLengthMs = 10.0f, maxLengthMs = 1000.0f;

// Per-pad settings; written by the UI, read by the audio thread.
struct PadState
{
    std::array<std::atomic<float>, maxPads> start {}, end {}, length {}, gain {};   // length: ms, < 0 = linked
    std::array<std::atomic<int>, maxPads> rate {}, reverse {};                      // rate: -1 = linked to global
    std::atomic<int> count { 0 };

    PadState()
    {
        for (int i = 0; i < maxPads; ++i)
        {
            start[(size_t) i] = 0.0f; end[(size_t) i] = 1.0f;
            length[(size_t) i] = -1.0f; gain[(size_t) i] = 1.0f;
            rate[(size_t) i] = -1; reverse[(size_t) i] = 0;
        }
    }
    bool hasOwnSettings (int i) const
    {
        return rate[(size_t) i].load() >= 0 || length[(size_t) i].load() >= 0.0f
            || reverse[(size_t) i].load() != 0 || std::abs (gain[(size_t) i].load() - 1.0f) > 0.001f;
    }
};

// Activity the display reads (lock-free rings).
struct Activity
{
    static constexpr int size = 512;
    std::array<std::atomic<float>, size> grainPos {}, grainLen {}, grainPan {};   // pos/len normalised to the sample
    std::array<std::atomic<int>, size> grainPad {}, grainPitch {};
    std::array<std::atomic<double>, size> grainTime {};                            // seconds since start (audio clock)
    std::atomic<int> grainWrite { 0 };

    static constexpr int restartSize = 64;
    std::array<std::atomic<int>, restartSize> restartPad {};
    std::atomic<int> restartWrite { 0 };

    std::array<std::atomic<int>, numVoices> voicePad {};
    std::array<std::atomic<float>, numVoices> voicePos {}, voiceLevel {};
    std::atomic<int> lastPad { -1 };
    std::atomic<double> clock { 0.0 };   // audio time in seconds

    Activity() { for (auto& v : voicePad) v = -1; }
};

//==============================================================================
// Rhythmic grain engine: long grains (10 ms .. 1 s) fired on the project grid
// (or a free rate), read at original speed from a head that scans through the pad.
class GrainEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice();
    }

    void setShared (PadState* p, Activity* a) { pads = p; act = a; }

    void noteOn (int note, float vel, const Sample& s, double ppqNow)
    {
        const int count = pads->count.load();
        const int pad = note - firstPadNote;
        if (pad < 0 || pad >= count) return;

        Voice* t = nullptr;
        for (auto& v : voices) if (v.active && v.note == note) { t = &v; break; }      // same key: reuse its voice
        if (t == nullptr) for (auto& v : voices) if (! v.active) { t = &v; break; }
        if (t == nullptr) { t = &voices[0]; for (auto& v : voices) if (v.age > t->age) t = &v; }
        auto& v = *t;
        const bool reuse = v.active;
        const float oldEnv = v.env;
        auto oldGrains = v.grains;
        v = Voice();
        if (reuse)
        {
            v.env = oldEnv;
            v.grains = oldGrains;
            for (auto& g : v.grains) if (g.active) g.fadeOut (fadeSamples());   // old grains fade quickly, no click
        }
        v.active = true; v.held = true; v.note = note; v.pad = pad;
        v.velocity = 0.3f + 0.7f * vel;
        v.rng.setSeed ((juce::int64) note * 7919 + (++seed) * 104729);
        setupRegion (v, s, true);
        v.rate = effectiveRate (pad);
        if (v.rate >= 0) v.lastTick = (juce::int64) std::floor (ppqNow / rateBeats (v.rate) + 1.0e-9);
        v.fireNow = true;
        if (act != nullptr) act->lastPad.store (pad);
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.note == note) { v.held = false; v.envStage = 3; }
    }

    void allNotesOff()
    {
        for (auto& v : voices) if (v.active) { v.held = false; v.envStage = 3; }
    }

    void render (float* L, float* R, int n, const Params& p, const Sample& s, double bpm, double ppqStart, double ppqPerSample)
    {
        globals = p; lastBpm = bpm;
        const double attackStep = 1.0 / juce::jmax (1.0, (double) p.attack * sr);
        const double decayCoef = std::exp (-6.9 / juce::jmax (1.0, (double) p.decayTime * sr));
        const double releaseCoef = std::exp (-6.9 / juce::jmax (1.0, (double) p.release * sr));
        const double srcRatio = s.sampleRate / sr;
        const auto* SL = s.audio.getReadPointer (0);
        const auto* SR = s.audio.getReadPointer (1);
        const int slen = s.length();

        for (int vi = 0; vi < numVoices; ++vi)
        {
            auto& v = voices[(size_t) vi];
            if (! v.active) continue;

            // effective trigger rate for this pad: synced division, or free Hz
            const int rate = effectiveRate (v.pad);
            const double beats = rate >= 0 ? rateBeats (rate) : 0.0;
            const double interval = rate >= 0 ? beats * 60.0 / bpm * sr : sr / juce::jmax (0.1, (double) p.hz);
            if (rate != v.rate)
            {
                v.rate = rate;
                if (rate >= 0) v.lastTick = (juce::int64) std::floor (ppqStart / beats + 1.0e-9);
            }
            const double lenSamples = juce::jlimit ((double) minLengthMs, (double) maxLengthMs, (double) effectiveLength (v.pad)) * 0.001 * sr;
            // overlapping grains are summed: keep loudness steady
            const float norm = 1.0f / std::sqrt ((float) juce::jmax (1.0, lenSamples / juce::jmax (1.0, interval)));

            // trigger points inside this segment
            std::array<int, 64> ticks {};
            int numTicks = 0;
            const double since0 = v.fireNow ? 0.0 : v.sinceTrigger;
            if (v.fireNow) { ticks[(size_t) numTicks++] = 0; v.fireNow = false; }
            if (v.held)
            {
                if (rate >= 0 && ppqPerSample > 0.0)
                {
                    for (juce::int64 g = v.lastTick + 1; numTicks < (int) ticks.size(); ++g)
                    {
                        const int at = (int) std::ceil ((g * beats - ppqStart) / ppqPerSample - 1.0e-6);
                        if (at >= n) break;
                        v.lastTick = g;
                        if (since0 + juce::jmax (0, at) < interval * 0.5) continue;   // too close to the note-on grain
                        ticks[(size_t) numTicks++] = juce::jmax (0, at);
                    }
                }
                else if (rate < 0)
                {
                    double next = interval - since0;
                    while (next < n && numTicks < (int) ticks.size())
                    {
                        const int at = juce::jmax (0, (int) std::ceil (next - 1.0e-6));
                        if (at >= n) break;
                        ticks[(size_t) numTicks++] = at;
                        next += interval;
                    }
                }
            }
            int tickIdx = 0;

            for (int i = 0; i < n; ++i)
            {
                while (tickIdx < numTicks && ticks[(size_t) tickIdx] == i)
                {
                    trigger (v, p, s, lenSamples, srcRatio);
                    ++tickIdx;
                }

                switch (v.envStage)
                {
                    case 0: v.env += (float) attackStep; if (v.env >= 1.0f) { v.env = 1.0f; v.envStage = 1; } break;
                    case 1: v.env = (float) (p.sustain + (v.env - p.sustain) * decayCoef);
                            if (std::abs (v.env - p.sustain) < 1.0e-4f) v.envStage = 2;
                            break;
                    case 2: v.env = p.sustain; break;
                    default: v.env *= (float) releaseCoef; break;
                }
                if (v.envStage == 3 && v.env < 1.0e-4f) { v.active = false; break; }

                // the head scans through the region at original speed (scan 1x) or slower / frozen
                const double regionLen = juce::jmax (8.0, v.regionEnd - v.regionStart);
                v.head += v.dir * p.scan * srcRatio;
                if (v.head >= v.regionEnd) v.head -= regionLen * std::floor ((v.head - v.regionStart) / regionLen);
                if (v.head < v.regionStart) v.head += regionLen * std::ceil ((v.regionStart - v.head) / regionLen);

                // sum grains
                float l = 0.0f, r = 0.0f;
                for (auto& g : v.grains)
                {
                    if (! g.active) continue;
                    const float w = g.envelope();
                    const double pl = g.pos, pr = g.pos + g.offsetR;
                    if (pl >= 0.0 && pl < slen - 1 && pr >= 0.0 && pr < slen - 1)
                    {
                        const int i0 = (int) pl, i1 = (int) pr;
                        const float fl = (float) (pl - i0), fr = (float) (pr - i1);
                        l += (SL[i0] + fl * (SL[i0 + 1] - SL[i0])) * w * g.panL;
                        r += (SR[i1] + fr * (SR[i1 + 1] - SR[i1])) * w * g.panR;
                    }
                    g.pos += g.step;
                    g.advance();
                }

                const float amp = v.env * v.velocity * v.gain * norm * 0.8f;
                L[i] += l * amp;
                R[i] += r * amp;
                v.sinceTrigger += 1.0;
                ++v.age;
            }
        }
    }

    void publish (const Sample& s, double clockSeconds)
    {
        if (act == nullptr) return;
        clock = clockSeconds;
        act->clock.store (clockSeconds);
        for (int i = 0; i < numVoices; ++i)
        {
            const auto& v = voices[(size_t) i];
            act->voicePad[(size_t) i].store (v.active ? v.pad : -1);
            act->voicePos[(size_t) i].store ((float) (v.head / juce::jmax (1, s.length())));
            act->voiceLevel[(size_t) i].store (v.active ? v.env : 0.0f);
        }
    }

    // How many grains are sounding (tests)
    int activeGrains() const
    {
        int c = 0;
        for (auto& v : voices) if (v.active) for (auto& g : v.grains) c += g.active ? 1 : 0;
        return c;
    }

private:
    struct Grain
    {
        bool active = false;
        int shape = ShapeSquare;
        double pos = 0.0, step = 1.0, offsetR = 0.0;
        double age = 0.0, length = 1.0, fade = 64.0;
        double killAt = -1.0;          // when >= 0: quick fade starting at this age
        float panL = 1.0f, panR = 1.0f, decay = 1.0f, pluck = 1.0f;

        void fadeOut (double f)
        {
            if (killAt < 0.0) { killAt = age; fade = juce::jmax (1.0, juce::jmin (fade, f)); }
        }

        float envelope() const
        {
            const double x = age / length;
            float e = 1.0f;
            switch (shape)
            {
                case ShapeSoft:  { const double t = 0.35;   // Tukey window
                                   if (x < t)            { const double s = std::sin (juce::MathConstants<double>::halfPi * x / t); e = (float) (s * s); }
                                   else if (x > 1.0 - t) { const double s = std::sin (juce::MathConstants<double>::halfPi * (1.0 - x) / t); e = (float) (s * s); }
                                   break; }
                case ShapePluck: e = pluck; break;
                case ShapeSwell: e = (float) (x < 0.85 ? std::pow (x / 0.85, 1.6) : 1.0); break;
                default: break;
            }
            // short fades at both ends so nothing clicks
            if (age < fade)               e *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * age / fade));
            const double left = length - age;
            if (left < fade)              e *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * juce::jmax (0.0, left) / fade));
            if (killAt >= 0.0)
            {
                const double k = age - killAt;
                e *= k >= fade ? 0.0f : (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * k / fade));
            }
            return e;
        }

        void advance()
        {
            age += 1.0;
            if (shape == ShapePluck && age > fade) pluck *= decay;
            if (age >= length || (killAt >= 0.0 && age - killAt >= fade)) active = false;
        }
    };

    struct Voice
    {
        bool active = false, held = false, fireNow = false;
        int note = 60, pad = 0, envStage = 0, rate = -100, dir = 1, triggers = 0;
        float velocity = 1.0f, env = 0.0f, gain = 1.0f;
        double head = 0.0, regionStart = 0.0, regionEnd = 1.0, sinceTrigger = 1.0e12;
        juce::int64 lastTick = 0, age = 0;
        std::array<Grain, maxGrains> grains {};
        juce::Random rng;
    };

    double fadeSamples() const { return 0.003 * sr; }

    int effectiveRate (int pad) const
    {
        const int padRate = pads->rate[(size_t) pad].load();
        if (padRate >= 0) return padRate;
        return globals.sync ? globals.rate : -1;
    }

    float effectiveLength (int pad) const
    {
        const float padLen = pads->length[(size_t) pad].load();
        return padLen >= 0.0f ? padLen : globals.lengthMs;
    }

    void setupRegion (Voice& v, const Sample& s, bool resetHead)
    {
        const int pad = v.pad;
        const int len = s.length();
        float a = pads->start[(size_t) pad].load(), b = pads->end[(size_t) pad].load();
        if (b < a) std::swap (a, b);
        v.regionStart = juce::jlimit (0.0, (double) len - 2.0, (double) a * len);
        v.regionEnd = juce::jlimit (v.regionStart + 8.0, (double) len - 1.0, (double) b * len);
        v.dir = pads->reverse[(size_t) pad].load() != 0 ? -1 : 1;
        v.gain = pads->gain[(size_t) pad].load();
        if (resetHead)
        {
            const double off = juce::jlimit (0.0, 0.999, (double) globals.position) * (v.regionEnd - v.regionStart);
            v.head = v.dir > 0 ? v.regionStart + off : v.regionEnd - 1.0 - off;
        }
    }

    void trigger (Voice& v, const Params& p, const Sample& s, double lenSamples, double srcRatio)
    {
        auto& rng = v.rng;
        v.sinceTrigger = 0.0;
        ++v.triggers;
        if (act != nullptr)
        {
            const int w = act->restartWrite.load (std::memory_order_relaxed);
            act->restartPad[(size_t) (w % Activity::restartSize)].store (v.pad);
            act->restartWrite.store (w + 1, std::memory_order_release);
        }
        // chaos: sometimes skip a hit (never the first one)
        if (v.triggers > 1 && p.chaos > 0.0f && rng.nextFloat() < p.chaos * 0.22f) return;

        Grain* g = nullptr;
        for (auto& x : v.grains) if (! x.active) { g = &x; break; }
        if (g == nullptr)
        {
            // all slots busy: start fading the oldest one so the next hit has room
            for (auto& x : v.grains) if (x.active && x.killAt < 0.0) { x.fadeOut (fadeSamples()); break; }
            return;
        }

        // pitch: global + chaos detune, octave jumps at higher chaos
        double semis = p.pitch;
        if (p.chaos > 0.0f)
        {
            semis += (rng.nextDouble() - 0.5) * 2.0 * p.chaos * 0.35;
            if (rng.nextFloat() < juce::jmax (0.0f, p.chaos - 0.35f) * 0.45f) semis += rng.nextBool() ? 12.0 : -12.0;
        }
        int dir = v.dir;
        if (p.chaos > 0.5f && rng.nextFloat() < (p.chaos - 0.5f) * 0.3f) dir = -dir;
        const double speed = std::pow (2.0, semis / 12.0) * srcRatio;
        const double regionLen = v.regionEnd - v.regionStart;

        double len = lenSamples * (1.0 + (rng.nextDouble() - 0.5) * 0.5 * p.chaos);
        len = juce::jmax (0.004 * sr, juce::jmin (len, regionLen / speed));   // never read past the pad
        const double span = len * speed;

        double pos = v.head;
        if (p.chaos > 0.0f) pos += (rng.nextDouble() - 0.5) * p.chaos * regionLen * 0.6;
        // keep the whole grain inside the pad region
        if (dir > 0) pos = juce::jlimit (v.regionStart, juce::jmax (v.regionStart, v.regionEnd - span - 2.0), pos);
        else         pos = juce::jlimit (juce::jmin (v.regionEnd - 2.0, v.regionStart + span + 2.0), v.regionEnd - 2.0, pos);

        // space: stereo placement + a tiny right-channel offset for width
        const float side = (v.triggers % 2 == 0 ? 1.0f : -1.0f) * (0.35f + 0.65f * rng.nextFloat());
        const float pan = 0.5f + 0.5f * p.space * side;
        const double haas = p.space * (0.002 + 0.008 * rng.nextDouble()) * s.sampleRate;

        g->active = true;
        g->shape = p.shape;
        g->pos = pos;
        g->step = speed * dir;
        g->offsetR = dir > 0 ? juce::jmin (haas, juce::jmax (0.0, v.regionEnd - (pos + span) - 2.0))
                             : -juce::jmin (haas, juce::jmax (0.0, pos - span - v.regionStart - 2.0));
        g->age = 0.0;
        g->length = len;
        g->killAt = -1.0;
        g->fade = juce::jmin (fadeSamples(), len * 0.25);
        g->pluck = 1.0f;
        g->decay = (float) std::exp (-5.0 / len);
        g->panL = std::cos (pan * juce::MathConstants<float>::halfPi) * 1.41421f;
        g->panR = std::sin (pan * juce::MathConstants<float>::halfPi) * 1.41421f;

        if (act != nullptr)
        {
            const int w = act->grainWrite.load (std::memory_order_relaxed);
            const auto k = (size_t) (w % Activity::size);
            const double slen = juce::jmax (1, s.length());
            act->grainPos[k].store ((float) (pos / slen));
            act->grainLen[k].store ((float) (span / slen) * (float) dir);
            act->grainPan[k].store (pan);
            act->grainPad[k].store (v.pad);
            act->grainPitch[k].store ((int) std::lround (semis));
            act->grainTime[k].store (clock);
            act->grainWrite.store (w + 1, std::memory_order_release);
        }
    }

public:
    // values noteOn() needs that live outside the voice
    void setGlobals (const Params& p, double bpm) { globals = p; lastBpm = bpm; }

private:
    std::array<Voice, numVoices> voices {};
    PadState* pads = nullptr;
    Activity* act = nullptr;
    Params globals;
    double sr = 44100.0, lastBpm = 120.0, clock = 0.0;
    juce::int64 seed = 0;
};

//==============================================================================
// Low cut / high cut, Crunch, Pump, Wash and output.
class FX
{
public:
    struct Settings { float lowCut = 20.0f, highCut = 20000.0f, reso = 0.7f, crunch = 0.0f, wash = 0.0f, pump = 0.0f, volumeDb = 0.0f; };

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, blockSize), 2 };
        for (auto* f : { &hp, &lp, &washLp }) { f->prepare (spec); f->reset(); }
        hp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        lp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        washLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        washLp.setResonance (0.6f);
        hp.setResonance (0.7f);
        reverb.setSampleRate (sr);
        reverb.reset();
        for (auto* s : { &crunchSm, &washSm, &pumpSm, &gainSm, &hpSm, &lpSm }) s->reset (sr, 0.05);
        holdCount = 0; follower = 0.0f;
        snapNext = true;
    }

    void process (float* L, float* R, int n, const Settings& s, double ppqStart, double ppqPerSample)
    {
        if (snapNext)
        {
            snapNext = false;
            hpSm.setCurrentAndTargetValue (s.lowCut);
            lpSm.setCurrentAndTargetValue (juce::jmin (s.highCut, (float) (sr * 0.45)));
            crunchSm.setCurrentAndTargetValue (s.crunch);
            washSm.setCurrentAndTargetValue (s.wash);
            pumpSm.setCurrentAndTargetValue (s.pump);
            gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (s.volumeDb));
            hp.setCutoffFrequency (s.lowCut);
            lp.setCutoffFrequency (juce::jmin (s.highCut, (float) (sr * 0.45)));
        }
        hpSm.setTargetValue (s.lowCut);
        lpSm.setTargetValue (juce::jmin (s.highCut, (float) (sr * 0.45)));
        crunchSm.setTargetValue (s.crunch);
        washSm.setTargetValue (s.wash);
        pumpSm.setTargetValue (s.pump);
        gainSm.setTargetValue (juce::Decibels::decibelsToGain (s.volumeDb));
        lp.setResonance (juce::jmax (0.5f, s.reso));
        const float relCoef = (float) std::exp (-1.0 / (0.4 * sr));

        for (int i = 0; i < n; ++i)
        {
            const float hpf = hpSm.getNextValue(), lpf = lpSm.getNextValue();
            const float w = washSm.getNextValue();
            if ((i & 15) == 0)
            {
                hp.setCutoffFrequency (hpf);
                lp.setCutoffFrequency (lpf);
                washLp.setCutoffFrequency (juce::jmin (20000.0f * std::pow (1.0f - w * 0.93f, 2.2f) + 400.0f, (float) (sr * 0.45)));
            }
            float l = L[i], r = R[i];
            const float lvl = juce::jmax (std::abs (l), std::abs (r));
            follower = lvl > follower ? lvl : follower * relCoef;

            l = lp.processSample (0, hp.processSample (0, l));
            r = lp.processSample (1, hp.processSample (1, r));

            const float c = crunchSm.getNextValue();
            if (c > 0.001f)
            {
                const float drive = 1.0f + c * 14.0f;
                const float nrm = 1.0f / std::tanh (drive);
                const float mix = juce::jmin (1.0f, c * 1.6f);
                l += (std::tanh (l * drive) * nrm * 0.8f - l) * mix;
                r += (std::tanh (r * drive) * nrm * 0.8f - r) * mix;
                const int hold = 1 + (int) (juce::jmax (0.0f, c - 0.5f) * 10.0f);
                if (holdCount <= 0) { holdL = l; holdR = r; holdCount = hold; }
                --holdCount;
                if (hold > 1) { l = holdL; r = holdR; }
            }

            const float pm = pumpSm.getNextValue();
            if (pm > 0.001f)
            {
                double ph = ppqStart + i * ppqPerSample;
                ph -= std::floor (ph);
                const float duck = (float) std::exp (-ph * 5.0);
                const float g = 1.0f - pm * 0.92f * duck * juce::jmin (1.0f, (float) (ph * 60.0) + 0.2f);
                l *= g; r *= g;
            }

            if (w > 0.001f)
            {
                l = washLp.processSample (0, l);
                r = washLp.processSample (1, r);
                const float gate = juce::jmin (1.0f, follower * 6.0f);
                if (rng.nextFloat() < w * 0.0012f * gate)
                {
                    const float pop = (rng.nextFloat() - 0.5f) * 0.25f * w;
                    l += pop; r += pop * 0.8f;
                }
                const float hiss = (rng.nextFloat() - 0.5f) * 0.004f * w * gate;
                l += hiss; r += hiss;
            }
            L[i] = l; R[i] = r;
        }
        hp.snapToZero(); lp.snapToZero(); washLp.snapToZero();

        const float wv = washSm.getCurrentValue();
        if (wv > 0.001f)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.6f + 0.38f * wv;
            rp.damping = 0.55f;
            rp.wetLevel = 0.32f * wv;
            rp.dryLevel = 0.5f * (1.0f - 0.35f * wv);
            rp.width = 1.0f;
            reverb.setParameters (rp);
            reverb.processStereo (L, R, n);
        }

        for (int i = 0; i < n; ++i)
        {
            const float g = gainSm.getNextValue();
            L[i] = safety (L[i] * g);
            R[i] = safety (R[i] * g);
        }
    }

private:
    static float safety (float x)
    {
        if (! std::isfinite (x)) return 0.0f;
        const float a = std::abs (x);
        if (a < 0.8f) return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.0f ? -y : y;
    }

    double sr = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> hp, lp, washLp;
    juce::Reverb reverb;
    juce::SmoothedValue<float> crunchSm, washSm, pumpSm, gainSm, hpSm, lpSm;
    float holdL = 0.0f, holdR = 0.0f, follower = 0.0f;
    int holdCount = 0;
    bool snapNext = true;
    juce::Random rng { 99 };
};

} // namespace prisma
