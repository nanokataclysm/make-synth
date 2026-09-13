# Three modular drone / noise patches

These are generic patch diagrams for a physical modular synth or virtual
rack. Each box names a function; use the equivalent module or built-in
function in your setup. The recipes are starting points and have not been
auditioned on your particular rack.

Open `modular-drone-patches.pdf` for three printable sheets. Editable SVGs
and PNG previews are also included. Teal solid cables carry the sound path;
purple dashed cables carry modulation. Arrows run from output to input.

**Start with output volume low and modulation depths at zero.** Get the
steady sound working first, then add the modulation cables one at a time.
Use your rack's output stage to feed speakers, headphones or an interface.

## The basic module names

| Label | What it does here |
|---|---|
| VCO | Oscillator: the pitched sound source |
| Noise | Unpitched sound source; use pink or white noise |
| VCF | Voltage-controlled filter: changes brightness or selects a frequency band |
| VCA | Voltage-controlled amplifier: changes loudness |
| LFO | Slow oscillator: moves another module's parameter repeatedly |
| Attenuator | Reduces modulation depth; an input's built-in depth knob can replace it |
| Mult | Copies one output into multiple inputs; it does not mix outputs together |
| Bias / initial gain | Holds a VCA partly open so a drone continues without a gate |

Manual controls and incoming CV can work together to set pitch, filter
frequency and loudness. The exact ranges depend on the module.
[Doepfer's voltage-control guide](https://doepfer.de/a100_man/a100t_e.htm)
explains those relationships.

## 1. Slow detuned drone

**Sound:** warm, steady, slightly beating. Start here for a pitched drone.

| From | To |
|---|---|
| VCO 1 triangle OUT | Mixer IN 1 |
| VCO 2 triangle OUT | Mixer IN 2 |
| Mixer OUT | Low-pass filter audio IN |
| Filter low-pass OUT | VCA audio IN |
| VCA OUT | Output module audio IN |
| LFO sine OUT | Attenuator IN |
| Attenuator OUT | Filter cutoff CV IN |

Set both VCOs around 110 Hz, or any comfortable low note. Match them by
ear, then turn VCO 2's fine knob slightly away from unison. A few cents is
enough for slow beating. Set both mixer channels modestly, use low filter
resonance and a cutoff around 800 Hz, and open the VCA with initial gain.

Set the LFO near 0.05 Hz: a 20-second cycle. Increase its depth until the
filter makes a gentle sweep. The four performance knobs are **VCO 2 fine
tune, filter cutoff, modulation depth and resonance**. Saw outputs add
more harmonics if you want a brighter sound.

## 2. Breathing noise

**Sound:** wind, surf, airy swells or resonant whistles. Start here if
texture matters more than a pitched note.

| From | To |
|---|---|
| Noise OUT | Band-pass filter audio IN |
| Filter band-pass OUT | VCA audio IN |
| VCA OUT | Reverb IN, or directly to output if omitting reverb |
| Reverb OUT, when used | Output module audio IN |
| LFO sine OUT | Mult IN |
| Mult OUT 1 | Filter-depth attenuator IN |
| Filter-depth attenuator OUT | Filter frequency CV IN |
| Mult OUT 2 | Volume-depth attenuator IN |
| Volume-depth attenuator OUT | VCA level CV IN |

Begin with pink noise if available, a filter center near 1 kHz, low
resonance, and a 10–40-second LFO cycle. Open the VCA enough to hear steady
noise before adding its CV. Raise the two modulation depths independently:
one moves the filter, the other changes the loudness. A little reverb,
around 20% wet as a starting point, adds space.

Play **filter frequency, resonance, LFO rate and volume-modulation depth**.
To keep the noise continuous, raise VCA bias or lower modulation depth until
the sound remains audible at the LFO's lowest point. If you only have a
low-pass filter, use its low-pass output for a softer wind-like variation.

An LFO can modulate both a filter and a VCA; see the
[Doepfer LFO manual](https://doepfer.de/a100_man/A145_man.pdf).

## 3. Slowly shifting metallic drone

**Sound:** a sustained FM texture that becomes richer and less plainly
pitched as modulation increases. The carrier needs a linear-FM input or mode.

| From | To |
|---|---|
| VCO 1 sine OUT | Low-pass filter audio IN |
| Filter low-pass OUT | VCA audio IN |
| VCA OUT | Output module audio IN |
| VCO 2 sine OUT | FM-depth attenuator IN |
| FM-depth attenuator OUT | VCO 1 linear FM IN |
| Slow LFO sine OUT | Drift-depth attenuator IN |
| Drift-depth attenuator OUT | VCO 2 pitch CV IN |

VCO 1 is the carrier you hear; VCO 2 is the modulator. Start them near
110 Hz and 156 Hz respectively, with FM and drift depth at zero. Keep the
filter fairly open and the VCA held open. Slowly increase FM depth, then
turn VCO 2's pitch knob to explore different ratios. Add only a very small
amount of pitch drift, with the LFO around 0.03 Hz.

Play **FM depth, modulator pitch, filter cutoff and drift depth**. Lower the
filter cutoff to soften a bright texture. The dashed FM cable carries an
audio-rate signal used as modulation; its color describes its role, not
its frequency.

Some oscillators expose separate pitch and linear-FM inputs; the
[Intellijel Dixie II manual](https://intellijel.com/downloads/manuals/dixie-2_manual_2018.09.13.pdf)
is one example. Use your oscillator's actual linear-FM jack or mode. An
exponential-FM input will behave differently from this recipe.

## If the patch is silent or a module is missing

- **Silent:** check that the VCA is open, mixer channels are up, the filter
  is not closed, and the output stage is passing sound. Restore the steady
  audio path before reconnecting modulation.
- **No VCA bias knob:** a suitable positive offset CV can hold it open.
  For patch 2, combine that offset with the LFO using a CV mixer/offset
  function. Follow the VCA's specified input range. The
  [Doepfer VCA manual](https://doepfer.de/a100_man/A1301_man.pdf) explains
  initial gain and the effect of bipolar modulation.
- **No VCA at all:** patches 1 and 3 can feed the filter into an output stage
  with level control. Patch 2 can still have moving filter color, but needs
  a VCA or equivalent to make the separate loudness swells.
- **No external attenuator:** use the destination input's depth control if
  it has one. The depth boxes are functions, not required extra purchases.
- **No reverb:** bypass the gray optional block in patch 2.
- **Huge pitch jumps or harsh sweeps:** reduce the relevant CV depth and
  establish its manual/base setting again.

These three recipes can reuse the same modules; they are alternatives,
not three racks to build simultaneously. Start with one patch and one or
two knobs, then add the remaining movement.

Version 0.1 • 2026-09-13.
