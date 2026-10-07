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
