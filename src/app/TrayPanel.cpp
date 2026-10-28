// FrameLens — tray quick-settings panel.
// SPDX-License-Identifier: MIT
#include "TrayPanel.h"
#include "OverlaySettings.h"

namespace fl
{
    namespace
    {
        ui::FormWindow& Panel()
        {
            static ui::FormWindow form;   // reused: Create() again after it closed
            return form;
        }
        ULONGLONG g_closedAt = 0;         // when it last closed by losing activation
    }

    D2D1_SIZE_F BuildTrayPanel(ui::FormWindow& f, std::shared_ptr<TrayPanelModel> m)
    {
        using namespace ui;
        constexpr float W = 360.f, M = 16.f, LabelW = 82.f, RowH = 40.f;
        f.ClearControls();
        float y = 12.f;

        auto& title = f.Add<Label>();
        title.rect = D2D1::RectF(M, y, W - M, y + 26);
        title.role = Label::Role::Heading;
        title.size = 15.f;
        title.text = [] { return std::wstring(L"FrameLens"); };
        y += 26.f;
        auto& status = f.Add<Label>();
        status.rect = D2D1::RectF(M, y, W - M, y + 20);
        status.role = Label::Role::Dim;
        status.size = 12.f;
        status.text = m->status;
        y += 28.f;

        auto& shown = f.Add<Toggle>();
        shown.rect = D2D1::RectF(M, y, W - M, y + 30);
        shown.text = L"Show overlay";
        if (!m->overlayHotkey.empty()) shown.note = [k = m->overlayHotkey] { return k; };
        shown.get = m->overlayShown;
        shown.set = m->setOverlayShown;
        y += 38.f;

        auto choice = [&](const wchar_t* label, const TrayPanelModel::Choice& c) {
            auto& d = f.Add<Dropdown>();
            d.rect = D2D1::RectF(M, y, W - M, y + RowH - 4);
            d.label = label;
            d.labelWidth = LabelW;
            d.items = c.items;
            d.selected = c.selected;
            d.onSelect = c.select;
            d.display = [items = c.items, sel = c.selected] {
                const auto all = items ? items() : std::vector<std::wstring>{};
                const int i = sel ? sel() : -1;
                return i >= 0 && i < static_cast<int>(all.size()) ? all[static_cast<size_t>(i)] : std::wstring(L"—");
            };
            y += RowH;
        };
        choice(L"Follow", m->follow);

        // Presets: three buttons, the one applied last highlighted
        auto& presetLabel = f.Add<Label>();
        presetLabel.rect = D2D1::RectF(M, y, M + LabelW, y + 36);
        presetLabel.role = Label::Role::Dim;
        presetLabel.text = [] { return std::wstring(L"Preset"); };
        const float bw = (W - M - (M + LabelW) - 2 * 6.f) / kPresetCount;
        for (int i = 0; i < kPresetCount; ++i) {
            auto& b = f.Add<Button>();
            const float x = M + LabelW + static_cast<float>(i) * (bw + 6.f);
            b.rect = D2D1::RectF(x, y + 2, x + bw, y + 34);
            b.text = [i] { return std::wstring(PresetName(static_cast<Preset>(i))); };
            b.onClick = [m, i] { if (m->setPreset) m->setPreset(i); };
            b.highlighted = [m, i] { return m->preset && m->preset() == i; };
        }
        y += RowH;
        choice(L"Monitor", m->monitor);
        choice(L"Position", m->position);

        y += 6.f;
        auto& autostart = f.Add<Toggle>();
        autostart.rect = D2D1::RectF(M, y, W - M, y + 30);
        autostart.text = L"Start with Windows";
        autostart.get = m->autostart;
        autostart.set = m->setAutostart;
        y += 40.f;

        // Actions: the ones that leave the panel close it
        auto action = [&](float l, float r, const wchar_t* text, Button::Kind kind, std::function<void()> fn, bool closes) {
            auto& b = f.Add<Button>();
            b.rect = D2D1::RectF(l, y, r, y + 34);
            b.kind = kind;
            b.text = [t = std::wstring(text)] { return t; };
            b.onClick = [m, fn = std::move(fn), closes] {
                if (closes && m->close) m->close();
                if (fn) fn();
            };
        };
        const float half = (W - 2 * M - 8.f) / 2.f;
        action(M, M + half, L"Customize overlay…", Button::Kind::Normal, m->customize, true);
        action(W - M - half, W - M, L"Open FrameLens", Button::Kind::Normal, m->open, true);
        y += 42.f;
        action(M, M + half, L"Reset stats", Button::Kind::Normal, m->resetStats, false);
        action(W - M - half, W - M, L"Exit", Button::Kind::Quiet, m->exit, true);
        y += 34.f + M;

        f.onEscape = [m] { if (m->close) m->close(); };
        return D2D1::SizeF(W, y);
    }

    void ToggleTrayPanel(HINSTANCE inst, HWND owner, const ui::Theme& theme, std::shared_ptr<TrayPanelModel> model, POINT anchor)
    {
        ui::FormWindow& f = Panel();
        if (f.Hwnd()) { CloseTrayPanel(); return; }
        // Clicking the tray icon while the panel is open first deactivates (closes) it; that same
        // click must not open it again.
        if (GetTickCount64() - g_closedAt < 300) return;

        model->close = [] { if (Panel().Hwnd()) PostMessageW(Panel().Hwnd(), WM_CLOSE, 0, 0); };   // not from inside a click
        f.SetTheme(theme);
        const D2D1_SIZE_F size = BuildTrayPanel(f, model);
        ui::FormWindow::Options o;
        o.className = L"FrameLensTrayPanel";
        o.title = L"FrameLens";
        o.style = WS_POPUP | WS_BORDER;
        o.exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;   // no taskbar button; above the game
        o.owner = owner;
        o.widthDip = size.width;
        o.heightDip = size.height;
        if (!f.Create(inst, o)) return;
        f.message = [](UINT msg, WPARAM w, LPARAM, LRESULT&) {
            if (msg == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) {   // clicked elsewhere / back to the game
                g_closedAt = GetTickCount64();
                PostMessageW(Panel().Hwnd(), WM_CLOSE, 0, 0);
            }
            return false;
        };

        // Next to the tray icon, inside the work area (above / beside the taskbar)
        RECT wr{};
        GetWindowRect(f.Hwnd(), &wr);
        SIZE sz{ wr.right - wr.left, wr.bottom - wr.top };
        RECT out{};
        if (!CalculatePopupWindowPosition(&anchor, &sz, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_WORKAREA, nullptr, &out))
            out = { anchor.x - sz.cx, anchor.y - sz.cy, anchor.x, anchor.y };
        SetWindowPos(f.Hwnd(), HWND_TOPMOST, out.left, out.top, 0, 0, SWP_NOSIZE);
        ShowWindow(f.Hwnd(), SW_SHOW);
        SetForegroundWindow(f.Hwnd());
    }

    void CloseTrayPanel()
    {
        if (Panel().Hwnd()) DestroyWindow(Panel().Hwnd());
    }

    void SetTrayPanelTheme(const ui::Theme& theme)
    {
        if (Panel().Hwnd()) Panel().SetTheme(theme);
    }

    void RefreshTrayPanel()
    {
        Panel().Invalidate();
    }
}
