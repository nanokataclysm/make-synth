#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace makesynth
{
constexpr double pi = 3.14159265358979323846;
struct Parameters
{
    int mode = 0;
    bool drone = false;
    float frequency = 110.0f, cutoff = 1200.0f, resonance = 0.15f;
    float rate = 0.05f, motion = 0.15f, detune = 6.0f;
    float fmRatio = 1.4142f, fmDepth = 0.8f, breath = 0.45f;
    // 0 sine, 1 triangle, 2 saw, 3 square, 4 pulse. Triangle is the default so
    // that sessions saved before the wave selector existed reload unchanged.
    int wave = 1;
    float width = 0.35f;
    // How much patched audio joins the source, and whether a Patch In bus is
    // connected at all. patchConnected comes from bus state, not signal
    // detection, and holds the envelope gate open so patched audio passes
    // with no note held.
    float patchLevel = 0.5f;
    bool patchConnected = false;
    bool pink = true;
};

// Per-sample inputs. Grows as later phases add sources; a struct keeps call
// sites readable and makes a mis-ordered argument a compile error rather than
// silently misrouted modulation.
struct SampleInputs
{
    float patch = 0;
    float cvCutoff = 0, cvPitch = 0, cvFmDepth = 0, cvWidth = 0;
};

class Filter
{
public:
    void reset() noexcept { s1 = s2 = 0; }
    void tune(double rate, double cutoff, double q) noexcept
    {
        const auto g = std::tan(pi * std::clamp(cutoff, 15.0, rate * 0.42) / rate);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float process(float input, bool bandpass = false) noexcept
    {
        const auto v3 = static_cast<double>(input) - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
        return static_cast<float>(bandpass ? k * v1 : v2);
    }
private:
    double s1 = 0, s2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1;
};

// Audio-thread core: fixed storage, no locks, no heap allocation, no I/O.
// The plugin runs this at 4x the host sample rate and downsamples its output.
class SynthEngine
{
public:
    void prepare(double sampleRate) noexcept
    {
        rate = std::max(8000.0, sampleRate);
        smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (0.025 * rate)));
        attack = static_cast<float>(1.0 - std::exp(-1.0 / (0.008 * rate)));
        release = static_cast<float>(1.0 - std::exp(-1.0 / (0.15 * rate)));
        dcCoefficient = std::exp(-2.0 * pi * 5.0 / rate);
        reset();
    }
    void reset() noexcept
    {
        phase1 = phase2 = carrierPhase = modPhase = lfoPhase = 0;
        envelope = 0; noteActive = false; noteFrequency = 110; velocity = 1;
        current = target;
        weights = {0, 0, 0}; weights[static_cast<size_t>(std::clamp(target.mode, 0, 2))] = 1;
        randomState = 0x8c31f249u; pinkCounter = 0; pinkRows.fill(0); pinkSum = 0;
        pinkMix = target.pink ? 1.0f : 0.0f;
        dcInput = dcOutput = 0;
        preFilterTap = postFilterTap = 0;
        coefficientCounter = 0;
        for (auto& f : filters) f.reset();
    }
    void setParameters(const Parameters& p) noexcept { target = p; }
    void setNote(bool active, float frequency, float noteVelocity = 1) noexcept
    {
        noteActive = active;
        if (active) { noteFrequency = frequency; velocity = noteVelocity; }
    }
    // Read after each process() call. Valid until the next call.
    float lastPreFilter()  const noexcept { return preFilterTap; }
    float lastPostFilter() const noexcept { return postFilterTap; }
    float process(const SampleInputs& in = {}) noexcept
    {
        auto smooth = [this](float& value, float to) { value += smoothing * (to - value); };
        const float wantedPitch = noteActive ? noteFrequency : (target.drone ? target.frequency : current.frequency);
        smooth(current.frequency, std::clamp(wantedPitch, 10.0f, 16000.0f));
        smooth(current.cutoff, target.cutoff); smooth(current.resonance, target.resonance);
        smooth(current.rate, target.rate); smooth(current.motion, target.motion);
        smooth(current.detune, target.detune); smooth(current.fmRatio, target.fmRatio);
        smooth(current.fmDepth, target.fmDepth); smooth(current.breath, target.breath);
        smooth(current.width, target.width);
        smooth(current.patchLevel, target.patchLevel);
        const float gate = noteActive ? velocity
                         : (target.drone || target.patchConnected) ? 1.0f
                         : 0.0f;
        envelope += (gate > envelope ? attack : release) * (gate - envelope);
        if (gate == 0 && envelope < 1.0e-7f) envelope = 0;
        for (size_t i = 0; i < 3; ++i)
            weights[i] += smoothing * ((static_cast<int>(i) == target.mode ? 1.0f : 0.0f) - weights[i]);

        advance(lfoPhase, current.rate);
        const auto lfo = std::sin(lfoPhase);
        const double base = std::clamp(static_cast<double>(current.frequency), 10.0, rate * 0.1);
        const double second = base * std::exp2(current.detune / 1200.0);
        advance(phase1, base); advance(phase2, second);
        const int shape = std::clamp(target.wave, 0, 4);
        const double pulseWidth = std::clamp(static_cast<double>(current.width), 0.15, 0.85);
        const float drone = 0.5f * waveGain[static_cast<size_t>(shape)]
                          * (oscillator(shape, phase1, base, pulseWidth)
                           + oscillator(shape, phase2, second, pulseWidth));

        const float white = random();
        const unsigned row = trailingZeroes(++pinkCounter) & 15u;
        pinkSum -= pinkRows[row]; pinkRows[row] = random(); pinkSum += pinkRows[row];
        const float pink = (pinkSum + white) * 0.13f;
        // Crossfade colour changes instead of abruptly changing noise streams.
        pinkMix += smoothing * ((target.pink ? 1.0f : 0.0f) - pinkMix);
        const float noise = (white + pinkMix * (pink - white)) * 0.75f;

        const auto modFrequency = std::clamp(base * current.fmRatio * std::exp2(current.motion * 0.04 * lfo), 1.0, rate * 0.12);
        // Restrict the modulation bandwidth near the internal Nyquist limit.
        const auto maxIndex = std::max(0.0, (rate * 0.4 - base) / modFrequency - 1.0);
        const auto index = std::min(static_cast<double>(current.fmDepth), maxIndex);
        advance(modPhase, modFrequency);
        advance(carrierPhase, base + modFrequency * index * std::sin(modPhase));
        const float metallic = static_cast<float>(std::sin(carrierPhase));

        if ((coefficientCounter++ & 31u) == 0)
        {
            const auto sweep = current.cutoff * std::exp2(current.motion * 3.0 * lfo);
            const auto q = 0.707 + std::clamp(current.resonance, 0.0f, 1.0f) * 9.0;
            filters[0].tune(rate, sweep, q);
            filters[1].tune(rate, sweep, q);
            filters[2].tune(rate, current.cutoff, q);
        }
        const auto swell = static_cast<float>(1.0 - current.breath * 0.5 + current.breath * 0.5 * lfo);
        // Patched audio joins each mode source, so it picks up whichever
        // filter the active mode uses. The weights sum to ~1, so its total
        // contribution stays at unity across mode changes.
        const float patch = in.patch * current.patchLevel;
        preFilterTap = weights[0] * drone + weights[1] * noise + weights[2] * metallic + patch;
        const float mixed = weights[0] * filters[0].process(drone + patch)
                          + weights[1] * filters[1].process(noise + patch, true) * swell
                          + weights[2] * filters[2].process(metallic + patch);
        postFilterTap = mixed;
        const auto shaped = std::tanh(mixed * 0.85);
        dcOutput = shaped - dcInput + dcCoefficient * dcOutput;
        dcInput = shaped;
        return static_cast<float>(dcOutput) * envelope;
    }
private:
    void advance(double& phase, double hz) const noexcept
    {
        phase += 2 * pi * hz / rate;
        if (phase >= 2*pi) phase -= 2*pi;
        if (phase < 0) phase += 2*pi;
    }
    // Corrects the step discontinuity of saw/square/pulse over one sample either
    // side of the edge, which removes most of the aliasing they would otherwise
    // fold back. Sine and triangle are alias-free by construction already.
    static double polyBlep(double t, double dt) noexcept
    {
        if (dt <= 0) return 0;
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0;
    }
    float oscillator(int shape, double phase, double hz, double width) const noexcept
    {
        if (shape == 1) return triangle(phase, hz);
        if (shape == 0) return static_cast<float>(std::sin(phase));
        const double dt = std::clamp(hz / rate, 0.0, 0.45);
        double t = phase / (2 * pi);
        t -= std::floor(t);
        if (shape == 2)
            return static_cast<float>(2.0 * t - 1.0 - polyBlep(t, dt));
        // Square is a fixed half cycle; pulse follows the width control.
        const double w = shape == 3 ? 0.5 : width;
        // Remove the width-dependent DC here: the waveshaper downstream would
        // otherwise clip the offset asymmetrically.
        double value = (t < w ? 1.0 : -1.0) - (2.0 * w - 1.0);
        value += polyBlep(t, dt);
        double fall = t - w; fall -= std::floor(fall);
        value -= polyBlep(fall, dt);
        return static_cast<float>(value);
    }
    float triangle(double phase, double hz) const noexcept
    {
        double result = 0;
        // Finite additive triangle: no discontinuity and no above-Nyquist partials.
        for (int n = 1; n <= 13 && n * hz < rate * 0.44; n += 2)
            result += (((n-1)/2) % 2 == 0 ? 1.0 : -1.0) * std::sin(n * phase) / (n*n);
        return static_cast<float>(result * (8.0 / (pi*pi)));
    }
    float random() noexcept
    {
        randomState ^= randomState << 13; randomState ^= randomState >> 17; randomState ^= randomState << 5;
        return static_cast<float>(static_cast<double>(randomState) / 2147483648.0 - 1.0);
    }
    static unsigned trailingZeroes(uint32_t n) noexcept
    {
        if (n == 0) return 16;
        unsigned count = 0; while ((n & 1u) == 0) { ++count; n >>= 1; } return count;
    }
    double rate = 192000, phase1 = 0, phase2 = 0, carrierPhase = 0, modPhase = 0, lfoPhase = 0;
    double dcInput = 0, dcOutput = 0, dcCoefficient = 0.999;
    float smoothing = 0.001f, attack = 0.001f, release = 0.0001f, envelope = 0;
    float preFilterTap = 0, postFilterTap = 0;
    float noteFrequency = 110, velocity = 1, pinkSum = 0, pinkMix = 1;
    bool noteActive = false;
    Parameters target, current;
    // Roughly equal loudness per shape, referenced to the triangle.
    static constexpr std::array<float,5> waveGain {0.82f, 1.0f, 1.0f, 0.58f, 0.58f};
    std::array<float,3> weights {1,0,0};
    std::array<Filter,3> filters;
    std::array<float,16> pinkRows {};
    uint32_t randomState = 0x8c31f249u, pinkCounter = 0, coefficientCounter = 0;
};
}
