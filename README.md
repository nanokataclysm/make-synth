# Make Synth

Three drone and noise synthesizer patches in a cross-platform instrument available as **VST3**, **AU**, **CLAP**, and **Web Audio**. Choose Detuned Drone, Breathing Noise, or Metallic Drone from the menu.

![Detuned Drone interface](docs/mode-1.png)

## Play

1. Install the matching bundle or format as described in [QUICKSTART.md](QUICKSTART.md).
2. Insert **Make Synth** on a stereo instrument/MIDI track in your DAW, or open `web/index.html` in your browser.
3. Press **DRONE** for continuous sound, or play MIDI notes.
4. Turn the knobs or use your hardware MIDI controller. **STOP** clears the held voice, MIDI notes, and reverb tail.

| Mode | Sound | Main controls |
| --- | --- | --- |
| Detuned Drone | Two triangle oscillators, beating slowly through a moving low-pass filter | Pitch, Detune, Filter, Motion |
| Breathing Noise | Pink or white noise through a band-pass filter and slow amplitude swells | Noise colour, Filter, Breathing, Motion |
| Metallic Drone | A sine pair with linear FM, filtered and gently drifting | Pitch, FM Ratio, FM Depth, Filter |

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

---

## Platforms & Formats

| Platform | Formats | Hosts / Compatibility |
| --- | --- | --- |
| **Linux (x86_64)** | VST3, CLAP | Bitwig, REAPER, Ardour, Renoise |
| **macOS (Universal arm64 / x86_64)** | VST3, AU, CLAP | Logic Pro, GarageBand, Ableton Live, FL Studio, Bitwig, Cubase |
| **Windows (x64)** | VST3, CLAP | FL Studio, Ableton Live, Cubase, Studio One, REAPER, Bitwig |
| **Web & Mobile (iOS / Android / Desktop)** | Web Audio, Web MIDI | Safari (iOS), Chrome (Android / Desktop), Firefox, Edge |

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
No build steps required—pure vanilla ES6 Web Audio & Web MIDI:
```sh
cd web
python3 -m http.server 8000
```
Open `http://localhost:8000` in any mobile or desktop browser.

---

## Validation & Quality Assurance

All platforms are checked with `ctest` covering sample accuracy, polyphony handling, mode crossfading, DC blocking, and extreme parameter stress tests. Plugin builds are validated using `pluginval` at strictness level 5.

```sh
build/MakeSynthRender_artefacts/Release/MakeSynthRender --render renders
build/MakeSynthRender_artefacts/Release/MakeSynthRender --snapshot docs
```

The audio path uses 4x oversampling, smoothed controls and mode transitions, DC removal, and a bounded output. Synthesis and MIDI handling allocate zero memory on the audio thread.
