# Sampler Expansion Roadmap

## Product goal
Cross-platform sampler instrument for macOS and Windows with AU, VST3 and standalone formats. Preserve the original sampler's quick workflow; keep advanced controls optional.

## First engine increment
- Instrument zones with sample references, key/velocity ranges, root key, gain, pan and fade regions.
- Velocity layers and simultaneous sample layers.
- Round-robin groups. The selector uses fixed-capacity storage (up to 128 simultaneous zone matches) and performs no heap allocation during note selection.
- MPE expression state: per-channel pitch bend, channel pressure, CC74 slide, sustain and per-note pressure.
- Validation and standalone C++ tests.

## Next engineering gates
1. Refactor CustomSamplerVoice to use immutable per-zone sample assets instead of the current single global sample buffer.
2. Route note-on through zone selection and allocate one voice for each matching layer.
3. Apply zone root key, gain, pan and fades in voice rendering.
4. Extend live MPE voice handling with configurable lower/upper zone selection, pitch-bend range, pressure and timbre destinations. The first lower-zone pass is now connected: member-channel pitch bend (48 semitones), channel/poly pressure (voice gain), CC74 (sample low-pass brightness), and channel sustain (JUCE synthesiser).
5. Decode/load samples on a worker thread and publish immutable assets safely.
6. Persist zones in a versioned JUCE ValueTree with missing-file relinking.
7. Add a keyboard/velocity zone editor, drag-drop mapping and preset browser.
8. Add slicing/transient detection and optional AI-assisted analysis after playback is stable.

## Audio-thread rules
No file I/O, decoding, heap allocation, mutex acquisition or blocking work in processBlock/voice rendering. Prepare immutable sample assets off-thread. Do not report zones as playable until their assets are loaded and validated. Keep MPE channel-scoped.

## Validation gates
Zone range/velocity boundaries; overlapping layers; round-robin; MPE channel isolation and expressive response; preset save/load; missing-file recovery; voice stealing; sample-rate conversion; CPU and memory limits; AU/VST3 host validation on macOS and VST3 validation on Windows.

## Current verified status (2026-11-13)
- Universal macOS Debug AU and VST3 builds succeed for x86_64 and arm64.
- AU installed at `~/Library/Audio/Plug-Ins/Components/Just a Sample.component`; complete Apple `auval -v aumu Bpf2 Manu` suite passes.
- VST3 installed at `~/Library/Audio/Plug-Ins/VST3/Just a Sample.vst3`; bundle signature and architecture checks pass. A separate VST3 host validator is not installed on the build Mac, so host-level VST3 validation remains open.
- First live lower-zone MPE pass is wired to sampler voices: 48-semitone pitch bend on member channels 2–16, pressure via channel/poly aftertouch, CC74 brightness via the voice low-pass cutoff, and sustain via JUCE's per-channel sustain handling. Upper-zone configuration, user-adjustable expression destinations, and end-to-end controller hardware testing remain open.
- Instrument-zone metadata and selection tests exist, but zone assets are not yet routed into live sample playback; do not describe multi-zone playback as complete.

## Licensing
Upstream project is MIT licensed. Preserve copyright and license notices. Audit every dependency separately before redistribution or commercial release.
