// FrameLens — theme contrast tests: every style, dark and light, must stay readable
// (WCAG 2.x: 4.5:1 for text, 3:1 for large text, graphics and UI parts).
// SPDX-License-Identifier: MIT
#include "../src/ui/Theme.h"
#include <cstdio>
#include <initializer_list>

using namespace fl::ui;

static int gFailures = 0;

static void Need(const Theme& t, const char* what, uint32_t fg, uint32_t bg, double min)
{
    const double r = ContrastRatio(fg, bg);
    if (r + 1e-9 < min) {
        std::printf("FAIL %ls %s: %s %06X on %06X = %.2f:1, need %.1f:1\n", StyleName(t.style), t.dark ? "dark" : "light",
                    what, fg, bg, r, min);
        ++gFailures;
    }
}

static void TestContrast()
{
    for (int s = 0; s < kStyleCount; ++s)
        for (bool dark : { false, true }) {
            const Theme& t = GetTheme(static_cast<Style>(s), dark);
            const Palette& c = t.c;
            for (uint32_t bg : { c.bg, c.surface }) {
                Need(t, "text", c.text, bg, 7.0);
                Need(t, "textDim", c.textDim, bg, 4.5);
                Need(t, "textFaint", c.textFaint, bg, 3.0);
                Need(t, "accent", c.accent, bg, 3.0);
            }
            Need(t, "text on input", c.text, c.surfaceAlt, 4.5);
            Need(t, "onAccent", c.onAccent, c.accent, 4.5);
            // Overlay, as if the panel were opaque (the user's opacity only lowers it further).
            Need(t, "panelText", c.panelText, c.panel, 7.0);
            Need(t, "panelMuted", c.panelMuted, c.panel, 3.0);
            for (uint32_t col : { c.gpu, c.cpu, c.ram, c.warm, c.hot, c.label })
                Need(t, "overlay colour", col, c.panel, 3.0);
        }
}

static void TestHighContrast()
{
    // Every overlay colour of the high-contrast variant reaches WCAG AAA (7:1) on its panel.
    for (int s = 0; s < kStyleCount; ++s)
        for (bool dark : { false, true }) {
            const Theme t = HighContrast(GetTheme(static_cast<Style>(s), dark));
            const Palette& c = t.c;
            Need(t, "HC panelText", c.panelText, c.panel, 15.0);
            Need(t, "HC panelMuted", c.panelMuted, c.panel, 7.0);
            for (uint32_t col : { c.gpu, c.cpu, c.ram, c.warm, c.hot, c.label, c.labelBar, c.accent })
                Need(t, "HC overlay colour", col, c.panel, 7.0);
            if (!t.outline || t.rain || t.glass || t.scanlines) { std::printf("FAIL HC decoration flags\n"); ++gFailures; }
        }
}

static void TestContrastMath()
{
    const double bw = ContrastRatio(0xFFFFFF, 0x000000);
    if (bw < 20.99 || bw > 21.01) { std::printf("FAIL white/black = %.3f\n", bw); ++gFailures; }
    if (ContrastRatio(0x777777, 0x777777) != 1.0) { std::printf("FAIL equal colours\n"); ++gFailures; }
}

int main()
{
    TestContrastMath();
    TestContrast();
    TestHighContrast();
    if (gFailures == 0) std::printf("All Theme tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
