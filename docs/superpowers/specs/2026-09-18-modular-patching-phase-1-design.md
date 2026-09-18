# Modular Patching, Phase 1: Audio Patching

**Date:** 2026-09-18
**Status:** Approved design, not yet implemented
**Scope:** Phase 1 of 3

## Goal

Let multiple Make Synth instances be patched together like analog modular
modules. Phase 1 delivers the audio half: an instance can receive another
instance's output, run it through its own filter/drive/reverb character, and
expose intermediate stages as separate outputs.

## Context

Make Synth is a JUCE instrument (VST3, AU, CLAP) with a mono `SynthEngine`
running at 4x the host sample rate. It is declared `IS_SYNTH TRUE` with a
single stereo output bus, and `isBusesLayoutSupported` currently rejects all
audio input:

```cpp
return b.getMainInputChannelSet().isDisabled()
    && b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
```

Nothing can be patched into it today, in any host, by any route. An audio
input bus is the prerequisite for every form of this feature, including the
JACK/Patchance workflow: JUCE's standalone exposes plugin buses as JACK ports,
so no input bus means no input ports to connect.

## Phasing

The full request (audio in, CV in, CV out, audio taps, across both DAW buses
and a JACK standalone, with an effect-variant entity for restrictive hosts) is
too large for one spec. It decomposes into three phases with a real dependency
order:

| Phase | Content | Why this order |
|---|---|---|
| **1 (this spec)** | Input bus, external audio through the filter chain, pre/post-filter output taps | Proves the host-routing story, the biggest unknown; prerequisite for the rest |
| 2 | CV in (cutoff, pitch, FM depth, pulse width), CV out (LFO, envelope) | Needs per-sample parameter modulation — a rework of the engine's smoothing model |
| 3 | Effect-variant plugin entity, JACK standalone, Patchance workflow docs | Pure packaging on proven DSP; lowest risk, benefits from being last |

Each phase gets its own spec, plan, and implementation cycle. Phases 2 and 3
are out of scope here and are described only for context.

## Decisions

Recorded with rationale, because each closed off alternatives:

1. **Both DAW buses and a JACK standalone** are targets. The bus work is done
   once and surfaces twice.
2. **All four signal types** are wanted eventually. Phase 1 covers audio in and
   audio taps; CV is Phase 2.
3. **All hosts supported, via a separate effect-variant entity** in Phase 3.
   Bitwig, REAPER, and Ardour route audio into instruments well; Ableton Live
   and Logic are restrictive and need an entity that declares itself an effect.
   This assumption is unverified and must be confirmed before Phase 3.
4. **External audio enters inside the engine, pre-filter**, so it passes the
   active mode's filter, the tanh drive, the DC blocker, and the reverb.
5. **The envelope keeps its current position**; a connected Patch In bus holds
   the gate open. Chosen over moving the envelope onto the voice, which would
   be modular-correct but would change the sound of already-released patches.

## Architecture

### Buses

Four discrete named buses. Every optional bus is disabled by default, so a host
that ignores them sees exactly today's plugin.

```cpp
BusesProperties()
    .withOutput("Output",      juce::AudioChannelSet::stereo(), true)
    .withInput ("Patch In",    juce::AudioChannelSet::stereo(), false)
    .withOutput("Pre-Filter",  juce::AudioChannelSet::stereo(), false)
    .withOutput("Post-Filter", juce::AudioChannelSet::stereo(), false)
```

`isBusesLayoutSupported` accepts:

- main output: stereo (required)
- Patch In: disabled, mono, or stereo
- Pre-Filter: disabled or stereo
- Post-Filter: disabled or stereo

and rejects everything else.

### Engine interface

The return type stays `float` so the 19 existing `engine.process()` call sites
in `Tests/DspTests.cpp`, and their mirror in `web/synth-engine.js`, compile
unchanged. (`Tools/Render.cpp` drives the processor via `processBlock` and
never calls the engine directly, so it is unaffected by this signature.)

```cpp
float process(float patchIn = 0.0f) noexcept;   // defaulted: existing calls unaffected
float lastPreFilter()  const noexcept;          // summed source, before filtering
float lastPostFilter() const noexcept;          // filter output, before drive
```

`patchIn` is mono, matching the engine's existing mono-to-both-channels design.
The taps are read-after-process accessors on an already-stateful object.

Two fields join `makesynth::Parameters`, reaching the engine through the
existing `setParameters` call and needing no new transport:

```cpp
float patchLevel = 0.5f;      // smoothed per-sample like the other floats
bool  patchConnected = false; // read directly from target, like mode and pink
```

`patchLevel` is smoothed so that automating it does not click. `patchConnected`
is a discrete flag the processor sets from bus state each block.

### Where the signal joins

`patchIn`, scaled by the new `patchLevel` parameter, is summed into each of the
three mode sources before filtering:

```
[ oscillator | noise | FM ] + patchIn --> mode filter --> tanh --> DC block --> *envelope --> reverb --> out
```

Because the three mode weights sum to approximately 1, the patched signal's
total contribution stays at unity across mode changes while picking up each
mode's filter character — including the bandpass and the breathing swell in
mode 1.

### Envelope gating

The gate calculation gains one term:

```cpp
const float gate = noteActive ? velocity
                 : (target.drone || patchConnected) ? 1.0f
                 : 0.0f;
```

`patchConnected` is supplied by the processor from bus state, not from signal
detection. Consequences accepted: patched audio fades in over the 8 ms attack
and out over the 150 ms release, and the envelope now carries two meanings.
In exchange, behaviour with no patch input is bit-identical to today.

## Signal flow in processBlock

Two hazards drive the implementation order.

### 1. Input and output buses alias the same memory

JUCE hands `processBlock` one buffer sized `max(totalIn, totalOut)`, in which
Patch In's channels 0-1 and the main output's channels 0-1 are the same memory.
The current first statement is `buffer.clear()`, which would destroy the patch
input before it is read.

Required order:

1. Read Patch In via `getBusBuffer(buffer, true, 0)` and sum it to mono into a
   pre-allocated scratch buffer.
2. Only then `buffer.clear()`.

The scratch buffer is sized in `prepareToPlay` alongside the existing `dry`
buffer. No allocation on the audio thread.

### 2. The existing oversampler already handles the input

`oversampling.processSamplesUp(host)` upsamples whatever `host` contains.
Today that is silence. Writing the mono patch signal into `host` first makes
the upsampled input available inside the existing loop, with no second
oversampler and no additional latency:

```cpp
auto high = oversampling.processSamplesUp(host);      // carries upsampled patch audio
for (size_t i = 0; i < high.getNumSamples(); ++i) {
    const float patchIn = high.getChannelPointer(0)[i];
    const float x = engine.process(patchIn);
    high.getChannelPointer(0)[i] = x;
    high.getChannelPointer(1)[i] = x;
}
```

### 3. Taps are decimated by box average

`lastPreFilter()` and `lastPostFilter()` produce values at 4x rate; the tap
buses run at host rate. Phase 1 uses a 4-sample box average rather than plain
decimation: cheap, stateless, and it suppresses the worst aliasing.

A proper halfband decimator would be cleaner but costs two more
`juce::dsp::Oversampling` filter chains, with matching reset and latency
bookkeeping. Deferred as a future refinement; revisit in Phase 2, where CV
outputs face the same problem and it can be solved once.

Taps are written only when their bus is enabled.

## Parameters

One new parameter, appended so the index-addressed CC table in
`PluginProcessor.cpp` stays intact:

| ID | Index | Name | Range | Default | MIDI CC |
|---|---|---|---|---|---|
| `patchLevel` | 16 | Patch level | 0.0 - 1.0 | 0.5 | 85 |

Appending rather than inserting is mandatory: `readParameters()` and every case
in `handleMidi()` address parameters by numeric position, and inserting would
shift every later index with no compile error. The parameter is also declared
last in `layout()` so existing host automation indices do not move. CC 85 is
undefined in General MIDI and unused in the current table.

## Editor

The knob grid is 5 columns by 2 rows and is completely full in mode 0
(`detune`, `wave`, `width`, `space`, `output`). Modes 1 and 2 have column 2
free, but mode 0 does not, so there is no cell available in all three modes.

A third row appears only when the Patch In bus is enabled, holding the
PATCH LEVEL knob. The editor grows roughly 130px and the limits at
`PluginEditor.cpp:118` adjust to match. With no patching, the window is
pixel-identical to today.

The signal-chain diagram in `paint()` gains a leading "PATCH IN" box when the
bus is active, so routing is visible rather than implied.

## Testing

### Regression guard

The most important test: **`process(0.0f)` must be sample-identical to today's
`process()`**, asserted against a captured reference buffer. This is what makes
it safe to change the engine's hot path without silently altering every saved
patch, and follows the precedent set when the oscillator wave selector kept
triangle bit-identical.

### Engine tests (`Tests/DspTests.cpp`)

- Patch audio is audible with no note held and DRONE off.
- Patch audio takes on each mode's filter character; spectra differ by mode.
- Extreme input values stay finite and bounded through the tanh stage.
- Taps are finite; `lastPostFilter()` diverges from `lastPreFilter()` as cutoff
  falls.
- Reset determinism is unchanged.

### Processor tests (`Tools/Render.cpp`)

- An explicit accept/reject table for `isBusesLayoutSupported`.
- **Buffer-aliasing guard:** a known signal fed into Patch In reaches the engine
  intact. This is the failure most likely to regress silently and the hardest to
  diagnose from a bug report.
- Tap buses produce audio when enabled and are untouched when disabled.
- CC 85 reaches `patchLevel`.
- State roundtrip still passes with the added parameter.

### Existing coverage

pluginval at strictness level 5 already runs in CI and exercises bus-layout
negotiation hard, which is meaningful free coverage for this change.

### Not covered

The web engine stays output-only. There is no host routing in a browser, so
`Tests/web-engine-tests.cjs` is untouched in Phase 1.

## Risks

| Risk | Mitigation |
|---|---|
| Buffer aliasing destroys patch input | Dedicated processor-level test; documented ordering requirement |
| Host bus-layout negotiation differs from expectations | pluginval strictness 5 in CI; manual check in Bitwig, REAPER, Ardour before merge |
| Box-average taps alias audibly | Documented limitation; proper decimation revisited in Phase 2 |
| Engine hot-path change alters existing patches | Sample-identity regression guard against a reference buffer |
| Editor growth breaks layouts at minimum window size | Third row is conditional; resize limits adjusted and tested at both extremes |

## Out of scope

- CV inputs and outputs (Phase 2)
- The effect-variant plugin entity (Phase 3)
- The JACK standalone and Patchance documentation (Phase 3)
- Polyphony, and any change to the existing three synth modes
- Web engine changes
