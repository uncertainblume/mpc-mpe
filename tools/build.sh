#!/bin/sh
# Builds the plugins for the MPC (arm32 + arm64) and for this computer, using Akai's SDK.
#   tools/build.sh                 build every plugin
#   tools/build.sh MPESynth        build one
#   tools/build.sh MPESynth --target native     extra arguments go to the SDK's build.py
# Output: plugins/<Name>/build/"MPE Tools - VST - <Plugin Name>"/  (copy that folder into
# "VST2 Plugins" on a drive).
#
# The SDK's build.py downloads the Zig compiler on first use (pip package "ziglang").
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
[ -f "$SDK/tools/build.py" ] || "$ROOT/tools/setup_sdk.sh"

if [ $# -gt 0 ] && [ -d "$ROOT/plugins/$1" ]; then NAMES=$1; shift; else NAMES="MPEBridge MPESynth"; fi
for name in $NAMES; do
    echo "== $name"
    python3 "$SDK/tools/build.py" "$ROOT/plugins/$name" "$@"
done
