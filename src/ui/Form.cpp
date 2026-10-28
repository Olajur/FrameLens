// FrameLens — custom-drawn UI toolkit.
// SPDX-License-Identifier: MIT
#include "Form.h"
#include <commdlg.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace fl::ui
{
    namespace
    {
        constexpr UINT WM_CLOSELIST = WM_APP + 0x50;
        constexpr UINT_PTR kAnimTimer = 0xF1;
        constexpr float kRowH = 30.f;   // dropdown list rows

        ID2D1Factory* D2D()
        {
            static ComPtr<ID2D1Factory> f = [] {
                ComPtr<ID2D1Factory> x;
                D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, x.GetAddressOf());
                return x;
            }();
            return f.Get();
        }

        IDWriteFactory* DWrite()
        {
            static ComPtr<IDWriteFactory> f = [] {
                ComPtr<IDWriteFactory> x;
                DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(x.GetAddressOf()));
                return x;
            }();
            return f.Get();
        }

        D2D1_RECT_F R(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
        bool Inside(const D2D1_RECT_F& r, D2D1_POINT_2F p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

        // Blend two colours: t = 0 -> a, 1 -> b.
        uint32_t Mix(uint32_t a, uint32_t b, float t)
        {
            auto ch = [&](int s) {
                const float x = static_cast<float>((a >> s) & 0xFF), y = static_cast<float>((b >> s) & 0xFF);
                return static_cast<uint32_t>(std::lround(x + (y - x) * t)) << s;
            };
            return ch(16) | ch(8) | ch(0);
        }

        uint32_t Hover(const Theme& t, uint32_t c, bool hot, bool pressed)
        {
            if (pressed) return Mix(c, 0x000000, 0.16f);
            if (hot) return Mix(c, t.dark ? 0xFFFFFF : 0x000000, 0.08f);
            return c;
        }

        COLORREF ToColorRef(uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
    }

    // ---------------------------------------------------------------- Painter
    std::wstring Painter::Caps(const std::wstring& s) const
    {
        if (!th->uppercase) return s;
        std::wstring u = s;
        for (auto& ch : u) ch = static_cast<wchar_t>(towupper(ch));
        return u;
    }

    void Painter::Text(const std::wstring& s, const D2D1_RECT_F& r, float size, DWRITE_FONT_WEIGHT weight, uint32_t rgb,
                       DWRITE_TEXT_ALIGNMENT align, bool display, bool wrap, float alpha, DWRITE_PARAGRAPH_ALIGNMENT para) const
    {
        if (s.empty()) return;
        IDWriteTextFormat* f = fonts->Get(display ? displayFont : uiFont, th->monospace ? size * 0.92f : size, weight,
                                          display && th->condensed, align, para, wrap);
        if (!f) return;
        const std::wstring t = Caps(s);
        brush->SetColor(Rgb(rgb, alpha));
        rt->DrawText(t.data(), static_cast<UINT32>(t.size()), f, r, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void Painter::Icon(const wchar_t* glyph, const D2D1_RECT_F& r, float size, uint32_t rgb, float alpha) const
    {
        IDWriteTextFormat* f = fonts->Get(iconFont, size, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (!f || !glyph) return;
        brush->SetColor(Rgb(rgb, alpha));
        rt->DrawText(glyph, static_cast<UINT32>(wcslen(glyph)), f, r, brush);
    }

    float Painter::TextWidth(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight, bool display) const
    {
        return fonts->Width(Caps(s), fonts->Get(display ? displayFont : uiFont, th->monospace ? size * 0.92f : size, weight,
                                                 display && th->condensed));
    }

    void Painter::FocusRing(const D2D1_RECT_F& r, float radius) const
    {
        StrokeShape(rt, brush, R(r.left - 3, r.top - 3, r.right + 3, r.bottom + 3), radius > 0 ? radius + 3 : 0.f, th->c.accent, 0.9f, 2.f);
    }

    // ---------------------------------------------------------------- simple controls
    void Label::Draw(const Painter& p, bool, bool, bool)
    {
        const Palette& c = p.th->c;
        uint32_t rgb = c.text;
        float sz = size;
        DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL;
        bool display = false;
        switch (role) {
        case Role::Dim: rgb = c.textDim; break;
        case Role::Faint: rgb = c.textFaint; break;
        case Role::Accent: rgb = c.accent; w = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case Role::Heading: w = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case Role::Title: w = DWRITE_FONT_WEIGHT_BOLD; display = true; break;
        default: break;
        }
        if (color) if (const uint32_t o = color()) rgb = o;
        p.Text(text ? text() : L"", rect, sz, w, rgb, align, display, wrap, 1.f,
               wrap ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void Card::Draw(const Painter& p, bool, bool, bool)
    {
        const Theme& t = *p.th;
        PanelBackground(p.rt, p.brush, *p.fonts, t, rect, t.radius, t.c.surface, 1.f, p.timeSec, false);
        if (t.scanlines) {
            p.rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
            Scanlines(p.rt, p.brush, rect, t.dark ? 0x000000 : 0xFFFFFF, t.dark ? 0.25f : 0.2f);
            p.rt->PopAxisAlignedClip();
        }
        if (!t.panelBorder) StrokeShape(p.rt, p.brush, rect, t.radius, t.c.border, 1.f, 1.f);
        if (!title.empty()) {
            const bool accentTitle = t.style == Style::SciFi2001 || t.style == Style::Matrix || t.style == Style::Classic;
            std::wstring s = title;
            if (t.style != Style::Matrix) for (auto& ch : s) ch = static_cast<wchar_t>(towupper(ch));   // small caps-like section titles
            p.Text(s, R(rect.left + 14, rect.top + 8, rect.right - 14, rect.top + 28), 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                   accentTitle ? t.c.accent : t.c.textDim, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        }
    }

    void Header::Draw(const Painter& p, bool, bool, bool)
    {
        const Theme& t = *p.th;
        if (t.glass) {
            FillGradient(p.rt, rect, 0.f, t.c.glassTop, t.c.glassBottom, 1.f);
            Gloss(p.rt, rect, 0.f, 1.f);
        }
        else if (t.rain) {
            FillShape(p.rt, p.brush, rect, 0.f, t.c.bg);
            p.rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
            GlyphRain(p.rt, p.brush, p.fonts->Get(p.displayFont, 11.f, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_CENTER),
                      rect, t.c.accent, t.dark ? 0.35f : 0.22f, p.timeSec);
            Scanlines(p.rt, p.brush, rect, t.dark ? 0x000000 : 0xFFFFFF, 0.3f);
            p.rt->PopAxisAlignedClip();
        }
        // Rule under the header: the overlay's green line in Classic, a hairline elsewhere.
        if (t.style == Style::Classic) FillShape(p.rt, p.brush, R(rect.left, rect.bottom - 2, rect.right, rect.bottom), 0.f, t.c.accent);
        else FillShape(p.rt, p.brush, R(rect.left, rect.bottom - 1, rect.right, rect.bottom), 0.f,
                       t.glass || t.rain ? t.c.accent : t.c.border, t.glass ? 0.6f : 1.f);

        const uint32_t titleRgb = (t.style == Style::Minimal2026) ? t.c.text : t.c.accent;
        const float titleSize = t.monospace ? 24.f : 26.f;
        const float h = rect.bottom - rect.top;
        p.Text(title, R(rect.left + 20, rect.top + h * 0.12f, rect.right - 120, rect.top + h * 0.62f), titleSize,
               t.style == Style::Minimal2026 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_BOLD, titleRgb,
               DWRITE_TEXT_ALIGNMENT_LEADING, true);
        p.Text(subtitle, R(rect.left + 21, rect.top + h * 0.58f, rect.right - 120, rect.top + h * 0.9f), 12.f,
               DWRITE_FONT_WEIGHT_NORMAL, t.c.textDim);
    }

    // ---------------------------------------------------------------- Button
    void Button::Draw(const Painter& p, bool hot, bool pressed, bool focused)
    {
        const Theme& t = *p.th;
        const Palette& c = t.c;
        const bool on = Enabled();
        hot = hot && on;
        pressed = pressed && on;
        const float a = on ? 1.f : 0.45f;
        const float rad = kind == Kind::Icon ? std::min(t.controlRadius, (rect.bottom - rect.top) / 2.f) : t.controlRadius;

        uint32_t fill = c.surfaceAlt, ink = c.text, line = c.border;
        float fillA = a;
        bool outline = true;
        const Kind look = highlighted && highlighted() ? Kind::Primary : kind;
        switch (look) {
        case Kind::Primary: fill = c.accent; ink = c.onAccent; line = c.accent; break;
        case Kind::Quiet:
        case Kind::Icon: ink = kind == Kind::Icon ? c.textDim : c.text; fillA = (hot || pressed) ? a : 0.f; outline = false; break;
        default: break;
        }
        if (t.style == Style::Matrix && look != Kind::Primary) {   // terminal buttons: outlined, green ink
            fill = c.bg;
            ink = c.accent;
            line = c.accent;
            if (kind == Kind::Icon || kind == Kind::Quiet) outline = hot;
            fillA = a;
            if (hot) fill = Mix(c.bg, c.accent, 0.18f);
        }
        else fill = Hover(t, fill, hot, pressed);

        if (fillA > 0.f) {
            if (t.glass && kind != Kind::Quiet && kind != Kind::Icon) {
                FillGradient(p.rt, rect, rad, Mix(fill, 0xFFFFFF, 0.28f), fill, fillA);
                Gloss(p.rt, rect, rad, fillA);
            }
            else FillShape(p.rt, p.brush, rect, rad, fill, fillA);
        }
        if (outline) StrokeShape(p.rt, p.brush, rect, rad, line, a * (kind == Kind::Primary ? 1.f : 0.9f), 1.f);
        if (t.glow && kind == Kind::Primary && t.style == Style::SciFi2001) Glow(p.rt, p.brush, rect, rad, c.accent, a);

        const std::wstring s = text ? text() : L"";
        if (kind == Kind::Icon) p.Icon(s.c_str(), rect, 15.f, hot ? c.text : ink, a);
        else p.Text(s, rect, 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, ink, DWRITE_TEXT_ALIGNMENT_CENTER, false, false, a);
        if (focused) p.FocusRing(rect, rad);
    }

    void Button::OnClick(D2D1_POINT_2F)
    {
        if (Enabled() && onClick) onClick();
    }

    bool Button::OnKey(UINT vk)
    {
        if (vk != VK_SPACE && vk != VK_RETURN) return false;
        OnClick({});
        return true;
    }

    // ---------------------------------------------------------------- Toggle
    void Toggle::Draw(const Painter& p, bool hot, bool pressed, bool focused)
    {
        const Theme& t = *p.th;
        const Palette& c = t.c;
        const bool on = get && get();
        const float a = Enabled() ? 1.f : 0.45f;
        const float cy = (rect.top + rect.bottom) / 2.f;
        D2D1_RECT_F box;
        if (t.switches) {
            box = R(rect.left, cy - 10, rect.left + 36, cy + 10);
            FillShape(p.rt, p.brush, box, 10.f, on ? Hover(t, c.accent, hot, pressed) : Hover(t, c.surfaceAlt, hot, pressed), a);
            if (!on) StrokeShape(p.rt, p.brush, box, 10.f, c.border, a, 1.f);
            const float kx = on ? box.right - 10 : box.left + 10;
            p.brush->SetColor(Rgb(on ? c.onAccent : c.textDim, a));
            p.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, cy), 7.f, 7.f), p.brush);
        }
        else {
            box = R(rect.left + 1, cy - 9, rect.left + 19, cy + 9);
            const float rad = std::min(t.controlRadius, 4.f);
            if (on) {
                if (t.glass) {
                    FillGradient(p.rt, box, rad, Mix(c.accent, 0xFFFFFF, 0.35f), c.accent, a);
                    Gloss(p.rt, box, rad, a);
                }
                else FillShape(p.rt, p.brush, box, rad, t.style == Style::Matrix ? c.bg : Hover(t, c.accent, hot, pressed), a);
                if (t.style == Style::Matrix) {   // [■]
                    StrokeShape(p.rt, p.brush, box, 0.f, c.accent, a, 1.f);
                    FillShape(p.rt, p.brush, R(box.left + 4, box.top + 4, box.right - 4, box.bottom - 4), 0.f, c.accent, a);
                }
                else {
                    ComPtr<ID2D1PathGeometry> tick;
                    ComPtr<ID2D1GeometrySink> sink;
                    if (SUCCEEDED(D2D()->CreatePathGeometry(&tick)) && SUCCEEDED(tick->Open(&sink))) {
                        sink->BeginFigure(D2D1::Point2F(box.left + 4.f, cy), D2D1_FIGURE_BEGIN_HOLLOW);
                        sink->AddLine(D2D1::Point2F(box.left + 7.5f, cy + 3.5f));
                        sink->AddLine(D2D1::Point2F(box.left + 14.f, cy - 4.f));
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        if (SUCCEEDED(sink->Close())) {
                            p.brush->SetColor(Rgb(c.onAccent, a));
                            p.rt->DrawGeometry(tick.Get(), p.brush, 2.f);
                        }
                    }
                }
            }
            else {
                FillShape(p.rt, p.brush, box, rad, Hover(t, t.style == Style::Matrix ? c.bg : c.surfaceAlt, hot, pressed), a);
                StrokeShape(p.rt, p.brush, box, rad, t.style == Style::Matrix ? c.accent : c.border, a, 1.f);
            }
        }
        const float tx = box.right + 10.f;
        p.Text(text, R(tx, rect.top, rect.right, rect.bottom), 13.f, DWRITE_FONT_WEIGHT_NORMAL, c.text,
               DWRITE_TEXT_ALIGNMENT_LEADING, false, false, a);
        if (note) {
            const std::wstring n = note();
            if (!n.empty()) {
                const float w = p.TextWidth(text, 13.f, DWRITE_FONT_WEIGHT_NORMAL);
                p.Text(n, R(tx + w + 8.f, rect.top, rect.right, rect.bottom), 11.5f, DWRITE_FONT_WEIGHT_NORMAL, c.textFaint,
                       DWRITE_TEXT_ALIGNMENT_LEADING, false, false, a);
            }
        }
        if (focused) p.FocusRing(box, t.switches ? 10.f : std::min(t.controlRadius, 4.f));
    }

    void Toggle::OnClick(D2D1_POINT_2F)
    {
        if (Enabled() && get && set) set(!get());
    }

    bool Toggle::OnKey(UINT vk)
    {
        if (vk != VK_SPACE && vk != VK_RETURN) return false;
        OnClick({});
        return true;
    }

    // ---------------------------------------------------------------- Slider
    D2D1_RECT_F Slider::Track() const
    {
        const float cy = (rect.top + rect.bottom) / 2.f;
        return R(rect.left + labelWidth + 8.f, cy - 2.f, rect.right - 58.f, cy + 2.f);
    }

    void Slider::SetValue(int v)
    {
        v = std::clamp(v, min, max);
        if (step > 1) v = min + (v - min + step / 2) / step * step;
        if (get && set && v != get()) set(v);
    }

    void Slider::Draw(const Painter& p, bool hot, bool pressed, bool focused)
    {
        const Theme& t = *p.th;
        const Palette& c = t.c;
        const float a = Enabled() ? 1.f : 0.45f;
        const int v = get ? get() : min;
        p.Text(label, R(rect.left, rect.top, rect.left + labelWidth, rect.bottom), 13.f, DWRITE_FONT_WEIGHT_NORMAL, c.textDim,
               DWRITE_TEXT_ALIGNMENT_LEADING, false, false, a);
        const D2D1_RECT_F tr = Track();
        const float f = max > min ? static_cast<float>(v - min) / static_cast<float>(max - min) : 0.f;
        const float x = tr.left + (tr.right - tr.left) * f;
        const float cy = (tr.top + tr.bottom) / 2.f;
        FillShape(p.rt, p.brush, tr, 2.f, t.style == Style::Matrix ? Mix(c.bg, c.accent, 0.25f) : c.border, a);
        FillShape(p.rt, p.brush, R(tr.left, tr.top, x, tr.bottom), 2.f, c.accent, a);
        D2D1_RECT_F thumb;
        if (t.style == Style::Matrix) {
            thumb = R(x - 4, cy - 9, x + 4, cy + 9);
            FillShape(p.rt, p.brush, thumb, 0.f, hot || pressed ? 0xFFFFFF : c.accent, a);
        }
        else {
            thumb = R(x - 8, cy - 8, x + 8, cy + 8);
            if (t.glass) {
                FillGradient(p.rt, thumb, 8.f, 0xFFFFFF, c.accent, a);
                StrokeShape(p.rt, p.brush, thumb, 8.f, c.accent, a, 1.f);
            }
            else {
                const uint32_t knob = t.style == Style::Minimal2026 ? 0xFFFFFF : c.accent;
                FillShape(p.rt, p.brush, thumb, 8.f, Hover(t, knob, hot, pressed), a);
                if (t.style == Style::Minimal2026) StrokeShape(p.rt, p.brush, thumb, 8.f, c.border, a, 1.f);
            }
        }
        const std::wstring val = format ? format(v) : std::to_wstring(v);
        p.Text(val, R(tr.right + 8.f, rect.top, rect.right, rect.bottom), 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, c.text,
               DWRITE_TEXT_ALIGNMENT_TRAILING, false, false, a);
        if (focused) p.FocusRing(thumb, t.style == Style::Matrix ? 0.f : 8.f);
    }

    void Slider::OnDrag(D2D1_POINT_2F pt)
    {
        if (!Enabled()) return;
        const D2D1_RECT_F tr = Track();
        const float f = std::clamp((pt.x - tr.left) / std::max(1.f, tr.right - tr.left), 0.f, 1.f);
        SetValue(min + static_cast<int>(std::lround(f * static_cast<float>(max - min))));
    }

    bool Slider::OnKey(UINT vk)
    {
        if (!Enabled() || !get) return false;
        const int v = get(), big = std::max(step * 10, (max - min) / 10);
        switch (vk) {
        case VK_LEFT: case VK_DOWN: SetValue(v - step); return true;
        case VK_RIGHT: case VK_UP: SetValue(v + step); return true;
        case VK_PRIOR: SetValue(v + big); return true;
        case VK_NEXT: SetValue(v - big); return true;
        case VK_HOME: SetValue(min); return true;
        case VK_END: SetValue(max); return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- Dropdown
    D2D1_RECT_F Dropdown::Field() const
    {
        return R(label.empty() ? rect.left : rect.left + labelWidth, rect.top + 3, rect.right, rect.bottom - 3);
    }

    void Dropdown::Draw(const Painter& p, bool hot, bool pressed, bool focused)
    {
        const Theme& t = *p.th;
        const Palette& c = t.c;
        const float a = Enabled() ? 1.f : 0.45f;
        if (!label.empty())
            p.Text(label, R(rect.left, rect.top, rect.left + labelWidth, rect.bottom), 13.f, DWRITE_FONT_WEIGHT_NORMAL, c.textDim,
                   DWRITE_TEXT_ALIGNMENT_LEADING, false, false, a);
        const D2D1_RECT_F f = Field();
        FillShape(p.rt, p.brush, f, t.controlRadius, Hover(t, t.style == Style::Matrix ? c.bg : c.surfaceAlt, hot, pressed), a);
        StrokeShape(p.rt, p.brush, f, t.controlRadius, t.style == Style::Matrix || hot ? c.accent : c.border, a, 1.f);
        p.Text(display ? display() : L"", R(f.left + 10, f.top, f.right - 28, f.bottom), 13.f, DWRITE_FONT_WEIGHT_NORMAL, c.text,
               DWRITE_TEXT_ALIGNMENT_LEADING, false, false, a);
        p.Icon(L"", R(f.right - 26, f.top, f.right - 6, f.bottom), 10.f, t.style == Style::Matrix ? c.accent : c.textDim, a);
        if (focused) p.FocusRing(f, t.controlRadius);
    }

    void Dropdown::OnClick(D2D1_POINT_2F pt)
    {
        if (Enabled() && Inside(Field(), pt)) owner->OpenList(this);
    }

    bool Dropdown::OnKey(UINT vk)
    {
        if (!Enabled()) return false;
        if (vk == VK_SPACE || vk == VK_RETURN || vk == VK_F4) { owner->OpenList(this); return true; }
        if ((vk == VK_UP || vk == VK_DOWN) && items && selected && onSelect) {
            const int n = static_cast<int>(items().size());
            const int s = std::clamp(selected() + (vk == VK_DOWN ? 1 : -1), 0, std::max(0, n - 1));
            if (n > 0 && s != selected()) onSelect(s);
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- ColorPicker
    D2D1_RECT_F ColorPicker::Swatch() const
    {
        const float cy = (rect.top + rect.bottom) / 2.f;
        return R(rect.left + labelWidth, cy - 10, rect.left + labelWidth + 40, cy + 10);
    }

    D2D1_RECT_F ColorPicker::ResetBox() const
    {
        const float cy = (rect.top + rect.bottom) / 2.f;
        return R(rect.right - 26, cy - 13, rect.right, cy + 13);
    }

    void ColorPicker::Draw(const Painter& p, bool hot, bool, bool focused)
    {
        const Theme& t = *p.th;
        const Palette& c = t.c;
        const bool own = get && get() >= 0;
        p.Text(label, R(rect.left, rect.top, rect.left + labelWidth, rect.bottom), 13.f, DWRITE_FONT_WEIGHT_NORMAL, c.textDim);
        const D2D1_RECT_F sw = Swatch();
        const float rad = std::min(t.controlRadius, 5.f);
        FillShape(p.rt, p.brush, sw, rad, effective ? effective() : 0);
        StrokeShape(p.rt, p.brush, sw, rad, hot ? c.accent : c.border, 1.f, 1.f);
        wchar_t hex[16];
        swprintf(hex, 16, L"#%06X", static_cast<unsigned>(own ? get() : 0));
        const float tx = sw.right + 10.f;
        const std::wstring what = own ? hex : L"Style colour";
        p.Text(what, R(tx, rect.top, rect.right - 28, rect.bottom), 12.5f, DWRITE_FONT_WEIGHT_NORMAL, own ? c.text : c.textFaint);
        if (note && !note().empty()) {   // a warning sign after the code (the card's note explains it)
            const float x = tx + p.TextWidth(what, 12.5f, DWRITE_FONT_WEIGHT_NORMAL) + 6.f;
            p.Icon(L"", R(x, rect.top, x + 20, rect.bottom), 13.f, c.hot);
        }
        if (own) p.Icon(L"", ResetBox(), 12.f, c.textDim);   // undo arrow: back to the style's colour
        if (focused) p.FocusRing(sw, rad);
    }

    void ColorPicker::OnClick(D2D1_POINT_2F pt)
    {
        if (get && get() >= 0 && Inside(ResetBox(), pt)) { if (set) set(-1); return; }
        Pick();
    }

    bool ColorPicker::OnKey(UINT vk)
    {
        if (vk == VK_SPACE || vk == VK_RETURN) { Pick(); return true; }
        if ((vk == VK_DELETE || vk == VK_BACK) && set) { set(-1); return true; }
        return false;
    }

    // Live preview: the colour dialog keeps its Red / Green / Blue boxes in step with every click,
    // drag and typed value, so watching their changes shows the colour on the overlay while the
    // dialog is still open. (The dialog is modal: one picker at a time.)
    namespace { ColorPicker* g_picking = nullptr; }

    UINT_PTR CALLBACK ColorPicker::PickerHook(HWND dlg, UINT msg, WPARAM w, LPARAM)
    {
        constexpr int kRed = 706, kGreen = 707, kBlue = 708;   // the dialog's RGB edit boxes (colordlg.h)
        if (msg == WM_INITDIALOG) return TRUE;
        if (msg == WM_COMMAND && HIWORD(w) == EN_CHANGE && LOWORD(w) >= kRed && LOWORD(w) <= kBlue && g_picking && g_picking->preview) {
            BOOL okR = FALSE, okG = FALSE, okB = FALSE;
            const UINT r = GetDlgItemInt(dlg, kRed, &okR, FALSE), g = GetDlgItemInt(dlg, kGreen, &okG, FALSE),
                       b = GetDlgItemInt(dlg, kBlue, &okB, FALSE);
            if (okR && okG && okB && r <= 255 && g <= 255 && b <= 255)   // ignore half-typed values
                g_picking->preview(static_cast<int>((r << 16) | (g << 8) | b));
        }
        return 0;   // the dialog handles everything itself
    }

    void ColorPicker::Pick()
    {
        if (!set || !owner || !owner->Hwnd()) return;
        static COLORREF custom[16]{};   // the dialog's "custom colours", kept for the session
        const int before = get ? get() : -1;
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof cc;
        cc.hwndOwner = owner->Hwnd();
        cc.rgbResult = ToColorRef(effective ? effective() : 0);
        cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR | CC_ENABLEHOOK;
        cc.lpfnHook = PickerHook;
        g_picking = this;
        const BOOL ok = ChooseColorW(&cc);
        g_picking = nullptr;
        if (ok) set(static_cast<int>((GetRValue(cc.rgbResult) << 16) | (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult)));
        else if (preview) preview(before);   // Cancel: back to the colour from before the dialog
        owner->Invalidate();
    }

    // ---------------------------------------------------------------- HotkeyBox
    std::wstring HotkeyText(uint32_t packed)
    {
        const UINT vk = packed & 0xFFFF, mods = packed >> 16;
        if (!vk) return L"None";
        std::wstring s;
        if (mods & MOD_CONTROL) s += L"Ctrl + ";
        if (mods & MOD_ALT) s += L"Alt + ";
        if (mods & MOD_SHIFT) s += L"Shift + ";
        if (mods & MOD_WIN) s += L"Win + ";
        wchar_t name[64]{};
        LONG scan = static_cast<LONG>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
        switch (vk) {   // keys whose name needs the "extended" bit (else: the numeric keypad's)
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK: scan |= 1 << 24; break;
        default: break;
        }
        if (vk >= VK_F1 && vk <= VK_F24) swprintf(name, 64, L"F%u", vk - VK_F1 + 1);
        else if (!GetKeyNameTextW(scan, name, 64)) swprintf(name, 64, L"Key %u", vk);
        return s + name;
    }

    D2D1_RECT_F HotkeyBox::Field() const
    {
        return R(rect.left + labelWidth, rect.top + 3, rect.right, rect.bottom - 3);
    }

    void HotkeyBox::Draw(const Painter& p, bool hot, bool pressed, bool focused)
    {
        const Theme& t = *p.th;
        const Palette& c = t.c;
        p.Text(label, R(rect.left, rect.top, rect.left + labelWidth, rect.bottom), 13.f, DWRITE_FONT_WEIGHT_NORMAL, c.textDim);
        const D2D1_RECT_F f = Field();
        FillShape(p.rt, p.brush, f, t.controlRadius, Hover(t, t.style == Style::Matrix ? c.bg : c.surfaceAlt, hot, pressed));
        StrokeShape(p.rt, p.brush, f, t.controlRadius, recording_ || t.style == Style::Matrix || hot ? c.accent : c.border, 1.f,
                    recording_ ? 2.f : 1.f);
        const uint32_t key = get ? get() : 0;
        std::wstring text;
        uint32_t rgb = c.text;
        if (recording_) { text = needModifier_ ? L"Add Alt, Ctrl or Shift" : L"Press the keys…"; rgb = c.accent; }
        else { text = HotkeyText(key); if (!key) rgb = c.textFaint; }
        const std::wstring warn = !recording_ && note ? note() : std::wstring();
        p.Text(text, R(f.left + 10, f.top, f.right - 10, f.bottom), 13.f, DWRITE_FONT_WEIGHT_NORMAL, rgb);
        if (!warn.empty())
            p.Text(warn, R(f.left + 10, f.top, f.right - 10, f.bottom), 11.5f, DWRITE_FONT_WEIGHT_NORMAL, c.hot, DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (focused && !recording_) p.FocusRing(f, t.controlRadius);
    }

    void HotkeyBox::Start()
    {
        if (recording_) return;
        recording_ = true;
        needModifier_ = false;
        if (recording) recording(true);
    }

    void HotkeyBox::Stop()
    {
        if (!recording_) return;
        recording_ = false;
        if (recording) recording(false);
        if (owner) owner->Invalidate();
    }

    void HotkeyBox::OnClick(D2D1_POINT_2F)
    {
        if (recording_) Stop(); else Start();
    }

    bool HotkeyBox::OnKey(UINT vk)
    {
        if (!recording_) {
            if (vk == VK_SPACE || vk == VK_RETURN) { Start(); return true; }
            if ((vk == VK_DELETE || vk == VK_BACK) && set) { set(0); return true; }
            return false;
        }
        switch (vk) {
        case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN:
        case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
            return true;   // wait for the key that goes with them
        default: break;
        }
        UINT mods = 0;
        if (GetKeyState(VK_MENU) < 0) mods |= MOD_ALT;
        if (GetKeyState(VK_CONTROL) < 0) mods |= MOD_CONTROL;
        if (GetKeyState(VK_SHIFT) < 0) mods |= MOD_SHIFT;
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) mods |= MOD_WIN;
        if (!mods) {
            if (vk == VK_ESCAPE) { Stop(); return true; }
            if (vk == VK_BACK || vk == VK_DELETE) { if (set) set(0); Stop(); return true; }
            if (vk < VK_F1 || vk > VK_F24) { needModifier_ = true; return true; }   // a plain letter would fire while typing
        }
        if (set) set((mods << 16) | vk);
        Stop();
        return true;
    }

    void InfoRow::Draw(const Painter& p, bool, bool, bool)
    {
        const Palette& c = p.th->c;
        p.Text(label, R(rect.left, rect.top + 5, rect.left + labelWidth, rect.bottom), 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, c.textDim,
               DWRITE_TEXT_ALIGNMENT_LEADING, false, false, 1.f, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        uint32_t rgb = c.text;
        if (color) if (const uint32_t o = color()) rgb = o;
        p.Text(value ? value() : L"", R(rect.left + labelWidth, rect.top + 5, rect.right, rect.bottom), 13.f, DWRITE_FONT_WEIGHT_NORMAL,
               rgb, DWRITE_TEXT_ALIGNMENT_LEADING, false, true, 1.f, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // ---------------------------------------------------------------- dropdown list popup
    // Never activated: the form keeps focus and forwards keys and the mouse wheel; mouse capture
    // lets a click outside close the list.
    class PopupList
    {
    public:
        PopupList(FormWindow* owner, Dropdown* d) : owner_(owner), d_(d) {}
        ~PopupList() { if (hwnd_) DestroyWindow(hwnd_); }

        bool Open()
        {
            items_ = d_->items ? d_->items() : std::vector<std::wstring>{};
            if (items_.empty()) return false;
            hover_ = d_->selected ? d_->selected() : -1;
            visible_ = std::min<int>(static_cast<int>(items_.size()), 10);
            scroll_ = std::clamp(hover_ - visible_ + 1, 0, static_cast<int>(items_.size()) - visible_);

            static bool registered = false;
            if (!registered) {
                WNDCLASSEXW wc{};
                wc.cbSize = sizeof wc;
                wc.lpfnWndProc = Proc;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                wc.lpszClassName = L"FrameLensList";
                wc.style = CS_DROPSHADOW;
                registered = RegisterClassExW(&wc) != 0;
            }
            const float s = owner_->Dpi() / 96.f;
            const D2D1_RECT_F f = d_->Field();
            POINT tl{ static_cast<LONG>(f.left * s), static_cast<LONG>(f.bottom * s + 2) };
            ClientToScreen(owner_->Hwnd(), &tl);
            const int w = static_cast<int>((f.right - f.left) * s);
            const int h = static_cast<int>((static_cast<float>(visible_) * kRowH + 8.f) * s);
            MONITORINFO mi{};
            mi.cbSize = sizeof mi;
            GetMonitorInfoW(MonitorFromPoint(tl, MONITOR_DEFAULTTONEAREST), &mi);
            if (tl.y + h > mi.rcWork.bottom) tl.y -= h + static_cast<LONG>((f.bottom - f.top + 4) * s);   // open upwards
            hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"FrameLensList", L"", WS_POPUP,
                                    tl.x, tl.y, w, h, owner_->Hwnd(), nullptr, GetModuleHandleW(nullptr), this);
            if (!hwnd_) return false;
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            SetCapture(hwnd_);
            return true;
        }

        bool Key(UINT vk)
        {
            const int n = static_cast<int>(items_.size());
            switch (vk) {
            case VK_ESCAPE: case VK_TAB: owner_->CloseList(); return true;
            case VK_UP: hover_ = std::max(0, hover_ - 1); break;
            case VK_DOWN: hover_ = std::min(n - 1, hover_ + 1); break;
            case VK_HOME: hover_ = 0; break;
            case VK_END: hover_ = n - 1; break;
            case VK_RETURN: case VK_SPACE: Choose(hover_); return true;
            default: return true;   // swallow while open
            }
            if (hover_ < scroll_) scroll_ = hover_;
            if (hover_ >= scroll_ + visible_) scroll_ = hover_ - visible_ + 1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }

        void Wheel(int delta)
        {
            const int n = static_cast<int>(items_.size());
            scroll_ = std::clamp(scroll_ - (delta > 0 ? 1 : -1) * 3, 0, std::max(0, n - visible_));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }

    private:
        static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l)
        {
            if (m == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
            auto* self = reinterpret_cast<PopupList*>(GetWindowLongPtrW(h, GWLP_USERDATA));
            if (!self) return DefWindowProcW(h, m, w, l);
            switch (m) {
            case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
            case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); self->Paint(); EndPaint(h, &ps); return 0; }
            case WM_MOUSEMOVE: {
                const int row = self->RowAt(l);
                if (row != self->hover_ && row >= 0) { self->hover_ = row; InvalidateRect(h, nullptr, FALSE); }
                return 0;
            }
            case WM_LBUTTONDOWN: {
                RECT rc;
                GetClientRect(h, &rc);
                const POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
                if (!PtInRect(&rc, pt)) { self->owner_->CloseList(); return 0; }   // click outside (captured)
                return 0;
            }
            case WM_LBUTTONUP: {
                const int row = self->RowAt(l);
                if (row >= 0) self->Choose(row);
                return 0;
            }
            case WM_MOUSEWHEEL: self->Wheel(GET_WHEEL_DELTA_WPARAM(w)); return 0;
            case WM_CAPTURECHANGED:
                if (reinterpret_cast<HWND>(l) != h) self->owner_->CloseList();   // e.g. Alt+Tab
                return 0;
            case WM_NCDESTROY: SetWindowLongPtrW(h, GWLP_USERDATA, 0); self->hwnd_ = nullptr; break;
            }
            return DefWindowProcW(h, m, w, l);
        }

        int RowAt(LPARAM l) const
        {
            const float s = owner_->Dpi() / 96.f;
            const float y = static_cast<float>(GET_Y_LPARAM(l)) / s - 4.f;
            RECT rc;
            GetClientRect(hwnd_, &rc);
            if (GET_X_LPARAM(l) < 0 || GET_X_LPARAM(l) >= rc.right || y < 0) return -1;
            const int row = scroll_ + static_cast<int>(y / kRowH);
            return row < static_cast<int>(items_.size()) && row < scroll_ + visible_ ? row : -1;
        }

        void Choose(int row)
        {
            Dropdown* d = d_;
            FormWindow* owner = owner_;
            owner->CloseList();   // deferred destroy: safe to keep using locals
            if (row >= 0 && d->onSelect) d->onSelect(row);
            owner->Invalidate();
        }

        void Paint()
        {
            const float dpi = owner_->Dpi();
            RECT rc;
            GetClientRect(hwnd_, &rc);
            if (!target_) {
                const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
                if (FAILED(D2D()->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right, rc.bottom)), &target_)))
                    return;
                target_->CreateSolidColorBrush(Rgb(0), &brush_);
            }
            const Theme& t = owner_->CurrentTheme();
            const Palette& c = t.c;
            Painter p{ target_.Get(), brush_.Get(), &owner_->fonts_, &t, owner_->uiFont_, owner_->displayFont_, owner_->iconFont_, 0.0 };
            const float w = static_cast<float>(rc.right) * 96.f / dpi, h = static_cast<float>(rc.bottom) * 96.f / dpi;
            target_->BeginDraw();
            target_->Clear(Rgb(t.style == Style::Matrix ? c.bg : c.surface));
            StrokeShape(target_.Get(), brush_.Get(), R(0, 0, w, h), 0.f, t.style == Style::Matrix || t.glass ? c.accent : c.border, 1.f, 1.f);
            const int sel = d_->selected ? d_->selected() : -1;
            for (int i = scroll_; i < scroll_ + visible_ && i < static_cast<int>(items_.size()); ++i) {
                const float y = 4.f + static_cast<float>(i - scroll_) * kRowH;
                const D2D1_RECT_F row = R(4, y, w - 4, y + kRowH);
                if (i == hover_) FillShape(target_.Get(), brush_.Get(), row, std::min(t.controlRadius, 6.f), c.accent, t.dark ? 0.22f : 0.14f);
                if (i == sel) FillShape(target_.Get(), brush_.Get(), R(row.left, row.top + 7, row.left + 3, row.bottom - 7), 1.5f, c.accent);
                p.Text(items_[i], R(row.left + 12, row.top, row.right - 8, row.bottom), 13.f,
                       i == sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, c.text);
            }
            if (target_->EndDraw() == D2DERR_RECREATE_TARGET) { target_.Reset(); brush_.Reset(); }
        }

        FormWindow* owner_;
        Dropdown* d_;
        HWND hwnd_ = nullptr;
        std::vector<std::wstring> items_;
        int hover_ = -1, scroll_ = 0, visible_ = 0;
        ComPtr<ID2D1HwndRenderTarget> target_;
        ComPtr<ID2D1SolidColorBrush> brush_;
    };

    // ---------------------------------------------------------------- FormWindow
    FormWindow::FormWindow() : theme_(&GetTheme(Style::Classic, true))
    {
        fonts_.Init(DWrite());
        SetTheme(*theme_);
    }
    FormWindow::~FormWindow()
    {
        list_.reset();
        if (hwnd_) DestroyWindow(hwnd_);
    }

    bool FormWindow::SystemPrefersDark()
    {
        DWORD light = 1, size = sizeof light;
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                         RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
            return false;
        return light == 0;
    }

    float FormWindow::Dpi() const { return hwnd_ ? static_cast<float>(GetDpiForWindow(hwnd_)) : 96.f; }

    bool FormWindow::Create(HINSTANCE inst, const Options& o)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        if (!GetClassInfoExW(inst, o.className, &wc)) {
            wc.cbSize = sizeof wc;
            wc.lpfnWndProc = Proc;
            wc.hInstance = inst;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hIcon = o.iconBig;
            wc.hIconSm = o.iconSmall;
            wc.lpszClassName = o.className;
            RegisterClassExW(&wc);
        }
        start_ = GetTickCount64();
        hwnd_ = CreateWindowExW(o.exStyle, o.className, o.title, o.style, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, o.owner, nullptr, inst, this);
        if (!hwnd_) return false;
        SetTheme(*theme_);
        SetClientSizeDip(o.widthDip, o.heightDip);
        return true;
    }

    void FormWindow::SetClientSizeDip(float w, float h)
    {
        const UINT dpi = GetDpiForWindow(hwnd_);
        RECT r{ 0, 0, static_cast<LONG>(std::ceil(w * dpi / 96.f)), static_cast<LONG>(std::ceil(h * dpi / 96.f)) };
        AdjustWindowRectExForDpi(&r, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), dpi);
        SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void FormWindow::SetTheme(const Theme& t)
    {
        theme_ = &t;
        fonts_.Clear();
        uiFont_ = fonts_.Resolve(t.uiFonts);
        displayFont_ = fonts_.Resolve(t.displayFonts);
        const wchar_t* icons[3] = { L"Segoe Fluent Icons", L"Segoe MDL2 Assets", nullptr };
        iconFont_ = fonts_.Resolve(icons);
        if (!hwnd_) return;
        ApplyTitleBar();
        if (t.rain) SetTimer(hwnd_, kAnimTimer, 150, nullptr);   // animate the glyph rain
        else KillTimer(hwnd_, kAnimTimer);
        Invalidate();
    }

    void FormWindow::ApplyTitleBar()
    {
        // Dark/light caption; on Windows 11 also the caption colour and corner style of the theme.
        const BOOL dark = theme_->dark;
        DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
        const COLORREF caption = ToColorRef(theme_->style == Style::SciFi2001 ? theme_->c.glassTop : theme_->c.bg);
        const COLORREF text = ToColorRef(theme_->style == Style::Matrix ? theme_->c.accent : theme_->c.text);
        DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof caption);
        DwmSetWindowAttribute(hwnd_, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof text);
        const int corners = theme_->style == Style::Matrix ? 1 /*DONOTROUND*/ : 2 /*ROUND*/;
        DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corners, sizeof corners);
        if (GetWindowLongPtrW(hwnd_, GWL_STYLE) & WS_POPUP) {   // popups (tray panel): the theme's border colour
            const COLORREF border = ToColorRef(theme_->style == Style::Matrix ? theme_->c.accent : theme_->c.border);
            DwmSetWindowAttribute(hwnd_, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof border);
        }
    }

    void FormWindow::Invalidate()
    {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void FormWindow::ClearControls()
    {
        CloseList();
        controls_.clear();
        hot_ = pressed_ = focus_ = -1;
    }

    void FormWindow::OpenList(Dropdown* d)
    {
        list_.reset();
        auto l = std::make_unique<PopupList>(this, d);
        if (l->Open()) list_ = std::move(l);
        Invalidate();
    }

    void FormWindow::CloseList()
    {
        // Deferred: this may be called from inside the list's own window procedure.
        if (list_ && hwnd_) PostMessageW(hwnd_, WM_CLOSELIST, 0, 0);
    }

    D2D1_POINT_2F FormWindow::ToDip(LPARAM l) const
    {
        const float s = 96.f / Dpi();
        return D2D1::Point2F(static_cast<float>(GET_X_LPARAM(l)) * s, static_cast<float>(GET_Y_LPARAM(l)) * s);
    }

    int FormWindow::HitTest(D2D1_POINT_2F p) const
    {
        for (int i = static_cast<int>(controls_.size()) - 1; i >= 0; --i) {   // topmost (last added) first
            const Control& c = *controls_[i];
            if (c.Focusable() && Inside(c.rect, p)) return i;   // labels and cards don't take the mouse
        }
        return -1;
    }

    void FormWindow::MoveFocus(int dir)
    {
        const int n = static_cast<int>(controls_.size());
        for (int k = 1; k <= n; ++k) {
            const int i = ((focus_ < 0 ? (dir > 0 ? -1 : 0) : focus_) + dir * k + n * 2) % n;
            if (controls_[i]->Focusable() && controls_[i]->Enabled()) { SetFocusIndex(i); break; }
        }
        keyboardFocus_ = true;
        Invalidate();
    }

    void FormWindow::SetFocusIndex(int i)
    {
        if (focus_ != i && focus_ >= 0 && focus_ < static_cast<int>(controls_.size())) controls_[focus_]->OnBlur();
        focus_ = i;
    }

    bool FormWindow::EnsureTarget()
    {
        if (target_) return true;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        const float dpi = Dpi();
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
        if (FAILED(D2D()->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right, rc.bottom)), &target_)))
            return false;
        target_->CreateSolidColorBrush(Rgb(0), &brush_);
        return true;
    }

    void FormWindow::Paint()
    {
        if (!EnsureTarget()) return;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        target_->BeginDraw();
        PaintTo(target_.Get(), brush_.Get(), static_cast<float>(rc.right) * 96.f / Dpi(), static_cast<float>(rc.bottom) * 96.f / Dpi(),
                static_cast<double>(GetTickCount64() - start_) / 1000.0);
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET) { target_.Reset(); brush_.Reset(); }
    }

    void FormWindow::PaintTo(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float w, float h, double timeSec)
    {
        const Theme& t = *theme_;
        Painter p{ rt, brush, &fonts_, &t, uiFont_, displayFont_, iconFont_, timeSec };
        rt->Clear(Rgb(t.c.bg));
        if (t.glass) FillGradient(rt, R(0, 0, w, h), 0.f, t.c.bg, t.c.glassBottom, 1.f);
        for (size_t i = 0; i < controls_.size(); ++i) {
            const int k = static_cast<int>(i);
            controls_[i]->Draw(p, k == hot_ && pressed_ < 0, k == pressed_, k == focus_ && keyboardFocus_);
        }
    }

    HRESULT FormWindow::RenderToPng(const wchar_t* path, float widthDip, float heightDip, float dpi, double timeSec)
    {
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICBitmap> bmp;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        const UINT pw = static_cast<UINT>(std::ceil(widthDip * dpi / 96.f)), ph = static_cast<UINT>(std::ceil(heightDip * dpi / 96.f));
        if (FAILED(hr) || FAILED(hr = wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return hr;
        ComPtr<ID2D1RenderTarget> rt;
        ComPtr<ID2D1SolidColorBrush> brush;
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
        if (FAILED(hr = D2D()->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt)) || FAILED(hr = rt->CreateSolidColorBrush(Rgb(0), &brush)))
            return hr;
        rt->BeginDraw();
        PaintTo(rt.Get(), brush.Get(), widthDip, heightDip, timeSec);
        if (FAILED(hr = rt->EndDraw())) return hr;

        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (SUCCEEDED(hr = wic->CreateStream(&stream)) && SUCCEEDED(hr = stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
            SUCCEEDED(hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(hr = encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(hr = frame->Initialize(nullptr)) &&
            SUCCEEDED(hr = frame->WriteSource(bmp.Get(), nullptr)) && SUCCEEDED(hr = frame->Commit()))
            hr = encoder->Commit();
        return hr;
    }

    LRESULT CALLBACK FormWindow::Proc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        if (m == WM_NCCREATE) {
            auto* self = reinterpret_cast<FormWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->hwnd_ = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<FormWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        return self ? self->Handle(m, w, l) : DefWindowProcW(h, m, w, l);
    }

    LRESULT FormWindow::Handle(UINT m, WPARAM w, LPARAM l)
    {
        if (message) {
            LRESULT r = 0;
            if (message(m, w, l, r)) return r;
        }
        switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Paint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_SIZE:
            if (target_) target_->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
            Invalidate();
            return 0;
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(l);
            target_.Reset();
            brush_.Reset();
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_TIMER:
            if (w == kAnimTimer) { Invalidate(); return 0; }
            break;
        case WM_MOUSEMOVE: {
            const D2D1_POINT_2F p = ToDip(l);
            if (pressed_ >= 0) { controls_[pressed_]->OnDrag(p); Invalidate(); return 0; }
            const int hit = HitTest(p);
            if (hit != hot_) {
                hot_ = hit;
                TRACKMOUSEEVENT tme{ sizeof tme, TME_LEAVE, hwnd_, 0 };
                TrackMouseEvent(&tme);
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            hot_ = -1;
            Invalidate();
            return 0;
        case WM_LBUTTONDOWN: {
            const D2D1_POINT_2F p = ToDip(l);
            const int hit = HitTest(p);
            if (hit >= 0 && controls_[hit]->Enabled()) {
                pressed_ = hit;
                SetFocusIndex(hit);
                keyboardFocus_ = false;
                SetCapture(hwnd_);
                controls_[hit]->OnPress(p);
                Invalidate();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (pressed_ < 0) return 0;
            const int was = pressed_;
            pressed_ = -1;
            ReleaseCapture();
            const D2D1_POINT_2F p = ToDip(l);
            if (Inside(controls_[was]->rect, p)) controls_[was]->OnClick(p);
            Invalidate();
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressed_ = -1;
            return 0;
        case WM_MOUSEWHEEL:
            if (list_) { list_->Wheel(GET_WHEEL_DELTA_WPARAM(w)); return 0; }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (focus_ >= 0 && controls_[focus_]->CapturesKeys()) {   // recording a hotkey: every key is for it
                if (m == WM_SYSKEYDOWN) swallowSysKeys_ = true;
                controls_[focus_]->OnKey(static_cast<UINT>(w));
                Invalidate();
                return 0;
            }
            if (m == WM_SYSKEYDOWN) break;   // Alt+F4, the system menu...
            if (list_) { list_->Key(static_cast<UINT>(w)); return 0; }
            if (w == VK_TAB) { MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1); return 0; }
            if (w == VK_ESCAPE) { if (onEscape) onEscape(); return 0; }
            if (focus_ >= 0 && controls_[focus_]->OnKey(static_cast<UINT>(w))) { keyboardFocus_ = true; Invalidate(); return 0; }
            break;
        case WM_SYSKEYUP:
        case WM_SYSCHAR:
            if (swallowSysKeys_) {
                if (m == WM_SYSKEYUP && (w == VK_MENU || w == VK_F10)) swallowSysKeys_ = false;
                return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE) {
                CloseList();
                if (focus_ >= 0 && focus_ < static_cast<int>(controls_.size())) controls_[focus_]->OnBlur();
            }
            break;
        case WM_CLOSELIST:
            list_.reset();
            Invalidate();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            list_.reset();
            KillTimer(hwnd_, kAnimTimer);
            target_.Reset();
            brush_.Reset();
            if (onDestroy) onDestroy();
            return 0;
        case WM_NCDESTROY: {
            const HWND h = hwnd_;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            return DefWindowProcW(h, m, w, l);
        }
        }
        return DefWindowProcW(hwnd_, m, w, l);
    }
}
