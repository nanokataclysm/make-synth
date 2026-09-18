# Modular Patching, Phase 2: CV Modulation

**Date:** 2026-09-18
**Status:** Approved design, not yet implemented
**Scope:** Phase 2 of 3
**Predecessor:** `docs/superpowers/specs/2026-09-18-modular-patching-phase-1-design.md` (merged)

## Goal

Turn instance-to-instance patching from a chain into a network. Phase 1 let one
instance's audio pass through another's filter. Phase 2 lets one instance's
control signals *shape* another: audio-rate CV into cutoff, pitch, FM depth and
pulse width, and CV out from the LFO and envelope.

## Context

Phase 1 shipped a "Patch In" audio bus, "Pre-Filter"/"Post-Filter" taps, a
`patchLevel` parameter on CC 85, and a conditional editor row. Phase 2 builds on
that bus and parameter discipline without disturbing it.

Two properties of the existing engine drive this design, and neither was chosen
for Phase 2's convenience.

### The smoothing model cannot carry CV

Every continuous parameter reaches the engine through a one-pole lag:

```cpp
auto smooth = [this](float& value, float to) { value += smoothing * (to - value); };
smooth(current.cutoff, target.cutoff);
```

`smoothing` is derived from a 25 ms time constant. That is correct for knob
moves and destroys control signals: a 200 Hz CV pushed through it is attenuated
to nothing. **CV therefore never joins `target`.** It applies after smoothing,
per sample, as a modulation of the already-smoothed value. This is arithmetic,
not preference.

### Filter coefficients refresh every 32 samples

```cpp
if ((coefficientCounter++ & 31u) == 0) { filters[0].tune(rate, sweep, q); ... }
```

`Filter::tune` calls `std::tan`, so the interval is a deliberate CPU
optimisation. At the 192 kHz internal rate it refreshes coefficients at 6 kHz —
ample for a 0.05 Hz LFO sweep, and wrong for audio-rate cutoff CV, which would
arrive stepped and aliased. Cutoff is the destination users reach for first, so
this is addressed rather than documented around.

## Decisions

Recorded with rationale, because each closed off alternatives.

1. **One 4-channel "CV In" bus**, channel order cutoff, pitch, FM depth, pulse
   width. Chosen over four discrete mono buses (self-documenting but four more
   entries cluttering every host's routing panel) and over a single mono bus with
   a destination parameter (smallest, but modulating cutoff and pitch together is
   the obvious patch and would be impossible). The channel-to-destination mapping
   is a convention; the documentation carries it.
2. **Four attenuverters, one per destination**, range −1…+1. Chosen over a single
   global depth knob and over fixed scaling with no control. An attenuverter on
   every input is the reason modular patches stay musical, and negative values
   give inversion without a host-side utility.
3. **CV is an offset, not a replacement.** The knob sets the base value; CV
   displaces it. This is why both exist.
4. **Engine inputs move to a `SampleInputs` struct** passed by const reference
   with a defaulted empty instance. Chosen over five defaulted scalar arguments
   (where swapping two at a call site compiles cleanly and misroutes CV) and over
   a pointer-plus-implicit-length array (undefined behaviour instead of a compile
   error). Existing zero-argument call sites keep working.
5. **Adaptive filter coefficient updates:** per-sample while cutoff CV is live,
   the existing 32-sample interval otherwise. Chosen over always-per-sample,
   which would impose a CPU regression on every existing patch including those
   that never touch CV.
6. **Pitch CV offsets whatever pitch is current** — the MIDI note if one is held,
   otherwise the drone pitch knob.
7. **CV outputs bypass the master level and limiter.** A control signal that gets
   quieter when you turn down the volume is not a control signal.

## Architecture

### Engine interface

```cpp
struct SampleInputs
{
    float patch = 0;
    float cvCutoff = 0, cvPitch = 0, cvFmDepth = 0, cvWidth = 0;
};

float process(const SampleInputs& in = {}) noexcept;
```

**C++17 only.** Designated initialisers (`{.patch = x}`) are C++20 and MSVC
builds in CI, so call sites use positional braces: `engine.process({x})` for
patch audio alone, `engine.process({x, c1, c2, c3, c4})` for the full set, and
`engine.process()` unchanged where nothing is fed.

The existing `float process(float patchIn = 0.0f)` signature is replaced, not
overloaded. Counted against the current tree: **8** patch-passing call sites in
`Tests/DspTests.cpp` each gain a brace, plus **1** in `Source/PluginProcessor.cpp`
(`engine.process(high.getChannelPointer(0)[i])`). The **20** zero-argument call
sites are untouched, which is the reason for the defaulted parameter.

### Where CV applies

After smoothing, before use. Exponential for the pitch-like destinations,
linear and clamped for the rest:

```cpp
const float cutoffMod = std::exp2(in.cvCutoff * current.cvCutoffAmount * 4.0f);   // +/-4 octaves
const float pitchMod  = std::exp2(in.cvPitch  * current.cvPitchAmount  * 2.0f);   // +/-2 octaves
const float fmDepth   = std::clamp(current.fmDepth + in.cvFmDepth * current.cvFmAmount * 5.0f, 0.0f, 5.0f);
const float width     = std::clamp(current.width   + in.cvWidth   * current.cvWidthAmount * 0.35f, 0.15f, 0.85f);
```

`cutoffMod` multiplies the smoothed cutoff before the existing `motion` LFO
sweep is applied, so CV and the internal LFO compose rather than fight.
`pitchMod` multiplies `base` after the existing clamp to `[10, rate*0.1]`, and
the result is re-clamped so CV cannot drive the oscillator past Nyquist.

The four attenuverter values are smoothed like any other knob — they are
parameters, not control signals.

### Adaptive coefficient updates

```cpp
const bool liveCutoffCv = target.cvConnected && current.cvCutoffAmount != 0.0f;
if (liveCutoffCv || (coefficientCounter++ & 31u) == 0)
    ... tune the three filters ...
```

`cvConnected` is supplied by the processor from bus state, mirroring Phase 1's
`patchConnected`. With the CV bus disabled or the cutoff attenuverter at zero,
timing and CPU cost are identical to today.

Note the counter must still advance on the sample-rate path so that disabling CV
mid-stream resumes the 32-sample cadence in phase rather than jumping.

### Buses

Appended, never inserted. Phase 1 established that disabled buses pack down and
that write offsets must be derived with `getChannelIndexInProcessBlockBuffer`
rather than hardcoded; that rule applies here unchanged.

| Bus | Index | Shape | Default | Carries |
|---|---|---|---|---|
| Patch In | input 0 | mono or stereo | disabled | Phase 1, unchanged |
| CV In | **input 1** | 4 channels | disabled | ch1 cutoff, ch2 pitch, ch3 FM depth, ch4 width |
| Output | output 0 | stereo | enabled | Phase 1, unchanged |
| Pre-Filter | output 1 | stereo | disabled | Phase 1, unchanged |
| Post-Filter | output 2 | stereo | disabled | Phase 1, unchanged |
| CV Out | **output 3** | stereo | disabled | L = LFO, R = envelope |

`isBusesLayoutSupported` gains: CV In must be disabled or exactly 4 channels;
CV Out must be disabled or stereo.

CV In is captured into a scratch buffer before `buffer.clear()`, exactly as
Phase 1 does for Patch In and for the same aliasing reason. Unlike Patch In it
is **not** summed to mono — each channel is a separate destination — so the
scratch buffer is 4 channels wide, sized in `prepareToPlay` to the same 32768
ceiling as `patchScratch`.

**Correction to an earlier draft of this section.** It claimed CV would be fed
through the existing oversampler alongside patch audio. That is infeasible: the
oversampler is constructed as `oversampling(2,2,...)` — two channels, exactly
enough for the stereo audio path — and four CV channels do not fit. Widening it
would mean four more polyphase filter chains running at 4x, which is a large
cost for signals that do not need it.

Instead, CV is read at host rate with a zero-order hold: for each oversampled
sample, the engine receives the CV value from the nearest host sample. The
incoming CV is already band-limited to the host Nyquist, so the imaging a ZOH
introduces sits above anything the filter or oscillator responds to musically,
and the destinations it drives — a filter cutoff, a pitch, a depth — are
smooth functions of it. This costs one integer division per sample and no
filter state at all.

### CV outputs

`lfo` and `envelope` already exist per-sample inside `process()`. They are
exposed the same way Phase 1 exposed the filter taps: read-after-process
accessors, box-average decimated from 4x to host rate.

```cpp
float lastLfo() const noexcept;        // bipolar, -1..+1
float lastEnvelope() const noexcept;   // unipolar, 0..1
```

The LFO runs continuously regardless of gate, which is correct for a modulation
source and means the CV Out bus is never silent — the same characteristic as the
Phase 1 taps, and documented the same way.

## Parameters

| ID | Index | Name | Range | Default | MIDI CC |
|---|---|---|---|---|---|
| `cvCutoffAmount` | 17 | CV cutoff amount | −1.0 … +1.0 | **0.0** | 86 |
| `cvPitchAmount` | 18 | CV pitch amount | −1.0 … +1.0 | **0.0** | 87 |
| `cvFmAmount` | 19 | CV FM amount | −1.0 … +1.0 | **0.0** | 88 |
| `cvWidthAmount` | 20 | CV width amount | −1.0 … +1.0 | **0.0** | 89 |

All default to zero. Enabling the CV bus alone changes nothing until an
attenuverter is deliberately raised — this keeps the golden guard green and
prevents a saved patch from acquiring unexpected movement.

Appending is mandatory: `readParameters()` and every `handleMidi()` case index
numerically into `ids` in `Source/PluginProcessor.h`, with no compile-time link
between an index and its id. The four new parameters are also declared last in
`layout()` so existing host automation indices do not shift. CCs 86–89 are
undefined in General MIDI and unused in the current table.

## Editor

No new row. Phase 1's conditional third row holds PATCH LEVEL in column 0 and
leaves columns 1–4 empty:

```
row 3:  PATCH LEVEL | CV CUTOFF | CV PITCH | CV FM | CV WIDTH
        (Patch In)    (------------- CV In -------------)
```

The row is visible when *either* bus is enabled; each group's knobs follow their
own bus. The window height stays at 820px and the `knobH = (h-322)/3` branch
fixed during Phase 1 continues to hold, so no layout arithmetic changes.

The signal-chain diagram gains no new box. Phase 1 already drops "SPACE" to keep
the count at four, and adding a fifth would break the `cw=(getWidth()-104)/4`
arithmetic.

## Testing

### The load-bearing test

**CV must demonstrably not be smoothed.** Feed a 500 Hz square wave into the
cutoff CV channel with the attenuverter at full, and assert the resulting
modulation depth matches the input's rate rather than collapsing. If a later
change folds CV back into the smoothing path, this test fails and nothing else
would — the sound would merely go dull.

Implementation: compare spectral energy, or measure peak-to-trough excursion of
the filtered output across a CV period, against the same run with the
attenuverter at zero.

### Everything differential

Three times during Phase 1 an assertion measuring absolute signal level passed
against broken code, because this engine is almost always making noise — with a
bus connected the gate is held open and the oscillator sounds regardless of
whether the feature works. Every new assertion compares paired runs that differ
only in the input under test.

### Engine tests (`Tests/DspTests.cpp`)

- Golden guard still passes with all CV defaults at zero.
- Each of the four destinations audibly changes the output when its attenuverter
  is raised and its channel is fed, and does not when the attenuverter is zero.
- A negative attenuverter inverts: the output differs from the positive case.
- Pitch CV cannot push the oscillator past Nyquist at extreme input.
- `lastLfo()` correlates with the rate knob; `lastEnvelope()` tracks the gate.
- Adaptive coefficient updates: output with cutoff CV live differs measurably
  from the same patch with the 32-sample cadence forced, proving the branch
  is reached.

### Processor tests (`Tools/Render.cpp`)

- Bus layout accept/reject table extended for CV In (disabled or 4ch) and CV Out
  (disabled or stereo).
- CV In survives `buffer.clear()` — the Phase 1 aliasing hazard, now with a
  4-channel scratch buffer.
- Each CV channel reaches its own destination and no other, which is the test
  that catches a channel-order mistake.
- CV Out carries audio on both channels when enabled.
- CCs 86–89 reach parameters 17–20, and the existing CC mappings have not
  drifted.

### Not covered

The web engine stays output-only. There is no host routing in a browser.

## Risks

| Risk | Mitigation |
|---|---|
| CV silently routed through the smoother | The load-bearing test above; it fails loudly and specifically |
| Channel order mistake sends CV to the wrong destination | Per-channel isolation test asserting each destination moves alone |
| 4-channel bus rejected or mishandled by a host | pluginval strictness 5 in CI; 4-channel buses are unusual and this is the main unknown |
| Per-sample `std::tan` causes CPU spikes | Adaptive branch keeps cost at today's level unless CV is live; measure before merge |
| CV out never silent surprises users | Documented, as the Phase 1 taps were |
| Parameter index drift breaks MIDI | Append-only discipline; existing CC assertions catch drift |

## Out of scope

- The effect-variant plugin entity, JACK standalone and Patchance docs (Phase 3)
- Polyphony, and any change to the three synth modes
- Web engine changes
- A CV routing matrix — channel-to-destination mapping is fixed
