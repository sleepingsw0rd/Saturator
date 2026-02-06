# Saturator

A Culture Vulture-inspired valve saturation plugin built with JUCE. Delivers warm analog-style harmonic distortion through an asymmetric waveshaper with pre/post emphasis EQ, oversampling, and dynamic sag compression.

## Signal Flow

```
Input Trim
  -> DC Blocker (5 Hz one-pole HPF)
  -> Pre-Emphasis EQ (HPF + mid boost + HF shelf)
  -> Oversampling (4x or 8x)
  -> Bias + Drive
  -> Nonlinear Valve Stage (asymmetric tanh waveshaper)
  -> Dynamic Sag / Valve Compression
  -> Downsample
  -> Post-Emphasis EQ (LPF + low shelf + presence dip)
  -> DC Blocker (5 Hz one-pole HPF)
  -> Output Trim
  -> Dry/Wet Mix
```

Each stage is designed to work together the way a real valve circuit does: the pre-emphasis EQ pushes mids into the distortion so it speaks instead of farting out, the oversampling prevents aliasing from the nonlinearity, the sag compresses sustained signals while letting transients punch through, and the post-emphasis EQ tames the harsh high-frequency content that distortion generates.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Input Trim** | -24 to +24 dB | 0 dB | Gain before the saturation stage. Use this to hit the valve harder or softer. |
| **Drive** | 0 to 60 dB | 20 dB | Main distortion amount. Converted to linear gain (`10^(dB/20)`) and applied before the waveshaper. Skew factor gives finer control at lower values. |
| **Bias** | -0.6 to +0.6 | 0.0 | Adds DC offset before the waveshaper, creating asymmetry. At 0 the clipping is roughly symmetric (triode-like even harmonics). Pushing it positive or negative shifts toward pentode-like odd harmonic content. |
| **Sag** | 0 to 60% | 15% | Dynamic valve compression. An envelope follower (8ms attack, 200ms release) tracks signal level and reduces the effective drive under sustained load. Makes bass compress and bloom while transients still punch through. |
| **Output Trim** | -24 to +24 dB | 0 dB | Gain after the saturation stage. Use to compensate for level changes from the drive. |
| **Mix** | 0 to 100% | 100% | Dry/wet parallel blend. Essential for parallel saturation on drums and bass. |
| **Mode** | Triode / Pentode / Torture | Triode | Selects the saturation character (see below). |

## Modes

### Triode
Warm, smooth saturation with moderate harmonic generation.
- Waveshaper curvature: 2.5, asymmetry: 0.5
- Pre-EQ: +4 dB mid boost @ 1 kHz, +2 dB HF shelf @ 6 kHz
- Post-EQ: 14 kHz LPF, -2 dB presence dip @ 3 kHz, +2 dB low shelf @ 120 Hz
- Oversampling: 4x

### Pentode
Aggressive saturation with strong odd-harmonic asymmetry and prominent mid emphasis.
- Waveshaper curvature: 4.0, asymmetry: 0.85
- Pre-EQ: +8 dB mid boost @ 1 kHz, +3 dB HF shelf @ 6 kHz
- Post-EQ: 11 kHz LPF, -4 dB presence dip @ 3 kHz, +3.5 dB low shelf @ 120 Hz
- Oversampling: 4x

### Torture
Extreme distortion with heavy filtering and maximum waveshaper aggression.
- Waveshaper curvature: 8.0, asymmetry: 0.7
- Pre-EQ: +6 dB mid boost @ 1 kHz, +4 dB HF shelf @ 6 kHz
- Post-EQ: 8 kHz LPF, -6 dB presence dip @ 3 kHz, +4 dB low shelf @ 120 Hz
- Oversampling: 8x

## Technical Details

### Valve Waveshaper

The core nonlinearity is an asymmetric soft clipper using hyperbolic tangent:

```cpp
float valveShaper(float x, float a, float b)
{
    float xp = x * (1.0f + b);  // positive half scaled up
    float xn = x * (1.0f - b);  // negative half scaled down

    return (x >= 0.0f)
        ? tanh(a * xp)
        : tanh(a * xn);
}
```

- `a` (curvature) controls how hard the signal clips — higher values create a sharper knee
- `b` (asymmetry) creates different gain for positive vs negative signal halves, generating even-order harmonics (2nd, 4th) alongside the odd-order harmonics (3rd, 5th) that symmetric clippers produce

### Oversampling

Uses JUCE's `dsp::Oversampling` with IIR polyphase half-band filters for minimum latency. 4x oversampling (2 cascaded 2x stages) for Triode and Pentode modes, 8x (3 cascaded 2x stages) for Torture mode. Both instances are pre-allocated and the active one is selected per audio block based on the current mode.

### DC Blocking

Two one-pole high-pass filters at ~5 Hz (one before and one after the nonlinear stage). The pre-blocker prevents any existing DC offset from affecting the bias point. The post-blocker removes the DC component introduced by the asymmetric waveshaper and bias offset.

```
y[n] = x[n] - x[n-1] + R * y[n-1]
R = 1 - (2 * pi * 5 / sampleRate)
```

### Pre-Emphasis EQ

Three biquad filters in series before the waveshaper:
1. **High-pass** at 60 Hz (Q=0.5) — prevents low-frequency content from dominating the distortion
2. **Peak filter** at 1 kHz (Q=0.6) — pushes mids into the valve for articulate distortion
3. **High shelf** at 6 kHz (Q=0.7) — adds presence before saturation

Gain values are mode-dependent (see Modes section).

### Post-Emphasis EQ

Three biquad filters in series after downsampling:
1. **Low-pass** — rolls off harsh high-frequency distortion products (mode-dependent cutoff)
2. **Low shelf** at 120 Hz — restores warmth that the pre-emphasis HPF removed
3. **Peak filter** at 3 kHz — dips the presence region to tame digital fizz

### Dynamic Sag

An envelope follower with 8ms attack and 200ms release tracks the signal amplitude at the oversampled rate. The envelope value modulates the effective drive:

```
effectiveDrive = drive * (1.0 - sagAmount * envelope)
```

This means louder/sustained passages get less drive (compressing naturally), while transients pass through at full drive before the envelope catches up.

### Parameter Smoothing

All continuous parameters use `juce::SmoothedValue` with a 50ms linear ramp to prevent zipper noise during automation. Values are advanced by the full block size each audio callback.

## Building

### Requirements
- CMake 3.22+
- C++17 compiler (MSVC, Clang, or GCC)
- Internet connection (JUCE 7.0.12 is fetched automatically via CMake FetchContent)

### Build Steps

```bash
cmake -B build -S .
cmake --build build --config Release
```

### Output

- **VST3**: `build/Saturator_artefacts/Release/VST3/Saturator.vst3`
- **Standalone**: `build/Saturator_artefacts/Release/Standalone/Saturator.exe`

A pre-built VST3 binary (Windows x64) is included in the `vst3/` folder.

## Project Structure

```
Saturator/
  CMakeLists.txt              # Build config, JUCE FetchContent
  Source/
    SaturatorDSP.h            # DSP engine class declaration
    SaturatorDSP.cpp          # Full signal chain implementation
    PluginProcessor.h          # JUCE AudioProcessor wrapper
    PluginProcessor.cpp        # Parameter layout, smoothing, processBlock
    PluginEditor.h             # GUI class declaration
    PluginEditor.cpp           # 6 rotary knobs + mode selector
  vst3/
    Saturator.vst3             # Pre-built Windows x64 binary
```

## License

This project is provided as-is for educational and personal use.
