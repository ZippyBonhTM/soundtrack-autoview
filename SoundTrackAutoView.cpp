// ==WindhawkMod==
// @id              soundtrack-autoview
// @name            SoundTrackAutoView
// @description     Shows a native-style "Now Playing" panel when you press a media key (play/pause/next/previous)
// @version         1.0
// @author          zippy
// @include         explorer.exe
// @compilerOptions -ld2d1 -ldwrite -ldwmapi -lwindowscodecs -lole32 -lruntimeobject -lshcore -lgdi32 -loleaut32
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

    void SetDurationSeconds(double durationSeconds) {
        durationSeconds_ = durationSeconds;
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

// A WH_KEYBOARD_LL hook's callback runs on the thread that installed it, and
// that thread must pump messages or Windows will eventually consider the
// hook unresponsive and silently remove it (or input across the system can
// lag while the hook is being serviced). Wh_ModInit's own calling thread
// isn't documented to pump messages, so the hook gets its own dedicated
// thread whose only job is installing the hook and running a message loop.
static HANDLE g_hookThreadHandle = nullptr;
static std::atomic<DWORD> g_hookThreadId{0};

static DWORD WINAPI HookThreadProc(LPVOID) {
    if (!InstallKeyboardHook()) {
        return 1;
    }

    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // ensure a message queue exists
    g_hookThreadId.store(GetCurrentThreadId());

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UninstallKeyboardHook();
    return 0;
}

#include <algorithm>
#include <chrono>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wincodec.h>
#include <shcore.h>

using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using winrt::Windows::Storage::Streams::IRandomAccessStreamReference;

// Waits for a WinRT async operation up to timeoutMs. Spec requires blocking
// SMTC/thumbnail reads to have a short timeout rather than hang the OSD
// thread (and, transitively, every subsequent trigger) on a slow or hung
// app. Returns false (and cancels the operation) on timeout or failure.
template <typename TAsync>
static bool WaitWithTimeout(TAsync const& async, int timeoutMs) {
    if (async.wait_for(std::chrono::milliseconds(timeoutMs)) ==
        AsyncStatus::Completed) {
        return true;
    }
    async.Cancel();
    return false;
}

static GlobalSystemMediaTransportControlsSessionManager g_sessionManager{nullptr};

static bool EnsureSessionManager() {
    if (g_sessionManager) {
        return true;
    }
    try {
        auto op = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        if (!WaitWithTimeout(op, 2000)) {
            Wh_Log(L"SoundTrackAutoView: timed out getting session manager");
            return false;
        }
        g_sessionManager = op.GetResults();
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

// Album art thumbnails come from whatever app currently owns the SMTC
// session, so their size can't be trusted: cap both the source image's
// dimensions and the final decoded size to keep a hostile or buggy app
// from forcing a multi-gigabyte allocation.
inline constexpr UINT kMaxThumbnailSourceDimension = 8192;
inline constexpr UINT kThumbnailTargetDimension = 256;

static bool TryDecodeThumbnail(IRandomAccessStreamReference const& thumbnailRef,
                                std::vector<uint8_t>& outPixelsBgra, int& outWidth,
                                int& outHeight) {
    try {
        auto openOp = thumbnailRef.OpenReadAsync();
        if (!WaitWithTimeout(openOp, 500)) {
            return false;
        }
        auto stream = openOp.GetResults();

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

        UINT sourceWidth = 0, sourceHeight = 0;
        winrt::check_hresult(frame->GetSize(&sourceWidth, &sourceHeight));
        if (sourceWidth == 0 || sourceHeight == 0 ||
            sourceWidth > kMaxThumbnailSourceDimension ||
            sourceHeight > kMaxThumbnailSourceDimension) {
            return false;
        }

        // Downscale to a fixed target size: the panel only ever draws this
        // art at a small fixed size, so decoding (and re-uploading to D2D
        // on every fade frame) at the source app's full resolution is pure
        // waste. Preserve aspect ratio, longest side = target dimension.
        double scale = static_cast<double>(kThumbnailTargetDimension) /
                       std::max(sourceWidth, sourceHeight);
        UINT targetWidth =
            std::max(1u, static_cast<UINT>(sourceWidth * scale + 0.5));
        UINT targetHeight =
            std::max(1u, static_cast<UINT>(sourceHeight * scale + 0.5));

        winrt::com_ptr<IWICBitmapSource> sourceForConversion = frame;
        if (targetWidth < sourceWidth || targetHeight < sourceHeight) {
            winrt::com_ptr<IWICBitmapScaler> scaler;
            winrt::check_hresult(wicFactory->CreateBitmapScaler(scaler.put()));
            winrt::check_hresult(scaler->Initialize(
                frame.get(), targetWidth, targetHeight,
                WICBitmapInterpolationModeFant));
            sourceForConversion = scaler;
        }

        winrt::com_ptr<IWICFormatConverter> converter;
        winrt::check_hresult(wicFactory->CreateFormatConverter(converter.put()));
        // Premultiplied to match the D2D1_ALPHA_MODE_PREMULTIPLIED bitmap
        // this gets uploaded into later (PaintOsdContent) — decoding
        // straight alpha here would make any translucent art render with
        // wrong (too-bright) colors once D2D treats it as premultiplied.
        winrt::check_hresult(converter->Initialize(
            sourceForConversion.get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));

        UINT width = 0, height = 0;
        winrt::check_hresult(converter->GetSize(&width, &height));

        outPixelsBgra.resize(static_cast<size_t>(width) * height * 4);
        winrt::check_hresult(converter->CopyPixels(
            nullptr, width * 4, static_cast<UINT>(outPixelsBgra.size()),
            outPixelsBgra.data()));

        outWidth = static_cast<int>(width);
        outHeight = static_cast<int>(height);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<TrackSnapshot> TryGetCurrentTrackSnapshot() {
    if (!EnsureSessionManager()) {
        return std::nullopt;
    }

    try {
        auto session = g_sessionManager.GetCurrentSession();
        if (!session) {
            return std::nullopt;
        }

        TrackSnapshot snapshot;

        auto playbackInfo = session.GetPlaybackInfo();
        snapshot.status = ToPlaybackState(playbackInfo.PlaybackStatus());

        auto propertiesOp = session.TryGetMediaPropertiesAsync();
        if (!WaitWithTimeout(propertiesOp, 500)) {
            Wh_Log(L"SoundTrackAutoView: timed out reading media properties");
            return std::nullopt;
        }
        auto properties = propertiesOp.GetResults();
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
    try {
        if (!EnsureSessionManager()) return;
        if (auto session = g_sessionManager.GetCurrentSession()) {
            session.TryTogglePlayPauseAsync();
        }
    } catch (const winrt::hresult_error& ex) {
        Wh_Log(L"SoundTrackAutoView: play/pause command failed: %s",
               ex.message().c_str());
    }
}

void SendNextTrackCommand() {
    try {
        if (!EnsureSessionManager()) return;
        if (auto session = g_sessionManager.GetCurrentSession()) {
            session.TrySkipNextAsync();
        }
    } catch (const winrt::hresult_error& ex) {
        Wh_Log(L"SoundTrackAutoView: next-track command failed: %s",
               ex.message().c_str());
    }
}

void SendPreviousTrackCommand() {
    try {
        if (!EnsureSessionManager()) return;
        if (auto session = g_sessionManager.GetCurrentSession()) {
            session.TrySkipPreviousAsync();
        }
    } catch (const winrt::hresult_error& ex) {
        Wh_Log(L"SoundTrackAutoView: previous-track command failed: %s",
               ex.message().c_str());
    }
}

#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>

static const wchar_t kOsdWindowClassName[] = L"SoundTrackAutoViewOsdWindow";

// Returns this mod DLL's own HMODULE. GetModuleHandleW(nullptr) would
// return explorer.exe's handle instead, which is wrong for RegisterClassExW
// here: every Windhawk compile produces a differently-named mod DLL, so a
// class registered under explorer's handle survives a mod reload/update and
// a later CreateWindowExW can resolve it back to the old, already-unloaded
// DLL's window procedure, crashing explorer.
// CreateWindowInBand is an undocumented user32.dll export that places a
// window in a specific shell Z-order "band" instead of the ordinary
// topmost band. Plain HWND_TOPMOST windows lose z-order contests against
// the shell's own topmost elements (the taskbar in particular): when the
// taskbar is set to auto-hide and hidden, a plain topmost OSD window can
// end up visually buried and only resurfaces once the taskbar is shown
// again. ZBID_IMMERSIVE_NOTIFICATION is the band Windows uses for system
// notification toasts, which is exactly the visibility behavior this
// panel wants (always on top, independent of taskbar state). This
// approach is already used — and confirmed working on this machine — by
// the "Taskbar Music Lounge" Windhawk mod.
enum ZBID {
    ZBID_DEFAULT = 0,
    ZBID_IMMERSIVE_NOTIFICATION = 4,
};

using PCreateWindowInBand = HWND(WINAPI*)(DWORD dwExStyle, LPCWSTR lpClassName,
                                           LPCWSTR lpWindowName, DWORD dwStyle,
                                           int x, int y, int nWidth, int nHeight,
                                           HWND hWndParent, HMENU hMenu,
                                           HINSTANCE hInstance, LPVOID lpParam,
                                           DWORD dwBand);

// Falls back to a plain CreateWindowExW if CreateWindowInBand isn't
// available (undocumented API; not guaranteed present on every Windows
// build) or fails for any reason.
static HWND CreateOsdWindowHandle(DWORD exStyle, LPCWSTR className,
                                   LPCWSTR windowName, DWORD style,
                                   HINSTANCE hInstance) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto createWindowInBand = reinterpret_cast<PCreateWindowInBand>(
            GetProcAddress(user32, "CreateWindowInBand"));
        if (createWindowInBand) {
            HWND hwnd = createWindowInBand(
                exStyle, className, windowName, style, 0, 0, 1, 1, nullptr,
                nullptr, hInstance, nullptr, ZBID_IMMERSIVE_NOTIFICATION);
            if (hwnd) {
                return hwnd;
            }
            Wh_Log(L"SoundTrackAutoView: CreateWindowInBand failed, error %lu",
                   GetLastError());
        }
    }

    return CreateWindowExW(exStyle, className, windowName, style, 0, 0, 1, 1,
                            nullptr, nullptr, hInstance, nullptr);
}

static HMODULE GetOwnModuleHandle() {
    HMODULE hModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetOwnModuleHandle), &hModule);
    return hModule;
}

static winrt::com_ptr<ID2D1Factory> g_d2dFactory;
static winrt::com_ptr<IDWriteFactory> g_dwriteFactory;
static HWND g_osdWindow = nullptr;
static PanelLayout g_currentLayout;
static ThemePalette g_currentPalette;
static TrackSnapshot g_currentSnapshot;
static float g_currentAlpha = 1.0f;
static UINT g_currentDpi = 96;
static OsdButton g_hoveredButton = OsdButton::None;

// WM_LBUTTONUP/WM_MOUSEMOVE deliver client-area coordinates in physical
// pixels; drawing (and therefore HitTestButton) operates in the fixed DIP
// panel space (see PaintOsdContent's SetDpi comment). Converts one into
// the other so a click always lands on the button actually drawn there.
static POINT PhysicalPointToDip(POINT pt, UINT dpi) {
    return POINT{MulDiv(pt.x, 96, static_cast<int>(dpi)),
                 MulDiv(pt.y, 96, static_cast<int>(dpi))};
}

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

// Returns the current Windows accent color, or false if DWM can't report
// one (e.g. composition disabled), leaving outColor untouched.
static bool TryGetSystemAccentColor(RgbaColor& outColor) {
    DWORD colorization = 0;
    BOOL opaqueBlend = FALSE;
    if (FAILED(DwmGetColorizationColor(&colorization, &opaqueBlend))) {
        return false;
    }
    // DwmGetColorizationColor returns 0xAARRGGBB.
    outColor.a = 1.0f;
    outColor.r = ((colorization >> 16) & 0xFF) / 255.0f;
    outColor.g = ((colorization >> 8) & 0xFF) / 255.0f;
    outColor.b = (colorization & 0xFF) / 255.0f;
    return true;
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
                             const TrackSnapshot& snapshot, float alpha,
                             UINT dpi, OsdButton hoveredButton) {
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

    // The bitmap is sized in physical pixels (layout.width/height already
    // scaled by dpi/96), but every drawing coordinate below is authored in
    // DIPs against the fixed base panel size (kPanelBaseWidthDip x
    // kPanelBaseHeightDip). Telling the render target the real monitor DPI
    // here is what makes D2D scale those DIP coordinates up to fill the
    // larger physical bitmap — without it, a DC render target defaults to
    // 96 DPI and everything draws at its 100%-scale size in the corner of
    // a bitmap that's actually bigger, while ComputeButtonLayout (driven
    // by the scaled PanelLayout the hit-test code also uses) would still
    // compute button positions against the full scaled size, so drawn and
    // clickable regions would disagree.
    dcTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));

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

    float artSize = static_cast<float>(kPanelBaseHeightDip) - 24.0f;
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
    D2D1_RECT_F titleRect = D2D1::RectF(
        textLeft, 14.0f, static_cast<float>(kPanelBaseWidthDip) - 12.0f, 34.0f);
    D2D1_RECT_F artistRect = D2D1::RectF(
        textLeft, 36.0f, static_cast<float>(kPanelBaseWidthDip) - 12.0f, 54.0f);

    dcTarget->DrawText(snapshot.title.c_str(),
                        static_cast<UINT32>(snapshot.title.size()), titleFormat.get(),
                        titleRect, textBrush.get());
    dcTarget->DrawText(snapshot.artist.c_str(),
                        static_cast<UINT32>(snapshot.artist.size()),
                        artistFormat.get(), artistRect, textSecondaryBrush.get());

    PanelLayout panelForButtons{0, 0, kPanelBaseWidthDip, kPanelBaseHeightDip};
    ButtonLayout buttons = ComputeButtonLayout(panelForButtons);

    winrt::com_ptr<ID2D1SolidColorBrush> accentHoverBrush;
    dcTarget->CreateSolidColorBrush(
        D2D1::ColorF(palette.accent.r, palette.accent.g, palette.accent.b,
                     std::min(1.0f, palette.accent.a + 0.2f)),
        accentHoverBrush.put());

    auto drawButton = [&](const RECT& r, const wchar_t* glyph, OsdButton id) {
        D2D1_ELLIPSE circle = D2D1::Ellipse(
            D2D1::Point2F((r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f),
            (r.right - r.left) / 2.0f, (r.bottom - r.top) / 2.0f);
        dcTarget->FillEllipse(
            circle, (id == hoveredButton) ? accentHoverBrush.get() : accentBrush.get());

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

    drawButton(buttons.previous, L"", OsdButton::Previous);
    drawButton(buttons.playPause,
               snapshot.status == PlaybackState::Playing ? L"" : L"",
               OsdButton::PlayPause);
    drawButton(buttons.next, L"", OsdButton::Next);

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
                    g_currentAlpha, g_currentDpi, g_hoveredButton);
}

static void StartHideAnimation(HWND hwnd) {
    KillTimer(hwnd, kAutoHideTimerId);
    g_fadingIn = false;
    g_fadingOut = true;
    SetTimer(hwnd, kFadeTimerId, kFadeTimerIntervalMs, nullptr);
}

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

            PanelLayout panelForButtons{0, 0, kPanelBaseWidthDip,
                                         kPanelBaseHeightDip};
            ButtonLayout buttons = ComputeButtonLayout(panelForButtons);
            POINT physicalPt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT dipPt = PhysicalPointToDip(physicalPt, g_currentDpi);
            OsdButton hit = HitTestButton(buttons, dipPt);
            if (hit != g_hoveredButton) {
                g_hoveredButton = hit;
                PaintOsdContent(hwnd, g_currentLayout, g_currentPalette,
                                g_currentSnapshot, g_currentAlpha, g_currentDpi,
                                g_hoveredButton);
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            g_timingController.OnMouseLeave(NowInSeconds());
            if (g_hoveredButton != OsdButton::None) {
                g_hoveredButton = OsdButton::None;
                PaintOsdContent(hwnd, g_currentLayout, g_currentPalette,
                                g_currentSnapshot, g_currentAlpha, g_currentDpi,
                                g_hoveredButton);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            PanelLayout panelForButtons{0, 0, kPanelBaseWidthDip,
                                         kPanelBaseHeightDip};
            ButtonLayout buttons = ComputeButtonLayout(panelForButtons);
            POINT physicalPt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT pt = PhysicalPointToDip(physicalPt, g_currentDpi);
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

HWND CreateOsdWindow(HINSTANCE /*hInstance*/) {
    HINSTANCE ownModule = reinterpret_cast<HINSTANCE>(GetOwnModuleHandle());

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OsdWndProc;
    wc.hInstance = ownModule;
    wc.lpszClassName = kOsdWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateOsdWindowHandle(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kOsdWindowClassName, L"SoundTrackAutoView", WS_POPUP, ownModule);

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
    g_currentDpi = dpi;

    g_currentLayout = ComputePanelPosition(monitorInfo.rcWork, dpi);

    auto settings = GetCurrentSettings();
    bool systemIsDark = IsSystemInDarkMode();
    ThemePalette customPalette{settings->customBackground, settings->customText,
                                settings->customText, settings->customAccent};
    g_currentPalette = ResolvePalette(settings->theme, systemIsDark, customPalette);

    // The native flyout's "auto" look follows the system accent color, not
    // just light/dark; light/dark/custom keep their own defined accent.
    if (settings->theme == ThemeMode::Auto) {
        RgbaColor systemAccent;
        if (TryGetSystemAccentColor(systemAccent)) {
            g_currentPalette.accent = systemAccent;
        }
    }

    SetWindowPos(hwnd, HWND_TOPMOST, g_currentLayout.x, g_currentLayout.y,
                 g_currentLayout.width, g_currentLayout.height, SWP_NOACTIVATE);

    PaintOsdContent(hwnd, g_currentLayout, g_currentPalette, g_currentSnapshot,
                    g_currentAlpha, g_currentDpi, g_hoveredButton);
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
        // Already visible: cancel any in-progress fade-out from a previous
        // auto-hide timeout so the panel snaps back to fully visible
        // instead of continuing to fade away under a fresh trigger.
        KillTimer(g_osdWindow, kFadeTimerId);
        g_fadingIn = false;
        g_fadingOut = false;
        g_currentAlpha = 1.0f;
        PaintOsdContent(g_osdWindow, g_currentLayout, g_currentPalette,
                        g_currentSnapshot, g_currentAlpha, g_currentDpi,
                        g_hoveredButton);
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
                    g_currentSnapshot, g_currentAlpha, g_currentDpi,
                    g_hoveredButton);
}

static constexpr UINT kOsdCorrectionMessage = WM_APP + 2;

static HANDLE g_osdThreadHandle = nullptr;
static GlobalSystemMediaTransportControlsSession::PlaybackInfoChanged_revoker
    g_playbackInfoRevoker;
static GlobalSystemMediaTransportControlsSession::MediaPropertiesChanged_revoker
    g_propertiesRevoker;
static GlobalSystemMediaTransportControlsSession g_subscribedSession{nullptr};

// Assigning to an event_revoker revokes whatever registration it previously
// held before taking the new one, so switching the "current" SMTC session
// (e.g. Spotify -> a browser tab) can never leave a handler registered
// against a session this mod no longer tracks. Wh_ModUninit resets both
// revokers to default-constructed (empty, already-revoked) ones, which is
// also safe to do unconditionally even if no session was ever subscribed.
static void EnsureSmtcSubscription() {
    try {
        if (!EnsureSessionManager()) return;
        auto session = g_sessionManager.GetCurrentSession();
        if (!session || session == g_subscribedSession) return;

        g_subscribedSession = session;
        g_playbackInfoRevoker = session.PlaybackInfoChanged(
            winrt::auto_revoke, [](auto&&, auto&&) {
                DWORD threadId = g_osdThreadId.load();
                if (threadId != 0) {
                    PostThreadMessageW(threadId, kOsdCorrectionMessage, 0, 0);
                }
            });
        g_propertiesRevoker = session.MediaPropertiesChanged(
            winrt::auto_revoke, [](auto&&, auto&&) {
                DWORD threadId = g_osdThreadId.load();
                if (threadId != 0) {
                    PostThreadMessageW(threadId, kOsdCorrectionMessage, 0, 0);
                }
            });
    } catch (const winrt::hresult_error& ex) {
        Wh_Log(L"SoundTrackAutoView: failed to subscribe to session events: %s",
               ex.message().c_str());
    }
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
            g_timingController.SetDurationSeconds(settings->durationSeconds);

            auto snapshot = TryGetCurrentTrackSnapshot();
            if (ShouldShowPanel(snapshot)) {
                EnsureSmtcSubscription();
                TriggerOsdDisplay(*snapshot);
            }
        } else if (msg.message == kOsdCorrectionMessage) {
            // Cheap check first: most SMTC events land outside the ~1s
            // correction window and should never pay for a session read
            // plus thumbnail decode just to be discarded.
            if (g_timingController.OnSmtcUpdate(NowInSeconds())) {
                auto snapshot = TryGetCurrentTrackSnapshot();
                if (ShouldShowPanel(snapshot)) {
                    ApplySmtcCorrection(*snapshot);
                }
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
    UnregisterClassW(kOsdWindowClassName,
                      reinterpret_cast<HINSTANCE>(GetOwnModuleHandle()));

    // Release every WinRT object on the same thread (and MTA apartment)
    // that created it, before tearing the apartment down. Doing this from
    // Wh_ModUninit's caller thread instead would release cross-apartment
    // COM proxies from the wrong thread, which is unsafe.
    g_playbackInfoRevoker = {};
    g_propertiesRevoker = {};
    g_subscribedSession = nullptr;
    g_sessionManager = nullptr;
    g_d2dFactory = nullptr;
    g_dwriteFactory = nullptr;

    winrt::uninit_apartment();
    return 0;
}

// @include explorer.exe also matches non-shell explorer.exe instances: a
// spawned folder/file-dialog process, COM "-Embedding"/"-ServerName:" host
// processes, and (on this dev machine specifically) other Windhawk mods'
// "-tool-mod" helper instances. Only the real shell process owns the
// taskbar's Shell_TrayWnd window, so that's used to tell it apart from the
// rest; installing the hook and showing panels from every matching instance
// would mean duplicate, stacked panels.
static bool IsMainShellExplorerProcess() {
    HWND trayWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!trayWnd) {
        return false;
    }
    DWORD trayProcessId = 0;
    GetWindowThreadProcessId(trayWnd, &trayProcessId);
    return trayProcessId == GetCurrentProcessId();
}

BOOL Wh_ModInit() {
    Wh_Log(L"SoundTrackAutoView: init");

    if (!IsMainShellExplorerProcess()) {
        Wh_Log(L"SoundTrackAutoView: not the main shell process, skipping");
        return TRUE;
    }

    RefreshSettingsFromWindhawk();

    g_hookThreadHandle = CreateThread(nullptr, 0, HookThreadProc, nullptr, 0, nullptr);
    if (!g_hookThreadHandle) {
        Wh_Log(L"SoundTrackAutoView: failed to create hook thread, error %lu",
               GetLastError());
        return FALSE;
    }

    g_osdThreadHandle = CreateThread(nullptr, 0, OsdThreadProc, nullptr, 0, nullptr);
    if (!g_osdThreadHandle) {
        Wh_Log(L"SoundTrackAutoView: failed to create OSD thread, error %lu",
               GetLastError());
        DWORD hookThreadId = g_hookThreadId.load();
        if (hookThreadId != 0) {
            PostThreadMessageW(hookThreadId, WM_QUIT, 0, 0);
        }
        WaitForSingleObject(g_hookThreadHandle, INFINITE);
        CloseHandle(g_hookThreadHandle);
        g_hookThreadHandle = nullptr;
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

    // Signal both threads to quit, then wait for them to fully exit before
    // returning. Windhawk unloads this DLL right after Wh_ModUninit
    // returns, so giving up early (e.g. on a timeout) would let either
    // thread keep running code that lives in a DLL that's no longer
    // mapped — a guaranteed crash. Each thread uninstalls its own hook /
    // destroys its own window and releases its own WinRT objects before
    // its loop exits (see HookThreadProc / OsdThreadProc).
    DWORD hookThreadId = g_hookThreadId.load();
    if (hookThreadId != 0) {
        PostThreadMessageW(hookThreadId, WM_QUIT, 0, 0);
    }
    if (g_hookThreadHandle) {
        WaitForSingleObject(g_hookThreadHandle, INFINITE);
        CloseHandle(g_hookThreadHandle);
        g_hookThreadHandle = nullptr;
    }

    DWORD osdThreadId = g_osdThreadId.load();
    if (osdThreadId != 0) {
        PostThreadMessageW(osdThreadId, WM_QUIT, 0, 0);
    }
    if (g_osdThreadHandle) {
        WaitForSingleObject(g_osdThreadHandle, INFINITE);
        CloseHandle(g_osdThreadHandle);
        g_osdThreadHandle = nullptr;
    }
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"SoundTrackAutoView: settings changed");
    RefreshSettingsFromWindhawk();
}

#endif  // SOUNDTRACK_AUTOVIEW_TEST_BUILD
