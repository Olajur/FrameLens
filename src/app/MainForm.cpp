// FrameLens — main window contents.
// SPDX-License-Identifier: MIT
#include "MainForm.h"

namespace fl
{
    void BuildMainForm(ui::FormWindow& f, const MainModel& m)
    {
        using namespace ui;
        constexpr float W = kMainFormWidth, M = 16.f, In = 30.f, LabelW = 116.f;
        f.ClearControls();

        auto& header = f.Add<Header>();
        header.rect = D2D1::RectF(0, 0, W, 78);
        header.title = L"FrameLens";
        header.subtitle = m.subtitle;

        auto& mode = f.Add<Button>();   // quick dark / light switch
        mode.rect = D2D1::RectF(W - 58, 22, W - 22, 58);
        mode.kind = Button::Kind::Icon;
        mode.text = [m] { return std::wstring(m.isDark && m.isDark() ? L"" : L""); };   // sun / moon
        mode.onClick = m.toggleDarkLight;

        // Game card
        auto& game = f.Add<Card>();
        game.rect = D2D1::RectF(M, 92, W - M, 270);
        game.title = L"Game";
        auto& follow = f.Add<Dropdown>();
        follow.rect = D2D1::RectF(In, 124, W - In, 160);
        follow.label = L"Follow";
        follow.labelWidth = LabelW;
        follow.display = m.followDisplay;
        follow.items = m.followItems;
        follow.selected = m.followSelected;
        follow.onSelect = m.followSelect;
        auto row = [&](const wchar_t* label, std::function<std::wstring()> value, float top, float bottom) -> InfoRow& {
            auto& r = f.Add<InfoRow>();
            r.rect = D2D1::RectF(In, top, W - In, bottom);
            r.label = label;
            r.labelWidth = LabelW;
            r.value = std::move(value);
            return r;
        };
        row(L"Game", m.game, 166, 192);
        row(L"FPS", m.fps, 192, 218);
        row(L"Frame capture", m.capture, 218, 262).color = m.captureColor;

        // Overlay card
        auto& overlay = f.Add<Card>();
        overlay.rect = D2D1::RectF(M, 282, W - M, 380);
        overlay.title = L"Overlay";
        row(L"Status", m.overlay, 314, 340);
        auto& note = f.Add<Label>();
        note.rect = D2D1::RectF(In, 342, W - In, 376);
        note.role = Label::Role::Faint;
        note.size = 12.f;
        note.wrap = true;
        note.text = [] { return std::wstring(L"Basic mode: the overlay shows over windowed and borderless games. "
                                             L"Games in exclusive fullscreen draw over it."); };

        // Actions
        constexpr float Y = 394, H = 36;
        auto& customize = f.Add<Button>();
        customize.rect = D2D1::RectF(M, Y, M + 172, Y + H);
        customize.kind = Button::Kind::Primary;
        customize.text = [] { return std::wstring(L"Customize overlay…"); };
        customize.onClick = m.customize;
        auto& reset = f.Add<Button>();
        reset.rect = D2D1::RectF(M + 180, Y, M + 296, Y + H);
        reset.text = [] { return std::wstring(L"Reset stats"); };
        reset.onClick = m.resetStats;
        auto& exit = f.Add<Button>();
        exit.rect = D2D1::RectF(W - M - 86, Y, W - M, Y + H);
        exit.kind = Button::Kind::Quiet;
        exit.text = [] { return std::wstring(L"Exit"); };
        exit.onClick = m.exit;
        auto& toggle = f.Add<Button>();
        toggle.rect = D2D1::RectF(W - M - 86 - 8 - 124, Y, W - M - 94, Y + H);
        toggle.text = m.overlayButtonText;
        toggle.onClick = m.toggleOverlay;
    }
}
