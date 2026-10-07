#!/usr/bin/env bash
set -euo pipefail

COMPILER="/c/Program Files/Windhawk/Compiler/bin/clang++.exe"
INCLUDE_DIR="/c/Program Files/Windhawk/Compiler/include"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/../tests/unit_tests.cpp"
OUT_DIR="$SCRIPT_DIR/../build"

mkdir -p "$OUT_DIR"

# -static: this test binary is a standalone exe (never loaded by Windhawk's
# engine), so it needs its own working libc++/libunwind at runtime. Without
# -static it dynamically links against libc++.dll/libunwind.dll, but
# Windhawk's copyCompilerLibs deliberately copies those under the names
# libc++.whl/libunwind.whl into its Mods folder for its OWN mod DLLs to
# load (see extension.js) -- it never places libc++.dll/libunwind.dll
# anywhere our test binary's DLL search path would find them. That is not
# a packaging defect to "fix"; it's how Windhawk's real mod loading works.
# -static sidesteps the whole question for this standalone test exe
# without touching how the real mod gets built or loaded (compile-check.sh
# and link-check.sh never link against this library pair).
"$COMPILER" -std=c++23 -target x86_64-w64-mingw32 \
  -DUNICODE -D_UNICODE \
  -static \
  -I "$INCLUDE_DIR" \
  "$SRC" -o "$OUT_DIR/unit_tests.exe"

"$OUT_DIR/unit_tests.exe"
