#include "PluginEditor.h"

SaturatorEditor::SaturatorEditor(SaturatorProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    auto& apvts = processor.getAPVTS();

    auto setupSlider = [&](juce::Slider& slider, juce::Label& label,
                           const juce::String& paramId, const juce::String& name,
                           std::unique_ptr<SliderAttachment>& attachment)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        addAndMakeVisible(slider);
        attachment = std::make_unique<SliderAttachment>(apvts, paramId, slider);
        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label);
    };

    setupSlider(inputTrimSlider, inputTrimLabel, "inputTrim", "Input", inputTrimAttachment);
    setupSlider(driveSlider, driveLabel, "drive", "Drive", driveAttachment);
    setupSlider(biasSlider, biasLabel, "bias", "Bias", biasAttachment);
    setupSlider(sagSlider, sagLabel, "sag", "Sag", sagAttachment);
    setupSlider(outputTrimSlider, outputTrimLabel, "outputTrim", "Output", outputTrimAttachment);
    setupSlider(mixSlider, mixLabel, "mix", "Mix", mixAttachment);

    modeBox.addItemList({"Triode", "Pentode", "Torture"}, 1);
    addAndMakeVisible(modeBox);
    modeAttachment = std::make_unique<ComboBoxAttachment>(apvts, "mode", modeBox);
    modeLabel.setText("Mode", juce::dontSendNotification);
    modeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(modeLabel);

    setSize(600, 350);
}

SaturatorEditor::~SaturatorEditor() {}

void SaturatorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));

    g.setColour(juce::Colour(0xffe94560));
    g.setFont(juce::Font(24.0f));
    g.drawText("SATURATOR", getLocalBounds().removeFromTop(40),
               juce::Justification::centred);
}

void SaturatorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    bounds.removeFromTop(40);

    auto knobArea = bounds.removeFromTop(240);
    int knobWidth = knobArea.getWidth() / 6;

    auto setupKnob = [](juce::Rectangle<int> area, juce::Slider& slider,
                         juce::Label& label)
    {
        label.setBounds(area.removeFromTop(20));
        slider.setBounds(area);
    };

    setupKnob(knobArea.removeFromLeft(knobWidth), inputTrimSlider, inputTrimLabel);
    setupKnob(knobArea.removeFromLeft(knobWidth), driveSlider, driveLabel);
    setupKnob(knobArea.removeFromLeft(knobWidth), biasSlider, biasLabel);
    setupKnob(knobArea.removeFromLeft(knobWidth), sagSlider, sagLabel);
    setupKnob(knobArea.removeFromLeft(knobWidth), outputTrimSlider, outputTrimLabel);
    setupKnob(knobArea, mixSlider, mixLabel);

    auto modeArea = bounds;
    modeLabel.setBounds(modeArea.removeFromLeft(50));
    modeBox.setBounds(modeArea.reduced(5));
}
