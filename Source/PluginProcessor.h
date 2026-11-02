#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

class PrismaProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    PrismaProcessor();
    ~PrismaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Prisma"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- sample + slicing (message thread)
    juce::String loadFile (const juce::File& file);
    void clearSample();
    void reslice();
    prisma::SamplePtr getSample() const { return std::atomic_load (&sample); }
    double sliceBpm() const;                 // tempo the grid uses
    double gridBeats() const;                // one grid step in beats
    std::vector<float> snapLines() const;    // normalised grid positions
    bool manualEdits = false;
    double bpmUsed = 120.0;
    juce::String missingFile;

    // ---- pads (message thread)
    int padCount() const { return pads.count.load(); }
    void setPadRegion (int pad, float start, float end);
    void setPadRate (int pad, int rateIndexOrMinusOne);
    void setPadLength (int pad, float lengthOrMinusOne);
    void setPadReverse (int pad, bool r);
    void setPadGain (int pad, float g);
    void resetPad (int pad);
    void resetAllPads();

    // ---- misc
    void setParam (const juce::String& id, float realValue);
    float getParam (const juce::String& id) const;
    void dice();
    void applyPreset (int index);
    juce::String settingsToText() const;
    int settingsFromText (const juce::String& text);
    void releaseRetired();

    juce::AudioProcessorValueTreeState apvts;
    prisma::PadState pads;
    prisma::Activity activity;
    std::atomic<float> hostBpm { 120.0f };

private:
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { reslice(); }
    prisma::Params readParams() const;
    void setSample (prisma::SamplePtr s);
    void storePads();
    juce::String padsToString() const;
    void padsFromString (const juce::String&);

    prisma::SamplePtr sample;
    std::vector<prisma::SamplePtr> retired;
    juce::CriticalSection retiredLock;

    prisma::GrainEngine engine;
    prisma::FX fx;
    double sr = 44100.0, internalPpq = 0.0;
    juce::Random uiRandom;
    bool restoring = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaProcessor)
};
