# Modular Patching Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a Make Synth instance receive another instance's audio, run it through its own filter/drive/reverb character, and expose pre-filter and post-filter stages as separate outputs.

**Architecture:** Four discrete audio buses (main out enabled; Patch In, Pre-Filter, Post-Filter disabled by default). External audio is summed into the engine before the mode filter, so it takes on each mode's character. The existing 4x oversampler is reused to upsample the input — no second filter chain. A connected Patch In bus holds the amplitude envelope open so patched audio passes with no note held.

**Tech Stack:** C++17, JUCE 8 (`juce_audio_processors`, `juce_dsp`), CMake + CTest, hand-rolled `require()` assertions (no test framework), pluginval for host validation.

**Spec:** `docs/superpowers/specs/2026-09-18-modular-patching-phase-1-design.md`

## Global Constraints

- **Never insert a parameter — only append.** `readParameters()` and every `case` in `handleMidi()` in `Source/PluginProcessor.cpp` address parameters by numeric index into `ids`. Inserting shifts every later index and breaks MIDI CC mapping with no compile error.
- **Declare new parameters last in `layout()`** so existing host automation indices do not move.
- **`engine.process(0.0f)` must be sample-identical to the pre-change `engine.process()`.** Task 1 builds the guard that enforces this; every later task must keep it green.
- **No allocation, locks, or I/O on the audio thread.** All buffers are sized in `prepareToPlay`.
- **Every new bus is disabled by default.** A host that ignores them must see exactly today's plugin.
- **The web engine (`web/synth-engine.js`) is untouched in Phase 1.** There is no host routing in a browser.
- **C++17.** No C++20 constructs.

## Build and test commands

```bash
# Engine tests only (fast: header-only + one .cpp) — use this while iterating
cmake --build build --target MakeSynthDspTests --parallel 4 && ./build/MakeSynthDspTests

# Processor tests (needs the full plugin build, slower)
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test

# Everything
ctest --test-dir build --output-on-failure
```

---

### Task 1: Golden-reference regression guard

Builds the safety net before anything changes. Captures the current engine output to a committed binary file, then adds a test that compares against it. Every later task must keep this green.

**Files:**
- Modify: `Tests/DspTests.cpp` (add golden write mode and comparison test)
- Create: `Tests/golden-engine.f32` (binary, committed)

**Interfaces:**
- Consumes: nothing
- Produces: `Tests/golden-engine.f32` — 8192 little-endian `float` samples; the comparison test that later tasks must not break

- [ ] **Step 1: Add a golden-writing mode to the test binary**

At the top of `Tests/DspTests.cpp`, change `int main()` to `int main(int argc, char** argv)` and add this helper above it:

```cpp
#include <fstream>

// Fixed scenario used as the bit-identity reference across the patching work.
static std::vector<float> goldenRender()
{
    makesynth::SynthEngine engine;
    makesynth::Parameters p;
    p.mode = 0; p.drone = true; p.frequency = 220; p.detune = 7;
    p.cutoff = 3000; p.resonance = 0.4f; p.motion = 0.3f; p.rate = 1.0f;
    engine.setParameters(p);
    engine.prepare(48000);
    std::vector<float> out(8192);
    for (auto& x : out) x = engine.process();
    return out;
}
```

- [ ] **Step 2: Wire the write mode into main**

As the first statement inside `main`'s `try` block in `Tests/DspTests.cpp`:

```cpp
if (argc == 2 && std::string(argv[1]) == "--write-golden")
{
    const auto samples = goldenRender();
    std::ofstream file("Tests/golden-engine.f32", std::ios::binary);
    require(file.good(), "Cannot open golden file for writing");
    file.write(reinterpret_cast<const char*>(samples.data()),
               static_cast<std::streamsize>(samples.size() * sizeof(float)));
    std::cout << "wrote " << samples.size() << " golden samples\n";
    return 0;
}
```

Add `#include <string>` and `#include <vector>` to the includes if absent.

- [ ] **Step 3: Build and capture the reference from the UNMODIFIED engine**

Run from the repository root:

```bash
cmake --build build --target MakeSynthDspTests --parallel 4
./build/MakeSynthDspTests --write-golden
```

Expected: `wrote 8192 golden samples`, and `Tests/golden-engine.f32` is 32768 bytes.

This must happen **before** any change to `Source/SynthEngine.h`. If the engine has already been modified, `git stash` the engine change, capture, then restore.

- [ ] **Step 4: Add the comparison test**

In `Tests/DspTests.cpp`, immediately after the existing `engine.prepare(48000);` silence check near the top of `main`:

```cpp
{
    std::ifstream file("Tests/golden-engine.f32", std::ios::binary);
    require(file.good(), "Golden reference file is missing — run --write-golden");
    std::vector<float> expected(8192);
    file.read(reinterpret_cast<char*>(expected.data()),
              static_cast<std::streamsize>(expected.size() * sizeof(float)));
    require(file.gcount() == static_cast<std::streamsize>(expected.size() * sizeof(float)),
            "Golden reference file is truncated");
    const auto actual = goldenRender();
    for (size_t i = 0; i < expected.size(); ++i)
        require(actual[i] == expected[i], "Engine output drifted from the golden reference");
    std::cout << "golden reference matches\n";
}
```

Note: the test reads a path relative to the working directory. CTest runs from the build directory, so also update the CTest registration in Step 5.

- [ ] **Step 5: Make CTest run from the source directory**

In `CMakeLists.txt`, replace the line `add_test(NAME dsp COMMAND MakeSynthDspTests)` with:

```cmake
add_test(NAME dsp COMMAND MakeSynthDspTests)
set_tests_properties(dsp PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
```

- [ ] **Step 6: Verify the guard passes on unmodified code**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4
ctest --test-dir build -R dsp --output-on-failure
```

Expected: PASS, with `golden reference matches` in the output.

- [ ] **Step 7: Verify the guard actually catches drift**

Temporarily change `0.85` to `0.86` in the `std::tanh(mixed * 0.85)` line of `Source/SynthEngine.h`, rebuild, and run the test.

Expected: FAIL with `Engine output drifted from the golden reference`. **Revert the change** and re-run to confirm PASS. A guard that never fails is not a guard.

- [ ] **Step 8: Commit**

```bash
git add Tests/DspTests.cpp Tests/golden-engine.f32 CMakeLists.txt
git commit -m "Add golden-reference guard for engine output"
```

---

### Task 2: Engine accepts patch input and exposes taps

**Files:**
- Modify: `Source/SynthEngine.h`
- Test: `Tests/DspTests.cpp`

**Interfaces:**
- Consumes: the golden guard from Task 1
- Produces:
  - `float SynthEngine::process(float patchIn = 0.0f) noexcept`
  - `float SynthEngine::lastPreFilter() const noexcept`
  - `float SynthEngine::lastPostFilter() const noexcept`

- [ ] **Step 1: Write the failing tests**

Add to `Tests/DspTests.cpp`, after the golden comparison block:

```cpp
{
    makesynth::SynthEngine e;
    makesynth::Parameters p;
    p.mode = 0; p.drone = true;
    e.setParameters(p); e.prepare(48000);

    // A non-zero patch signal must change the output.
    double silentEnergy = 0, drivenEnergy = 0;
    for (int i = 0; i < 4800; ++i) { const auto x = e.process(0.0f); silentEnergy += x * x; }
    e.reset();
    for (int i = 0; i < 4800; ++i)
    {
        const auto x = e.process(0.5f * std::sin(i * 0.05));
        drivenEnergy += x * x;
        require(std::isfinite(x), "Patch input produced a non-finite sample");
    }
    require(drivenEnergy != silentEnergy, "Patch input had no effect on the output");

    // Taps must be finite and must differ from each other once the filter bites.
    p.cutoff = 200; p.resonance = 0;
    e.setParameters(p); e.prepare(48000);
    double tapDifference = 0;
    for (int i = 0; i < 4800; ++i)
    {
        e.process(0.5f * std::sin(i * 0.3));
        require(std::isfinite(e.lastPreFilter()), "Pre-filter tap is non-finite");
        require(std::isfinite(e.lastPostFilter()), "Post-filter tap is non-finite");
        tapDifference += std::abs(e.lastPreFilter() - e.lastPostFilter());
    }
    require(tapDifference > 1.0, "Taps are identical; the filter is not in the tap path");

    // Extremes must stay bounded through the tanh stage.
    for (int i = 0; i < 4800; ++i)
    {
        const auto x = e.process(i % 2 ? 50.0f : -50.0f);
        require(std::isfinite(x) && std::abs(x) < 2.0f, "Extreme patch input is unstable");
    }
    std::cout << "patch input checks passed\n";
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4
```

Expected: COMPILE FAILURE — `too many arguments to function call` on `e.process(0.0f)`, and `no member named 'lastPreFilter'`.

- [ ] **Step 3: Add the tap members**

In the private data section of `SynthEngine` in `Source/SynthEngine.h`, beside `float smoothing = 0.001f, ...`:

```cpp
    float preFilterTap = 0, postFilterTap = 0;
```

And in `reset()`, beside `dcInput = dcOutput = 0;`:

```cpp
        preFilterTap = postFilterTap = 0;
```

- [ ] **Step 4: Add the public accessors**

In the public section of `SynthEngine`, directly after `setNote`:

```cpp
    // Read after each process() call. Valid until the next call.
    float lastPreFilter()  const noexcept { return preFilterTap; }
    float lastPostFilter() const noexcept { return postFilterTap; }
```

- [ ] **Step 5: Change the process signature and sum the input**

Change `float process() noexcept` to:

```cpp
    float process(float patchIn = 0.0f) noexcept
```

Then replace the `mixed` block (currently `const float mixed = weights[0] * filters[0].process(drone) ...`) with:

```cpp
        // Patched audio joins each mode source, so it picks up whichever
        // filter the active mode uses. The weights sum to ~1, so its total
        // contribution stays at unity across mode changes.
        preFilterTap = weights[0] * drone + weights[1] * noise + weights[2] * metallic + patchIn;
        const float mixed = weights[0] * filters[0].process(drone + patchIn)
                          + weights[1] * filters[1].process(noise + patchIn, true) * swell
                          + weights[2] * filters[2].process(metallic + patchIn);
        postFilterTap = mixed;
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4 && ctest --test-dir build -R dsp --output-on-failure
```

Expected: PASS, including `golden reference matches`. If the golden check fails, `patchIn` is leaking into the zero-input path — the default argument must contribute exactly `0.0f`.

- [ ] **Step 7: Verify reset still clears everything**

The existing `Reset must be deterministic` test in `Tests/DspTests.cpp` calls
`engine.process()` with no argument, which still compiles via the default. Add
a check that `reset()` also clears the new tap state — append to the test block
added in Step 1:

```cpp
{
    makesynth::SynthEngine e;
    makesynth::Parameters p;
    p.mode = 0; p.drone = true;
    e.setParameters(p); e.prepare(48000);
    for (int i = 0; i < 1000; ++i) e.process(0.5f);
    require(e.lastPreFilter() != 0 || e.lastPostFilter() != 0, "Taps never became non-zero");
    e.reset();
    require(e.lastPreFilter() == 0 && e.lastPostFilter() == 0, "reset() must clear the taps");
}
```

Run: `cmake --build build --target MakeSynthDspTests --parallel 4 && ctest --test-dir build -R dsp --output-on-failure`
Expected: PASS, including the existing determinism test.

- [ ] **Step 8: Commit**

```bash
git add Source/SynthEngine.h Tests/DspTests.cpp
git commit -m "Let the engine take patched audio and expose filter taps"
```

---

### Task 3: Patch level and gate-holding in the engine

**Files:**
- Modify: `Source/SynthEngine.h`
- Test: `Tests/DspTests.cpp`

**Interfaces:**
- Consumes: `process(float patchIn)` from Task 2
- Produces: `makesynth::Parameters::patchLevel` (float, default 0.5f), `makesynth::Parameters::patchConnected` (bool, default false)

- [ ] **Step 1: Write the failing tests**

Add to `Tests/DspTests.cpp`:

```cpp
{
    makesynth::SynthEngine e;
    makesynth::Parameters p;
    p.mode = 0; p.drone = false; p.patchConnected = true; p.patchLevel = 1.0f;
    e.setParameters(p); e.prepare(48000);
    double energy = 0;
    for (int i = 0; i < 24000; ++i)
    {
        const auto x = e.process(0.4f * std::sin(i * 0.1));
        energy += x * x;
    }
    require(energy > 0.01, "Connected patch input must pass with no note held");

    // patchLevel must scale the contribution.
    auto energyAtLevel = [](float level)
    {
        makesynth::SynthEngine engine;
        makesynth::Parameters q;
        q.mode = 0; q.drone = false; q.patchConnected = true; q.patchLevel = level;
        engine.setParameters(q); engine.prepare(48000);
        double total = 0;
        for (int i = 0; i < 24000; ++i)
        {
            const auto x = engine.process(0.4f * std::sin(i * 0.1));
            total += x * x;
        }
        return total;
    };
    require(energyAtLevel(1.0f) > energyAtLevel(0.25f) * 2.0,
            "patchLevel does not scale the patched signal");
    require(energyAtLevel(0.0f) < 1.0e-6, "patchLevel of zero must mute patched audio");

    // Disconnected patch input must not open the gate.
    makesynth::Parameters q;
    q.mode = 0; q.drone = false; q.patchConnected = false; q.patchLevel = 1.0f;
    makesynth::SynthEngine quiet;
    quiet.setParameters(q); quiet.prepare(48000);
    double closed = 0;
    for (int i = 0; i < 24000; ++i) closed += std::abs(quiet.process(0.4f * std::sin(i * 0.1)));
    require(closed < 1.0e-4, "Disconnected patch input must stay gated");

    // Each mode must colour the patched signal differently: mode 0 and 2 are
    // low-pass, mode 1 is band-pass with the breathing swell on top.
    auto energyInMode = [](int mode)
    {
        makesynth::SynthEngine engine;
        makesynth::Parameters q;
        q.mode = mode; q.drone = false; q.patchConnected = true; q.patchLevel = 1.0f;
        q.cutoff = 400; q.resonance = 0; q.motion = 0; q.breath = 0;
        engine.setParameters(q); engine.prepare(48000);
        double total = 0;
        for (int i = 0; i < 4800; ++i) engine.process(0.0f);      // settle the crossfade
        for (int i = 0; i < 24000; ++i)
        {
            const auto x = engine.process(0.4f * std::sin(i * 0.4));  // well above cutoff
            total += x * x;
        }
        return total;
    };
    const auto lowPass = energyInMode(0), bandPass = energyInMode(1);
    require(lowPass > 0 && bandPass > 0, "Patched audio vanished in a mode");
    require(std::abs(lowPass - bandPass) / std::max(lowPass, bandPass) > 0.1,
            "Modes do not colour patched audio differently");
    std::cout << "patch gate checks passed\n";
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4
```

Expected: COMPILE FAILURE — `no member named 'patchConnected' in 'makesynth::Parameters'`.

- [ ] **Step 3: Add the parameter fields**

In `struct Parameters` in `Source/SynthEngine.h`, after `float width = 0.35f;`:

```cpp
    // How much patched audio joins the source, and whether a Patch In bus is
    // connected at all. patchConnected comes from bus state, not signal
    // detection, and holds the envelope gate open so patched audio passes
    // with no note held.
    float patchLevel = 0.5f;
    bool patchConnected = false;
```

- [ ] **Step 4: Smooth patchLevel and apply it**

In `process()`, beside the existing `smooth(current.width, target.width);`:

```cpp
        smooth(current.patchLevel, target.patchLevel);
```

Then, immediately before the `preFilterTap` line added in Task 2, scale the input:

```cpp
        patchIn *= current.patchLevel;
```

`patchIn` is a by-value parameter, so this is local and safe.

- [ ] **Step 5: Hold the gate open**

Replace the gate line in `process()`:

```cpp
        const float gate = noteActive ? velocity : (target.drone ? 1.0f : 0.0f);
```

with:

```cpp
        const float gate = noteActive ? velocity
                         : (target.drone || target.patchConnected) ? 1.0f
                         : 0.0f;
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build --target MakeSynthDspTests --parallel 4 && ctest --test-dir build -R dsp --output-on-failure
```

Expected: PASS, golden reference still matching. `patchConnected` defaults to `false`, so the golden scenario is unaffected.

- [ ] **Step 7: Commit**

```bash
git add Source/SynthEngine.h Tests/DspTests.cpp
git commit -m "Add patch level and patch-held gating to the engine"
```

---

### Task 4: Declare the buses

**Files:**
- Modify: `Source/PluginProcessor.cpp:38` (constructor `BusesProperties`), `Source/PluginProcessor.cpp:81` (`isBusesLayoutSupported`)
- Test: `Tools/Render.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks
- Produces: bus indices used by Tasks 5 and 6 — input bus 0 = "Patch In", output bus 0 = "Output", output bus 1 = "Pre-Filter", output bus 2 = "Post-Filter"

- [ ] **Step 1: Write the failing test**

In `Tools/Render.cpp`, inside `tests()` immediately after `require(p.getLatencySamples()>0,...)`:

```cpp
    {
        require(p.getBusCount(true) == 1, "Expected exactly one input bus");
        require(p.getBusCount(false) == 3, "Expected three output buses");
        require(p.getBus(true, 0)->getName() == "Patch In", "Input bus 0 must be Patch In");
        require(p.getBus(false, 1)->getName() == "Pre-Filter", "Output bus 1 must be Pre-Filter");
        require(p.getBus(false, 2)->getName() == "Post-Filter", "Output bus 2 must be Post-Filter");
        require(!p.getBus(true, 0)->isEnabledByDefault(), "Patch In must default to disabled");
        require(!p.getBus(false, 1)->isEnabledByDefault(), "Pre-Filter must default to disabled");
        require(!p.getBus(false, 2)->isEnabledByDefault(), "Post-Filter must default to disabled");

        using Set = juce::AudioChannelSet;
        auto layoutOf = [](Set in, Set out0, Set out1, Set out2)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add(in);
            l.outputBuses.add(out0); l.outputBuses.add(out1); l.outputBuses.add(out2);
            return l;
        };
        const auto none = Set::disabled();
        require(p.checkBusesLayoutSupported(layoutOf(none, Set::stereo(), none, none)),
                "Stereo out with everything else off must be supported");
        require(p.checkBusesLayoutSupported(layoutOf(Set::stereo(), Set::stereo(), none, none)),
                "Stereo patch input must be supported");
        require(p.checkBusesLayoutSupported(layoutOf(Set::mono(), Set::stereo(), none, none)),
                "Mono patch input must be supported");
        require(p.checkBusesLayoutSupported(layoutOf(none, Set::stereo(), Set::stereo(), Set::stereo())),
                "Both taps enabled must be supported");
        require(!p.checkBusesLayoutSupported(layoutOf(none, Set::mono(), none, none)),
                "Mono main output must be rejected");
        require(!p.checkBusesLayoutSupported(layoutOf(none, none, none, none)),
                "Disabled main output must be rejected");
        require(!p.checkBusesLayoutSupported(layoutOf(none, Set::stereo(), Set::mono(), none)),
                "Mono tap must be rejected");
    }
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
```

Expected: FAIL with `Expected three output buses`.

- [ ] **Step 3: Declare the buses**

In `Source/PluginProcessor.cpp`, replace the constructor's base-class initialiser:

```cpp
    : AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)),
```

with:

```cpp
    : AudioProcessor(BusesProperties()
          .withOutput("Output",      juce::AudioChannelSet::stereo(), true)
          .withInput ("Patch In",    juce::AudioChannelSet::stereo(), false)
          .withOutput("Pre-Filter",  juce::AudioChannelSet::stereo(), false)
          .withOutput("Post-Filter", juce::AudioChannelSet::stereo(), false)),
```

- [ ] **Step 4: Accept the new layouts**

Replace the body of `isBusesLayoutSupported`:

```cpp
bool MakeSynthProcessor::isBusesLayoutSupported(const BusesLayout& b) const
{
    if (b.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto patch = b.getChannelSet(true, 0);
    if (!patch.isDisabled() && patch != juce::AudioChannelSet::mono()
                            && patch != juce::AudioChannelSet::stereo())
        return false;
    for (int bus = 1; bus <= 2; ++bus)
    {
        const auto tap = b.getChannelSet(false, bus);
        if (!tap.isDisabled() && tap != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}
```

- [ ] **Step 5: Run the tests**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
```

Expected: PASS. The existing `New instrument should be silent` check must still pass — with the optional buses off, `render()`'s 2-channel buffer is still correct.

- [ ] **Step 6: Commit**

```bash
git add Source/PluginProcessor.cpp Tools/Render.cpp
git commit -m "Declare patch input and filter tap buses"
```

---

### Task 5: Route patch input into the engine

The aliasing hazard lives here. Input bus channels and main output channels are the same memory; `buffer.clear()` currently runs first and would destroy the input.

**Files:**
- Modify: `Source/PluginProcessor.h` (add scratch buffer member), `Source/PluginProcessor.cpp` (`prepareToPlay`, `processBlock`)
- Test: `Tools/Render.cpp`

**Interfaces:**
- Consumes: bus indices from Task 4; `process(float)` and `Parameters::patchConnected` from Tasks 2-3
- Produces: `renderWithPatch(MakeSynthProcessor&, int count, float amplitude, double radiansPerSample, int block = 257)` test helper in `Tools/Render.cpp`

- [ ] **Step 1: Widen the test helper**

`Tools/Render.cpp:35` allocates `juce::AudioBuffer<float> b(2,n)`, which is too narrow once buses are enabled. Add this helper directly after the existing `render()` function:

```cpp
// Renders with the Patch In bus enabled, feeding a sine into it.
juce::AudioBuffer<float> renderWithPatch(MakeSynthProcessor& p, int count, float amplitude,
                                         double radiansPerSample, int block = 257)
{
    juce::AudioBuffer<float> result(2, count);
    int phase = 0;
    for (int start = 0; start < count; start += block)
    {
        const auto n = std::min(block, count - start);
        juce::AudioBuffer<float> b(p.getTotalNumOutputChannels(), n);
        b.clear();
        // Patch In is input bus 0, which shares channels 0-1 with the main output.
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                b.setSample(c, i, amplitude * static_cast<float>(std::sin((phase + i) * radiansPerSample)));
        phase += n;
        juce::MidiBuffer midi;
        p.processBlock(b, midi);
        for (int c = 0; c < 2; ++c) result.copyFrom(c, start, b, c, 0, n);
    }
    return result;
}
```

- [ ] **Step 2: Write the failing test**

In `tests()`, after the bus-layout block from Task 4:

```cpp
    {
        MakeSynthProcessor patched;
        auto layout = patched.getBusesLayout();
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        require(patched.setBusesLayout(layout), "Could not enable the Patch In bus");
        set(patched, "space", 0); set(patched, "output", 0); set(patched, "drone", 0);
        // patchLevel is not a host parameter until Task 7; Parameters::patchLevel
        // defaults to 0.5f and readParameters does not touch it yet.
        setup(patched);
        const auto level = rms(renderWithPatch(patched, 24000, 0.5f, 0.05));
        require(level > 0.001, "Patched audio did not survive into the output");
        std::cout << "patch input RMS " << level << '\n';
    }
```

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
```

Expected: FAIL with `Patched audio did not survive into the output`, because `buffer.clear()` destroys the input before it is read.

- [ ] **Step 4: Add the scratch buffer**

In `Source/PluginProcessor.h`, beside `juce::AudioBuffer<float> dry;`:

```cpp
    juce::AudioBuffer<float> patchScratch;
```

In `prepareToPlay`, beside `dry.setSize(2,maximumBlock,false,false,true);`:

```cpp
    patchScratch.setSize(1,maximumBlock,false,false,true);
```

- [ ] **Step 5: Capture the input before clearing**

In `processBlock`, replace the opening lines:

```cpp
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels()<2) return;
    engine.setParameters(readParameters());
```

with:

```cpp
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels()<2) return;
    // Input and output buses alias the same memory, so the patch signal must be
    // copied out before the buffer is cleared.
    const bool patchActive = getBus(true,0) != nullptr && getBus(true,0)->isEnabled();
    const int patchSamples = std::min(buffer.getNumSamples(), patchScratch.getNumSamples());
    patchScratch.clear();
    if (patchActive)
    {
        auto patchBus = getBusBuffer(buffer, true, 0);
        const int channels = patchBus.getNumChannels();
        for (int c = 0; c < channels; ++c)
            patchScratch.addFrom(0, 0, patchBus, c, 0, patchSamples, 1.0f / static_cast<float>(channels));
    }
    buffer.clear();
    auto parameters = readParameters();
    parameters.patchConnected = patchActive;
    engine.setParameters(parameters);
```

- [ ] **Step 6: Feed it through the oversampler**

Inside the sub-block loop in `processBlock`, insert immediately before `auto high=oversampling.processSamplesUp(host);`:

```cpp
        // Seed the block with the patch signal so the existing oversampler
        // upsamples it for us — no second filter chain, no extra latency.
        for (int c=0;c<2;++c)
        {
            auto* channel=buffer.getWritePointer(c,start);
            for (int i=0;i<count;++i)
                channel[i] = start+i < patchSamples ? patchScratch.getSample(0,start+i) : 0.0f;
        }
```

Then change the engine call inside the 4x loop from:

```cpp
            const float x=engine.process();
```

to:

```cpp
            const float x=engine.process(high.getChannelPointer(0)[i]);
```

- [ ] **Step 7: Run the tests**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
ctest --test-dir build --output-on-failure
```

Expected: PASS. `New instrument should be silent` must still hold — with Patch In disabled, `patchScratch` is cleared and contributes nothing.

- [ ] **Step 8: Commit**

```bash
git add Source/PluginProcessor.h Source/PluginProcessor.cpp Tools/Render.cpp
git commit -m "Route patch input through the oversampler into the engine"
```

---

### Task 6: Write the filter tap outputs

**Files:**
- Modify: `Source/PluginProcessor.cpp` (`processBlock`)
- Test: `Tools/Render.cpp`

**Interfaces:**
- Consumes: `lastPreFilter()` / `lastPostFilter()` from Task 2; `renderWithPatch` from Task 5
- Produces: audio on output buses 1 and 2 when enabled

- [ ] **Step 1: Write the failing test**

In `tests()`:

```cpp
    {
        MakeSynthProcessor tapped;
        auto layout = tapped.getBusesLayout();
        layout.outputBuses.getReference(1) = juce::AudioChannelSet::stereo();
        layout.outputBuses.getReference(2) = juce::AudioChannelSet::stereo();
        require(tapped.setBusesLayout(layout), "Could not enable the tap buses");
        set(tapped, "drone", 1); set(tapped, "output", 0); set(tapped, "space", 0);
        setup(tapped);

        juce::AudioBuffer<float> b(tapped.getTotalNumOutputChannels(), 4096);
        b.clear();
        juce::MidiBuffer midi;
        tapped.processBlock(b, midi);

        double pre = 0, post = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto a = b.getSample(2, i), c = b.getSample(4, i);
            require(std::isfinite(a) && std::isfinite(c), "Tap output is non-finite");
            pre += a * a; post += c * c;
        }
        require(pre > 0.0001, "Pre-filter tap produced no audio");
        require(post > 0.0001, "Post-filter tap produced no audio");
        std::cout << "taps pre " << std::sqrt(pre / 4096) << " post " << std::sqrt(post / 4096) << '\n';

        // With the tap buses off, nothing beyond the main pair may be written.
        MakeSynthProcessor plain;
        set(plain, "drone", 1); set(plain, "output", 0);
        setup(plain);
        require(plain.getTotalNumOutputChannels() == 2,
                "Disabled taps must not add output channels");
        juce::AudioBuffer<float> narrow(2, 4096);
        narrow.clear();
        juce::MidiBuffer none;
        plain.processBlock(narrow, none);
        require(rms(narrow) > 0.0001, "Main output went silent when taps were disabled");
    }
```

Channel indices: main output occupies 0-1, Pre-Filter 2-3, Post-Filter 4-5.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
```

Expected: FAIL with `Pre-filter tap produced no audio`.

- [ ] **Step 3: Accumulate taps in the 4x loop**

In `processBlock`, declare accumulators immediately before the `for (size_t i=0;i<high.getNumSamples();++i)` loop:

```cpp
        const bool wantPre  = getBus(false,1) != nullptr && getBus(false,1)->isEnabled();
        const bool wantPost = getBus(false,2) != nullptr && getBus(false,2)->isEnabled();
        float preSum = 0, postSum = 0;
```

Inside that loop, immediately after `const float x=engine.process(...)`:

```cpp
            preSum  += engine.lastPreFilter();
            postSum += engine.lastPostFilter();
            if ((i&3u)==3u)
            {
                // Box-average of the four oversampled values: a cheap, stateless
                // decimation that suppresses the worst aliasing. A halfband
                // decimator would be cleaner but costs two more filter chains.
                // The channel guards matter: a host may enable one tap and not
                // the other, so the buffer can be narrower than 6 channels.
                const int host = start + static_cast<int>(i/4);
                const int channels = buffer.getNumChannels();
                if (wantPre  && channels > 3) for (int c=2;c<4;++c) buffer.setSample(c,host,preSum*0.25f);
                if (wantPost && channels > 5) for (int c=4;c<6;++c) buffer.setSample(c,host,postSum*0.25f);
                preSum = postSum = 0;
            }
```

- [ ] **Step 4: Keep taps out of the main mix**

The existing reverb, wet/dry and limiter loop operates on `buffer.getWritePointer(c,start)` for `c` in `{0,1}` only, so taps are already excluded. Confirm by reading the loop — no change needed. If a later edit widens that loop, taps would be reverberated, which is wrong.

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
git commit -m "Write pre-filter and post-filter tap outputs"
```

---

### Task 7: Expose patchLevel as a parameter with MIDI CC

**Files:**
- Modify: `Source/PluginProcessor.h` (`ids`, array sizes), `Source/PluginProcessor.cpp` (`layout`, `readParameters`, `handleMidi`)
- Test: `Tools/Render.cpp`

**Interfaces:**
- Consumes: `Parameters::patchLevel` from Task 3
- Produces: parameter id `"patchLevel"` at index 16, MIDI CC 85

- [ ] **Step 1: Write the failing test**

In `tests()`:

```cpp
    {
        MakeSynthProcessor cc;
        setup(cc);
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::controllerEvent(1, 85, 100), 10);
        render(cc, 512, 512, events);
        require(std::abs(cc.state.getRawParameterValue("patchLevel")->load() - (100.0f/127.0f)) < 0.01f,
                "MIDI CC 85 must control patch level");
    }
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Release --parallel 4
```

Expected: the build succeeds but the test FAILS at runtime with `Unknown parameter` from `set`, or a null dereference on `getRawParameterValue("patchLevel")`.

- [ ] **Step 3: Extend the id table**

In `Source/PluginProcessor.h`, change the `ids` array and both array sizes from 16 to 17:

```cpp
    static constexpr std::array<const char*,17> ids {
        "mode","drone","frequency","cutoff","resonance","rate","motion",
        "detune","fmRatio","fmDepth","breath","noise","space","output",
        "wave","width","patchLevel"};
```

```cpp
    std::array<std::atomic<float>*,17> values {};
    std::array<juce::RangedAudioParameter*,17> params {};
```

- [ ] **Step 4: Declare the parameter last**

In `layout()` in `Source/PluginProcessor.cpp`, after `add("width","Pulse width",0.15f,0.85f,0.35f);`:

```cpp
    add("patchLevel","Patch level",0,1,0.5f);
```

- [ ] **Step 5: Read it**

In `readParameters()`, after the `p.width=...` line:

```cpp
    p.patchLevel=std::clamp(value(16,0.5f),0.0f,1.0f);
```

- [ ] **Step 6: Map the CC**

In the `switch (cc)` block in `handleMidi`, after `case 79:`:

```cpp
                case 85: setParameterFromMidi(16, v); break;               // Patch level
```

- [ ] **Step 7: Run the tests**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --test
ctest --test-dir build --output-on-failure
```

Expected: PASS, including the existing state-roundtrip test.

- [ ] **Step 8: Commit**

```bash
git add Source/PluginProcessor.h Source/PluginProcessor.cpp Tools/Render.cpp
git commit -m "Expose patch level as a parameter on CC 85"
```

---

### Task 8: Editor patch row

**Files:**
- Modify: `Source/PluginEditor.h`, `Source/PluginEditor.cpp`

**Interfaces:**
- Consumes: parameter id `"patchLevel"` from Task 7
- Produces: no interface other tasks rely on

- [ ] **Step 1: Add the knob member**

In `Source/PluginEditor.h`, extend the knob list (declaration order must match the constructor's initialiser order):

```cpp
    Knob pitch,cutoff,resonance,rate,motion,detune,fmRatio,fmDepth,breath,width,patchLevel,space,output;
```

And add a cached flag beside `int selectedMode=-1,selectedWave=-1;`:

```cpp
    bool patchVisible=false;
```

- [ ] **Step 2: Construct the knob**

In the constructor initialiser list in `Source/PluginEditor.cpp`, between the `width(...)` and `space(...)` entries:

```cpp
      patchLevel(p.state,"patchLevel","PATCH LEVEL","","How much patched-in audio joins the oscillator before the filter."),
```

Add `&patchLevel` to the `addAndMakeVisible` loop:

```cpp
    for (auto* k : {&pitch,&cutoff,&resonance,&rate,&motion,&detune,&fmRatio,&fmDepth,&breath,&width,&patchLevel,&space,&output}) addAndMakeVisible(k);
```

- [ ] **Step 3: Show the row only when patching is active**

In `updateMode()` in `Source/PluginEditor.cpp`, replace the early return:

```cpp
    if (m==selectedMode && w==selectedWave) return;
```

with:

```cpp
    const auto* patchBus=processor.getBus(true,0);
    const bool patched=patchBus!=nullptr && patchBus->isEnabled();
    if (m==selectedMode && w==selectedWave && patched==patchVisible) return;
    patchVisible=patched;
    patchLevel.setVisible(patched);
```

- [ ] **Step 4: Grow the window and place the knob**

In `resized()`, after the existing `output.setBounds(...)` line:

```cpp
    const int patchRow=y+knobH+7;
    patchLevel.setBounds(margin,patchRow,cell,knobH);
```

In the constructor, widen the resize limits so the third row fits:

```cpp
    setResizable(true,true); setResizeLimits(900,620,1440,1130); setSize(1000,690);
```

In `updateMode()`, after `patchLevel.setVisible(patched);`:

```cpp
    setSize(getWidth(), patched ? 820 : 690);
```

- [ ] **Step 5: Show PATCH IN in the signal chain**

In `paint()`, replace the mode-0 route array line:

```cpp
    const juce::StringArray routes=selectedMode==0?juce::StringArray{oscillators,"LOW-PASS","HELD VOICE","SPACE"}:
```

with a version that prepends the patch box when active:

```cpp
    juce::StringArray routes=selectedMode==0?juce::StringArray{oscillators,"LOW-PASS","HELD VOICE","SPACE"}:
```

and immediately after the full `routes` assignment completes, insert:

```cpp
    if (patchVisible) { routes.remove(3); routes.insert(0,"PATCH IN"); }
```

This keeps the box count at four so the existing `cw=(getWidth()-104)/4` layout maths is unchanged.

- [ ] **Step 6: Build and check visually**

```bash
cmake --build build --config Release --parallel 4
./build/MakeSynthRender_artefacts/Release/MakeSynthRender --snapshot /tmp/shots
```

Expected: three PNGs written with no crash. Open `/tmp/shots/mode-1.png` and confirm the layout is unchanged from today (the snapshot tool does not enable the Patch In bus, so the third row must be absent).

- [ ] **Step 7: Run the full suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 3/3 pass. The existing editor test in `Render.cpp` walks every slider checking for duplicated unit suffixes; `PATCH LEVEL` uses an empty suffix so it passes.

- [ ] **Step 8: Commit**

```bash
git add Source/PluginEditor.h Source/PluginEditor.cpp
git commit -m "Add a patch level row that appears when Patch In is connected"
```

---

### Task 9: Documentation and host verification

**Files:**
- Modify: `README.md`, `QUICKSTART.md`

**Interfaces:**
- Consumes: everything above
- Produces: nothing code depends on

- [ ] **Step 1: Document the buses in README**

In `README.md`, after the MIDI controller mapping table, add:

```markdown
## Patching Instances Together

Make Synth exposes three optional buses beyond its main output. All are
disabled by default; enable them in your host's routing or pin matrix.

| Bus | Direction | Purpose |
| --- | --- | --- |
| **Patch In** | Input (mono or stereo) | Audio from another instance, summed into the oscillator before the filter. It takes on the active mode's filter character, drive and reverb. |
| **Pre-Filter** | Output (stereo) | The summed source before filtering. |
| **Post-Filter** | Output (stereo) | The filter output before the drive stage. |

Connecting Patch In holds the amplitude envelope open, so patched audio passes
with no note held and DRONE off. **Patch Level** sets how much joins the source.

Verified in Bitwig, REAPER and Ardour. Ableton Live and Logic restrict audio
routing into instrument plugins; a dedicated effect build is planned.
```

Add the CC row to the existing mapping table:

```markdown
| **CC 85** | Patch level |
```

- [ ] **Step 2: Document in QUICKSTART**

In `QUICKSTART.md`, under "First Sound & MIDI Controller Controls":

```markdown
- To patch two instances together, enable the **Patch In** bus on the receiving
  instance and route the first instance's output to it. A **PATCH LEVEL** knob
  appears once the bus is connected.
```

And in the CC list:

```markdown
  - **CC 85**: Patch level.
```

- [ ] **Step 3: Verify in a real host**

Load two instances in Bitwig, REAPER or Ardour. Route instance A's output into instance B's Patch In. Confirm:

- B's editor grows to show PATCH LEVEL, and the chain shows PATCH IN.
- A's sound passes through B's filter, and moving B's FILTER knob changes it.
- Switching B's mode changes the character (low-pass, band-pass with swell, low-pass).
- B alone, with nothing patched, behaves exactly as before.

Record the host and version used in the commit message.

- [ ] **Step 4: Run pluginval**

```bash
python3 Tools/fetch_pluginval.py build/pluginval
xvfb-run -a build/pluginval/pluginval --strictness-level 5 --validate "build/MakeSynth_artefacts/Release/VST3/Make Synth.vst3"
```

Expected: `ALL TESTS PASSED`. This exercises bus-layout negotiation hard and is the main automated defence against host incompatibility.

- [ ] **Step 5: Commit**

```bash
git add README.md QUICKSTART.md
git commit -m "Document instance patching and the new buses"
```

---

## Done when

- `ctest --test-dir build --output-on-failure` passes 3/3.
- The golden reference still matches, proving no existing patch changed.
- pluginval at strictness 5 passes.
- Two instances patch together in at least one of Bitwig, REAPER or Ardour.
- With no bus enabled, the plugin is indistinguishable from the previous release.
