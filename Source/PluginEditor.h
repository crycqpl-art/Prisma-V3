#pragma once

#include "Hardware.h"
#include "PluginProcessor.h"

namespace prisma
{

// The black screen: whole sample, pad regions you can drag, grains, pad strip.
class Display : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    Display (PrismaProcessor&, std::function<void (int)> onSelect);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    void tick();
    void setStatus (const juce::String& s) { status = s; statusFrames = 240; }
    void setSelected (int p) { selected = p; }

private:
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> sideArea() const;      // prism + rhythm waterfall
    juce::Rectangle<float> stripArea() const;
    juce::Rectangle<float> padCell (int i, int n) const;
    float xOf (float norm) const;
    float normOf (float x) const;
    float snap (float norm, bool allowSnap) const;
    void rebuildWave (const Sample&);
    void drawMask (juce::Graphics&, juce::Rectangle<float> clip, juce::Colour, float dx, float dy) const;
    void drawVfdText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, float size, juce::Colour) const;
    juce::Point<float> prismIn() const;
    juce::Point<float> prismOut() const;

    PrismaProcessor& proc;
    std::function<void (int)> select;
    int selected = 0;

    enum Drag { DragNone, DragMove, DragStart, DragEnd };
    Drag drag = DragNone;
    float dragAnchor = 0.0f, dragOrigStart = 0.0f, dragOrigEnd = 1.0f;

    std::array<float, maxPads> energy {};

    // a piece of the waveform lifted out of the screen when a grain fires
    struct Tile { float from, to, life, hue, pan; };
    std::vector<Tile> tiles;
    // a falling mark in the rhythm waterfall (lives as long as the grain sounds)
    struct Drop { float x, w, hue; int framesLeft; };
    std::vector<Drop> drops;
    float beam = 0.0f;   // brightness of the prism

    int readGrain = 0, readRestart = 0, frame = 0, statusFrames = 0;
    juce::String status;
    bool dragOver = false;
    juce::Image overlay, dots, waveMask, waterfall;
    const Sample* waveFor = nullptr;
    int waveW = 0;
    juce::Random rng;
};

} // namespace prisma

//==============================================================================
class PrismaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrismaEditor (PrismaProcessor&);
    ~PrismaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void animate() { timerCallback(); }
    void selectPad (int p);

private:
    void timerCallback() override;
    void refreshPadPanel();
    void loadWithChooser();

    PrismaProcessor& proc;
    prisma::HardwareLook look;
    juce::TooltipWindow tooltips { this, 600 };
    int selected = 0, lastSeenPad = -1;
    bool updatingPad = false;

    prisma::Display display;
    juce::ComboBox presetBox;
    juce::TextButton diceButton { "DICE" }, copyButton { "COPY" }, pasteButton { "PASTE" };
    juce::TextButton loadButton { "LOAD" }, clearButton { "CLEAR" }, resliceButton { "RE-SLICE" };
    prisma::KeyGroup divideKeys, typeKeys;
    juce::ComboBox gridBox, countBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> gridAtt, countAtt;
    prisma::ValueBar bpmBar, sensBar;
    prisma::LedToggle snapToggle;

    // grain
    prisma::Knob rate, hz, length, position, scan, pitch, chaos, space;
    prisma::LedToggle syncToggle;
    prisma::KeyGroup shapeKeys;
    // pad
    prisma::Knob padRate { "RATE" }, padLength { "LENGTH" };
    prisma::LedButton rateLink, lengthLink, reverseButton;
    juce::TextButton resetPadButton { "RESET PAD" }, resetAllButton { "RESET ALL PADS" };
    // right side
    prisma::Knob attack, decayTime, sustain, release, lowCut, highCut, reso, crunch, wash, pump, volume;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image texture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaEditor)
};
