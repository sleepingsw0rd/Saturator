---
date: 2026-02-06T00:00:00Z
planner: Claude
git_commit: (no commits yet)
branch: master
repository: Saturator
plan_type: "implementation"
topic: "Culture Vulture-Style DSP Engine"
tags: [implementation, planning, dsp, saturation, valve-emulation, oversampling]
status: complete
last_updated: 2026-02-06
last_updated_by: Claude
---
# Culture Vulture-Style DSP Engine Implementation Plan

**Date**: 2026-02-06
**Planner**: Claude
**Git Commit**: (no commits yet)
**Branch**: master
**Repository**: Saturator

## Overview

Replace the existing simple saturator DSP with a full Culture Vulture-style valve saturation engine. The signal chain includes input trim, DC blocking, pre-emphasis EQ, 4x/8x oversampling, bias + drive, asymmetric valve waveshaping, dynamic sag/compression, post-emphasis EQ, downsampling, output DC blocking, output trim, and dry/wet mixing. Three modes (Triode, Pentode, Torture) configure the internal behavior to match the Culture Vulture's character.

## Current State Analysis

The project has 4 source files built with JUCE 7.0.12 via CMake FetchContent:

- `Source/PluginProcessor.h` — Declares `SaturatorProcessor` with a simple `saturate()` function and 4 APVTS parameters
- `Source/PluginProcessor.cpp` — Per-sample processing with SoftClip/HardClip/TanH modes, no oversampling, no DC blocking, no EQ, no parameter smoothing, empty `prepareToPlay()`
- `Source/PluginEditor.h` — GUI with 3 rotary knobs (Drive, Mix, Output) + mode ComboBox
- `Source/PluginEditor.cpp` — 400x300 window, dark navy theme
- `CMakeLists.txt` — Links `juce_audio_utils` and `juce_dsp` (dsp is included but unused)

### Key Discoveries:

- `juce_dsp` is already linked at `CMakeLists.txt:46`, giving us access to `juce::dsp::Oversampling`, `juce::dsp::IIR::Filter`, `juce::dsp::IIR::Coefficients`, and `juce::dsp::ProcessorDuplicator`
- No DSP state is allocated — `prepareToPlay()` is empty at `PluginProcessor.cpp:52`
- Parameters lack smoothing — read once per block at `PluginProcessor.cpp:106-110`
- The existing `saturate()` function at `PluginProcessor.cpp:68-94` and the entire `processBlock()` at `PluginProcessor.cpp:96-123` will be replaced

## Desired End State

A fully functional Culture Vulture-style saturator plugin with:

1. **Complete signal chain**: Input Trim → DC Block → Pre-EQ → Oversample Up → Bias+Drive → Valve Shaper → Sag → Post-EQ → Downsample → DC Block → Output Trim → Dry/Wet
2. **Three modes**: Triode (soft, warm), Pentode (aggressive, odd harmonics), Torture (extreme)
3. **Seven user-facing parameters**: Input Trim, Drive, Bias (Triode↔Pentode), Sag, Output Trim, Mix, Mode
4. **Oversampling**: 4x default, 8x in Torture mode (automatic, not user-selectable)
5. **Internal fixed EQ curves**: Pre-emphasis (mid push) and post-emphasis (HF taming) — not user-exposed
6. **Smooth parameter changes**: No zipper noise via `juce::SmoothedValue`
7. **Correct latency reporting**: From oversampling

### Verification:

- Plugin builds as VST3 and Standalone without errors
- Audio passes through the chain with audible saturation character
- Mode switching changes saturation character appropriately
- No DC offset at output (verified with DC meter)
- No aliasing artifacts at moderate drive levels
- Parameters automate smoothly without clicks
- State save/restore works in DAW

## What We're NOT Doing

- **Custom GUI look-and-feel** — We keep the existing visual style, just expand it for new controls
- **User-adjustable EQ** — Pre/post emphasis curves are internal and fixed per mode
- **Auto-gain compensation** — Only manual Output Trim (auto-gain adds complexity and pumping risk)
- **User-selectable oversampling rate** — Automatic: 4x for Triode/Pentode, 8x for Torture
- **Separate left/right processing** — Same processing on both channels (standard for saturation)
- **Preset system** — Modes configure internal parameters; no user preset browser
- **Unit tests** — No test framework is set up; verification is manual + build checks

## Implementation Approach

Create a standalone `SaturatorDSP` class that encapsulates the entire signal chain. This keeps the processor clean (just wiring parameters to DSP) and makes the DSP testable in isolation. The class owns all filters, oversampling, and state. The processor owns the APVTS and feeds parameter values into the DSP each block.

We need two `Oversampling` instances (4x and 8x) because JUCE's `Oversampling` class doesn't support changing the factor at runtime without reallocation. Both are initialized in `prepareToPlay`; the active one is selected per-block based on mode.

---

## Phase 1: Create the DSP Engine (`SaturatorDSP` class)

### Overview

Create `Source/SaturatorDSP.h` and `Source/SaturatorDSP.cpp` containing the complete signal chain. This is the largest phase and contains all the audio processing logic.

### Changes Required:

#### 1. New File: `Source/SaturatorDSP.h`

**File**: `Source/SaturatorDSP.h`
**Changes**: Create the DSP engine class declaration

```cpp
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>

class SaturatorDSP
{
public:
    // Mode enum matching the Culture Vulture behavior
    enum class Mode { Triode, Pentode, Torture };

    SaturatorDSP();

    // Call from prepareToPlay
    void prepare(double sampleRate, int samplesPerBlock, int numChannels);

    // Call from releaseResources
    void reset();

    // Process a full audio buffer in-place
    void process(juce::AudioBuffer<float>& buffer,
                 float inputTrimDb,
                 float driveDb,
                 float bias,
                 float sagAmount,
                 float outputTrimDb,
                 float mix,
                 Mode mode);

    // Returns latency in samples for the current mode's oversampling
    float getLatencyInSamples(Mode mode) const;

private:
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentNumChannels = 2;

    // --- DC Blockers (one-pole HPF at ~5 Hz) ---
    // Per-channel state for pre and post DC blockers
    struct DCBlocker
    {
        float x1 = 0.0f; // previous input
        float y1 = 0.0f; // previous output
        float coeff = 0.0f; // filter coefficient R

        void prepare(double sampleRate);
        void reset();
        float process(float x);
    };
    std::array<DCBlocker, 2> preDCBlocker;   // one per channel
    std::array<DCBlocker, 2> postDCBlocker;

    // --- Pre-Emphasis EQ ---
    // HPF ~50 Hz, peak ~1 kHz, HF shelf ~7 kHz
    using IIRFilter = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    IIRFilter preHPF;       // gentle high-pass ~50 Hz
    IIRFilter preMidBoost;  // broad peak +3 dB @ 1 kHz
    IIRFilter preHFShelf;   // gentle HF shelf +1.5 dB @ 7 kHz

    // --- Post-Emphasis EQ ---
    // LPF ~14 kHz, low shelf ~100 Hz, presence dip ~3 kHz
    IIRFilter postLPF;        // gentle low-pass ~14 kHz
    IIRFilter postLowShelf;   // low shelf +1.5 dB @ 100 Hz
    IIRFilter postPresenceDip; // dip -2 dB @ 3 kHz

    // --- Oversampling ---
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling4x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling8x;

    // --- Sag Envelope Follower ---
    struct EnvelopeFollower
    {
        float envelope = 0.0f;
        float attackCoeff = 0.0f;
        float releaseCoeff = 0.0f;

        void prepare(double sampleRate);
        void reset();
        float process(float input); // returns envelope value 0..1
    };
    std::array<EnvelopeFollower, 2> sagEnvelope; // one per channel

    // --- Valve Waveshaper ---
    // Mode-dependent curvature and asymmetry
    struct ValveParams
    {
        float curvature;   // 'a' in the shaper
        float asymmetry;   // 'b' in the shaper
    };
    static ValveParams getValveParams(Mode mode);
    static float valveShaper(float x, float a, float b);

    // --- Internal helpers ---
    void updatePreEmphasis(double sampleRate, Mode mode);
    void updatePostEmphasis(double sampleRate, Mode mode);

    // Dry buffer for mix blending
    juce::AudioBuffer<float> dryBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturatorDSP)
};
```

#### 2. New File: `Source/SaturatorDSP.cpp`

**File**: `Source/SaturatorDSP.cpp`
**Changes**: Implement the full DSP signal chain

```cpp
#include "SaturatorDSP.h"
#include <cmath>

//==============================================================================
// DC Blocker — one-pole HPF
//==============================================================================

void SaturatorDSP::DCBlocker::prepare(double sampleRate)
{
    // R = 1 - (2*pi*fc / sampleRate)
    // fc = 5 Hz gives very gentle DC removal
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
    // Attack: ~8 ms
    attackCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.008)));
    // Release: ~200 ms
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
        case Mode::Triode:  return { 1.2f, 0.3f };  // softer curve, mild asymmetry
        case Mode::Pentode: return { 1.5f, 0.6f };  // moderate curve, strong asymmetry
        case Mode::Torture: return { 2.5f, 0.5f };  // hard curve, moderate asymmetry
        default:            return { 1.5f, 0.5f };
    }
}

float SaturatorDSP::valveShaper(float x, float a, float b)
{
    // Asymmetric soft clip with smooth knee
    // b controls asymmetry: 0 = symmetric, >0 = boosts positive half
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
    // Pre-emphasis: shape the signal before distortion
    // This is critical for the CV vibe — push mids, not lows

    float hpfFreq = 50.0f;
    float midFreq = 1000.0f;
    float midGainDb = 3.0f;
    float midQ = 0.7f;    // broad
    float hfShelfFreq = 7000.0f;
    float hfShelfGainDb = 1.5f;

    // Mode-specific adjustments
    switch (mode)
    {
        case Mode::Triode:
            midGainDb = 2.0f;   // less mid emphasis
            hfShelfGainDb = 1.0f;
            break;
        case Mode::Pentode:
            midGainDb = 4.0f;   // more mid emphasis
            hfShelfGainDb = 1.5f;
            break;
        case Mode::Torture:
            midGainDb = 3.5f;
            hfShelfGainDb = 2.0f;
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
    // Post-emphasis: tame HF junk from distortion, warm up the bottom

    float lpfFreq = 14000.0f;
    float lowShelfFreq = 100.0f;
    float lowShelfGainDb = 1.5f;
    float presenceDipFreq = 3000.0f;
    float presenceDipDb = -2.0f;

    // Mode-specific adjustments
    switch (mode)
    {
        case Mode::Triode:
            lpfFreq = 16000.0f;     // more open top end
            presenceDipDb = -1.0f;   // less dip
            break;
        case Mode::Pentode:
            lpfFreq = 14000.0f;
            presenceDipDb = -2.5f;   // more presence taming
            break;
        case Mode::Torture:
            lpfFreq = 11000.0f;     // aggressive HF rolloff
            presenceDipDb = -3.0f;
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

    // DC Blockers
    for (auto& dc : preDCBlocker)  dc.prepare(sampleRate);
    for (auto& dc : postDCBlocker) dc.prepare(sampleRate);

    // Envelope followers (operate at oversampled rate, but prepare at base rate
    // — coefficients will be recalculated in process() based on actual OS rate)
    for (auto& env : sagEnvelope) env.prepare(sampleRate);

    // Oversampling: 4x (factor=2, since 2^2=4) and 8x (factor=3, since 2^3=8)
    oversampling4x = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(numChannels), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);

    oversampling8x = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(numChannels), 3,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);

    oversampling4x->initProcessing(static_cast<size_t>(samplesPerBlock));
    oversampling8x->initProcessing(static_cast<size_t>(samplesPerBlock));

    // Pre-emphasis EQ (operates at base sample rate, before oversampling)
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(numChannels);

    preHPF.prepare(spec);
    preMidBoost.prepare(spec);
    preHFShelf.prepare(spec);

    // Post-emphasis EQ (operates at base sample rate, after downsampling)
    postLPF.prepare(spec);
    postLowShelf.prepare(spec);
    postPresenceDip.prepare(spec);

    // Initialize EQ coefficients for default mode
    updatePreEmphasis(sampleRate, Mode::Triode);
    updatePostEmphasis(sampleRate, Mode::Triode);

    // Dry buffer for mix blending
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

    // Prepare envelope followers at oversampled rate
    double osRate = currentSampleRate * (useTorture ? 8.0 : 4.0);
    for (auto& env : sagEnvelope) env.prepare(osRate);

    for (int ch = 0; ch < osNumChannels; ++ch)
    {
        auto* data = oversampledBlock.getChannelPointer(static_cast<size_t>(ch));

        for (int i = 0; i < osNumSamples; ++i)
        {
            // Envelope for sag
            float env = sagEnvelope[static_cast<size_t>(ch)].process(data[i]);

            // Dynamic sag: reduce drive under sustained load
            float effectiveDrive = driveLinear * (1.0f - sagAmount * env);

            // Bias + drive
            float x = data[i] + bias;
            x *= effectiveDrive;

            // Valve waveshaper
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
```

### Design Decisions:

1. **Two Oversampling instances** — JUCE `Oversampling` cannot change factor at runtime. We allocate both 4x and 8x in `prepare()` and select per-block. Memory cost is modest (internal buffers for max block size).

2. **DC Blocker as manual one-pole** — JUCE doesn't provide a dedicated DC blocker. The `y = x - x[n-1] + R * y[n-1]` topology is standard and lightweight. R ≈ 0.999 at 44.1 kHz gives ~5 Hz cutoff.

3. **Envelope follower at oversampled rate** — The sag envelope must track the actual signal level inside the oversampled domain to respond correctly to the driven signal dynamics. Re-preparing coefficients per block is cheap (two `exp()` calls).

4. **EQ updates per block** — Calling `updatePreEmphasis`/`updatePostEmphasis` every block is slightly wasteful when mode doesn't change, but the coefficient calculation is just a few multiplies and the code stays simple. A future optimization could cache the current mode and skip recalculation.

5. **Pre-emphasis before oversampling** — The EQ shapes what frequencies hit the distortion stage. Operating at base rate is fine because we're shaping before the nonlinearity. Post-emphasis also operates at base rate since we want to tame the downsampled result.

6. **Bias applied inside the oversampled domain** — Bias must be added before the waveshaper to generate the desired harmonic asymmetry. It's inside the oversampled loop so the DC it introduces gets properly handled by the oversampled processing and the post DC blocker.

### Success Criteria:

#### Automated Verification:

- [x] `Source/SaturatorDSP.h` exists and compiles (verified by full build)
- [x] `Source/SaturatorDSP.cpp` exists and compiles
- [x] CMake configures: `cmake -B build -S .`
- [x] Build succeeds: `cmake --build build --config Release`

#### Manual Verification:

- [x] Code review: signal chain matches spec ordering
- [x] All 12 signal chain stages are implemented
- [x] Mode-dependent behavior for Triode, Pentode, Torture
- [x] Valve shaper matches the spec's `valveShaper()` function signature

---

## Phase 2: Update the Parameter System

### Overview

Replace the existing 4-parameter APVTS layout with the new 7-parameter layout. Add parameter smoothing to prevent zipper noise.

### Changes Required:

#### 1. PluginProcessor.h — New parameter IDs and DSP member

**File**: `Source/PluginProcessor.h`
**Changes**: Remove old `saturate()` and `SaturationMode`, add `SaturatorDSP` member, add smoothed parameter members

```cpp
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

    // Smoothed parameters
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothInputTrim;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDrive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothBias;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothSag;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothOutputTrim;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothMix;

    SaturatorDSP::Mode lastMode = SaturatorDSP::Mode::Triode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturatorProcessor)
};
```

#### 2. PluginProcessor.cpp — New parameter layout and processBlock

**File**: `Source/PluginProcessor.cpp`
**Changes**: Replace `createParameterLayout()` with new parameters, rewrite `processBlock()` and `prepareToPlay()`

New `createParameterLayout()`:

```cpp
juce::AudioProcessorValueTreeState::ParameterLayout SaturatorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Input Trim: -24 dB to +24 dB, default 0 dB
    // Calibrated so -18 dBFS input ≈ clean at 0 dB trim
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"inputTrim", 1}, "Input Trim",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f));

    // Drive: 0 dB to 48 dB, default 12 dB
    // Exponential feel via skew factor
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drive", 1}, "Drive",
        juce::NormalisableRange<float>(0.0f, 48.0f, 0.1f, 0.5f),
        12.0f));

    // Bias: -0.3 to +0.3, default 0 (triode-ish center)
    // Negative = more triode, Positive = more pentode character
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"bias", 1}, "Bias",
        juce::NormalisableRange<float>(-0.3f, 0.3f, 0.01f),
        0.0f));

    // Sag: 0% to 30%, default 10%
    // Controls dynamic compression / valve sag amount
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sag", 1}, "Sag",
        juce::NormalisableRange<float>(0.0f, 0.3f, 0.01f),
        0.1f));

    // Output Trim: -24 dB to +24 dB, default 0 dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputTrim", 1}, "Output Trim",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f));

    // Mix: 0% to 100%, default 100%
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f));

    // Mode: Triode / Pentode / Torture
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mode", 1}, "Mode",
        juce::StringArray{"Triode", "Pentode", "Torture"},
        0));

    return { params.begin(), params.end() };
}
```

New `prepareToPlay()`:

```cpp
void SaturatorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dsp.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());

    // Initialize smoothed values (50ms ramp time)
    double rampTimeSecs = 0.05;
    smoothInputTrim.reset(sampleRate, rampTimeSecs);
    smoothDrive.reset(sampleRate, rampTimeSecs);
    smoothBias.reset(sampleRate, rampTimeSecs);
    smoothSag.reset(sampleRate, rampTimeSecs);
    smoothOutputTrim.reset(sampleRate, rampTimeSecs);
    smoothMix.reset(sampleRate, rampTimeSecs);

    // Report latency for default mode
    setLatencySamples(static_cast<int>(
        std::ceil(dsp.getLatencyInSamples(SaturatorDSP::Mode::Triode))));
}
```

New `processBlock()`:

```cpp
void SaturatorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Read parameters
    float inputTrimDb = apvts.getRawParameterValue("inputTrim")->load();
    float driveDd     = apvts.getRawParameterValue("drive")->load();
    float bias        = apvts.getRawParameterValue("bias")->load();
    float sag         = apvts.getRawParameterValue("sag")->load();
    float outputTrimDb= apvts.getRawParameterValue("outputTrim")->load();
    float mix         = apvts.getRawParameterValue("mix")->load() / 100.0f;
    int modeIndex     = static_cast<int>(apvts.getRawParameterValue("mode")->load());
    auto mode         = static_cast<SaturatorDSP::Mode>(modeIndex);

    // Update smoothed values
    smoothInputTrim.setTargetValue(inputTrimDb);
    smoothDrive.setTargetValue(driveDd);
    smoothBias.setTargetValue(bias);
    smoothSag.setTargetValue(sag);
    smoothOutputTrim.setTargetValue(outputTrimDb);
    smoothMix.setTargetValue(mix);

    // Update latency if mode changed
    if (mode != lastMode)
    {
        lastMode = mode;
        setLatencySamples(static_cast<int>(
            std::ceil(dsp.getLatencyInSamples(mode))));
    }

    // Process through the DSP chain
    // Using the current smoothed values (block-level smoothing)
    dsp.process(buffer,
                smoothInputTrim.getNextValue(),
                smoothDrive.getNextValue(),
                smoothBias.getNextValue(),
                smoothSag.getNextValue(),
                smoothOutputTrim.getNextValue(),
                smoothMix.getNextValue(),
                mode);
}
```

Also update `releaseResources()`:

```cpp
void SaturatorProcessor::releaseResources()
{
    dsp.reset();
}
```

### Parameter Summary Table:

| ID | Name | Range | Default | Unit | Notes |
|----|------|-------|---------|------|-------|
| `inputTrim` | Input Trim | -24 to +24 | 0 | dB | -18 dBFS ≈ clean |
| `drive` | Drive | 0 to 48 | 12 | dB | Skew 0.5 for fine control at low end |
| `bias` | Bias | -0.3 to +0.3 | 0 | — | 0 = triode center |
| `sag` | Sag | 0 to 0.3 | 0.1 | — | 0.1 = 10% sag |
| `outputTrim` | Output Trim | -24 to +24 | 0 | dB | |
| `mix` | Mix | 0 to 100 | 100 | % | Normalized to 0-1 internally |
| `mode` | Mode | Triode/Pentode/Torture | 0 | choice | Controls internal curve + EQ + OS |

### Success Criteria:

#### Automated Verification:

- [x] CMake configures: `cmake -B build -S .`
- [x] Build succeeds: `cmake --build build --config Release`
- [x] No compiler warnings related to parameter types

#### Manual Verification:

- [ ] All 7 parameters appear in DAW automation
- [ ] Parameter ranges match the table above
- [ ] State save/restore works (close and reopen plugin, values persist)

---

## Phase 3: Update CMakeLists.txt

### Overview

Add the new `SaturatorDSP.cpp` source file to the build.

### Changes Required:

#### 1. CMakeLists.txt — Add new source file

**File**: `CMakeLists.txt`
**Changes**: Add `Source/SaturatorDSP.cpp` to `target_sources`

```cmake
target_sources(Saturator PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SaturatorDSP.cpp
)
```

### Success Criteria:

#### Automated Verification:

- [x] CMake configures without errors: `cmake -B build -S .`
- [x] Build succeeds without errors: `cmake --build build --config Release`

---

## Phase 4: Update the Editor (GUI)

### Overview

Expand the GUI to expose the 7 new parameters. Layout: title row, then a row of 6 rotary knobs (Input Trim, Drive, Bias, Sag, Output Trim, Mix), then the mode selector at the bottom.

### Changes Required:

#### 1. PluginEditor.h — New components and attachments

**File**: `Source/PluginEditor.h`
**Changes**: Replace existing component declarations

```cpp
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

    // Knobs
    juce::Slider inputTrimSlider, driveSlider, biasSlider,
                 sagSlider, outputTrimSlider, mixSlider;

    // Labels
    juce::Label inputTrimLabel, driveLabel, biasLabel,
                sagLabel, outputTrimLabel, mixLabel;

    // Mode selector
    juce::ComboBox modeBox;
    juce::Label modeLabel;

    // APVTS attachments
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
```

#### 2. PluginEditor.cpp — Wire up all controls

**File**: `Source/PluginEditor.cpp`
**Changes**: Create and layout all 6 knobs + mode selector

```cpp
#include "PluginEditor.h"

SaturatorEditor::SaturatorEditor(SaturatorProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    auto& apvts = processor.getAPVTS();

    // Helper lambda to set up a rotary knob
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

    // Mode selector
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
    bounds.removeFromTop(40); // title space

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
```

Window size increases from 400x300 to **600x350** to accommodate 6 knobs.

### Success Criteria:

#### Automated Verification:

- [x] CMake configures: `cmake -B build -S .`
- [x] Build succeeds: `cmake --build build --config Release`
- [x] VST3 artifact exists: `build/Saturator_artefacts/Release/VST3/Saturator.vst3`
- [x] Standalone artifact exists: `build/Saturator_artefacts/Release/Standalone/Saturator.exe`

#### Manual Verification:

- [ ] Plugin loads in Standalone — 6 rotary knobs visible
- [ ] Input Trim knob adjusts level before saturation
- [ ] Drive knob increases distortion intensity
- [ ] Bias knob shifts harmonic character (even vs odd)
- [ ] Sag knob adds dynamic compression on sustained signals
- [ ] Output Trim adjusts final level
- [ ] Mix blends dry and wet (0% = clean passthrough)
- [ ] Mode selector switches between Triode, Pentode, Torture
- [ ] Triode sounds warm and soft
- [ ] Pentode sounds aggressive with more odd harmonics
- [ ] Torture sounds extreme with heavily rolled-off top end
- [ ] No audio clicks when adjusting parameters
- [ ] No DC offset audible at output (even at extreme Bias settings)
- [ ] State save/restore works (DAW preset recall)

---

## Implementation Order

```
Phase 3 (CMakeLists.txt)  ← Do first so new files are in the build
Phase 1 (SaturatorDSP)    ← Core DSP engine
Phase 2 (Processor)       ← Wire parameters to DSP
Phase 4 (Editor)          ← GUI for new parameters
```

The recommended implementation order differs from the numbering because CMakeLists.txt must reference the new file before it can compile, and the DSP engine must exist before the processor can reference it.

## Testing Strategy

### Build Verification:

- Configure: `cmake -B build -S .`
- Build: `cmake --build build --config Release`
- Verify artifacts exist at `build/Saturator_artefacts/Release/`

### Manual Audio Testing Steps:

1. **Clean signal test**: Set Drive to 0 dB, Mix to 100% — signal should pass through with minimal coloration (just the pre/post EQ curves)
2. **Drive sweep**: Increase Drive from 0 to 48 dB at 1 kHz sine — observe progressive saturation, no clicks
3. **Bias test**: At moderate Drive, sweep Bias from -0.3 to +0.3 — listen for changing harmonic character
4. **Sag test**: Send sustained bass note, increase Sag — bass should compress and bloom, transients should still punch
5. **DC offset test**: Set Bias to +0.3, max Drive — check DC meter on output, should read near 0 (DC blocker working)
6. **Mode comparison**: Same signal through Triode vs Pentode vs Torture — each should sound distinctly different
7. **Mix test**: At high Drive, sweep Mix from 0% to 100% — hear gradual blend from dry to wet
8. **Aliasing check**: 10 kHz sine at max Drive — listen for metallic artifacts (should be minimal due to oversampling)

### Harmonic Analysis (if tools available):

- 100 Hz sine → check 2nd/3rd harmonic levels in Triode vs Pentode
- 1 kHz sine → verify harmonic spectrum shape
- Dual-tone (1 kHz + 1.5 kHz) → check intermodulation products

## Performance Considerations

- **Oversampling is the main CPU cost**. 4x processing means 4x the waveshaper work per sample. 8x (Torture) doubles that again. The IIR polyphase oversampler was chosen over FIR equiripple for lower latency.
- **Two `Oversampling` instances** both allocate internal buffers at `prepare()`. Memory impact is ~(8x block size × channels × sizeof(float) × 2) for the 8x instance. At 512 samples stereo, that's ~32 KB — negligible.
- **EQ coefficient recalculation per block** is 6 biquad coefficient computations per block (a few trig calls each). Negligible vs the oversampled processing loop.
- **Parameter smoothing** uses block-level granularity (one smoothed value per block, not per sample). This is a deliberate tradeoff: per-sample smoothing would require restructuring the DSP to accept per-sample parameters, significantly complicating the code for minimal audible benefit at typical block sizes (256-1024 samples).

## Migration Notes

- **Parameter IDs change**: `"drive"` range changes from 0-50 (linear) to 0-48 dB. `"mix"` and `"output"` IDs change to `"mix"` (same) and `"outputTrim"`. New parameters `"inputTrim"`, `"bias"`, `"sag"` are added. Old `"mode"` choices change from "Soft Clip/Hard Clip/TanH" to "Triode/Pentode/Torture".
- **State compatibility**: Existing presets/sessions saved with the old plugin will not load correctly due to changed parameter IDs and ranges. This is acceptable for a pre-release project with no user base.
- **Latency changes**: The plugin now reports latency (from oversampling). DAWs will automatically compensate, but users may notice a slight delay if monitoring through the plugin.

## References

- Signal chain spec: User-provided Culture Vulture DSP specification
- JUCE Oversampling API: `build/_deps/juce-src/modules/juce_dsp/processors/juce_Oversampling.h`
- JUCE IIR Coefficients API: `build/_deps/juce-src/modules/juce_dsp/processors/juce_IIRFilter.h`
- JUCE ProcessorDuplicator: `build/_deps/juce-src/modules/juce_dsp/processors/juce_ProcessorDuplicator.h`
- Existing processor: `Source/PluginProcessor.cpp:68-94` (current saturate function, to be replaced)
- Existing editor: `Source/PluginEditor.cpp` (current GUI, to be expanded)
