#!/bin/bash
# COHERENCE: make the public copy of the source from a git revision, without the internal notes and records.
#
#   bash Tools/package/export_public.sh <revision> <empty target folder>
#
# Kept out: internal working notes, the old source checkpoints, the Pro-C 3 characterisation tools and data, the
# owner's own installer, and most measurement records (a few summaries are kept because the README points at them).
set -euo pipefail
REV="${1:?revision}"; OUT="${2:?target folder}"
cd "$(dirname "$0")/../.."
[ ! -e "$OUT" ] || [ -z "$(ls -A "$OUT")" ] || { echo "$OUT isn't empty." >&2; exit 1; }
mkdir -p "$OUT"
git archive "$REV" | tar -x -C "$OUT"
cd "$OUT"
rm -rf CLAUDE.md AGENTS.md RESUME.md RELEASE-PLAN.md COMPRESSOR-TESTING.md VALIDATION.md DESIGN.md references.sha256 \
       opt3_bell_B1.log opt3_hs_B1.log checkpoints Tools/proc3 Tools/installer "Tools/reference kit/HOW TO RENDER.md"
KEEP="mac-results/weiss-model/weiss_compare.md mac-results/v0.5.0/compare.md mac-results/v0.5.2/vst3-parameters.txt mac-results/gui-preview/COHERENCE-preview-A-default.png mac-results/gui-preview/COHERENCE-themes-side-by-side.png"
TMP="$(mktemp -d)"
for k in $KEEP; do [ -e "$k" ] && { mkdir -p "$TMP/$(dirname "$k")"; cp -R "$k" "$TMP/$k"; }; done
rm -rf mac-results
[ -d "$TMP/mac-results" ] && mv "$TMP/mac-results" mac-results
rm -rf "$TMP"
echo "Public copy of $REV in $OUT"
