#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace prisma;

static const char* sliceParams[] = { "divide", "gridDiv", "gridType", "sampleBpm", "count", "sensitivity" };

juce::AudioProcessorValueTreeState::ParameterLayout PrismaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v < 10.0f ? v : (float) roundToInt (v), v < 10.0f ? 1 : 0) + " ms"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });
    auto st = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " st"; });
    auto plain = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); });
    auto perSec = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " /s"; });
    auto speed = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 0.005f ? String ("freeze") : String (v, 2) + "x"; });
    auto bpm = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 1.0f ? String ("Project") : String (v, 1); });

    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, AudioParameterFloatAttributes a)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, a)); };
    auto c = [&] (const char* id, const char* name, StringArray items, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };

    // grain
    c ("rate", "Rate", rateNames(), 0);
    f ("length", "Length", { 0.05f, 1.0f }, 1.0f, pct);
    f ("grainSize", "Grain Size", { 10.0f, 500.0f, 0.0f, 0.45f }, 80.0f, ms);
    f ("density", "Density", { 1.0f, 100.0f, 0.0f, 0.55f }, 30.0f, perSec);
    f ("scan", "Scan", { 0.0f, 2.0f, 0.0f, 0.7f }, 1.0f, speed);
    f ("spread", "Spread", { 0.0f, 1.0f, 0.0f, 0.6f }, 0.05f, pct);
    f ("chaos", "Chaos", { 0.0f, 1.0f }, 0.0f, pct);
    f ("pitch", "Pitch", { -24.0f, 24.0f, 1.0f }, 0.0f, st);
    // slicing
    c ("divide", "Divide", divideNames(), 0);
    c ("gridDiv", "Grid", gridDivNames(), 3);
    c ("gridType", "Grid Type", gridTypeNames(), 0);
    f ("sampleBpm", "Sample BPM", { 0.0f, 300.0f, 0.1f }, 0.0f, bpm);
    c ("count", "Slices", countNames(), 3);
    f ("sensitivity", "Sensitivity", { 0.0f, 1.0f }, 0.5f, pct);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "snap", 1 }, "Snap", true));
    // amp
    f ("attack", "Attack", { 0.5f, 2000.0f, 0.0f, 0.3f }, 3.0f, ms);
    f ("decayTime", "Decay", { 5.0f, 4000.0f, 0.0f, 0.3f }, 300.0f, ms);
    f ("sustain", "Sustain", { 0.0f, 1.0f }, 1.0f, pct);
    f ("release", "Release", { 5.0f, 5000.0f, 0.0f, 0.3f }, 200.0f, ms);
    // tone + color + out
    f ("lowCut", "Low Cut", { 20.0f, 2000.0f, 0.0f, 0.3f }, 20.0f, hz);
    f ("highCut", "High Cut", { 200.0f, 20000.0f, 0.0f, 0.3f }, 20000.0f, hz);
    f ("reso", "Resonance", { 0.5f, 5.0f, 0.0f, 0.5f }, 0.7f, plain);
    f ("crunch", "Crunch", { 0.0f, 1.0f }, 0.0f, pct);
    f ("wash", "Wash", { 0.0f, 1.0f }, 0.0f, pct);
    f ("pump", "Pump", { 0.0f, 1.0f }, 0.0f, pct);
    f ("volume", "Volume", { -30.0f, 12.0f }, 0.0f, db);
    return l;
}

PrismaProcessor::PrismaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PRISMA", createLayout())
{
    engine.setShared (&pads, &activity);
    for (auto* id : sliceParams) apvts.addParameterListener (id, this);
}

PrismaProcessor::~PrismaProcessor()
{
    cancelPendingUpdate();
    for (auto* id : sliceParams) apvts.removeParameterListener (id, this);
}

bool PrismaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

void PrismaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sr);
    fx.prepare (sr, samplesPerBlock);
}

Params PrismaProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    Params p;
    p.rate = (int) v ("rate");
    p.length = v ("length");
    p.grainMs = v ("grainSize");
    p.density = v ("density");
    p.scan = v ("scan");
    p.spread = v ("spread");
    p.chaos = v ("chaos");
    p.pitch = v ("pitch");
    p.attack = v ("attack") * 0.001f;
    p.decayTime = v ("decayTime") * 0.001f;
    p.sustain = v ("sustain");
    p.release = v ("release") * 0.001f;
    return p;
}

void PrismaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || buffer.getNumChannels() < 2) return;

    const auto p = readParams();
    const auto smp = std::atomic_load (&sample);

    double bpm = 120.0, ppq = internalPpq;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    hostBpm.store ((float) bpm);
    const double ppqPerSample = bpm / (60.0 * sr);
    engine.setGlobals (p, bpm);

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    if (smp != nullptr)
    {
        int cursor = 0;
        for (const auto meta : midi)
        {
            const int at = juce::jlimit (0, n - 1, meta.samplePosition);
            if (at > cursor)
            {
                engine.render (L + cursor, R + cursor, at - cursor, p, *smp, bpm, ppq + cursor * ppqPerSample, ppqPerSample);
                cursor = at;
            }
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), *smp, ppq + at * ppqPerSample);
            else if (msg.isNoteOff()) engine.noteOff (msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allNotesOff();
        }
        if (cursor < n)
            engine.render (L + cursor, R + cursor, n - cursor, p, *smp, bpm, ppq + cursor * ppqPerSample, ppqPerSample);
    }

    FX::Settings s;
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    s.lowCut = v ("lowCut"); s.highCut = v ("highCut"); s.reso = v ("reso");
    s.crunch = v ("crunch"); s.wash = v ("wash"); s.pump = v ("pump"); s.volumeDb = v ("volume");
    fx.process (L, R, n, s, ppq, ppqPerSample);

    internalPpq = ppq + n * ppqPerSample;
}

//==============================================================================
void PrismaProcessor::setSample (SamplePtr s)
{
    auto old = std::atomic_exchange (&sample, s);
    const juce::ScopedLock sl (retiredLock);
    if (old != nullptr) retired.push_back (old);
}

void PrismaProcessor::releaseRetired()
{
    const juce::ScopedLock sl (retiredLock);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const SamplePtr& p) { return p.use_count() == 1; }),
                   retired.end());
}

juce::String PrismaProcessor::loadFile (const juce::File& file)
{
    juce::String err;
    auto s = loadSample (file, err);
    if (s == nullptr) return "Could not load " + file.getFileName() + ": " + err;
    // stop old voices from reading the previous sample's regions: they keep their own region copies
    setSample (s);
    missingFile.clear();
    apvts.state.setProperty ("file", file.getFullPathName(), nullptr);
    reslice();
    return "Loaded " + s->name + ": " + juce::String (padCount()) + " pads";
}

void PrismaProcessor::clearSample()
{
    pads.count.store (0);
    setSample (nullptr);
    missingFile.clear();
    apvts.state.setProperty ("file", "", nullptr);
    storePads();
}

double PrismaProcessor::sliceBpm() const
{
    const float b = getParam ("sampleBpm");
    return b >= 1.0f ? (double) b : (double) hostBpm.load();
}

double PrismaProcessor::gridBeats() const
{
    return gridDivBeats ((int) getParam ("gridDiv")) * gridTypeFactor ((int) getParam ("gridType"));
}

std::vector<float> PrismaProcessor::snapLines() const
{
    auto s = getSample();
    if (s == nullptr) return {};
    return gridLines (*s, gridBeats(), sliceBpm());
}

void PrismaProcessor::reslice()
{
    auto s = getSample();
    if (s == nullptr || restoring) return;
    SliceSettings st;
    st.mode = (int) getParam ("divide");
    st.gridBeats = gridBeats();
    st.bpm = sliceBpm();
    st.equalCount = countValue ((int) getParam ("count"));
    st.sensitivity = getParam ("sensitivity");
    const auto regions = slice (*s, st);
    const int n = juce::jmin ((int) regions.size(), maxPads);
    // write the regions before publishing the new count
    for (int i = 0; i < n; ++i)
    {
        pads.start[(size_t) i].store (regions[(size_t) i].start);
        pads.end[(size_t) i].store (regions[(size_t) i].end);
    }
    pads.count.store (n);
    manualEdits = false;
    bpmUsed = st.bpm;
    storePads();
}

//==============================================================================
void PrismaProcessor::setPadRegion (int pad, float start, float end)
{
    if (pad < 0 || pad >= maxPads) return;
    start = juce::jlimit (0.0f, 1.0f, start);
    end = juce::jlimit (0.0f, 1.0f, end);
    if (end < start) std::swap (start, end);
    if (end - start < 0.0005f) end = juce::jmin (1.0f, start + 0.0005f);
    pads.start[(size_t) pad].store (start);
    pads.end[(size_t) pad].store (end);
    manualEdits = true;
    storePads();
}

void PrismaProcessor::setPadRate (int pad, int r)     { if (pad >= 0 && pad < maxPads) { pads.rate[(size_t) pad].store (juce::jlimit (-1, 13, r)); storePads(); } }
void PrismaProcessor::setPadLength (int pad, float v) { if (pad >= 0 && pad < maxPads) { pads.length[(size_t) pad].store (v < 0.0f ? -1.0f : juce::jlimit (0.05f, 1.0f, v)); storePads(); } }
void PrismaProcessor::setPadReverse (int pad, bool r) { if (pad >= 0 && pad < maxPads) { pads.reverse[(size_t) pad].store (r ? 1 : 0); storePads(); } }
void PrismaProcessor::setPadGain (int pad, float g)   { if (pad >= 0 && pad < maxPads) { pads.gain[(size_t) pad].store (juce::jlimit (0.0f, 2.0f, g)); storePads(); } }

void PrismaProcessor::resetPad (int pad)
{
    if (pad < 0 || pad >= maxPads) return;
    pads.rate[(size_t) pad].store (-1);
    pads.length[(size_t) pad].store (-1.0f);
    pads.reverse[(size_t) pad].store (0);
    pads.gain[(size_t) pad].store (1.0f);
    storePads();
}

void PrismaProcessor::resetAllPads()
{
    for (int i = 0; i < maxPads; ++i)
    {
        pads.rate[(size_t) i].store (-1);
        pads.length[(size_t) i].store (-1.0f);
        pads.reverse[(size_t) i].store (0);
        pads.gain[(size_t) i].store (1.0f);
    }
    storePads();
}

juce::String PrismaProcessor::padsToString() const
{
    juce::StringArray items;
    for (int i = 0; i < pads.count.load(); ++i)
        items.add (juce::String (pads.start[(size_t) i].load(), 6) + ":" + juce::String (pads.end[(size_t) i].load(), 6) + ":"
                   + juce::String (pads.rate[(size_t) i].load()) + ":" + juce::String (pads.length[(size_t) i].load(), 4) + ":"
                   + juce::String (pads.reverse[(size_t) i].load()) + ":" + juce::String (pads.gain[(size_t) i].load(), 3));
    return items.joinIntoString (";");
}

void PrismaProcessor::padsFromString (const juce::String& text)
{
    auto items = juce::StringArray::fromTokens (text, ";", "");
    items.removeEmptyStrings();
    const int n = juce::jmin (maxPads, items.size());
    for (int i = 0; i < n; ++i)
    {
        auto f = juce::StringArray::fromTokens (items[i], ":", "");
        if (f.size() < 6) continue;
        pads.start[(size_t) i].store (juce::jlimit (0.0f, 1.0f, f[0].getFloatValue()));
        pads.end[(size_t) i].store (juce::jlimit (0.0f, 1.0f, f[1].getFloatValue()));
        pads.rate[(size_t) i].store (juce::jlimit (-1, 13, f[2].getIntValue()));
        pads.length[(size_t) i].store (f[3].getFloatValue() < 0.0f ? -1.0f : juce::jlimit (0.05f, 1.0f, f[3].getFloatValue()));
        pads.reverse[(size_t) i].store (f[4].getIntValue() != 0 ? 1 : 0);
        pads.gain[(size_t) i].store (juce::jlimit (0.0f, 2.0f, f[5].getFloatValue()));
    }
    pads.count.store (n);
}

void PrismaProcessor::storePads()
{
    apvts.state.setProperty ("pads", padsToString(), nullptr);
    apvts.state.setProperty ("manual", manualEdits, nullptr);
    apvts.state.setProperty ("bpmUsed", bpmUsed, nullptr);
}

//==============================================================================
void PrismaProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
        prm->endChangeGesture();
    }
}

float PrismaProcessor::getParam (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void PrismaProcessor::dice()
{
    auto& r = uiRandom;
    const int rates[] = { 0, 0, 7, 8, 10, 10, 11, 12 };
    setParam ("rate", (float) rates[r.nextInt (8)]);
    setParam ("length", 0.35f + 0.65f * r.nextFloat());
    setParam ("grainSize", 20.0f + 300.0f * r.nextFloat() * r.nextFloat());
    setParam ("density", 8.0f + 60.0f * r.nextFloat());
    setParam ("scan", r.nextFloat() < 0.25f ? 0.0f : 0.25f + 1.25f * r.nextFloat());
    setParam ("spread", 0.3f * r.nextFloat() * r.nextFloat());
    setParam ("chaos", r.nextFloat() < 0.5f ? 0.0f : 0.6f * r.nextFloat());
    const float pitches[] = { 0, 0, 0, -12, 12, 7, -5 };
    setParam ("pitch", pitches[r.nextInt (7)]);
}

juce::String PrismaProcessor::settingsToText() const
{
    juce::String t;
    t << "PRISMA " << PRISMA_VERSION << " settings\n";
    auto s = getSample();
    t << "# sample: " << (s != nullptr ? s->name : juce::String ("(none)")) << ", pads: " << padCount()
      << (manualEdits ? " (edited by hand)" : "") << ", grid bpm " << juce::String (bpmUsed, 1) << "\n";
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            t << rp->getParameterID() << "=" << juce::String (rp->convertFrom0to1 (rp->getValue()), 4)
              << "  # " << rp->getCurrentValueAsText() << "\n";
    t << "pads=" << padsToString() << "\n";
    return t;
}

int PrismaProcessor::settingsFromText (const juce::String& text)
{
    int applied = 0;
    juce::String padsLine;
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.upToFirstOccurrenceOf ("#", false, false).trim();
        if (! line.containsChar ('=')) continue;
        const auto id = line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto val = line.fromFirstOccurrenceOf ("=", false, false).trim();
        if (id == "pads") { padsLine = val; ++applied; }
        else if (apvts.getParameter (id) != nullptr && val.isNotEmpty()) { setParam (id, val.getFloatValue()); ++applied; }
    }
    if (padsLine.isNotEmpty())
    {
        cancelPendingUpdate();   // slicing parameters just changed: keep the pasted pads instead
        padsFromString (padsLine);
        manualEdits = true;
        storePads();
    }
    return applied;
}

//==============================================================================
void PrismaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pads", padsToString(), nullptr);
    state.setProperty ("manual", manualEdits, nullptr);
    state.setProperty ("bpmUsed", bpmUsed, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PrismaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    restoring = true;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    cancelPendingUpdate();
    restoring = false;

    const auto path = apvts.state.getProperty ("file").toString();
    const auto padText = apvts.state.getProperty ("pads").toString();
    manualEdits = (bool) apvts.state.getProperty ("manual", false);
    bpmUsed = (double) apvts.state.getProperty ("bpmUsed", 120.0);

    missingFile.clear();
    if (path.isEmpty()) { setSample (nullptr); pads.count.store (0); return; }
    juce::File f (path);
    juce::String err;
    auto s = f.existsAsFile() ? loadSample (f, err) : nullptr;
    if (s == nullptr) { missingFile = f.getFileName(); setSample (nullptr); pads.count.store (0); return; }
    setSample (s);
    if (padText.isNotEmpty()) padsFromString (padText);
    else reslice();
}

juce::AudioProcessorEditor* PrismaProcessor::createEditor()
{
    return new PrismaEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrismaProcessor();
}
