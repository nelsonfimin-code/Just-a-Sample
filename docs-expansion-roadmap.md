# Sampler Expansion Roadmap

## Product goal
Cross-platform sampler instrument for macOS and Windows with AU, VST3 and standalone formats. Preserve the original sampler's quick workflow; keep advanced controls optional.

## First engine increment
- Instrument zones with sample references, key/velocity ranges, root key, gain, pan and fade regions.
- Velocity layers and simultaneous sample layers.
- Round-robin groups.
- MPE expression state: per-channel pitch bend, channel pressure, CC74 slide, sustain and per-note pressure.
- Validation and standalone C++ tests.

## Next engineering gates
1. Refactor CustomSamplerVoice to use immutable per-zone sample assets instead of the current single global sample buffer.
2. Route note-on through zone selection and allocate one voice for each matching layer.
3. Apply zone root key, gain, pan and fades in voice rendering.
4. Connect MPE expression to active voices with configurable pitch, pressure and slide destinations.
5. Decode/load samples on a worker thread and publish immutable assets safely.
6. Persist zones in a versioned JUCE ValueTree with missing-file relinking.
7. Add a keyboard/velocity zone editor, drag-drop mapping and preset browser.
8. Add slicing/transient detection and optional AI-assisted analysis after playback is stable.

## Audio-thread rules
No file I/O, decoding, heap allocation, mutex acquisition or blocking work in processBlock/voice rendering. Prepare immutable sample assets off-thread. Do not report zones as playable until their assets are loaded and validated. Keep MPE channel-scoped.

## Validation gates
Zone range/velocity boundaries; overlapping layers; round-robin; MPE channel isolation; preset save/load; missing-file recovery; voice stealing; sample-rate conversion; CPU and memory limits; AU/VST3 host validation on macOS and VST3 validation on Windows.

## Licensing
Upstream project is MIT licensed. Preserve copyright and license notices. Audit every dependency separately before redistribution or commercial release.
