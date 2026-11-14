# Sampler Expansion Roadmap

## Product goal
Cross-platform sampler instrument for macOS and Windows with AU, VST3 and standalone formats. Preserve the original sampler's quick workflow; keep advanced controls optional. The current pass is engine-first; the existing interface is intentionally unchanged.

## Implemented engine increment

### Instrument model
- Sample zones with sample references, key/velocity ranges, root key, gain, pan, and key/velocity fade regions.
- Velocity layers and simultaneous sample layers.
- Round-robin groups.
- Fixed-capacity zone selection: up to 128 simultaneous matches, no heap allocation during note selection.
- Versioned JASZ binary serialization with bounds checks, validation, corruption/truncation rejection, and transactional restore.

### Multisample playback engine
- Fixed-capacity engine for 128 zones and 256 voices; default voice limit 88, linked to the existing voice-count parameter.
- Live MIDI note-on/off, pitch wheel, channel pressure, poly pressure, CC1, CC64, and CC74 handling.
- Sample-rate conversion, interpolated playback, root-key tuning, zone gain/pan, velocity/key fades, round robin, voice stealing, attack/release ramps, and per-voice LFO.
- Modulation matrix with up to 64 routes and sources for velocity, key tracking, channel/poly pressure, slide, pitch bend, mod wheel, LFO, and envelope; destinations include pitch, gain, pan, filter cutoff, attack, release, and sample start.
- MPE lower/upper zone configuration and master/member expression composition.
- Processor API for loading/clearing an instrument and setting modulation, MPE, and LFO configuration.
- Instrument map is serialized into plugin state. Sample files are decoded before entering the audio callback; audio assets are held by the processor for the engine's lifetime.
- The audio callback renders the multisample engine between MIDI event offsets. The engine render path uses fixed storage and does not perform file I/O or heap allocation.

## Next engineering gates
1. Add per-zone start/end, loop start/end, loop modes, and crossfade looping to the playback model/engine.
2. Add transient detection and sample slicing as non-UI engine services.
3. Add an import/preset API for .jasz maps, robust missing-sample relinking, and safe background loading.
4. Add the instrument browser, keyboard/velocity mapping editor, drag/drop, slicing view, and MPE/modulation controls. UI work is deferred until the engine features are stable.
5. Add deeper per-zone effects routing and performance profiling with large instruments.
6. Complete VST3 host validation and test the installed AU/VST3 in real DAW sessions, including MIDI controller and state round-trip tests.
7. Validate Windows VST3 builds and runtime separately.

## Audio-thread rules
No file I/O, decoding, heap allocation, or blocking work in the audio callback or engine rendering. Prepare immutable sample assets before publishing them to the engine. Processor state changes are protected by the existing voice lock. Do not report an instrument as playable until all referenced assets are loaded and validated.

## Validation gates
Zone range/velocity boundaries; overlapping layers; round-robin; serialization round-trip and malformed input; MPE channel isolation and expression composition; modulation routes; voice allocation/stealing; sample-rate conversion; audio output and release; CPU/memory limits; AU/VST3 host validation on macOS; VST3 validation on Windows.

## Current verified status (2026-11-14)
- Universal macOS Debug AU and VST3 builds succeed for x86_64 and arm64.
- Both bundles pass deep/strict code-signature verification.
- AU installed at ~/Library/Audio/Plug-Ins/Components/Just a Sample.component; Apple's complete auval -v aumu Bpf2 Manu suite passes, including render and MIDI tests.
- VST3 installed at ~/Library/Audio/Plug-Ins/VST3/Just a Sample.vst3; universal architecture and code-signature checks pass. A dedicated VST3 host validator is not installed; Ableton's plugin scan did not populate its plugin database during this check, so host-level VST3 validation remains open.
- sampler_expansion_tests builds and passes through CTest (1/1). The same standalone suite passes under AddressSanitizer and UndefinedBehaviorSanitizer.
- Existing UI remains unchanged. The processor's instrument-loading API is implemented, but no browser/import control exposes it yet; a real user-facing multisample workflow is therefore not complete.
- Per-zone looping/slicing, missing-sample relinking, host-level VST3 playback, and Windows runtime validation remain open.

## Licensing
Upstream project is MIT licensed. Preserve copyright and license notices. Audit every dependency separately before redistribution or commercial release.
