// FrameLens — visual styles (themes) for the overlay and the app windows (platform independent,
// unit-tested for contrast).
//
// Four styles, each with a dark and a light palette:
//   Classic      the original FrameLens look (DIN-style condensed type, black panel)
//   Sci-Fi 2001  Vista-era "future": glossy glass gradients, cyan glow, rounded panels
//   Matrix       green-on-black terminal: monospace, scanlines, falling glyphs
//   Minimal 2026 flat and quiet: large radii, hairline borders, one soft accent
// A theme only describes colours, fonts and decorations; the drawing code reads it.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace fl::ui
{
    enum class Style : int { Classic, SciFi2001, Matrix, Minimal2026 };
    constexpr int kStyleCount = 4;
    enum class ColorMode : int { System, Dark, Light };   // System = follow Windows' app mode

    struct Palette
    {
        // App windows
        uint32_t bg;            // window background
        uint32_t surface;       // cards
        uint32_t surfaceAlt;    // inputs, hovered controls
        uint32_t border;
        uint32_t text, textDim, textFaint;
        uint32_t accent;        // focus, primary buttons, selection
        uint32_t onAccent;      // text on an accent fill
        // Shared (overlay + app)
        uint32_t gpu, cpu, ram; // section colours
        uint32_t warm, hot;     // threshold colours
        uint32_t label;         // frame-time label / secondary values ("khaki" in Classic)
        uint32_t labelBar;      // secondary stat bars
        // Overlay
        uint32_t panel;         // overlay panel fill (opacity comes from the user's setting)
        uint32_t panelText;
        uint32_t panelMuted;    // n/a, --, captions
        // Glass gradient (Sci-Fi 2001): top and bottom of panels / header
        uint32_t glassTop, glassBottom;
    };

    struct Theme
    {
        Style style = Style::Classic;
        bool dark = true;
        Palette c{};
        // Font candidates, first installed one wins (Bahnschrift: Win10 1709+, Segoe UI Variable: Win11).
        const wchar_t* uiFonts[3]{};        // body text in the app windows
        const wchar_t* displayFonts[3]{};   // headings, overlay text and numbers
        bool condensed = false;             // use the semi-condensed width of the display font
        bool monospace = false;
        bool uppercase = false;             // labels and buttons in capitals
        float radius = 0.f;                 // corner radius of panels / cards (DIPs)
        float controlRadius = 0.f;          // buttons, inputs
        float borderWidth = 1.f;
        bool panelBorder = false;           // outline the overlay panel
        bool glass = false;                 // glossy vertical gradient + highlight
        bool glow = false;                  // soft glow around accents and outlines
        bool scanlines = false;
        bool rain = false;                  // falling glyphs behind the content
        bool switches = false;              // toggles drawn as switches (else checkboxes)
        bool outline = false;               // high contrast: a solid outline around overlay text
        float lineScale = 1.f;              // overlay graph line width multiplier
    };

    const Theme& GetTheme(Style style, bool dark);

    // Overlay variant for maximum legibility: solid black (dark) or white (light) panel, pure white /
    // black values with an outline, strongly saturated section colours (every overlay colour at
    // least 7:1 against the panel, WCAG AAA), thicker graphs, no glass / glow / scanlines / rain.
    // Fonts and shapes stay those of `base`.
    Theme HighContrast(const Theme& base);
    const wchar_t* StyleName(Style style);          // "Classic", "Sci-Fi 2001", "Matrix", "Minimal 2026"
    const wchar_t* ColorModeName(ColorMode mode);   // "Follow Windows", "Dark", "Light"

    // WCAG 2.x contrast ratio between two sRGB colours (1..21).
    double ContrastRatio(uint32_t a, uint32_t b);
}
