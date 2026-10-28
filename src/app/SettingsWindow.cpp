// FrameLens — "Customize overlay" window.
// SPDX-License-Identifier: MIT
#include "SettingsWindow.h"
#include <algorithm>

namespace fl
{
    namespace
    {
        ui::FormWindow& Form()
        {
            static ui::FormWindow form;   // reused: Create() again after the window was closed
            return form;
        }
    }

    D2D1_SIZE_F BuildSettingsForm(ui::FormWindow& f, std::shared_ptr<SettingsModel> m)
    {
        using namespace ui;
        constexpr float M = 20.f, ColW = 296.f, Gap = 14.f, RowT = 30.f, RowF = 40.f, CardTop = 38.f, CardPad = 10.f;
        constexpr float W = M * 2 + ColW * 4 + Gap * 3;
        const float colX[4] = { M, M + ColW + Gap, M + (ColW + Gap) * 2, M + (ColW + Gap) * 3 };
        float colY[4] = { 72.f, 72.f, 72.f, 72.f };
        OverlaySettings& s = *m->settings;
        f.ClearControls();

        auto changed = [m] { if (m->changed) m->changed(); };

        // Presets
        auto& presets = f.Add<Label>();
        presets.rect = D2D1::RectF(M, 18, M + 76, 54);
        presets.role = Label::Role::Dim;
        presets.text = [] { return std::wstring(L"Presets"); };
        const std::pair<const wchar_t*, Preset> kPresets[] = { { L"Only FPS", Preset::OnlyFps }, { L"Basic", Preset::Basic }, { L"Advanced", Preset::Advanced } };
        float px = M + 80;
        for (const auto& [name, preset] : kPresets) {
            auto& b = f.Add<Button>();
            b.rect = D2D1::RectF(px, 18, px + 112, 54);
            b.text = [n = std::wstring(name)] { return n; };
            b.onClick = [&s, changed, p = preset] { ApplyPreset(s, p); changed(); };
            px += 120;
        }

        // A card whose height follows its rows: rows are added below its title, then the card is sized.
        struct Column { int i; float top; Card* card; };
        auto begin = [&](int col, const wchar_t* title) {
            auto& c = f.Add<Card>();
            c.title = title;
            const Column k{ col, colY[col], &c };
            colY[col] += CardTop;
            return k;
        };
        auto end = [&](const Column& k) {
            colY[k.i] += CardPad;
            k.card->rect = D2D1::RectF(colX[k.i], k.top, colX[k.i] + ColW, colY[k.i]);
            colY[k.i] += Gap;
        };
        auto rowRect = [&](int col, float h) {
            const D2D1_RECT_F r = D2D1::RectF(colX[col] + 14, colY[col], colX[col] + ColW - 14, colY[col] + h);
            colY[col] += h;
            return r;
        };
        auto toggle = [&](int col, const ToggleDef& t) {
            auto& tg = f.Add<Toggle>();
            tg.rect = rowRect(col, RowT);
            tg.text = t.label;
            tg.get = [&s, field = t.field] { return s.*field; };
            tg.set = [&s, field = t.field, changed](bool v) { s.*field = v; changed(); };
            if (m->notReadable && m->notReadable(t)) tg.note = [] { return std::wstring(L"n/a on this PC"); };
            return &tg;
        };
        auto toggles = [&](int col, ToggleGroup g, bool OverlaySettings::* after, std::function<void()> extra) {
            for (size_t i = 0; i < kToggleCount; ++i) {
                if (kToggles[i].group != g) continue;
                toggle(col, kToggles[i]);
                if (kToggles[i].field == after && extra) extra();
            }
        };
        auto dropdown = [&](int col, const wchar_t* label, std::function<std::vector<std::wstring>()> items, std::function<int()> sel,
                            std::function<void(int)> pick) {
            auto& d = f.Add<Dropdown>();
            d.rect = rowRect(col, RowF);
            d.label = label;
            d.labelWidth = 92.f;
            d.items = items;
            d.selected = sel;
            d.onSelect = std::move(pick);
            d.display = [items, sel] {
                const auto all = items();
                const int i = sel();
                return i >= 0 && i < static_cast<int>(all.size()) ? all[static_cast<size_t>(i)] : std::wstring();
            };
            return &d;
        };
        auto slider = [&](int col, const wchar_t* label, int lo, int hi, int step, int OverlaySettings::* field, const wchar_t* unit) {
            auto& sl = f.Add<Slider>();
            sl.rect = rowRect(col, RowF);
            sl.label = label;
            sl.labelWidth = 92.f;
            sl.min = lo;
            sl.max = hi;
            sl.step = step;
            sl.get = [&s, field] { return s.*field; };
            sl.set = [&s, field, changed](int v) { s.*field = v; changed(); };
            sl.format = [u = std::wstring(unit)](int v) { return std::to_wstring(v) + u; };
            return &sl;
        };

        // Column 1: appearance and placement
        {
            const auto k = begin(0, L"Appearance");
            dropdown(0, L"Style", [] { std::vector<std::wstring> v; for (int i = 0; i < kStyleCount; ++i) v.push_back(StyleName(static_cast<Style>(i))); return v; },
                     [&s] { return static_cast<int>(s.style); }, [&s, changed](int i) { s.style = static_cast<Style>(i); changed(); });
            dropdown(0, L"Mode", [] { return std::vector<std::wstring>{ ColorModeName(ColorMode::System), ColorModeName(ColorMode::Dark), ColorModeName(ColorMode::Light) }; },
                     [&s] { return static_cast<int>(s.colorMode); }, [&s, changed](int i) { s.colorMode = static_cast<ColorMode>(i); changed(); });
            dropdown(0, L"Overlay",
                     [] { std::vector<std::wstring> v{ L"Same as the app" }; for (int i = 0; i < kStyleCount; ++i) v.push_back(StyleName(static_cast<Style>(i))); return v; },
                     [&s] { return s.overlayStyle + 1; }, [&s, changed](int i) { s.overlayStyle = i - 1; changed(); });
            end(k);
        }
        {
            const auto k = begin(0, L"Placement");
            auto monitorItems = [m] {
                std::vector<std::wstring> v{ L"The game's monitor" };
                for (const auto& mon : m->monitors) v.push_back(mon.label);
                if (!m->settings->monitor.empty() &&
                    std::none_of(m->monitors.begin(), m->monitors.end(), [&](const MonitorEntry& e) { return e.device == m->settings->monitor; }))
                    v.push_back(m->settings->monitor + L"  ·  not connected");
                return v;
            };
            dropdown(0, L"Monitor", monitorItems,
                     [m] {
                         const auto& mons = m->monitors;
                         if (m->settings->monitor.empty()) return 0;
                         for (size_t i = 0; i < mons.size(); ++i) if (mons[i].device == m->settings->monitor) return static_cast<int>(i) + 1;
                         return static_cast<int>(mons.size()) + 1;   // the "not connected" entry
                     },
                     [m, changed](int i) {
                         const auto& mons = m->monitors;
                         if (i == 0) m->settings->monitor.clear();
                         else if (i <= static_cast<int>(mons.size())) m->settings->monitor = mons[static_cast<size_t>(i - 1)].device;
                         changed();
                     });
            dropdown(0, L"Position",
                     [] { std::vector<std::wstring> v; for (OverlayPosition p : kPositionOrder) v.push_back(PositionName(p)); return v; },
                     [&s] { return static_cast<int>(std::find(std::begin(kPositionOrder), std::end(kPositionOrder), s.position) - std::begin(kPositionOrder)); },
                     [&s, changed](int i) { s.position = kPositionOrder[i]; changed(); });
            slider(0, L"Size", 50, 200, 10, &OverlaySettings::scalePct, L" %");
            toggles(0, ToggleGroup::Look, &OverlaySettings::background, [&] {
                slider(0, L"Opacity", 10, 100, 5, &OverlaySettings::opacityPct, L" %")->enabledFn = [&s] { return s.background; };
            });
            end(k);
        }

        // Column 2: frames and memory
        {
            const auto k = begin(1, L"Frames");
            toggles(1, ToggleGroup::Frames, &OverlaySettings::graphs, [&] {
                slider(1, L"Samples", 10, 1000, 10, &OverlaySettings::windowSamples, L"");
                auto& note = f.Add<Label>();
                note.rect = rowRect(1, 22.f);
                note.role = Label::Role::Faint;
                note.size = 12.f;
                note.text = [&s] {
                    const int sec = (s.windowSamples + 2) / 4;   // samples are 0.25 s each
                    return L"AVG / MIN / MAX: last " +
                           (sec < 120 ? std::to_wstring(sec) + L" s" : std::to_wstring(sec / 60) + L" min " + std::to_wstring(sec % 60) + L" s") +
                           L" of play";
                };
            });
            end(k);
        }
        {
            const auto k = begin(1, L"Memory");
            toggles(1, ToggleGroup::Memory, nullptr, nullptr);
            end(k);
        }
        {
            const auto k = begin(1, L"Hotkeys");
            for (int i = 0; i < kHotkeyCount; ++i) {
                auto& hk = f.Add<HotkeyBox>();
                hk.rect = rowRect(1, RowF);
                hk.label = HotkeyActionName(static_cast<HotkeyAction>(i));
                hk.labelWidth = 110.f;
                hk.get = [&s, i] { return s.hotkeys[i]; };
                hk.set = [&s, i, changed](uint32_t key) {
                    if (key)   // one key combination, one action
                        for (uint32_t& other : s.hotkeys) if (other == key) other = 0;
                    s.hotkeys[i] = key;
                    changed();
                };
                hk.recording = [m](bool on) { if (m->pauseHotkeys) m->pauseHotkeys(on); };
                hk.note = [m, i] { return m->hotkeyNote ? m->hotkeyNote(i) : std::wstring(); };
            }
            auto& note = f.Add<Label>();
            note.rect = rowRect(1, 38.f);
            note.role = Label::Role::Faint;
            note.size = 11.5f;
            note.wrap = true;
            note.text = [] { return std::wstring(L"Work in games too. Click a field and press the keys; Esc cancels, Backspace clears."); };
            end(k);
        }

        // Column 3: GPU (which GPUs, then the metrics) and CPU
        {
            const auto k = begin(2, L"GPU");
            if (m->gpus.size() > 1) {
                for (const auto& g : m->gpus) {
                    auto& tg = f.Add<Toggle>();
                    tg.rect = rowRect(2, RowT);
                    tg.text = L"Show " + g.label;
                    tg.get = [&s, key = g.key] { return std::find(s.hiddenGpus.begin(), s.hiddenGpus.end(), key) == s.hiddenGpus.end(); };
                    tg.set = [&s, key = g.key, changed](bool show) {
                        auto& hidden = s.hiddenGpus;
                        hidden.erase(std::remove(hidden.begin(), hidden.end(), key), hidden.end());
                        if (!show) hidden.push_back(key);
                        changed();
                    };
                }
                colY[2] += 8.f;
            }
            toggles(2, ToggleGroup::Gpu, nullptr, nullptr);
            end(k);
        }
        {
            const auto k = begin(2, L"CPU");
            toggles(2, ToggleGroup::Cpu, nullptr, nullptr);
            end(k);
        }

        // Column 4: high contrast and own colours for the values
        {
            const auto k = begin(3, L"Colors");
            dropdown(3, L"Contrast",
                     [] { return std::vector<std::wstring>{ HighContrastName(HighContrastMode::Off), HighContrastName(HighContrastMode::On),
                                                            HighContrastName(HighContrastMode::FollowWindows) }; },
                     [&s] { return static_cast<int>(s.highContrast); },
                     [&s, changed](int i) { s.highContrast = static_cast<HighContrastMode>(i); changed(); });
            auto& hcNote = f.Add<Label>();
            hcNote.rect = rowRect(3, 34.f);
            hcNote.role = Label::Role::Faint;
            hcNote.size = 11.5f;
            hcNote.wrap = true;
            hcNote.text = [] { return std::wstring(L"Bright values with an outline on a solid panel, readable over any scene."); };
            colY[3] += 6.f;
            for (size_t i = 0; i < kColorCount; ++i) {
                const ColorDef& def = kColors[i];
                auto& cp = f.Add<ColorPicker>();
                cp.rect = rowRect(3, 34.f);
                cp.label = def.label;
                cp.get = [&s, field = def.field] { return s.*field; };
                cp.set = [&s, field = def.field, changed](int v) { s.*field = v; changed(); };
                cp.preview = [&s, field = def.field, m](int v) {
                    s.*field = v;
                    if (m->previewed) m->previewed();
                };
                cp.effective = [&s, m, &def] {
                    const int own = s.*def.field;
                    return own >= 0 ? static_cast<uint32_t>(own) : (m->styleColor ? m->styleColor(def) : 0u);
                };
                cp.note = [&s, m, &def] {
                    const int own = s.*def.field;
                    if (own < 0 || !m->overlayPanel) return std::wstring();
                    return ContrastRatio(static_cast<uint32_t>(own), m->overlayPanel()) < 3.0 ? std::wstring(L"low contrast") : std::wstring();
                };
            }
            auto& note = f.Add<Label>();
            note.rect = rowRect(3, 50.f);
            note.role = Label::Role::Faint;
            note.size = 11.5f;
            note.wrap = true;
            note.text = [] { return std::wstring(L"Click a swatch to pick a colour; ↺ returns to the style's colour; ⚠ = hard to read on the panel."); };
            end(k);
        }

        // Footer
        const float y = std::max({ colY[0], colY[1], colY[2], colY[3] }) + 4.f;
        auto& reset = f.Add<Button>();
        reset.rect = D2D1::RectF(M, y, M + 160, y + 36);
        reset.kind = Button::Kind::Quiet;
        reset.text = [] { return std::wstring(L"Reset to defaults"); };
        reset.onClick = [&s, changed] {
            const ui::Style style = s.style;              // keep the look; reset what is shown and where
            const ui::ColorMode mode = s.colorMode;
            uint32_t keys[kHotkeyCount];                  // and the hotkeys
            std::copy(std::begin(s.hotkeys), std::end(s.hotkeys), std::begin(keys));
            s = OverlaySettings{};
            s.style = style;
            s.colorMode = mode;
            std::copy(std::begin(keys), std::end(keys), std::begin(s.hotkeys));
            changed();
        };
        auto& close = f.Add<Button>();
        close.rect = D2D1::RectF(W - M - 120, y, W - M, y + 36);
        close.kind = Button::Kind::Primary;
        close.text = [] { return std::wstring(L"Close"); };
        close.onClick = [m] { if (m->close) m->close(); };
        f.onEscape = [m] { if (m->close) m->close(); };
        return D2D1::SizeF(W, y + 36 + M);
    }

    void ShowSettingsWindow(HINSTANCE inst, HWND owner, const ui::Theme& theme, OverlaySettings& settings,
                            std::vector<GpuChoice> gpus, std::function<bool(const ToggleDef&)> notReadable,
                            std::function<uint32_t(const ColorDef&)> styleColor, std::function<uint32_t()> overlayPanel,
                            std::function<void(bool)> pauseHotkeys, std::function<std::wstring(int)> hotkeyNote,
                            std::function<void()> onChange)
    {
        ui::FormWindow& f = Form();
        if (f.Hwnd()) {
            ShowWindow(f.Hwnd(), IsIconic(f.Hwnd()) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(f.Hwnd());
            return;
        }
        auto model = std::make_shared<SettingsModel>();
        model->settings = &settings;
        model->gpus = std::move(gpus);
        model->monitors = ListMonitors();
        model->notReadable = std::move(notReadable);
        model->styleColor = std::move(styleColor);
        model->overlayPanel = std::move(overlayPanel);
        model->pauseHotkeys = std::move(pauseHotkeys);
        model->hotkeyNote = std::move(hotkeyNote);
        model->changed = [&settings, onChange] {
            SaveSettings(settings);
            if (onChange) onChange();
            Form().Invalidate();
        };
        model->previewed = [onChange] {   // the overlay shows the colour now; saving waits for OK
            if (onChange) onChange();
            Form().Invalidate();
        };
        model->close = [] { if (Form().Hwnd()) DestroyWindow(Form().Hwnd()); };
        f.onDestroy = [p = model->pauseHotkeys] { if (p) p(false); };   // closed while recording

        f.SetTheme(theme);
        const D2D1_SIZE_F size = BuildSettingsForm(f, model);
        ui::FormWindow::Options o;
        o.className = L"FrameLensSettings";
        o.title = L"Customize overlay";
        o.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        o.owner = owner;   // no taskbar button of its own; stays above FrameLens
        o.widthDip = size.width;
        o.heightDip = size.height;
        if (!f.Create(inst, o)) return;

        // Open next to the FrameLens window when there is room on its monitor, else centred on it.
        RECT ow{}, me{};
        GetWindowRect(owner, &ow);
        GetWindowRect(f.Hwnd(), &me);
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
        const int w = me.right - me.left, h = me.bottom - me.top;
        int x = ow.right + 8;
        if (x + w > mi.rcWork.right) x = ow.left - w - 8;
        if (x < mi.rcWork.left) x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
        const int y = std::clamp(static_cast<int>(ow.top), static_cast<int>(mi.rcWork.top),
                                 std::max(static_cast<int>(mi.rcWork.top), static_cast<int>(mi.rcWork.bottom) - h));
        SetWindowPos(f.Hwnd(), nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        ShowWindow(f.Hwnd(), SW_SHOW);
    }

    void SetSettingsWindowTheme(const ui::Theme& theme)
    {
        if (Form().Hwnd()) Form().SetTheme(theme);
    }

    void RefreshSettingsWindow()
    {
        Form().Invalidate();
    }
}
