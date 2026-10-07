# Converb: codebase notes and build plan

## Phase 1 findings

1. **Framework & build:** JUCE 9.0.1 (git submodule), CMake via the Pamplejuce template, C++23. Formats: Standalone, AU, VST3, AUv3, plus CLAP through `clap-juce-extensions`. AAX is not built.
2. **Project layout:** The template has a single `PluginProcessor` / `PluginEditor` pair in `source/`, with no existing DSP, UI component or LookAndFeel code. `source/` is globbed into the `SharedCode` INTERFACE library, which both the plugin and the `Tests` target link. Binary assets live in `assets/`.
3. **Parameter system:** There is none yet (`getStateInformation` is a stub). I chose the idiomatic JUCE pattern: an `AudioProcessorValueTreeState` with versioned `juce::ParameterID`s, camelCase IDs, and state saved as XML through `copyXmlToBinary`.
4. **Existing DSP:** None in the project. JUCE's `juce::dsp::Convolution` is reused. It does uniform or non-uniform partitioned FFT convolution, crossfades between IRs (50 ms), and resamples the IR to the host rate on every `prepare()`.
   - **Caveat found in JUCE source:** `Convolution::loadImpulseResponse()` writes a `pendingCommand` member that the audio thread also touches in `process()`. Calling it from a loader thread would be a data race, so ready IRs are handed to the audio thread through a lock-free mailbox, and the audio thread makes the (wait-free) call itself.
   - **Caveat:** when no IR has been loaded, JUCE's engine uses a unit impulse, so the wet signal equals the dry signal. To get silence with no IR loaded, the processor loads a one-sample silent IR at construction.
5. **UI conventions:** None (the template has a "Hello" editor with a melatonin inspector button). This work adds a new `ConverbLookAndFeel`, a `Palette` of colour constants, and its own components in `source/ui/`.
6. **Tests & tooling:** Catch2 v3 (`tests/`, picked up by glob), Catch2 benchmarks, pluginval at strictness 10 in CI, `.clang-format` (Allman braces, 4-space indent).

## Files

- `source/Parameters.{h,cpp}`: parameter IDs, layout and gain-law helpers
- `source/dsp/ImpulseResponse.{h,cpp}`: decode, trim, length cap, normalise, display peaks, FLAC state encoding
- `source/dsp/IRMailbox.h`: lock-free handoff of ready IR buffers to the audio thread
- `source/PluginProcessor.{h,cpp}`: signal chain, background IR loader, state
- `source/ui/*`: palette, LookAndFeel, gain strip, level meter, rotary control, IR display, IR panel, main view
- `source/PluginEditor.{h,cpp}`: resizable fixed-aspect host for the main view
- `tests/ConverbTests.cpp`: DSP and state tests

## Deviations from the prompt

- **Accent colour:** yellow from the prompt (chosen over the mockup's cream).
- **Extra readouts from the mockup:** level meters, RMS / Peak / Length / Channels / Sample-rate stats row, and a status bar with the IR path. Preset browser, A/B compare, undo/redo, settings and help are left out.
- **Reverb Amount curve:** `gain = amount²` (0 % = −inf, 50 % ≈ −12 dB, 100 % = 0 dB).
- **IR normalisation:** done by Converb itself, energy-based (unit energy in the loudest channel). JUCE's own normaliser targets −18 dB and is turned off.
- **IR state:** the trimmed and capped (un-normalised) IR is stored as 24-bit FLAC, base64-encoded in the plugin state, alongside the original file path.
- **Channels:** files with more than 2 channels use only their first two. True-stereo 4-channel IRs are not supported.

## Verification (2026-10-08, macOS arm64)

- Debug and Release builds of all formats (VST3, AU, CLAP, Standalone) complete with no warnings.
- `Tests`: 12 cases and 657 assertions pass in both Debug and Release. `./Tests "[screenshot]"` renders the editor to PNG.
- `Benchmarks`: processing a 512-sample block with a 5 s stereo IR at 48 kHz averages about 218 µs (Release), roughly 2 % of the block's real-time budget.
- pluginval at strictness 10 passes for VST3 (including GUI tests) and AU.
- Not yet done: manual listening checks in a DAW (clicks under fast automation, changing sample rate mid-session, real mono, stereo and long IR files).

## Known limitations

- Files with more than 2 channels use only their first two; true-stereo (4-channel) IRs are not supported.
- The IR is stored in the plugin state as FLAC, adding about 1–3 MB per instance for long stereo IRs.
- When a stereo IR plays on a mono track, only its left channel is used.
