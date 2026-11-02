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
    juce::Rectangle<float> stripArea() const;
    juce::Rectangle<float> padCell (int i, int n) const;
    float xOf (float norm) const;
    float normOf (float x) const;
    float snap (float norm, bool allowSnap) const;
    void drawWave (juce::Graphics&, const Sample&, juce::Rectangle<float> area, float from, float to,
                   juce::Colour, float xOffset) const;

    PrismaProcessor& proc;
    std::function<void (int)> select;
    int selected = 0;

    enum Drag { DragNone, DragMove, DragStart, DragEnd };
    Drag drag = DragNone;
    float dragAnchor = 0.0f, dragOrigStart = 0.0f, dragOrigEnd = 1.0f;

    std::array<float, maxPads> energy {};
    std::array<juce::Colour, maxPads> colA, colB;
    struct Particle { float x, y, life, size; juce::Colour c; };
    std::vector<Particle> particles;
    int readGrain = 0, readRestart = 0, frame = 0, statusFrames = 0;
    juce::String status;
    bool dragOver = false;
    juce::Image overlay;
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
    prisma::Knob rate, length, grainSize, density, scan, spread, chaos, pitch;
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
