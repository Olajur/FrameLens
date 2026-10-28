// FrameLens — on-screen display (OSD) renderer.
// SPDX-License-Identifier: MIT
#include "OsdRenderer.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace fl::osd
{
    namespace
    {
        constexpr float kInitialHeightDip = 100.f;   // first swap chain size; Draw resizes it to the content

        D2D1_RECT_F R(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
        UINT ToPx(float dip, float dpi) { return static_cast<UINT>(std::ceil(dip * dpi / 96.f)); }
    }

    namespace
    {
        bool Fail(Renderer& r, std::wstring& error, const wchar_t* what, HRESULT hr)
        {
            wchar_t b[128];
            swprintf(b, 128, L"%ls failed (0x%08lX)", what, static_cast<unsigned long>(hr));
            error = b;
            r.Reset();
            return false;
        }
    }

    bool Renderer::CreateOffscreen(float dpi, std::wstring& error)
    {
        dpi_ = dpi;
        return CreateDrawing(false, error);
    }

    bool Renderer::Create(HWND hwnd, bool warp, std::wstring& error)
    {
        auto fail = [&](const wchar_t* what, HRESULT hr) { return Fail(*this, error, what, hr); };
        dpi_ = static_cast<float>(GetDpiForWindow(hwnd));
        if (!CreateDrawing(warp, error)) return false;

        ComPtr<IDXGIDevice> dxgiDev;
        d3d_.As(&dxgiDev);
        HRESULT hr;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return fail(L"CreateDXGIFactory2", hr);

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = ToPx(kWidthDip, dpi_);
        sd.Height = ToPx(kInitialHeightDip, dpi_);
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(hr = factory->CreateSwapChainForComposition(dxgiDev.Get(), &sd, nullptr, &swap_))) return fail(L"CreateSwapChainForComposition", hr);
        size_ = baseSize_ = { static_cast<LONG>(sd.Width), static_cast<LONG>(sd.Height) };

        if (FAILED(hr = DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&dcomp_)))) return fail(L"DCompositionCreateDevice", hr);
        if (FAILED(hr = dcomp_->CreateTargetForHwnd(hwnd, TRUE, &target_))) return fail(L"CreateTargetForHwnd", hr);
        if (FAILED(hr = dcomp_->CreateVisual(&visual_))) return fail(L"CreateVisual", hr);
        visual_->SetContent(swap_.Get());
        target_->SetRoot(visual_.Get());
        if (FAILED(hr = dcomp_->Commit())) return fail(L"DComp Commit", hr);
        return true;
    }

    bool Renderer::CreateDrawing(bool warp, std::wstring& error)
    {
        auto fail = [&](const wchar_t* what, HRESULT hr) { return Fail(*this, error, what, hr); };
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
        if (FAILED(hr) && !warp)
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
        if (FAILED(hr)) return fail(L"D3D11CreateDevice", hr);

        ComPtr<IDXGIDevice> dxgiDev;
        d3d_.As(&dxgiDev);
        D2D1_FACTORY_OPTIONS fo{};
        if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                          reinterpret_cast<void**>(d2dFactory_.GetAddressOf())))) return fail(L"D2D1CreateFactory", hr);
        ComPtr<ID2D1Device> d2dDev;
        if (FAILED(hr = d2dFactory_->CreateDevice(dxgiDev.Get(), &d2dDev))) return fail(L"D2D CreateDevice", hr);
        if (FAILED(hr = d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx_))) return fail(L"CreateDeviceContext", hr);
        ctx_->SetDpi(dpi_, dpi_);
        ctx_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);   // ClearType needs an opaque target
        if (FAILED(hr = ctx_->CreateSolidColorBrush(D2D1::ColorF(P().panelText), &brush_))) return fail(L"CreateSolidColorBrush", hr);

        if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                            reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) return fail(L"DWriteCreateFactory", hr);
        // Bahnschrift (condensed DIN-style, ships with Windows 10 1709+) gives the classic OSD look.
        uiFonts_.Init(dwrite_.Get());
        family_ = uiFonts_.Resolve(theme_->displayFonts);
        return true;
    }

    void Renderer::SetTheme(const ui::Theme& theme)
    {
        // The app passes an adjusted copy (high contrast, own colours) that it updates in place, so
        // compare the parts that affect text formats rather than the address.
        const bool fontsChange = theme.style != fontStyle_ || theme.monospace != theme_->monospace || theme.condensed != theme_->condensed;
        theme_ = &theme;
        if (!fontsChange && !family_.empty()) return;
        fontStyle_ = theme.style;
        formats_.clear();
        uiFonts_.Clear();
        if (dwrite_) family_ = uiFonts_.Resolve(theme_->displayFonts);
    }

    uint32_t Renderer::ToneColor(Tone t, uint32_t normal) const
    {
        switch (t) {
        case Tone::Warm: return P().warm;
        case Tone::Hot: return P().hot;
        case Tone::Muted: return P().panelMuted;
        default: return normal;
        }
    }

    void Renderer::Reset()
    {
        formats_.clear();
        brush_.Reset();
        ctx_.Reset();
        d2dFactory_.Reset();
        visual_.Reset();
        target_.Reset();
        dcomp_.Reset();
        swap_.Reset();
        d3d_.Reset();
        dwrite_.Reset();
        size_ = baseSize_ = {};
    }

    IDWriteTextFormat* Renderer::Format(float size, DWRITE_FONT_WEIGHT weight, bool wrap)
    {
        const uint32_t key = (static_cast<uint32_t>(size * 4.f) << 12) | (static_cast<uint32_t>(weight) << 1) | (wrap ? 1u : 0u);
        auto& f = formats_[key];
        if (!f) {
            dwrite_->CreateTextFormat(family_.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                      theme_->condensed ? DWRITE_FONT_STRETCH_SEMI_CONDENSED : DWRITE_FONT_STRETCH_NORMAL,
                                      theme_->monospace ? size * 0.86f : size, L"en-us", &f);   // monospace runs wide
            if (!f) return nullptr;
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        return f.Get();
    }

    void Renderer::Text(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight, D2D1_RECT_F r, uint32_t rgb,
                        DWRITE_TEXT_ALIGNMENT align, bool wrap)
    {
        if (measuring_ || s.empty()) return;
        IDWriteTextFormat* f = Format(size, weight, wrap);
        if (!f) return;
        f->SetTextAlignment(align);
        std::wstring text = s;
        if (theme_->uppercase) for (auto& ch : text) ch = static_cast<wchar_t>(towupper(ch));
        const UINT32 n = static_cast<UINT32>(text.size());
        if (theme_->outline) {   // high contrast: a solid 1 px outline, readable over any scene
            brush_->SetColor(D2D1::ColorF(theme_->dark ? 0x000000 : 0xFFFFFF));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dy)
                        ctx_->DrawText(text.data(), n, f, R(r.left + static_cast<float>(dx), r.top + static_cast<float>(dy),
                                                            r.right + static_cast<float>(dx), r.bottom + static_cast<float>(dy)), brush_.Get());
        }
        else if (shadow_) {   // keeps text readable over bright game scenes without a panel
            brush_->SetColor(D2D1::ColorF(theme_->dark ? 0x000000 : 0xFFFFFF, 0.85f));
            ctx_->DrawText(text.data(), n, f, R(r.left + 1, r.top + 1, r.right + 1, r.bottom + 1), brush_.Get());
        }
        if (theme_->monospace && theme_->glow) {   // phosphor glow of a green-screen terminal
            brush_->SetColor(D2D1::ColorF(rgb, 0.22f));
            for (const auto [dx, dy] : { std::pair{ -1.f, 0.f }, std::pair{ 1.f, 0.f }, std::pair{ 0.f, -1.f }, std::pair{ 0.f, 1.f } })
                ctx_->DrawText(text.data(), n, f, R(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy), brush_.Get());
        }
        brush_->SetColor(D2D1::ColorF(rgb));
        ctx_->DrawText(text.data(), n, f, r, brush_.Get());
    }

    // The GPU name shrinks to fit the panel width instead of being cut off.
    void Renderer::Title(const std::wstring& s, D2D1_RECT_F r, uint32_t rgb)
    {
        if (measuring_ || s.empty()) return;
        IDWriteTextFormat* f = Format(21.f, DWRITE_FONT_WEIGHT_BOLD);
        if (!f) return;
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        ComPtr<IDWriteTextLayout> layout;
        const float w = r.right - r.left, h = r.bottom - r.top;
        if (FAILED(dwrite_->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), f, w, h, &layout))) return;
        DWRITE_TEXT_METRICS m{};
        if (SUCCEEDED(layout->GetMetrics(&m)) && m.width > w)
            layout->SetFontSize(std::max(11.f, 21.f * w / m.width), { 0, static_cast<UINT32>(s.size()) });
        if (theme_->outline) {
            brush_->SetColor(D2D1::ColorF(theme_->dark ? 0x000000 : 0xFFFFFF));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dy)
                        ctx_->DrawTextLayout(D2D1::Point2F(r.left + static_cast<float>(dx), r.top + static_cast<float>(dy)), layout.Get(), brush_.Get());
        }
        else if (shadow_) {
            brush_->SetColor(D2D1::ColorF(theme_->dark ? 0x000000 : 0xFFFFFF, 0.85f));
            ctx_->DrawTextLayout(D2D1::Point2F(r.left + 1, r.top + 1), layout.Get(), brush_.Get());
        }
        brush_->SetColor(D2D1::ColorF(rgb));
        ctx_->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), brush_.Get());
    }

    void Renderer::Graph(const std::vector<float>& v, float lo, float hi, D2D1_RECT_F r, uint32_t rgb, float width)
    {
        if (measuring_ || v.size() < 2 || hi <= lo) return;
        ComPtr<ID2D1PathGeometry> geo;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(d2dFactory_->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return;
        const float n = static_cast<float>(v.size() - 1);
        auto pt = [&](size_t i) {
            const float t = (std::clamp(v[i], lo, hi) - lo) / (hi - lo);
            return D2D1::Point2F(r.left + (r.right - r.left) * static_cast<float>(i) / n, r.bottom - t * (r.bottom - r.top));
        };
        sink->BeginFigure(pt(0), D2D1_FIGURE_BEGIN_HOLLOW);
        for (size_t i = 1; i < v.size(); ++i) sink->AddLine(pt(i));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        if (FAILED(sink->Close())) return;
        if (theme_->style == ui::Style::Minimal2026 || theme_->style == ui::Style::SciFi2001) {
            ComPtr<ID2D1PathGeometry> area;
            ComPtr<ID2D1GeometrySink> as;
            if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&area)) && SUCCEEDED(area->Open(&as))) {
                as->BeginFigure(D2D1::Point2F(r.left, r.bottom), D2D1_FIGURE_BEGIN_FILLED);
                for (size_t i = 0; i < v.size(); ++i) as->AddLine(pt(i));
                as->AddLine(D2D1::Point2F(r.right, r.bottom));
                as->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(as->Close())) {
                    brush_->SetColor(D2D1::ColorF(rgb, 0.16f));
                    ctx_->FillGeometry(area.Get(), brush_.Get());
                }
            }
        }
        brush_->SetColor(D2D1::ColorF(rgb));
        ctx_->DrawGeometry(geo.Get(), brush_.Get(), width * theme_->lineScale);
    }

    void Renderer::Fill(D2D1_RECT_F r, uint32_t rgb, float alpha)
    {
        if (measuring_) return;
        brush_->SetColor(D2D1::ColorF(rgb, alpha));
        ctx_->FillRectangle(r, brush_.Get());
    }

    // One value cell: right-aligned number + small unit. Units are left out after n/a / --,
    // so the panel never suggests a value exists.
    void Renderer::CellAt(const Cell& c, float x, float y, float w)
    {
        Text(c.value, 18.f, DWRITE_FONT_WEIGHT_BOLD, R(x, y, x + 40, y + 22), ToneColor(c.tone, P().panelText), DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (c.tone != Tone::Muted)
            Text(c.unit, 10.f, DWRITE_FONT_WEIGHT_BOLD, R(x + 42, y + 6, x + w + 24, y + 22), ToneColor(c.tone, P().panelText));
    }

    float Renderer::TextWidth(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight)
    {
        IDWriteTextFormat* f = Format(size, weight);
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS m{};
        if (!f || s.empty() || FAILED(dwrite_->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), f, 4096.f, 64.f, &layout)) ||
            FAILED(layout->GetMetrics(&m)))
            return 0.f;
        return m.widthIncludingTrailingWhitespace;
    }

    // Bar: [GPU 74% 64°C …] | [GPU2 …] | [CPU 47% …] | [RAM 10.9 GB] | 103 FPS 9.7 ms AVG … ~graph~
    // Left to right in one row; items that don't fit the monitor's width are cut off at the edge.
    float Renderer::LayoutBar(const OsdView& v)
    {
        constexpr auto kBold = DWRITE_FONT_WEIGHT_BOLD;
        constexpr auto kSemi = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        constexpr float H = 28.f, kPad = 12.f;
        float x = kPad;

        auto put = [&](const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight, uint32_t rgb, float gapAfter, float drop = 0.f) {
            if (s.empty()) return;
            const float w = TextWidth(s, size, weight);
            Text(s, size, weight, R(x, drop, x + w + 2.f, H + drop), rgb);
            x += w + gapAfter;
        };
        auto valueUnit = [&](const Cell& c, float valueSize, uint32_t normal, float gapAfter) {
            put(c.value, valueSize, kBold, ToneColor(c.tone, normal), c.tone == Tone::Muted || c.unit.empty() ? gapAfter : 1.f);
            if (c.tone != Tone::Muted) put(c.unit, 10.f, kBold, ToneColor(c.tone, normal), gapAfter, 1.5f);
        };
        auto divider = [&] {
            if (x <= kPad) return;
            Fill(R(x, 7, x + 1.f, H - 7), P().panelMuted, 0.55f);
            x += 11.f;
        };

        for (const auto& s : v.sections) {
            divider();
            // With several GPUs the caption starts with the GPU's number ("2 · …"): label "GPU2".
            std::wstring label = s.label;
            if (!s.caption.empty()) label += s.caption.substr(0, s.caption.find(L' '));
            put(label, 13.f, kBold, s.rgb, 7.f);
            if (s.headline) valueUnit(s.head, 15.f, P().panelText, 8.f);
            for (const auto& c : s.cells) valueUnit(c, 14.f, P().panelText, 8.f);
        }

        if (v.showFps || v.showFrametime || !v.stats.empty()) {
            divider();
            if (!v.frameNote.empty() && (v.showFps || v.showFrametime)) put(v.frameNote, 12.f, DWRITE_FONT_WEIGHT_NORMAL, P().panelMuted, 10.f);
            if (v.showFps) {
                put(v.fps, 17.f, kBold, v.fpsRgb >= 0 ? static_cast<uint32_t>(v.fpsRgb) : P().panelText, 2.f);
                put(L"FPS", 10.f, kBold, P().panelMuted, 9.f, 1.5f);
            }
            if (v.showFrametime) {
                put(v.frametime, 14.f, kBold, P().label, 1.f);
                put(L"ms", 10.f, kBold, P().label, 9.f, 1.5f);
            }
            for (const auto& s : v.stats) {
                put(s.label, 10.f, kSemi, s.primary ? P().panelText : P().label, 3.f, 1.f);
                put(s.value, 13.f, kBold, P().panelText, 8.f);
            }
            if (v.graphs && v.frameNote.empty() && v.frametimeHistory.size() >= 2) {
                float avg = 0.f, peak = 0.f;
                for (float ms : v.frametimeHistory) { avg += ms; peak = std::max(peak, ms); }
                avg /= static_cast<float>(v.frametimeHistory.size());
                Graph(v.frametimeHistory, 0.f, std::max(peak, avg * 2.f), R(x + 2.f, 6, x + 112.f, H - 6), P().labelBar, 1.f);
                x += 120.f;
            }
        }
        return H;
    }

    float Renderer::Layout(const OsdView& v)
    {
        if (v.bar) return LayoutBar(v);
        constexpr auto kBold = DWRITE_FONT_WEIGHT_BOLD;
        constexpr auto kSemi = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        constexpr auto kRight = DWRITE_TEXT_ALIGNMENT_TRAILING;
        const float L = 12.f, Rt = width_ - 12.f;
        const bool compact = width_ < kWidthDip;
        float y = 8.f;

        // Header: GPU name + rule.
        if (!v.title.empty()) {
            Title(v.title, R(L, 6, Rt, 34), P().accent);
            Fill(R(8, 37, width_ - 8, 39), P().accent);
            y = 44.f;
        }

        // Why there are no frames: shown once, in place of the first frame graph.
        bool noteShown = false;
        auto note = [&](D2D1_RECT_F r) {
            if (v.frameNote.empty() || noteShown || compact) return false;
            Text(v.frameNote, 12.f, DWRITE_FONT_WEIGHT_NORMAL, r, P().panelMuted, DWRITE_TEXT_ALIGNMENT_LEADING, true);
            noteShown = true;
            return true;
        };

        if (v.showFps) {
            Text(v.fps, 44.f, kBold, R(L, y - 2, 112, y + 48), v.fpsRgb >= 0 ? static_cast<uint32_t>(v.fpsRgb) : P().panelText, kRight);
            if (!note(R(122, y, Rt, y + 48)) && v.graphs && v.fpsHistory.size() >= 2) {
                const auto [lo, hi] = std::minmax_element(v.fpsHistory.begin(), v.fpsHistory.end());
                const float pad = std::max(2.f, (*hi - *lo) * 0.15f);
                Graph(v.fpsHistory, std::max(0.f, *lo - pad), *hi + pad, R(122, y + 6, Rt, y + 28), P().panelText, 1.4f);
            }
            y += 50.f;
        }

        // Frame time + per-frame graph (spikes = stutters).
        if (v.showFrametime) {
            Text(L"FTIME", 14.f, kSemi, R(L, y + 2, 70, y + 28), P().label);
            Text(v.frametime, 24.f, kBold, R(56, y, 112, y + 30), P().label, kRight);
            if (!note(R(122, y, Rt, y + 30)) && v.graphs && v.frametimeHistory.size() >= 2) {
                float avg = 0.f, peak = 0.f;
                for (float ms : v.frametimeHistory) { avg += ms; peak = std::max(peak, ms); }
                avg /= static_cast<float>(v.frametimeHistory.size());
                Fill(R(122, y + 24.5f, Rt, y + 25.5f), P().labelBar, 0.5f);
                Graph(v.frametimeHistory, 0.f, std::max(peak, avg * 2.f), R(122, y + 6, Rt, y + 25), P().labelBar, 1.f);
            }
            y += 32.f;
        }

        // AVG / MAX / MIN / lows with bars.
        if (!v.stats.empty()) {
            y += 4.f;
            for (const auto& s : v.stats) {
                Text(s.label, 16.f, kSemi, R(L, y, 58, y + 22), s.primary ? P().panelText : P().label);
                Text(s.value, 16.f, kBold, R(58, y, 100, y + 22), P().panelText, kRight);
                if (s.bar > 0.f) Fill(R(108, y + 4, 108 + std::min(s.bar, 1.f) * (Rt - 108), y + 18), s.primary ? P().panelText : P().labelBar);
                y += 23.f;
            }
        }

        // Sections: label (+ headline value and graph), then a grid of cells.
        for (const auto& s : v.sections) {
            y += 6.f;
            Text(s.label, 24.f, kBold, R(L, y, 72, y + 30), s.rgb);
            // Caption (which GPU, when there are several) on its own line under the label row.
            const bool caption = !s.caption.empty();
            if (caption) Text(s.caption, 11.f, kSemi, R(L, y + 29, Rt, y + 43), P().panelMuted);
            const float labelRows = caption ? 44.f : 30.f;
            float x0 = L, top = y + labelRows + 2.f;
            int cols = 3;
            if (s.headline) {
                Text(s.head.value, 24.f, kBold, R(60, y, 104, y + 30), ToneColor(s.head.tone, P().panelText), kRight);
                if (s.head.tone != Tone::Muted) Text(s.head.unit, 11.f, kBold, R(106, y + 10, 124, y + 28), ToneColor(s.head.tone, P().panelText));
                if (v.graphs) Graph(s.history, 0.f, 100.f, R(128, y + 4, Rt, y + 26), s.rgb, 1.2f);
            }
            else if (!caption) {
                x0 = 76.f;   // no headline: the cells start on the label's row
                top = y + 4.f;
                cols = 2;
            }
            const float colW = (Rt - x0) / static_cast<float>(cols);
            for (size_t i = 0; i < s.cells.size(); ++i)
                CellAt(s.cells[i], x0 + static_cast<float>(i % cols) * colW, top + static_cast<float>(i / cols) * 24.f, colW);
            const size_t rows = (s.cells.size() + cols - 1) / cols;
            y = std::max(y + labelRows, top + static_cast<float>(rows) * 24.f);

            // Per-core grid: 4 cores per row (2 when load and clock are both shown).
            if (!s.cores.empty()) {
                const bool both = s.coreLoad && s.coreClock;
                const int gc = both ? 2 : 4;
                const float cw = (Rt - L) / static_cast<float>(gc), rh = 19.f, gt = y + 2.f;
                for (size_t i = 0; i < s.cores.size(); ++i) {
                    const CoreCell& c = s.cores[i];
                    const float x = L + static_cast<float>(i % gc) * cw, cy = gt + static_cast<float>(i / gc) * rh;
                    Text(c.label, 10.f, kSemi, R(x, cy, x + 24, cy + rh), P().panelMuted);
                    float vx = x + 22.f;
                    if (s.coreLoad) {
                        // Alone: the value ends 8 DIP before the next cell, so "11%" and "P2" don't touch.
                        const float vr = both ? vx + 34.f : x + cw - 8.f;
                        Text(c.load, 13.f, kBold, R(vx, cy, vr, cy + rh), ToneColor(c.tone, P().panelText), kRight);
                        if (c.loadFraction > 0.f)
                            Fill(R(x + 2, cy + rh - 3.5f, x + 2 + std::min(c.loadFraction, 1.f) * (cw - 8), cy + rh - 2), s.rgb, 0.6f);
                        vx += 38.f;
                    }
                    if (s.coreClock)
                        Text(c.clock, both ? 12.f : 13.f, both ? DWRITE_FONT_WEIGHT_NORMAL : kBold, R(vx, cy, x + cw - 4, cy + rh),
                             P().panelText, both ? DWRITE_TEXT_ALIGNMENT_LEADING : kRight);
                }
                y = gt + static_cast<float>((s.cores.size() + gc - 1) / gc) * rh + 2.f;
            }
        }
        return y + 8.f;
    }

    SIZE Renderer::Measure(const OsdView& v)
    {
        if (v.bar) width_ = std::max(kCompactWidthDip, v.barWidthDip);
        else width_ = (v.title.empty() && !v.showFrametime && v.stats.empty() && v.sections.empty() && !v.graphs)
                          ? kCompactWidthDip : kWidthDip;
        const float scale = std::clamp(v.scale, 0.5f, 2.f);
        measuring_ = true;
        heightDip_ = Layout(v);
        measuring_ = false;
        baseSize_ = { static_cast<LONG>(ToPx(width_, dpi_)), static_cast<LONG>(ToPx(heightDip_, dpi_)) };
        return { static_cast<LONG>(ToPx(width_ * scale, dpi_)), static_cast<LONG>(ToPx(heightDip_ * scale, dpi_)) };
    }

    HRESULT Renderer::Draw(const OsdView& v, POINT offset, SIZE surfaceSize)
    {
        if (!ctx_) return E_UNEXPECTED;

        Measure(v);
        const UINT w = static_cast<UINT>(std::max<LONG>(1, surfaceSize.cx)), h = static_cast<UINT>(std::max<LONG>(1, surfaceSize.cy));
        HRESULT hr = S_OK;
        if (static_cast<LONG>(w) != size_.cx || static_cast<LONG>(h) != size_.cy) {
            if (FAILED(hr = swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return hr;
            size_ = { static_cast<LONG>(w), static_cast<LONG>(h) };
        }

        ComPtr<IDXGISurface> surface;
        if (FAILED(hr = swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return hr;
        const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_, dpi_);
        ComPtr<ID2D1Bitmap1> bmp;
        if (FAILED(hr = ctx_->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &bmp))) return hr;

        ctx_->SetTarget(bmp.Get());
        ctx_->BeginDraw();
        ctx_->Clear(D2D1::ColorF(0, 0, 0, 0));
        Paint(v, offset);
        hr = ctx_->EndDraw();
        ctx_->SetTarget(nullptr);
        if (FAILED(hr)) return hr;
        return swap_->Present(0, 0);
    }

    void Renderer::Paint(const OsdView& v, POINT offset)
    {
        // Size setting scales everything, text included; then the panel moves to its offset.
        const float scale = std::clamp(v.scale, 0.5f, 2.f);
        ctx_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                           D2D1::Matrix3x2F::Translation(offset.x * 96.f / dpi_, offset.y * 96.f / dpi_));
        if (v.background)
            ui::PanelBackground(ctx_.Get(), brush_.Get(), uiFonts_, *theme_, R(0, 0, width_, heightDip_), v.bar ? 0.f : theme_->radius,
                                P().panel, std::clamp(v.opacity, 0.f, 1.f), v.timeSec, true);
        shadow_ = !v.background || v.opacity < 0.5f;
        Layout(v);
        ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    HRESULT Renderer::RenderToPng(const OsdView& v, const wchar_t* path, uint32_t backdropRgb)
    {
        if (!ctx_) return E_UNEXPECTED;
        const SIZE size = Measure(v);
        const D2D1_SIZE_U px = D2D1::SizeU(static_cast<UINT32>(size.cx), static_cast<UINT32>(size.cy));
        const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        ComPtr<ID2D1Bitmap1> target, readback;
        HRESULT hr = ctx_->CreateBitmap(px, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, dpi_, dpi_), &target);
        if (FAILED(hr)) return hr;
        if (FAILED(hr = ctx_->CreateBitmap(px, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format, dpi_, dpi_), &readback)))
            return hr;

        ctx_->SetTarget(target.Get());
        ctx_->BeginDraw();
        ctx_->Clear(D2D1::ColorF(backdropRgb));   // stands in for the game behind the overlay
        Paint(v, { 0, 0 });
        hr = ctx_->EndDraw();
        ctx_->SetTarget(nullptr);
        if (FAILED(hr) || FAILED(hr = readback->CopyFromBitmap(nullptr, target.Get(), nullptr))) return hr;

        D2D1_MAPPED_RECT map{};
        if (FAILED(hr = readback->Map(D2D1_MAP_OPTIONS_READ, &map))) return hr;
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppPBGRA;
        if (SUCCEEDED(hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
            SUCCEEDED(hr = wic->CreateStream(&stream)) &&
            SUCCEEDED(hr = stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
            SUCCEEDED(hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(hr = encoder->CreateNewFrame(&frame, nullptr)) &&
            SUCCEEDED(hr = frame->Initialize(nullptr)) &&
            SUCCEEDED(hr = frame->SetSize(px.width, px.height)) &&
            SUCCEEDED(hr = frame->SetPixelFormat(&pf)) &&
            SUCCEEDED(hr = frame->WritePixels(px.height, map.pitch, map.pitch * px.height, map.bits)) &&
            SUCCEEDED(hr = frame->Commit()))
            hr = encoder->Commit();
        readback->Unmap();
        return hr;
    }
}
