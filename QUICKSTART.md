# Make Synth 0.1.0

Cross-platform drone and noise synthesizer available as **VST3**, **AU**, **CLAP**, and interactive **Web Audio / Web MIDI**.

---

## Linux

Extract the release zip.

- **VST3**: Copy `Make Synth.vst3` into `~/.vst3/`.
- **CLAP**: Copy `Make Synth.clap` into `~/.clap/`.

Restart your DAW or rescan plugins (Bitwig, REAPER, Ardour, Renoise, etc.), then insert **Make Synth** on an instrument track.

---

## macOS (Apple Silicon & Intel Universal)

Extract the zip in Finder.

- **VST3**: Copy `Make Synth.vst3` to `~/Library/Audio/Plug-Ins/VST3/` (for Ableton Live, FL Studio, Bitwig, REAPER, Cubase).
- **AU (Audio Unit)**: Copy `Make Synth.component` to `~/Library/Audio/Plug-Ins/Components/` (for Logic Pro, GarageBand).
- **CLAP**: Copy `Make Synth.clap` to `~/Library/Audio/Plug-Ins/CLAP/` (for Bitwig, REAPER).

If macOS displays a quarantine prompt for downloaded builds, strip the quarantine flag from the bundles:

```sh
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Make Synth.vst3"
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/Components/Make Synth.component"
```

Restart your DAW and rescan plugins.

---

## Windows (x64)

Extract the release zip in File Explorer.

- **VST3**: Copy `Make Synth.vst3` into `C:\Program Files\Common Files\VST3\`.
- **CLAP**: Copy `Make Synth.clap` into `C:\Program Files\Common Files\CLAP\`.

Rescan plugins in your DAW (FL Studio, Ableton Live, Cubase, Studio One, REAPER, Bitwig).

---

## Web & Mobile (iOS / Android / Desktop)

Make Synth runs directly inside modern web browsers with zero installation required:

- **Desktop & Laptops**: Open `web/index.html` in Chrome, Firefox, Safari, or Edge.
- **iPhone / iPad (iOS)**: Open in Mobile Safari. Full touch-screen knob gestures and an on-screen keyboard let you play notes or drone immediately.
- **Android**: Open in Chrome. Touch gestures and USB/Bluetooth Web MIDI controllers are fully supported.
- **Local Testing**:
  ```sh
  cd /home/nanokat/dev/make-synth/web
  python3 -m http.server 8000
  ```
  Then navigate to `http://localhost:8000` on any device on your local network.

---

## First Sound & MIDI Controller Controls

- The plugin starts silent. Click **DRONE** for continuous sound, or play MIDI notes / keys.
- Select modes: **Detuned Drone**, **Breathing Noise**, or **Metallic Drone**.
- **Hardware MIDI Mapping**:
  - **CC 1 (Mod Wheel) / CC 11 (Expression)**: Primary expressive motion / breath / FM depth.
  - **CC 74**: Filter cutoff / brightness.
  - **CC 71**: Filter resonance / timbre.
  - **CC 76 / 14**: Modulation rate.
  - **CC 7**: Master volume output.
  - **CC 91**: Reverb space.
  - **CC 82**: Mode switch (Detuned / Breathing / Metallic).
  - **CC 65 / 81**: Toggle drone latch.
  - **CC 64**: Sustain pedal latch.
