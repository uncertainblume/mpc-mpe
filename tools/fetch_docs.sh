#!/bin/sh
# Downloads the reference material listed in docs/sources.md into third_party/ (not
# committed). Nothing downloaded is executed. Safe to re-run.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TP="$ROOT/third_party"
mkdir -p "$TP/akai-docs"

clone_at() {   # url, commit, folder
    if [ ! -d "$TP/$3/.git" ]; then git clone --quiet "$1" "$TP/$3"; fi
    git -C "$TP/$3" cat-file -e "$2^{commit}" 2>/dev/null || git -C "$TP/$3" fetch --quiet origin
    git -C "$TP/$3" checkout --quiet "$2"
    echo "ok  $3 @ $2"
}
clone_at https://github.com/inmusic-software/mpc-plugin-builder-sdk febd2ca5bd9eb16c4a412837196e145566bc981b mpc-plugin-builder-sdk
clone_at https://github.com/rogerlinndesign/linnstrument-firmware   02261136771f647491817553eb456b793e4f6cec linnstrument-firmware
clone_at https://github.com/open-mpc/mpc-vst-plugins                a409770a3b61402877ca384c97c40934104be2fe mpc-vst-plugins

get() {   # file, url
    if [ -s "$TP/akai-docs/$1" ]; then echo "ok  akai-docs/$1 (already there)"; return; fi
    if curl -fsSL --max-time 300 -o "$TP/akai-docs/$1" "$2"; then echo "ok  akai-docs/$1"
    else rm -f "$TP/akai-docs/$1"; echo "MISSING  $1  ($2)"; fi
}
get "MPC Standalone OS - User Guide - v3.4.pdf" "https://cdn.inmusicbrands.com/akai/MPC3-NI/MPC%20Standalone%20OS%20-%20User%20Guide%20-%20v3.4.pdf"
get "MPC 3.6 Release Notes.pdf"                 "https://cdn.inmusicbrands.com/Software/36/MPC%203.6%20Firmware%20Update.pdf"
get "MPC 3.7 Release Notes.pdf"                 "https://cdn.inmusicbrands.com/Software/37/MPC%203.7%20Update%20Release%20Notes.pdf"
