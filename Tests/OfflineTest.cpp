// Offline tests for Prisma 0.3 (granular slicer)
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Presets.h"
#include <set>
using namespace prisma;

static constexpr double SR = 48000.0;
static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr = SR)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

struct FakePlayHead : public juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

using Events = std::vector<std::tuple<double, int, bool>>;
struct Stats { float peak = 0, rms = 0; bool finite = true; int restarts = 0, grains = 0; double cpu = 0; };

static Stats render (PrismaProcessor& p, const Events& ev, double seconds, juce::AudioBuffer<float>& out,
                     bool randomBlocks = false, FakePlayHead* head = nullptr, std::function<void (int)> perBlock = {})
{
    p.setPlayHead (head);
    p.setPlayConfigDetails (0, 2, SR, 1024);
    p.prepareToPlay (SR, 1024);
    if (head != nullptr) head->ppq = 0.0;
    const int total = (int) (seconds * SR);
    out.setSize (2, total);
    out.clear();
    juce::Random r (3);
    const int r0 = p.activity.restartWrite.load(), g0 = p.activity.grainWrite.load();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    int pos = 0, block = 0;
    while (pos < total)
    {
        const int bs = juce::jmin (randomBlocks ? 1 + r.nextInt (1024) : 512, total - pos);
        juce::AudioBuffer<float> b (2, bs);
        juce::MidiBuffer midi;
        for (auto& [t, note, on] : ev)
        {
            const int s = (int) std::llround (t * SR);
            if (s >= pos && s < pos + bs)
                midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), s - pos);
        }
        if (perBlock) perBlock (block);
        p.processBlock (b, midi);
        if (head != nullptr) head->ppq += bs * head->bpm / (60.0 * SR);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, bs);
        pos += bs; ++block;
    }
    Stats st;
    st.cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / seconds;
    st.restarts = p.activity.restartWrite.load() - r0;
    st.grains = p.activity.grainWrite.load() - g0;
    double sum = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < total; ++i)
        {
            const float v = out.getSample (c, i);
            if (! std::isfinite (v)) st.finite = false;
            st.peak = std::max (st.peak, std::abs (v));
            sum += v * v;
        }
    st.rms = (float) std::sqrt (sum / (2.0 * total));
    p.setPlayHead (nullptr);
    return st;
}

static double rmsOf (const juce::AudioBuffer<float>& b, double from, double to)
{
    double s = 0; int n = 0;
    for (int i = (int) (from * SR); i < (int) (to * SR) && i < b.getNumSamples(); ++i, ++n) s += b.getSample (0, i) * b.getSample (0, i);
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

static double maxSecondDiff (const juce::AudioBuffer<float>& b, int from, int to)
{
    double m = 0;
    for (int i = from + 1; i < juce::jmin (to, b.getNumSamples()) - 1; ++i)
        m = std::max (m, (double) std::abs (b.getSample (0, i + 1) - 2 * b.getSample (0, i) + b.getSample (0, i - 1)));
    return m;
}

// 2 bars at 120 BPM (4 s): a chord pad with a click on every 1/16 so slices are audible and transients exist
static juce::AudioBuffer<float> makeLoop()
{
    const int n = (int) (4.0 * SR);
    juce::AudioBuffer<float> b (2, n);
    juce::Random r (9);
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR;
        const int c = (int) (t / 1.0) % 4;
        double s = 0.0;
        for (int k = 0; k < 3; ++k) s += std::sin (juce::MathConstants<double>::twoPi * 440.0 * std::pow (2.0, (chords[c][k] - 69) / 12.0) * t) * 0.15;
        const double ts = std::fmod (t, 0.5);
        s += (r.nextFloat() - 0.5) * 0.9 * std::exp (-ts * 40.0);   // hit every quarter note
        b.setSample (0, i, (float) s);
        b.setSample (1, i, (float) (s * 0.95));
    }
    return b;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    auto dir = outDir.getChildFile ("sounds");
    dir.createDirectory();
    auto loopFile = dir.getChildFile ("loop_120bpm_2bars.wav");
    writeWav (loopFile, makeLoop());
    juce::AudioBuffer<float> sine (1, (int) (4.0 * SR));
    for (int i = 0; i < sine.getNumSamples(); ++i) sine.setSample (0, i, (float) (0.8 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / SR)));
    auto sineFile = dir.getChildFile ("sine.wav");
    writeWav (sineFile, sine);

    if (const char* snap = std::getenv ("PRISMA_SNAPSHOT"))
    {
        PrismaProcessor p;
        if (snap[0] != '2') p.loadFile (loopFile);
        p.setParam ("rate", 10);
        p.setPadRate (2, 12);
        p.setPadLength (2, 0.5f);
        FakePlayHead head;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* pe = dynamic_cast<PrismaEditor*> (ed.get());
        p.setPlayHead (&head);
        p.setPlayConfigDetails (0, 2, SR, 512);
        p.prepareToPlay (SR, 512);
        for (int k = 0; k < 50; ++k)
        {
            juce::AudioBuffer<float> blk (2, 512);
            juce::MidiBuffer m;
            if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), 0);
            if (k == 10) m.addEvent (juce::MidiMessage::noteOn (1, 41, (juce::uint8) 100), 0);
            p.processBlock (blk, m);
            head.ppq += 512 * 120.0 / (60.0 * SR);
            pe->animate();
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile (snap[0] == '2' ? "ui_empty.png" : "ui.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::cout << "snapshot written" << std::endl;
        p.setPlayHead (nullptr);
        return 0;
    }

    // ---- slicing
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.setParam ("gridDiv", 4);      // 1/16
        std::cout << "  " << p.loadFile (loopFile) << std::endl;
        check (p.padCount() == 32, "2 bars on a 1/16 grid = 32 pads (" + juce::String (p.padCount()) + ")");
        p.setParam ("gridDiv", 3); p.setParam ("gridType", 1);   // 1/8 triplets
        p.reslice();
        check (p.padCount() == 24, "1/8 triplets = 24 pads (" + juce::String (p.padCount()) + ")");
        p.setParam ("gridType", 2);                               // 1/8 dotted
        p.reslice();
        check (p.padCount() == 11, "1/8 dotted = 11 pads (" + juce::String (p.padCount()) + ")");
        p.setParam ("gridType", 0); p.setParam ("gridDiv", 2);    // 1/4
        p.setParam ("sampleBpm", 60.0f);                          // sample thought to be at 60 BPM: quarter = 1 s
        p.reslice();
        check (p.padCount() == 4, "sample BPM changes the grid (60 BPM, 1/4 = 4 pads)");
        p.setParam ("divide", 1); p.setParam ("count", 4);       // equal 24
        p.reslice();
        check (p.padCount() == 24, "equal split into 24");
        p.setParam ("divide", 2);
        p.reslice();
        std::cout << "  transients found " << p.padCount() << " pads (expect 8: one per beat)" << std::endl;
        check (p.padCount() >= 6 && p.padCount() <= 10, "transient split");
        bool ordered = true;
        for (int i = 1; i < p.padCount(); ++i) ordered &= p.pads.start[(size_t) i].load() > p.pads.start[(size_t) i - 1].load();
        check (ordered, "pads are in order along the sample");
    }

    // ---- keys play their own region
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.setParam ("gridDiv", 2);   // 1/4 -> 8 pads of 0.5 s
        p.loadFile (loopFile);
        p.setParam ("scan", 0.0f);
        p.setParam ("spread", 0.0f);
        bool ok = true;
        for (int pad : { 0, 3, 7 })
        {
            juce::AudioBuffer<float> out;
            const int g0 = p.activity.grainWrite.load();
            render (p, { { 0.0, firstPadNote + pad, true }, { 0.3, firstPadNote + pad, false } }, 0.4, out);
            const int g1 = p.activity.grainWrite.load();
            for (int k = g0; k < g1; ++k)
            {
                const float pos = p.activity.grainPos[(size_t) (k % Activity::size)].load();
                if (pos < p.pads.start[(size_t) pad].load() - 1.0e-4f || pos > p.pads.end[(size_t) pad].load() + 1.0e-4f) ok = false;
            }
        }
        check (ok, "each key only plays inside its own pad region");
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, firstPadNote + 20, true }, { 0.3, firstPadNote + 20, false } }, 0.4, out);
        check (s.grains == 0 && s.peak < 1.0e-5f, "keys above the last pad stay silent");
    }

    // ---- rate restarts on the grid, length gates
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.setParam ("gridDiv", 1);   // 1/2 -> 4 pads of 1 s
        p.loadFile (loopFile);
        p.setParam ("rate", 10);     // 1/16 = 0.125 s at 120
        p.setParam ("length", 0.5f);
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, firstPadNote, true }, { 1.999, firstPadNote, false } }, 2.2, out, false, &head);
        check (s.restarts == 16, "1/16 for 2 s = 16 restarts (" + juce::String (s.restarts) + ")");
        double on = 0, off = 0;
        for (int k = 2; k < 14; ++k) { on += rmsOf (out, k * 0.125 + 0.01, k * 0.125 + 0.05); off += rmsOf (out, k * 0.125 + 0.085, k * 0.125 + 0.12); }
        check (off < on * 0.12, "LENGTH 50% leaves the second half of every step quiet (" + juce::String (off / juce::jmax (1e-9, on), 3) + ")");
        writeWav (outDir.getChildFile ("01_rate_1-16_length50.wav"), out);
    }

    // ---- per-pad rate and length with LINK
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.setParam ("gridDiv", 1);
        p.loadFile (loopFile);
        p.setParam ("rate", 0);                 // global: no restarts
        p.setPadRate (1, 12);                   // pad 2: own 1/32
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        auto s = render (p, { { 0.0, firstPadNote, true }, { 1.0, firstPadNote, false } }, 1.1, out, false, &head);
        check (s.restarts == 1, "linked pad follows global RATE off (1 restart)");
        s = render (p, { { 0.0, firstPadNote + 1, true }, { 1.0, firstPadNote + 1, false } }, 1.1, out, false, &head);
        check (s.restarts == 16, "pad with its own 1/32 restarts 16 times per second at 120 BPM (" + juce::String (s.restarts) + ")");
        p.setPadRate (1, -1);                   // LINK on again
        s = render (p, { { 0.0, firstPadNote + 1, true }, { 1.0, firstPadNote + 1, false } }, 1.1, out, false, &head);
        check (s.restarts == 1, "LINK returns the pad to the global setting");
        p.setPadLength (0, 0.3f); p.setPadReverse (0, true); p.resetPad (0);
        check (! p.pads.hasOwnSettings (0), "RESET PAD clears own settings");
        p.setPadRate (3, 4); p.setPadLength (2, 0.2f); p.resetAllPads();
        check (! p.pads.hasOwnSettings (2) && ! p.pads.hasOwnSettings (3), "RESET ALL clears every pad");
    }

    // ---- automation of RATE while playing
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.loadFile (loopFile);
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, firstPadNote, true }, { 2.0, firstPadNote, false } }, 2.2, out, true, &head,
                               [&p] (int block) { if (block % 20 == 0) p.setParam ("rate", (float) ((block / 20) % 14)); });
        check (s.finite && s.peak <= 1.0f && s.restarts > 4, "RATE automated while a key is held (" + juce::String (s.restarts) + " restarts)");
    }

    // ---- scan, reverse, pitch
    {
        PrismaProcessor p;
        p.setParam ("divide", 1); p.setParam ("count", 0);   // 4 equal pads of 1 s
        p.loadFile (loopFile);
        p.setParam ("spread", 0.0f);
        p.setParam ("scan", 0.0f);
        juce::AudioBuffer<float> out;
        int g0 = p.activity.grainWrite.load();
        render (p, { { 0.0, firstPadNote, true }, { 0.8, firstPadNote, false } }, 0.9, out);
        float maxPos = 0.0f;
        for (int k = g0; k < p.activity.grainWrite.load(); ++k) maxPos = std::max (maxPos, p.activity.grainPos[(size_t) (k % Activity::size)].load());
        check (maxPos < 0.01f, "SCAN 0 (freeze) stays at the start of the pad");
        p.setParam ("scan", 1.0f);
        g0 = p.activity.grainWrite.load();
        render (p, { { 0.0, firstPadNote, true }, { 0.8, firstPadNote, false } }, 0.9, out);
        maxPos = 0.0f;
        for (int k = g0; k < p.activity.grainWrite.load(); ++k) maxPos = std::max (maxPos, p.activity.grainPos[(size_t) (k % Activity::size)].load());
        check (maxPos > 0.15f && maxPos < 0.25f, "SCAN 1 moves through the pad in real time (" + juce::String (maxPos, 3) + " after 0.8 s)");
        p.setPadReverse (0, true);
        g0 = p.activity.grainWrite.load();
        render (p, { { 0.0, firstPadNote, true }, { 0.5, firstPadNote, false } }, 0.6, out);
        float minPos = 1.0f;
        for (int k = g0; k < p.activity.grainWrite.load(); ++k) minPos = std::min (minPos, p.activity.grainPos[(size_t) (k % Activity::size)].load());
        check (minPos > 0.1f, "REVERSE starts from the end of the pad");
    }

    // ---- clicks with a pure sine, rate on and off
    {
        for (int rate : { 0, 10, 12 })
        {
            PrismaProcessor p;
            p.setParam ("divide", 1); p.setParam ("count", 0);
            p.loadFile (sineFile);
            p.setParam ("rate", (float) rate);
            p.setParam ("grainSize", 120.0f);
            p.setParam ("density", 40.0f);
            p.setParam ("length", 0.7f);
            FakePlayHead head;
            juce::AudioBuffer<float> out;
            const auto s = render (p, { { 0.0, firstPadNote, true }, { 1.5, firstPadNote, false } }, 1.8, out, false, &head);
            const double d2 = maxSecondDiff (out, 100, (int) (1.45 * SR));
            check (s.finite && d2 < 0.02 && s.rms > 0.02f, "no clicks, rate " + rateNames()[rate] + " (max 2nd diff " + juce::String (d2, 4) + ")");
        }
    }

    // ---- presets, chords, dice, stress
    {
        const auto names = presetNames();
        for (int i = 0; i < names.size(); ++i)
        {
            PrismaProcessor p;
            p.setParam ("sampleBpm", 120.0f);
            p.loadFile (loopFile);
            p.applyPreset (i);
            FakePlayHead head;
            Events ev { { 0.0, firstPadNote, true }, { 1.0, firstPadNote, false }, { 1.1, firstPadNote + 5, true },
                        { 1.1, firstPadNote + 9, true }, { 2.5, firstPadNote + 5, false }, { 2.5, firstPadNote + 9, false } };
            juce::AudioBuffer<float> out;
            const auto s = render (p, ev, 4.0, out, i % 2 == 1, &head);
            check (s.finite && s.peak <= 1.0f && s.rms > 0.003f, "preset " + names[i] + " (peak " + juce::String (s.peak, 2)
                                                                   + ", rms " + juce::String (s.rms, 3) + ", CPU " + juce::String (s.cpu, 1) + " ms/s)");
            writeWav (outDir.getChildFile ("p" + juce::String (i).paddedLeft ('0', 2) + "_" + names[i].replaceCharacters (" /", "__") + ".wav"), out);
        }

        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.loadFile (loopFile);
        bool ok = true;
        juce::AudioBuffer<float> out;
        for (int k = 0; k < 8; ++k)
        {
            p.dice();
            p.setParam ("crunch", (float) (k % 3) / 2.0f);
            p.setParam ("wash", (float) ((k + 1) % 3) / 2.0f);
            p.setPadRate (k, k % 14);
            Events ev;
            for (int c = 0; c < 8; ++c) { ev.push_back ({ 0.05 * c, firstPadNote + c * 2, true }); ev.push_back ({ 1.5, firstPadNote + c * 2, false }); }
            const auto s = render (p, ev, 2.5, out, true);
            ok &= s.finite && s.peak <= 1.0f;
        }
        check (ok, "8 dice runs with 8-note chords and per-pad rates");

        p.setParam ("grainSize", 500.0f); p.setParam ("density", 100.0f);
        Events ev;
        for (int c = 0; c < 8; ++c) ev.push_back ({ 0.0, firstPadNote + c, true });
        const auto s = render (p, ev, 2.0, out);
        std::cout << "  stress (8 voices, 500 ms grains, 100/s): CPU " << s.cpu << " ms per second" << std::endl;
        check (s.finite && s.peak <= 1.0f && s.cpu < 500.0, "stress test");
    }

    // ---- editing regions while playing, state, settings text
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.loadFile (loopFile);
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, firstPadNote, true }, { 1.5, firstPadNote, false } }, 1.6, out, true, nullptr,
                               [&p] (int block) { p.setPadRegion (0, 0.01f * (float) (block % 50), 0.3f + 0.01f * (float) (block % 30)); });
        check (s.finite && p.manualEdits, "dragging a region while it plays");

        p.setPadRate (4, 11); p.setPadReverse (5, true); p.setParam ("chaos", 0.4f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        PrismaProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = q.padCount() == p.padCount() && q.manualEdits;
        for (int i = 0; i < p.padCount(); ++i)
            same &= std::abs (q.pads.start[(size_t) i].load() - p.pads.start[(size_t) i].load()) < 1e-5f
                 && q.pads.rate[(size_t) i].load() == p.pads.rate[(size_t) i].load()
                 && q.pads.reverse[(size_t) i].load() == p.pads.reverse[(size_t) i].load();
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                same &= std::abs (rp->getValue() - q.apvts.getParameter (rp->getParameterID())->getValue()) < 1e-5f;
        check (same && q.getSample() != nullptr, "project save/load keeps sample, hand-edited pads and per-pad settings");

        PrismaProcessor c;
        c.loadFile (loopFile);
        const int k = c.settingsFromText (p.settingsToText());
        bool same2 = c.padCount() == p.padCount();
        for (int i = 0; i < p.padCount(); ++i)
            same2 &= std::abs (c.pads.end[(size_t) i].load() - p.pads.end[(size_t) i].load()) < 1e-5f && c.pads.rate[(size_t) i].load() == p.pads.rate[(size_t) i].load();
        check (k > 20 && same2, "copy / paste settings including pads (" + juce::String (k) + " values)");
    }

    // ---- project tempo drives the grid when Sample BPM is 0
    {
        PrismaProcessor p;
        p.hostBpm.store (90.0f);
        p.setParam ("gridDiv", 2);   // 1/4 at 90 BPM = 0.667 s -> 4 s / 0.667 = 6 pads
        p.loadFile (loopFile);
        check (p.padCount() == 6, "grid uses the project tempo (90 BPM, 1/4 = 6 pads, got " + juce::String (p.padCount()) + ")");
    }

    // ---- loading, clearing, re-slicing while playing
    {
        PrismaProcessor p;
        p.loadFile (loopFile);
        p.setPlayConfigDetails (0, 2, SR, 256);
        p.prepareToPlay (SR, 256);
        bool ok = true;
        for (int k = 0; k < 600; ++k)
        {
            juce::AudioBuffer<float> blk (2, 256);
            juce::MidiBuffer m;
            if (k % 40 == 0) m.addEvent (juce::MidiMessage::noteOn (1, firstPadNote + (k / 40) % 12, (juce::uint8) 90), 0);
            p.processBlock (blk, m);
            if (k % 90 == 45) p.loadFile (k % 180 == 45 ? sineFile : loopFile);
            if (k % 200 == 150) p.clearSample();
            if (k % 13 == 0) { p.setParam ("gridDiv", (float) (k % 6)); p.reslice(); }
            p.releaseRetired();
            for (int i = 0; i < 256; ++i) ok &= std::isfinite (blk.getSample (0, i));
        }
        check (ok, "loading, clearing and re-slicing while playing");
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
