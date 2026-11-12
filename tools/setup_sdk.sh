#!/bin/sh
# Fetches Akai's MPC Plugin SDK (MIT) at the commit this project was written against,
# into third_party/mpc-plugin-builder-sdk (not committed). Run once; safe to re-run.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK_URL=https://github.com/inmusic-software/mpc-plugin-builder-sdk
SDK_COMMIT=febd2ca5bd9eb16c4a412837196e145566bc981b
DEST="$ROOT/third_party/mpc-plugin-builder-sdk"

if [ ! -d "$DEST/.git" ]; then
    mkdir -p "$ROOT/third_party"
    git clone --quiet "$SDK_URL" "$DEST"
fi
if ! git -C "$DEST" cat-file -e "$SDK_COMMIT^{commit}" 2>/dev/null; then
    git -C "$DEST" fetch --quiet origin
fi
git -C "$DEST" checkout --quiet "$SDK_COMMIT"
echo "MPC Plugin SDK ready at $DEST ($SDK_COMMIT)"
