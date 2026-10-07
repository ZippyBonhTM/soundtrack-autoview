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
