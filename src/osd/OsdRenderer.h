// FrameLens — on-screen display (OSD) renderer.
//
// Draws the overlay panel with Direct2D/DirectWrite into a DirectComposition swap chain attached
// to a small click-through window. The panel is built from an OsdView: an optional title, the
// frame block (FPS, frame time, AVG/MAX/MIN/lows) and any number of sections (GPU, CPU, RAM),
// each with an optional headline value + graph and a grid of value cells. The panel's height
// follows its content. The host gives each Draw a surface (window) size and the panel's offset
// inside it, so the panel can move or change Size without the window moving.
// SPDX-License-Identifier: MIT
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include "../ui/Draw.h"
#include <wrl/client.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fl::osd
{
    constexpr float kWidthDip = 260.f;        // panel width at 100 %; the height depends on the content
    constexpr float kCompactWidthDip = 124.f; // FPS number alone (no title, graphs, stats or sections)

    // Value colouring: Normal = the row's base colour, Warm/Hot = threshold colours, Muted = n/a or --.
    enum class Tone : uint8_t { Normal, Warm, Hot, Muted };

    struct Cell
    {
        std::wstring value, unit;   // e.g. "69" + "°C"; the unit is hidden when Muted
        Tone tone = Tone::Normal;
    };

    // One physical core in a section's core grid.
    struct CoreCell
    {
        std::wstring label;         // "C1", "P1", "E3"
        std::wstring load, clock;   // "37%", "4.61" (either may be empty when not shown)
        float loadFraction = 0.f;   // 0..1, for the thin load bar
        Tone tone = Tone::Normal;
    };

    struct Section
    {
        std::wstring label;         // "GPU", "CPU", "RAM"
        std::wstring caption;       // small line under the label, e.g. "1 · AMD Radeon RX 9060 XT" (multi-GPU)
        uint32_t rgb = 0xFFFFFF;    // label and graph colour
        bool headline = false;      // big value next to the label (load %)
        Cell head;
        std::vector<float> history; // headline graph, 0..100
        std::vector<Cell> cells;
        std::vector<CoreCell> cores;    // per-core grid under the section (CPU); not shown in the bar
        bool coreLoad = false, coreClock = false;
    };

    struct StatRow
    {
        std::wstring label, value;  // "AVG", "82"
        float bar = 0.f;            // 0..1 of the bar width, 0 = no bar
        bool primary = false;       // white (AVG) instead of khaki
    };

    struct OsdView
    {
        bool background = true;
        double timeSec = 0.0;       // animation clock (Matrix glyph rain)
        int fpsRgb = -1;            // colour of the FPS number; -1 = the theme's value colour
        float opacity = 0.9f;       // background alpha
        float scale = 1.f;          // whole panel incl. text, 0.5..2
        // Bar style (SteamOS-like): one full-width row along the top or bottom of the monitor.
        bool bar = false;
        float barWidthDip = 0.f;    // layout width at scale 1; the host makes width x scale = monitor width
        bool graphs = true;

        std::wstring title;         // empty = no title row

        bool showFps = true, showFrametime = true;
        std::wstring fps, frametime;        // "85", "11.7" or "--"
        std::wstring frameNote;             // shown instead of the graphs when there are no frames
        std::vector<float> fpsHistory;      // oldest first
        std::vector<float> frametimeHistory;
        std::vector<StatRow> stats;

        std::vector<Section> sections;
    };

    class Renderer
    {
    public:
        // Uses the window's current DPI.
        bool Create(HWND hwnd, bool warp, std::wstring& error);
        // Colours, fonts and panel decoration; Classic dark until set. `theme` must outlive the
        // renderer (ui::GetTheme returns static themes).
        void SetTheme(const ui::Theme& theme);
        // No window: for RenderToPng only (layout previews and tests).
        bool CreateOffscreen(float dpi, std::wstring& error);
        void Reset();
        bool Ok() const noexcept { return ctx_ != nullptr; }
        UINT Dpi() const noexcept { return static_cast<UINT>(dpi_); }
        SIZE PixelSize() const noexcept { return size_; }   // current surface size
        // Pixel size of the last measured view at 100 % Size (the host keeps its centre fixed).
        SIZE BaseSize() const noexcept { return baseSize_; }

        // Pixel size of the panel for v (at v.scale); BaseSize() is then its size at 100 %.
        SIZE Measure(const OsdView& v);

        // Draws v with its top-left at `offset` (pixels) in a transparent surface of `surfaceSize`
        // pixels, resizing the swap chain only when `surfaceSize` changes. Returns S_OK, or a device-lost
        // HRESULT: then Reset() + Create() again.
        HRESULT Draw(const OsdView& v, POINT offset, SIZE surfaceSize);

        // Draws v at its measured size over an opaque backdrop colour and saves it as a PNG.
        HRESULT RenderToPng(const OsdView& v, const wchar_t* path, uint32_t backdropRgb);

    private:
        bool CreateDrawing(bool warp, std::wstring& error);   // D3D device + D2D context + DirectWrite
        float Layout(const OsdView& v);     // draws unless measuring_; returns the height in DIPs (at scale 1)
        float LayoutBar(const OsdView& v);  // the bar style of Layout
        float TextWidth(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight);
        void Paint(const OsdView& v, POINT offset);   // between BeginDraw / EndDraw, after Measure
        IDWriteTextFormat* Format(float size, DWRITE_FONT_WEIGHT weight, bool wrap = false);
        void Text(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight, D2D1_RECT_F r, uint32_t rgb,
                  DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false);
        void Title(const std::wstring& s, D2D1_RECT_F r, uint32_t rgb);
        void Graph(const std::vector<float>& v, float lo, float hi, D2D1_RECT_F r, uint32_t rgb, float width);
        void Fill(D2D1_RECT_F r, uint32_t rgb, float alpha = 1.f);
        void CellAt(const Cell& c, float x, float y, float w);
        uint32_t ToneColor(Tone t, uint32_t normal) const;
        const ui::Palette& P() const noexcept { return theme_->c; }

        float dpi_ = 96.f;
        float width_ = kWidthDip;           // layout width of the current view, DIPs at scale 1
        float heightDip_ = 0.f;             // its height, set by Measure
        SIZE size_{}, baseSize_{};
        bool measuring_ = false;
        bool shadow_ = false;               // text shadow when there is no (or a faint) background
        std::wstring family_;
        const ui::Theme* theme_ = &ui::GetTheme(ui::Style::Classic, true);
        ui::Style fontStyle_ = ui::Style::Classic;   // style the cached text formats were made for
        ui::Fonts uiFonts_;
        Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
        Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_;
        Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
        Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
        Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> ctx_;
        Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
        std::map<uint32_t, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    };
}
