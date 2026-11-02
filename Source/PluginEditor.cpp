#include "PluginEditor.h"
#include "Presets.h"

using namespace prisma;

static const juce::uint32 palette[] = { 0xffff2e88, 0xff2ee6ff, 0xffe8ff3a, 0xffff7a1a, 0xff9b5cff, 0xff3aff9e, 0xffff3a3a };

//==============================================================================
Display::Display (PrismaProcessor& p, std::function<void (int)> onSelect) : proc (p), select (std::move (onSelect))
{
    colA.fill (juce::Colours::magenta);
    colB.fill (juce::Colours::cyan);
    readGrain = proc.activity.grainWrite.load();
    readRestart = proc.activity.restartWrite.load();
}

void Display::resized() { overlay = makeScreenOverlay (getWidth(), getHeight()); }

juce::Rectangle<float> Display::waveArea() const
{
    return getLocalBounds().toFloat().reduced (18.0f, 0.0f).withTrimmedTop (34.0f).withTrimmedBottom (66.0f);
}

juce::Rectangle<float> Display::stripArea() const
{
    auto r = getLocalBounds().toFloat().reduced (18.0f, 0.0f);
    return r.removeFromBottom (54.0f).withTrimmedBottom (14.0f);
}

juce::Rectangle<float> Display::padCell (int i, int n) const
{
    auto a = stripArea();
    const float gap = n > 32 ? 2.0f : 3.0f;
    const float w = (a.getWidth() - gap * (float) (n - 1)) / (float) juce::jmax (1, n);
    return { a.getX() + (float) i * (w + gap), a.getY(), w, a.getHeight() };
}

float Display::xOf (float norm) const   { auto a = waveArea(); return a.getX() + norm * a.getWidth(); }
float Display::normOf (float x) const   { auto a = waveArea(); return juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()); }

float Display::snap (float norm, bool allowSnap) const
{
    if (! allowSnap || proc.getParam ("snap") < 0.5f) return norm;
    float best = norm, bd = 1.0e9f;
    for (auto l : proc.snapLines())
        if (std::abs (l - norm) < bd) { bd = std::abs (l - norm); best = l; }
    if (std::abs (1.0f - norm) < bd) best = 1.0f;
    return best;
}

void Display::drawWave (juce::Graphics& g, const Sample& s, juce::Rectangle<float> area, float from, float to,
                        juce::Colour c, float xOffset) const
{
    if (s.overview.empty()) return;
    g.setColour (c);
    const float mid = area.getCentreY();
    const float x0 = xOf (from), x1 = xOf (to);
    const int nb = (int) s.overview.size();
    for (float x = std::floor (x0); x < x1; x += 1.0f)
    {
        const float n0 = normOf (x), n1 = normOf (x + 1.0f);
        int b0 = (int) (n0 * (float) nb), b1 = juce::jmax (b0 + 1, (int) (n1 * (float) nb));
        float pk = 0.0f;
        for (int b = b0; b < b1 && b < nb; ++b) pk = juce::jmax (pk, s.overview[(size_t) b]);
        const float a = juce::jmax (0.5f, pk * area.getHeight() * 0.47f);
        g.fillRect (x + xOffset, mid - a, 1.0f, a * 2.0f);
    }
}

void Display::tick()
{
    ++frame;
    auto& act = proc.activity;
    const auto wave = waveArea();

    const int rw = act.restartWrite.load (std::memory_order_acquire);
    if (rw - readRestart > Activity::restartSize) readRestart = rw - Activity::restartSize;
    for (; readRestart < rw; ++readRestart)
    {
        const int pad = act.restartPad[(size_t) (readRestart % Activity::restartSize)].load();
        if (pad < 0 || pad >= maxPads) continue;
        energy[(size_t) pad] = 1.0f;
        const int a = rng.nextInt (7);
        colA[(size_t) pad] = juce::Colour (palette[a]);
        colB[(size_t) pad] = juce::Colour (palette[(a + 2 + rng.nextInt (4)) % 7]);
    }

    const int gw = act.grainWrite.load (std::memory_order_acquire);
    if (gw - readGrain > Activity::size) readGrain = gw - Activity::size;
    int added = 0;
    for (; readGrain < gw; ++readGrain)
    {
        if (++added > 40) continue;   // keep the screen readable at high densities
        const auto i = (size_t) (readGrain % Activity::size);
        const float x = xOf (act.grainPos[i].load());
        const int pitch = act.grainPitch[i].load();
        const float y = wave.getCentreY() - (float) pitch * wave.getHeight() / 60.0f + (rng.nextFloat() - 0.5f) * wave.getHeight() * 0.7f;
        particles.push_back ({ x, y, 1.0f, 3.0f + rng.nextFloat() * 6.0f, juce::Colour (palette[rng.nextInt (7)]) });
    }
    for (auto& p : particles) { p.life -= 0.045f; p.y -= 0.4f; }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }), particles.end());
    if (particles.size() > 400) particles.erase (particles.begin(), particles.begin() + (long) (particles.size() - 400));

    for (auto& e : energy) e *= 0.9f;
    if (statusFrames > 0) --statusFrames;
    repaint();
}

void Display::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Theme::screen());
    g.fillRoundedRectangle (bounds, 6.0f);

    auto smp = proc.getSample();
    const int n = proc.padCount();
    const auto wave = waveArea();

    // header
    const auto header = bounds.reduced (18.0f, 0.0f).withHeight (30.0f).withY (6.0f);
    g.setFont (monoFont (12.0f));
    g.setColour (Theme::lcd());
    juce::String left = "G-1  ";
    left << (smp != nullptr ? smp->name.toUpperCase().substring (0, 28) + "  " + juce::String (smp->seconds(), 2) + " S" : juce::String ("NO SAMPLE"));
    g.drawText (left, header, juce::Justification::centredLeft);
    const int divide = (int) proc.getParam ("divide");
    juce::String right;
    if (divide == DivideGrid)
        right << "GRID " << gridDivNames()[(int) proc.getParam ("gridDiv")] << (proc.getParam ("gridType") > 0.5f ? (proc.getParam ("gridType") > 1.5f ? " DOT" : " TRI") : "")
              << " @ " << juce::String (proc.sliceBpm(), 1) << " BPM";
    else if (divide == DivideEqual) right << "EQUAL " << countValue ((int) proc.getParam ("count"));
    else right << "TRANSIENTS";
    right << "   " << n << " PADS" << (proc.manualEdits ? " (EDITED)" : "");
    g.drawText (right, header, juce::Justification::centredRight);
    if (statusFrames > 0 && (statusFrames > 40 || (frame / 6) % 2 == 0))
    {
        g.setColour (juce::Colour (0xffffb23a));
        g.drawText (status.toUpperCase(), header.reduced (240.0f, 0.0f), juce::Justification::centred);
    }

    if (smp == nullptr)
    {
        g.setColour (Theme::lcd());
        g.setFont (monoFont (20.0f));
        g.drawText (juce::String ("DROP A SAMPLE HERE") + ((frame / 30) % 2 == 0 ? "_" : " "), bounds.withTrimmedBottom (30.0f), juce::Justification::centred);
        g.setColour (Theme::lcd().withAlpha (0.55f));
        g.setFont (monoFont (12.0f));
        g.drawText ("a loop, a vocal, a piece of a song: it gets sliced onto your keys from C1", bounds.withTrimmedTop (60.0f), juce::Justification::centred);
        if (proc.missingFile.isNotEmpty())
        {
            g.setColour (juce::Colour (0xffff6a4a));
            g.drawText ("MISSING FILE: " + proc.missingFile.toUpperCase(), bounds.withTrimmedTop (120.0f), juce::Justification::centred);
        }
    }
    else
    {
        // grid lines
        if (divide == DivideGrid || proc.getParam ("snap") > 0.5f)
        {
            g.setColour (juce::Colour (0xff1c1e22));
            for (auto l : proc.snapLines()) g.fillRect (xOf (l), wave.getY(), 1.0f, wave.getHeight());
        }
        // base waveform
        drawWave (g, *smp, wave, 0.0f, 1.0f, juce::Colour (0xff3a3e44), 0.0f);

        // pad regions
        for (int i = 0; i < n; ++i)
        {
            const float a = proc.pads.start[(size_t) i].load(), b = proc.pads.end[(size_t) i].load();
            juce::Rectangle<float> r (xOf (a), wave.getY(), juce::jmax (1.0f, xOf (b) - xOf (a)), wave.getHeight());
            const float e = energy[(size_t) i];
            if (e > 0.02f)
            {
                const auto ca = colA[(size_t) i], cb = colB[(size_t) i];
                g.setColour (ca.withAlpha (0.08f * e));
                g.fillRect (r);
                const int bands = 6;
                for (int k = 0; k < bands; ++k)
                {
                    auto band = r.withHeight (r.getHeight() / (float) bands).withY (r.getY() + r.getHeight() * (float) k / (float) bands);
                    const float slip = (rng.nextFloat() - 0.5f) * e * juce::jmin (36.0f, r.getWidth() * 0.4f);
                    const float dx = 1.5f + 6.0f * e;
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (band.expanded (40.0f, 0.0f).getIntersection (wave).toNearestInt());
                    drawWave (g, *smp, wave, a, b, ca.withAlpha (0.85f * e), slip - dx);
                    drawWave (g, *smp, wave, a, b, cb.withAlpha (0.85f * e), slip + dx);
                    drawWave (g, *smp, wave, a, b, juce::Colours::white.withAlpha (0.5f * e), slip);
                }
                if (rng.nextFloat() < 0.3f * e)
                {
                    g.setColour (cb.withAlpha (0.35f * e));
                    g.fillRect (r.getX() - 4.0f, r.getY() + rng.nextFloat() * r.getHeight(), r.getWidth() + 8.0f, 1.0f + rng.nextFloat() * 2.5f);
                }
            }
            // boundary tick and own-settings dot
            g.setColour (juce::Colour (0xff5a5f66));
            g.fillRect (r.getX(), wave.getY(), 1.0f, 8.0f);
            g.fillRect (r.getX(), wave.getBottom() - 8.0f, 1.0f, 8.0f);
            if (proc.pads.hasOwnSettings (i))
            {
                g.setColour (Theme::accent());
                g.fillEllipse (r.getX() + 3.0f, wave.getY() + 3.0f, 5.0f, 5.0f);
            }
        }

        // grains
        for (auto& p : particles)
        {
            g.setColour (p.c.withAlpha (0.25f * p.life));
            g.fillRect (p.x - p.size, p.y - p.size, p.size * 2.0f, p.size * 2.0f);
            g.setColour (p.c.withAlpha (0.9f * p.life));
            g.drawRect (p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size, 1.0f);
        }

        // selected pad frame with handles
        if (selected >= 0 && selected < n)
        {
            const float a = proc.pads.start[(size_t) selected].load(), b = proc.pads.end[(size_t) selected].load();
            juce::Rectangle<float> r (xOf (a), wave.getY() - 2.0f, juce::jmax (2.0f, xOf (b) - xOf (a)), wave.getHeight() + 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (r);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRect (r, 1.2f);
            g.fillRect (r.getX() - 2.0f, r.getCentreY() - 12.0f, 4.0f, 24.0f);
            g.fillRect (r.getRight() - 2.0f, r.getCentreY() - 12.0f, 4.0f, 24.0f);
            g.setFont (monoFont (11.0f));
            g.drawText (juce::String (selected + 1).paddedLeft ('0', 2) + " " + noteName (firstPadNote + selected),
                        juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 4.0f, 80.0f, 14.0f), juce::Justification::centredLeft);
        }

        // playheads
        for (int v = 0; v < numVoices; ++v)
        {
            const float lvl = proc.activity.voiceLevel[(size_t) v].load();
            if (proc.activity.voicePad[(size_t) v].load() < 0 || lvl < 0.01f) continue;
            const float x = xOf (proc.activity.voicePos[(size_t) v].load());
            g.setColour (juce::Colours::white.withAlpha (0.25f * lvl));
            g.fillRect (x - 3.0f, wave.getY(), 6.0f, wave.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.95f * lvl));
            g.fillRect (x - 0.75f, wave.getY(), 1.5f, wave.getHeight());
        }

        // pad strip
        g.setFont (monoFont (n > 32 ? 9.0f : 10.5f));
        for (int i = 0; i < n; ++i)
        {
            auto c = padCell (i, n);
            const bool sel = i == selected;
            const float e = energy[(size_t) i];
            if (sel) { g.setColour (Theme::lcd()); g.fillRect (c); }
            else
            {
                g.setColour (colA[(size_t) i].withAlpha (0.55f * e));
                g.fillRect (c);
                g.setColour (Theme::lcd().withAlpha (0.35f));
                g.drawRect (c, 1.0f);
            }
            g.setColour (sel ? Theme::screen() : Theme::lcd().withAlpha (0.8f));
            if (c.getWidth() > 13.0f) g.drawText (juce::String (i + 1), c.withTrimmedBottom (c.getHeight() * 0.45f), juce::Justification::centred);
            if (c.getWidth() > 22.0f)
            {
                g.setFont (monoFont (8.5f));
                g.drawText (noteName (firstPadNote + i), c.withTrimmedTop (c.getHeight() * 0.5f), juce::Justification::centred);
                g.setFont (monoFont (n > 32 ? 9.0f : 10.5f));
            }
            if (proc.pads.hasOwnSettings (i))
            {
                g.setColour (Theme::accent());
                g.fillEllipse (c.getRight() - 6.0f, c.getY() + 2.0f, 4.0f, 4.0f);
            }
        }
    }

    g.drawImageAt (overlay, 0, 0);
    if (dragOver)
    {
        g.setColour (Theme::accent());
        juce::Path p, dashed;
        p.addRoundedRectangle (bounds.reduced (6.0f), 6.0f);
        const float dash[] = { 8.0f, 6.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, p, dash, 2);
        g.fillPath (dashed);
    }
}

void Display::mouseMove (const juce::MouseEvent& e)
{
    const int n = proc.padCount();
    if (selected >= 0 && selected < n && waveArea().contains (e.position))
    {
        const float xa = xOf (proc.pads.start[(size_t) selected].load()), xb = xOf (proc.pads.end[(size_t) selected].load());
        if (std::abs (e.position.x - xa) < 6.0f || std::abs (e.position.x - xb) < 6.0f) { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); return; }
        if (e.position.x > xa && e.position.x < xb) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); return; }
    }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void Display::mouseDown (const juce::MouseEvent& e)
{
    drag = DragNone;
    const int n = proc.padCount();
    if (n == 0) return;

    if (stripArea().contains (e.position))
    {
        for (int i = 0; i < n; ++i)
            if (padCell (i, n).contains (e.position)) { select (i); return; }
        return;
    }
    if (! waveArea().contains (e.position)) return;

    if (selected >= 0 && selected < n)
    {
        const float xa = xOf (proc.pads.start[(size_t) selected].load()), xb = xOf (proc.pads.end[(size_t) selected].load());
        if (std::abs (e.position.x - xa) < 6.0f) drag = DragStart;
        else if (std::abs (e.position.x - xb) < 6.0f) drag = DragEnd;
        else if (e.position.x > xa && e.position.x < xb) drag = DragMove;
    }
    if (drag == DragNone)
    {
        // pick the smallest region under the mouse
        const float x = normOf (e.position.x);
        int best = -1; float bestLen = 2.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = proc.pads.start[(size_t) i].load(), b = proc.pads.end[(size_t) i].load();
            if (x >= a && x <= b && b - a < bestLen) { best = i; bestLen = b - a; }
        }
        if (best < 0) return;
        select (best);
        drag = DragMove;
    }
    dragAnchor = normOf (e.position.x);
    dragOrigStart = proc.pads.start[(size_t) selected].load();
    dragOrigEnd = proc.pads.end[(size_t) selected].load();
}

void Display::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == DragNone || selected < 0 || selected >= proc.padCount()) return;
    const bool allowSnap = ! e.mods.isShiftDown();
    const float x = normOf (e.position.x);
    float a = dragOrigStart, b = dragOrigEnd;
    if (drag == DragMove)
    {
        const float len = b - a;
        a = snap (juce::jlimit (0.0f, 1.0f - len, dragOrigStart + (x - dragAnchor)), allowSnap);
        a = juce::jlimit (0.0f, 1.0f - len, a);
        b = a + len;
    }
    else if (drag == DragStart) a = juce::jmin (snap (x, allowSnap), b - 0.0005f);
    else b = juce::jmax (snap (x, allowSnap), a + 0.0005f);
    proc.setPadRegion (selected, a, b);
}

void Display::mouseUp (const juce::MouseEvent&)
{
    if (drag != DragNone) setStatus ("Pad " + juce::String (selected + 1) + " region set");
    drag = DragNone;
}

bool Display::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void Display::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
        {
            setStatus (proc.loadFile (juce::File (f)));
            select (0);
            return;
        }
}

//==============================================================================
PrismaEditor::PrismaEditor (PrismaProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      display (p, [this] (int i) { selectPad (i); }),
      divideKeys (p.apvts, "divide", 3), typeKeys (p.apvts, "gridType", 3, { "STR", "TRI", "DOT" }),
      bpmBar (p.apvts, "sampleBpm", "BPM"), sensBar (p.apvts, "sensitivity", "SENS"),
      snapToggle (p.apvts, "snap", "SNAP"),
      rate (p.apvts, "rate", "RATE", true), length (p.apvts, "length", "LENGTH"),
      grainSize (p.apvts, "grainSize", "GRAIN SIZE"), density (p.apvts, "density", "DENSITY"),
      scan (p.apvts, "scan", "SCAN"), spread (p.apvts, "spread", "SPREAD"),
      chaos (p.apvts, "chaos", "CHAOS"), pitch (p.apvts, "pitch", "PITCH"),
      rateLink ("LINK", [this] { if (proc.pads.rate[(size_t) selected].load() >= 0) proc.setPadRate (selected, -1);
                                 else proc.setPadRate (selected, (int) proc.getParam ("rate"));
                                 refreshPadPanel(); }),
      lengthLink ("LINK", [this] { if (proc.pads.length[(size_t) selected].load() >= 0.0f) proc.setPadLength (selected, -1.0f);
                                   else proc.setPadLength (selected, proc.getParam ("length"));
                                   refreshPadPanel(); }),
      reverseButton ("REVERSE", [this] { proc.setPadReverse (selected, proc.pads.reverse[(size_t) selected].load() == 0); refreshPadPanel(); }),
      attack (p.apvts, "attack", "ATTACK"), decayTime (p.apvts, "decayTime", "DECAY"),
      sustain (p.apvts, "sustain", "SUSTAIN"), release (p.apvts, "release", "RELEASE"),
      lowCut (p.apvts, "lowCut", "LOW CUT"), highCut (p.apvts, "highCut", "HIGH CUT"), reso (p.apvts, "reso", "RESO"),
      crunch (p.apvts, "crunch", "CRUNCH"), wash (p.apvts, "wash", "WASH"), pump (p.apvts, "pump", "PUMP"),
      volume (p.apvts, "volume", "VOLUME")
{
    setLookAndFeel (&look);
    addAndMakeVisible (display);
    display.setTooltip ("Drop a sample. Drag a pad's frame to move it, drag its edges to resize (hold Shift to ignore the grid).");

    presetBox.addItemList (presetNames(), 1);
    presetBox.setTextWhenNothingSelected ("PRESETS");
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedItemIndex() >= 0) { proc.applyPreset (presetBox.getSelectedItemIndex()); display.setStatus ("Preset " + presetBox.getText()); }
    };
    addAndMakeVisible (presetBox);

    diceButton.onClick = [this] { proc.dice(); presetBox.setSelectedItemIndex (-1, juce::dontSendNotification); display.setStatus ("Dice"); };
    copyButton.setTooltip ("Copy all settings and pads as text (paste it into the chat)");
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (proc.settingsToText()); display.setStatus ("Settings copied"); };
    pasteButton.onClick = [this]
    {
        const int k = proc.settingsFromText (juce::SystemClipboard::getTextFromClipboard());
        display.setStatus (k > 0 ? "Applied " + juce::String (k) + " settings" : juce::String ("Nothing to paste"));
        refreshPadPanel();
    };
    loadButton.onClick = [this] { loadWithChooser(); };
    clearButton.onClick = [this] { proc.clearSample(); display.setStatus ("Cleared"); };
    resliceButton.setTooltip ("Slice again with the current settings (undoes hand edits)");
    resliceButton.onClick = [this] { proc.reslice(); display.setStatus ("Re-sliced: " + juce::String (proc.padCount()) + " pads"); };
    for (auto* b : { &diceButton, &copyButton, &pasteButton, &loadButton, &clearButton, &resliceButton }) addAndMakeVisible (b);

    gridBox.addItemList (gridDivNames(), 1);
    countBox.addItemList (countNames(), 1);
    gridAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "gridDiv", gridBox);
    countAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "count", countBox);
    bpmBar.slider.setTooltip ("Tempo of the sample. Drag down to 0 = use the project tempo");
    for (juce::Component* c : std::initializer_list<juce::Component*> { &divideKeys, &typeKeys, &gridBox, &countBox, &bpmBar, &sensBar, &snapToggle })
        addAndMakeVisible (c);

    // pad knobs
    padRate.slider.setRange (0.0, 13.0, 1.0);
    padRate.slider.textFromValueFunction = [] (double v) { return rateNames()[juce::jlimit (0, 13, juce::roundToInt (v))]; };
    padRate.slider.onValueChange = [this] { if (! updatingPad) { proc.setPadRate (selected, juce::roundToInt (padRate.slider.getValue())); refreshPadPanel(); } };
    padLength.slider.setRange (0.05, 1.0, 0.0);
    padLength.slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    padLength.slider.onValueChange = [this] { if (! updatingPad) { proc.setPadLength (selected, (float) padLength.slider.getValue()); refreshPadPanel(); } };
    rateLink.setTooltip ("On: this pad follows the main RATE. Turn the pad knob to give it its own.");
    lengthLink.setTooltip ("On: this pad follows the main LENGTH. Turn the pad knob to give it its own.");
    resetPadButton.onClick = [this] { proc.resetPad (selected); refreshPadPanel(); display.setStatus ("Pad reset"); };
    resetAllButton.onClick = [this] { proc.resetAllPads(); refreshPadPanel(); display.setStatus ("All pads reset"); };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &rate, &length, &grainSize, &density, &scan, &spread, &chaos, &pitch,
             &padRate, &padLength, &rateLink, &lengthLink, &reverseButton, &resetPadButton, &resetAllButton,
             &attack, &decayTime, &sustain, &release, &lowCut, &highCut, &reso, &crunch, &wash, &pump, &volume })
        addAndMakeVisible (c);

    setSize (1180, 812);
    selectPad (0);
    startTimerHz (60);
}

PrismaEditor::~PrismaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PrismaEditor::selectPad (int p)
{
    selected = juce::jlimit (0, maxPads - 1, p);
    display.setSelected (selected);
    refreshPadPanel();
    repaint (600, 452, 252, 30);
}

void PrismaEditor::refreshPadPanel()
{
    const juce::ScopedValueSetter<bool> svs (updatingPad, true);
    const int r = proc.pads.rate[(size_t) selected].load();
    const float l = proc.pads.length[(size_t) selected].load();
    rateLink.setOn (r < 0);
    lengthLink.setOn (l < 0.0f);
    reverseButton.setOn (proc.pads.reverse[(size_t) selected].load() != 0);
    padRate.slider.setValue (r >= 0 ? r : proc.getParam ("rate"), juce::dontSendNotification);
    padLength.slider.setValue (l >= 0.0f ? l : proc.getParam ("length"), juce::dontSendNotification);
    padRate.slider.getProperties().set ("dimmed", r < 0);
    padLength.slider.getProperties().set ("dimmed", l < 0.0f);
    padRate.slider.updateText();
    padLength.slider.updateText();
    padRate.repaint();
    padLength.repaint();
}

void PrismaEditor::loadWithChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile()) { display.setStatus (proc.loadFile (fc.getResult())); selectPad (0); }
                          });
}

void PrismaEditor::timerCallback()
{
    proc.releaseRetired();

    // grid follows the project tempo until you edit pads by hand
    if (proc.getSample() != nullptr && proc.getParam ("sampleBpm") < 1.0f && ! proc.manualEdits
        && proc.getParam ("divide") < 0.5f && std::abs (proc.hostBpm.load() - proc.bpmUsed) > 0.05)
        proc.reslice();

    // follow the pad you play
    const int played = proc.activity.lastPad.load();
    if (played != lastSeenPad) { lastSeenPad = played; if (played >= 0) selectPad (played); }

    // show only the slicing controls of the chosen mode
    const int divide = (int) proc.getParam ("divide");
    gridBox.setVisible (divide == DivideGrid);
    typeKeys.setVisible (divide == DivideGrid);
    bpmBar.setVisible (divide == DivideGrid);
    countBox.setVisible (divide == DivideEqual);
    sensBar.setVisible (divide == DivideTransients);

    // linked pad knobs mirror the main ones
    if (! padRate.slider.isMouseButtonDown() && ! padLength.slider.isMouseButtonDown()) refreshPadPanel();
    display.tick();
}

void PrismaEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    juce::ColourGradient body (Theme::bodyTop(), 0.0f, 0.0f, Theme::bodyBottom(), 0.0f, b.getBottom(), false);
    g.setGradientFill (body);
    g.fillAll();
    if (texture.isValid()) g.drawImageAt (texture, 0, 0);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawLine (1.0f, 0.0f, 1.0f, b.getBottom(), 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.drawLine (b.getRight() - 1.0f, 0.0f, b.getRight() - 1.0f, b.getBottom(), 2.0f);
    for (auto p : { juce::Point<float> (14.0f, 14.0f), juce::Point<float> (b.getRight() - 14.0f, 14.0f),
                    juce::Point<float> (14.0f, b.getBottom() - 14.0f), juce::Point<float> (b.getRight() - 14.0f, b.getBottom() - 14.0f) })
        drawScrew (g, p);

    g.setColour (Theme::ink());
    drawTracked (g, "PRISMA", { 34.0f, 18.0f, 200.0f, 38.0f }, labelFont (30.0f), 7.0f, juce::Justification::centredLeft);
    auto badge = juce::Rectangle<float> (232.0f, 27.0f, 40.0f, 21.0f);
    g.setColour (Theme::accent());
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (juce::Colours::white);
    g.setFont (labelFont (12.0f));
    g.drawText ("G-1", badge, juce::Justification::centred);
    g.setColour (Theme::label());
    drawTracked (g, "GRANULAR SLICER", { 284.0f, 28.0f, 220.0f, 20.0f }, labelFont (10.5f), 2.6f, juce::Justification::centredLeft);
    g.setFont (monoFont (10.0f));
    g.drawText (juce::String ("V") + PRISMA_VERSION, juce::Rectangle<float> (470.0f, 28.0f, 70.0f, 20.0f), juce::Justification::centredLeft);
    for (int i = 0; i < 7; ++i)
    {
        auto slot = juce::Rectangle<float> (556.0f + (float) i * 14.0f, 26.0f, 5.0f, 24.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (slot, 2.5f);
    }

    drawBezel (g, display.getBounds().toFloat());

    drawPanel (g, { 28, 452, 560, 340 }, "GRAIN  /  ALL PADS");
    drawPanel (g, { 600, 452, 250, 340 }, "PAD " + juce::String (selected + 1).paddedLeft ('0', 2) + "   " + noteName (firstPadNote + selected));
    drawPanel (g, { 862, 452, 290, 110 }, "AMP");
    drawPanel (g, { 862, 568, 290, 110 }, "TONE");
    drawPanel (g, { 862, 684, 290, 108 }, "COLOR  /  OUT");

    g.setColour (Theme::label());
    g.setFont (labelFont (10.5f, false));
    g.drawFittedText ("RATE: hold a key and the pad restarts on the grid.\nOFF = smooth grain cloud.",
                      juce::Rectangle<int> (44, 724, 180, 52), juce::Justification::centredLeft, 3);
}

void PrismaEditor::resized()
{
    presetBox.setBounds (680, 24, 200, 30);
    diceButton.setBounds (890, 24, 74, 30);
    copyButton.setBounds (972, 24, 86, 30);
    pasteButton.setBounds (1066, 24, 86, 30);

    display.setBounds (44, 84, 1092, 300);

    // slicing row
    loadButton.setBounds (28, 408, 72, 34);
    clearButton.setBounds (106, 408, 72, 34);
    divideKeys.setBounds (192, 408, 300, 34);
    gridBox.setBounds (504, 409, 80, 32);
    typeKeys.setBounds (592, 408, 210, 34);
    bpmBar.setBounds (810, 408, 130, 34);
    countBox.setBounds (504, 409, 100, 32);
    sensBar.setBounds (504, 408, 200, 34);
    snapToggle.setBounds (952, 408, 92, 34);
    resliceButton.setBounds (1050, 408, 102, 34);

    // grain panel
    rate.setBounds (40, 482, 180, 236);
    const int gx = 232, cw = 88;
    length.setBounds (gx, 480, cw, 130);
    grainSize.setBounds (gx + cw, 480, cw, 130);
    density.setBounds (gx + cw * 2, 480, cw, 130);
    scan.setBounds (gx + cw * 3, 480, cw, 130);
    spread.setBounds (gx, 640, cw, 130);
    chaos.setBounds (gx + cw, 640, cw, 130);
    pitch.setBounds (gx + cw * 2, 640, cw, 130);

    // pad panel
    padRate.setBounds (612, 478, 104, 118);
    rateLink.setBounds (726, 520, 112, 34);
    padLength.setBounds (612, 600, 104, 118);
    lengthLink.setBounds (726, 642, 112, 34);
    reverseButton.setBounds (612, 724, 108, 30);
    resetPadButton.setBounds (728, 724, 110, 30);
    resetAllButton.setBounds (612, 758, 226, 28);

    auto row = [] (juce::Rectangle<int> panel, std::initializer_list<juce::Component*> comps)
    {
        auto r = panel.reduced (8, 0).withTrimmedTop (24).withTrimmedBottom (4);
        const int w = r.getWidth() / (int) comps.size();
        for (auto* c : comps)
        {
            auto cell = r.removeFromLeft (w);
            c->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 72), cell.getHeight()));
        }
    };
    row ({ 862, 452, 290, 110 }, { &attack, &decayTime, &sustain, &release });
    row ({ 862, 568, 290, 110 }, { &lowCut, &highCut, &reso });
    row ({ 862, 684, 290, 108 }, { &crunch, &wash, &pump, &volume });

    texture = makePaperTexture (getWidth(), getHeight());
}
