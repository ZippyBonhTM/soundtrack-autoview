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

    wprintf(L"\n%d/%d tests passed\n", g_testsRun - g_testsFailed, g_testsRun);
    return g_testsFailed == 0 ? 0 : 1;
}
