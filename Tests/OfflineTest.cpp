// Offline tests for Prisma 0.4 (rhythmic grain slicer)
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

// richer 2-bar loop for the preview video: keys chords, bass, kick, snare, hats (120 BPM)
static juce::AudioBuffer<float> makeDemoLoop()
{
    const int n = (int) (4.0 * SR);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random r (5);
    const int chords[4][4] = { { 57, 60, 64, 67 }, { 53, 57, 60, 64 }, { 50, 53, 57, 60 }, { 52, 55, 59, 62 } };
    const int bass[4] = { 45, 41, 38, 40 };
    auto hz = [] (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); };
    double lpL = 0, lpR = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR, beat = t * 2.0;
        const int bar4 = (int) (beat / 2.0) % 4;               // chord every 2 beats
        const double cb = std::fmod (beat, 2.0) / 2.0;         // 0..1 inside chord
        double keysL = 0, keysR = 0;
        for (int k = 0; k < 4; ++k)
        {
            const double f = hz (chords[bar4][k]);
            double v = 0;
            for (int h = 1; h <= 5; ++h) v += std::sin (juce::MathConstants<double>::twoPi * f * h * t * (1.0 + 0.0007 * k)) / (h * h);
            const double env = std::exp (-cb * 2.2) * (0.6 + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 5.0 * t));
            keysL += v * env * (k % 2 == 0 ? 0.11 : 0.07);
            keysR += v * env * (k % 2 == 0 ? 0.07 : 0.11);
        }
        const double bf = hz (bass[bar4]);
        const double bassV = (std::sin (juce::MathConstants<double>::twoPi * bf * t) + 0.3 * std::sin (juce::MathConstants<double>::twoPi * bf * 2 * t))
                             * 0.22 * std::exp (-std::fmod (beat, 1.0) * 1.5);
        const double bt = std::fmod (beat, 2.0) * 0.5;          // seconds since kick (beats 1 and 3)
        const double kick = std::sin (juce::MathConstants<double>::twoPi * (50.0 * bt + 90.0 * (1.0 - std::exp (-bt * 30.0)) / 30.0)) * std::exp (-bt * 9.0) * 0.6;
        const double st = std::fmod (beat + 1.0, 2.0) * 0.5;    // snare on 2 and 4
        const double snare = ((r.nextFloat() - 0.5) * 0.7 + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 190.0 * st)) * std::exp (-st * 18.0);
        const double ht = std::fmod (beat, 0.5) * 0.5;
        const double hat = (r.nextFloat() - 0.5) * 0.18 * std::exp (-ht * 70.0);
        lpL += 0.25 * (keysL - lpL); lpR += 0.25 * (keysR - lpR);
        const double l = lpL + bassV + kick + snare * 0.9 + hat * 0.7;
        const double rr = lpR + bassV + kick + snare * 0.9 + hat * 1.0;
        b.setSample (0, i, (float) std::tanh (l * 0.9));
        b.setSample (1, i, (float) std::tanh (rr * 0.9));
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

    if (std::getenv ("PRISMA_VIDEO") != nullptr)
    {
        // frames (30 fps) + audio of the plugin playing, for a preview video
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        auto demoFile = dir.getChildFile ("demo_loop_120bpm.wav");
        writeWav (demoFile, makeDemoLoop());
        p.loadFile (demoFile);
        FakePlayHead head;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* pe = dynamic_cast<PrismaEditor*> (ed.get());
        p.setPlayHead (&head);
        p.setPlayConfigDetails (0, 2, SR, 800);
        p.prepareToPlay (SR, 800);
        auto frames = outDir.getChildFile ("frames");
        frames.deleteRecursively();
        frames.createDirectory();
        const double seconds = 16.0;
        const int total = (int) (seconds * SR);
        juce::AudioBuffer<float> audio (2, total);
        audio.clear();
        // section: preset, then (beat, pad, beats held)
        struct Hit { double beat; int pad; double len; };
        struct Section { int preset; std::vector<Hit> hits; };
        const std::vector<Section> song = {
            { 1, { { 0, 0, 2 }, { 2, 4, 2 }, { 4, 2, 1.5 }, { 5.5, 6, 2.5 } } },                       // Chop 1/16
            { 2, { { 8, 1, 4 }, { 8, 9, 4 }, { 12, 5, 4 } } },                                         // Long Grains
            { 10, { { 16, 3, 1 }, { 17, 11, 1 }, { 18, 7, 2 }, { 20, 14, 2 }, { 22, 0, 2 } } },         // Glitch
            { 9, { { 24, 8, 4 }, { 24, 12, 4 }, { 28, 2, 4 } } },                                      // Shimmer
        };
        const auto presetIdx = [&] (int s) { return song[(size_t) s].preset; };
        int section = -1, frame = 0;
        const int step = 1600;   // 30 fps
        for (int pos = 0; pos < total; pos += step, ++frame)
        {
            const double beat = head.ppq;
            const int sec = juce::jlimit (0, (int) song.size() - 1, (int) (beat / 8.0));
            if (sec != section) { section = sec; p.applyPreset (presetIdx (sec)); }
            for (int half = 0; half < 2; ++half)
            {
                const int bs = juce::jmin (step / 2, total - (pos + half * step / 2));
                if (bs <= 0) break;
                juce::AudioBuffer<float> blk (2, bs);
                juce::MidiBuffer m;
                const double b0 = head.ppq, b1 = head.ppq + bs * 2.0 / SR;   // 120 BPM: 2 beats per second
                for (auto& sct : song)
                    for (auto& h : sct.hits)
                    {
                        if (h.beat >= b0 && h.beat < b1) m.addEvent (juce::MidiMessage::noteOn (1, firstPadNote + h.pad, (juce::uint8) 110), (int) ((h.beat - b0) * SR / 2.0));
                        const double off = h.beat + h.len - 0.02;
                        if (off >= b0 && off < b1) m.addEvent (juce::MidiMessage::noteOff (1, firstPadNote + h.pad), (int) ((off - b0) * SR / 2.0));
                    }
                p.processBlock (blk, m);
                for (int c = 0; c < 2; ++c) audio.copyFrom (c, pos + half * step / 2, blk, c, 0, bs);
                head.ppq = b1;
                pe->animate();
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            juce::FileOutputStream os (frames.getChildFile ("f" + juce::String (frame).paddedLeft ('0', 4) + ".png"));
            png.writeImageToStream (img, os);
        }
        writeWav (outDir.getChildFile ("video_audio.wav"), audio);
        std::cout << "video frames: " << frame << std::endl;
        p.setPlayHead (nullptr);
        return 0;
    }

    if (const char* snap = std::getenv ("PRISMA_SNAPSHOT"))
    {
        PrismaProcessor p;
        if (snap[0] != '2') p.loadFile (loopFile);
        p.setParam ("rate", 9);
        p.setParam ("length", 220.0f);
        p.setParam ("chaos", 0.3f);
        p.setPadRate (2, 11);
        p.setPadLength (2, 80.0f);
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

    // ---- keys play their own region (whole grains stay inside, even with chaos)
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.setParam ("gridDiv", 2);   // 1/4 -> 8 pads of 0.5 s
        p.loadFile (loopFile);
        p.setParam ("length", 400.0f);
        bool ok = true;
        for (float chaos : { 0.0f, 0.9f })
        {
            p.setParam ("chaos", chaos);
            p.setParam ("pitch", chaos > 0.0f ? -7.0f : 0.0f);
            for (int pad : { 0, 3, 7 })
            {
                juce::AudioBuffer<float> out;
                const int g0 = p.activity.grainWrite.load();
                FakePlayHead head;
                render (p, { { 0.0, firstPadNote + pad, true }, { 0.9, firstPadNote + pad, false } }, 1.0, out, false, &head);
                const int g1 = p.activity.grainWrite.load();
                for (int k = g0; k < g1; ++k)
                {
                    const auto i = (size_t) (k % Activity::size);
                    const float a = p.activity.grainPos[i].load(), e = a + p.activity.grainLen[i].load();
                    const float lo = p.pads.start[(size_t) pad].load() - 1.0e-4f, hi = p.pads.end[(size_t) pad].load() + 1.0e-4f;
                    if (std::min (a, e) < lo || std::max (a, e) > hi) ok = false;
                }
            }
        }
        check (ok, "each key only plays inside its own pad region (also with chaos and pitch down)");
        p.setParam ("chaos", 0.0f); p.setParam ("pitch", 0.0f);
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, firstPadNote + 20, true }, { 0.3, firstPadNote + 20, false } }, 0.4, out);
        check (s.grains == 0 && s.peak < 1.0e-5f, "keys above the last pad stay silent");
    }

    // ---- grains fire on the grid; short LENGTH leaves gaps
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.setParam ("gridDiv", 1);   // 1/2 -> 4 pads of 1 s
        p.loadFile (loopFile);
        p.setParam ("rate", 9);      // 1/16 = 0.125 s at 120
        p.setParam ("length", 60.0f);
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, firstPadNote, true }, { 1.999, firstPadNote, false } }, 2.2, out, false, &head);
        check (s.grains == 16, "1/16 for 2 s = 16 grains (" + juce::String (s.grains) + ")");
        double on = 0, off = 0;
        for (int k = 2; k < 14; ++k) { on += rmsOf (out, k * 0.125 + 0.01, k * 0.125 + 0.05); off += rmsOf (out, k * 0.125 + 0.075, k * 0.125 + 0.12); }
        check (off < on * 0.12, "LENGTH 60 ms leaves a gap before the next 1/16 (" + juce::String (off / juce::jmax (1e-9, on), 3) + ")");
        writeWav (outDir.getChildFile ("01_grains_1-16_60ms.wav"), out);

        // long grains overlap and keep a steady level
        p.setParam ("length", 350.0f);
        juce::AudioBuffer<float> out2;
        int maxActive = 0;
        render (p, { { 0.0, firstPadNote, true }, { 1.999, firstPadNote, false } }, 2.2, out2, false, &head,
                [&] (int) { maxActive = juce::jmax (maxActive, p.activity.grainWrite.load() > 0 ? 1 : 0); });
        const double r1 = rmsOf (out, 0.3, 1.8), r2 = rmsOf (out2, 0.3, 1.8);
        check (r2 > r1 * 0.9 && r2 < r1 * 3.5, "350 ms grains overlap without blowing up (rms " + juce::String (r1, 3) + " -> " + juce::String (r2, 3) + ")");
        double gaps = 0;
        for (int k = 2; k < 14; ++k) gaps += rmsOf (out2, k * 0.125 + 0.075, k * 0.125 + 0.12);
        check (gaps > on * 0.4, "overlapping grains have no gaps");
        writeWav (outDir.getChildFile ("02_grains_1-16_350ms.wav"), out2);
    }

    // ---- free rate (SYNC off)
    {
        PrismaProcessor p;
        p.setParam ("divide", 1); p.setParam ("count", 0);
        p.loadFile (loopFile);
        p.setParam ("sync", 0.0f);
        p.setParam ("hz", 10.0f);
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, firstPadNote, true }, { 0.999, firstPadNote, false } }, 1.1, out);
        check (s.grains >= 10 && s.grains <= 11, "free 10 Hz = 10 grains per second (" + juce::String (s.grains) + ")");
    }

    // ---- per-pad rate and length with LINK
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.setParam ("gridDiv", 1);
        p.loadFile (loopFile);
        p.setParam ("rate", 3);                 // global 1/4 = 2 per second
        p.setPadRate (1, 11);                   // pad 2: own 1/32
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        auto s = render (p, { { 0.0, firstPadNote, true }, { 0.999, firstPadNote, false } }, 1.1, out, false, &head);
        check (s.grains == 2, "linked pad follows the global 1/4 (" + juce::String (s.grains) + " grains)");
        s = render (p, { { 0.0, firstPadNote + 1, true }, { 0.999, firstPadNote + 1, false } }, 1.1, out, false, &head);
        check (s.grains == 16, "pad with its own 1/32 fires 16 times per second at 120 BPM (" + juce::String (s.grains) + ")");
        p.setPadRate (1, -1);
        s = render (p, { { 0.0, firstPadNote + 1, true }, { 0.999, firstPadNote + 1, false } }, 1.1, out, false, &head);
        check (s.grains == 2, "LINK returns the pad to the global setting");

        p.setParam ("length", 400.0f);
        p.setPadLength (2, 30.0f);
        s = render (p, { { 0.0, firstPadNote + 2, true }, { 0.999, firstPadNote + 2, false } }, 1.1, out, false, &head);
        check (rmsOf (out, 0.1, 0.45) < rmsOf (out, 0.01, 0.03) * 0.05, "pad with its own 30 ms length goes quiet between hits");
        p.setPadLength (0, 300.0f); p.setPadReverse (0, true); p.resetPad (0);
        check (! p.pads.hasOwnSettings (0), "RESET PAD clears own settings");
        p.setPadRate (3, 4); p.setPadLength (2, 20.0f); p.resetAllPads();
        check (! p.pads.hasOwnSettings (2) && ! p.pads.hasOwnSettings (3), "RESET ALL clears every pad");
    }

    // ---- automation while playing
    {
        PrismaProcessor p;
        p.setParam ("sampleBpm", 120.0f);
        p.loadFile (loopFile);
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, firstPadNote, true }, { 2.0, firstPadNote, false } }, 2.2, out, true, &head,
                               [&p] (int block)
                               {
                                   if (block % 20 == 0) p.setParam ("rate", (float) ((block / 20) % numRates));
                                   if (block % 33 == 0) p.setParam ("sync", (float) ((block / 33) % 2));
                                   if (block % 7 == 0) p.setParam ("length", 10.0f + (float) ((block * 37) % 990));
                                   if (block % 11 == 0) p.setParam ("shape", (float) ((block / 11) % 4));
                               });
        check (s.finite && s.peak <= 1.0f && s.grains > 8, "rate, sync, length and shape automated while a key is held (" + juce::String (s.grains) + " grains)");
    }

    // ---- scan, position, reverse
    {
        PrismaProcessor p;
        p.setParam ("divide", 1); p.setParam ("count", 0);   // 4 equal pads of 1 s
        p.loadFile (loopFile);
        p.setParam ("scan", 0.0f);
        p.setParam ("length", 80.0f);
        juce::AudioBuffer<float> out;
        auto grainRange = [&p] (int g0, float& lo, float& hi)
        {
            lo = 1.0f; hi = 0.0f;
            for (int k = g0; k < p.activity.grainWrite.load(); ++k)
            {
                const float v = p.activity.grainPos[(size_t) (k % Activity::size)].load();
                lo = std::min (lo, v); hi = std::max (hi, v);
            }
        };
        float lo, hi;
        int g0 = p.activity.grainWrite.load();
        render (p, { { 0.0, firstPadNote, true }, { 0.8, firstPadNote, false } }, 0.9, out);
        grainRange (g0, lo, hi);
        check (hi < 0.002f, "SCAN 0 (freeze) repeats the start of the pad");
        p.setParam ("position", 0.5f);
        g0 = p.activity.grainWrite.load();
        render (p, { { 0.0, firstPadNote, true }, { 0.8, firstPadNote, false } }, 0.9, out);
        grainRange (g0, lo, hi);
        check (std::abs (lo - 0.125f) < 0.003f && hi < 0.128f, "POSITION 50% starts in the middle of the pad");
        p.setParam ("position", 0.0f);
        p.setParam ("scan", 1.0f);
        g0 = p.activity.grainWrite.load();
        render (p, { { 0.0, firstPadNote, true }, { 0.8, firstPadNote, false } }, 0.9, out);
        grainRange (g0, lo, hi);
        check (hi > 0.15f && hi < 0.25f, "SCAN 1 moves through the pad in real time (" + juce::String (hi, 3) + " after 0.8 s)");
        p.setPadReverse (0, true);
        g0 = p.activity.grainWrite.load();
        render (p, { { 0.0, firstPadNote, true }, { 0.5, firstPadNote, false } }, 0.6, out);
        grainRange (g0, lo, hi);
        check (lo > 0.1f, "REVERSE starts from the end of the pad");
    }

    // ---- clicks with a pure sine: every shape, gaps and overlaps
    {
        for (int shape = 0; shape < 4; ++shape)
            for (float len : { 70.0f, 300.0f })
            {
                PrismaProcessor p;
                p.setParam ("divide", 1); p.setParam ("count", 0);
                p.loadFile (sineFile);
                p.setParam ("rate", 9);
                p.setParam ("shape", (float) shape);
                p.setParam ("length", len);
                p.setParam ("space", 0.6f);
                FakePlayHead head;
                juce::AudioBuffer<float> out;
                const auto s = render (p, { { 0.0, firstPadNote, true }, { 1.5, firstPadNote, false } }, 1.8, out, false, &head);
                const double d2 = maxSecondDiff (out, 100, (int) (1.45 * SR));
                check (s.finite && d2 < 0.02 && s.rms > 0.01f, "no clicks, shape " + shapeNames()[shape] + " " + juce::String ((int) len)
                                                               + " ms (max 2nd diff " + juce::String (d2, 4) + ")");
            }
    }

    // ---- space makes it wide, 0 keeps it centred
    {
        PrismaProcessor p;
        p.setParam ("divide", 1); p.setParam ("count", 0);
        p.loadFile (sineFile);
        auto sideRatio = [&p] (float space)
        {
            p.setParam ("space", space);
            FakePlayHead head;
            juce::AudioBuffer<float> out;
            render (p, { { 0.0, firstPadNote, true }, { 1.0, firstPadNote, false } }, 1.0, out, false, &head);
            double m = 0, sd = 0;
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const double l = out.getSample (0, i), r = out.getSample (1, i);
                m += (l + r) * (l + r); sd += (l - r) * (l - r);
            }
            return std::sqrt (sd / juce::jmax (1e-12, m));
        };
        const double s0 = sideRatio (0.0f), s1 = sideRatio (1.0f);
        check (s0 < 0.02 && s1 > 0.25, "SPACE 0 = mono, SPACE 100% = wide (side/mid " + juce::String (s0, 3) + " -> " + juce::String (s1, 3) + ")");
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
            p.setPadRate (k, k % numRates);
            Events ev;
            for (int c = 0; c < 8; ++c) { ev.push_back ({ 0.05 * c, firstPadNote + c * 2, true }); ev.push_back ({ 1.5, firstPadNote + c * 2, false }); }
            const auto s = render (p, ev, 2.5, out, true);
            ok &= s.finite && s.peak <= 1.0f;
        }
        check (ok, "8 dice runs with 8-note chords and per-pad rates");

        p.resetAllPads();
        p.setParam ("rate", 13); p.setParam ("length", 1000.0f); p.setParam ("chaos", 0.5f); p.setParam ("space", 1.0f);
        Events ev;
        for (int c = 0; c < 8; ++c) ev.push_back ({ 0.0, firstPadNote + c, true });
        const auto s = render (p, ev, 2.0, out);
        std::cout << "  stress (8 voices, 1 s grains every 1/64): CPU " << s.cpu << " ms per second" << std::endl;
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

        p.setPadRate (4, 11); p.setPadLength (6, 250.0f); p.setPadReverse (5, true); p.setParam ("chaos", 0.4f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        PrismaProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = q.padCount() == p.padCount() && q.manualEdits;
        for (int i = 0; i < p.padCount(); ++i)
            same &= std::abs (q.pads.start[(size_t) i].load() - p.pads.start[(size_t) i].load()) < 1e-5f
                 && q.pads.rate[(size_t) i].load() == p.pads.rate[(size_t) i].load()
                 && std::abs (q.pads.length[(size_t) i].load() - p.pads.length[(size_t) i].load()) < 0.01f
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

    // ---- a v0.3 project: pads keep their regions, old per-pad rate/length are linked again
    {
        PrismaProcessor p;
        p.loadFile (loopFile);
        p.settingsFromText ("pads=0.0:0.25:12:0.5:1:1.0;0.25:0.5:-1:-1:0:1.0\n");
        check (p.padCount() == 2 && p.pads.rate[0].load() == -1 && p.pads.length[0].load() < 0.0f && p.pads.reverse[0].load() == 1
               && std::abs (p.pads.end[0].load() - 0.25f) < 1e-5f, "old v0.3 pad settings load safely");
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
