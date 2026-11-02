#!/usr/bin/env bash
# End-to-end check: render SuperBand through scsynth (non-real-time) and verify
# pitch content, per-channel independence and reported latency.
#
# Usage: tests/verify_in_sc.sh
# Requires: SuperCollider installed (sclang/scsynth), a built SuperBand.scx and
# the SuperBand class file. Set SC_APP to the SuperCollider.app path if it lives
# somewhere unusual.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
sc_app="${SC_APP:-/Applications/SuperCollider.app}"
sclang="$sc_app/Contents/MacOS/sclang"
plugin="$root/build/plugins/SuperBand.scx"

if [ ! -x "$sclang" ]; then
    echo "sclang not found at $sclang (set SC_APP)" >&2
    exit 1
fi
if [ ! -f "$plugin" ]; then
    echo "plugin not built: $plugin" >&2
    exit 1
fi

# Isolated HOME so the user's SuperCollider extensions are untouched.
export HOME="${HOME_OVERRIDE:-$(mktemp -d)}"
ext="$HOME/Library/Application Support/SuperCollider/Extensions/SuperBand"
mkdir -p "$ext"
cp "$root/sc/classes/SuperBand.sc" "$ext/"
cp "$plugin" "$ext/"

tmp="$(mktemp -d)"
status=0

render() { # ratio pitch freq out
    SB_RATIO="$1" SB_PITCH="$2" SB_FREQ="${3:-440}" SB_OUT="$4" \
        timeout 120 "$sclang" "$here/render_nrt.scd" 2>&1 |
        grep -E "^rendered|ERROR|error" || true
}

check() { # label expected_freq reject_freq expected_latency wav
    echo
    echo "=== $1 ==="
    if ! python3 "$here/analyse_nrt.py" "$5" --expect-freq "$2" --reject-freq "$3" \
            --expect-latency "$4"; then
        status=1
    fi
}

render 1 1    440 "$tmp/identity.wav"
render 1 2    440 "$tmp/up-octave.wav"
render 1 0.5  440 "$tmp/down-octave.wav"

check "identity (ratio 1, pitch 1)" 440 880 1024 "$tmp/identity.wav"
check "octave up (pitch 2)"         880 440 512  "$tmp/up-octave.wav"
check "octave down (pitch 0.5)"     220 440 2048 "$tmp/down-octave.wav"

# Per-channel: two frequencies in, one stretcher per channel via expansion.
SB_STEREO=1 SB_RATIO=1 SB_PITCH=1.5 SB_FREQ=440 SB_FREQ2=660 SB_OUT="$tmp/stereo.wav" \
    timeout 120 "$sclang" "$here/render_nrt.scd" 2>&1 |
    grep -E "^rendered|ERROR|error" || true
echo
echo "=== stereo, per-channel independence (pitch 1.5) ==="
if ! python3 "$here/analyse_nrt.py" "$tmp/stereo.wav" --stereo \
        --expect-freq 660 --reject-freq 440 --expect-freq2 990; then
    status=1
fi

echo
if [ "$status" -eq 0 ]; then
    echo "all SuperCollider render checks passed"
else
    echo "SuperCollider render checks FAILED"
fi
exit "$status"
