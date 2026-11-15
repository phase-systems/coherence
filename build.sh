#!/bin/bash
# COHERENCE (Phase Systems) - build and install the AU and VST3 on macOS.
# Run from Terminal, in the source folder:  bash build.sh

set -euo pipefail
cd "$(dirname "$0")"
HERE="$(pwd)"

say() { printf "\n\033[1;34m==> %s\033[0m\n" "$1"; }
fail() { printf "\n\033[1;31mProblem: %s\033[0m\n" "$1"; exit 1; }
PROBLEMS=0
problem() { printf "\033[1;31mProblem: %s\033[0m\n" "$1"; PROBLEMS=$((PROBLEMS + 1)); }

[ "$(uname)" = "Darwin" ] || fail "This script is for macOS."

# 1. Apple's Command Line Tools (compiler). No full Xcode needed.
say "Checking Apple Command Line Tools"
if ! xcode-select -p >/dev/null 2>&1 || ! xcrun --find clang++ >/dev/null 2>&1; then
    echo "They're not installed yet. A popup will appear: click Install, wait for it to finish,"
    echo "then run this script again."
    xcode-select --install || true
    exit 1
fi
echo "OK: $(xcode-select -p)"

# 2. CMake 3.22+. Uses one you already have,
#    otherwise installs a private copy inside this folder.
say "Checking CMake"
cmake_ok() {
    local v major minor
    v="$("$1" --version 2>/dev/null | head -1 | awk '{print $3}')"
    major="${v%%.*}"; minor="${v#*.}"; minor="${minor%%.*}"
    [ -n "$major" ] && { [ "$major" -gt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -ge 22 ]; }; }
}
CMAKE=""
for c in "$(command -v cmake 2>/dev/null || true)" \
         "$HERE/.build-tools/bin/cmake"; do
    if [ -n "$c" ] && [ -x "$c" ] && cmake_ok "$c"; then CMAKE="$c"; break; fi
done
if [ -z "$CMAKE" ]; then
    echo "Installing CMake into $HERE/.build-tools (one time, needs internet)..."
    /usr/bin/python3 -m venv "$HERE/.build-tools"
    "$HERE/.build-tools/bin/python3" -m pip install --quiet --upgrade pip
    "$HERE/.build-tools/bin/python3" -m pip install --quiet cmake
    CMAKE="$HERE/.build-tools/bin/cmake"
fi
echo "OK: $("$CMAKE" --version | head -1)"

# 3. Build the AU and VST3 for this Mac.
JOBS="$(sysctl -n hw.ncpu)"
say "Building COHERENCE (a few minutes the first time)"
"$CMAKE" -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCOHERENCE_BUILD_FORMAT_CHECK=ON
"$CMAKE" --build build --config Release --parallel "$JOBS" --target Coherence_AU Coherence_VST3

AU_SRC="build/Coherence_artefacts/Release/AU/COHERENCE.component"
VST3_SRC="build/Coherence_artefacts/Release/VST3/COHERENCE.vst3"
[ -d "$AU_SRC" ] || fail "The AU wasn't built. Scroll up for the first error."
[ -d "$VST3_SRC" ] || fail "The VST3 wasn't built. Scroll up for the first error."
VERSION="$(sed -n 's/^project(Coherence VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
for b in "$AU_SRC" "$VST3_SRC"; do
    [ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$b/Contents/Info.plist")" = "$VERSION" ] \
        || fail "$(basename "$b") was not built as version $VERSION. Delete the build folder and run this again."
    codesign --force --deep --sign - "$b" >/dev/null 2>&1 || true
done
echo "Built version $VERSION."

# 4. Install into your user plug-in folders.
say "Installing"
PREVIOUS_VERSION=""
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
SYSTEM_AU_DIR="/Library/Audio/Plug-Ins/Components"
USER_VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
SYSTEM_VST3_DIR="/Library/Audio/Plug-Ins/VST3"
# SSL 360 Link scans only the system VST3 folder, so the VST3 goes there when it can be written. Exactly one copy is
# installed: a second copy is how stale builds ended up being hosted. DAWs scan both folders.
if [ -d "$SYSTEM_VST3_DIR" ] && [ -w "$SYSTEM_VST3_DIR" ]; then
    VST3_DIR="$SYSTEM_VST3_DIR"
else
    VST3_DIR="$USER_VST3_DIR"
    echo "Note: $SYSTEM_VST3_DIR can't be written, so the VST3 goes to your user folder. SSL 360 Link won't see it there."
    if [ -d "$SYSTEM_VST3_DIR/COHERENCE.vst3" ]; then
        echo "Warning: an older COHERENCE.vst3 remains in $SYSTEM_VST3_DIR. Hosts may load it instead of this build."
    fi
fi
mkdir -p "$AU_DIR" "$VST3_DIR"
# Keep the build that is installed now, so it can be put back. Saved outside the plug-in folders (hosts don't
# scan it), in "PHASE SYSTEMS/Installed builds/COHERENCE <version>". A version already saved is not overwritten.
for b in "$AU_DIR/COHERENCE.component" "$SYSTEM_AU_DIR/COHERENCE.component" "$USER_VST3_DIR/COHERENCE.vst3" "$SYSTEM_VST3_DIR/COHERENCE.vst3"; do
    if [ -d "$b" ]; then
        V="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$b/Contents/Info.plist" 2>/dev/null || echo unknown)"
        PREVIOUS_VERSION="$V"
        KEEP="$HERE/../Installed builds/COHERENCE $V"
        if [ ! -d "$KEEP/$(basename "$b")" ]; then
            mkdir -p "$KEEP" && cp -R "$b" "$KEEP/" && echo "Kept the installed $(basename "$b") $V in: Installed builds/COHERENCE $V"
        fi
    fi
done
# An older system-wide AU would shadow the new one, as a stale system-wide VST3 once did. It is kept above.
if [ -d "$SYSTEM_AU_DIR/COHERENCE.component" ] && ! rm -rf "$SYSTEM_AU_DIR/COHERENCE.component" 2>/dev/null; then
    problem "An older COHERENCE.component remains in $SYSTEM_AU_DIR and can't be removed. Hosts may load it."
fi
rm -rf "$AU_DIR/COHERENCE.component" "$USER_VST3_DIR/COHERENCE.vst3" "$VST3_DIR/COHERENCE.vst3"
cp -R "$AU_SRC" "$AU_DIR/"
cp -R "$VST3_SRC" "$VST3_DIR/"
for b in "$AU_DIR/COHERENCE.component" "$VST3_DIR/COHERENCE.vst3"; do
    xattr -dr com.apple.quarantine "$b" 2>/dev/null || true
done
echo "AU:   $AU_DIR/COHERENCE.component"
echo "VST3: $VST3_DIR/COHERENCE.vst3"

# The installed files must be exactly the ones just built.
diff -rq "$AU_SRC" "$AU_DIR/COHERENCE.component" >/dev/null || problem "The installed AU differs from the one just built."
diff -rq "$VST3_SRC" "$VST3_DIR/COHERENCE.vst3" >/dev/null || problem "The installed VST3 differs from the one just built."
echo "Checked: the installed files are identical to the build."

# The 360 Link profile that goes with this build. The one in use is kept first, under its checksum, so no copy is lost.
PROFILE_DIR="/Library/Application Support/SSL/SSLPlugins/SSL360Link"
PROFILE_SRC="Tools/ssl360link/COHERENCE.user"
if [ -d "$PROFILE_DIR" ] && [ -f "$PROFILE_SRC" ]; then
    if [ -f "$PROFILE_DIR/COHERENCE.user" ] && ! cmp -s "$PROFILE_SRC" "$PROFILE_DIR/COHERENCE.user"; then
        SUM="$(shasum -a 256 "$PROFILE_DIR/COHERENCE.user" | cut -c1-8)"
        KEEP="$HERE/../Installed builds/360 Link profiles"
        mkdir -p "$KEEP"
        cp -n "$PROFILE_DIR/COHERENCE.user" "$KEEP/COHERENCE-$SUM.user" || true
        echo "Kept the previous 360 Link profile as: Installed builds/360 Link profiles/COHERENCE-$SUM.user"
    fi
    if cp "$PROFILE_SRC" "$PROFILE_DIR/COHERENCE.user" 2>/dev/null; then
        echo "360 Link profile: $PROFILE_DIR/COHERENCE.user"
    else
        problem "The 360 Link profile couldn't be written to $PROFILE_DIR."
    fi
else
    echo "SSL 360 Link isn't installed here, so no profile was installed."
fi

# Make macOS notice the new Audio Unit.
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

# 5. Validate the AU the same way Logic and Ableton do.
say "Validating the Audio Unit"
sleep 2
if auval -v aufx Cohr Phsy > build/auval.log 2>&1 && grep -q "AU VALIDATION SUCCEEDED" build/auval.log; then
    echo "AU validation succeeded."
    grep -q "Component Version: $VERSION " build/auval.log || problem "auval didn't see version $VERSION (details in $HERE/build/auval.log)."
else
    problem "auval didn't pass (details in $HERE/build/auval.log)."
fi

# 6. AU / VST3 consistency: load both like a DAW, compare their audio, automation and recall.
say "Comparing the AU and VST3"
REPORT="$HERE/../COHERENCE format check.md"
if [ -f "$REPORT" ]; then
    # The previous report is moved next to the build it describes, not overwritten.
    KEEP="$HERE/../Installed builds/COHERENCE ${PREVIOUS_VERSION:-unknown}"
    mkdir -p "$KEEP"
    [ -f "$KEEP/COHERENCE format check.md" ] || cp "$REPORT" "$KEEP/COHERENCE format check.md"
fi
if "$CMAKE" --build build --config Release --parallel "$JOBS" --target CoherenceFormatCheck >build/format-check-build.log 2>&1; then
    CHECK="build/CoherenceFormatCheck_artefacts/Release/CoherenceFormatCheck"
    if "$CHECK" "$REPORT" "$VST3_DIR/COHERENCE.vst3" "AudioUnit:Effects/aufx,Cohr,Phsy" >build/format-check.log 2>&1; then
        echo "AU and VST3 match (report: $REPORT)."
    else
        problem "The format check found differences or couldn't run. Report: $REPORT (log: $HERE/build/format-check.log)"
    fi
else
    problem "The format check didn't build (log: $HERE/build/format-check-build.log)."
fi

if [ "$PROBLEMS" -gt 0 ]; then
    say "Installed version $VERSION, with $PROBLEMS problem(s) listed above"
    exit 1
fi
say "Done: COHERENCE $VERSION is installed and passed its checks"
echo "In Ableton: Preferences > Plug-Ins > Rescan, then look for COHERENCE under Plug-Ins > Phase Systems."
echo "In SSL 360: quit and reopen it, then rescan in 360 Link, so that it hosts this build and reads its profile."
