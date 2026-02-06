#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "SaturatorDSP.h"

class SaturatorProcessor : public juce::AudioProcessor
{
public:
    SaturatorProcessor();
    ~SaturatorProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SaturatorDSP dsp;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothInputTrim;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDrive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothBias;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothSag;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothOutputTrim;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothMix;

    SaturatorDSP::Mode lastMode = SaturatorDSP::Mode::Triode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturatorProcessor)
};
