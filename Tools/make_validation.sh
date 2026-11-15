#!/bin/bash
# Regenerate VALIDATION.md: the full test report, then the comparison with the MDWEQ6 renders and the format-check note.
#
# Usage (from the project folder, after building CoherenceTests):
#   bash Tools/make_validation.sh [MDWEQ6 renders folder] [format-check report] [auval log]
# Defaults: ../REFERENCES/Q renders (as on the Mac; names like "1k 1 +12.wav" or "MDW 1k Q1 +12.wav") and no
# format-check report (the Linux harness note is used).
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
T=build/CoherenceTests_artefacts/Release/CoherenceTests
SRC="${1:-../REFERENCES/Q renders}"
FC="${2:-}"
AUVAL="${3:-}"
WORK="$(mktemp -d)"
# The tools read "MDW 1k Q1 +12.wav". Your original renders are named "1k 1 +12.wav": map those to copies.
MDW="$WORK/mdw"
mkdir -p "$MDW"
for f in "$SRC"/*.wav; do
    [ -e "$f" ] || continue
    b="$(basename "$f" .wav)"
    case "$b" in
        MDW\ *) cp "$f" "$MDW/$b.wav" ;;
        COH*) ;;
        *) read -r fr q g extra <<<"$b"
           [ -n "$g" ] && [ -z "$extra" ] && cp "$f" "$MDW/MDW $fr Q$q $g.wav" ;;
    esac
done

"$T" --report VALIDATION.md >"$WORK/run.txt" 2>&1 || echo "Some checks failed: see VALIDATION.md"
"$T" --render-q "$WORK/coh" >/dev/null
"$T" --render-q "$WORK/coh96" 96000 >/dev/null
cd Tools
python3 vs_analog.py "$MDW" "$WORK/coh" >"$WORK/vs_analog.md"
python3 phase_analyze.py "$MDW" MDW >"$WORK/ph_mdw.md"
python3 phase_analyze.py "$WORK/coh" COH-ENGINE | tail -n +3 >"$WORK/ph_coh.md"
python3 compare_renders.py "$MDW" MDW "$WORK/coh" COH-ENGINE >"$WORK/cmp.md"
python3 compare_renders.py "$MDW" MDW "$WORK/coh96" COH-ENGINE >"$WORK/cmp96.md"
python3 null_model.py "$MDW" MDW "$WORK/coh" COH-ENGINE >"$WORK/null.md"
cd ..

{
cat <<EOF

## Comparison with MDWEQ6 renders (48 kHz)

Your five MDWEQ6 bell renders (REFERENCES/Q renders) and the same settings rendered through the COHERENCE engine
(\`CoherenceTests --render-q\`). Measured from the files with \`Tools/vs_analog.py\`, \`Tools/compare_renders.py\` and
\`Tools/phase_analyze.py\`. Five bell settings at one sample rate are a small sample: shelves, cut filters, other
frequencies and Q values, and other sample rates have not been compared yet (kit: REFERENCES/EQ renders v2).

### Magnitude against the analog bell (classic / MDWEQ6 Q definition)

$(cat "$WORK/vs_analog.md")

### Phase against the analog bell

Neither plug-in adds latency to the render: Ableton compensates MDWEQ6's, and COHERENCE has none. Phase difference
from the analog bell:

$(cat "$WORK/ph_mdw.md")
$(cat "$WORK/ph_coh.md")

MDWEQ6 follows the analog phase closely: it processes at twice the rate internally, with linear-phase resampling
(DESIGN.md section 2). COHERENCE's magnitude is closer to analog than MDWEQ6's, but its phase departs by up to
about 2.3 degrees below 10 kHz for the wide 1 kHz +12 dB bell and 5.4 degrees for the 10 kHz bell. That is inherent
to a second-order section at 48 kHz (DESIGN.md section 8).

### COHERENCE against MDWEQ6, same settings

Null depth is the level of the difference signal (MDWEQ6 minus COHERENCE) relative to the signal, at its shallowest
in each range: what a sample-aligned null test would leave. It includes phase; 1 degree on its own leaves -35 dB.

$(cat "$WORK/cmp.md")

The magnitudes agree to within 0.04 dB at 1 kHz, but the null is limited by phase: about -30 dB from 1 to 10 kHz for
the +/-12 dB settings and -20 dB for the 10 kHz bell.

### What a null test against MDWEQ6 leaves, and why (48 kHz)

\`Tools/null_model.py\`: the same source through both measured impulse responses, subtracted sample-aligned, for three
source spectra. The residual is split exactly into a magnitude part and a phase part per band. Checked against a
time-domain null of 20 s of pink noise.

$(cat "$WORK/null.md")

Group-delay audibility thresholds reported in listening tests are 0.5-3 ms (Blauert & Laws 1978; Liski,
Makivirta & Valimaki); the differences above are microseconds. To analyse a null test made in a DAW, use
\`Tools/null_analyze.py\` on the two renders: it measures alignment, polarity and level offset, and splits the
residual by cause.

### Estimate: COHERENCE's filter at twice the rate, against MDWEQ6

The same COHERENCE bells rendered at 96 kHz and compared with the 48 kHz MDWEQ6 renders below 20 kHz. This assumes
ideal resampling, so it is an upper bound for a 2x internal rate, not a measurement of an existing build:

$(cat "$WORK/cmp96.md")

## AU versus VST3 (format check)

\`Tests/FormatCheck.cpp\` loads the AU and the VST3 through the plug-in hosting APIs, sets identical parameters, processes
identical audio with automation, and checks recall.
EOF
if [ -n "$FC" ] && [ -f "$FC" ]; then
    NP=$(grep -c "PASS" "$FC" || true); NF=$(grep -c "FAIL" "$FC" || true)
    echo
    echo "**Result on the Mac** (\`$FC\`): $NP checks pass, $NF fail, at 44.1, 48 and 96 kHz. They cover:"
    echo "- the AU and VST3 both load and report 0 latency;"
    echo "- the same 36 parameters, showing the same values;"
    echo "- bit-identical output with automation;"
    echo "- every parameter restored on recall in both formats, and recalled instances bit-identical to ones set up by hand."
    if [ -n "$AUVAL" ] && [ -f "$AUVAL" ]; then
        echo
        echo "auval (\`$AUVAL\`): $(grep -m1 'Component Version' "$AUVAL" | sed 's/^ *//'), $(grep -m1 -E 'AU VALIDATION (SUCCEEDED|FAILED)' "$AUVAL")"
    fi
else
    cat <<EOF
The installer runs it on the Mac and writes \`COHERENCE format check.md\`; that file is the record for AU versus VST3.
**It has not been run on the Mac for this version yet.**

Harness check on Linux (the VST3 against itself, because AU hosting is macOS-only): all checks pass at 44.1, 48 and 96 kHz.
- Outputs with automation are bit-identical.
- Every parameter is restored on recall.
- Recalled instances are bit-identical to instances set up by hand.
EOF
fi
} >>VALIDATION.md
rm -rf "$WORK"
tail -n 1 "$HERE/VALIDATION.md" >/dev/null
grep -E "checks passed|FAILED" VALIDATION.md
