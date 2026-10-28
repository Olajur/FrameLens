// FrameLens — M3.0 OSD spike.
//
// Purpose: answer, on real hardware, *where* an external (non-injected) overlay
// window can appear and *what it costs*. It is deliberately draw-only:
// no input handling, no hotkeys, no network, no file parsing.
//
// Two builds of this same source:
//   framelens-osd-spike.exe      manifest uiAccess="false"  -> Tier 1 test
//   framelens-osd-spike-uia.exe  manifest uiAccess="true"   -> Tier 2 test
//                                (must be signed + run from Program Files)
//
// Command line:
//   --hz <n>        redraw rate, 1..60 (default 4)
//   --warp          render with the WARP software rasterizer instead of the GPU
//   --x <dip> --y <dip>   offset from the top-left of the foreground window's monitor (default 24, 24)
//   --minutes <n>   exit automatically after n minutes (default: run until stopped)
//   --no-layered    diagnostic: drop WS_EX_LAYERED|WS_EX_TRANSPARENT (window becomes NOT click-through)
//   --stop          stop the running spike instance and exit
//
// Log: %LOCALAPPDATA%\FrameLens\osd-spike.log (startup facts + a line every 10 s)
// SPDX-License-Identifier: MIT
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // Windows 10: per-monitor DPI v2, WS_EX_NOREDIRECTIONBITMAP
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr wchar_t kStopEventName[] = L"Local\\FrameLens.OsdSpike.Stop";
    constexpr wchar_t kClassName[] = L"FrameLensOsdSpike";
    constexpr float kWidthDip = 380.f;
    constexpr float kHeightDip = 168.f;

    struct Options
    {
        int hz = 4;
        bool warp = false;
        bool layered = true;
        bool stop = false;
        int minutes = 0;
        float offX = 24.f, offY = 24.f;
    };

    // ---------------------------------------------------------------- logging
    FILE* gLog = nullptr;

    void OpenLog()
    {
        PWSTR base = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) return;
        std::wstring dir = std::wstring(base) + L"\\FrameLens";
        CoTaskMemFree(base);
        CreateDirectoryW(dir.c_str(), nullptr);
        gLog = _wfopen((dir + L"\\osd-spike.log").c_str(), L"a, ccs=UTF-8");
    }

    void Log(const wchar_t* fmt, ...)
    {
        if (!gLog) return;
        SYSTEMTIME st; GetLocalTime(&st);
        fwprintf(gLog, L"%04u-%02u-%02u %02u:%02u:%02u.%03u  ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap; va_start(ap, fmt); vfwprintf(gLog, fmt, ap); va_end(ap);
        fputwc(L'\n', gLog);
        fflush(gLog);
    }

    // ---------------------------------------------------------------- facts about this process
    bool HasUiAccess()
    {
        HANDLE tok = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
        DWORD v = 0, len = 0;
        const BOOL ok = GetTokenInformation(tok, TokenUIAccess, &v, sizeof v, &len);
        CloseHandle(tok);
        return ok && v != 0;
    }

    std::wstring ExeNameOf(DWORD pid)
    {
        std::wstring name = L"?";
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return name;
        wchar_t path[MAX_PATH]{}; DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &n)) {
            const wchar_t* f = wcsrchr(path, L'\\');
            name = f ? f + 1 : path;
        }
        CloseHandle(h);
        return name;
    }

    double NowMs()
    {
        static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        return t.QuadPart * 1000.0 / f.QuadPart;
    }

    double ProcessCpuMs()
    {
        FILETIME c, e, k, u;
        GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
        auto toMs = [](FILETIME f) { return (double(f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) / 10000.0; };
        return toMs(k) + toMs(u);
    }

    // ---------------------------------------------------------------- renderer
    class Renderer
    {
    public:
        bool Create(HWND hwnd, bool warp, std::wstring& deviceDesc)
        {
            hwnd_ = hwnd;
            const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            HRESULT hr = D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                           nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
            if (FAILED(hr) && !warp) {
                Log(L"hardware D3D11 device failed (0x%08lX), falling back to WARP", hr);
                hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
                warp = true;
            }
            if (FAILED(hr)) { Log(L"D3D11CreateDevice failed 0x%08lX", hr); return false; }

            ComPtr<IDXGIDevice> dxgiDev;
            d3d_.As(&dxgiDev);
            ComPtr<IDXGIAdapter> adapter;
            dxgiDev->GetAdapter(&adapter);
            DXGI_ADAPTER_DESC ad{};
            adapter->GetDesc(&ad);
            deviceDesc = std::wstring(warp ? L"WARP (CPU): " : L"GPU: ") + ad.Description;

            ComPtr<IDXGIFactory2> factory;
            if (FAILED(hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) { Log(L"CreateDXGIFactory2 0x%08lX", hr); return false; }

            dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
            const UINT w = static_cast<UINT>(std::ceil(kWidthDip * dpi_ / 96.f));
            const UINT h = static_cast<UINT>(std::ceil(kHeightDip * dpi_ / 96.f));

            DXGI_SWAP_CHAIN_DESC1 sd{};
            sd.Width = w; sd.Height = h;
            sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
            if (FAILED(hr = factory->CreateSwapChainForComposition(dxgiDev.Get(), &sd, nullptr, &swap_))) {
                Log(L"CreateSwapChainForComposition 0x%08lX", hr); return false;
            }

            if (FAILED(hr = DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&dcomp_)))) { Log(L"DCompositionCreateDevice 0x%08lX", hr); return false; }
            if (FAILED(hr = dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &target_))) { Log(L"CreateTargetForHwnd 0x%08lX", hr); return false; }
            if (FAILED(hr = dcomp_->CreateVisual(&visual_))) { Log(L"CreateVisual 0x%08lX", hr); return false; }
            visual_->SetContent(swap_.Get());
            target_->SetRoot(visual_.Get());
            if (FAILED(hr = dcomp_->Commit())) { Log(L"DComp Commit 0x%08lX", hr); return false; }

            D2D1_FACTORY_OPTIONS fo{};
            if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                              reinterpret_cast<void**>(d2dFactory_.GetAddressOf())))) { Log(L"D2D1CreateFactory 0x%08lX", hr); return false; }
            ComPtr<ID2D1Device> d2dDev;
            if (FAILED(hr = d2dFactory_->CreateDevice(dxgiDev.Get(), &d2dDev))) { Log(L"D2D CreateDevice 0x%08lX", hr); return false; }
            if (FAILED(hr = d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx_))) { Log(L"CreateDeviceContext 0x%08lX", hr); return false; }
            ctx_->SetDpi(dpi_, dpi_);

            if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) { Log(L"DWriteCreateFactory 0x%08lX", hr); return false; }
            dwrite_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, 13.f, L"en-us", &text_);
            dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, 15.f, L"en-us", &title_);
            ctx_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.62f), &panel_);
            ctx_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &white_);
            ctx_->CreateSolidColorBrush(D2D1::ColorF(0.49f, 1.f, 0.49f, 1), &green_);
            ctx_->CreateSolidColorBrush(D2D1::ColorF(1.f, 0.76f, 0.30f, 1), &amber_);
            return text_ && title_ && panel_ && white_;
        }

        // Returns S_OK, or a device-lost HRESULT the caller must handle by recreating everything.
        HRESULT Draw(const std::wstring& body, bool uia, double phase)
        {
            ComPtr<IDXGISurface> surface;
            HRESULT hr = swap_->GetBuffer(0, IID_PPV_ARGS(&surface));
            if (FAILED(hr)) return hr;
            const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_, dpi_);
            ComPtr<ID2D1Bitmap1> bmp;
            if (FAILED(hr = ctx_->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &bmp))) return hr;

            ctx_->SetTarget(bmp.Get());
            ctx_->BeginDraw();
            ctx_->Clear(D2D1::ColorF(0, 0, 0, 0));
            ctx_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, kWidthDip, kHeightDip), 10, 10), panel_.Get());

            const wchar_t* t = uia ? L"FrameLens OSD spike  •  Tier 2 (uiAccess ON)" : L"FrameLens OSD spike  •  Tier 1 (uiAccess OFF)";
            ctx_->DrawText(t, (UINT32)wcslen(t), title_.Get(), D2D1::RectF(12, 8, kWidthDip - 8, 30), uia ? green_.Get() : amber_.Get());
            ctx_->DrawText(body.c_str(), (UINT32)body.size(), text_.Get(), D2D1::RectF(12, 34, kWidthDip - 8, kHeightDip - 18), white_.Get());

            // Moving marker: proves the OSD is live (not a frozen frame) when seen over a game.
            const float x = 12.f + static_cast<float>((std::sin(phase) * 0.5 + 0.5) * (kWidthDip - 36.f));
            ctx_->FillRectangle(D2D1::RectF(x, kHeightDip - 14, x + 12, kHeightDip - 8), green_.Get());

            hr = ctx_->EndDraw();
            ctx_->SetTarget(nullptr);
            if (FAILED(hr)) return hr;
            return swap_->Present(0, 0);
        }

    private:
        HWND hwnd_ = nullptr;
        float dpi_ = 96.f;
        ComPtr<ID3D11Device> d3d_;
        ComPtr<IDXGISwapChain1> swap_;
        ComPtr<IDCompositionDevice> dcomp_;
        ComPtr<IDCompositionTarget> target_;
        ComPtr<IDCompositionVisual> visual_;
        ComPtr<ID2D1Factory1> d2dFactory_;
        ComPtr<ID2D1DeviceContext> ctx_;
        ComPtr<IDWriteFactory> dwrite_;
        ComPtr<IDWriteTextFormat> text_, title_;
        ComPtr<ID2D1SolidColorBrush> panel_, white_, green_, amber_;
    };

    bool IsDeviceLost(HRESULT hr)
    {
        return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == D2DERR_RECREATE_TARGET;
    }

    LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        switch (m) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // never take focus from the game
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    Options ParseArgs()
    {
        Options o;
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; ++i) {
            const wchar_t* a = argv[i];
            auto next = [&](int def) { return (i + 1 < argc) ? _wtoi(argv[++i]) : def; };
            if (!wcscmp(a, L"--hz")) { o.hz = next(4); if (o.hz < 1) o.hz = 1; if (o.hz > 60) o.hz = 60; }
            else if (!wcscmp(a, L"--warp")) o.warp = true;
            else if (!wcscmp(a, L"--no-layered")) o.layered = false;
            else if (!wcscmp(a, L"--stop")) o.stop = true;
            else if (!wcscmp(a, L"--minutes")) o.minutes = next(0);
            else if (!wcscmp(a, L"--x")) o.offX = static_cast<float>(next(24));
            else if (!wcscmp(a, L"--y")) o.offY = static_cast<float>(next(24));
        }
        if (argv) LocalFree(argv);
        return o;
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    const Options opt = ParseArgs();

    if (opt.stop) {
        HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStopEventName);
        if (!ev) return 1;   // nothing running
        SetEvent(ev);
        CloseHandle(ev);
        return 0;
    }

    HANDLE stopEv = CreateEventW(nullptr, TRUE, FALSE, kStopEventName);
    if (stopEv && GetLastError() == ERROR_ALREADY_EXISTS) return 3;   // one instance at a time

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OpenLog();
    const bool uia = HasUiAccess();
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    Log(L"=== start  exe=%ls  uiAccess=%ls  hz=%d  warp=%ls  layered=%ls", exe, uia ? L"ON" : L"OFF", opt.hz,
        opt.warp ? L"yes" : L"no", opt.layered ? L"yes" : L"no");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hCursor = nullptr;
    RegisterClassExW(&wc);

    DWORD ex = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP;
    if (opt.layered) ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT;   // click-through (same combination as Intel PresentMon's overlay)

    HWND hwnd = CreateWindowExW(ex, kClassName, L"FrameLens OSD spike", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, inst, nullptr);
    if (!hwnd) { Log(L"CreateWindowEx failed %lu", GetLastError()); return 2; }
    if (opt.layered) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);   // else a layered window may stay invisible

    // Size the window to the content at the window's DPI.
    const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
    const int wPx = static_cast<int>(std::ceil(kWidthDip * dpi / 96.f));
    const int hPx = static_cast<int>(std::ceil(kHeightDip * dpi / 96.f));

    Renderer r;
    std::wstring deviceDesc;
    if (!r.Create(hwnd, opt.warp, deviceDesc)) { Log(L"renderer creation failed"); return 2; }
    Log(L"device: %ls  dpi=%.0f  size=%dx%d px", deviceDesc.c_str(), dpi, wPx, hPx);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, wPx, hPx, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    const double periodMs = 1000.0 / opt.hz;
    const double startMs = NowMs();
    double nextTick = startMs, nextLog = startMs + 10000.0, lastPlace = 0;
    double renderSumMs = 0, renderMaxMs = 0;
    unsigned long long updates = 0, updatesSinceLog = 0, deviceResets = 0;
    double cpuAtLog = ProcessCpuMs(), wallAtLog = startMs;
    double lastCpuPct = 0; SIZE_T lastWs = 0;
    std::wstring lastFg;
    int curX = INT_MIN, curY = INT_MIN;

    for (;;) {
        const double now = NowMs();
        DWORD waitMs = (nextTick > now) ? static_cast<DWORD>(nextTick - now) : 0;
        const DWORD w = MsgWaitForMultipleObjects(1, &stopEv, FALSE, waitMs, QS_ALLINPUT);
        if (w == WAIT_OBJECT_0) { Log(L"stop requested"); break; }
        if (w == WAIT_OBJECT_0 + 1) {
            MSG msg; bool quit = false;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) quit = true;
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (quit) break;
            continue;
        }
        if (opt.minutes > 0 && now - startMs > opt.minutes * 60000.0) { Log(L"time limit reached"); break; }
        if (NowMs() < nextTick) continue;
        nextTick += periodMs;
        if (nextTick < NowMs()) nextTick = NowMs() + periodMs;   // don't try to catch up after a stall

        // Once a second: follow the foreground window's monitor and re-assert topmost.
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg) GetWindowThreadProcessId(fg, &fgPid);
        if (now - lastPlace >= 1000.0) {
            lastPlace = now;
            HMONITOR mon = MonitorFromWindow(fg && fg != hwnd ? fg : hwnd, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{};
            mi.cbSize = sizeof mi;
            GetMonitorInfoW(mon, &mi);
            UINT mdpiX = 96, mdpiY = 96;
            using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            static auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
                reinterpret_cast<void*>(GetProcAddress(LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "GetDpiForMonitor")));
            if (getDpiForMonitor) getDpiForMonitor(mon, 0 /*MDT_EFFECTIVE_DPI*/, &mdpiX, &mdpiY);
            const int x = mi.rcMonitor.left + static_cast<int>(opt.offX * mdpiX / 96.f);
            const int y = mi.rcMonitor.top + static_cast<int>(opt.offY * mdpiY / 96.f);
            const UINT moveFlags = (x == curX && y == curY) ? SWP_NOMOVE : 0;
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | moveFlags);
            curX = x; curY = y;

            std::wstring fgName = fgPid ? ExeNameOf(fgPid) : L"(none)";
            if (fgName != lastFg) {
                RECT wr{}; if (fg) GetWindowRect(fg, &wr);
                const bool coversMonitor = fg && wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
                                           wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
                Log(L"foreground: %ls  rect=(%ld,%ld)-(%ld,%ld)  covers-monitor=%ls", fgName.c_str(),
                    wr.left, wr.top, wr.right, wr.bottom, coversMonitor ? L"yes" : L"no");
                lastFg = fgName;
            }
        }

        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t body[640];
        swprintf(body, 640,
                 L"%ls\n"
                 L"Clock     %02u:%02u:%02u.%u   updates %llu\n"
                 L"Render    %.3f ms avg   %.3f ms max (CPU)\n"
                 L"Process   %.2f %% CPU   %.1f MB RAM   resets %llu\n"
                 L"On top of %ls",
                 deviceDesc.c_str(), st.wHour, st.wMinute, st.wSecond, st.wMilliseconds / 100, updates,
                 updates ? renderSumMs / updates : 0.0, renderMaxMs, lastCpuPct, lastWs / 1048576.0, deviceResets,
                 lastFg.c_str());

        const double t0 = NowMs();
        HRESULT hr = r.Draw(body, uia, (t0 - startMs) / 1000.0 * 2.0);
        const double dt = NowMs() - t0;

        if (IsDeviceLost(hr)) {
            ++deviceResets;
            Log(L"device lost (0x%08lX) - recreating", hr);
            r = Renderer();
            if (!r.Create(hwnd, opt.warp, deviceDesc)) { Log(L"recreate failed"); break; }
            continue;
        }
        if (FAILED(hr)) Log(L"draw failed 0x%08lX", hr);

        ++updates; ++updatesSinceLog;
        renderSumMs += dt;
        if (dt > renderMaxMs) renderMaxMs = dt;

        if (now >= nextLog) {
            const double cpu = ProcessCpuMs(), wall = NowMs();
            lastCpuPct = (cpu - cpuAtLog) / (wall - wallAtLog) * 100.0;   // % of ONE core
            PROCESS_MEMORY_COUNTERS pmc{};
            pmc.cb = sizeof pmc;
            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) lastWs = pmc.WorkingSetSize;
            Log(L"stats: updates=%llu  render avg=%.3f ms max=%.3f ms  cpu=%.3f%% of one core  ws=%.1f MB  resets=%llu  fg=%ls",
                updatesSinceLog, renderSumMs / updates, renderMaxMs, lastCpuPct, lastWs / 1048576.0, deviceResets, lastFg.c_str());
            cpuAtLog = cpu; wallAtLog = wall; updatesSinceLog = 0;
            nextLog = now + 10000.0;
        }
    }

    Log(L"=== exit  updates=%llu", updates);
    if (gLog) fclose(gLog);
    DestroyWindow(hwnd);
    if (stopEv) CloseHandle(stopEv);
    return 0;
}
