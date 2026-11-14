# Multisample Workspace — User Guide

## Open the workspace

Open Just a Sample in Logic and choose **MAP** from the top workspace bar.

## Build an instrument

1. Choose **+ Add samples** and select one or more WAV, AIFF, FLAC, MP3 or OGG files.
2. A single sample is mapped across the full MIDI keyboard with C4 as its root note.
3. Multiple samples are distributed into adjacent key zones. Up to 49 samples are mapped across C2–C6; larger sets are spread across the full MIDI range.
4. Select a zone from the list or click its range in the keyboard map.
5. Edit key/velocity ranges, root key, round-robin group, key/velocity fades, sample start/end, loop start/end/mode/crossfade, gain, pan, filter, cutoff, resonance, saturation drive, reverb send and delay send. Press **Apply zone changes** to commit the edit.

A round-robin group of `0` layers the zone normally. Zones sharing the same non-zero group are selected one at a time in rotation when their key and velocity ranges match. Fade values are measured in MIDI key or velocity steps; sample and loop boundaries are measured in frames.

The keyboard map shows up to eight visual lanes. The zone list remains the authoritative full list when an instrument has more than eight zones.

## Slice a sample

Select a zone and choose **Slice sample**. Transient analysis runs on a background worker, detects slice points, and replaces the selected zone with key-mapped slice zones. Slices are distributed across MIDI keys 36–84. The analysis path currently accepts samples up to 20 million frames per channel; unusually long files should be trimmed before slicing.

## Save and load

- **Save .jasz** writes the current zone map and sample references to a versioned instrument preset.
- **Load .jasz** restores the instrument and decodes its sample references asynchronously.
- Keep the source samples in their original folders when moving presets. If a sample cannot be found or a filename is ambiguous, add the sample from its new location and rebuild the affected zone.

The current `.jasz` writer emits version 5. Readers retain compatibility with versions 1–4.

## MPE and modulation

Choose **MOD** to configure:

- Lower- or upper-zone MPE layout.
- 1–15 member channels and pitch-bend range.
- LFO 1 rate and waveform.
- Modulation routes from velocity, key tracking, channel/poly pressure, slide, pitch bend, mod wheel, aftertouch, LFO 1 or envelope 1 to supported destinations.

Routes, MPE configuration and LFO settings are included in plug-in state recall.

## Play, edit and effects

- **PLAY** retains the familiar sample performance workflow.
- **EDIT** is a focused waveform workspace with the sample navigator; PLAY returns to the full familiar control layout.
- **FX** opens the existing effect rack.
- Per-zone reverb and delay sends feed shared time-effect buses. The current delay is 375 ms with 28% feedback and 38% wet return; the reverb uses a medium room setting. These are fixed engine defaults in this iteration, while each zone controls its own send amount.

## Compatibility and safety

- The audio engine uses fixed-capacity voice and zone storage and preallocated send buffers. Sample decoding and transient analysis happen off the audio thread. Instrument decoding is capped at 512 MiB of decoded float audio per loaded instrument, and only the first two channels are retained because the engine renders stereo.
- Zone filters use a resonant state-variable filter with low-pass, high-pass and band-pass modes.
- The installed AU and VST3 are universal macOS builds for Intel and Apple Silicon.
- Do not replace an open Logic session to load a new plug-in build. Close/reopen Logic or rescan plug-ins when ready to test the installed version in a separate project.
