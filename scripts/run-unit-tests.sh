#!/usr/bin/env bash
set -euo pipefail

COMPILER="/c/Program Files/Windhawk/Compiler/bin/clang++.exe"
INCLUDE_DIR="/c/Program Files/Windhawk/Compiler/include"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/../tests/unit_tests.cpp"
OUT_DIR="$SCRIPT_DIR/../build"

mkdir -p "$OUT_DIR"

"$COMPILER" -std=c++23 -target x86_64-w64-mingw32 \
  -DUNICODE -D_UNICODE \
  -I "$INCLUDE_DIR" \
  "$SRC" -o "$OUT_DIR/unit_tests.exe"

"$OUT_DIR/unit_tests.exe"
