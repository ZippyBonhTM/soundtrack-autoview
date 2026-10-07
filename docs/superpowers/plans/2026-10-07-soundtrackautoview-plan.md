# SoundTrackAutoView Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a single-file Windhawk mod that shows a native-styled "Now Playing" panel near the system tray whenever the user presses a media key (Play/Pause, Next, Previous), auto-hiding after a configurable duration, with theming and clickable transport controls.

**Architecture:** A `WH_KEYBOARD_LL` keyboard hook (installed in `explorer.exe`) detects media key presses and posts a trigger to a dedicated OSD thread. That thread owns a WinRT `GlobalSystemMediaTransportControlsSessionManager` (data source for track/art/status), a layered `WS_POPUP` window rendered with Direct2D + DirectWrite (composited via `UpdateLayeredWindow`, with a DWM Mica/acrylic backdrop on Windows 11), and a small pure state machine that governs show/hide timing, the post-trigger SMTC "correction window", and hover-pause behavior.

**Tech Stack:** C++23, compiled with Windhawk's bundled Clang (`C:\Program Files\Windhawk\Compiler\bin\clang++.exe`, target `x86_64-w64-mingw32`). C++/WinRT for SMTC. Direct2D/DirectWrite/WIC for rendering. DWM API for rounded corners and backdrop blur.

**Spec:** [docs/superpowers/specs/2026-10-07-soundtrackautoview-design.md](../specs/2026-10-07-soundtrackautoview-design.md)

## Global Constraints

- Single-file Windhawk mod, `@include explorer.exe` (spec §3).
- Compiled with Windhawk's bundled Clang for target `x86_64-w64-mingw32`, `-std=c++23` (verified against `compile_flags.txt` in the Windhawk install and a working smoke-test compile — see Task 1).
- Trigger is `WH_KEYBOARD_LL` on `VK_MEDIA_PLAY_PAUSE` / `VK_MEDIA_NEXT_TRACK` / `VK_MEDIA_PREV_TRACK` / `VK_MEDIA_STOP` key-down only (spec §5). SMTC changes outside the ~1s post-trigger correction window must never trigger the panel (spec §6).
- Fade in ~150ms, fade out ~200ms (spec §7).
- Panel: `WS_POPUP`, layered, topmost, tool-window (excluded from taskbar/Alt-Tab), anchored to the bottom-right of the work area of the monitor hosting the primary taskbar's notification area (spec §7).
- Rounded corners via `DWMWA_WINDOW_CORNER_PREFERENCE`; backdrop via `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW` where supported, graceful degrade otherwise (spec §7).
- Settings and defaults: `duration=4`, `theme=auto`, `customBackgroundColor=#2C2C2C`, `customTextColor=#FFFFFF`, `customAccentColor=#0078D4` (spec §9).
- No active media session → panel must never be created/shown (spec §10).

## Review Focus

1. A media command fires with no active media session → the panel must never appear. Owned by Task 7 (`ShouldShowPanel`), tested there.
2. Malformed settings (invalid hex color, out-of-range duration, unrecognized theme name) must not crash and must fall back to sane defaults. Owned by Task 2, tested there.
3. Rapid repeated key presses must coalesce into one updated panel, resetting a single hide timer, never stacking windows or replaying the fade-in. Owned by Task 4, tested there.
4. An app's SMTC update arriving after the ~1s correction window must be ignored, not silently applied indefinitely. Owned by Task 4, tested there.
5. Panel position must stay correctly anchored across DPI scaling and multi-monitor offsets — never drifting off-screen or assuming the monitor origin is (0,0). Owned by Task 3, tested there.

---

## Environment notes (read before starting)

- Windhawk is installed at `C:\Program Files\Windhawk`. Its bundled compiler is Clang 20 targeting `x86_64-w64-mingw32`, at `C:\Program Files\Windhawk\Compiler\bin\clang++.exe`, with all needed headers (WinRT `winrt/Windows.Media.Control.h`, Direct2D, DirectWrite, DWM, WIC, `windhawk_api.h`) under `C:\Program Files\Windhawk\Compiler\include`, and import libraries (`libd2d1.a`, `libdwrite.a`, `libdwmapi.a`, `libwindowscodecs.a`, `libole32.a`, `libruntimeobject.a`, `libshcore.a`) under `C:\Program Files\Windhawk\Compiler\x86_64-w64-mingw32\lib`. This was verified working in this session with a real smoke-test compile.
- Because a published Windhawk mod must be a single `.cpp` file, this plan keeps **one** source file (`SoundTrackAutoView.cpp`) for the whole project. Pure, OS-independent logic (theme/settings parsing, layout math, timing state machine) lives in an unguarded section at the top of the file. Everything that depends on the Windhawk engine or real OS state (hook installation, WinRT/SMTC, the window, rendering) is wrapped in `#ifndef SOUNDTRACK_AUTOVIEW_TEST_BUILD`.
- `tests/unit_tests.cpp` does `#define SOUNDTRACK_AUTOVIEW_TEST_BUILD` then `#include "../SoundTrackAutoView.cpp"`, giving it access to the pure-logic section while the guarded section disappears, so the test binary never needs the Windhawk engine or Windhawk-only functions to link and run standalone.
- There is no automated way to verify the mod's final *link* step (the `Wh_*` engine functions are resolved by Windhawk's runtime at mod-load time, not by a standalone linker). `scripts/compile-check.sh` therefore does a `-c` (compile-only) check — it catches the large majority of real bugs (type errors, API misuse, missing includes) but not link-time issues. Those surface only when the mod is actually loaded in the Windhawk app, which is why several tasks end with a manual load-and-check step.
- To manually load the mod into Windhawk during a task: open the Windhawk app, use its "Create a new mod" / local-mod editor (exact wording may vary slightly by version — look for an option to create or edit a mod from source rather than installing one from the online repository), paste the full current contents of `SoundTrackAutoView.cpp`, save, and enable it. View its log output via the mod's "Logs" view in the Windhawk app (or the tray icon's logs option). Disable/remove the mod after each check unless a later task says to keep it enabled.

---

### Task 1: Project scaffold and build tooling

**Files:**
- Create: `SoundTrackAutoView.cpp`
- Create: `scripts/compile-check.sh`
- Create: `scripts/run-unit-tests.sh`
- Create: `tests/unit_tests.cpp`
- Create: `.gitignore`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: the `SOUNDTRACK_AUTOVIEW_TEST_BUILD` convention; the compile-check and unit-test scripts every later task reuses verbatim.

- [ ] **Step 1: Create the mod skeleton**

```cpp
// ==WindhawkMod==
// @id              soundtrack-autoview
// @name            SoundTrackAutoView
// @description     Shows a native-style "Now Playing" panel when you press a media key (play/pause/next/previous)
// @version         1.0
// @author          zippy
// @include         explorer.exe
// @compilerOptions -ld2d1 -ldwrite -ldwmapi -lwindowscodecs -lole32 -lruntimeobject -lshcore
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# SoundTrackAutoView

Shows a small panel, styled like Windows 11's native "Now Playing" flyout,
whenever you press a media key (Play/Pause, Next, Previous) on your
keyboard, headset, or remote. The panel shows the current track's art,
title, artist and playback state, and lets you click its buttons to
control playback.

The panel only appears for an actual media key command — it will not
appear when you pause/skip from inside the player app's own UI, or when
a track changes automatically (e.g. playlist autoplay).

Note: media commands sent by third-party keyboard macro software are
detected as long as that software simulates the real media keystroke
(the standard, most compatible way such software implements "send media
key" — this is also what Windows' own native flyout relies on).
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- duration: 4
  $name: Display duration (seconds)
  $description: >-
    How long the panel stays visible after a media command, in seconds.
- theme: auto
  $name: Theme
  $options:
  - auto: Automatic (match Windows light/dark mode)
  - light: Light
  - dark: Dark
  - custom: Custom colors
- customBackgroundColor: "#2C2C2C"
  $name: Custom background color
  $description: >-
    Used only when Theme is set to Custom. Format: #RRGGBB or #RRGGBBAA.
- customTextColor: "#FFFFFF"
  $name: Custom text color
  $description: >-
    Used only when Theme is set to Custom. Format: #RRGGBB or #RRGGBBAA.
- customAccentColor: "#0078D4"
  $name: Custom accent color
  $description: >-
    Used only when Theme is set to Custom. Format: #RRGGBB or #RRGGBBAA.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <string>

// ---------------------------------------------------------------------------
// Pure logic (unit-tested via tests/unit_tests.cpp, which #includes this
// file with SOUNDTRACK_AUTOVIEW_TEST_BUILD defined). Code in this section
// must not call any Wh_* API or touch real OS state directly.
// ---------------------------------------------------------------------------

// (pure-logic helpers added in later tasks go here)

// ---------------------------------------------------------------------------
// Windhawk mod entry points and OS-integration code. Excluded from the unit
// test build since they depend on the Windhawk engine (Wh_* functions) and
// real OS hooks, neither of which exist in a standalone test executable.
// ---------------------------------------------------------------------------
#ifndef SOUNDTRACK_AUTOVIEW_TEST_BUILD

BOOL Wh_ModInit() {
    Wh_Log(L"SoundTrackAutoView: init");
    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"SoundTrackAutoView: uninit");
}

#endif  // SOUNDTRACK_AUTOVIEW_TEST_BUILD
```

- [ ] **Step 2: Create the compile-check script**

```bash
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
```

- [ ] **Step 3: Run the compile-check script, confirm it passes**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.` printed, exit code 0.

- [ ] **Step 4: Create the initial unit test file**

```cpp
#define SOUNDTRACK_AUTOVIEW_TEST_BUILD
#include "../SoundTrackAutoView.cpp"

#include <cstdio>

static int g_testsRun = 0;
static int g_testsFailed = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        g_testsRun++;                                                  \
        if (!(cond)) {                                                 \
            g_testsFailed++;                                           \
            wprintf(L"FAIL: %hs:%d: %hs\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

static void Test_HarnessSmokeTest() {
    CHECK(1 + 1 == 2);
}

int main() {
    Test_HarnessSmokeTest();

    wprintf(L"\n%d/%d tests passed\n", g_testsRun - g_testsFailed, g_testsRun);
    return g_testsFailed == 0 ? 0 : 1;
}
```

- [ ] **Step 5: Create the unit-test runner script**

```bash
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
```

- [ ] **Step 6: Run the unit tests, confirm they pass**

Run: `bash scripts/run-unit-tests.sh`
Expected: `1/1 tests passed` printed, exit code 0.

- [ ] **Step 7: Add a .gitignore for build output**

```
build/
```

- [ ] **Step 8: Commit**

```bash
git add SoundTrackAutoView.cpp scripts/compile-check.sh scripts/run-unit-tests.sh tests/unit_tests.cpp .gitignore
git commit -m "Add Windhawk mod scaffold with compile-check and unit test tooling"
```

---

### Task 2: Theme and settings parsing (pure logic)

**Files:**
- Modify: `SoundTrackAutoView.cpp` (pure-logic section)
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: nothing beyond `<string>` (already included).
- Produces: `struct RgbaColor`, `enum class ThemeMode { Auto, Light, Dark, Custom }`, `bool TryParseHexColor(const std::wstring&, RgbaColor&)`, `ThemeMode ParseThemeMode(const std::wstring&)`, `struct ThemePalette { RgbaColor background, text, textSecondary, accent; }`, `const ThemePalette kLightPalette`, `const ThemePalette kDarkPalette`, `ThemePalette ResolvePalette(ThemeMode, bool systemIsDarkMode, const ThemePalette& customPalette)`, `struct ModSettings { int durationSeconds; ThemeMode theme; RgbaColor customBackground, customText, customAccent; }`, `int ClampDurationSeconds(int)`, `ModSettings BuildSettings(int rawDuration, const std::wstring& rawTheme, const std::wstring& rawBgHex, const std::wstring& rawTextHex, const std::wstring& rawAccentHex)`. Used by Task 5 (settings wiring) and Task 8 (rendering).

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit_tests.cpp` (before `int main()`):

```cpp
static void Test_TryParseHexColor_ValidSixDigit() {
    RgbaColor color{};
    CHECK(TryParseHexColor(L"#2C2C2C", color));
    CHECK(color.r > 0.172f && color.r < 0.174f);
    CHECK(color.a == 1.0f);
}

static void Test_TryParseHexColor_ValidEightDigitWithAlpha() {
    RgbaColor color{};
    CHECK(TryParseHexColor(L"#FFFFFF80", color));
    CHECK(color.r == 1.0f);
    CHECK(color.a > 0.49f && color.a < 0.51f);
}

static void Test_TryParseHexColor_MissingHash() {
    RgbaColor color{};
    CHECK(!TryParseHexColor(L"2C2C2C", color));
}

static void Test_TryParseHexColor_WrongLength() {
    RgbaColor color{};
    CHECK(!TryParseHexColor(L"#2C2C2", color));
}

static void Test_TryParseHexColor_InvalidHexChars() {
    RgbaColor color{};
    CHECK(!TryParseHexColor(L"#ZZZZZZ", color));
}

static void Test_ParseThemeMode_RecognizedValues() {
    CHECK(ParseThemeMode(L"light") == ThemeMode::Light);
    CHECK(ParseThemeMode(L"dark") == ThemeMode::Dark);
    CHECK(ParseThemeMode(L"custom") == ThemeMode::Custom);
    CHECK(ParseThemeMode(L"auto") == ThemeMode::Auto);
}

static void Test_ParseThemeMode_UnknownDefaultsToAuto() {
    CHECK(ParseThemeMode(L"something-else") == ThemeMode::Auto);
}

static void Test_ResolvePalette_AutoFollowsSystemMode() {
    ThemePalette custom{};
    CHECK(ResolvePalette(ThemeMode::Auto, true, custom).background.r ==
          kDarkPalette.background.r);
    CHECK(ResolvePalette(ThemeMode::Auto, false, custom).background.r ==
          kLightPalette.background.r);
}

static void Test_ResolvePalette_LightAndDarkIgnoreSystemMode() {
    ThemePalette custom{};
    CHECK(ResolvePalette(ThemeMode::Light, true, custom).background.r ==
          kLightPalette.background.r);
    CHECK(ResolvePalette(ThemeMode::Dark, false, custom).background.r ==
          kDarkPalette.background.r);
}

static void Test_ResolvePalette_CustomReturnsSuppliedPalette() {
    ThemePalette custom{};
    custom.background = RgbaColor{0.1f, 0.2f, 0.3f, 1.0f};
    ThemePalette resolved = ResolvePalette(ThemeMode::Custom, true, custom);
    CHECK(resolved.background.r == 0.1f);
    CHECK(resolved.background.g == 0.2f);
}

static void Test_ClampDurationSeconds_ClampsToRange() {
    CHECK(ClampDurationSeconds(0) == 1);
    CHECK(ClampDurationSeconds(-5) == 1);
    CHECK(ClampDurationSeconds(999) == 30);
    CHECK(ClampDurationSeconds(10) == 10);
}

static void Test_BuildSettings_ValidValuesApplied() {
    ModSettings settings =
        BuildSettings(7, L"dark", L"#111111", L"#222222", L"#333333");
    CHECK(settings.durationSeconds == 7);
    CHECK(settings.theme == ThemeMode::Dark);
    CHECK(settings.customBackground.r > 0.06f && settings.customBackground.r < 0.07f);
}

static void Test_BuildSettings_MalformedHexFallsBackToDefault() {
    ModSettings defaults;
    ModSettings settings =
        BuildSettings(4, L"custom", L"not-a-color", L"#FFFFFF", L"#0078D4");
    CHECK(settings.customBackground.r == defaults.customBackground.r);
    CHECK(settings.customBackground.g == defaults.customBackground.g);
}

static void Test_BuildSettings_UnknownThemeDefaultsToAuto() {
    ModSettings settings =
        BuildSettings(4, L"not-a-theme", L"#2C2C2C", L"#FFFFFF", L"#0078D4");
    CHECK(settings.theme == ThemeMode::Auto);
}
```

Add the corresponding calls inside `main()`, above the summary `wprintf`:

```cpp
    Test_TryParseHexColor_ValidSixDigit();
    Test_TryParseHexColor_ValidEightDigitWithAlpha();
    Test_TryParseHexColor_MissingHash();
    Test_TryParseHexColor_WrongLength();
    Test_TryParseHexColor_InvalidHexChars();
    Test_ParseThemeMode_RecognizedValues();
    Test_ParseThemeMode_UnknownDefaultsToAuto();
    Test_ResolvePalette_AutoFollowsSystemMode();
    Test_ResolvePalette_LightAndDarkIgnoreSystemMode();
    Test_ResolvePalette_CustomReturnsSuppliedPalette();
    Test_ClampDurationSeconds_ClampsToRange();
    Test_BuildSettings_ValidValuesApplied();
    Test_BuildSettings_MalformedHexFallsBackToDefault();
    Test_BuildSettings_UnknownThemeDefaultsToAuto();
```

- [ ] **Step 2: Run tests, verify they fail to compile (the symbols don't exist yet)**

Run: `bash scripts/run-unit-tests.sh`
Expected: compile error, e.g. `use of undeclared identifier 'RgbaColor'`.

- [ ] **Step 3: Implement the pure logic**

Replace the `// (pure-logic helpers added in later tasks go here)` line in `SoundTrackAutoView.cpp` with:

```cpp
struct RgbaColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

enum class ThemeMode {
    Auto,
    Light,
    Dark,
    Custom,
};

inline bool TryParseHexColor(const std::wstring& hex, RgbaColor& outColor) {
    if (hex.size() != 7 && hex.size() != 9) {
        return false;
    }
    if (hex[0] != L'#') {
        return false;
    }

    auto hexDigit = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };

    int values[4] = {0, 0, 0, 255};
    size_t channelCount = (hex.size() == 9) ? 4 : 3;
    for (size_t i = 0; i < channelCount; i++) {
        int hi = hexDigit(hex[1 + i * 2]);
        int lo = hexDigit(hex[1 + i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        values[i] = hi * 16 + lo;
    }

    outColor.r = values[0] / 255.0f;
    outColor.g = values[1] / 255.0f;
    outColor.b = values[2] / 255.0f;
    outColor.a = values[3] / 255.0f;
    return true;
}

inline ThemeMode ParseThemeMode(const std::wstring& value) {
    if (value == L"light") return ThemeMode::Light;
    if (value == L"dark") return ThemeMode::Dark;
    if (value == L"custom") return ThemeMode::Custom;
    return ThemeMode::Auto;
}

struct ThemePalette {
    RgbaColor background;
    RgbaColor text;
    RgbaColor textSecondary;
    RgbaColor accent;
};

inline const ThemePalette kLightPalette{
    /*background*/ {0.96f, 0.96f, 0.96f, 0.85f},
    /*text*/ {0.0f, 0.0f, 0.0f, 1.0f},
    /*textSecondary*/ {0.35f, 0.35f, 0.35f, 1.0f},
    /*accent*/ {0.0f, 0.47f, 0.83f, 1.0f},
};

inline const ThemePalette kDarkPalette{
    /*background*/ {0.17f, 0.17f, 0.17f, 0.85f},
    /*text*/ {1.0f, 1.0f, 1.0f, 1.0f},
    /*textSecondary*/ {0.78f, 0.78f, 0.78f, 1.0f},
    /*accent*/ {0.0f, 0.47f, 0.83f, 1.0f},
};

inline ThemePalette ResolvePalette(ThemeMode mode, bool systemIsDarkMode,
                                    const ThemePalette& customPalette) {
    switch (mode) {
        case ThemeMode::Light:
            return kLightPalette;
        case ThemeMode::Dark:
            return kDarkPalette;
        case ThemeMode::Custom:
            return customPalette;
        case ThemeMode::Auto:
        default:
            return systemIsDarkMode ? kDarkPalette : kLightPalette;
    }
}

struct ModSettings {
    int durationSeconds = 4;
    ThemeMode theme = ThemeMode::Auto;
    RgbaColor customBackground{0.17f, 0.17f, 0.17f, 0.85f};
    RgbaColor customText{1.0f, 1.0f, 1.0f, 1.0f};
    RgbaColor customAccent{0.0f, 0.47f, 0.83f, 1.0f};
};

inline constexpr int kMinDurationSeconds = 1;
inline constexpr int kMaxDurationSeconds = 30;

inline int ClampDurationSeconds(int rawDuration) {
    if (rawDuration < kMinDurationSeconds) return kMinDurationSeconds;
    if (rawDuration > kMaxDurationSeconds) return kMaxDurationSeconds;
    return rawDuration;
}

inline ModSettings BuildSettings(int rawDuration, const std::wstring& rawTheme,
                                  const std::wstring& rawBgHex,
                                  const std::wstring& rawTextHex,
                                  const std::wstring& rawAccentHex) {
    ModSettings settings;
    settings.durationSeconds = ClampDurationSeconds(rawDuration);
    settings.theme = ParseThemeMode(rawTheme);

    RgbaColor parsed;
    if (TryParseHexColor(rawBgHex, parsed)) settings.customBackground = parsed;
    if (TryParseHexColor(rawTextHex, parsed)) settings.customText = parsed;
    if (TryParseHexColor(rawAccentHex, parsed)) settings.customAccent = parsed;

    return settings;
}
```

- [ ] **Step 4: Run tests, verify they pass**

Run: `bash scripts/run-unit-tests.sh`
Expected: `31/31 tests passed`, exit code 0.

- [ ] **Step 5: Run the compile-check script, verify the mod itself still compiles**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 6: Commit**

```bash
git add SoundTrackAutoView.cpp tests/unit_tests.cpp
git commit -m "Add theme and settings parsing logic with tests"
```

---

### Task 3: Panel and button layout math (pure logic)

**Files:**
- Modify: `SoundTrackAutoView.cpp` (pure-logic section, after Task 2's code)
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: `<windows.h>` (`RECT`, `UINT`, `POINT`) — already included.
- Produces: `struct PanelLayout { int x, y, width, height; }`, `PanelLayout ComputePanelPosition(const RECT& workArea, UINT dpi)`, `struct ButtonLayout { RECT previous, playPause, next; }`, `ButtonLayout ComputeButtonLayout(const PanelLayout& panel)`, `enum class OsdButton { None, Previous, PlayPause, Next }`, `OsdButton HitTestButton(const ButtonLayout& buttons, POINT pt)`. Used by Task 8 (rendering/positioning) and Task 9 (click handling).

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit_tests.cpp`:

```cpp
static void Test_ComputePanelPosition_StandardDpi() {
    RECT workArea{0, 0, 1920, 1040};
    PanelLayout layout = ComputePanelPosition(workArea, 96);
    CHECK(layout.width == 360);
    CHECK(layout.height == 100);
    CHECK(layout.x == 1920 - 360 - 12);
    CHECK(layout.y == 1040 - 100 - 12);
}

static void Test_ComputePanelPosition_ScaledDpi() {
    RECT workArea{0, 0, 3840, 2080};
    PanelLayout layout = ComputePanelPosition(workArea, 144);  // 150%
    CHECK(layout.width == 540);
    CHECK(layout.height == 150);
    CHECK(layout.x == 3840 - 540 - 18);
}

static void Test_ComputePanelPosition_SecondMonitorOffset() {
    RECT workArea{1920, 0, 3840, 1040};  // monitor to the right, non-zero origin
    PanelLayout layout = ComputePanelPosition(workArea, 96);
    CHECK(layout.x == 3840 - 360 - 12);
    CHECK(layout.x > 1920);
}

static void Test_ComputePanelPosition_ClampsOnTinyWorkArea() {
    RECT workArea{0, 0, 300, 80};
    PanelLayout layout = ComputePanelPosition(workArea, 96);
    CHECK(layout.x == 0);   // would be negative otherwise, clamped to workArea.left
    CHECK(layout.y == 0);   // would be negative otherwise, clamped to workArea.top
}

static void Test_ComputeButtonLayout_ThreeDistinctRegions() {
    PanelLayout panel{0, 0, 360, 100};
    ButtonLayout buttons = ComputeButtonLayout(panel);
    CHECK(buttons.previous.left < buttons.playPause.left);
    CHECK(buttons.playPause.left < buttons.next.left);
}

static void Test_HitTestButton_InsideEachButton() {
    PanelLayout panel{0, 0, 360, 100};
    ButtonLayout buttons = ComputeButtonLayout(panel);

    POINT centerOfPlayPause{(buttons.playPause.left + buttons.playPause.right) / 2,
                             (buttons.playPause.top + buttons.playPause.bottom) / 2};
    CHECK(HitTestButton(buttons, centerOfPlayPause) == OsdButton::PlayPause);

    POINT centerOfPrevious{(buttons.previous.left + buttons.previous.right) / 2,
                            (buttons.previous.top + buttons.previous.bottom) / 2};
    CHECK(HitTestButton(buttons, centerOfPrevious) == OsdButton::Previous);

    POINT centerOfNext{(buttons.next.left + buttons.next.right) / 2,
                        (buttons.next.top + buttons.next.bottom) / 2};
    CHECK(HitTestButton(buttons, centerOfNext) == OsdButton::Next);
}

static void Test_HitTestButton_OutsideAnyButton() {
    PanelLayout panel{0, 0, 360, 100};
    ButtonLayout buttons = ComputeButtonLayout(panel);
    POINT farCorner{0, 0};
    CHECK(HitTestButton(buttons, farCorner) == OsdButton::None);
}
```

Add the calls in `main()`:

```cpp
    Test_ComputePanelPosition_StandardDpi();
    Test_ComputePanelPosition_ScaledDpi();
    Test_ComputePanelPosition_SecondMonitorOffset();
    Test_ComputePanelPosition_ClampsOnTinyWorkArea();
    Test_ComputeButtonLayout_ThreeDistinctRegions();
    Test_HitTestButton_InsideEachButton();
    Test_HitTestButton_OutsideAnyButton();
```

- [ ] **Step 2: Run tests, verify they fail to compile**

Run: `bash scripts/run-unit-tests.sh`
Expected: compile error, e.g. `use of undeclared identifier 'PanelLayout'`.

- [ ] **Step 3: Implement the layout logic**

Append to the pure-logic section of `SoundTrackAutoView.cpp`, after Task 2's code:

```cpp
struct PanelLayout {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline constexpr int kPanelBaseWidthDip = 360;
inline constexpr int kPanelBaseHeightDip = 100;
inline constexpr int kPanelMarginDip = 12;

inline PanelLayout ComputePanelPosition(const RECT& workArea, UINT dpi) {
    double scale = dpi / 96.0;
    int width = static_cast<int>(kPanelBaseWidthDip * scale);
    int height = static_cast<int>(kPanelBaseHeightDip * scale);
    int margin = static_cast<int>(kPanelMarginDip * scale);

    int x = workArea.right - width - margin;
    int y = workArea.bottom - height - margin;

    if (x < workArea.left) x = workArea.left;
    if (y < workArea.top) y = workArea.top;

    PanelLayout layout;
    layout.x = x;
    layout.y = y;
    layout.width = width;
    layout.height = height;
    return layout;
}

struct ButtonLayout {
    RECT previous;
    RECT playPause;
    RECT next;
};

inline ButtonLayout ComputeButtonLayout(const PanelLayout& panel) {
    int buttonSize = panel.height / 3;
    int centerY = panel.y + panel.height - buttonSize - (panel.height / 8);
    int spacing = buttonSize + buttonSize / 2;
    int centerX = panel.x + panel.width / 2;

    ButtonLayout buttons;
    buttons.playPause = {centerX - buttonSize / 2, centerY,
                          centerX + buttonSize / 2, centerY + buttonSize};
    buttons.previous = {centerX - spacing - buttonSize / 2, centerY,
                         centerX - spacing + buttonSize / 2, centerY + buttonSize};
    buttons.next = {centerX + spacing - buttonSize / 2, centerY,
                     centerX + spacing + buttonSize / 2, centerY + buttonSize};
    return buttons;
}

enum class OsdButton {
    None,
    Previous,
    PlayPause,
    Next,
};

inline OsdButton HitTestButton(const ButtonLayout& buttons, POINT pt) {
    auto inside = [](const RECT& r, POINT p) {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    };
    if (inside(buttons.playPause, pt)) return OsdButton::PlayPause;
    if (inside(buttons.previous, pt)) return OsdButton::Previous;
    if (inside(buttons.next, pt)) return OsdButton::Next;
    return OsdButton::None;
}
```

- [ ] **Step 4: Run tests, verify they pass**

Run: `bash scripts/run-unit-tests.sh`
Expected: `48/48 tests passed`.

- [ ] **Step 5: Run the compile-check script**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 6: Commit**

```bash
git add SoundTrackAutoView.cpp tests/unit_tests.cpp
git commit -m "Add panel and button layout math with tests"
```

---

### Task 4: OSD timing and state controller (pure logic)

**Files:**
- Modify: `SoundTrackAutoView.cpp` (pure-logic section, after Task 3's code)
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `class OsdTimingController` with `OnTrigger(double now) -> TriggerResult{bool shouldPlayFadeIn}`, `OnSmtcUpdate(double now) const -> bool`, `OnMouseEnter(double now)`, `OnMouseLeave(double now)`, `ShouldHideNow(double now) const -> bool`, `IsVisible() const -> bool`, `MarkHidden()`. Used by Task 9 (interactivity/animation) and Task 10 (wire-up).

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit_tests.cpp`:

```cpp
static void Test_OsdTimingController_FirstTriggerPlaysFadeIn() {
    OsdTimingController controller(4.0);
    auto result = controller.OnTrigger(0.0);
    CHECK(result.shouldPlayFadeIn);
    CHECK(controller.IsVisible());
}

static void Test_OsdTimingController_SecondTriggerWhileVisibleSkipsFadeIn() {
    OsdTimingController controller(4.0);
    controller.OnTrigger(0.0);
    auto result = controller.OnTrigger(0.5);
    CHECK(!result.shouldPlayFadeIn);
}

static void Test_OsdTimingController_HidesAfterDuration() {
    OsdTimingController controller(4.0);
    controller.OnTrigger(0.0);
    CHECK(!controller.ShouldHideNow(3.9));
    CHECK(controller.ShouldHideNow(4.0));
}

static void Test_OsdTimingController_SecondTriggerResetsHideDeadline() {
    OsdTimingController controller(4.0);
    controller.OnTrigger(0.0);
    controller.OnTrigger(2.0);
    CHECK(!controller.ShouldHideNow(4.0));  // would have fired under the first trigger
    CHECK(controller.ShouldHideNow(6.0));
}

static void Test_OsdTimingController_SmtcUpdateOnlyAppliesWithinCorrectionWindow() {
    OsdTimingController controller(4.0, 1.0);
    controller.OnTrigger(0.0);
    CHECK(controller.OnSmtcUpdate(0.5));
    CHECK(!controller.OnSmtcUpdate(1.5));
}

static void Test_OsdTimingController_HoverPausesCountdown() {
    OsdTimingController controller(4.0);
    controller.OnTrigger(0.0);
    controller.OnMouseEnter(1.0);
    CHECK(!controller.ShouldHideNow(10.0));
}

static void Test_OsdTimingController_MouseLeaveResumesFromFullDuration() {
    OsdTimingController controller(4.0);
    controller.OnTrigger(0.0);
    controller.OnMouseEnter(1.0);
    controller.OnMouseLeave(10.0);
    CHECK(!controller.ShouldHideNow(13.9));
    CHECK(controller.ShouldHideNow(14.0));
}

static void Test_OsdTimingController_RetriggerAfterHideReplaysFadeIn() {
    OsdTimingController controller(4.0);
    controller.OnTrigger(0.0);
    controller.MarkHidden();
    auto result = controller.OnTrigger(10.0);
    CHECK(result.shouldPlayFadeIn);
}
```

Add the calls in `main()`:

```cpp
    Test_OsdTimingController_FirstTriggerPlaysFadeIn();
    Test_OsdTimingController_SecondTriggerWhileVisibleSkipsFadeIn();
    Test_OsdTimingController_HidesAfterDuration();
    Test_OsdTimingController_SecondTriggerResetsHideDeadline();
    Test_OsdTimingController_SmtcUpdateOnlyAppliesWithinCorrectionWindow();
    Test_OsdTimingController_HoverPausesCountdown();
    Test_OsdTimingController_MouseLeaveResumesFromFullDuration();
    Test_OsdTimingController_RetriggerAfterHideReplaysFadeIn();
```

- [ ] **Step 2: Run tests, verify they fail to compile**

Run: `bash scripts/run-unit-tests.sh`
Expected: compile error, e.g. `use of undeclared identifier 'OsdTimingController'`.

- [ ] **Step 3: Implement the controller**

Append to the pure-logic section of `SoundTrackAutoView.cpp`, after Task 3's code:

```cpp
class OsdTimingController {
public:
    explicit OsdTimingController(double durationSeconds,
                                  double correctionWindowSeconds = 1.0)
        : durationSeconds_(durationSeconds),
          correctionWindowSeconds_(correctionWindowSeconds) {}

    struct TriggerResult {
        bool shouldPlayFadeIn = false;
    };

    TriggerResult OnTrigger(double nowSeconds) {
        TriggerResult result;
        result.shouldPlayFadeIn = !visible_;
        visible_ = true;
        hovered_ = false;
        hideDeadlineSeconds_ = nowSeconds + durationSeconds_;
        correctionDeadlineSeconds_ = nowSeconds + correctionWindowSeconds_;
        return result;
    }

    bool OnSmtcUpdate(double nowSeconds) const {
        return visible_ && nowSeconds <= correctionDeadlineSeconds_;
    }

    void OnMouseEnter(double /*nowSeconds*/) {
        hovered_ = true;
    }

    void OnMouseLeave(double nowSeconds) {
        hovered_ = false;
        hideDeadlineSeconds_ = nowSeconds + durationSeconds_;
    }

    bool ShouldHideNow(double nowSeconds) const {
        return visible_ && !hovered_ && nowSeconds >= hideDeadlineSeconds_;
    }

    bool IsVisible() const {
        return visible_;
    }

    void MarkHidden() {
        visible_ = false;
        hovered_ = false;
    }

private:
    double durationSeconds_;
    double correctionWindowSeconds_;
    bool visible_ = false;
    bool hovered_ = false;
    double hideDeadlineSeconds_ = 0.0;
    double correctionDeadlineSeconds_ = 0.0;
};
```

- [ ] **Step 4: Run tests, verify they pass**

Run: `bash scripts/run-unit-tests.sh`
Expected: `61/61 tests passed`.

- [ ] **Step 5: Run the compile-check script**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 6: Commit**

```bash
git add SoundTrackAutoView.cpp tests/unit_tests.cpp
git commit -m "Add OSD timing/state controller with tests"
```

---

### Task 5: Wire Windhawk settings into ModSettings

**Files:**
- Modify: `SoundTrackAutoView.cpp` (guarded section)

**Interfaces:**
- Consumes: `ModSettings`, `BuildSettings` (Task 2).
- Produces: `std::shared_ptr<const ModSettings> GetCurrentSettings()`, `void RefreshSettingsFromWindhawk()`, `void Wh_ModSettingsChanged()`. `GetCurrentSettings()` is used by Task 8 (rendering) and Task 9 (duration).

This task has no automated test (it only calls real `Wh_*` engine functions, which don't exist outside a loaded mod). It's verified by compile-check now and by the full manual QA pass in Task 11.

- [ ] **Step 1: Add the settings storage and refresh logic**

Add near the top of the guarded section in `SoundTrackAutoView.cpp` (before `Wh_ModInit`):

```cpp
#include <memory>
#include <atomic>

static std::atomic<std::shared_ptr<const ModSettings>> g_settings{
    std::make_shared<const ModSettings>()};

std::shared_ptr<const ModSettings> GetCurrentSettings() {
    return g_settings.load();
}

void RefreshSettingsFromWindhawk() {
    int rawDuration = Wh_GetIntSetting(L"duration");

    PCWSTR rawTheme = Wh_GetStringSetting(L"theme");
    std::wstring theme(rawTheme);
    Wh_FreeStringSetting(rawTheme);

    PCWSTR rawBg = Wh_GetStringSetting(L"customBackgroundColor");
    std::wstring bg(rawBg);
    Wh_FreeStringSetting(rawBg);

    PCWSTR rawText = Wh_GetStringSetting(L"customTextColor");
    std::wstring text(rawText);
    Wh_FreeStringSetting(rawText);

    PCWSTR rawAccent = Wh_GetStringSetting(L"customAccentColor");
    std::wstring accent(rawAccent);
    Wh_FreeStringSetting(rawAccent);

    auto settings = std::make_shared<const ModSettings>(
        BuildSettings(rawDuration, theme, bg, text, accent));
    g_settings.store(settings);
}
```

- [ ] **Step 2: Wire it into the mod entry points**

Replace `Wh_ModInit` and `Wh_ModUninit` in `SoundTrackAutoView.cpp` with:

```cpp
BOOL Wh_ModInit() {
    Wh_Log(L"SoundTrackAutoView: init");
    RefreshSettingsFromWindhawk();
    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"SoundTrackAutoView: uninit");
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"SoundTrackAutoView: settings changed");
    RefreshSettingsFromWindhawk();
}
```

- [ ] **Step 3: Run the compile-check script**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 4: Run the unit tests (confirm this task didn't break pure-logic tests)**

Run: `bash scripts/run-unit-tests.sh`
Expected: `61/61 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add SoundTrackAutoView.cpp
git commit -m "Wire Windhawk settings into ModSettings"
```

---

### Task 6: Keyboard hook for media key detection

**Files:**
- Modify: `SoundTrackAutoView.cpp` (guarded section)

**Interfaces:**
- Consumes: nothing new.
- Produces: `constexpr UINT kOsdTriggerMessage`, `std::atomic<DWORD> g_osdThreadId`, `bool InstallKeyboardHook()`, `void UninstallKeyboardHook()`. `g_osdThreadId` and `kOsdTriggerMessage` are set/read by Task 10 (OSD thread).

- [ ] **Step 1: Add the keyboard hook**

Add after the settings code from Task 5:

```cpp
inline constexpr UINT kOsdTriggerMessage = WM_APP + 1;

static std::atomic<DWORD> g_osdThreadId{0};
static HHOOK g_keyboardHook = nullptr;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam,
                                              LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        switch (info->vkCode) {
            case VK_MEDIA_PLAY_PAUSE:
            case VK_MEDIA_NEXT_TRACK:
            case VK_MEDIA_PREV_TRACK:
            case VK_MEDIA_STOP: {
                Wh_Log(L"SoundTrackAutoView: media key detected (vk=%u)",
                       info->vkCode);
                DWORD threadId = g_osdThreadId.load();
                if (threadId != 0) {
                    PostThreadMessageW(threadId, kOsdTriggerMessage, 0, 0);
                }
                break;
            }
            default:
                break;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool InstallKeyboardHook() {
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                        GetModuleHandleW(nullptr), 0);
    if (!g_keyboardHook) {
        Wh_Log(L"SoundTrackAutoView: failed to install keyboard hook, error %lu",
               GetLastError());
        return false;
    }
    return true;
}

void UninstallKeyboardHook() {
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
}
```

- [ ] **Step 2: Wire hook install/uninstall into the mod entry points**

Replace `Wh_ModInit` and `Wh_ModUninit` with:

```cpp
BOOL Wh_ModInit() {
    Wh_Log(L"SoundTrackAutoView: init");
    RefreshSettingsFromWindhawk();

    if (!InstallKeyboardHook()) {
        return FALSE;
    }

    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"SoundTrackAutoView: uninit");
    UninstallKeyboardHook();
}
```

- [ ] **Step 3: Run the compile-check script**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 4: Manual verification**

Load `SoundTrackAutoView.cpp` into Windhawk (see "Environment notes" above) and enable it. Press a physical media key (Play/Pause, Next, or Previous) on your keyboard. Open the mod's log view and confirm a line like `SoundTrackAutoView: media key detected (vk=179)` appears. Disable the mod afterward (the OSD thread doesn't exist yet, so nothing will visibly happen beyond the log line).

- [ ] **Step 5: Commit**

```bash
git add SoundTrackAutoView.cpp
git commit -m "Add low-level keyboard hook for media key detection"
```

---

### Task 7: SMTC data source

**Files:**
- Modify: `SoundTrackAutoView.cpp` (pure-logic section for the snapshot types, guarded section for the WinRT implementation)

**Interfaces:**
- Consumes: nothing new in the pure-logic section.
- Produces (pure): `enum class PlaybackState { Unknown, Playing, Paused, Stopped }`, `struct TrackSnapshot { std::wstring title, artist; PlaybackState status; std::vector<uint8_t> artPixelsBgra; int artWidth, artHeight; }`, `bool ShouldShowPanel(const std::optional<TrackSnapshot>&)`.
- Produces (guarded): `GlobalSystemMediaTransportControlsSessionManager g_sessionManager`, `bool EnsureSessionManager()`, `std::optional<TrackSnapshot> TryGetCurrentTrackSnapshot()`, `void SendPlayPauseCommand()`, `void SendNextTrackCommand()`, `void SendPreviousTrackCommand()`. Used by Task 8 (rendering), Task 9 (button clicks), Task 10 (wire-up).

- [ ] **Step 1: Write the failing test for the pure decision helper**

Append to `tests/unit_tests.cpp`:

```cpp
static void Test_ShouldShowPanel_TrueWhenSnapshotPresent() {
    TrackSnapshot snapshot;
    snapshot.title = L"Falling Down - Bonus Track";
    CHECK(ShouldShowPanel(std::optional<TrackSnapshot>(snapshot)));
}

static void Test_ShouldShowPanel_FalseWhenNoSession() {
    CHECK(!ShouldShowPanel(std::optional<TrackSnapshot>()));
}
```

Add the calls in `main()`:

```cpp
    Test_ShouldShowPanel_TrueWhenSnapshotPresent();
    Test_ShouldShowPanel_FalseWhenNoSession();
```

- [ ] **Step 2: Run tests, verify they fail to compile**

Run: `bash scripts/run-unit-tests.sh`
Expected: compile error, e.g. `use of undeclared identifier 'TrackSnapshot'`.

- [ ] **Step 3: Add the pure snapshot types and decision helper**

Append to the pure-logic section of `SoundTrackAutoView.cpp`, after Task 4's code (add `#include <optional>`, `#include <vector>`, `#include <cstdint>` to the top-of-file includes alongside `<string>`):

```cpp
enum class PlaybackState {
    Unknown,
    Playing,
    Paused,
    Stopped,
};

struct TrackSnapshot {
    std::wstring title;
    std::wstring artist;
    PlaybackState status = PlaybackState::Unknown;
    std::vector<uint8_t> artPixelsBgra;  // empty if there's no/failed art
    int artWidth = 0;
    int artHeight = 0;
};

inline bool ShouldShowPanel(const std::optional<TrackSnapshot>& snapshot) {
    return snapshot.has_value();
}
```

- [ ] **Step 4: Run tests, verify they pass**

Run: `bash scripts/run-unit-tests.sh`
Expected: `63/63 tests passed`.

- [ ] **Step 5: Add the WinRT session manager and thumbnail decoding**

Add to the guarded section of `SoundTrackAutoView.cpp`, after Task 6's keyboard hook code:

```cpp
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wincodec.h>
#include <shcore.h>

using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using winrt::Windows::Storage::Streams::IRandomAccessStreamReference;

static GlobalSystemMediaTransportControlsSessionManager g_sessionManager{nullptr};

static bool EnsureSessionManager() {
    if (g_sessionManager) {
        return true;
    }
    try {
        g_sessionManager =
            GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        return true;
    } catch (const winrt::hresult_error& ex) {
        Wh_Log(L"SoundTrackAutoView: failed to get session manager: %s",
               ex.message().c_str());
        return false;
    }
}

static PlaybackState ToPlaybackState(
    GlobalSystemMediaTransportControlsSessionPlaybackStatus status) {
    switch (status) {
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
            return PlaybackState::Playing;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
            return PlaybackState::Paused;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
            return PlaybackState::Stopped;
        default:
            return PlaybackState::Unknown;
    }
}

static bool TryDecodeThumbnail(IRandomAccessStreamReference const& thumbnailRef,
                                std::vector<uint8_t>& outPixelsBgra, int& outWidth,
                                int& outHeight) {
    try {
        auto stream = thumbnailRef.OpenReadAsync().get();

        winrt::com_ptr<IStream> comStream;
        winrt::check_hresult(CreateStreamOverRandomAccessStream(
            winrt::get_unknown(stream), IID_PPV_ARGS(comStream.put())));

        winrt::com_ptr<IWICImagingFactory> wicFactory;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                               CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(wicFactory.put())));

        winrt::com_ptr<IWICBitmapDecoder> decoder;
        winrt::check_hresult(wicFactory->CreateDecoderFromStream(
            comStream.get(), nullptr, WICDecodeMetadataCacheOnDemand,
            decoder.put()));

        winrt::com_ptr<IWICBitmapFrameDecode> frame;
        winrt::check_hresult(decoder->GetFrame(0, frame.put()));

        winrt::com_ptr<IWICFormatConverter> converter;
        winrt::check_hresult(wicFactory->CreateFormatConverter(converter.put()));
        winrt::check_hresult(converter->Initialize(
            frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom));

        UINT width = 0, height = 0;
        winrt::check_hresult(converter->GetSize(&width, &height));

        outPixelsBgra.resize(static_cast<size_t>(width) * height * 4);
        winrt::check_hresult(converter->CopyPixels(
            nullptr, width * 4, static_cast<UINT>(outPixelsBgra.size()),
            outPixelsBgra.data()));

        outWidth = static_cast<int>(width);
        outHeight = static_cast<int>(height);
        return true;
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

std::optional<TrackSnapshot> TryGetCurrentTrackSnapshot() {
    if (!EnsureSessionManager()) {
        return std::nullopt;
    }

    auto session = g_sessionManager.GetCurrentSession();
    if (!session) {
        return std::nullopt;
    }

    try {
        TrackSnapshot snapshot;

        auto playbackInfo = session.GetPlaybackInfo();
        snapshot.status = ToPlaybackState(playbackInfo.PlaybackStatus());

        auto properties = session.TryGetMediaPropertiesAsync().get();
        snapshot.title = properties.Title().c_str();
        snapshot.artist = properties.Artist().c_str();

        auto thumbnailRef = properties.Thumbnail();
        if (thumbnailRef) {
            TryDecodeThumbnail(thumbnailRef, snapshot.artPixelsBgra,
                                snapshot.artWidth, snapshot.artHeight);
        }

        return snapshot;
    } catch (const winrt::hresult_error& ex) {
        Wh_Log(L"SoundTrackAutoView: failed to read session state: %s",
               ex.message().c_str());
        return std::nullopt;
    }
}

void SendPlayPauseCommand() {
    if (!EnsureSessionManager()) return;
    if (auto session = g_sessionManager.GetCurrentSession()) {
        session.TryTogglePlayPauseAsync();
    }
}

void SendNextTrackCommand() {
    if (!EnsureSessionManager()) return;
    if (auto session = g_sessionManager.GetCurrentSession()) {
        session.TrySkipNextAsync();
    }
}

void SendPreviousTrackCommand() {
    if (!EnsureSessionManager()) return;
    if (auto session = g_sessionManager.GetCurrentSession()) {
        session.TrySkipPreviousAsync();
    }
}
```

- [ ] **Step 6: Run the compile-check script**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.` If it fails with an unresolved WinRT type, double check the `#include` list above matches exactly — C++/WinRT headers are sensitive to include order for operator overloads.

- [ ] **Step 7: Run the unit tests (confirm the guarded WinRT code didn't break the pure-logic build)**

Run: `bash scripts/run-unit-tests.sh`
Expected: `63/63 tests passed`.

- [ ] **Step 8: Commit**

```bash
git add SoundTrackAutoView.cpp tests/unit_tests.cpp
git commit -m "Add SMTC session data source and playback control commands"
```

---

### Task 8: OSD window chrome and static rendering

**Files:**
- Modify: `SoundTrackAutoView.cpp` (guarded section)

**Interfaces:**
- Consumes: `ThemePalette`, `ResolvePalette`, `ModSettings` (Task 2), `PanelLayout`, `ComputePanelPosition`, `ButtonLayout`, `ComputeButtonLayout` (Task 3), `TrackSnapshot`, `PlaybackState` (Task 7), `GetCurrentSettings()` (Task 5).
- Produces: `HWND CreateOsdWindow(HINSTANCE)`, `void UpdateOsdContent(HWND, const TrackSnapshot&)`, `static float g_currentAlpha` (wired through for Task 9's animation), `static PanelLayout g_currentLayout`, `static ThemePalette g_currentPalette`, `static TrackSnapshot g_currentSnapshot`, `static HWND g_osdWindow`. Used by Task 9 (interactivity/animation) and Task 10 (wire-up).

This task has no automated test — it's real window/GPU rendering. It's verified by compile-check and a manual visual check.

- [ ] **Step 1: Add the rendering and window-creation code**

Add to the guarded section, after Task 7's code:

```cpp
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>

static const wchar_t kOsdWindowClassName[] = L"SoundTrackAutoViewOsdWindow";

static winrt::com_ptr<ID2D1Factory> g_d2dFactory;
static winrt::com_ptr<IDWriteFactory> g_dwriteFactory;
static HWND g_osdWindow = nullptr;
static PanelLayout g_currentLayout;
static ThemePalette g_currentPalette;
static TrackSnapshot g_currentSnapshot;
static float g_currentAlpha = 1.0f;

static void EnsureGraphicsFactories() {
    if (!g_d2dFactory) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2dFactory.put());
    }
    if (!g_dwriteFactory) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(g_dwriteFactory.put()));
    }
}

static bool IsSystemInDarkMode() {
    HKEY key;
    if (RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
            KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = REG_DWORD;
    LONG result = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                    reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        return false;
    }
    return value == 0;  // AppsUseLightTheme == 0 means dark mode
}

static void DrawMusicNotePlaceholder(ID2D1DCRenderTarget* dcTarget,
                                      const D2D1_RECT_F& rect,
                                      ID2D1SolidColorBrush* brush) {
    float size = rect.bottom - rect.top;
    float noteHeadRadius = size * 0.14f;
    float stemWidth = size * 0.07f;
    float stemHeight = size * 0.55f;

    float centerX = rect.left + size * 0.42f;
    float centerY = rect.bottom - size * 0.22f;

    D2D1_ELLIPSE noteHead =
        D2D1::Ellipse(D2D1::Point2F(centerX, centerY), noteHeadRadius, noteHeadRadius);
    dcTarget->FillEllipse(noteHead, brush);

    D2D1_RECT_F stem =
        D2D1::RectF(centerX + noteHeadRadius - stemWidth, centerY - stemHeight,
                    centerX + noteHeadRadius, centerY);
    dcTarget->FillRectangle(stem, brush);

    D2D1_RECT_F flag = D2D1::RectF(centerX + noteHeadRadius - stemWidth,
                                    centerY - stemHeight,
                                    centerX + noteHeadRadius + size * 0.16f,
                                    centerY - stemHeight * 0.65f);
    dcTarget->FillRectangle(flag, brush);
}

static void PaintOsdContent(HWND hwnd, const PanelLayout& layout,
                             const ThemePalette& palette,
                             const TrackSnapshot& snapshot, float alpha) {
    EnsureGraphicsFactories();

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = layout.width;
    bmi.bmiHeader.biHeight = -layout.height;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDc, bitmap));

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    winrt::com_ptr<ID2D1DCRenderTarget> dcTarget;
    g_d2dFactory->CreateDCRenderTarget(&rtProps, dcTarget.put());

    RECT bindRect{0, 0, layout.width, layout.height};
    dcTarget->BindDC(memDc, &bindRect);

    dcTarget->BeginDraw();
    dcTarget->Clear(D2D1::ColorF(palette.background.r, palette.background.g,
                                  palette.background.b, palette.background.a));

    winrt::com_ptr<ID2D1SolidColorBrush> textBrush;
    dcTarget->CreateSolidColorBrush(
        D2D1::ColorF(palette.text.r, palette.text.g, palette.text.b, palette.text.a),
        textBrush.put());

    winrt::com_ptr<ID2D1SolidColorBrush> textSecondaryBrush;
    dcTarget->CreateSolidColorBrush(
        D2D1::ColorF(palette.textSecondary.r, palette.textSecondary.g,
                     palette.textSecondary.b, palette.textSecondary.a),
        textSecondaryBrush.put());

    winrt::com_ptr<ID2D1SolidColorBrush> accentBrush;
    dcTarget->CreateSolidColorBrush(
        D2D1::ColorF(palette.accent.r, palette.accent.g, palette.accent.b,
                     palette.accent.a),
        accentBrush.put());

    winrt::com_ptr<IDWriteTextFormat> titleFormat;
    g_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us",
        titleFormat.put());

    winrt::com_ptr<IDWriteTextFormat> artistFormat;
    g_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us",
        artistFormat.put());

    float artSize = static_cast<float>(layout.height) - 24.0f;
    D2D1_RECT_F artRect = D2D1::RectF(12.0f, 12.0f, 12.0f + artSize, 12.0f + artSize);

    if (!snapshot.artPixelsBgra.empty()) {
        winrt::com_ptr<ID2D1Bitmap> artBitmap;
        D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                               D2D1_ALPHA_MODE_PREMULTIPLIED));
        HRESULT hr = dcTarget->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(snapshot.artWidth),
                        static_cast<UINT32>(snapshot.artHeight)),
            snapshot.artPixelsBgra.data(),
            static_cast<UINT32>(snapshot.artWidth) * 4, bitmapProps, artBitmap.put());
        if (SUCCEEDED(hr)) {
            dcTarget->DrawBitmap(artBitmap.get(), artRect);
        }
    } else {
        winrt::com_ptr<ID2D1SolidColorBrush> placeholderBgBrush;
        dcTarget->CreateSolidColorBrush(
            D2D1::ColorF(palette.textSecondary.r, palette.textSecondary.g,
                         palette.textSecondary.b, 0.15f),
            placeholderBgBrush.put());
        dcTarget->FillRectangle(artRect, placeholderBgBrush.get());
        DrawMusicNotePlaceholder(dcTarget.get(), artRect, textSecondaryBrush.get());
    }

    float textLeft = artRect.right + 12.0f;
    D2D1_RECT_F titleRect =
        D2D1::RectF(textLeft, 14.0f, static_cast<float>(layout.width) - 12.0f, 34.0f);
    D2D1_RECT_F artistRect =
        D2D1::RectF(textLeft, 36.0f, static_cast<float>(layout.width) - 12.0f, 54.0f);

    dcTarget->DrawText(snapshot.title.c_str(),
                        static_cast<UINT32>(snapshot.title.size()), titleFormat.get(),
                        titleRect, textBrush.get());
    dcTarget->DrawText(snapshot.artist.c_str(),
                        static_cast<UINT32>(snapshot.artist.size()),
                        artistFormat.get(), artistRect, textSecondaryBrush.get());

    PanelLayout panelForButtons{0, 0, layout.width, layout.height};
    ButtonLayout buttons = ComputeButtonLayout(panelForButtons);

    auto drawButton = [&](const RECT& r, const wchar_t* glyph) {
        D2D1_ELLIPSE circle = D2D1::Ellipse(
            D2D1::Point2F((r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f),
            (r.right - r.left) / 2.0f, (r.bottom - r.top) / 2.0f);
        dcTarget->FillEllipse(circle, accentBrush.get());

        D2D1_RECT_F glyphRect =
            D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                        static_cast<float>(r.right), static_cast<float>(r.bottom));
        winrt::com_ptr<IDWriteTextFormat> glyphFormat;
        g_dwriteFactory->CreateTextFormat(
            L"Segoe Fluent Icons", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us",
            glyphFormat.put());
        glyphFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        glyphFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        dcTarget->DrawText(glyph, 1, glyphFormat.get(), glyphRect, textBrush.get());
    };

    drawButton(buttons.previous, L"\uE892");
    drawButton(buttons.playPause,
               snapshot.status == PlaybackState::Playing ? L"\uE769" : L"\uE768");
    drawButton(buttons.next, L"\uE893");

    dcTarget->EndDraw();

    POINT sourcePoint{0, 0};
    POINT windowPos{layout.x, layout.y};
    SIZE windowSize{layout.width, layout.height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0,
                         static_cast<BYTE>(255.0f * alpha), AC_SRC_ALPHA};

    UpdateLayeredWindow(hwnd, screenDc, &windowPos, &windowSize, memDc, &sourcePoint,
                         0, &blend, ULW_ALPHA);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}

static LRESULT CALLBACK OsdWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateOsdWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OsdWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kOsdWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kOsdWindowClassName, L"SoundTrackAutoView", WS_POPUP, 0, 0, 1, 1, nullptr,
        nullptr, hInstance, nullptr);

    if (hwnd) {
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                               sizeof(corner));

        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TRANSIENTWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                               sizeof(backdrop));
    }

    return hwnd;
}

void UpdateOsdContent(HWND hwnd, const TrackSnapshot& snapshot) {
    g_currentSnapshot = snapshot;

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(monitor, &monitorInfo);

    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;

    g_currentLayout = ComputePanelPosition(monitorInfo.rcWork, dpi);

    auto settings = GetCurrentSettings();
    bool systemIsDark = IsSystemInDarkMode();
    ThemePalette customPalette{settings->customBackground, settings->customText,
                                settings->customText, settings->customAccent};
    g_currentPalette = ResolvePalette(settings->theme, systemIsDark, customPalette);

    SetWindowPos(hwnd, HWND_TOPMOST, g_currentLayout.x, g_currentLayout.y,
                 g_currentLayout.width, g_currentLayout.height, SWP_NOACTIVATE);

    PaintOsdContent(hwnd, g_currentLayout, g_currentPalette, g_currentSnapshot,
                    g_currentAlpha);
}
```

Note: `customPalette` reuses `customText` for both `text` and `textSecondary` since the mod only exposes three custom color settings (background/text/accent) per spec §9 — this is intentional, not an oversight.

- [ ] **Step 2: Run the compile-check script**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 3: Temporarily wire window creation into Wh_ModInit for a manual visual check**

Temporarily add to `Wh_ModInit` (after `InstallKeyboardHook()`), and remember these two lines get replaced by Task 10's real thread-based wiring:

```cpp
    g_osdWindow = CreateOsdWindow(GetModuleHandleW(nullptr));
    TrackSnapshot debugSnapshot;
    debugSnapshot.title = L"Falling Down - Bonus Track";
    debugSnapshot.artist = L"Lil Peep";
    debugSnapshot.status = PlaybackState::Playing;
    UpdateOsdContent(g_osdWindow, debugSnapshot);
    ShowWindow(g_osdWindow, SW_SHOWNOACTIVATE);
```

- [ ] **Step 4: Manual verification**

Load the mod into Windhawk and enable it. Confirm a rounded, blurred panel appears near the bottom-right of the screen (next to where the system tray/clock is) showing "Falling Down - Bonus Track" / "Lil Peep" with three circular buttons. Check both Windows light and dark mode (Settings > Personalization > Colors) to confirm the `auto` theme setting switches the panel's look accordingly. Disable the mod afterward.

- [ ] **Step 5: Remove the temporary debug wiring**

Remove the 6 lines added in Step 3 from `Wh_ModInit` (Task 10 replaces them with the real flow).

- [ ] **Step 6: Run the compile-check script again**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 7: Run the unit tests**

Run: `bash scripts/run-unit-tests.sh`
Expected: `63/63 tests passed`.

- [ ] **Step 8: Commit**

```bash
git add SoundTrackAutoView.cpp
git commit -m "Add OSD window chrome and static rendering"
```

---

### Task 9: OSD interactivity (buttons, hover, fade animation, auto-hide)

**Files:**
- Modify: `SoundTrackAutoView.cpp` (guarded section)

**Interfaces:**
- Consumes: `OsdTimingController` (Task 4), `HitTestButton`, `ButtonLayout`, `ComputeButtonLayout` (Task 3), `SendPlayPauseCommand`/`SendNextTrackCommand`/`SendPreviousTrackCommand` (Task 7), `PaintOsdContent`, `g_osdWindow`, `g_currentLayout`, `g_currentPalette`, `g_currentSnapshot`, `g_currentAlpha`, `UpdateOsdContent` (Task 8).
- Produces: `OsdTimingController g_timingController`, `void TriggerOsdDisplay(const TrackSnapshot&)`, `void ApplySmtcCorrection(const TrackSnapshot&)`. Used by Task 10 (wire-up, which calls these from the OSD thread's message loop).

This task has no automated test — it's real window message handling and timers. It's verified by compile-check and a manual interaction check.

- [ ] **Step 1: Add animation state, timers, and the trigger/correction entry points**

Add to the guarded section, after Task 8's code:

```cpp
static constexpr UINT_PTR kFadeTimerId = 1;
static constexpr UINT_PTR kAutoHideTimerId = 2;
static constexpr UINT kFadeTimerIntervalMs = 15;
static constexpr double kFadeInSeconds = 0.15;
static constexpr double kFadeOutSeconds = 0.20;

static OsdTimingController g_timingController(4.0);
static bool g_fadingIn = false;
static bool g_fadingOut = false;

static double NowInSeconds() {
    return GetTickCount64() / 1000.0;
}

static void StepFadeAnimation(HWND hwnd) {
    double step = kFadeTimerIntervalMs / 1000.0;
    if (g_fadingIn) {
        g_currentAlpha += static_cast<float>(step / kFadeInSeconds);
        if (g_currentAlpha >= 1.0f) {
            g_currentAlpha = 1.0f;
            g_fadingIn = false;
            KillTimer(hwnd, kFadeTimerId);
        }
    } else if (g_fadingOut) {
        g_currentAlpha -= static_cast<float>(step / kFadeOutSeconds);
        if (g_currentAlpha <= 0.0f) {
            g_currentAlpha = 0.0f;
            g_fadingOut = false;
            KillTimer(hwnd, kFadeTimerId);
            ShowWindow(hwnd, SW_HIDE);
            g_timingController.MarkHidden();
            return;
        }
    }
    PaintOsdContent(hwnd, g_currentLayout, g_currentPalette, g_currentSnapshot,
                    g_currentAlpha);
}

static void StartHideAnimation(HWND hwnd) {
    KillTimer(hwnd, kAutoHideTimerId);
    g_fadingIn = false;
    g_fadingOut = true;
    SetTimer(hwnd, kFadeTimerId, kFadeTimerIntervalMs, nullptr);
}

void TriggerOsdDisplay(const TrackSnapshot& snapshot) {
    double now = NowInSeconds();
    auto result = g_timingController.OnTrigger(now);

    UpdateOsdContent(g_osdWindow, snapshot);

    if (result.shouldPlayFadeIn) {
        ShowWindow(g_osdWindow, SW_SHOWNOACTIVATE);
        g_currentAlpha = 0.0f;
        g_fadingIn = true;
        g_fadingOut = false;
        SetTimer(g_osdWindow, kFadeTimerId, kFadeTimerIntervalMs, nullptr);
    } else {
        g_currentAlpha = 1.0f;
        PaintOsdContent(g_osdWindow, g_currentLayout, g_currentPalette,
                        g_currentSnapshot, g_currentAlpha);
    }

    SetTimer(g_osdWindow, kAutoHideTimerId, 100, nullptr);
}

void ApplySmtcCorrection(const TrackSnapshot& snapshot) {
    double now = NowInSeconds();
    if (!g_timingController.OnSmtcUpdate(now)) {
        return;
    }
    UpdateOsdContent(g_osdWindow, snapshot);
    PaintOsdContent(g_osdWindow, g_currentLayout, g_currentPalette,
                    g_currentSnapshot, g_currentAlpha);
}
```

- [ ] **Step 2: Wire mouse and timer handling into the window procedure**

Replace `OsdWndProc` (added in Task 8) with:

```cpp
static LRESULT CALLBACK OsdWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
    switch (msg) {
        case WM_TIMER: {
            if (wParam == kFadeTimerId) {
                StepFadeAnimation(hwnd);
                return 0;
            }
            if (wParam == kAutoHideTimerId) {
                if (g_timingController.ShouldHideNow(NowInSeconds())) {
                    KillTimer(hwnd, kAutoHideTimerId);
                    StartHideAnimation(hwnd);
                }
                return 0;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g_timingController.OnMouseEnter(NowInSeconds());
            return 0;
        }
        case WM_MOUSELEAVE: {
            g_timingController.OnMouseLeave(NowInSeconds());
            return 0;
        }
        case WM_LBUTTONUP: {
            PanelLayout panelForButtons{0, 0, g_currentLayout.width,
                                         g_currentLayout.height};
            ButtonLayout buttons = ComputeButtonLayout(panelForButtons);
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            switch (HitTestButton(buttons, pt)) {
                case OsdButton::Previous:
                    SendPreviousTrackCommand();
                    break;
                case OsdButton::PlayPause:
                    SendPlayPauseCommand();
                    break;
                case OsdButton::Next:
                    SendNextTrackCommand();
                    break;
                case OsdButton::None:
                    break;
            }
            g_timingController.OnTrigger(NowInSeconds());
            SetTimer(hwnd, kAutoHideTimerId, 100, nullptr);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
```

Add `#include <windowsx.h>` near the other guarded-section includes (for `GET_X_LPARAM`/`GET_Y_LPARAM`).

- [ ] **Step 3: Run the compile-check script**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 4: Manual verification**

Temporarily add the following to `Wh_ModInit` (after `InstallKeyboardHook()`):

```cpp
    g_osdWindow = CreateOsdWindow(GetModuleHandleW(nullptr));
    TrackSnapshot debugSnapshot;
    debugSnapshot.title = L"Falling Down - Bonus Track";
    debugSnapshot.artist = L"Lil Peep";
    debugSnapshot.status = PlaybackState::Playing;
    TriggerOsdDisplay(debugSnapshot);
```

Load the mod into Windhawk and enable it. Confirm: the panel fades in smoothly (not an instant snap); hovering the mouse over it prevents it from disappearing; moving the mouse away makes it disappear after the full configured duration; clicking the play/pause button actually pauses/resumes whatever is currently playing. Remove the 6 temporary lines above afterward (Task 10 replaces them with the real flow) and disable the mod.

- [ ] **Step 5: Run the unit tests (confirm nothing in the pure-logic build broke)**

Run: `bash scripts/run-unit-tests.sh`
Expected: `63/63 tests passed`.

- [ ] **Step 6: Commit**

```bash
git add SoundTrackAutoView.cpp
git commit -m "Add OSD interactivity: buttons, hover pause, fade animation, auto-hide"
```

---

### Task 10: Full wire-up (OSD thread, message loop, SMTC subscription, mod lifecycle)

**Files:**
- Modify: `SoundTrackAutoView.cpp` (guarded section — this is the final, authoritative version of `Wh_ModInit`/`Wh_ModUninit`)

**Interfaces:**
- Consumes: everything from Tasks 5-9.
- Produces: the final `Wh_ModInit`, `Wh_ModUninit`, `Wh_ModSettingsChanged`, plus `OsdThreadProc`, `EnsureSmtcSubscription`.

This task has no automated test — it's thread lifecycle and real OS integration. It's verified by compile-check and the comprehensive manual QA pass in Task 11.

- [ ] **Step 1: Add the OSD thread and SMTC event subscription**

Add to the guarded section, after Task 9's code:

```cpp
static constexpr UINT kOsdCorrectionMessage = WM_APP + 2;

static HANDLE g_osdThreadHandle = nullptr;
static winrt::event_token g_playbackInfoToken;
static winrt::event_token g_propertiesToken;
static GlobalSystemMediaTransportControlsSession g_subscribedSession{nullptr};

static void EnsureSmtcSubscription() {
    if (!EnsureSessionManager()) return;
    auto session = g_sessionManager.GetCurrentSession();
    if (!session || session == g_subscribedSession) return;

    g_subscribedSession = session;
    g_playbackInfoToken = session.PlaybackInfoChanged([](auto&&, auto&&) {
        DWORD threadId = g_osdThreadId.load();
        if (threadId != 0) {
            PostThreadMessageW(threadId, kOsdCorrectionMessage, 0, 0);
        }
    });
    g_propertiesToken = session.MediaPropertiesChanged([](auto&&, auto&&) {
        DWORD threadId = g_osdThreadId.load();
        if (threadId != 0) {
            PostThreadMessageW(threadId, kOsdCorrectionMessage, 0, 0);
        }
    });
}

static DWORD WINAPI OsdThreadProc(LPVOID) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    g_osdWindow = CreateOsdWindow(GetModuleHandleW(nullptr));

    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // ensure a message queue exists
    g_osdThreadId.store(GetCurrentThreadId());

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kOsdTriggerMessage) {
            auto settings = GetCurrentSettings();
            g_timingController = OsdTimingController(settings->durationSeconds);

            auto snapshot = TryGetCurrentTrackSnapshot();
            if (ShouldShowPanel(snapshot)) {
                EnsureSmtcSubscription();
                TriggerOsdDisplay(*snapshot);
            }
        } else if (msg.message == kOsdCorrectionMessage) {
            auto snapshot = TryGetCurrentTrackSnapshot();
            if (ShouldShowPanel(snapshot)) {
                ApplySmtcCorrection(*snapshot);
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_osdWindow) {
        DestroyWindow(g_osdWindow);
        g_osdWindow = nullptr;
    }
    winrt::uninit_apartment();
    return 0;
}
```

Note: `g_timingController` is reconstructed with the current `durationSeconds` setting on every trigger, so a `duration` change made via Windhawk's settings UI (which calls `Wh_ModSettingsChanged`, which calls `RefreshSettingsFromWindhawk`) takes effect on the next media key press without needing a mod reload.

- [ ] **Step 2: Replace Wh_ModInit, Wh_ModUninit, and Wh_ModSettingsChanged with their final versions**

```cpp
BOOL Wh_ModInit() {
    Wh_Log(L"SoundTrackAutoView: init");

    RefreshSettingsFromWindhawk();

    if (!InstallKeyboardHook()) {
        return FALSE;
    }

    g_osdThreadHandle = CreateThread(nullptr, 0, OsdThreadProc, nullptr, 0, nullptr);
    if (!g_osdThreadHandle) {
        Wh_Log(L"SoundTrackAutoView: failed to create OSD thread, error %lu",
               GetLastError());
        UninstallKeyboardHook();
        return FALSE;
    }

    // Wait briefly for the OSD thread to publish its thread id before
    // returning, so a media key pressed immediately after mod load isn't
    // dropped (PostThreadMessage silently fails if the thread id is 0 or
    // doesn't have a message queue yet).
    for (int i = 0; i < 50 && g_osdThreadId.load() == 0; i++) {
        Sleep(10);
    }

    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"SoundTrackAutoView: uninit");

    UninstallKeyboardHook();

    DWORD threadId = g_osdThreadId.load();
    if (threadId != 0) {
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }
    if (g_osdThreadHandle) {
        WaitForSingleObject(g_osdThreadHandle, 2000);
        CloseHandle(g_osdThreadHandle);
        g_osdThreadHandle = nullptr;
    }

    if (g_subscribedSession) {
        g_subscribedSession.PlaybackInfoChanged(g_playbackInfoToken);
        g_subscribedSession.MediaPropertiesChanged(g_propertiesToken);
        g_subscribedSession = nullptr;
    }
    g_sessionManager = nullptr;
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"SoundTrackAutoView: settings changed");
    RefreshSettingsFromWindhawk();
}
```

- [ ] **Step 3: Run the compile-check script**

Run: `bash scripts/compile-check.sh`
Expected: `Compile check passed.`

- [ ] **Step 4: Run the unit tests**

Run: `bash scripts/run-unit-tests.sh`
Expected: `63/63 tests passed`.

- [ ] **Step 5: Manual verification**

Load the mod into Windhawk and enable it. Start playing music in Spotify (or any SMTC-aware app). Press Play/Pause, Next, and Previous on your keyboard and confirm the panel appears each time with correct track info, auto-hides after the configured duration, and responds to hover/clicks as in Task 9. Leave the mod enabled for Task 11.

- [ ] **Step 6: Commit**

```bash
git add SoundTrackAutoView.cpp
git commit -m "Wire up OSD thread, message loop, and SMTC subscription lifecycle"
```

---

### Task 11: Full manual QA pass

**Files:** none (verification only).

This task has no code changes — it walks through spec §12's full testing plan on the completed mod. Keep a running note of any failures; if something doesn't match, fix it in `SoundTrackAutoView.cpp`, re-run `bash scripts/compile-check.sh` and `bash scripts/run-unit-tests.sh`, and re-test before checking the box.

- [ ] **Step 1: Physical media keys trigger the panel correctly**

With Spotify desktop playing: press Play/Pause, confirm the panel appears with correct title/artist/art and the correct play/pause icon. Press Next and Previous, confirm the panel updates to the new track each time.

- [ ] **Step 2: In-app clicks do NOT trigger the panel**

Click the pause button inside Spotify's own window (not a keyboard key). Confirm the panel does **not** appear.

- [ ] **Step 3: Autoplay does NOT trigger the panel**

Let a track finish naturally so Spotify auto-advances to the next queued track. Confirm the panel does **not** appear.

- [ ] **Step 4: Works across different SMTC-aware apps**

Repeat step 1 with a browser tab playing audio (e.g. a Spotify Web or YouTube tab) and with VLC or Windows Media Player. Confirm the panel reflects whichever app is the current SMTC session.

- [ ] **Step 5: Settings take effect without reload**

In Windhawk's mod settings UI, change `duration` to a different value (e.g. 10) and confirm a subsequent media key press respects the new duration. Set `theme` to each of `light`, `dark`, and `custom` (with sample hex colors for the three custom color settings) and confirm the panel's appearance updates accordingly without disabling/re-enabling the mod.

- [ ] **Step 6: Buttons control playback**

Click each of the panel's three buttons while a track is playing and confirm Previous/Play-Pause/Next actually change playback in the app, matching the app's own UI state afterward.

- [ ] **Step 7: Soak test**

Leave the mod enabled and Windows running normally (with occasional media key presses) for at least 30 minutes. Confirm no crash, no visible memory/handle growth in Task Manager for `explorer.exe`, and that `Wh_ModUninit` (disabling the mod) completes cleanly without hanging.

- [ ] **Step 8: Final commit**

If any fixes were made during this pass, commit them:

```bash
git add SoundTrackAutoView.cpp
git commit -m "Fix issues found during manual QA pass"
```

If no fixes were needed, no commit is required for this task.
