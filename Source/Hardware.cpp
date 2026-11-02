#include "Hardware.h"

namespace prisma
{

juce::Font labelFont (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

static float textWidth (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }

void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                  const juce::Font& font, float spacing, juce::Justification just)
{
    float total = 0.0f;
    for (int i = 0; i < text.length(); ++i)
        total += textWidth (font, text.substring (i, i + 1)) + (i + 1 < text.length() ? spacing : 0.0f);
    float x = area.getX();
    if (just.testFlags (juce::Justification::horizontallyCentred)) x = area.getCentreX() - total * 0.5f;
    else if (just.testFlags (juce::Justification::right)) x = area.getRight() - total;
    g.setFont (font);
    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text.substring (i, i + 1);
        const float w = textWidth (font, ch);
        g.drawText (ch, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()), juce::Justification::centredLeft, false);
        x += w + spacing;
    }
}

void drawKeyBody (juce::Graphics& g, juce::Rectangle<float> r, bool down, bool hover)
{
    r = r.reduced (1.0f);
    const float rad = 4.0f;
    g.setColour (juce::Colours::black.withAlpha (down ? 0.10f : 0.18f));
    g.fillRoundedRectangle (r.translated (0.0f, down ? 1.0f : 2.5f), rad);
    auto body = r.translated (0.0f, down ? 1.0f : 0.0f);
    juce::ColourGradient grad (down ? juce::Colour (0xffd6d2c9) : juce::Colour (0xfff8f7f3), body.getX(), body.getY(),
                               down ? juce::Colour (0xffe8e5de) : juce::Colour (0xffd9d5cc), body.getX(), body.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (body, rad);
    g.setColour (juce::Colour (0xffaea99f).withAlpha (hover ? 1.0f : 0.85f));
    g.drawRoundedRectangle (body, rad, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.2f : 0.7f));
    g.drawLine (body.getX() + rad, body.getY() + 1.2f, body.getRight() - rad, body.getY() + 1.2f, 1.0f);
}

void drawLed (juce::Graphics& g, juce::Point<float> c, bool on)
{
    if (on)
    {
        g.setColour (Theme::accent().withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (c));
        g.setColour (Theme::accent());
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillEllipse (juce::Rectangle<float> (2.5f, 2.5f).withCentre (c.translated (-1.0f, -1.0f)));
    }
    else
    {
        g.setColour (juce::Colour (0xff8f7a70).withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c), 0.8f);
    }
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> b, const juce::String& title)
{
    auto r = b.toFloat();
    g.setColour (Theme::panel());
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.drawLine (r.getX() + 8.0f, r.getBottom() + 0.5f, r.getRight() - 8.0f, r.getBottom() + 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.06f));
    g.drawLine (r.getX() + 8.0f, r.getY() + 1.5f, r.getRight() - 8.0f, r.getY() + 1.5f, 1.5f);
    g.setColour (Theme::label());
    drawTracked (g, title, { r.getX() + 14.0f, r.getY() + 8.0f, r.getWidth() - 28.0f, 14.0f }, labelFont (10.5f), 2.2f,
                 juce::Justification::centredLeft);
}

void drawScrew (juce::Graphics& g, juce::Point<float> c)
{
    g.setColour (juce::Colours::black.withAlpha (0.15f));
    g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (c.translated (0.0f, 1.0f)));
    juce::ColourGradient grad (juce::Colour (0xfff4f3ef), c.x, c.y - 6.0f, juce::Colour (0xffa9a49a), c.x, c.y + 6.0f, false);
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c));
    g.setColour (juce::Colour (0xff77736b));
    g.drawLine (c.x - 3.5f, c.y - 1.5f, c.x + 3.5f, c.y + 1.5f, 1.4f);
}

void drawBezel (juce::Graphics& g, juce::Rectangle<float> screen)
{
    auto bezel = screen.expanded (16.0f);
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillRoundedRectangle (bezel.translated (0.0f, 3.0f), 16.0f);
    juce::ColourGradient bz (juce::Colour (0xff3a3a3d), bezel.getX(), bezel.getY(), juce::Colour (0xff1d1d1f), bezel.getX(), bezel.getBottom(), false);
    g.setGradientFill (bz);
    g.fillRoundedRectangle (bezel, 16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (bezel.reduced (1.0f), 15.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (screen.expanded (2.0f), 8.0f, 2.0f);
}

juce::Image makePaperTexture (int w, int h)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Graphics g (img);
    juce::Random r (17);
    for (int i = 0; i < w * h / 40; ++i)
    {
        g.setColour ((r.nextBool() ? juce::Colours::black : juce::Colours::white).withAlpha (0.035f));
        g.fillRect (r.nextInt (juce::jmax (1, w)), r.nextInt (juce::jmax (1, h)), 1, 1);
    }
    return img;
}

juce::Image makeScreenOverlay (int w, int h)
{
    w = juce::jmax (1, w); h = juce::jmax (1, h);
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Graphics g (img);
    g.setColour (juce::Colours::black.withAlpha (0.32f));
    for (int y = 0; y < h; y += 3) g.fillRect (0, y, w, 1);
    juce::ColourGradient vig (juce::Colours::transparentBlack, (float) w * 0.5f, (float) h * 0.5f,
                              juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig);
    g.fillRect (0, 0, w, h);
    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.055f), 0.0f, 0.0f,
                                juce::Colours::transparentWhite, 0.0f, (float) h * 0.45f, false);
    g.setGradientFill (glass);
    g.fillRect (0, 0, w, h / 2);
    return img;
}

//==============================================================================
HardwareLook::HardwareLook()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xfff2f0eb));
    setColour (juce::PopupMenu::textColourId, Theme::ink());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent());
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, Theme::ink());
    setColour (juce::ComboBox::arrowColourId, Theme::label());
    setColour (juce::TextButton::textColourOffId, Theme::ink());
    setColour (juce::TextButton::textColourOnId, Theme::ink());
    setColour (juce::Label::textColourId, Theme::label());
    setColour (juce::Slider::textBoxTextColourId, Theme::ink());
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff2b2b2d));
    setColour (juce::TooltipWindow::textColourId, juce::Colour (0xffe9e7e2));
    setColour (juce::TooltipWindow::outlineColourId, juce::Colour (0xff2b2b2d));
}

void HardwareLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const float size = (float) juce::jmin (w, h);
    const float r = size * 0.5f - 7.0f;
    if (r < 6.0f) return;
    const juce::Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float ang = a0 + pos * (a1 - a0);
    const float alpha = s.isEnabled() ? 1.0f : 0.45f;
    const bool dimmed = (bool) s.getProperties()["dimmed"];

    g.setColour (juce::Colour (0xff8b877e).withMultipliedAlpha (alpha));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        const float rr = r + 4.5f;
        const float d = (i == 0 || i == 10 || i == 5) ? 2.6f : 1.7f;
        g.fillEllipse (c.x + rr * std::sin (a) - d * 0.5f, c.y - rr * std::cos (a) - d * 0.5f, d, d);
    }
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.3f, r * 2.3f).withCentre (c.translated (0.0f, r * 0.16f)));
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.06f, r * 2.06f).withCentre (c.translated (0.0f, r * 0.09f)));
    {
        juce::ColourGradient sk (juce::Colour (0xff5b5954), c.x, c.y - r, juce::Colour (0xff2d2c2a), c.x, c.y + r, false);
        g.setGradientFill (sk);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    }
    const float rb = r * 0.86f;
    {
        juce::ColourGradient side (juce::Colour (0xfffbfaf7), c.x, c.y - rb, juce::Colour (0xffc4c0b6), c.x, c.y + rb, false);
        g.setGradientFill (side);
        g.fillEllipse (juce::Rectangle<float> (rb * 2.0f, rb * 2.0f).withCentre (c));
    }
    g.setColour (juce::Colours::black.withAlpha (0.09f));
    for (int i = 0; i < 36; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f + ang;
        g.drawLine (c.x + rb * 0.88f * std::sin (a), c.y - rb * 0.88f * std::cos (a),
                    c.x + rb * 0.99f * std::sin (a), c.y - rb * 0.99f * std::cos (a), 1.0f);
    }
    const float rt = r * 0.70f;
    {
        juce::ColourGradient top (juce::Colours::white, c.x - rt * 0.4f, c.y - rt * 0.6f,
                                  juce::Colour (0xffe1ded6), c.x + rt * 0.5f, c.y + rt * 0.8f, true);
        g.setGradientFill (top);
        g.fillEllipse (juce::Rectangle<float> (rt * 2.0f, rt * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.drawEllipse (juce::Rectangle<float> (rt * 2.0f, rt * 2.0f).withCentre (c), 1.0f);
    }
    g.setColour (dimmed ? juce::Colour (0xffa7a29a) : Theme::accent().withMultipliedAlpha (alpha));
    const float lw = juce::jmax (2.2f, r * 0.08f);
    juce::Path line;
    line.startNewSubPath (c.x + rt * 0.25f * std::sin (ang), c.y - rt * 0.25f * std::cos (ang));
    line.lineTo (c.x + rt * 0.92f * std::sin (ang), c.y - rt * 0.92f * std::cos (ang));
    g.strokePath (line, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void HardwareLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                     juce::Slider::SliderStyle, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    drawKeyBody (g, r, false, s.isMouseOverOrDragging());
    auto inner = r.reduced (4.0f, 5.0f);
    g.setColour (Theme::accent().withAlpha (0.16f));
    g.fillRoundedRectangle (inner.withWidth (juce::jmax (0.0f, pos - inner.getX())), 3.0f);
    g.setColour (Theme::label());
    drawTracked (g, s.getProperties()["label"].toString(), inner.reduced (6.0f, 0.0f), labelFont (10.0f), 1.5f,
                 juce::Justification::centredLeft);
    g.setColour (Theme::ink());
    g.setFont (monoFont (12.0f));
    g.drawText (s.getTextFromValue (s.getValue()), inner.reduced (6.0f, 0.0f), juce::Justification::centredRight);
}

void HardwareLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    drawKeyBody (g, b.getLocalBounds().toFloat(), down, over);
}

void HardwareLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setColour (Theme::ink().withAlpha (b.isEnabled() ? 1.0f : 0.4f));
    drawTracked (g, b.getButtonText(), b.getLocalBounds().toFloat().translated (0.0f, down ? 1.0f : 0.0f),
                 labelFont (11.0f), 1.2f, juce::Justification::centred);
}

void HardwareLook::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    drawKeyBody (g, juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), down, box.isMouseOver());
    juce::Path p;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Theme::label());
    g.fillPath (p);
    if (! box.isEnabled())
    {
        g.setColour (Theme::bodyBottom().withAlpha (0.55f));
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), 4.0f);
    }
}

void HardwareLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 3);
    label.setFont (getComboBoxFont (box));
}

juce::Font HardwareLook::getComboBoxFont (juce::ComboBox&) { return labelFont (12.0f); }
juce::Font HardwareLook::getPopupMenuFont()                { return labelFont (13.0f, false); }

juce::Label* HardwareLook::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (monoFont ((bool) s.getProperties()["big"] ? 17.0f : 11.5f));
    l->setColour (juce::Label::textColourId, Theme::ink().withAlpha (0.8f));
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textWhenEditingColourId, Theme::ink());
    l->setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    l->setColour (juce::TextEditor::highlightColourId, Theme::accent().withAlpha (0.3f));
    return l;
}

//==============================================================================
static void setupKnob (Knob& k, const juce::String& name, bool isBig)
{
    k.big = isBig;
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, isBig ? 22 : 16);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, Theme::ink());
    k.slider.setMouseDragSensitivity (isBig ? 260 : 200);
    k.slider.getProperties().set ("big", isBig);
    k.addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (labelFont (isBig ? 13.0f : 10.5f));
    k.label.setColour (juce::Label::textColourId, Theme::label());
    k.label.setInterceptsMouseClicks (false, false);
    k.addAndMakeVisible (k.label);
}

Knob::Knob (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& name, bool isBig)
{
    setupKnob (*this, name, isBig);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, id, slider);
}

Knob::Knob (const juce::String& name) { setupKnob (*this, name, false); }

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (big ? 20 : 15));
    slider.setBounds (r);
}

ValueBar::ValueBar (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::LinearBar);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.getProperties().set ("label", name);
    slider.setMouseDragSensitivity (300);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, id, slider);
}

KeyGroup::KeyGroup (juce::AudioProcessorValueTreeState& st, const juce::String& id, int cols, juce::StringArray labels) : columns (cols)
{
    auto* param = st.getParameter (id);
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param)) options = c->choices;
    if (! labels.isEmpty()) options = labels;
    att = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { value = juce::roundToInt (v); repaint(); }, nullptr);
    att->sendInitialUpdate();
}

juce::Rectangle<float> KeyGroup::cell (int i) const
{
    const int rows = (options.size() + columns - 1) / columns;
    const float gap = 6.0f;
    const float w = ((float) getWidth() - gap * (float) (columns - 1)) / (float) columns;
    const float h = ((float) getHeight() - gap * (float) (rows - 1)) / (float) rows;
    return { (float) (i % columns) * (w + gap), (float) (i / columns) * (h + gap), w, h };
}

void KeyGroup::paint (juce::Graphics& g)
{
    for (int i = 0; i < options.size(); ++i)
    {
        auto r = cell (i);
        const bool down = pressed == i;
        drawKeyBody (g, r, down, false);
        auto body = r.translated (0.0f, down ? 1.0f : 0.0f);
        drawLed (g, { body.getX() + 11.0f, body.getCentreY() }, value == i);
        g.setColour (Theme::ink());
        drawTracked (g, options[i].toUpperCase(), body.withTrimmedLeft (18.0f), labelFont (10.0f), 0.8f, juce::Justification::centred);
    }
}

void KeyGroup::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < options.size(); ++i)
        if (cell (i).contains (e.position))
        {
            pressed = i;
            att->setValueAsCompleteGesture ((float) i);
            repaint();
            return;
        }
}

LedToggle::LedToggle (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& t) : text (t)
{
    att = std::make_unique<juce::ParameterAttachment> (*st.getParameter (id), [this] (float v) { on = v > 0.5f; repaint(); }, nullptr);
    att->sendInitialUpdate();
}

void LedToggle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawKeyBody (g, r, down, false);
    auto body = r.translated (0.0f, down ? 1.0f : 0.0f);
    drawLed (g, { body.getX() + 13.0f, body.getCentreY() }, on);
    g.setColour (Theme::ink());
    drawTracked (g, text, body.withTrimmedLeft (20.0f), labelFont (10.5f), 1.0f, juce::Justification::centred);
}

void LedToggle::mouseDown (const juce::MouseEvent&)
{
    down = true;
    att->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
    repaint();
}

void LedButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawKeyBody (g, r, down, false);
    auto body = r.translated (0.0f, down ? 1.0f : 0.0f);
    drawLed (g, { body.getX() + 13.0f, body.getCentreY() }, on);
    g.setColour (Theme::ink());
    drawTracked (g, text, body.withTrimmedLeft (20.0f), labelFont (10.5f), 1.0f, juce::Justification::centred);
}

} // namespace prisma
