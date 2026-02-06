#include "PluginProcessor.h"
#include "PluginEditor.h"

SaturatorProcessor::SaturatorProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

SaturatorProcessor::~SaturatorProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout SaturatorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"inputTrim", 1}, "Input Trim",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drive", 1}, "Drive",
        juce::NormalisableRange<float>(0.0f, 60.0f, 0.1f, 0.4f),
        20.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"bias", 1}, "Bias",
        juce::NormalisableRange<float>(-0.6f, 0.6f, 0.01f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sag", 1}, "Sag",
        juce::NormalisableRange<float>(0.0f, 0.6f, 0.01f),
        0.15f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputTrim", 1}, "Output Trim",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mode", 1}, "Mode",
        juce::StringArray{"Triode", "Pentode", "Torture"},
        0));

    return { params.begin(), params.end() };
}

const juce::String SaturatorProcessor::getName() const { return JucePlugin_Name; }
bool SaturatorProcessor::acceptsMidi() const { return false; }
bool SaturatorProcessor::producesMidi() const { return false; }
bool SaturatorProcessor::isMidiEffect() const { return false; }
double SaturatorProcessor::getTailLengthSeconds() const { return 0.0; }
int SaturatorProcessor::getNumPrograms() { return 1; }
int SaturatorProcessor::getCurrentProgram() { return 0; }
void SaturatorProcessor::setCurrentProgram(int) {}
const juce::String SaturatorProcessor::getProgramName(int) { return {}; }
void SaturatorProcessor::changeProgramName(int, const juce::String&) {}

void SaturatorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dsp.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());

    double rampTimeSecs = 0.05;
    smoothInputTrim.reset(sampleRate, rampTimeSecs);
    smoothDrive.reset(sampleRate, rampTimeSecs);
    smoothBias.reset(sampleRate, rampTimeSecs);
    smoothSag.reset(sampleRate, rampTimeSecs);
    smoothOutputTrim.reset(sampleRate, rampTimeSecs);
    smoothMix.reset(sampleRate, rampTimeSecs);

    setLatencySamples(static_cast<int>(
        std::ceil(dsp.getLatencyInSamples(SaturatorDSP::Mode::Triode))));
}

void SaturatorProcessor::releaseResources()
{
    dsp.reset();
}

bool SaturatorProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void SaturatorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    float inputTrimDb  = apvts.getRawParameterValue("inputTrim")->load();
    float driveDb      = apvts.getRawParameterValue("drive")->load();
    float bias         = apvts.getRawParameterValue("bias")->load();
    float sag          = apvts.getRawParameterValue("sag")->load();
    float outputTrimDb = apvts.getRawParameterValue("outputTrim")->load();
    float mix          = apvts.getRawParameterValue("mix")->load() / 100.0f;
    int modeIndex      = static_cast<int>(apvts.getRawParameterValue("mode")->load());
    auto mode          = static_cast<SaturatorDSP::Mode>(modeIndex);

    smoothInputTrim.setTargetValue(inputTrimDb);
    smoothDrive.setTargetValue(driveDb);
    smoothBias.setTargetValue(bias);
    smoothSag.setTargetValue(sag);
    smoothOutputTrim.setTargetValue(outputTrimDb);
    smoothMix.setTargetValue(mix);

    int numSamples = buffer.getNumSamples();
    smoothInputTrim.skip(numSamples);
    smoothDrive.skip(numSamples);
    smoothBias.skip(numSamples);
    smoothSag.skip(numSamples);
    smoothOutputTrim.skip(numSamples);
    smoothMix.skip(numSamples);

    if (mode != lastMode)
    {
        lastMode = mode;
        setLatencySamples(static_cast<int>(
            std::ceil(dsp.getLatencyInSamples(mode))));
    }

    dsp.process(buffer,
                smoothInputTrim.getCurrentValue(),
                smoothDrive.getCurrentValue(),
                smoothBias.getCurrentValue(),
                smoothSag.getCurrentValue(),
                smoothOutputTrim.getCurrentValue(),
                smoothMix.getCurrentValue(),
                mode);
}

bool SaturatorProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SaturatorProcessor::createEditor()
{
    return new SaturatorEditor(*this);
}

void SaturatorProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SaturatorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SaturatorProcessor();
}
