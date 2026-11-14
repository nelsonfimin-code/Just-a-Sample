# Sampler Expansion Roadmap

## Product goal
Cross-platform sampler instrument for macOS and Windows with AU, VST3 and standalone formats. Preserve the original sampler's quick workflow; keep advanced controls optional. The current pass is engine-first; the existing interface is intentionally unchanged.

## Implemented engine increment

### Instrument model
- Sample zones with sample references, key/velocity ranges, root key, gain, pan, and key/velocity fade regions.
- Velocity layers and simultaneous sample layers.
- Round-robin groups.
- Fixed-capacity zone selection: up to 128 simultaneous matches, no heap allocation during note selection.
- Versioned JASZ binary serialization (v2) with bounds checks, validation, corruption/truncation rejection, transactional restore, and v1 compatibility.
- Per-zone playback start/end, forward/ping-pong loop modes, and forward-loop crossfades.

### Multisample playback engine
- Fixed-capacity engine for 128 zones and 256 voices; default voice limit 88, linked to the existing voice-count parameter.
- Live MIDI note-on/off, pitch wheel, channel pressure, poly pressure, CC1, CC64, and CC74 handling.
- Sample-rate conversion, interpolated playback, root-key tuning, zone gain/pan, velocity/key fades, round robin, voice stealing, attack/release ramps, and per-voice LFO.
- Modulation matrix with up to 64 routes and sources for velocity, key tracking, channel/poly pressure, slide, pitch bend, mod wheel, LFO, and envelope; destinations include pitch, gain, pan, filter cutoff, attack, release, and sample start.
- MPE lower/upper zone configuration and master/member expression composition.
- Processor API for loading/clearing an instrument and setting modulation, MPE, and LFO configuration.
- Instrument map is serialized into plugin state. Sample files are decoded before entering the audio callback; audio assets are held by the processor for the engine's lifetime.
- The audio callback renders the multisample engine between MIDI event offsets. The engine render path uses fixed storage and does not perform file I/O or heap allocation.
- Offline transient analysis service computes multi-channel RMS envelopes and bounded slice markers with adjustable sensitivity, window size, minimum spacing, and maximum marker count.
- Transient-to-slice region generation creates one-key sample zones with exclusive sample boundaries, velocity ranges, configurable key ranges, minimum slice length, and a 128-zone cap. Generated slices can be serialized in the existing JASZ instrument format and loaded by the multisample engine.
- Offline .jasz preset file API supports bounded, atomic save/load. Missing-sample relinking searches user-supplied folders by exact filename and refuses ambiguous matches; relinking is transactional and never guesses between duplicates.

## Next engineering gates
1. Add safe background instrument loading and connect the .jasz import/relink APIs to the processor without blocking the audio or message thread.
2. Add slice auditioning and optional destructive/non-destructive region editing through the future editor workflow.
3. Add the instrument browser, keyboard/velocity mapping editor, drag/drop, slicing view, and MPE/modulation controls. UI work remains deferred as requested.
4. Add deeper per-zone effects routing and performance profiling with large instruments.
5. Test installed AU/VST3 in real Logic sessions, including multisample loading, MIDI controller expression, and state round-trip. pluginval/auval checks are already passing.
6. Validate Windows VST3 builds and runtime separately.

## Audio-thread rules
No file I/O, decoding, heap allocation, or blocking work in the audio callback or engine rendering. Prepare immutable sample assets before publishing them to the engine. Processor state changes are protected by the existing voice lock. Do not report an instrument as playable until all referenced assets are loaded and validated.

## Validation gates
Zone range/velocity boundaries; overlapping layers; round-robin; serialization round-trip and malformed input; forward/ping-pong loops and crossfades; transient analysis and malformed input; MPE channel isolation and expression composition; modulation routes; voice allocation/stealing; sample-rate conversion; audio output and release; CPU/memory limits; AU/VST3 host validation on macOS; VST3 validation on Windows.

## Current verified status (2026-11-14)
- Latest universal macOS Debug AU and VST3 builds succeed for x86_64 and arm64.
- Both installed bundles pass deep/strict code-signature verification.
- AU installed at ~/Library/Audio/Plug-Ins/Components/Just a Sample.component; Apple's complete auval -v aumu Bpf2 Manu suite passes, including render, MIDI, format, and parameter tests.
- VST3 installed at ~/Library/Audio/Plug-Ins/VST3/Just a Sample.vst3; pluginval strictness levels 5 and 10 both pass, including parameter fuzzing, state restore, automation, bus configuration, and processing tests. The optional standalone Steinberg VST3 validator was not configured in pluginval.
- sampler_expansion_tests passes through CTest (1/1); the expanded standalone suite also passes AddressSanitizer and UndefinedBehaviorSanitizer.
- Existing UI remains unchanged. The processor's instrument-loading API is implemented, but no browser/import control exposes it yet; a real user-facing multisample workflow is therefore not complete.
- Forward/ping-pong looping, forward-loop crossfade, transient marker analysis, and transient-to-slice zone generation are implemented. Slice audition through generated zones is supported by the engine API but not yet exposed in the UI.
- Missing-sample relinking is implemented and tested for unique and ambiguous filename matches. In-DAW multisample editing, Windows runtime validation, and a real Logic project test of multisample loading/state round-trip remain open.

## Licensing
Upstream project is MIT licensed. Preserve copyright and license notices. Audit every dependency separately before redistribution or commercial release.
