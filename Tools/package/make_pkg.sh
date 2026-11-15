#!/bin/bash
# COHERENCE: build the macOS installer package (.pkg) for other people.
#
#   bash Tools/package/make_pkg.sh            # build universal AU + VST3, test, package
#   bash Tools/package/make_pkg.sh --no-test  # skip the test suite
#
# The plug-ins are built for Apple Silicon and Intel in one binary, signed ad hoc (no Apple Developer ID) and put in
# an unsigned package: macOS asks the user to approve it once (INSTALL.md). The package installs
#   /Library/Audio/Plug-Ins/Components/COHERENCE.component   (AU)
#   /Library/Audio/Plug-Ins/VST3/COHERENCE.vst3              (VST3)
#   /Library/Application Support/SSL/SSLPlugins/SSL360Link/COHERENCE.user   (optional SSL 360 Link profile)
# Output: dist/COHERENCE-<version>.pkg and dist/COHERENCE-<version>.pkg.sha256

set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
TEST=1; [ "${1:-}" = "--no-test" ] && TEST=0
say() { printf "\n\033[1;34m==> %s\033[0m\n" "$1"; }
fail() { printf "\n\033[1;31mProblem: %s\033[0m\n" "$1"; exit 1; }

[ "$(uname)" = "Darwin" ] || fail "This script is for macOS."
CMAKE="${CMAKE:-$(command -v cmake || true)}"
[ -n "$CMAKE" ] || fail "CMake 3.22 or later is needed (set CMAKE=/path/to/cmake)."
VERSION="$(sed -n 's/^project(Coherence VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$VERSION" ] || fail "No version in CMakeLists.txt."
BUILD="$ROOT/build-pkg"
DIST="$ROOT/dist"
JOBS="$(sysctl -n hw.ncpu)"

say "Building COHERENCE $VERSION for Apple Silicon and Intel"
TARGETS="Coherence_AU Coherence_VST3"; [ "$TEST" = 1 ] && TARGETS="$TARGETS CoherenceTests"
"$CMAKE" -S . -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCOHERENCE_BUILD_TESTS=$([ "$TEST" = 1 ] && echo ON || echo OFF)
# shellcheck disable=SC2086
"$CMAKE" --build "$BUILD" --config Release --parallel "$JOBS" --target $TARGETS

AU="$BUILD/Coherence_artefacts/Release/AU/COHERENCE.component"
VST3="$BUILD/Coherence_artefacts/Release/VST3/COHERENCE.vst3"
for b in "$AU" "$VST3"; do
    [ -d "$b" ] || fail "$(basename "$b") wasn't built."
    [ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$b/Contents/Info.plist")" = "$VERSION" ] \
        || fail "$(basename "$b") isn't version $VERSION. Delete build-pkg and run this again."
    BIN="$b/Contents/MacOS/COHERENCE"
    ARCHS="$(lipo -archs "$BIN")"
    case "$ARCHS" in *arm64*x86_64*|*x86_64*arm64*) ;; *) fail "$(basename "$b") is only built for: $ARCHS";; esac
    # Licences travel with the binaries: AGPLv3, the notice, and the third-party licences of what is compiled in.
    L="$b/Contents/Resources/Licenses"; rm -rf "$L"; mkdir -p "$L"
    cp LICENSE "$L/COHERENCE-LICENSE-AGPLv3.txt"; cp NOTICE.md "$L/NOTICE.md"
    cp licenses/JUCE-LICENSE.md "$L/JUCE-LICENSE.md"; cp licenses/Inter-OFL.txt "$L/Inter-OFL.txt"
    cp deps/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt "$L/VST3-SDK-LICENSE.txt"
    cp deps/JUCE/modules/juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt "$L/AudioUnitSDK-LICENSE.txt"
    codesign --force --deep --sign - "$b"
    codesign --verify --deep --strict "$b" || fail "$(basename "$b") doesn't verify after signing."
    echo "$(basename "$b"): $ARCHS, signed ad hoc"
done

if [ "$TEST" = 1 ]; then
    TESTS="$BUILD/CoherenceTests_artefacts/Release/CoherenceTests"
    say "Test suite (Apple Silicon)"
    "$TESTS" > "$BUILD/suite-arm64.txt" 2>&1 || { tail -20 "$BUILD/suite-arm64.txt"; fail "The test suite failed (Apple Silicon): $BUILD/suite-arm64.txt"; }
    tail -1 "$BUILD/suite-arm64.txt"
    if arch -x86_64 /usr/bin/true 2>/dev/null; then
        say "Test suite (Intel, under Rosetta)"
        arch -x86_64 "$TESTS" > "$BUILD/suite-x86_64.txt" 2>&1 || { tail -20 "$BUILD/suite-x86_64.txt"; fail "The test suite failed (Intel): $BUILD/suite-x86_64.txt"; }
        tail -1 "$BUILD/suite-x86_64.txt"
    else
        echo "Rosetta isn't installed, so the Intel code wasn't tested."
    fi
fi

say "Packaging"
STAGE="$BUILD/pkg-stage"
rm -rf "$STAGE"; mkdir -p "$STAGE/au" "$STAGE/vst3" "$STAGE/profile" "$STAGE/scripts-au" "$STAGE/resources" "$STAGE/pkgs"
ditto "$AU" "$STAGE/au/COHERENCE.component"
ditto "$VST3" "$STAGE/vst3/COHERENCE.vst3"
cp Tools/ssl360link/COHERENCE.user "$STAGE/profile/COHERENCE.user"
cat > "$STAGE/scripts-au/postinstall" <<'POST'
#!/bin/sh
# Make macOS notice the new Audio Unit.
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
POST
chmod 755 "$STAGE/scripts-au/postinstall"

# Bundles must install where they are meant to go, never "relocated" onto another copy found elsewhere on the disk.
component_plist() {
    pkgbuild --analyze --root "$1" "$2" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$2" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$2" >/dev/null 2>&1 || true
        /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$2"
        i=$((i + 1))
    done
}
component_plist "$STAGE/au" "$STAGE/au.plist"
component_plist "$STAGE/vst3" "$STAGE/vst3.plist"

pkgbuild --root "$STAGE/au" --component-plist "$STAGE/au.plist" --scripts "$STAGE/scripts-au" \
    --identifier com.phasesystems.coherence.pkg.au --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/Components" "$STAGE/pkgs/au.pkg"
pkgbuild --root "$STAGE/vst3" --component-plist "$STAGE/vst3.plist" \
    --identifier com.phasesystems.coherence.pkg.vst3 --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/VST3" "$STAGE/pkgs/vst3.pkg"
pkgbuild --root "$STAGE/profile" \
    --identifier com.phasesystems.coherence.pkg.profile --version "$VERSION" \
    --install-location "/Library/Application Support/SSL/SSLPlugins/SSL360Link" "$STAGE/pkgs/profile.pkg"

cp LICENSE "$STAGE/resources/LICENSE.txt"
sed "s/@VERSION@/$VERSION/g" Tools/package/resources/welcome.html > "$STAGE/resources/welcome.html"
cp Tools/package/resources/conclusion.html "$STAGE/resources/conclusion.html"
sed "s/@VERSION@/$VERSION/g" Tools/package/distribution.xml > "$STAGE/distribution.xml"

mkdir -p "$DIST"
OUT="$DIST/COHERENCE-$VERSION.pkg"
rm -f "$OUT"
productbuild --distribution "$STAGE/distribution.xml" --resources "$STAGE/resources" --package-path "$STAGE/pkgs" "$OUT"
(cd "$DIST" && shasum -a 256 "COHERENCE-$VERSION.pkg" > "COHERENCE-$VERSION.pkg.sha256")
say "Done: $OUT"
cat "$OUT.sha256"
