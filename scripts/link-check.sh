#!/usr/bin/env bash
set -euo pipefail

# Mirrors Windhawk's own mod build command (compileModInternal in
# extension.js: UI/resources/app/extensions/windhawk/dist/extension.js),
# so a failure here reproduces exactly what happens when the mod is
# loaded for real in the Windhawk app. compile-check.sh only does -c
# (compile, no link), which misses missing-library link errors entirely.
#
# Windhawk's real command passes no -I: clang auto-locates its bundled
# headers relative to its own binary path. This script passes -I
# explicitly instead, for robustness independent of that auto-detection.
#
# @compilerOptions below must be kept in sync with the metadata block at
# the top of SoundTrackAutoView.cpp.

COMPILER="/c/Program Files/Windhawk/Compiler/bin/clang++.exe"
INCLUDE_DIR="/c/Program Files/Windhawk/Compiler/include"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/../SoundTrackAutoView.cpp"
OUT_DIR="$SCRIPT_DIR/../build"

mkdir -p "$OUT_DIR"

ENGINE_LIB=$(find "/c/Program Files/Windhawk/Engine" -maxdepth 3 -path "*/64/windhawk.lib" | sort -V | tail -1)
if [[ -z "$ENGINE_LIB" ]]; then
  echo "Could not locate windhawk.lib under Windhawk's Engine directory." >&2
  exit 1
fi

"$COMPILER" -std=c++23 -O2 -shared -DUNICODE -D_UNICODE \
  -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 \
  -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD \
  '-DWH_MOD_ID=L"soundtrack-autoview"' '-DWH_MOD_VERSION=L"1.0"' \
  "$ENGINE_LIB" \
  -x c++ "$SRC" \
  -include windhawk_api.h \
  -target x86_64-w64-mingw32 \
  -Wl,--export-all-symbols \
  -o "$OUT_DIR/SoundTrackAutoView.dll" \
  -I "$INCLUDE_DIR" \
  -ld2d1 -ldwrite -ldwmapi -lwindowscodecs -lole32 -lruntimeobject -lshcore -lgdi32 -loleaut32

echo "Link check passed."
