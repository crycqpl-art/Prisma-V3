#include "PluginEditor.h"
#include "Presets.h"

using namespace prisma;

namespace
{
// vacuum fluorescent display colours
const juce::Colour vfd      (0xff5af5da);
const juce::Colour vfdDim   (0xff175a50);
const juce::Colour vfdFaint (0xff0b1a17);
const juce::Colour vfdAmber (0xffffb84a);
constexpr int dotPitch = 3;

juce::Colour spectrum (float hue, float alpha = 1.0f)
{
    hue -= std::floor (hue);
    return juce::Colour::fromHSV (hue, 0.62f, 1.0f, alpha);
}

juce::Image makeDots (int w, int h)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Graphics g (img);
    g.setColour (vfdFaint);
    for (int y = 1; y < h; y += dotPitch)
        for (int x = 1; x < w; x += dotPitch)
            g.fillRect (x, y, 2, 2);
    return img;
}

juce::Image makeVfdOverlay (int w, int h)
{
    w = juce::jmax (1, w); h = juce::jmax (1, h);
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Graphics g (img);
    // fine mesh in front of the glowing segments
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    for (int y = 0; y < h; y += dotPitch) g.fillRect (0, y, w, 1);
    juce::ColourGradient vig (juce::Colours::transparentBlack, (float) w * 0.5f, (float) h * 0.5f,
                              juce::Colours::black.withAlpha (0.5f), 0.0f, 0.0f, true);
    g.setGradientFill (vig);
    g.fillRect (0, 0, w, h);
    // smoked glass reflection
    juce::Path p;
    p.startNewSubPath (0.0f, 0.0f);
    p.lineTo ((float) w * 0.62f, 0.0f);
    p.lineTo ((float) w * 0.38f, (float) h * 0.5f);
    p.lineTo (0.0f, (float) h * 0.5f);
    p.closeSubPath();
    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.05f), 0.0f, 0.0f,
                                juce::Colours::transparentWhite, 0.0f, (float) h * 0.5f, false);
    g.setGradientFill (glass);
    g.fillPath (p);
    return img;
}
} // namespace

//==============================================================================
Display::Display (PrismaProcessor& p, std::function<void (int)> onSelect) : proc (p), select (std::move (onSelect))
{
    readGrain = proc.activity.grainWrite.load();
    readRestart = proc.activity.restartWrite.load();
}

void Display::resized()
{
    overlay = makeVfdOverlay (getWidth(), getHeight());
    dots = makeDots (getWidth(), getHeight());
    auto side = sideArea();
    waterfall = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) side.getWidth()), juce::jmax (1, (int) side.getHeight() - 74), true);
    waveFor = nullptr;
}

juce::Rectangle<float> Display::waveArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (18.0f).withTrimmedRight (196.0f).withTrimmedTop (36.0f).withTrimmedBottom (66.0f);
}

juce::Rectangle<float> Display::sideArea() const
{
    auto w = waveArea();
    return { w.getRight() + 22.0f, w.getY(), (float) getWidth() - 18.0f - (w.getRight() + 22.0f), w.getHeight() };
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

juce::Point<float> Display::prismIn() const  { auto s = sideArea(); return { s.getCentreX() - 16.0f, s.getY() + 41.0f }; }
juce::Point<float> Display::prismOut() const { auto s = sideArea(); return { s.getCentreX() + 2.0f, s.getY() + 62.0f }; }

// The waveform as a mask of VFD dots, so pieces of it can be drawn in any colour and position.
void Display::rebuildWave (const Sample& s)
{
    const auto wave = waveArea();
    const int w = (int) wave.getWidth(), h = (int) wave.getHeight();
    waveMask = juce::Image (juce::Image::SingleChannel, juce::jmax (1, w), juce::jmax (1, h), true);
    waveFor = &s; waveW = w;
    if (s.overview.empty()) return;
    juce::Graphics g (waveMask);
    g.setColour (juce::Colours::white);
    const int nb = (int) s.overview.size();
    const int mid = (h / 2 / dotPitch) * dotPitch + 1;
    for (int x = 1; x < w; x += dotPitch)
    {
        int b0 = (int) ((float) x / (float) w * (float) nb), b1 = juce::jmax (b0 + 1, (int) ((float) (x + dotPitch) / (float) w * (float) nb));
        float pk = 0.0f;
        for (int b = b0; b < b1 && b < nb; ++b) pk = juce::jmax (pk, s.overview[(size_t) b]);
        const int a = (int) (pk * (float) h * 0.47f);
        g.fillRect (x, mid, 2, 2);
        for (int k = dotPitch; k <= a; k += dotPitch)
        {
            g.fillRect (x, mid - k, 2, 2);
            g.fillRect (x, mid + k, 2, 2);
        }
    }
}

void Display::drawMask (juce::Graphics& g, juce::Rectangle<float> clip, juce::Colour c, float dx, float dy) const
{
    if (! waveMask.isValid()) return;
    const auto wave = waveArea();
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (clip.translated (dx, dy).getSmallestIntegerContainer());
    g.setColour (c);
    g.drawImageAt (waveMask, (int) std::round (wave.getX() + dx), (int) std::round (wave.getY() + dy), true);
}

void Display::drawVfdText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, float size, juce::Colour c) const
{
    g.setFont (monoFont (size));
    g.setColour (c.withAlpha (0.18f));
    for (auto d : { juce::Point<float> (-1.0f, 0.0f), juce::Point<float> (1.0f, 0.0f), juce::Point<float> (0.0f, -1.0f), juce::Point<float> (0.0f, 1.0f) })
        g.drawText (t, r.translated (d.x, d.y), j);
    g.setColour (c);
    g.drawText (t, r, j);
}

void Display::tick()
{
    ++frame;
    auto& act = proc.activity;
    auto smp = proc.getSample();

    const int rw = act.restartWrite.load (std::memory_order_acquire);
    if (rw - readRestart > Activity::restartSize) readRestart = rw - Activity::restartSize;
    for (; readRestart < rw; ++readRestart)
    {
        const int pad = act.restartPad[(size_t) (readRestart % Activity::restartSize)].load();
        if (pad >= 0 && pad < maxPads) energy[(size_t) pad] = 1.0f;
    }

    const int gw = act.grainWrite.load (std::memory_order_acquire);
    if (gw - readGrain > Activity::size) readGrain = gw - Activity::size;
    int added = 0;
    for (; readGrain < gw; ++readGrain)
    {
        if (++added > 12) continue;   // keep the screen readable at very fast rates
        const auto i = (size_t) (readGrain % Activity::size);
        const float pos = act.grainPos[i].load(), len = act.grainLen[i].load();
        const float hue = 0.47f + (float) act.grainPitch[i].load() / 30.0f + (rng.nextFloat() - 0.5f) * 0.05f;
        const float pan = act.grainPan[i].load();
        tiles.push_back ({ juce::jmin (pos, pos + len), juce::jmax (pos, pos + len), 1.0f, hue, pan });
        const double secs = smp != nullptr ? std::abs (len) * smp->seconds() : 0.1;
        const auto wf = (float) waterfall.getWidth();
        drops.push_back ({ 6.0f + pan * (wf - 12.0f), juce::jlimit (3.0f, 16.0f, 2.0f + (float) secs * 30.0f), hue,
                           juce::jlimit (2, 90, (int) (secs * 60.0)) });
        beam = 1.0f;
    }
    for (auto& t : tiles) t.life -= 0.032f;
    tiles.erase (std::remove_if (tiles.begin(), tiles.end(), [] (const Tile& t) { return t.life <= 0.0f; }), tiles.end());
    if (tiles.size() > 24) tiles.erase (tiles.begin(), tiles.begin() + (long) (tiles.size() - 24));

    // rhythm waterfall: scroll down one line, print what sounds now on the top line
    if (waterfall.isValid())
    {
        const int w = waterfall.getWidth(), h = waterfall.getHeight();
        if (frame % 2 == 0)
        {
            waterfall.moveImageSection (0, 1, 0, 0, w, h - 1);
            waterfall.clear ({ 0, 0, w, 1 });
            juce::Graphics g (waterfall);
            for (auto& d : drops)
            {
                g.setColour (spectrum (d.hue, 0.95f));
                g.fillRect (d.x - d.w * 0.5f, 0.0f, d.w, 1.0f);
                g.setColour (spectrum (d.hue + 0.08f, 0.35f));   // a thin split edge, like light through glass
                g.fillRect (d.x + d.w * 0.5f, 0.0f, 2.0f, 1.0f);
            }
            for (auto& d : drops) --d.framesLeft;
            drops.erase (std::remove_if (drops.begin(), drops.end(), [] (const Drop& d) { return d.framesLeft <= 0; }), drops.end());
        }
    }

    beam *= 0.9f;
    for (auto& e : energy) e *= 0.9f;
    if (statusFrames > 0) --statusFrames;
    repaint();
}

void Display::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Theme::screen());
    g.fillRoundedRectangle (bounds, 6.0f);
    g.drawImageAt (dots, 0, 0);

    auto smp = proc.getSample();
    const int n = proc.padCount();
    const auto wave = waveArea();
    const auto side = sideArea();

    // header line
    const auto header = bounds.reduced (18.0f, 0.0f).withHeight (28.0f).withY (6.0f);
    juce::String left = "G-1  ";
    left << (smp != nullptr ? smp->name.toUpperCase().substring (0, 28) + "  " + juce::String (smp->seconds(), 2) + " S" : juce::String ("NO SAMPLE"));
    drawVfdText (g, left, header, juce::Justification::centredLeft, 12.0f, vfd);
    const int divide = (int) proc.getParam ("divide");
    juce::String right;
    if (divide == DivideGrid)
        right << "GRID " << gridDivNames()[(int) proc.getParam ("gridDiv")] << (proc.getParam ("gridType") > 0.5f ? (proc.getParam ("gridType") > 1.5f ? " DOT" : " TRI") : "")
              << " @ " << juce::String (proc.sliceBpm(), 1) << " BPM";
    else if (divide == DivideEqual) right << "EQUAL " << countValue ((int) proc.getParam ("count"));
    else right << "TRANSIENTS";
    right << "   " << n << " PADS" << (proc.manualEdits ? " (EDITED)" : "");
    drawVfdText (g, right, header, juce::Justification::centredRight, 12.0f, vfd);
    if (statusFrames > 0 && (statusFrames > 40 || (frame / 6) % 2 == 0))
        drawVfdText (g, status.toUpperCase(), header.reduced (250.0f, 0.0f), juce::Justification::centred, 12.0f, vfdAmber);

    // grain rhythm readout above the prism
    {
        juce::String gr;
        if (proc.getParam ("sync") > 0.5f) gr << "GRAINS " << rateNames()[(int) proc.getParam ("rate")];
        else gr << "GRAINS " << juce::String (proc.getParam ("hz"), 1) << " HZ";
        gr << "  " << juce::roundToInt (proc.getParam ("length")) << " MS";
        drawVfdText (g, gr, side.withHeight (14.0f).translated (0.0f, -2.0f), juce::Justification::centred, 10.0f, vfdDim.brighter (0.6f));
    }

    // prism: light comes in from the grains, leaves split into colours
    {
        const float cx = side.getCentreX(), top = side.getY() + 16.0f;
        juce::Path tri;
        tri.addTriangle (cx, top, cx - 32.0f, top + 50.0f, cx + 32.0f, top + 50.0f);
        g.setColour (vfd.withAlpha (0.06f + 0.18f * beam));
        g.fillPath (tri);
        g.setColour (vfd.withAlpha (0.45f + 0.5f * beam));
        g.strokePath (tri, juce::PathStrokeType (1.2f));
        const auto out = prismOut();
        const auto wf = juce::Rectangle<float> (side.getX(), side.getY() + 74.0f, side.getWidth(), side.getHeight() - 74.0f);
        for (int k = 0; k < 7; ++k)
        {
            const float t = (float) k / 6.0f;
            g.setColour (spectrum (0.02f + t * 0.75f, 0.1f + 0.75f * beam));
            g.drawLine (out.x, out.y, wf.getX() + 6.0f + t * (wf.getWidth() - 12.0f), wf.getY(), 1.0f + beam);
        }
        // waterfall, fading as it falls
        g.drawImageAt (waterfall, (int) wf.getX(), (int) wf.getY());
        juce::ColourGradient fade (juce::Colours::transparentBlack, 0.0f, wf.getY(), Theme::screen().withAlpha (0.9f), 0.0f, wf.getBottom(), false);
        g.setGradientFill (fade);
        g.fillRect (wf);
        g.setColour (vfdDim.withAlpha (0.6f));
        g.drawRect (wf, 1.0f);
    }

    if (smp == nullptr)
    {
        drawVfdText (g, juce::String ("DROP A SAMPLE HERE") + ((frame / 30) % 2 == 0 ? "_" : " "), wave.withTrimmedBottom (20.0f),
                     juce::Justification::centred, 20.0f, vfd);
        drawVfdText (g, "a loop, a vocal, a piece of a song: it gets sliced onto your keys from C1", wave.withTrimmedTop (50.0f),
                     juce::Justification::centred, 11.0f, vfdDim.brighter (0.5f));
        if (proc.missingFile.isNotEmpty())
            drawVfdText (g, "MISSING FILE: " + proc.missingFile.toUpperCase(), wave.withTrimmedTop (110.0f), juce::Justification::centred, 11.0f, vfdAmber);
    }
    else
    {
        if (waveFor != smp.get() || waveW != (int) wave.getWidth()) rebuildWave (*smp);

        // grid as dotted lines
        if (divide == DivideGrid || proc.getParam ("snap") > 0.5f)
        {
            g.setColour (vfdDim.withAlpha (0.45f));
            for (auto l : proc.snapLines())
            {
                const float x = std::round (xOf (l));
                for (float y = wave.getY(); y < wave.getBottom(); y += 6.0f) g.fillRect (x, y, 1.0f, 2.0f);
            }
        }

        // whole sample, dim
        drawMask (g, wave, vfdDim, 0.0f, 0.0f);

        // pad regions: lit while they play
        for (int i = 0; i < n; ++i)
        {
            const float a = proc.pads.start[(size_t) i].load(), b = proc.pads.end[(size_t) i].load();
            juce::Rectangle<float> r (xOf (a), wave.getY(), juce::jmax (1.0f, xOf (b) - xOf (a)), wave.getHeight());
            const float e = energy[(size_t) i];
            if (e > 0.02f) drawMask (g, r, vfd.withAlpha (0.25f + 0.75f * e), 0.0f, 0.0f);
            g.setColour (vfdDim);
            g.fillRect (r.getX(), wave.getY(), 1.0f, 6.0f);
            g.fillRect (r.getX(), wave.getBottom() - 6.0f, 1.0f, 6.0f);
            if (proc.pads.hasOwnSettings (i))
            {
                g.setColour (vfdAmber);
                g.fillRect (r.getX() + 3.0f, wave.getY() + 2.0f, 4.0f, 4.0f);
            }
        }

        // selected pad frame with handles
        if (selected >= 0 && selected < n)
        {
            const float a = proc.pads.start[(size_t) selected].load(), b = proc.pads.end[(size_t) selected].load();
            juce::Rectangle<float> r (xOf (a), wave.getY() - 2.0f, juce::jmax (2.0f, xOf (b) - xOf (a)), wave.getHeight() + 4.0f);
            drawMask (g, r, vfd.withAlpha (0.55f), 0.0f, 0.0f);
            g.setColour (vfd.withAlpha (0.85f));
            g.drawRect (r, 1.0f);
            g.fillRect (r.getX() - 2.0f, r.getCentreY() - 12.0f, 4.0f, 24.0f);
            g.fillRect (r.getRight() - 2.0f, r.getCentreY() - 12.0f, 4.0f, 24.0f);
            drawVfdText (g, juce::String (selected + 1).paddedLeft ('0', 2) + " " + noteName (firstPadNote + selected),
                         { r.getX() + 5.0f, r.getY() + 4.0f, 80.0f, 14.0f }, juce::Justification::centredLeft, 11.0f, vfd);
        }

        // lifted tiles: the piece of sound each grain plays rises out of the wave and splits into colour
        for (auto& t : tiles)
        {
            const float age = 1.0f - t.life;
            const float lift = -std::pow (age, 0.7f) * wave.getHeight() * 0.22f;
            const float d = 1.0f + age * 9.0f;
            juce::Rectangle<float> r (xOf (t.from), wave.getY(), juce::jmax (3.0f, xOf (t.to) - xOf (t.from)), wave.getHeight());
            const float a = t.life * t.life;
            drawMask (g, r, juce::Colour (0xffff4d6d).withAlpha (0.55f * a), -d, lift);
            drawMask (g, r, juce::Colour (0xff4d7bff).withAlpha (0.55f * a), d, lift);
            drawMask (g, r, spectrum (t.hue, 0.9f * a), 0.0f, lift);
            if (t.life > 0.75f)
            {
                // the beam into the prism
                const auto in = prismIn();
                g.setColour (juce::Colours::white.withAlpha ((t.life - 0.75f) * 1.2f));
                g.drawLine (r.getCentreX(), wave.getY() + wave.getHeight() * 0.5f + lift, in.x, in.y, 1.0f);
            }
        }

        // playheads
        for (int v = 0; v < numVoices; ++v)
        {
            const float lvl = proc.activity.voiceLevel[(size_t) v].load();
            if (proc.activity.voicePad[(size_t) v].load() < 0 || lvl < 0.01f) continue;
            const float x = std::round (xOf (proc.activity.voicePos[(size_t) v].load()));
            g.setColour (vfd.withAlpha (0.2f * lvl));
            g.fillRect (x - 3.0f, wave.getY(), 7.0f, wave.getHeight());
            g.setColour (vfd.withAlpha (0.95f * lvl));
            for (float y = wave.getY(); y < wave.getBottom(); y += (float) dotPitch) g.fillRect (x, y, 2.0f, 2.0f);
        }

        // pad strip
        for (int i = 0; i < n; ++i)
        {
            auto c = padCell (i, n);
            const bool sel = i == selected;
            const float e = energy[(size_t) i];
            if (sel) { g.setColour (vfd); g.fillRect (c); }
            else
            {
                g.setColour (vfd.withAlpha (0.6f * e));
                g.fillRect (c);
                g.setColour (vfdDim);
                g.drawRect (c, 1.0f);
            }
            const auto tc = sel ? Theme::screen() : (e > 0.5f ? Theme::screen() : vfd.withAlpha (0.85f));
            g.setColour (tc);
            g.setFont (monoFont (n > 32 ? 9.0f : 10.5f));
            if (c.getWidth() > 13.0f) g.drawText (juce::String (i + 1), c.withTrimmedBottom (c.getHeight() * 0.45f), juce::Justification::centred);
            if (c.getWidth() > 22.0f)
            {
                g.setFont (monoFont (8.5f));
                g.drawText (noteName (firstPadNote + i), c.withTrimmedTop (c.getHeight() * 0.5f), juce::Justification::centred);
            }
            if (proc.pads.hasOwnSettings (i))
            {
                g.setColour (vfdAmber);
                g.fillRect (c.getRight() - 6.0f, c.getY() + 2.0f, 4.0f, 4.0f);
            }
        }
    }

    g.drawImageAt (overlay, 0, 0);
    if (dragOver)
    {
        g.setColour (vfdAmber);
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
      rate (p.apvts, "rate", "GRAINS", true), hz (p.apvts, "hz", "GRAINS", true),
      length (p.apvts, "length", "LENGTH"), position (p.apvts, "position", "POSITION"),
      scan (p.apvts, "scan", "SCAN"), pitch (p.apvts, "pitch", "PITCH"),
      chaos (p.apvts, "chaos", "CHAOS"), space (p.apvts, "space", "SPACE"),
      syncToggle (p.apvts, "sync", "SYNC"),
      shapeKeys (p.apvts, "shape", 2, { "SQUARE", "SOFT", "PLUCK", "SWELL" }),
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
    padRate.slider.setRange (0.0, (double) (numRates - 1), 1.0);
    padRate.slider.textFromValueFunction = [] (double v) { return rateNames()[juce::jlimit (0, numRates - 1, juce::roundToInt (v))]; };
    padRate.slider.onValueChange = [this] { if (! updatingPad) { proc.setPadRate (selected, juce::roundToInt (padRate.slider.getValue())); refreshPadPanel(); } };
    padLength.slider.setRange (minLengthMs, maxLengthMs, 0.0);
    padLength.slider.setSkewFactor (0.45);
    padLength.slider.textFromValueFunction = [] (double v) { return v >= 1000.0 ? juce::String ("1.00 s") : juce::String (juce::roundToInt (v)) + " ms"; };
    padLength.slider.onValueChange = [this] { if (! updatingPad) { proc.setPadLength (selected, (float) padLength.slider.getValue()); refreshPadPanel(); } };
    rateLink.setTooltip ("On: this pad follows the main GRAINS rate. Turn the pad knob to give it its own (always synced).");
    rate.slider.setTooltip ("How often a grain fires, locked to the project grid");
    hz.slider.setTooltip ("How often a grain fires, free (not locked to tempo)");
    length.slider.setTooltip ("How long each grain plays. Longer than the grain rate = grains overlap");
    position.slider.setTooltip ("Where in the pad the grains start");
    scan.slider.setTooltip ("How fast the grains move through the pad. 1x = normal, 0 = freeze");
    chaos.slider.setTooltip ("Random position, pitch, skipped hits, octaves and reverse grains");
    space.slider.setTooltip ("Stereo width: grains spread left and right");
    lengthLink.setTooltip ("On: this pad follows the main LENGTH. Turn the pad knob to give it its own.");
    resetPadButton.onClick = [this] { proc.resetPad (selected); refreshPadPanel(); display.setStatus ("Pad reset"); };
    resetAllButton.onClick = [this] { proc.resetAllPads(); refreshPadPanel(); display.setStatus ("All pads reset"); };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &rate, &hz, &syncToggle, &shapeKeys, &length, &position, &scan, &pitch, &chaos, &space,
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
    const bool sync = proc.getParam ("sync") > 0.5f;
    rate.setVisible (sync);
    hz.setVisible (! sync);

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
    drawTracked (g, "SHAPE", { (float) shapeKeys.getX(), (float) shapeKeys.getY() - 20.0f, 120.0f, 14.0f }, labelFont (10.0f), 1.8f,
                 juce::Justification::centredLeft);
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
    rate.setBounds (40, 482, 180, 222);
    hz.setBounds (40, 482, 180, 222);
    syncToggle.setBounds (70, 728, 120, 34);
    const int gx = 232, cw = 88;
    length.setBounds (gx, 480, cw, 130);
    position.setBounds (gx + cw, 480, cw, 130);
    scan.setBounds (gx + cw * 2, 480, cw, 130);
    pitch.setBounds (gx + cw * 3, 480, cw, 130);
    chaos.setBounds (gx, 640, cw, 130);
    space.setBounds (gx + cw, 640, cw, 130);
    shapeKeys.setBounds (gx + cw * 2 + 6, 666, cw * 2 - 12, 92);

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
