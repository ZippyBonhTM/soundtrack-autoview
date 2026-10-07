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
#include <optional>
#include <vector>
#include <cstdint>

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

// ---------------------------------------------------------------------------
// Windhawk mod entry points and OS-integration code. Excluded from the unit
// test build since they depend on the Windhawk engine (Wh_* functions) and
// real OS hooks, neither of which exist in a standalone test executable.
// ---------------------------------------------------------------------------
#ifndef SOUNDTRACK_AUTOVIEW_TEST_BUILD

#include <memory>
#include <mutex>

static std::mutex g_settingsMutex;
static std::shared_ptr<const ModSettings> g_settings =
    std::make_shared<const ModSettings>();

std::shared_ptr<const ModSettings> GetCurrentSettings() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settings;
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

    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = settings;
}

#include <atomic>

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

void Wh_ModSettingsChanged() {
    Wh_Log(L"SoundTrackAutoView: settings changed");
    RefreshSettingsFromWindhawk();
}

#endif  // SOUNDTRACK_AUTOVIEW_TEST_BUILD
