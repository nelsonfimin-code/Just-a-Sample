# Sampler Expansion Roadmap

## Product goal
Cross-platform sampler instrument for macOS and Windows with AU and VST3 formats. Preserve the original sampler's quick PLAY workflow; expose advanced mapping, editing and MPE controls in separate workspaces. The native JUCE editor now includes PLAY, MAP, EDIT, MOD and FX workspaces.

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

## Remaining external validation gates
1. Run the Windows x64 VST3 GitHub Actions workflow and fix any platform-specific build or test failures.
2. Validate the latest installed AU/VST3 in a separate real Logic project: import samples, edit key/velocity maps and loops, play MPE expression, save/reopen state, and run a long session. Do not disturb the user's already-open Logic project.
3. Profile 88-voice playback and large multisample sets in Release on the 2017 Intel Mac with real source material, not only synthetic test buffers.
4. Complete host-side usability review of all five workspaces at minimum and maximum editor sizes; verify host scaling, keyboard focus, automation, and preset recall.

The JIVE experiment remains optional and is not a production dependency. The implementation uses native JUCE components and the existing custom LookAndFeel to avoid an unverified UI framework dependency.

## Audio-thread rules
No file I/O, decoding, heap allocation, or blocking work in the audio callback or engine rendering. Prepare immutable sample assets before publishing them to the engine. Processor state changes are protected by the existing voice lock. Do not report an instrument as playable until all referenced assets are loaded and validated.

## Validation gates
Zone range/velocity boundaries; overlapping layers; round-robin; serialization round-trip and malformed input; forward/ping-pong loops and crossfades; transient analysis and malformed input; MPE channel isolation and expression composition; modulation routes; voice allocation/stealing; sample-rate conversion; audio output and release; CPU/memory limits; AU/VST3 host validation on macOS; VST3 validation on Windows.

## Current verified status (2026-11-14)

- Latest universal macOS Debug AU and VST3 build artifacts completed successfully for x86_64 and arm64.
- Both generated build bundles pass `codesign --verify --deep --strict`. This verifies bundle signatures, not host compatibility.
- CTest passes 2/2 registered tests: `sampler_expansion_tests` and `sampler_processor_tests`.
- The Envelope1 modulation correction is covered by a regression test that failed before the fix and passes after it.
- Commit `46a463a` (`Drive envelope modulation from full voice envelope`) is pushed to `origin/feature/instrument-zones-mpe`; the local worktree is clean.
- The newly built bundles have **not** been installed over the user's existing plug-ins because Logic was open. The current installed binaries therefore must not be assumed to contain this commit.
- AU validation is still open. A previous `auval -v aumu Bpf2 Manu` attempt failed to discover/read the component name and version; the latest build has not yet passed `auval`.
- VST3 host validation is still open. `pluginval` is not installed on the development Mac, and no real DAW playback test has been completed for this build.
- ASan/UBSan were not run as part of this latest verification pass.

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

The latest verification is listed in **Current verified status** above. In particular, build, CTest and code-signature checks passed; AU/VST3 host validation, installation of this build, and sanitizer runs remain outstanding.

### Remaining external host gates

1. Run the new Windows GitHub Actions workflow and fix any platform-specific failures it reveals.
2. Test the installed AU/VST3 inside a separate Logic project, including sample import, mapping, MPE, preset recall and long-session stability. The existing open Logic project was deliberately left untouched.
3. Profile 88-voice playback and large multisample sets on the 2017 Intel Mac before treating the build as production-ready.
4. The JIVE compatibility spike did not complete within the local build window, so JIVE was not added. The production UI remains native JUCE to avoid an unverified layout dependency.

The editor and engine are now integrated, but production readiness is still conditional on the Windows CI and separate real-Logic host gates above.
