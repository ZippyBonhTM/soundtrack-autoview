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

static void Test_ParseCornerStyle_RecognizedValues() {
    CHECK(ParseCornerStyle(L"round") == CornerStyle::Round);
    CHECK(ParseCornerStyle(L"small") == CornerStyle::Small);
    CHECK(ParseCornerStyle(L"square") == CornerStyle::Square);
}

static void Test_ParseCornerStyle_UnknownDefaultsToRound() {
    CHECK(ParseCornerStyle(L"nonsense") == CornerStyle::Round);
}

static void Test_CornerRadiusDipForStyle_MapsEachStyle() {
    CHECK(CornerRadiusDipForStyle(CornerStyle::Round) == kCornerRadiusRoundDip);
    CHECK(CornerRadiusDipForStyle(CornerStyle::Small) == kCornerRadiusSmallDip);
    CHECK(CornerRadiusDipForStyle(CornerStyle::Square) == 0.0f);
}

static void Test_ClampBackgroundOpacityPercent_ClampsToRange() {
    CHECK(ClampBackgroundOpacityPercent(-10) == 0);
    CHECK(ClampBackgroundOpacityPercent(150) == 100);
    CHECK(ClampBackgroundOpacityPercent(50) == 50);
}

static void Test_ApplyBackgroundOpacity_SetsAlphaFromPercent() {
    RgbaColor color{0.5f, 0.5f, 0.5f, 1.0f};
    RgbaColor result = ApplyBackgroundOpacity(color, 50);
    CHECK(result.a > 0.49f && result.a < 0.51f);
    CHECK(result.r == 0.5f);
}

static void Test_BuildSettings_NewFieldsDefaultWhenNotProvided() {
    ModSettings settings =
        BuildSettings(4, L"auto", L"#2C2C2C", L"#FFFFFF", L"#0078D4");
    CHECK(settings.backgroundOpacityPercent == 85);
    CHECK(settings.cornerStyle == CornerStyle::Round);
}

static void Test_BuildSettings_NewFieldsAppliedWhenProvided() {
    ModSettings settings = BuildSettings(4, L"auto", L"#2C2C2C", L"#FFFFFF",
                                          L"#0078D4", 40, L"square");
    CHECK(settings.backgroundOpacityPercent == 40);
    CHECK(settings.cornerStyle == CornerStyle::Square);
}

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

static void Test_OsdTimingController_SetDurationSecondsDoesNotResetVisibility() {
    OsdTimingController controller(4.0);
    controller.OnTrigger(0.0);
    controller.SetDurationSeconds(10.0);
    CHECK(controller.IsVisible());
    auto result = controller.OnTrigger(0.5);
    CHECK(!result.shouldPlayFadeIn);  // still visible, no fade-in replay
}

static void Test_OsdTimingController_SetDurationSecondsAffectsNextDeadline() {
    OsdTimingController controller(4.0);
    controller.OnTrigger(0.0);
    controller.SetDurationSeconds(10.0);
    controller.OnTrigger(0.0);  // re-trigger, now under the new duration
    CHECK(!controller.ShouldHideNow(9.9));
    CHECK(controller.ShouldHideNow(10.0));
}

static void Test_ShouldShowPanel_TrueWhenSnapshotPresent() {
    TrackSnapshot snapshot;
    snapshot.title = L"Falling Down - Bonus Track";
    CHECK(ShouldShowPanel(std::optional<TrackSnapshot>(snapshot)));
}

static void Test_ShouldShowPanel_FalseWhenNoSession() {
    CHECK(!ShouldShowPanel(std::optional<TrackSnapshot>()));
}

int main() {
    Test_HarnessSmokeTest();
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
    Test_ParseCornerStyle_RecognizedValues();
    Test_ParseCornerStyle_UnknownDefaultsToRound();
    Test_CornerRadiusDipForStyle_MapsEachStyle();
    Test_ClampBackgroundOpacityPercent_ClampsToRange();
    Test_ApplyBackgroundOpacity_SetsAlphaFromPercent();
    Test_BuildSettings_NewFieldsDefaultWhenNotProvided();
    Test_BuildSettings_NewFieldsAppliedWhenProvided();
    Test_ComputePanelPosition_StandardDpi();
    Test_ComputePanelPosition_ScaledDpi();
    Test_ComputePanelPosition_SecondMonitorOffset();
    Test_ComputePanelPosition_ClampsOnTinyWorkArea();
    Test_ComputeButtonLayout_ThreeDistinctRegions();
    Test_HitTestButton_InsideEachButton();
    Test_HitTestButton_OutsideAnyButton();
    Test_OsdTimingController_FirstTriggerPlaysFadeIn();
    Test_OsdTimingController_SecondTriggerWhileVisibleSkipsFadeIn();
    Test_OsdTimingController_HidesAfterDuration();
    Test_OsdTimingController_SecondTriggerResetsHideDeadline();
    Test_OsdTimingController_SmtcUpdateOnlyAppliesWithinCorrectionWindow();
    Test_OsdTimingController_HoverPausesCountdown();
    Test_OsdTimingController_MouseLeaveResumesFromFullDuration();
    Test_OsdTimingController_RetriggerAfterHideReplaysFadeIn();
    Test_OsdTimingController_SetDurationSecondsDoesNotResetVisibility();
    Test_OsdTimingController_SetDurationSecondsAffectsNextDeadline();
    Test_ShouldShowPanel_TrueWhenSnapshotPresent();
    Test_ShouldShowPanel_FalseWhenNoSession();

    wprintf(L"\n%d/%d tests passed\n", g_testsRun - g_testsFailed, g_testsRun);
    return g_testsFailed == 0 ? 0 : 1;
}
