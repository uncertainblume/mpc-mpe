#!/bin/sh
# Desktop tests for the MPE routing logic. Needs only a C++17 compiler.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/tests/build"
mkdir -p "$OUT"
CXX=${CXX:-c++}
"$CXX" -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I"$ROOT/shared" \
    "$ROOT/tests/test_router.cpp" -o "$OUT/test_router" -ldl
"$OUT/test_router"
"$CXX" -std=c++17 -Wall -Wextra -I"$ROOT/shared" "$ROOT/tests/test_alsa_abi.cpp" -o "$OUT/test_alsa_abi" -ldl
"$OUT/test_alsa_abi"
