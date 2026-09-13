# Make Synth 0.1.0

## Linux

Extract the zip. Copy the entire `Make Synth.vst3` directory into `~/.vst3/`.
Keep its `Contents/` structure intact. Restart your DAW or rescan VST3 plugins,
then add **Make Synth** to a stereo instrument track. If replacing an older
version, move the old bundle out of the plugin directory first.

## macOS

Extract the zip in Finder. Copy the whole `Make Synth.vst3` bundle into
`~/Library/Audio/Plug-Ins/VST3/` (create this directory if needed). In Finder,
Go > Go to Folder accepts that path. Restart your VST3-compatible DAW and rescan.
The same bundle supports Apple Silicon and Intel Macs.

This private development build is ad-hoc signed, not Apple notarized. macOS may
block a browser-downloaded copy. After checking that it is your own build from
the private repository, you can remove quarantine from this specific bundle:

```sh
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Make Synth.vst3"
```

This changes only that plugin's download quarantine attribute. No system-wide
Gatekeeper changes are needed. Logic Pro requires Audio Units and cannot load
this VST3; use a VST3 host such as REAPER, Ableton Live, Cubase, or Bitwig.

## First sound

- The plugin starts silent. Press **DRONE**, or send a MIDI note.
- Use the top menu to choose one of the three synths.
- **Filter** changes cutoff/colour; **Motion Rate** sets how slowly it changes.
- Try **Detune** around 4-10 cents for a beating drone, **Breathing** near 0.8
  for noise swells, or **FM Depth** around 1-2 for metallic tones.
- **Space** adds stereo reverb. **Output** controls the final level.
- Switch Drone off for a gradual release. **STOP** cuts notes and the reverb.

The noise patch is naturally quieter than the oscillators; increase Output to
taste. Knob positions, mode, and Drone state are saved in your DAW session.
When Drone is saved on, reopening the session resumes continuous sound.
