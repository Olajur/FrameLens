// FrameLens — visual styles.
// SPDX-License-Identifier: MIT
#include "Theme.h"
#include <cmath>

namespace fl::ui
{
    namespace
    {
        //                     bg        surface   surfAlt   border    text      textDim   textFaint accent    onAccent
        //                     gpu       cpu       ram       warm      hot       label     labelBar  panel     panelText panelMuted glassTop glassBottom
        constexpr Palette kClassicDark{ 0x161616, 0x202020, 0x2B2B2B, 0x3A3A3A, 0xF2F2F2, 0xBDBDBD, 0x8F8F8F, 0x4CD94C, 0x0B0B0B,
                                        0x4CD94C, 0x8DB8FF, 0xD9A6FF, 0xFF8C3A, 0xFF4545, 0xE8DE8E, 0xD6CC82, 0x000000, 0xFFFFFF, 0x8A8A8A, 0x202020, 0x202020 };
        constexpr Palette kClassicLight{ 0xF2F2F2, 0xFFFFFF, 0xEAEAEA, 0xCFCFCF, 0x141414, 0x474747, 0x666666, 0x177A26, 0xFFFFFF,
                                         0x177A26, 0x1F56B5, 0x7E35B8, 0xB04F00, 0xC62828, 0x6B5A00, 0xA8962E, 0xFFFFFF, 0x111111, 0x5E5E5E, 0xFFFFFF, 0xFFFFFF };

        constexpr Palette kSciFiDark{ 0x08131F, 0x0F2236, 0x163452, 0x2E5F8C, 0xEAF6FF, 0xA9C9E6, 0x86A9C8, 0x38C8FF, 0x03141F,
                                      0x5CF2C2, 0x6CB8FF, 0xC69CFF, 0xFFB347, 0xFF5C78, 0x9FE6FF, 0x38C8FF, 0x0A1D31, 0xF2FAFF, 0x8EB3D3, 0x1D4D78, 0x0A1A2C };
        constexpr Palette kSciFiLight{ 0xDDEBF7, 0xF5FAFF, 0xE2EFFA, 0x8DB6DA, 0x0A1F33, 0x2C4E6E, 0x4A6D8C, 0x0A6EB8, 0xFFFFFF,
                                       0x0B7D59, 0x1856AA, 0x6F37B5, 0xA65200, 0xBE1A3A, 0x0A5A86, 0x2A92CE, 0xEFF7FF, 0x0A1F33, 0x4C6B88, 0xFFFFFF, 0xC9E1F5 };

        constexpr Palette kMatrixDark{ 0x000000, 0x020A04, 0x07230E, 0x0E6A26, 0x8CFF9C, 0x3BDB63, 0x24A847, 0x00FF41, 0x000000,
                                       0x00FF41, 0x8CFF9C, 0x3BE36B, 0xE6FF5C, 0xFF5252, 0x5CFF7A, 0x16A83B, 0x000000, 0x8CFF9C, 0x24A847, 0x000000, 0x000000 };
        constexpr Palette kMatrixLight{ 0xE4F1E5, 0xF1F9F1, 0xD5EAD7, 0x5E9C6B, 0x04360F, 0x0C5320, 0x26683A, 0x0A7428, 0xFFFFFF,
                                        0x0A7428, 0x11633A, 0x2C641D, 0x7A5C00, 0xB0241B, 0x0C5A22, 0x3A9452, 0xEAF6EA, 0x04360F, 0x2E6B3E, 0xF1F9F1, 0xF1F9F1 };

        constexpr Palette kMinimalDark{ 0x0E0F12, 0x16181D, 0x20232A, 0x2B2F38, 0xF5F6F8, 0xA9AFBA, 0x858C99, 0x8AB4FF, 0x0B1020,
                                        0x7EE0B5, 0x8AB4FF, 0xC9A7FF, 0xFFB86B, 0xFF6B6B, 0xD3D8E0, 0x8AB4FF, 0x101115, 0xF5F6F8, 0x8C93A0, 0x16181D, 0x16181D };
        constexpr Palette kMinimalLight{ 0xF5F6F8, 0xFFFFFF, 0xEDEFF3, 0xDDE1E8, 0x111318, 0x474D5A, 0x646B79, 0x2B62F0, 0xFFFFFF,
                                         0x0D7E57, 0x2B62F0, 0x7246D6, 0xA85A00, 0xC92A2A, 0x2E323B, 0x2B62F0, 0xFFFFFF, 0x111318, 0x5E6573, 0xFFFFFF, 0xFFFFFF };

        Theme Make(Style s, bool dark)
        {
            Theme t;
            t.style = s;
            t.dark = dark;
            switch (s) {
            case Style::Classic:
                t.c = dark ? kClassicDark : kClassicLight;
                t.uiFonts[0] = L"Segoe UI";
                t.displayFonts[0] = L"Bahnschrift"; t.displayFonts[1] = L"Segoe UI";
                t.condensed = true;
                t.radius = 6.f;
                t.controlRadius = 4.f;
                break;
            case Style::SciFi2001:
                t.c = dark ? kSciFiDark : kSciFiLight;
                t.uiFonts[0] = L"Segoe UI";   // Vista's own typeface
                t.displayFonts[0] = L"Bahnschrift"; t.displayFonts[1] = L"Segoe UI";
                t.radius = 10.f;
                t.controlRadius = 6.f;
                t.panelBorder = true;
                t.glass = true;
                t.glow = true;
                break;
            case Style::Matrix:
                t.c = dark ? kMatrixDark : kMatrixLight;
                t.uiFonts[0] = L"Cascadia Mono"; t.uiFonts[1] = L"Consolas"; t.uiFonts[2] = L"Lucida Console";
                t.displayFonts[0] = L"Cascadia Mono"; t.displayFonts[1] = L"Consolas"; t.displayFonts[2] = L"Lucida Console";
                t.monospace = true;
                t.uppercase = true;
                t.radius = 0.f;
                t.controlRadius = 0.f;
                t.panelBorder = true;
                t.glow = dark;
                t.scanlines = true;
                t.rain = true;
                break;
            case Style::Minimal2026:
                t.c = dark ? kMinimalDark : kMinimalLight;
                t.uiFonts[0] = L"Segoe UI Variable Text"; t.uiFonts[1] = L"Segoe UI";
                t.displayFonts[0] = L"Segoe UI Variable Display"; t.displayFonts[1] = L"Segoe UI";
                t.radius = 14.f;
                t.controlRadius = 8.f;
                t.panelBorder = true;
                t.switches = true;
                break;
            }
            return t;
        }

        const Theme kThemes[kStyleCount][2] = {
            { Make(Style::Classic, false), Make(Style::Classic, true) },
            { Make(Style::SciFi2001, false), Make(Style::SciFi2001, true) },
            { Make(Style::Matrix, false), Make(Style::Matrix, true) },
            { Make(Style::Minimal2026, false), Make(Style::Minimal2026, true) },
        };

        double Linear(double c)
        {
            c /= 255.0;
            return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }

        double Luminance(uint32_t rgb)
        {
            return 0.2126 * Linear((rgb >> 16) & 0xFF) + 0.7152 * Linear((rgb >> 8) & 0xFF) + 0.0722 * Linear(rgb & 0xFF);
        }
    }

    const Theme& GetTheme(Style style, bool dark)
    {
        const int s = static_cast<int>(style);
        return kThemes[(s >= 0 && s < kStyleCount) ? s : 0][dark ? 1 : 0];
    }

    Theme HighContrast(const Theme& base)
    {
        Theme t = base;
        Palette& c = t.c;
        if (base.dark) {
            c.panel = 0x000000; c.panelText = 0xFFFFFF; c.panelMuted = 0xC8C8C8;
            c.gpu = 0x3CFF3C; c.cpu = 0x5CC8FF; c.ram = 0xFF7CFF;
            c.warm = 0xFFB000; c.hot = 0xFF6B6B;
            c.label = 0xFFE45C; c.labelBar = 0xFFE45C;
            c.accent = 0xFFFFFF;
        }
        else {
            c.panel = 0xFFFFFF; c.panelText = 0x000000; c.panelMuted = 0x404040;
            c.gpu = 0x005A00; c.cpu = 0x0040C0; c.ram = 0x8000A0;
            c.warm = 0x7A3D00; c.hot = 0xB00000;
            c.label = 0x5A4A00; c.labelBar = 0x5A4A00;
            c.accent = 0x000000;
        }
        c.glassTop = c.glassBottom = c.panel;
        t.glass = t.glow = t.scanlines = t.rain = false;
        t.panelBorder = true;
        t.outline = true;
        t.lineScale = 1.7f;
        return t;
    }

    const wchar_t* StyleName(Style style)
    {
        switch (style) {
        case Style::SciFi2001: return L"Sci-Fi 2001";
        case Style::Matrix: return L"Matrix";
        case Style::Minimal2026: return L"Minimal 2026";
        default: return L"Classic";
        }
    }

    const wchar_t* ColorModeName(ColorMode mode)
    {
        switch (mode) {
        case ColorMode::Dark: return L"Dark";
        case ColorMode::Light: return L"Light";
        default: return L"Follow Windows";
        }
    }

    double ContrastRatio(uint32_t a, uint32_t b)
    {
        double la = Luminance(a), lb = Luminance(b);
        if (la < lb) { const double t = la; la = lb; lb = t; }
        return (la + 0.05) / (lb + 0.05);
    }
}
