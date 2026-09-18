# Modular Patching Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let one Make Synth instance's control signals shape another — audio-rate CV into cutoff, pitch, FM depth and pulse width, with the LFO and envelope exposed as CV outputs.

**Architecture:** CV applies *after* the existing 25 ms parameter smoother, per sample, as a modulation of the already-smoothed value — the smoother would otherwise destroy any control signal. A 4-channel "CV In" bus carries the four destinations; four attenuverters (−1…+1, default 0) scale and invert them. Filter coefficients, normally refreshed every 32 samples to avoid per-sample `std::tan`, switch to per-sample tuning while cutoff CV is live.

**Tech Stack:** C++17, JUCE 8 (`juce_audio_processors`, `juce_dsp`), CMake + CTest, hand-rolled `require()` assertions (no test framework), pluginval for host validation.

**Spec:** `docs/superpowers/specs/2026-09-18-modular-patching-phase-2-design.md`

## Global Constraints

- **C++17 only.** Designated initialisers (`{.patch = x}`) are C++20 and MSVC builds in CI — use positional braces.
- **Never insert a parameter — only append.** `readParameters()` and every `case` in `handleMidi()` index numerically into `ids` in `Source/PluginProcessor.h`. Inserting shifts every later index and breaks MIDI CC mapping with no compiler error.
- **Declare new parameters last in `layout()`** so existing host automation indices do not move.
- **Never insert a bus — only append.** Phase 1 proved disabled buses pack down; write offsets must be derived with `getChannelIndexInProcessBlockBuffer`, never hardcoded.
- **The golden guard must stay green.** `Tests/golden-engine.f32` compares 8192 engine samples within `1.0e-5f`. All CV attenuverters default to 0, so CV must be a no-op at defaults. If the guard fails, that is a real bug — never regenerate the reference or loosen the tolerance.
- **No allocation, locks, or I/O on the audio thread.** `process()` runs per sample at 4x host rate; `processBlock` is the audio callback.
- **Every new bus disabled by default.**
- **The web engine (`web/synth-engine.js`) is untouched in Phase 2.**

## Build and test commands

```bash
# Engine tests only (fast) — use while iterating
cmake --build build --target MakeSynthDspTests --parallel 4 && ctest --test-dir build -R dsp --output-on-failure

# Processor tests (full plugin build, several minutes)
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test

# Everything
ctest --test-dir build --output-on-failure
```

---

### Task 1: SampleInputs struct — pure refactor, zero behaviour change

**Files:**
- Modify: `Source/SynthEngine.h`
- Modify: `Source/PluginProcessor.cpp` (1 call site)
- Modify: `Tests/DspTests.cpp` (8 call sites)

**Interfaces:**
- Consumes: nothing
- Produces: `makesynth::SampleInputs` and `float SynthEngine::process(const SampleInputs& in = {}) noexcept`

- [ ] **Step 1: Add the struct**

In `Source/SynthEngine.h`, immediately after the closing `};` of `struct Parameters`:

```cpp
// Per-sample inputs. Grows as later phases add sources; a struct keeps call
// sites readable and makes a mis-ordered argument a compile error rather than
// silently misrouted modulation.
struct SampleInputs
{
    float patch = 0;
    float cvCutoff = 0, cvPitch = 0, cvFmDepth = 0, cvWidth = 0;
};
```

- [ ] **Step 2: Change the signature**

Replace `float process(float patchIn = 0.0f) noexcept` with:

```cpp
    float process(const SampleInputs& in = {}) noexcept
```

- [ ] **Step 3: Replace the mutated local**

The body currently does `patchIn *= current.patchLevel;`. `in` is a const reference, so that mutation is illegal. Replace that line with a local, and update the three uses below it:

```cpp
        const float patch = in.patch * current.patchLevel;
        preFilterTap = weights[0] * drone + weights[1] * noise + weights[2] * metallic + patch;
        const float mixed = weights[0] * filters[0].process(drone + patch)
                          + weights[1] * filters[1].process(noise + patch, true) * swell
                          + weights[2] * filters[2].process(metallic + patch);
```

- [ ] **Step 4: Update the processor call site**

In `Source/PluginProcessor.cpp`, change:

```cpp
            const float x=engine.process(high.getChannelPointer(0)[i]);
```

to:

```cpp
            const float x=engine.process({high.getChannelPointer(0)[i]});
```

- [ ] **Step 5: Update the test call sites**

In `Tests/DspTests.cpp` there are 8 calls passing an argument. Wrap each argument in braces — for example `e.process(0.5f)` becomes `e.process({0.5f})`, and `engine.process(0.4f * std::sin(i * 0.1))` becomes `engine.process({0.4f * std::sin(i * 0.1)})`. The 20 zero-argument `process()` calls are unchanged.

Find them with: `grep -n "\.process(" Tests/DspTests.cpp`

- [ ] **Step 6: Build and run the full suite**

```bash
cmake --build build --config Release --parallel 4
ctest --test-dir build --output-on-failure
```

Expected: 3/3 pass, including the golden guard. This task changes no behaviour whatsoever — if the golden fails, the `patch` local was wired differently from the `patchIn` it replaced.

- [ ] **Step 7: Commit**

```bash
git add Source/SynthEngine.h Source/PluginProcessor.cpp Tests/DspTests.cpp
git commit -m "Pass per-sample engine inputs as a struct"
```

---

### Task 2: CV parameters and the three non-filter destinations

**Files:**
- Modify: `Source/SynthEngine.h`
- Test: `Tests/DspTests.cpp`

**Interfaces:**
- Consumes: `SampleInputs` from Task 1
- Produces: `Parameters::cvCutoffAmount`, `cvPitchAmount`, `cvFmAmount`, `cvWidthAmount` (float, default 0), `Parameters::cvConnected` (bool, default false)

- [ ] **Step 1: Write the failing tests**

Add to `Tests/DspTests.cpp`:

```cpp
{
    // Each destination must move the output when its attenuverter is raised and
    // stay inert when it is zero. Differenced against a silent-CV run, because
    // the oscillator sounds either way and an absolute threshold would measure it.
    auto energyWith = [](float amount, bool feedCv, int destination)
    {
        makesynth::SynthEngine engine;
        makesynth::Parameters q;
        q.mode = destination == 2 ? 2 : 0;
        q.drone = true; q.cvConnected = true;
        q.motion = 0; q.breath = 0;
        if (destination == 1) q.cvPitchAmount = amount;
        if (destination == 2) q.cvFmAmount = amount;
        if (destination == 3) { q.wave = 4; q.cvWidthAmount = amount; }
        engine.setParameters(q); engine.prepare(48000);
        double total = 0;
        for (int i = 0; i < 24000; ++i)
        {
            makesynth::SampleInputs in;
            const float cv = feedCv ? 0.6f * std::sin(i * 0.02) : 0.0f;
            if (destination == 1) in.cvPitch = cv;
            if (destination == 2) in.cvFmDepth = cv;
            if (destination == 3) in.cvWidth = cv;
            const auto x = engine.process(in);
            total += x * x;
        }
        return total;
    };

    for (int destination = 1; destination <= 3; ++destination)
    {
        const auto live = energyWith(1.0f, true, destination);
        const auto inert = energyWith(1.0f, false, destination);
        require(std::abs(live - inert) / std::max(live, inert) > 0.01,
                "A CV destination did not respond to its channel");
        require(energyWith(0.0f, true, destination) == energyWith(0.0f, false, destination),
                "A CV destination moved with its attenuverter at zero");
    }

    // Pitch CV must not push the oscillator past Nyquist at extreme input.
    makesynth::SynthEngine hot;
    makesynth::Parameters h;
    h.mode = 0; h.drone = true; h.frequency = 1000; h.cvPitchAmount = 1.0f; h.cvConnected = true;
    hot.setParameters(h); hot.prepare(48000);
    for (int i = 0; i < 24000; ++i)
    {
        makesynth::SampleInputs in; in.cvPitch = 1.0f;
        const auto x = hot.process(in);
        require(std::isfinite(x) && std::abs(x) < 2.0f, "Extreme pitch CV is unstable");
    }
    std::cout << "CV destination checks passed\n";
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4
```

Expected: COMPILE FAILURE — `no member named 'cvConnected' in 'makesynth::Parameters'`.

- [ ] **Step 3: Add the parameter fields**

In `struct Parameters` in `Source/SynthEngine.h`, after `bool patchConnected = false;`:

```cpp
    // Attenuverters: bipolar so a negative value inverts the incoming CV.
    // All default to zero, so enabling the CV bus alone changes nothing.
    float cvCutoffAmount = 0, cvPitchAmount = 0, cvFmAmount = 0, cvWidthAmount = 0;
    bool cvConnected = false;
```

- [ ] **Step 4: Smooth the attenuverters**

They are knobs, not control signals, so they smooth like any other parameter. Beside `smooth(current.patchLevel, target.patchLevel);`:

```cpp
        smooth(current.cvCutoffAmount, target.cvCutoffAmount);
        smooth(current.cvPitchAmount, target.cvPitchAmount);
        smooth(current.cvFmAmount, target.cvFmAmount);
        smooth(current.cvWidthAmount, target.cvWidthAmount);
```

- [ ] **Step 5: Apply pitch CV**

Pitch CV multiplies the frequency *inside* the existing clamp, so the clamp's upper bound keeps CV from driving the oscillator past Nyquist for free. Replace:

```cpp
        const double base = std::clamp(static_cast<double>(current.frequency), 10.0, rate * 0.1);
```

with:

```cpp
        // +/-2 octaves at full attenuverter. Inside the clamp so CV cannot
        // push the oscillator past the internal Nyquist limit.
        const double pitchMod = std::exp2(in.cvPitch * current.cvPitchAmount * 2.0f);
        const double base = std::clamp(static_cast<double>(current.frequency) * pitchMod, 10.0, rate * 0.1);
```

- [ ] **Step 6: Apply width CV**

Replace:

```cpp
        const double pulseWidth = std::clamp(static_cast<double>(current.width), 0.15, 0.85);
```

with:

```cpp
        const double pulseWidth = std::clamp(static_cast<double>(current.width)
                                             + in.cvWidth * current.cvWidthAmount * 0.35f, 0.15, 0.85);
```

- [ ] **Step 7: Apply FM depth CV**

Replace:

```cpp
        const auto index = std::min(static_cast<double>(current.fmDepth), maxIndex);
```

with:

```cpp
        const auto modulatedFmDepth = std::clamp(current.fmDepth + in.cvFmDepth * current.cvFmAmount * 5.0f, 0.0f, 5.0f);
        const auto index = std::min(static_cast<double>(modulatedFmDepth), maxIndex);
```

- [ ] **Step 8: Run the tests**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4 && ctest --test-dir build -R dsp --output-on-failure
```

Expected: PASS including the golden guard. All attenuverters default to 0, so `std::exp2(0)` is 1 and the additive terms are 0 — CV is a mathematical no-op at defaults.

- [ ] **Step 9: Commit**

```bash
git add Source/SynthEngine.h Tests/DspTests.cpp
git commit -m "Add CV attenuverters and modulate pitch, FM depth and width"
```

---

### Task 3: Cutoff CV and adaptive coefficient updates

This is the task the spec exists for. Cutoff is the destination users reach for first, and it is the one the 32-sample coefficient interval breaks.

**Files:**
- Modify: `Source/SynthEngine.h`
- Test: `Tests/DspTests.cpp`

**Interfaces:**
- Consumes: `Parameters::cvCutoffAmount`, `cvConnected` from Task 2
- Produces: no new names

- [ ] **Step 1: Write the load-bearing test**

This is the test that catches CV being folded back into the smoothing path. A 25 ms one-pole lag attenuates a 500 Hz signal by roughly 40x; an unsmoothed path does not. Comparing a fast CV against a slow one at equal amplitude isolates that directly.

Add to `Tests/DspTests.cpp`:

```cpp
{
    // Modulation depth achieved by CV at two rates. If CV were routed through
    // the 25ms parameter smoother, the fast rate would be attenuated ~40x
    // relative to the slow one. Unsmoothed, both retain their effect.
    auto depthAtRate = [](double radiansPerSample, float amount)
    {
        makesynth::SynthEngine engine;
        makesynth::Parameters q;
        q.mode = 0; q.drone = true; q.cvConnected = true;
        q.cutoff = 800; q.resonance = 0; q.motion = 0; q.breath = 0;
        q.cvCutoffAmount = amount;
        engine.setParameters(q); engine.prepare(48000);
        for (int i = 0; i < 8000; ++i) { makesynth::SampleInputs in; engine.process(in); }
        double total = 0;
        for (int i = 0; i < 48000; ++i)
        {
            makesynth::SampleInputs in;
            in.cvCutoff = static_cast<float>(std::sin(i * radiansPerSample));
            const auto x = engine.process(in);
            total += x * x;
        }
        return total;
    };

    const double slowRate = 2.0 * 3.14159265358979 * 2.0 / 48000.0;    // 2 Hz
    const double fastRate = 2.0 * 3.14159265358979 * 500.0 / 48000.0;  // 500 Hz

    const auto slowLive = depthAtRate(slowRate, 1.0f);
    const auto slowFlat = depthAtRate(slowRate, 0.0f);
    const auto fastLive = depthAtRate(fastRate, 1.0f);
    const auto fastFlat = depthAtRate(fastRate, 0.0f);

    const auto slowEffect = std::abs(slowLive - slowFlat) / slowFlat;
    const auto fastEffect = std::abs(fastLive - fastFlat) / fastFlat;

    std::cout << "cutoff CV effect slow " << slowEffect << " fast " << fastEffect << '\n';
    require(slowEffect > 0.01, "Slow cutoff CV had no effect");
    require(fastEffect > slowEffect * 0.25,
            "Fast cutoff CV was attenuated — CV is being smoothed");
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4 && ctest --test-dir build -R dsp --output-on-failure
```

Expected: FAIL with `Slow cutoff CV had no effect` — cutoff CV is not implemented yet, so both runs are identical.

- [ ] **Step 3: Apply cutoff CV and make the update adaptive**

Replace the whole coefficient block:

```cpp
        if ((coefficientCounter++ & 31u) == 0)
        {
            const auto sweep = current.cutoff * std::exp2(current.motion * 3.0 * lfo);
            const auto q = 0.707 + std::clamp(current.resonance, 0.0f, 1.0f) * 9.0;
            filters[0].tune(rate, sweep, q);
            filters[1].tune(rate, sweep, q);
            filters[2].tune(rate, current.cutoff, q);
        }
```

with:

```cpp
        // Tuning calls std::tan, so coefficients normally refresh every 32
        // samples. That is 6kHz at the internal rate — ample for the LFO sweep
        // and far too slow for audio-rate CV, which would arrive stepped. Tune
        // every sample only while cutoff CV can actually move the filter, so
        // patches that never use it keep today's cost exactly.
        const float cutoffMod = std::exp2(in.cvCutoff * current.cvCutoffAmount * 4.0f);
        const auto modulatedCutoff = current.cutoff * cutoffMod;
        const bool liveCutoffCv = target.cvConnected && current.cvCutoffAmount != 0.0f;
        const bool dueForTune = (coefficientCounter++ & 31u) == 0;
        if (liveCutoffCv || dueForTune)
        {
            const auto sweep = modulatedCutoff * std::exp2(current.motion * 3.0 * lfo);
            const auto q = 0.707 + std::clamp(current.resonance, 0.0f, 1.0f) * 9.0;
            filters[0].tune(rate, sweep, q);
            filters[1].tune(rate, sweep, q);
            filters[2].tune(rate, modulatedCutoff, q);
        }
```

Note `coefficientCounter++` is evaluated unconditionally via `dueForTune`, so disabling CV mid-stream resumes the 32-sample cadence in phase instead of jumping.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4 && ctest --test-dir build -R dsp --output-on-failure
```

Expected: PASS, with both effect figures printed and the golden guard still green.

- [ ] **Step 5: Prove the test would catch smoothing**

Temporarily route CV through the smoother to confirm the guard fires: add `smooth(current.cvCutoffAmount, 0.0f);` immediately before the coefficient block (a crude stand-in for smoothing the CV path), rebuild, and confirm the test FAILS with `Fast cutoff CV was attenuated`. Then revert with `git checkout -- Source/SynthEngine.h` and confirm `git status` is clean.

Report both outputs. A test for a subtle regression that has never been seen to fail is not yet a test.

- [ ] **Step 6: Commit**

```bash
git add Source/SynthEngine.h Tests/DspTests.cpp
git commit -m "Modulate cutoff from CV with adaptive coefficient updates"
```

---

### Task 4: CV outputs from the LFO and envelope

**Files:**
- Modify: `Source/SynthEngine.h`
- Test: `Tests/DspTests.cpp`

**Interfaces:**
- Consumes: nothing from Tasks 1-3
- Produces: `float SynthEngine::lastLfo() const noexcept` (bipolar −1…+1), `float SynthEngine::lastEnvelope() const noexcept` (unipolar 0…1)

- [ ] **Step 1: Write the failing test**

```cpp
{
    makesynth::SynthEngine e;
    makesynth::Parameters p;
    p.mode = 0; p.drone = true; p.rate = 5.0f;
    e.setParameters(p); e.prepare(48000);

    // The LFO must actually traverse its range at the configured rate.
    float lowest = 1.0f, highest = -1.0f;
    for (int i = 0; i < 48000; ++i)
    {
        e.process();
        lowest = std::min(lowest, e.lastLfo());
        highest = std::max(highest, e.lastLfo());
        require(e.lastLfo() >= -1.0f && e.lastLfo() <= 1.0f, "LFO CV left its range");
    }
    require(highest > 0.9f && lowest < -0.9f, "LFO CV did not traverse its range");

    // The envelope must track the gate: rising while held, falling once released.
    makesynth::SynthEngine g;
    makesynth::Parameters q;
    q.mode = 0; q.drone = true;
    g.setParameters(q); g.prepare(48000);
    for (int i = 0; i < 4800; ++i) g.process();
    const auto held = g.lastEnvelope();
    require(held > 0.9f, "Envelope CV did not open on a held gate");
    q.drone = false;
    g.setParameters(q);
    for (int i = 0; i < 48000; ++i) g.process();
    require(g.lastEnvelope() < held * 0.1f, "Envelope CV did not fall after release");
    require(g.lastEnvelope() >= 0.0f, "Envelope CV went negative");
    std::cout << "CV output checks passed\n";
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4
```

Expected: COMPILE FAILURE — `no member named 'lastLfo'`.

- [ ] **Step 3: Add the members**

Beside `float preFilterTap = 0, postFilterTap = 0;` in the private data:

```cpp
    float lfoTap = 0, envelopeTap = 0;
```

And in `reset()`, beside `preFilterTap = postFilterTap = 0;`:

```cpp
        lfoTap = envelopeTap = 0;
```

- [ ] **Step 4: Add the accessors**

Directly after `lastPostFilter()`:

```cpp
    // Modulation sources for the CV Out bus. The LFO runs regardless of gate,
    // so this bus is never silent — correct for a modulation source.
    float lastLfo()      const noexcept { return lfoTap; }
    float lastEnvelope() const noexcept { return envelopeTap; }
```

- [ ] **Step 5: Capture them in process()**

Immediately after `const auto lfo = std::sin(lfoPhase);`:

```cpp
        lfoTap = static_cast<float>(lfo);
```

And immediately after the `if (gate == 0 && envelope < 1.0e-7f) envelope = 0;` line:

```cpp
        envelopeTap = envelope;
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4 && ctest --test-dir build -R dsp --output-on-failure
```

Expected: PASS, golden guard green (these are write-only taps that never feed the audio path).

- [ ] **Step 7: Commit**

```bash
git add Source/SynthEngine.h Tests/DspTests.cpp
git commit -m "Expose the LFO and envelope as CV outputs"
```

---

### Task 5: Declare the CV buses

**Files:**
- Modify: `Source/PluginProcessor.cpp` (constructor `BusesProperties`, `isBusesLayoutSupported`)
- Test: `Tools/Render.cpp`

**Interfaces:**
- Consumes: nothing
- Produces: input bus 1 = "CV In" (4 channels), output bus 3 = "CV Out" (stereo), both disabled by default

- [ ] **Step 1: Write the failing test**

In `Tools/Render.cpp`, inside `tests()` immediately after the existing bus-layout block:

```cpp
    {
        require(p.getBusCount(true) == 2, "Expected two input buses");
        require(p.getBusCount(false) == 4, "Expected four output buses");
        require(p.getBus(true, 1)->getName() == "CV In", "Input bus 1 must be CV In");
        require(p.getBus(false, 3)->getName() == "CV Out", "Output bus 3 must be CV Out");
        require(!p.getBus(true, 1)->isEnabledByDefault(), "CV In must default to disabled");
        require(!p.getBus(false, 3)->isEnabledByDefault(), "CV Out must default to disabled");

        using Set = juce::AudioChannelSet;
        auto layoutOf = [](Set patch, Set cv, Set out0, Set out1, Set out2, Set cvOut)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add(patch); l.inputBuses.add(cv);
            l.outputBuses.add(out0); l.outputBuses.add(out1);
            l.outputBuses.add(out2); l.outputBuses.add(cvOut);
            return l;
        };
        const auto none = Set::disabled();
        const auto quad = Set::quadraphonic();
        require(quad.size() == 4, "quadraphonic must be a 4-channel set");
        require(p.checkBusesLayoutSupported(layoutOf(none, none, Set::stereo(), none, none, none)),
                "Everything optional disabled must be supported");
        require(p.checkBusesLayoutSupported(layoutOf(none, quad, Set::stereo(), none, none, none)),
                "4-channel CV In must be supported");
        require(p.checkBusesLayoutSupported(layoutOf(none, none, Set::stereo(), none, none, Set::stereo())),
                "Stereo CV Out must be supported");
        require(!p.checkBusesLayoutSupported(layoutOf(none, Set::stereo(), Set::stereo(), none, none, none)),
                "Stereo CV In must be rejected");
        require(!p.checkBusesLayoutSupported(layoutOf(none, none, Set::stereo(), none, none, Set::mono())),
                "Mono CV Out must be rejected");
    }
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
```

Expected: FAIL with `Expected two input buses`.

- [ ] **Step 3: Declare the buses**

Append to the constructor's `BusesProperties` chain in `Source/PluginProcessor.cpp` — appended so Phase 1's bus indices do not move:

```cpp
          .withInput ("CV In",       juce::AudioChannelSet::quadraphonic(), false)
          .withOutput("CV Out",      juce::AudioChannelSet::stereo(), false)),
```

Place `.withInput("CV In", ...)` after the existing `.withInput("Patch In", ...)` and `.withOutput("CV Out", ...)` after `.withOutput("Post-Filter", ...)`.

- [ ] **Step 4: Accept the new layouts**

In `isBusesLayoutSupported`, before the final `return true;`:

```cpp
    const auto cv = b.getChannelSet(true, 1);
    if (!cv.isDisabled() && cv.size() != 4)
        return false;
    const auto cvOut = b.getChannelSet(false, 3);
    if (!cvOut.isDisabled() && cvOut != juce::AudioChannelSet::stereo())
        return false;
```

- [ ] **Step 5: Run the tests**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
ctest --test-dir build --output-on-failure
```

Expected: PASS. The existing `New instrument should be silent` assertion must still hold — every optional bus is still disabled by default.

- [ ] **Step 6: Commit**

```bash
git add Source/PluginProcessor.cpp Tools/Render.cpp
git commit -m "Declare the CV input and output buses"
```

---

### Task 6: Route CV In into the engine

The Phase 1 aliasing hazard applies again: input and output buses share buffer memory, so CV must be captured before `buffer.clear()`.

**Files:**
- Modify: `Source/PluginProcessor.h` (scratch buffer member), `Source/PluginProcessor.cpp` (`prepareToPlay`, `processBlock`)
- Test: `Tools/Render.cpp`

**Interfaces:**
- Consumes: bus indices from Task 5; `SampleInputs` from Task 1; `Parameters::cvConnected` from Task 2
- Produces: CV reaching the engine per sample

- [ ] **Step 1: Write the failing test**

In `Tools/Render.cpp`:

```cpp
    {
        // Each CV channel must reach its own destination and no other. This is
        // the test that catches a channel-order mistake, which would otherwise
        // be silent and deeply confusing.
        auto renderWithCv = [](int channel, float amount)
        {
            MakeSynthProcessor cp;
            auto layout = cp.getBusesLayout();
            layout.inputBuses.getReference(1) = juce::AudioChannelSet::quadraphonic();
            require(cp.setBusesLayout(layout), "Could not enable the CV In bus");
            set(cp, "drone", 1); set(cp, "space", 0); set(cp, "output", 0);
            const char* ids[4] = {"cvCutoffAmount","cvPitchAmount","cvFmAmount","cvWidthAmount"};
            set(cp, ids[channel], amount);
            setup(cp);
            const int base = cp.getChannelIndexInProcessBlockBuffer(true, 1, 0);
            juce::AudioBuffer<float> b(cp.getTotalNumOutputChannels(), 4096);
            juce::AudioBuffer<float> result(2, 4096);
            b.clear();
            for (int i = 0; i < 4096; ++i)
                b.setSample(base + channel, i, 0.6f * static_cast<float>(std::sin(i * 0.02)));
            juce::MidiBuffer midi;
            cp.processBlock(b, midi);
            for (int c = 0; c < 2; ++c) result.copyFrom(c, 0, b, c, 0, 4096);
            return result;
        };

        for (int channel = 0; channel < 4; ++channel)
        {
            const auto live = renderWithCv(channel, 1.0f);
            const auto inert = renderWithCv(channel, 0.0f);
            require(diffRms(live, inert, 0, 4096) > 0.0005,
                    "A CV channel did not reach its destination");
        }
    }
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
```

Expected: FAIL with `A CV channel did not reach its destination` — CV is captured nowhere yet.

- [ ] **Step 3: Add the scratch buffer**

In `Source/PluginProcessor.h`, beside `juce::AudioBuffer<float> patchScratch;`:

```cpp
    juce::AudioBuffer<float> cvScratch;
```

In `prepareToPlay`, beside `patchScratch.setSize(1,32768,false,false,true);`:

```cpp
    cvScratch.setSize(4,32768,false,false,true);
```

Four channels, not summed to mono — each channel is a separate destination.

- [ ] **Step 4: Capture CV before the clear**

In `processBlock`, immediately after the existing Patch In capture block and **before** `buffer.clear()`:

```cpp
    const bool cvActive = getBus(true,1) != nullptr && getBus(true,1)->isEnabled();
    cvScratch.clear(0, 0, patchSamples);
    cvScratch.clear(1, 0, patchSamples);
    cvScratch.clear(2, 0, patchSamples);
    cvScratch.clear(3, 0, patchSamples);
    if (cvActive)
    {
        auto cvBus = getBusBuffer(buffer, true, 1);
        const int channels = std::min(4, cvBus.getNumChannels());
        for (int c = 0; c < channels; ++c)
            cvScratch.copyFrom(c, 0, cvBus, c, 0, patchSamples);
    }
```

- [ ] **Step 5: Pass CV to the engine**

CV is at host rate in `cvScratch` while the engine runs at 4x. Read the nearest host sample for each oversampled sample — CV is a control signal, so nearest-neighbour is adequate and avoids a second oversampler.

Set `parameters.cvConnected = cvActive;` beside the existing `parameters.patchConnected = patchActive;`, then change the engine call inside the 4x loop from:

```cpp
            const float x=engine.process({high.getChannelPointer(0)[i]});
```

to:

```cpp
            const int cvSample = std::min(start + static_cast<int>(i/4), patchSamples - 1);
            makesynth::SampleInputs inputs;
            inputs.patch = high.getChannelPointer(0)[i];
            if (cvActive && cvSample >= 0)
            {
                inputs.cvCutoff  = cvScratch.getSample(0, cvSample);
                inputs.cvPitch   = cvScratch.getSample(1, cvSample);
                inputs.cvFmDepth = cvScratch.getSample(2, cvSample);
                inputs.cvWidth   = cvScratch.getSample(3, cvSample);
            }
            const float x=engine.process(inputs);
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
ctest --test-dir build --output-on-failure
```

Expected: PASS, and `New instrument should be silent` still holds.

- [ ] **Step 7: Prove the aliasing guard**

Temporarily move `buffer.clear()` above the CV capture, rebuild, and confirm the channel test FAILS. Revert and confirm `git status` is clean. Report both outputs.

- [ ] **Step 8: Commit**

```bash
git add Source/PluginProcessor.h Source/PluginProcessor.cpp Tools/Render.cpp
git commit -m "Route CV input channels into the engine"
```

---

### Task 7: Write the CV output bus

**Files:**
- Modify: `Source/PluginProcessor.cpp` (`processBlock`)
- Test: `Tools/Render.cpp`

**Interfaces:**
- Consumes: `lastLfo()` / `lastEnvelope()` from Task 4; bus index 3 from Task 5
- Produces: audio on output bus 3 when enabled

- [ ] **Step 1: Write the failing test**

```cpp
    {
        MakeSynthProcessor cv;
        auto layout = cv.getBusesLayout();
        layout.outputBuses.getReference(3) = juce::AudioChannelSet::stereo();
        require(cv.setBusesLayout(layout), "Could not enable the CV Out bus");
        set(cv, "drone", 1); set(cv, "rate", 5.0f); set(cv, "output", -48);
        setup(cv);
        const int base = cv.getChannelIndexInProcessBlockBuffer(false, 3, 0);
        juce::AudioBuffer<float> b(cv.getTotalNumOutputChannels(), 8192);
        b.clear();
        juce::MidiBuffer midi;
        cv.processBlock(b, midi);
        double lfoEnergy = 0, envEnergy = 0;
        float envLowest = 1.0f;
        for (int i = 0; i < 8192; ++i)
        {
            const auto l = b.getSample(base, i), e = b.getSample(base + 1, i);
            require(std::isfinite(l) && std::isfinite(e), "CV output is non-finite");
            lfoEnergy += l * l; envEnergy += e * e;
            envLowest = std::min(envLowest, e);
        }
        require(lfoEnergy > 0.001, "LFO CV output produced nothing");
        require(envEnergy > 0.001, "Envelope CV output produced nothing");
        require(envLowest >= 0.0f, "Envelope CV output went negative");
        // Master level is -48dB; CV must ignore it entirely.
        require(envEnergy > 100.0, "Envelope CV was scaled by the master level");
    }
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
```

Expected: FAIL with `LFO CV output produced nothing`.

- [ ] **Step 3: Accumulate and write the CV outputs**

Beside the existing tap accumulators in the sub-block scope:

```cpp
        const bool wantCvOut = getBus(false,3) != nullptr && getBus(false,3)->isEnabled();
        const int cvOutChannel = wantCvOut ? getChannelIndexInProcessBlockBuffer(false,3,0) : -1;
        float lfoSum = 0, envSum = 0;
```

Inside the 4x loop, beside the existing `preSum`/`postSum` accumulation:

```cpp
            lfoSum += engine.lastLfo();
            envSum += engine.lastEnvelope();
```

And inside the `if ((i&3u)==3u)` decimation block, beside the existing tap writes:

```cpp
                if (cvOutChannel >= 0 && cvOutChannel + 1 < buffer.getNumChannels())
                {
                    buffer.setSample(cvOutChannel,     hostSample, lfoSum * 0.25f);
                    buffer.setSample(cvOutChannel + 1, hostSample, envSum * 0.25f);
                }
                lfoSum = envSum = 0;
```

- [ ] **Step 4: Confirm CV Out bypasses the master stage**

The existing reverb, wet/dry mix, master gain and limiter loop operates on channels 0 and 1 only. Read it and confirm CV Out is untouched — no change should be needed. If a future edit widens that loop, CV would be scaled by the volume knob, which the test in Step 1 now catches.

- [ ] **Step 5: Run the tests**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add Source/PluginProcessor.cpp Tools/Render.cpp
git commit -m "Write the LFO and envelope to the CV output bus"
```

---

### Task 8: Expose the attenuverters as host parameters

**Files:**
- Modify: `Source/PluginProcessor.h` (`ids`, array sizes), `Source/PluginProcessor.cpp` (`layout`, `readParameters`, `handleMidi`)
- Test: `Tools/Render.cpp`

**Interfaces:**
- Consumes: `Parameters::cvCutoffAmount` etc. from Task 2
- Produces: parameter ids `cvCutoffAmount` (17), `cvPitchAmount` (18), `cvFmAmount` (19), `cvWidthAmount` (20) on CCs 86-89

- [ ] **Step 1: Write the failing test**

```cpp
    {
        MakeSynthProcessor cc;
        setup(cc);
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::controllerEvent(1, 86, 127), 10);
        events.addEvent(juce::MidiMessage::controllerEvent(1, 87, 0), 20);
        events.addEvent(juce::MidiMessage::controllerEvent(1, 88, 64), 30);
        events.addEvent(juce::MidiMessage::controllerEvent(1, 89, 127), 40);
        render(cc, 512, 512, events);
        require(cc.state.getRawParameterValue("cvCutoffAmount")->load() > 0.99f,
                "MIDI CC 86 must control cutoff CV amount");
        require(cc.state.getRawParameterValue("cvPitchAmount")->load() < -0.99f,
                "MIDI CC 87 must control pitch CV amount");
        require(std::abs(cc.state.getRawParameterValue("cvFmAmount")->load()) < 0.02f,
                "MIDI CC 88 must centre FM CV amount");
        require(cc.state.getRawParameterValue("cvWidthAmount")->load() > 0.99f,
                "MIDI CC 89 must control width CV amount");
        // Existing mappings must not have drifted.
        require(std::abs(cc.state.getRawParameterValue("patchLevel")->load() - 0.5f) < 0.01f,
                "patchLevel drifted from its default");
    }
```

- [ ] **Step 2: Run to verify it fails**

Expected: runtime failure — `getRawParameterValue("cvCutoffAmount")` returns null and the test dereferences it (SIGSEGV), or `Unknown parameter` from `set`.

- [ ] **Step 3: Extend the id table**

In `Source/PluginProcessor.h`, change the `ids` array and both array sizes from 17 to 21:

```cpp
    static constexpr std::array<const char*,21> ids {
        "mode","drone","frequency","cutoff","resonance","rate","motion",
        "detune","fmRatio","fmDepth","breath","noise","space","output",
        "wave","width","patchLevel",
        "cvCutoffAmount","cvPitchAmount","cvFmAmount","cvWidthAmount"};
```

```cpp
    std::array<std::atomic<float>*,21> values {};
    std::array<juce::RangedAudioParameter*,21> params {};
```

- [ ] **Step 4: Declare the parameters last**

In `layout()`, after `add("patchLevel","Patch level",0,1,0.5f);`:

```cpp
    add("cvCutoffAmount","CV cutoff amount",-1,1,0);
    add("cvPitchAmount","CV pitch amount",-1,1,0);
    add("cvFmAmount","CV FM amount",-1,1,0);
    add("cvWidthAmount","CV width amount",-1,1,0);
```

- [ ] **Step 5: Read them**

In `readParameters()`, after the `p.patchLevel=...` line:

```cpp
    p.cvCutoffAmount=std::clamp(value(17),-1.0f,1.0f);
    p.cvPitchAmount=std::clamp(value(18),-1.0f,1.0f);
    p.cvFmAmount=std::clamp(value(19),-1.0f,1.0f);
    p.cvWidthAmount=std::clamp(value(20),-1.0f,1.0f);
```

- [ ] **Step 6: Map the CCs**

In the `switch (cc)` block in `handleMidi`, after `case 85:`:

```cpp
                case 86: setParameterFromMidi(17, v); break;               // CV cutoff amount
                case 87: setParameterFromMidi(18, v); break;               // CV pitch amount
                case 88: setParameterFromMidi(19, v); break;               // CV FM amount
                case 89: setParameterFromMidi(20, v); break;               // CV width amount
```

- [ ] **Step 7: Run the tests**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
ctest --test-dir build --output-on-failure
```

Expected: PASS, including the existing CC 74/71/1 assertions and the state roundtrip. If any pre-existing CC assertion fails, a parameter was inserted rather than appended — fix the ordering, never the test.

- [ ] **Step 8: Commit**

```bash
git add Source/PluginProcessor.h Source/PluginProcessor.cpp Tools/Render.cpp
git commit -m "Expose the CV attenuverters on CCs 86-89"
```

---

### Task 9: Editor attenuverter knobs

**Files:**
- Modify: `Source/PluginEditor.h`, `Source/PluginEditor.cpp`

**Interfaces:**
- Consumes: parameter ids from Task 8; bus index 1 from Task 5
- Produces: no interface other tasks rely on

- [ ] **Step 1: Add the knob members**

In `Source/PluginEditor.h`, extend the knob list — declaration order must match the constructor's initialiser order:

```cpp
    Knob pitch,cutoff,resonance,rate,motion,detune,fmRatio,fmDepth,breath,width,patchLevel,
         cvCutoffAmount,cvPitchAmount,cvFmAmount,cvWidthAmount,space,output;
```

And beside `bool patchVisible=false;`:

```cpp
    bool cvVisible=false;
```

- [ ] **Step 2: Construct the knobs**

In the constructor initialiser list, between the `patchLevel(...)` and `space(...)` entries:

```cpp
      cvCutoffAmount(p.state,"cvCutoffAmount","CV CUTOFF","","How much CV channel 1 moves the filter. Negative values invert it."),
      cvPitchAmount(p.state,"cvPitchAmount","CV PITCH","","How much CV channel 2 moves the pitch. Negative values invert it."),
      cvFmAmount(p.state,"cvFmAmount","CV FM","","How much CV channel 3 moves the FM depth. Negative values invert it."),
      cvWidthAmount(p.state,"cvWidthAmount","CV WIDTH","","How much CV channel 4 moves the pulse width. Negative values invert it."),
```

Add all four to the `addAndMakeVisible` loop:

```cpp
    for (auto* k : {&pitch,&cutoff,&resonance,&rate,&motion,&detune,&fmRatio,&fmDepth,&breath,&width,&patchLevel,&cvCutoffAmount,&cvPitchAmount,&cvFmAmount,&cvWidthAmount,&space,&output}) addAndMakeVisible(k);
```

- [ ] **Step 3: Track CV bus visibility**

In `updateMode()`, extend the state capture and guard. Replace:

```cpp
    const auto* patchBus=processor.getBus(true,0);
    const bool patched=patchBus!=nullptr && patchBus->isEnabled();
    if (m==selectedMode && w==selectedWave && patched==patchVisible) return;
    const bool wasPatched=patchVisible;
    patchVisible=patched;
    patchLevel.setVisible(patched);
    if (patched!=wasPatched) setSize(getWidth(), patched ? 820 : 690);
```

with:

```cpp
    const auto* patchBus=processor.getBus(true,0);
    const auto* cvBus=processor.getBus(true,1);
    const bool patched=patchBus!=nullptr && patchBus->isEnabled();
    const bool cv=cvBus!=nullptr && cvBus->isEnabled();
    if (m==selectedMode && w==selectedWave && patched==patchVisible && cv==cvVisible) return;
    const bool hadRow=patchVisible || cvVisible;
    patchVisible=patched; cvVisible=cv;
    patchLevel.setVisible(patched);
    for (auto* k : {&cvCutoffAmount,&cvPitchAmount,&cvFmAmount,&cvWidthAmount}) k->setVisible(cv);
    const bool wantRow=patched || cv;
    if (wantRow!=hadRow) setSize(getWidth(), wantRow ? 820 : 690);
```

- [ ] **Step 4: Place the knobs**

In `resized()`, change the `knobH` branch to use the shared row flag and add the four positions. Replace:

```cpp
    const int cell=(w-2*margin)/5,knobH=patchVisible?(h-322)/3:(h-315)/2;
```

with:

```cpp
    const bool thirdRow=patchVisible||cvVisible;
    const int cell=(w-2*margin)/5,knobH=thirdRow?(h-322)/3:(h-315)/2;
```

And after the existing `patchLevel.setBounds(margin,patchRow,cell,knobH);`:

```cpp
    cvCutoffAmount.setBounds(margin+cell,patchRow,cell,knobH);
    cvPitchAmount.setBounds(margin+2*cell,patchRow,cell,knobH);
    cvFmAmount.setBounds(margin+3*cell,patchRow,cell,knobH);
    cvWidthAmount.setBounds(margin+4*cell,patchRow,cell,knobH);
```

Also update the `paint()` reference to `patchVisible` that positions the row-2 caption so it uses the same `thirdRow` condition — search `paint()` for `patchVisible` and replace with `(patchVisible||cvVisible)`.

- [ ] **Step 5: Build and check the layout visually**

```bash
cmake --build build --config Release --parallel 4
ctest --test-dir build --output-on-failure
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --snapshot /tmp/task9-shots
```

The snapshot tool enables no optional bus, so `/tmp/task9-shots/mode-1.png` must be byte-identical to `docs/mode-1.png`. Verify with `sha256sum`.

Then verify the CV row renders correctly: temporarily patch `snapshot()` in `Tools/Render.cpp` to enable input bus 1 before `createEditor()`, rebuild, render to a scratch directory, and **look at the image** — confirm all four attenuverters are visible, inside the window, and not overlapping the footer or each other. Revert `Tools/Render.cpp` with `git checkout --` and confirm `git status` is clean. Report what you saw.

- [ ] **Step 6: Commit**

```bash
git add Source/PluginEditor.h Source/PluginEditor.cpp
git commit -m "Add CV attenuverter knobs to the patch row"
```

---

### Task 10: Documentation and validation

**Files:**
- Modify: `README.md`, `QUICKSTART.md`

**Interfaces:**
- Consumes: everything above
- Produces: nothing code depends on

- [ ] **Step 1: Document the CV buses in README**

Extend the bus table in the "Patching Instances Together" section:

```markdown
| **CV In** | Input (4 channels) | Control signals: channel 1 filter cutoff, 2 pitch, 3 FM depth, 4 pulse width. Each is scaled by its **CV** knob, which is bipolar — negative values invert the signal. |
| **CV Out** | Output (stereo) | Left carries the LFO (−1…+1), right the amplitude envelope (0…1). |
```

Add below it:

```markdown
CV is an offset, not a replacement: the knob sets the base value and CV moves it
from there. Cutoff and pitch respond exponentially (±4 and ±2 octaves at full
attenuverter); FM depth and pulse width respond linearly. All four **CV** knobs
default to zero, so connecting the bus changes nothing until you raise one.

Like the filter taps, **CV Out is never silent** — the LFO runs regardless of
whether a note is held, which is what makes it useful as a modulation source.
It also ignores the output level and limiter entirely.
```

Add the CC rows to the MIDI table:

```markdown
| **CC 86** | CV cutoff amount |
| **CC 87** | CV pitch amount |
| **CC 88** | CV FM amount |
| **CC 89** | CV width amount |
```

- [ ] **Step 2: Document in QUICKSTART**

Under "First Sound & MIDI Controller Controls":

```markdown
- To modulate one instance from another, enable the **CV In** bus and route a signal
  to channel 1 (cutoff), 2 (pitch), 3 (FM depth) or 4 (pulse width), then raise the
  matching **CV** knob. Enable **CV Out** to use this instance's LFO and envelope as
  a modulation source elsewhere.
```

And in the CC list:

```markdown
  - **CC 86-89**: CV amount for cutoff, pitch, FM depth and pulse width.
```

Do **not** write that any of this was verified in a specific host — no manual host testing is performed by this plan.

- [ ] **Step 3: Run pluginval**

```bash
python3 Tools/fetch_pluginval.py build/pluginval
build/pluginval/pluginval --strictness-level 5 --validate "build/MakeSynth_artefacts/Release/VST3/Make Synth.vst3"
```

A 4-channel bus is unusual, so this is the main automated check that hosts can negotiate the new layout. If it fails, report the failure verbatim rather than adjusting the layout until it passes — a validator failure here most likely means the CV bus shape is wrong, which is exactly what needs to surface.

- [ ] **Step 4: Measure the CPU cost of live cutoff CV**

The adaptive branch trades CPU for fidelity, and the spec asks for this to be measured rather than assumed. Time the existing render path with cutoff CV inert versus live:

```bash
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test 2>&1 | grep -i "seconds\|core"
```

The processor test already prints a render-time percentage. Report the figure with CV inert, then report whether it is materially different from the pre-Phase-2 baseline of roughly 8% of one core. If live cutoff CV pushes a realistic patch above ~25% of one core, say so — that is a finding, not a detail.

- [ ] **Step 5: Run the full suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 3/3.

- [ ] **Step 6: Commit**

```bash
git add README.md QUICKSTART.md
git commit -m "Document CV modulation and the CV buses"
```

---

## Done when

- `ctest --test-dir build --output-on-failure` passes 3/3.
- The golden guard still matches, proving no existing patch changed.
- The fast-versus-slow cutoff CV test passes, proving CV is not being smoothed.
- Each of the four CV channels is shown to reach its own destination.
- pluginval at strictness 5 passes with the 4-channel CV bus declared.
- With no optional bus enabled, the plugin is indistinguishable from Phase 1.
