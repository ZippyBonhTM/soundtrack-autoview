#!/usr/bin/env bash
set -euo pipefail

COMPILER="/c/Program Files/Windhawk/Compiler/bin/clang++.exe"
INCLUDE_DIR="/c/Program Files/Windhawk/Compiler/include"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/../SoundTrackAutoView.cpp"
OUT_DIR="$SCRIPT_DIR/../build"

mkdir -p "$OUT_DIR"

"$COMPILER" -c -x c++ -std=c++23 -target x86_64-w64-mingw32 \
  -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 \
  -DNTDDI_VERSION=0x0A000008 -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD \
  -I "$INCLUDE_DIR" \
  -include windhawk_api.h \
  -Wno-pragma-pack -Wno-pragma-system-header-outside-header \
  "$SRC" -o "$OUT_DIR/SoundTrackAutoView.o"

echo "Compile check passed."
