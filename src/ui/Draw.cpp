// FrameLens — Direct2D drawing helpers.
// SPDX-License-Identifier: MIT
#include "Draw.h"
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace fl::ui
{
    std::wstring Fonts::Resolve(const wchar_t* const candidates[3]) const
    {
        ComPtr<IDWriteFontCollection> fonts;
        if (dwrite_ && SUCCEEDED(dwrite_->GetSystemFontCollection(&fonts))) {
            for (int i = 0; i < 3 && candidates[i]; ++i) {
                UINT32 index = 0;
                BOOL exists = FALSE;
                if (SUCCEEDED(fonts->FindFamilyName(candidates[i], &index, &exists)) && exists) return candidates[i];
            }
        }
        return L"Segoe UI";
    }

    IDWriteTextFormat* Fonts::Get(const std::wstring& family, float size, DWRITE_FONT_WEIGHT weight, bool condensed,
                                  DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT para, bool wrap)
    {
        if (!dwrite_) return nullptr;
        auto& f = formats_[Key{ family, static_cast<int>(size * 4.f), static_cast<int>(weight), condensed,
                                static_cast<int>(align), static_cast<int>(para), wrap }];
        if (!f) {
            dwrite_->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                      condensed ? DWRITE_FONT_STRETCH_SEMI_CONDENSED : DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
            if (!f) return nullptr;
            f->SetTextAlignment(align);
            f->SetParagraphAlignment(para);
            f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
            if (!wrap) {   // single lines end in "…" instead of being cut mid-letter
                const DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                ComPtr<IDWriteInlineObject> ellipsis;
                if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(f.Get(), &ellipsis))) f->SetTrimming(&trim, ellipsis.Get());
            }
        }
        return f.Get();
    }

    float Fonts::Width(const std::wstring& s, IDWriteTextFormat* format) const
    {
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS m{};
        if (!dwrite_ || !format || s.empty() ||
            FAILED(dwrite_->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), format, 8192.f, 256.f, &layout)) ||
            FAILED(layout->GetMetrics(&m)))
            return 0.f;
        return m.widthIncludingTrailingWhitespace;
    }

    void FillShape(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& r, float radius, uint32_t rgb, float alpha)
    {
        brush->SetColor(Rgb(rgb, alpha));
        if (radius > 0.f) rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush);
        else rt->FillRectangle(r, brush);
    }

    void StrokeShape(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& r, float radius, uint32_t rgb,
                     float alpha, float width)
    {
        // Inset by half the stroke so the line stays inside the shape.
        const float h = width / 2.f;
        const D2D1_RECT_F in = D2D1::RectF(r.left + h, r.top + h, r.right - h, r.bottom - h);
        brush->SetColor(Rgb(rgb, alpha));
        if (radius > 0.f) rt->DrawRoundedRectangle(D2D1::RoundedRect(in, std::max(0.f, radius - h), std::max(0.f, radius - h)), brush, width);
        else rt->DrawRectangle(in, brush, width);
    }

    void FillGradient(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, uint32_t top, uint32_t bottom, float alpha)
    {
        const D2D1_GRADIENT_STOP stops[] = { { 0.f, Rgb(top, alpha) }, { 1.f, Rgb(bottom, alpha) } };
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1LinearGradientBrush> g;
        if (FAILED(rt->CreateGradientStopCollection(stops, 2, &coll)) ||
            FAILED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.left, r.bottom)),
                                                 coll.Get(), &g)))
            return;
        if (radius > 0.f) rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), g.Get());
        else rt->FillRectangle(r, g.Get());
    }

    void Gloss(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, float alpha)
    {
        // The classic Aero sheen: bright at the top edge, fading out by the middle.
        const D2D1_RECT_F top = D2D1::RectF(r.left + 1.f, r.top + 1.f, r.right - 1.f, r.top + (r.bottom - r.top) * 0.5f);
        const D2D1_GRADIENT_STOP stops[] = { { 0.f, Rgb(0xFFFFFF, 0.28f * alpha) }, { 1.f, Rgb(0xFFFFFF, 0.f) } };
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1LinearGradientBrush> g;
        if (FAILED(rt->CreateGradientStopCollection(stops, 2, &coll)) ||
            FAILED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(top.left, top.top), D2D1::Point2F(top.left, top.bottom)),
                                                 coll.Get(), &g)))
            return;
        if (radius > 1.f) rt->FillRoundedRectangle(D2D1::RoundedRect(top, radius - 1.f, radius - 1.f), g.Get());
        else rt->FillRectangle(top, g.Get());
    }

    void Glow(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& r, float radius, uint32_t rgb, float alpha)
    {
        // Concentric outlines with falling alpha: a cheap soft glow (no blur effect needed).
        for (int i = 3; i >= 1; --i) {
            const float d = static_cast<float>(i) * 1.5f;
            brush->SetColor(Rgb(rgb, alpha * 0.18f / static_cast<float>(i)));
            const D2D1_RECT_F o = D2D1::RectF(r.left - d, r.top - d, r.right + d, r.bottom + d);
            if (radius > 0.f) rt->DrawRoundedRectangle(D2D1::RoundedRect(o, radius + d, radius + d), brush, 1.5f);
            else rt->DrawRectangle(o, brush, 1.5f);
        }
    }

    void Scanlines(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& r, uint32_t rgb, float alpha)
    {
        brush->SetColor(Rgb(rgb, alpha));
        for (float y = r.top + 1.f; y < r.bottom; y += 3.f)
            rt->FillRectangle(D2D1::RectF(r.left, y, r.right, y + 1.f), brush);
    }

    namespace
    {
        uint32_t Hash(uint32_t x)
        {
            x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
            return x;
        }
    }

    void GlyphRain(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, IDWriteTextFormat* glyphFormat, const D2D1_RECT_F& r,
                   uint32_t rgb, float alpha, double timeSec)
    {
        // Half-width katakana and digits, as in the film's code rain.
        static const wchar_t kGlyphs[] = L"ｦｧｨｩｪｫｬｭｮｯｱｲｳｴｵ"
                                         L"ｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄ"
                                         L"0123456789Z:=*+";
        constexpr int kGlyphCount = static_cast<int>(sizeof kGlyphs / sizeof kGlyphs[0]) - 1;
        constexpr float kCell = 14.f;
        if (!glyphFormat) return;
        const int cols = static_cast<int>((r.right - r.left) / kCell) + 1;
        const int rows = static_cast<int>((r.bottom - r.top) / kCell) + 1;
        for (int c = 0; c < cols; ++c) {
            const uint32_t h = Hash(static_cast<uint32_t>(c) * 2654435761u + 17u);
            if (h % 3 == 0) continue;                                    // not every column rains
            const double speed = 3.0 + (h >> 8) % 7;                     // cells per second
            const int trail = 5 + static_cast<int>((h >> 16) % 8);
            const int span = rows + trail + static_cast<int>((h >> 4) % 12);
            const int head = static_cast<int>(std::fmod(timeSec * speed + (h % 97), static_cast<double>(span)));
            for (int k = 0; k < trail; ++k) {
                const int row = head - k;
                if (row < 0 || row >= rows) continue;
                const uint32_t g = Hash(static_cast<uint32_t>(c * 131 + row * 7919) ^ static_cast<uint32_t>(timeSec * 2.0 + (h & 7)));
                const wchar_t ch = kGlyphs[g % kGlyphCount];
                const float a = alpha * (k == 0 ? 1.f : 0.75f * (1.f - static_cast<float>(k) / static_cast<float>(trail)));
                brush->SetColor(Rgb(k == 0 ? 0xD8FFD8 : rgb, a));
                const float x = r.left + static_cast<float>(c) * kCell, y = r.top + static_cast<float>(row) * kCell;
                rt->DrawText(&ch, 1, glyphFormat, D2D1::RectF(x, y, x + kCell, y + kCell), brush);
            }
        }
    }

    void PanelBackground(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, Fonts& fonts, const Theme& t,
                         const D2D1_RECT_F& r, float radius, uint32_t fill, float alpha, double timeSec, bool effects)
    {
        if (t.glass) {
            FillGradient(rt, r, radius, t.c.glassTop, t.c.glassBottom, alpha);
            Gloss(rt, r, radius, alpha);
        }
        else {
            FillShape(rt, brush, r, radius, fill, alpha);
        }
        if (effects && (t.scanlines || t.rain)) {
            rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
            if (t.rain) {
                const std::wstring mono = fonts.Resolve(t.displayFonts);
                GlyphRain(rt, brush, fonts.Get(mono, 11.f, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_CENTER),
                          r, t.c.accent, t.dark ? 0.24f : 0.16f, timeSec);
            }
            if (t.scanlines) Scanlines(rt, brush, r, t.dark ? 0x000000 : 0xFFFFFF, t.dark ? 0.35f : 0.25f);
            rt->PopAxisAlignedClip();
        }
        if (t.glow) Glow(rt, brush, r, radius, t.c.accent, 1.f);
        if (t.panelBorder) {
            const uint32_t line = t.style == Style::Minimal2026 ? (t.dark ? 0xFFFFFF : 0x000000) : t.c.accent;
            const float lineAlpha = t.style == Style::Minimal2026 ? 0.10f : (t.glass ? 0.55f : 0.65f);
            StrokeShape(rt, brush, r, radius, line, lineAlpha, t.borderWidth);
        }
    }
}
