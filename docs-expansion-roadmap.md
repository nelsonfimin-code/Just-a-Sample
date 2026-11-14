# Sampler Expansion Roadmap

## Product goal
Cross-platform sampler instrument for macOS and Windows with AU, VST3 and standalone formats. Preserve the original sampler's quick workflow; keep advanced controls optional. The current pass is engine-first; the existing interface is intentionally unchanged.

## Implemented engine increment

### Instrument model
- Sample zones with sample references, key/velocity ranges, root key, gain, pan, and key/velocity fade regions.
- Velocity layers and simultaneous sample layers.
- Round-robin groups.
- Fixed-capacity zone selection: up to 128 simultaneous matches, no heap allocation during note selection.
- Versioned JASZ binary serialization (v3) with bounds checks, validation, corruption/truncation rejection, transactional restore, and v1/v2 compatibility.
- Per-zone filter modes (off/low-pass/high-pass/band-pass), cutoff frequency, and normalized soft-saturation drive; legacy presets default these additions safely to bypass.
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
- Processor exposes background .jasz loading with sample decoding off the message/audio threads, generation-based cancellation/supersession, message-thread completion callbacks, and processor-lifetime-safe shutdown.

## Next engineering gates
1. Run the installed AU/VST3 in a separate real Logic project and verify multisample loading, MIDI expression, state round-trip, and long-session stability. Do not disturb the user's open Logic project.
2. Implement the approved modern sampler interface direction in `docs-ui-modernization-research.md`; preserve the existing fast workflow and keep advanced mapping/MPE controls progressively disclosed. The research is complete; UI code remains unchanged until the implementation spike is validated.
3. Connect the background .jasz loading/cancellation API to the future instrument browser and add progress/error presentation.
4. Add slice auditioning and optional destructive/non-destructive region editing through the future editor workflow.
5. Extend the per-zone effects stage with resonant filter models and optional time-based sends, then profile large instruments.
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
- sampler_expansion_tests and sampler_processor_tests pass through CTest (2/2). The processor integration test loads a real WAV, renders finite nonzero audio from MIDI, serializes/restores the instrument through plugin state, then loads a real .jasz preset on the background worker and renders again. The expansion suite also passes AddressSanitizer and UndefinedBehaviorSanitizer, including v1/v2 preset compatibility and per-zone filter modes.
- Existing UI remains unchanged. Processor-level multisample playback, plugin-state restoration, and background .jasz sample loading are now covered by an integration harness. No browser/import control exposes these features yet; a real user-facing multisample workflow is therefore not complete.
- Forward/ping-pong looping, forward-loop crossfade, transient marker analysis, and transient-to-slice zone generation are implemented. Slice audition through generated zones is supported by the engine API but not yet exposed in the UI.
- Missing-sample relinking is implemented and tested for unique and ambiguous filename matches. In-DAW multisample editing, Windows runtime validation, and a real Logic project test of multisample loading/state round-trip remain open.

## Licensing
Upstream project is MIT licensed. Preserve copyright and license notices. Audit every dependency separately before redistribution or commercial release.

## Execution update — 2026-11-14

### Completed in this pass

- Added a processor-level integration target. It loads a WAV into a zone, receives MIDI, renders non-zero audio, edits a zone, restores instrument state asynchronously, restores MPE/LFO/modulation-matrix settings, loads a real `.jasz` file in the background, and checks time-effect tails.
- Added resonant per-zone low-pass/high-pass/band-pass filtering, per-zone soft saturation, per-zone reverb/delay sends, and a shared preallocated reverb/delay output path. The delay defaults to 375 ms, 28% feedback and 38% wet return; the reverb defaults to a medium room.
- Bumped `.jasz` serialization to v5 for time-effect sends. Backward compatibility is tested for v1–v4.
- Changed processor state restoration to load the instrument on the cancellable background worker rather than synchronously decoding samples in the host state callback.
- Added native JUCE workspaces: PLAY, MAP, EDIT, MOD and FX. MAP includes sample import, automatic key mapping, a keyboard range display, zone inspector, per-zone effects, slicing, and `.jasz` load/save. MOD includes MPE, LFO and modulation routing.
- Added a user guide at `docs-multisample-workspace.md`.
- Added a Windows x64 VST3 build/test workflow at `.github/workflows/windows-build.yml`.

### Verification recorded

- Universal macOS Debug AU and VST3 builds succeeded for x86_64 and arm64.
- CTest: 2/2 passed.
- Processor-level WAV/MIDI/state/.jasz/time-effect integration test passed.
- AddressSanitizer and UndefinedBehaviorSanitizer expansion tests passed.
- `auval` passed.
- `pluginval` strictness level 10 passed.
- Installed AU/VST3 bundles were copied from the verified build artifacts and code-signature verification passed.

### Remaining external host gates

1. Run the new Windows GitHub Actions workflow and fix any platform-specific failures it reveals.
2. Test the installed AU/VST3 inside a separate Logic project, including sample import, mapping, MPE, preset recall and long-session stability. The existing open Logic project was deliberately left untouched.
3. Profile 88-voice playback and large multisample sets on the 2017 Intel Mac before treating the build as production-ready.
4. The JIVE compatibility spike did not complete within the local build window, so JIVE was not added. The production UI remains native JUCE to avoid an unverified layout dependency.

The editor and engine are now integrated, but production readiness is still conditional on the Windows CI and separate real-Logic host gates above.
