// FrameLens — overlay layout preview: renders sample overlay panels to PNG files, offscreen
// (no window, nothing shown on screen). For checking layouts such as multi-GPU setups on a PC that
// doesn't have that hardware.
//
//   framelens-osd-preview <output folder>
// SPDX-License-Identifier: MIT
#include "../../src/osd/OsdRenderer.h"
#include "../../src/app/MainForm.h"
#include "../../src/app/SettingsWindow.h"
#include "../../src/app/TrayPanel.h"
#include <objbase.h>
#include <cmath>
#include <cstdio>
#include <string>

using namespace fl::osd;

namespace
{
    std::vector<float> Wave(size_t n, float base, float amp, float freq)
    {
        std::vector<float> v(n);
        for (size_t i = 0; i < n; ++i) v[i] = base + amp * std::sin(static_cast<float>(i) * freq) + amp * 0.3f * std::sin(static_cast<float>(i) * freq * 3.7f);
        return v;
    }

    Section Gpu(const std::wstring& caption, const wchar_t* load, Tone loadTone, std::vector<Cell> cells, float histBase)
    {
        Section s;
        s.label = L"GPU";
        s.caption = caption;
        s.rgb = 0x4CD94C;
        s.headline = true;
        s.head = { load, L"%", loadTone };
        s.history = Wave(60, histBase, 8, 0.35f);
        s.cells = std::move(cells);
        return s;
    }

    OsdView Base()
    {
        OsdView v;
        v.title = L"AMD RADEON RX 9060 XT";
        v.fps = L"144";
        v.frametime = L"6.9";
        v.fpsHistory = Wave(100, 140, 6, 0.21f);
        v.frametimeHistory = Wave(150, 7, 1.2f, 0.9f);
        v.stats = { { L"AVG", L"141", 0.85f, true }, { L"MAX", L"152", 0.92f, false }, { L"MIN", L"118", 0.71f, false } };
        Section cpu;
        cpu.label = L"CPU";
        cpu.rgb = 0x8DB8FF;
        cpu.headline = true;
        cpu.head = { L"34", L"%", Tone::Normal };
        cpu.history = Wave(60, 34, 6, 0.5f);
        cpu.cells = { { L"4612", L"MHz", Tone::Normal } };
        Section ram;
        ram.label = L"RAM";
        ram.rgb = 0xD9A6FF;
        ram.cells = { { L"15.7", L"GB/32GB", Tone::Normal } };
        v.sections = { cpu, ram };
        return v;
    }

    const std::vector<Cell> kAmdCells = { { L"64", L"°C", Tone::Warm }, { L"2950", L"MHz", Tone::Normal }, { L"1281", L"RPM", Tone::Normal }, { L"9.4", L"GB", Tone::Normal } };
    const std::vector<Cell> kIntelCells = { { L"48", L"°C", Tone::Normal }, { L"1550", L"MHz", Tone::Normal }, { L"0.6", L"GB", Tone::Normal } };
    const std::vector<Cell> kNvidiaCells = { { L"71", L"°C", Tone::Warm }, { L"1875", L"MHz", Tone::Normal }, { L"7001", L"MHz", Tone::Normal },
                                             { L"148", L"W", Tone::Normal }, { L"55", L"% FAN", Tone::Normal }, { L"5.1", L"GB", Tone::Normal } };
}

int wmain(int argc, wchar_t** argv)
{
    const std::wstring dir = argc > 1 ? argv[1] : L".";
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;

    Renderer r;
    std::wstring error;
    if (!r.CreateOffscreen(96.f, error)) { std::fwprintf(stderr, L"renderer: %ls\n", error.c_str()); return 1; }

    struct Case { const wchar_t* file; OsdView view; };
    std::vector<Case> cases;

    OsdView one = Base();   // single GPU: no caption, the title names the card
    one.sections.insert(one.sections.begin(), Gpu(L"", L"98", Tone::Hot, kAmdCells, 90));
    cases.push_back({ L"1-gpu.png", one });

    OsdView two = Base();   // discrete + integrated
    two.sections.insert(two.sections.begin(), { Gpu(L"1 · AMD Radeon RX 9060 XT", L"98", Tone::Hot, kAmdCells, 90),
                                                Gpu(L"2 · Intel(R) UHD Graphics 770", L"6", Tone::Normal, kIntelCells, 6) });
    cases.push_back({ L"2-gpus.png", two });

    OsdView three = Base();   // three cards, every GPU metric on
    three.sections.insert(three.sections.begin(), { Gpu(L"1 · AMD Radeon RX 9060 XT", L"98", Tone::Hot, kAmdCells, 90),
                                                    Gpu(L"2 · NVIDIA GeForce RTX 4070 SUPER", L"74", Tone::Warm, kNvidiaCells, 70),
                                                    Gpu(L"3 · Intel(R) UHD Graphics 770", L"6", Tone::Normal, kIntelCells, 6) });
    cases.push_back({ L"3-gpus.png", three });

    OsdView big = two;   // Size 150 %, no background (text shadow)
    big.scale = 1.5f;
    big.background = false;
    cases.push_back({ L"2-gpus-150-nobg.png", big });

    // Bars (SteamOS style) for a 1920 px wide monitor: the host lays them out at width / Size.
    auto bar = [](OsdView v, float scale) {
        v.bar = true;
        v.scale = scale;
        v.barWidthDip = 1920.f / scale;
        return v;
    };
    cases.push_back({ L"bar-1-gpu.png", bar(one, 1.f) });
    cases.push_back({ L"bar-2-gpus.png", bar(two, 1.f) });
    cases.push_back({ L"bar-2-gpus-150-nobg.png", bar(big, 1.5f) });
    OsdView waiting = bar(one, 1.f);   // no game yet
    waiting.frameNote = L"Waiting for a game…";
    waiting.fps = waiting.frametime = L"--";
    waiting.frametimeHistory.clear();
    cases.push_back({ L"bar-waiting.png", waiting });

    // CPU core grid: 8 P-cores + 8 E-cores (hybrid), load only and load + clock.
    auto withCores = [](OsdView v, bool load, bool clock) {
        for (auto& sec : v.sections) {
            if (sec.label != L"CPU") continue;
            sec.coreLoad = load;
            sec.coreClock = clock;
            for (int i = 0; i < 16; ++i) {
                CoreCell c;
                const bool e = i >= 8;
                c.label = (e ? L"E" : L"P") + std::to_wstring(e ? i - 7 : i + 1);
                const int pct = (i * 37 + 11) % 100;
                if (load) {
                    c.load = std::to_wstring(pct) + L"%";
                    c.loadFraction = static_cast<float>(pct) / 100.f;
                    c.tone = pct >= 90 ? Tone::Hot : pct >= 70 ? Tone::Warm : Tone::Normal;
                }
                if (clock) c.clock = std::wstring(e ? L"3.80" : L"5.40") + (load ? L" GHz" : L"");
                sec.cores.push_back(c);
            }
        }
        return v;
    };
    cases.push_back({ L"cores-load.png", withCores(one, true, false) });
    cases.push_back({ L"cores-load-clock.png", withCores(one, true, true) });

    int failures = 0;
    auto render = [&](const std::wstring& file, const OsdView& view) {
        const std::wstring path = dir + L"\\" + file;
        const HRESULT hr = r.RenderToPng(view, path.c_str(), 0x5A6470);
        std::wprintf(L"%ls  %ls\n", SUCCEEDED(hr) ? L"ok  " : L"FAIL", path.c_str());
        if (FAILED(hr)) ++failures;
    };
    for (const auto& c : cases) render(c.file, c.view);

    // Every style in dark and light: the two-GPU panel and the bar.
    for (int s = 0; s < fl::ui::kStyleCount; ++s)
        for (bool dark : { true, false }) {
            const auto& theme = fl::ui::GetTheme(static_cast<fl::ui::Style>(s), dark);
            r.SetTheme(theme);
            auto themed = [&](OsdView v) {
                v.timeSec = 12.3;   // a fixed frame of the Matrix rain
                for (auto& sec : v.sections)
                    sec.rgb = sec.label == L"GPU" ? theme.c.gpu : sec.label == L"CPU" ? theme.c.cpu : theme.c.ram;
                return v;
            };
            std::wstring name = fl::ui::StyleName(theme.style);
            for (auto& ch : name) if (ch == L' ') ch = L'-';
            name += dark ? L"-dark" : L"-light";
            render(L"style-" + name + L".png", themed(two));
            render(L"style-" + name + L"-bar.png", themed(bar(two, 1.f)));

            // High-contrast values (the section colours come from the high-contrast palette).
            const fl::ui::Theme hc = fl::ui::HighContrast(theme);
            r.SetTheme(hc);
            OsdView hv = themed(two);
            for (auto& sec : hv.sections) sec.rgb = sec.label == L"GPU" ? hc.c.gpu : sec.label == L"CPU" ? hc.c.cpu : hc.c.ram;
            render(L"hc-" + name + L".png", hv);
        }

        // Own colours: yellow values, cyan FPS, orange frame time on Classic dark.
        fl::ui::Theme own = fl::ui::GetTheme(fl::ui::Style::Classic, true);
        own.c.panelText = 0xFFE14D;
        own.c.label = own.c.labelBar = 0xFF9F43;
        r.SetTheme(own);
        OsdView ov = two;
        ov.fpsRgb = 0x4DE8FF;
        render(L"own-colours.png", ov);
    // The app windows (FrameLens and Customize overlay) in every style, with sample data.
    fl::MainModel mm;
    mm.subtitle = L"FPS & hardware monitor  ·  v0.2.0";
    mm.followDisplay = [] { return std::wstring(L"Automatic — the game in focus"); };
    mm.followItems = [] { return std::vector<std::wstring>{ L"Automatic — the game in focus", L"Cyberpunk2077.exe  ·  141 FPS  (pid 4144)" }; };
    mm.followSelected = [] { return 0; };
    mm.game = [] { return std::wstring(L"Cyberpunk2077.exe  (pid 4144)  — automatic"); };
    mm.fps = [] { return std::wstring(L"141 FPS  ·  7.1 ms  ·  avg 138  ·  1% low 97"); };
    mm.capture = [] { return std::wstring(L"running"); };
    mm.overlay = [] { return std::wstring(L"shown — top left of the game's monitor"); };
    mm.overlayButtonText = [] { return std::wstring(L"Hide overlay"); };
    fl::OverlaySettings settings;
    auto sm = std::make_shared<fl::SettingsModel>();
    sm->settings = &settings;
    sm->gpus = { { L"1002-7590-86011EAE-0", L"1 · AMD Radeon RX 9060 XT" }, { L"8086-A780-00000000-0", L"2 · Intel(R) UHD Graphics 770" } };
    fl::MonitorEntry m1, m2;
    m1.device = L"\\\\.\\DISPLAY1"; m1.label = L"M27T20  ·  2560×1440  ·  main";
    m2.device = L"\\\\.\\DISPLAY2"; m2.label = L"Hanns.G HA195  ·  1366×768";
    sm->monitors = { m1, m2 };
    sm->notReadable = [](const fl::ToggleDef& t) { return t.field == &fl::OverlaySettings::cpuTemp || t.field == &fl::OverlaySettings::cpuPower; };
    sm->styleColor = [](const fl::ColorDef&) { return 0xFFFFFFu; };
    sm->overlayPanel = [] { return 0x000000u; };
    auto tm = std::make_shared<fl::TrayPanelModel>();
    tm->status = [] { return std::wstring(L"Cyberpunk2077.exe  ·  141 FPS"); };
    tm->overlayShown = [] { return true; };
    tm->overlayHotkey = L"Alt + 1";
    tm->follow = { mm.followItems, mm.followSelected, nullptr };
    tm->monitor.items = [] { return std::vector<std::wstring>{ L"The game's monitor", L"M27T20  ·  2560×1440  ·  main" }; };
    tm->monitor.selected = [] { return 0; };
    tm->position.items = [] { return std::vector<std::wstring>{ L"Top left" }; };
    tm->position.selected = [] { return 0; };
    tm->preset = [] { return 1; };
    tm->autostart = [] { return false; };
    settings.colorFps = 0x4DE8FF;      // one own colour, and one too dark for the black panel
    settings.colorRam = 0x202020;
    for (int s = 0; s < fl::ui::kStyleCount; ++s)
        for (bool dark : { true, false }) {
            const auto& theme = fl::ui::GetTheme(static_cast<fl::ui::Style>(s), dark);
            mm.isDark = [dark] { return dark; };
            std::wstring name = fl::ui::StyleName(theme.style);
            for (auto& ch : name) if (ch == L' ') ch = L'-';
            name += dark ? L"-dark" : L"-light";
            fl::ui::FormWindow main, custom, tray;
            tray.SetTheme(theme);
            const D2D1_SIZE_F ts = fl::BuildTrayPanel(tray, tm);
            main.SetTheme(theme);
            fl::BuildMainForm(main, mm);
            custom.SetTheme(theme);
            const D2D1_SIZE_F cs = fl::BuildSettingsForm(custom, sm);
            for (auto [form, file, w, h] : { std::tuple{ &main, L"app-main-", fl::kMainFormWidth, fl::kMainFormHeight },
                                             std::tuple{ &custom, L"app-customize-", cs.width, cs.height },
                                             std::tuple{ &tray, L"app-tray-", ts.width, ts.height } }) {
                const std::wstring path = dir + L"\\" + file + name + L".png";
                const HRESULT hr = form->RenderToPng(path.c_str(), w, h, 96.f, 12.3);
                std::wprintf(L"%ls  %ls\n", SUCCEEDED(hr) ? L"ok  " : L"FAIL", path.c_str());
                if (FAILED(hr)) ++failures;
            }
        }

    r.Reset();
    CoUninitialize();
    return failures ? 1 : 0;
}
