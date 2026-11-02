#pragma once

// Shared "80s hardware" look: knobs, keys with LEDs, panels. Used by every plugin in the family.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace prisma
{
struct Theme
{
    static juce::Colour bodyTop()    { return juce::Colour (0xffefede8); }
    static juce::Colour bodyBottom() { return juce::Colour (0xffdcd9d1); }
    static juce::Colour panel()      { return juce::Colour (0xffe4e1da); }
    static juce::Colour ink()        { return juce::Colour (0xff3a3936); }
    static juce::Colour label()      { return juce::Colour (0xff6c6962); }
    static juce::Colour accent()     { return juce::Colour (0xfff0502a); }
    static juce::Colour screen()     { return juce::Colour (0xff060607); }
    static juce::Colour lcd()        { return juce::Colour (0xffc9cdd2); }
};

juce::Font labelFont (float size, bool bold = true);
juce::Font monoFont (float size);
void drawTracked (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&, float spacing, juce::Justification);
void drawKeyBody (juce::Graphics&, juce::Rectangle<float>, bool down, bool hover);
void drawLed (juce::Graphics&, juce::Point<float> centre, bool on);
void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);
void drawScrew (juce::Graphics&, juce::Point<float>);
void drawBezel (juce::Graphics&, juce::Rectangle<float> screen);
juce::Image makePaperTexture (int w, int h);
juce::Image makeScreenOverlay (int w, int h);

class HardwareLook : public juce::LookAndFeel_V4
{
public:
    HardwareLook();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// Rotary knob with a printed name above and the value below, bound to a parameter (or free when id is empty).
struct Knob : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name, bool big = false);
    explicit Knob (const juce::String& name);   // not bound to a parameter
    void resized() override;
    juce::Slider slider;
    juce::Label label;
    bool big = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// Horizontal "value bar" (BPM, sensitivity), bound to a parameter.
struct ValueBar : public juce::Component
{
    ValueBar (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name);
    void resized() override { slider.setBounds (getLocalBounds()); }
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// Row/grid of keys with LEDs bound to a choice parameter.
class KeyGroup : public juce::Component
{
public:
    KeyGroup (juce::AudioProcessorValueTreeState&, const juce::String& id, int columns, juce::StringArray labels = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { pressed = -1; repaint(); }

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray options;
    int columns, pressed = -1, value = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Key with an LED bound to a bool parameter.
class LedToggle : public juce::Component
{
public:
    LedToggle (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& text);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); }

private:
    juce::String text;
    bool on = false, down = false;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Key with an LED that is not a parameter.
class LedButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    LedButton (const juce::String& t, std::function<void()> onClick) : text (t), click (std::move (onClick)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); if (click) click(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); }
    void setOn (bool o) { if (o != on) { on = o; repaint(); } }
    bool isOn() const { return on; }

private:
    juce::String text;
    bool on = false, down = false;
    std::function<void()> click;
};

} // namespace prisma
