# Contributing to COHERENCE

Thank you for your interest. COHERENCE is a small project maintained by Phase Systems in spare time, so replies can be slow.

## Bug reports

Open an issue with:
- your Mac (Apple Silicon or Intel), the macOS version, your DAW and its version, AU or VST3;
- what you did, what you expected and what happened;
- if it's about the sound: the settings (a saved preset or a screenshot of the panel) and, if possible, a short audio example.

## What changes are welcome

- **Bug fixes**, especially crashes, host compatibility, state recall and installer problems.
- **Documentation** fixes.
- **Not the sound by default.** The EQ, the compressor and the signal path are deliberately frozen. A DSP change needs a measurement that shows a real problem, and an explanation of what else it changes. Please open an issue to discuss it before writing code.
- **No changes** to parameter IDs, names, ranges or the saved-state format: sessions, automation and control-surface mappings depend on them.

## Before you send a pull request

1. Build and run the test suite. Everything must pass, apart from the one documented LIMIT:

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOHERENCE_BUILD_TESTS=ON
   cmake --build build --config Release --target CoherenceTests
   build/CoherenceTests_artefacts/Release/CoherenceTests
   ```

2. COHERENCE mode must stay bit-identical on Apple Silicon. Run this and expect 22 times `OK`:

   ```bash
   build/CoherenceTests_artefacts/Release/CoherenceTests --render-ref /tmp/coh-ref && (cd /tmp/coh-ref && shasum -a 256 -c "$OLDPWD/references-coh-normal.mac.sha256")
   ```

3. Keep changes small and focused, in the existing code style. Don't update `deps/JUCE` in the same change as anything else.

## Licence of contributions

COHERENCE is licensed under the GNU AGPLv3 (`LICENSE`). By contributing, you agree that your contribution is licensed under the same terms. Don't submit code you didn't write yourself unless its licence is compatible with the AGPLv3, and say where it comes from.
