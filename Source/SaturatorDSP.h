#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>

class SaturatorDSP
{
public:
    enum class Mode { Triode, Pentode, Torture };

    SaturatorDSP();

    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();

    void process(juce::AudioBuffer<float>& buffer,
                 float inputTrimDb,
                 float driveDb,
                 float bias,
                 float sagAmount,
                 float outputTrimDb,
                 float mix,
                 Mode mode);

    float getLatencyInSamples(Mode mode) const;

private:
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentNumChannels = 2;

    // --- DC Blockers (one-pole HPF at ~5 Hz) ---
    struct DCBlocker
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float coeff = 0.0f;

        void prepare(double sampleRate);
        void reset();
        float process(float x);
    };
    std::array<DCBlocker, 2> preDCBlocker;
    std::array<DCBlocker, 2> postDCBlocker;

    // --- Pre-Emphasis EQ ---
    using IIRFilter = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    IIRFilter preHPF;
    IIRFilter preMidBoost;
    IIRFilter preHFShelf;

    // --- Post-Emphasis EQ ---
    IIRFilter postLPF;
    IIRFilter postLowShelf;
    IIRFilter postPresenceDip;

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
        float process(float input);
    };
    std::array<EnvelopeFollower, 2> sagEnvelope;

    // --- Valve Waveshaper ---
    struct ValveParams
    {
        float curvature;
        float asymmetry;
    };
    static ValveParams getValveParams(Mode mode);
    static float valveShaper(float x, float a, float b);

    // --- Internal helpers ---
    void updatePreEmphasis(double sampleRate, Mode mode);
    void updatePostEmphasis(double sampleRate, Mode mode);

    juce::AudioBuffer<float> dryBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturatorDSP)
};
