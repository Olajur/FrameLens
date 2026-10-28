// FrameLens — Direct2D drawing helpers shared by the overlay and the app windows: themed panels
// (rounded, glass gradient, gloss, glow, outline), Matrix scanlines and glyph rain, and a cache of
// DirectWrite text formats with font fallback.
// SPDX-License-Identifier: MIT
#pragma once
#include "Theme.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <tuple>

namespace fl::ui
{
    inline D2D1_COLOR_F Rgb(uint32_t rgb, float alpha = 1.f) { return D2D1::ColorF(rgb, alpha); }

    // Text formats keyed by family / size / weight / width / alignment, created on first use.
    class Fonts
    {
    public:
        void Init(IDWriteFactory* dwrite) { dwrite_ = dwrite; formats_.clear(); }
        void Clear() { formats_.clear(); }
        IDWriteFactory* Factory() const noexcept { return dwrite_; }

        // The first installed family of up to three candidates (falls back to "Segoe UI").
        std::wstring Resolve(const wchar_t* const candidates[3]) const;

        IDWriteTextFormat* Get(const std::wstring& family, float size, DWRITE_FONT_WEIGHT weight, bool condensed,
                               DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                               DWRITE_PARAGRAPH_ALIGNMENT para = DWRITE_PARAGRAPH_ALIGNMENT_CENTER, bool wrap = false);
        // Width of `s` in DIPs.
        float Width(const std::wstring& s, IDWriteTextFormat* format) const;

    private:
        using Key = std::tuple<std::wstring, int, int, bool, int, int, bool>;
        IDWriteFactory* dwrite_ = nullptr;
        std::map<Key, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    };

    // Basic shapes; radius 0 = square corners.
    void FillShape(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& r, float radius, uint32_t rgb, float alpha = 1.f);
    void StrokeShape(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& r, float radius, uint32_t rgb,
                     float alpha, float width);

    // Vertical gradient fill (Sci-Fi glass), the white gloss on its upper half, and a soft outer glow.
    void FillGradient(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, uint32_t top, uint32_t bottom, float alpha);
    void Gloss(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, float alpha);
    void Glow(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& r, float radius, uint32_t rgb, float alpha);

    // Matrix effects, clipped to `r` by the caller: horizontal scanlines, and columns of falling
    // glyphs whose positions depend on `timeSec` (a still frame for a given time).
    void Scanlines(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& r, uint32_t rgb, float alpha);
    void GlyphRain(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, IDWriteTextFormat* glyphFormat, const D2D1_RECT_F& r,
                   uint32_t rgb, float alpha, double timeSec);

    // A themed panel / card background: fill (or glass gradient + gloss), outline and glow as the
    // theme asks; Matrix scanlines and rain on top of the fill. `alpha` scales the fill only.
    void PanelBackground(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, Fonts& fonts, const Theme& t,
                         const D2D1_RECT_F& r, float radius, uint32_t fill, float alpha, double timeSec, bool effects);
}
