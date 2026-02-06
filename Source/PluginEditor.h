#pragma once

#include "PluginProcessor.h"

class SaturatorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SaturatorEditor(SaturatorProcessor&);
    ~SaturatorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SaturatorProcessor& processor;

    juce::Slider inputTrimSlider, driveSlider, biasSlider,
                 sagSlider, outputTrimSlider, mixSlider;

    juce::Label inputTrimLabel, driveLabel, biasLabel,
                sagLabel, outputTrimLabel, mixLabel;

    juce::ComboBox modeBox;
    juce::Label modeLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> inputTrimAttachment;
    std::unique_ptr<SliderAttachment> driveAttachment;
    std::unique_ptr<SliderAttachment> biasAttachment;
    std::unique_ptr<SliderAttachment> sagAttachment;
    std::unique_ptr<SliderAttachment> outputTrimAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturatorEditor)
};
