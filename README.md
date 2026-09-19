# Make Synth

Three drone and noise synthesizer patches in a cross-platform instrument available as **VST3**, **AU**, **CLAP**, and **Web Audio**. Choose Detuned Drone, Breathing Noise, or Metallic Drone from the menu.

![Detuned Drone interface](docs/mode-1.png)

## Development status

The `modular-patching-phase-2` branch contains work in progress on CV modulation
for the native plugins. CV input routing, modulation destinations, host parameters
and MIDI CC mappings are implemented. The CV output bus is declared but does not
yet emit signals, and the dedicated editor controls are still pending. This
branch is not a completed Phase 2 release; the design and implementation checklist
are in the [Phase 2 plan](docs/superpowers/plans/2026-09-18-modular-patching-phase-2.md).
That checklist records the proposed work; the status here reflects the current code.

## Play

1. Install the matching bundle or format as described in [QUICKSTART.md](QUICKSTART.md).
2. Insert **Make Synth** on a stereo instrument/MIDI track in your DAW, or open `web/index.html` in your browser.
3. Press **DRONE** for continuous sound, or play MIDI notes.
4. Turn the knobs or use your hardware MIDI controller. **STOP** clears the held voice, MIDI notes, and reverb tail; see the [patching limitations](#patching-instances-together) when Patch In is enabled.

| Mode | Sound | Main controls |
| --- | --- | --- |
| Detuned Drone | Two oscillators — sine, triangle, saw, square or pulse — beating slowly through a moving low-pass filter | Wave, Pulse Width, Pitch, Detune, Filter, Motion |
| Breathing Noise | Pink or white noise through a band-pass filter and slow amplitude swells | Noise colour, Filter, Breathing, Motion |
| Metallic Drone | A sine pair with linear FM, filtered and gently drifting | Pitch, FM Ratio, FM Depth, Filter |

Detuned Drone carries the full classic waveform set: **Sine**, **Triangle**, **Saw**, **Square**, and **Pulse** with a **Pulse Width** control. Saw, square, and pulse are band-limited with polyBLEP and run through the plugin's 4x oversampling, so they stay clean up to the top of the pitch range. Triangle is the default, so patches saved before the wave selector existed reload unchanged.

All modes share resonance, modulation rate/depth, stereo reverb (**Space**), and output level. The mode menu changes the signal path and visible controls; it preserves knob settings. Parameters are automatable, MIDI CC responsive, and saved with your session.

### MIDI Controller Mapping

| CC Number | Destination |
| --- | --- |
| **CC 1** (Mod Wheel) / **CC 11** (Expression) | Primary dynamic motion / breath / FM depth for active mode |
| **CC 74** | Filter cutoff / brightness |
| **CC 71** | Filter resonance / timbre |
| **CC 76** / **CC 14** | Modulation rate |
| **CC 7** | Master volume output |
| **CC 91** | Reverb space |
| **CC 77** / **CC 12** | Detune spread (Mode 0) |
| **CC 78** / **CC 13** | FM ratio (Mode 2) |
| **CC 75** / **CC 15** | FM depth (Mode 2) |
| **CC 73** | Breathing swell (Mode 1) |
| **CC 80** / **CC 16** | Drone pitch |
| **CC 64** | Sustain pedal latch |
| **CC 65** / **CC 81** | Drone latch toggle |
| **CC 82** | Mode switch (0: Detuned, 1: Breathing, 2: Metallic) |
| **CC 83** | Noise color toggle (Pink / White) |
| **CC 70** | Oscillator wave (Mode 0: Sine / Triangle / Saw / Square / Pulse) |
| **CC 79** | Pulse width (Mode 0, Pulse wave) |
| **CC 85** | Patch level |
| **CC 86** | CV cutoff amount (Phase 2) |
| **CC 87** | CV pitch amount (Phase 2) |
| **CC 88** | CV FM depth amount (Phase 2) |
| **CC 89** | CV pulse width amount (Phase 2) |

## Patching Instances Together

The native plugins expose the following audio patching buses beyond their main
output. All are disabled by default; enable them in your host's routing or pin
matrix. The Phase 2 CV buses are described separately below.

| Bus | Direction | Purpose |
| --- | --- | --- |
| **Patch In** | Input (mono or stereo) | Audio from another instance, summed into the oscillator before the filter. It takes on the active mode's filter character, drive and reverb. |
| **Pre-Filter** | Output (stereo) | The summed source before filtering. |
| **Post-Filter** | Output (stereo) | The filter output before the drive stage. |

Enabling Patch In holds the amplitude envelope open, so patched audio passes
with no note held and DRONE off. **Patch Level** sets how much joins the source.
A stereo Patch In is summed to mono (0.5/0.5) before it enters the engine.

This has consequences worth knowing before you wire it up:

- **Enabling Patch In makes the instance sound on its own.** The held-open
  gate applies to the whole voice, not just the patched signal, so with Patch
  In enabled, DRONE off, no notes held, and even Patch Level at 0, the
  instance still emits its own drone continuously.
- **STOP does not silence a patched instance.** STOP resets the amplitude
  envelope, but the held-open gate re-attacks it whenever Patch In is enabled.
- **Pre-Filter and Post-Filter are pre-envelope taps.** They carry full-level
  audio continuously, including after STOP and with nothing playing. This is
  deliberate — like tapping a VCO ahead of a VCA — but surprising if you
  expect the taps to go quiet with the rest of the instance.

None of the above has been verified against a real host; it follows from the
engine and processor code.

These buses are designed for hosts with flexible audio routing — Bitwig,
REAPER and Ardour are the expected targets. Ableton Live and Logic restrict
audio routing into instrument plugins; a dedicated effect build is planned
to cover them.

### CV modulation (Phase 2, in development)

Enable the four-channel **CV In** bus and use the host's parameter controls or
MIDI CCs 86–89 to set a destination's amount. The amounts range from −1 to +1,
default to zero, and can invert the incoming signal. Enabling CV In alone does
not open the voice gate; play a note or enable DRONE to hear modulation.

| CV In channel | Destination | Amount parameter / MIDI CC | Effect at full amount with a ±1 signal |
| --- | --- | --- | --- |
| 1 | Filter cutoff | `cvCutoffAmount` / 86 | Up to ±4 octaves around the knob setting, within the filter's limits |
| 2 | Pitch | `cvPitchAmount` / 87 | Up to ±2 octaves around the held note or drone pitch, within the engine's limits |
| 3 | FM depth | `cvFmAmount` / 88 | Adds up to ±5, clamped to 0–5; audible in Metallic Drone |
| 4 | Pulse width | `cvWidthAmount` / 89 | Adds up to ±0.35, clamped to 0.15–0.85; audible with the Pulse waveform in Detuned Drone |

The four incoming channels are captured separately before the shared audio
buffer is cleared, then held at their host-sample values during 4× processing.
Cutoff coefficients update every internal sample while cutoff CV is active.
These native plugin buses are not implemented in the Web Audio player.

The optional stereo **CV Out** bus is reserved for LFO (left) and envelope
(right). The engine exposes those values, but the processor does not yet write
them to the bus: **CV Out currently remains silent**. CV knobs in the custom
editor, host routing validation, and a completed Phase 2 release remain pending.

---

## Platforms & Formats

| Platform | Formats | Hosts / Compatibility |
| --- | --- | --- |
| **Linux (x86_64)** | VST3, CLAP | Bitwig, REAPER, Ardour, Renoise |
| **macOS (Universal arm64 / x86_64)** | VST3, AU, CLAP | Logic Pro, GarageBand, Ableton Live, FL Studio, Bitwig, Cubase |
| **Windows (x64)** | VST3, CLAP | FL Studio, Ableton Live, Cubase, Studio One, REAPER, Bitwig |
| **Web & Mobile (iOS / Android / Desktop)** | Web Audio; Web MIDI where the browser implements it | Chrome/Edge (desktop and Android) and Firefox desktop for Web MIDI. iOS (Safari and every other browser) has **no Web MIDI** — use the on-screen keyboard. Safari on macOS has no Web MIDI. |

---

## Builds

The repository's **Build VST3, AU, CLAP, and Web** GitHub Actions workflow produces:

- `MakeSynth-linux-x86_64`: Linux VST3 and CLAP bundles built on Ubuntu.
- `MakeSynth-macos-universal`: Universal macOS bundle containing VST3, AU (`.component`), and CLAP for Apple Silicon and Intel Macs.
- `MakeSynth-windows-x86_64`: Windows 64-bit VST3 and CLAP binaries built on Windows Server 2022.
- `MakeSynth-web`: Web Audio synthesizer player ready for local hosting or GitHub Pages deployment.

### Source Builds

#### Linux
```sh
sudo apt install build-essential cmake pkg-config \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxrender-dev libxi-dev libgl1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build --output-on-failure
python3 Tools/package.py --platform linux-x86_64 --output dist
```

#### macOS
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --config Release --parallel 2
ctest --test-dir build --output-on-failure
python3 Tools/package.py --platform macos-universal --output dist
```

#### Windows
```cmd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build --output-on-failure -C Release
python Tools/package.py --platform windows-x86_64 --output dist
```

#### Web (Browser / Mobile)
No build steps required — vanilla ES6 Web Audio, plus Web MIDI in browsers that implement it:
```sh
cd web
python3 -m http.server 8000
```
Open `http://localhost:8000`. The web engine follows the same oscillators, filter, and envelope as `Source/SynthEngine.h`, at the AudioContext sample rate (no 4× oversampling) with the plugin's dB output range and limiter. It is not a sample-accurate copy of the VST3/AU/CLAP build.

```sh
node Tests/web-engine-tests.cjs
```

---

## Validation & Quality Assurance

`ctest` runs the C++ DSP and processor binaries. Those checks cover default silence, pitched-mode frequency, envelope release, MIDI note timing, sustain-pedal last-note hold, MIDI CC 1/71/74, parameter state round-trip, sample-rate changes, FM DC, and extreme-parameter stability. Phase 2 adds engine CV response and pitch-clamp tests, LFO/envelope accessor checks, accepted bus layouts, CC 86–89 mapping, and processor CV input routing. The instrument is **monophonic** (last-note priority); there is no polyphonic voice test and no named mode-crossfade assertion. If `node` is on `PATH`, `ctest` also runs `Tests/web-engine-tests.cjs` (RNG range, default metallic headroom, limiter ceiling, dB gain, pink vs white).

Plugin CI validates **VST3** with `pluginval` at strictness level 5 (Linux, Windows, macOS arm64 and x86_64). CLAP and AU are built and packaged; they are not pluginval'd. `validation/summary.json` is a historical local note from commits `d482760` / `b086642` and is **not** evidence for `eaaf65f` or later.

The workflow runs on pushes to `main`, pull requests targeting `main`, or a
manual dispatch. Pushing this development branch alone does not run that
workflow. Passing local tests does not establish current cross-platform builds,
pluginval acceptance, real-host CV routing, or release package validation.

```sh
build/MakeSynthRender_artefacts/Release/MakeSynthRender --render renders
build/MakeSynthRender_artefacts/Release/MakeSynthRender --snapshot docs
```

The plugin audio path uses 4x oversampling, smoothed controls and mode transitions, DC removal, and a bounded output. Synthesis and MIDI handling allocate zero memory on the audio thread.
