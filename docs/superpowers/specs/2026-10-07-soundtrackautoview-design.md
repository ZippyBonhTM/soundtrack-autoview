# SoundTrackAutoView — Design Spec

Date: 2026-10-07
Status: Approved for implementation planning

## 1. Purpose

A [Windhawk](https://windhawk.net) mod that shows a small on-screen panel — styled like
Windows 11's native "Now Playing" flyout card (the one that appears next to the
system tray/clock) — whenever the user issues an explicit media command
(play, pause, next track, previous track). The panel displays the current
track's art, title, artist and playback state, auto-hides after a
configurable duration, supports theming (including custom colors), and lets
the user click its transport buttons to actually control playback.

It must **not** appear for playback changes the app makes on its own
(autoplay advancing to the next track) or for clicks on transport controls
inside the player app's own UI — only for an explicit user command.

## 2. Non-goals

- Replicating the full Windows 11 Quick Settings flyout (toggle tiles for
  accessibility, energy saver, night light, etc.) — only the Now Playing
  card is in scope.
- Distinguishing "next" from "previous" visually (e.g. a directional arrow)
  — the native panel doesn't do this either; the panel simply reflects the
  updated track/state, matching native behavior.
- Capturing media commands sent through mechanisms other than simulated
  key input (see §8 Known Limitations).

## 3. Target process & distribution

Single-file Windhawk mod (`SoundTrackAutoView.cpp`) with the standard
Windhawk metadata header, targeting `explorer.exe` (`@include explorer.exe`).
Explorer is always running, hosts the shell/taskbar, and is the conventional
target for Windhawk mods that need a persistent, system-wide overlay.

Required metadata fields: `@id`, `@name`, `@description`, `@version`,
`@author`, `@include explorer.exe`. `@compilerOptions` will need to link
WinRT/COM support and DirectX/DWM libraries (see §9).

## 4. Architecture overview

```
┌─────────────────────────────────────────────────────────────┐
│ explorer.exe (Windhawk-injected mod)                         │
│                                                                │
│  Wh_ModInit                                                   │
│    ├─ Install WH_KEYBOARD_LL hook (main thread's hook chain)  │
│    ├─ Spawn OSD thread:                                       │
│    │    ├─ CoInitializeEx (MTA) / RoInitialize                │
│    │    ├─ Create hidden message-only pump                    │
│    │    ├─ Lazily acquire GSMTC session manager on first use  │
│    │    └─ Own the OSD popup window (created lazily)          │
│    └─ Register Wh_ModSettingsChanged callback                 │
│                                                                │
│  Keyboard hook callback (low-level, runs on hook thread)       │
│    └─ On VK_MEDIA_PLAY_PAUSE / NEXT_TRACK / PREV_TRACK / STOP  │
│         key-down → PostThreadMessage to OSD thread             │
│                                                                │
│  OSD thread loop                                               │
│    ├─ On "command" message:                                   │
│    │    ├─ Query current GSMTC session for track + playback   │
│    │    │   status snapshot                                   │
│    │    ├─ Show/update popup with snapshot, (re)start the      │
│    │    │   auto-hide timer                                   │
│    │    └─ Arm a short (~1s) correction window: if             │
│    │        PlaybackInfoChanged/MediaPropertiesChanged fires   │
│    │        within it, refresh the panel in place              │
│    ├─ Outside the correction window: SMTC change events are    │
│    │   ignored entirely (no panel trigger from app-only        │
│    │   changes)                                                │
│    └─ Button clicks (prev/play-pause/next) call the matching   │
│        GSMTC Try*Async control method and reset the timer      │
│                                                                │
│  Wh_ModUninit                                                  │
│    └─ Unhook keyboard hook, signal OSD thread to unsubscribe    │
│        SMTC events, destroy window, release WinRT, join thread │
└─────────────────────────────────────────────────────────────┘
```

## 5. Trigger: keyboard command detection

- A `WH_KEYBOARD_LL` low-level keyboard hook is installed from within the
  mod (no cross-process injection required — low-level hooks run in the
  installing process/thread).
- Filters key-down events for `VK_MEDIA_PLAY_PAUSE`, `VK_MEDIA_NEXT_TRACK`,
  `VK_MEDIA_PREV_TRACK`, `VK_MEDIA_STOP`.
- This catches: physical multimedia keyboard keys, Bluetooth/wireless
  headset media buttons (the OS driver synthesizes the same virtual-key
  codes), and the overwhelming majority of keyboard macro/scripting
  software (Logitech, Razer, Corsair, AutoHotkey, etc.), because the
  standard and most compatible way such software implements "send media
  key" is by simulating the real keystroke via `SendInput`/`keybd_event`
  — which flows through the same low-level input pipeline this hook reads.
- Deliberately **not** used as triggers: SMTC `PlaybackInfoChanged` /
  `MediaPropertiesChanged` events arriving outside the short correction
  window described in §6, and any in-app UI interaction that doesn't
  produce a media key event.

## 6. Data source: SMTC (System Media Transport Controls)

- Uses `Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager`
  via C++/WinRT, acquired once (lazily, on first command) and cached on the
  OSD thread.
- On a trigger (§5), reads the manager's current session synchronously
  (best-effort, with a short timeout) to get:
  - `MediaProperties` (title, artist, thumbnail `IRandomAccessStreamReference`)
  - `PlaybackInfo.PlaybackStatus` (Playing/Paused/Stopped)
- Because an app may take tens to a couple hundred ms to update its SMTC
  state after receiving the command, the panel shows immediately with
  whatever is currently available, then stays subscribed to
  `PlaybackInfoChanged`/`MediaPropertiesChanged` for ~1 second afterward
  only, refreshing the displayed content in place if an update lands
  within that window. After the window closes, the subscription goes
  quiet (events are received but not acted upon) until the next trigger.
- Thumbnail decoding: the `IRandomAccessStreamReference` is opened and
  decoded via WIC into a Direct2D bitmap. Decode failures fall back to a
  neutral placeholder glyph (music note).

## 7. Window & rendering

- A single popup window (`WS_POPUP`, layered, topmost, `WS_EX_TOOLWINDOW`
  so it's excluded from the taskbar and Alt-Tab), created lazily on first
  use and reused for subsequent triggers (shown/hidden/updated, not
  recreated).
- Positioned at the bottom-right corner of the work area of the monitor
  hosting the primary taskbar's notification area, matching where the
  native flyout appears.
- Rendered with Direct2D + DirectWrite (text) + WIC (album art), redrawn
  on content/theme change only — no continuous render loop.
- Visual chrome:
  - Rounded corners via `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)`.
  - Backdrop blur via `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE,
    DWMSBT_TRANSIENTWINDOW)` on Windows 11 22H2+ — the same backdrop
    material the native transient flyouts use.
  - Fallback on older Windows builds (no `DWMSBT_TRANSIENTWINDOW` support):
    a solid themed fill at fixed alpha (~85%) via layered window alpha
    blending, approximating the acrylic look.
  - Re-queries DPI on show (`GetDpiForWindow`) and scales drawing
    accordingly for per-monitor DPI correctness.
- Content: album art thumbnail, track title, artist, and three button
  hit-regions (previous / play-pause / next) with hover-state redraw.
- Interaction:
  - Clicking a button calls the matching GSMTC control method
    (`TrySkipPreviousAsync` / `TryTogglePlayPauseAsync` /
    `TrySkipNextAsync`) and resets the auto-hide timer.
  - Moving the mouse over the panel pauses the auto-hide countdown;
    moving it off resumes the countdown from the configured duration.
- Lifecycle: on trigger, cancel any pending hide timer, update content,
  fade in (~150ms alpha animation) if currently hidden, (re)start a
  `SetTimer` for the configured duration; on fire, fade out (~200ms) then
  `ShowWindow(SW_HIDE)`. Rapid repeated triggers update content in place
  without replaying the fade-in.

## 8. Theming

Resolved fresh each time the panel is shown (no live registry-change
listener needed), from the `theme` setting:

- `auto` (default) — follows current Windows light/dark mode
  (`AppsUseLightTheme` registry value) and accent color
  (`DwmGetColorizationColor`).
- `light` / `dark` — fixed palette regardless of the system setting.
- `custom` — uses `customBackgroundColor`, `customTextColor`,
  `customAccentColor` (hex strings) from settings.

Because the panel is fully self-drawn (not a reskin of the native flyout),
it is unaffected by other Windhawk mods that restyle the taskbar or native
flyouts — it always renders on top with whichever theme is configured here.

## 9. Settings (Windhawk mod settings block)

| Setting | Type | Default | Notes |
|---|---|---|---|
| `duration` | int (seconds) | 4 | How long the panel stays visible before auto-hiding. |
| `theme` | enum: auto/light/dark/custom | auto | |
| `customBackgroundColor` | hex string | `#2C2C2C` | Used only when `theme=custom`. |
| `customTextColor` | hex string | `#FFFFFF` | Used only when `theme=custom`. |
| `customAccentColor` | hex string | `#0078D4` | Used only when `theme=custom`. |

Settings changes are picked up via the `Wh_ModSettingsChanged` callback
without requiring a mod reload.

`@compilerOptions` will need to add the libraries required for WinRT/COM
activation and Direct2D/DirectWrite/DWM/WIC (`-lruntimeobject -ld2d1
-ldwrite -ldwmapi -lwindowscodecs -lole32`), plus C++/WinRT header usage
(header-only, ships with the Windows SDK).

## 10. Error handling & edge cases

- No active media session → the panel is never created/shown; a trigger
  with no session is a silent no-op.
- Album art missing or fails to decode → neutral music-note placeholder.
- GSMTC manager request fails (e.g. unsupported Windows version) → logged
  via `Wh_Log`; the mod stays inert for triggers (no retry storm — one
  retry after a short delay, then give up until the next trigger).
- Explorer restart → Windhawk reloads the mod automatically;
  `Wh_ModInit` re-runs cleanly (hook reinstalled, OSD thread
  respawned).
- Multi-monitor → panel shows on the monitor hosting the primary
  taskbar's notification area.
- DPI change between shows → re-queried and re-scaled on each show.
- Rapid-fire key presses → debounced/coalesced; content updates in place,
  auto-hide timer resets, no stacking of windows or animations.

## 11. Known limitations

- Keyboard macro/scripting software that issues the media command through
  a mechanism other than simulated key input (e.g. directly messaging the
  target app, or calling a proprietary API) will not be detected by the
  low-level keyboard hook. Standard practice for such software is to
  simulate the actual media keystroke — which this mod (and Windows' own
  native flyout) both rely on — so this should cover the large majority of
  real-world setups. This will be called out in the mod's description.
- Directional intent (next vs. previous) is not visually distinguished,
  matching native behavior — only the resulting track/state is shown.

## 12. Testing plan

- Manual verification with Spotify desktop, a browser tab (Spotify
  Web/YouTube — both implement SMTC), and VLC/Windows Media Player:
  - Physical media keys: play, pause, next, previous all show the panel
    with correct content.
  - Clicking transport controls inside the app's own UI: panel does
    **not** appear.
  - Track auto-advancing on its own (playlist/autoplay): panel does
    **not** appear.
  - Switching which app is the "current" SMTC session, then issuing a
    command: panel reflects the right session's data.
- Settings: changing `duration` takes effect without reload; each `theme`
  value (including `custom` with sample hex colors) renders correctly in
  both Windows light and dark mode.
- Button clicks on the panel actually control playback (verified against
  the app's own UI state).
- Soak test: mod left running over an extended idle period — no crash,
  no handle/COM leak; clean teardown on `Wh_ModUninit` (hook removed,
  SMTC events unsubscribed, window destroyed, WinRT released, thread
  joined).
