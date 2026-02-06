#include "SaturatorDSP.h"
#include <cmath>

//==============================================================================
// DC Blocker — one-pole HPF
//==============================================================================

void SaturatorDSP::DCBlocker::prepare(double sampleRate)
{
    const double fc = 5.0;
    coeff = static_cast<float>(1.0 - (2.0 * juce::MathConstants<double>::pi * fc / sampleRate));
    reset();
}

void SaturatorDSP::DCBlocker::reset()
{
    x1 = 0.0f;
    y1 = 0.0f;
}

float SaturatorDSP::DCBlocker::process(float x)
{
    // y[n] = x[n] - x[n-1] + R * y[n-1]
    float y = x - x1 + coeff * y1;
    x1 = x;
    y1 = y;
    return y;
}

//==============================================================================
// Envelope Follower for Sag
//==============================================================================

void SaturatorDSP::EnvelopeFollower::prepare(double sampleRate)
{
    attackCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.008)));
    releaseCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.200)));
    reset();
}

void SaturatorDSP::EnvelopeFollower::reset()
{
    envelope = 0.0f;
}

float SaturatorDSP::EnvelopeFollower::process(float input)
{
    float rectified = std::abs(input);
    float c = (rectified > envelope) ? attackCoeff : releaseCoeff;
    envelope += c * (rectified - envelope);
    return envelope;
}

//==============================================================================
// Valve Shaper
//==============================================================================

SaturatorDSP::ValveParams SaturatorDSP::getValveParams(Mode mode)
{
    switch (mode)
    {
        case Mode::Triode:  return { 2.5f, 0.5f };
        case Mode::Pentode: return { 4.0f, 0.85f };
        case Mode::Torture: return { 8.0f, 0.7f };
        default:            return { 4.0f, 0.5f };
    }
}

float SaturatorDSP::valveShaper(float x, float a, float b)
{
    float xp = x * (1.0f + b);
    float xn = x * (1.0f - b);

    return (x >= 0.0f)
        ? std::tanh(a * xp)
        : std::tanh(a * xn);
}

//==============================================================================
// EQ Configuration
//==============================================================================

void SaturatorDSP::updatePreEmphasis(double sampleRate, Mode mode)
{
    float hpfFreq = 60.0f;
    float midFreq = 1000.0f;
    float midGainDb = 6.0f;
    float midQ = 0.6f;
    float hfShelfFreq = 6000.0f;
    float hfShelfGainDb = 3.0f;

    switch (mode)
    {
        case Mode::Triode:
            midGainDb = 4.0f;
            hfShelfGainDb = 2.0f;
            break;
        case Mode::Pentode:
            midGainDb = 8.0f;
            hfShelfGainDb = 3.0f;
            break;
        case Mode::Torture:
            midGainDb = 6.0f;
            hfShelfGainDb = 4.0f;
            break;
    }

    *preHPF.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, hpfFreq, 0.5f);

    *preMidBoost.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, midFreq, midQ,
        juce::Decibels::decibelsToGain(midGainDb));

    *preHFShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, hfShelfFreq, 0.7f,
        juce::Decibels::decibelsToGain(hfShelfGainDb));
}

void SaturatorDSP::updatePostEmphasis(double sampleRate, Mode mode)
{
    float lpfFreq = 12000.0f;
    float lowShelfFreq = 120.0f;
    float lowShelfGainDb = 3.0f;
    float presenceDipFreq = 3000.0f;
    float presenceDipDb = -3.0f;

    switch (mode)
    {
        case Mode::Triode:
            lpfFreq = 14000.0f;
            presenceDipDb = -2.0f;
            lowShelfGainDb = 2.0f;
            break;
        case Mode::Pentode:
            lpfFreq = 11000.0f;
            presenceDipDb = -4.0f;
            lowShelfGainDb = 3.5f;
            break;
        case Mode::Torture:
            lpfFreq = 8000.0f;
            presenceDipDb = -6.0f;
            lowShelfGainDb = 4.0f;
            break;
    }

    *postLPF.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, lpfFreq, 0.7f);

    *postLowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sampleRate, lowShelfFreq, 0.7f,
        juce::Decibels::decibelsToGain(lowShelfGainDb));

    *postPresenceDip.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, presenceDipFreq, 1.0f,
        juce::Decibels::decibelsToGain(presenceDipDb));
}

//==============================================================================
// SaturatorDSP Main Implementation
//==============================================================================

SaturatorDSP::SaturatorDSP() {}

void SaturatorDSP::prepare(double sampleRate, int samplesPerBlock, int numChannels)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    currentNumChannels = numChannels;

    for (auto& dc : preDCBlocker)  dc.prepare(sampleRate);
    for (auto& dc : postDCBlocker) dc.prepare(sampleRate);

    for (auto& env : sagEnvelope) env.prepare(sampleRate);

    oversampling4x = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(numChannels), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);

    oversampling8x = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(numChannels), 3,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);

    oversampling4x->initProcessing(static_cast<size_t>(samplesPerBlock));
    oversampling8x->initProcessing(static_cast<size_t>(samplesPerBlock));

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(numChannels);

    preHPF.prepare(spec);
    preMidBoost.prepare(spec);
    preHFShelf.prepare(spec);

    postLPF.prepare(spec);
    postLowShelf.prepare(spec);
    postPresenceDip.prepare(spec);

    updatePreEmphasis(sampleRate, Mode::Triode);
    updatePostEmphasis(sampleRate, Mode::Triode);

    dryBuffer.setSize(numChannels, samplesPerBlock);
}

void SaturatorDSP::reset()
{
    for (auto& dc : preDCBlocker)  dc.reset();
    for (auto& dc : postDCBlocker) dc.reset();
    for (auto& env : sagEnvelope)  env.reset();

    preHPF.reset();
    preMidBoost.reset();
    preHFShelf.reset();
    postLPF.reset();
    postLowShelf.reset();
    postPresenceDip.reset();

    if (oversampling4x) oversampling4x->reset();
    if (oversampling8x) oversampling8x->reset();
}

float SaturatorDSP::getLatencyInSamples(Mode mode) const
{
    if (mode == Mode::Torture && oversampling8x)
        return oversampling8x->getLatencyInSamples();
    if (oversampling4x)
        return oversampling4x->getLatencyInSamples();
    return 0.0f;
}

void SaturatorDSP::process(juce::AudioBuffer<float>& buffer,
                            float inputTrimDb,
                            float driveDb,
                            float bias,
                            float sagAmount,
                            float outputTrimDb,
                            float mix,
                            Mode mode)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // --- Save dry signal for mix blending ---
    dryBuffer.makeCopyOf(buffer, true);

    // --- 1. Input Trim ---
    float inputGain = juce::Decibels::decibelsToGain(inputTrimDb);
    buffer.applyGain(inputGain);

    // --- 2. DC Blocker (pre) ---
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = preDCBlocker[static_cast<size_t>(ch)].process(data[i]);
    }

    // --- 3. Pre-Emphasis EQ ---
    updatePreEmphasis(currentSampleRate, mode);
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        preHPF.process(ctx);
        preMidBoost.process(ctx);
        preHFShelf.process(ctx);
    }

    // --- 4. Oversampling (up) ---
    bool useTorture = (mode == Mode::Torture);
    auto& oversampler = useTorture ? *oversampling8x : *oversampling4x;

    juce::dsp::AudioBlock<float> inputBlock(buffer);
    auto oversampledBlock = oversampler.processSamplesUp(inputBlock);

    const int osNumSamples = static_cast<int>(oversampledBlock.getNumSamples());
    const int osNumChannels = static_cast<int>(oversampledBlock.getNumChannels());

    // --- 5 + 6 + 7. Drive, Valve Shaper, and Sag (at oversampled rate) ---
    float driveLinear = std::pow(10.0f, driveDb / 20.0f);
    auto valveParams = getValveParams(mode);

    double osRate = currentSampleRate * (useTorture ? 8.0 : 4.0);
    for (auto& env : sagEnvelope) env.prepare(osRate);

    for (int ch = 0; ch < osNumChannels; ++ch)
    {
        auto* data = oversampledBlock.getChannelPointer(static_cast<size_t>(ch));

        for (int i = 0; i < osNumSamples; ++i)
        {
            float env = sagEnvelope[static_cast<size_t>(ch)].process(data[i]);

            float effectiveDrive = driveLinear * (1.0f - sagAmount * env);

            float x = data[i] + bias;
            x *= effectiveDrive;

            data[i] = valveShaper(x, valveParams.curvature, valveParams.asymmetry);
        }
    }

    // --- 8. Downsample ---
    juce::dsp::AudioBlock<float> outputBlock(buffer);
    oversampler.processSamplesDown(outputBlock);

    // --- 9. Post-Emphasis EQ ---
    updatePostEmphasis(currentSampleRate, mode);
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        postLPF.process(ctx);
        postLowShelf.process(ctx);
        postPresenceDip.process(ctx);
    }

    // --- 10. DC Blocker (post) ---
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = postDCBlocker[static_cast<size_t>(ch)].process(data[i]);
    }

    // --- 11. Output Trim ---
    float outputGain = juce::Decibels::decibelsToGain(outputTrimDb);
    buffer.applyGain(outputGain);

    // --- 12. Dry/Wet Mix ---
    if (mix < 1.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wetData = buffer.getWritePointer(ch);
            const auto* dryData = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wetData[i] = dryData[i] * (1.0f - mix) + wetData[i] * mix;
        }
    }
}
