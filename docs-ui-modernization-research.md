# Sampler UI Modernisation Research

Date: 2026-11-14
Scope: research only; no interface code or existing UI behaviour changed.

## Executive recommendation

Keep the native JUCE interface and evolve it into a compact, resizable, tabbed sampler workspace. Do not replace the project with a different sampler, migrate the plug-in to a WebView, or import another plugin's complete editor. The existing custom controls, waveform editor, navigator, FX chain, theme system, and processor attachments are valuable assets.

The current editor is based on a 1999 px Figma reference width. `PluginEditor.cpp::resized()` scales most fixed-width modules from that width and lays tuning, attack, release, playback, loop, and master controls in one horizontal toolbar. The editor permits very small dimensions (250 x 200), although this layout does not reflow into a genuinely compact arrangement at those sizes. The new multisample engine also needs a visible mapping and zone workflow, which the current screen does not provide.

## Reference products and transferable ideas

| Reference | Useful pattern | What to borrow |
|---|---|---|
| KODA Sampler | Separate Play, Mapping, Zone Edit, Mods, Setup and Routing workflows; layered zones; timeline editing; detachable/resizable views | Dedicated Mapping and Zone Edit views, progressive disclosure, grouped layers/round robins, inspector-based editing |
| Logic Pro Sampler | Distinct Mapping and Zone panes with a keyboard map and selected-zone waveform controls | A familiar keyboard map plus a focused selected-zone inspector; keep it understandable to Logic users |
| AudioModern Soundbox | Large waveform with sample/loop/fade controls and a clear key-map area | Waveform-first editing with visible loop boundaries and mapping beneath it |
| LANDR Sampler | Clear mode switching between waveform, FX and sequencing, with a keyboard at the bottom | Simple top-level modes and a persistent playable keyboard |
| The Audio Programmer JUCE sampler series | Custom JUCE editor, custom LookAndFeel, reusable graphics/assets and custom controls | Native JUCE implementation patterns; avoid replacing the processor or engine |

## Proposed information architecture

1. **PLAY** (default): loaded sample/instrument name, large waveform or performance display, essential tuning/attack/release/level controls, play/record, loop status, and playable keyboard. The sample browser remains collapsed until requested.
2. **MAP**: zone list/group tree at left, key/velocity map in the centre, keyboard along the bottom, and selected-zone properties at right. Show velocity layers and round-robin stacks without requiring the user to open each sample individually.
3. **EDIT**: large waveform, start/end/loop handles, fades, transient markers and slice operations. The selected zone is always explicit.
4. **MOD**: MPE zone configuration, modulation sources/routes, LFOs and envelopes. Keep advanced routing hidden from the Play view.
5. **FX**: ordered effect rack with compact modules, clear bypass state, and expanded visual editors only for the selected effect.

A compact header should provide preset/sample name, load/save, undo/redo, voice activity, output level and the five view tabs. Use a persistent keyboard only where it contributes to the current workflow; in Map it is part of the editor, while in Play it is a performance control.

## Visual direction

- Graphite/charcoal foundation, restrained warm amber for active selections and loop handles, and a secondary cool accent for zone/layer distinctions.
- Inter for interface text and a legible mono face for sample positions, values and technical readouts.
- Clear hierarchy: one primary waveform/map area, one inspector, and compact section controls. Avoid the current all-controls-at-once toolbar.
- Thin separators, consistent spacing, restrained shadows, high contrast, readable value readouts, and precise hit targets.
- Real responsive breakpoints: wide layout uses sidebars; medium layout collapses the inspector into a drawer; narrow layout stacks panels and retains usable controls. Do not shrink every control proportionally to fit.
- Preserve familiar sampler conventions and avoid changing the core sample-playback interaction just for visual novelty.

## Open-source implementation candidates

### JIVE — first candidate to evaluate
- MIT-licensed JUCE modules for declarative layouts and JSON-like style sheets, using JUCE `ValueTree` and existing JUCE components.
- No additional runtime dependency is advertised. It can improve layout/theme iteration while leaving DSP, parameter attachments and custom waveform components native.
- It is a layout/styling framework, not a finished sampler UI. JUCE 8.0.12 compatibility must be proven in an isolated build before adoption.
- Repository: https://github.com/ImJimmi/JIVE

### foleys_gui_magic — secondary candidate
- BSD-3-Clause JUCE GUI builder with drag/drop layout, stylesheet support and parameter connections.
- Potentially useful for prototyping conventional controls, but its latest published version history is older than JIVE's current development. Test JUCE 8 compatibility and maintenance before adding it.
- Repository: https://github.com/ffAudio/foleys_gui_magic

### Not recommended as the base
- OpenSampler is built on iPlug2 rather than JUCE, so its source is not a direct drop-in for this project.
- React/WebView audio-control libraries add another runtime and bridge layer; they are not justified for this native plugin and the current macOS target.
- KODA is a strong UX reference, not an open-source codebase to copy.

## Safe implementation sequence

1. Capture the current editor in a separate standalone/plugin test instance and record its behaviour before changing it.
2. Build a small JUCE 8.0.12 compatibility spike for JIVE without touching the production editor.
3. Implement the new header, tab navigation, design tokens and responsive layout shell while keeping existing controls wired to their current APVTS attachments.
4. Move the current waveform/sample workflow into PLAY/EDIT without changing parameter IDs or state format.
5. Add MAP as a separate view backed by the already-tested instrument model and processor API.
6. Add MOD and FX views after mapping is stable.
7. Validate resizing, keyboard focus, accessibility, automation, host scaling, AU/VST3 state recall and pluginval after each stage.

## Acceptance criteria

- Existing presets, parameter IDs, automation and host state remain compatible.
- No audio-thread work is introduced by UI navigation or painting.
- Play remains immediately usable without exposing advanced panels.
- Multisample zones, key ranges, velocity layers and round robins can be inspected and edited visually.
- Waveform editing remains accurate at multiple zoom levels.
- Layout reflows at common plugin sizes instead of merely shrinking controls.
- AU and VST3 pass pluginval/auval; a separate Logic project confirms real interaction and state recall.

## Sources

- KODA Sampler product workflow: https://kodasampler.com/
- KODA v1.0.11 update history (2026-10-23): https://community.kodasampler.com/t/koda-update-history-latest-v1-0-11-oct-23rd-26/16
- Logic Pro Sampler Mapping and Zone panes: https://support.apple.com/en-md/guide/logicpro/lgsifc8617c2/mac
- Sound On Sound, AudioModern Soundbox review: https://www.soundonsound.com/reviews/audiomodern-soundbox
- MusicRadar, LANDR Sampler review: https://www.musicradar.com/music-tech/its-a-stupendously-powerful-tool-10-of-the-best-sampler-plugins-in-2025
- JIVE repository and MIT licence: https://github.com/ImJimmi/JIVE
- foleys_gui_magic repository and BSD-3-Clause licence: https://github.com/ffAudio/foleys_gui_magic
- The Audio Programmer, JUCE sampler UI series (2026-05-07): https://www.theaudioprogrammer.com/content/build-this-awesome-sampler-plugin-part-4-juce-ui-basics
- OpenSampler (iPlug2-based reference): https://github.com/futureboard/opensampler


## Implementation status (2026-11-14)

The native JUCE workspace shell is now implemented with PLAY, MAP, EDIT, MOD and FX navigation. MAP includes sample import, keyboard zone lanes, selected-zone editing, per-zone filter/saturation/time-effect controls, transient slicing, and `.jasz` load/save. MOD includes MPE configuration, LFO controls and modulation route editing. The UI uses translucent graphite surfaces, thin light borders and restrained amber active states. Existing parameter attachments and the original sample workflow are retained.

JIVE was not integrated: the isolated JUCE 8 compatibility spike did not finish within the available local build window, so compatibility was not established. Native JUCE remains the lower-risk implementation for the existing plug-in. The Windows CI and separate Logic-project host checks remain outstanding.
