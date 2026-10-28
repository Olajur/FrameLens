# FrameLens — Feature Status

Legend: **✅ Done** (implemented and verified as stated) · **🟡 Implemented, not yet verified on hardware** · **⏳ Planned** · **⛔ Unsupported** (with reason)

*Updated 2026-10-20 (end of M2 coding). Nothing has been run on physical Windows hardware yet; that is the next step.*

## Verification performed so far

- `FrameStats` (FPS / frame time / min / max / 1% / 0.1% lows): 8 unit tests incl. edge cases, run under AddressSanitizer + UBSan — **pass**.
- GPU counter instance-name parser: unit tests — **pass**.
- All Windows sources (sensors, engine, CLI) cross-compile cleanly with clang/MinGW-w64 at `-Wall -Wextra`; the CLI imports only OS DLLs (kernel32, user32, advapi32, pdh, dxgi, ole32, UCRT).
- The PresentData integration (`EtwFrameSource.cpp` with `FL_WITH_PRESENTDATA=1`) requires MSVC and has **not been compiled yet**. A trial clang build showed the compatibility shims resolve; remaining errors were MinGW SDK header gaps.

## Frames

| Feature | Status | Notes |
|---|---|---|
| Current / average FPS, current / average frame time | 🟡 | ETW via PresentData; stats math ✅ unit-tested ; presents PresentData loses track of (frequent with FreeSync / VRR) still count, and late ones are put back in time order before intervals are taken (✅ unit-tested; 144 Hz vsync test window read as 144 FPS). Not yet re-checked in a game with FreeSync |
| Average / min / max FPS | 🟡 | rolling window of 0.25 s FPS samples (ring buffer, default 100 ≈ 25 s, adjustable 10–1000 in Customize overlay); loading-screen bursts drop out ✅ unit-tested |
| 1% / 0.1% low | 🟡 | per-frame, over the span the rolling window covers; percentile (default) or average-of-worst |
| Reset statistics | 🟡 | *Reset stats* button and tray menu |
| Frame-time history (graph data) | 🟡 | last 300 frames; CLI sparkline |
| Displayed FPS | 🟡 | |
| Frame-generation share | 🟡 | depends on driver-reported frame type |
| DX9 / DX10-12 / Vulkan / OpenGL coverage | 🟡 | single ETW path covers all |
| Auto-follow foreground game / fixed PID | 🟡 | |
| ETW privilege handling | 🟡 | clear message on access denied; installer step in M7 |

## Hardware

| Feature | Status | Notes |
|---|---|---|
| CPU total + per-core utilization | 🟡 | PDH `% Processor Utility` (Task Manager method) |
| CPU effective clock (total + per core) | 🟡 | `% Processor Performance × Processor Frequency` |
| Per physical core load / clock (overlay grid, CLI) | ✅ grouping verified on a Ryzen 7 5800X (16 threads → 8 cores); hybrid P/E labels unit-tested | threads grouped by `GetLogicalProcessorInformationEx`; load = average of the core's threads, clock = fastest thread; not in the bar layout |
| CPU temperature / package temperature | ⛔ | needs a kernel driver; WinRing0 is blocklisted — see ARCHITECTURE §1.3 |
| CPU package power | ⛔ → investigate | Energy Meter counters on some laptops (M2c) |
| RAM used / available / % | 🟡 | `GlobalMemoryStatusEx` |
| GPU discovery, multi-GPU | 🟡 | DXGI, de-duplicated by LUID; indirect-display (virtual monitor) adapters skipped via the kernel adapter type ✅ verified (RX 9060 XT + 4 virtual displays → 1 GPU) |
| Installer GPU detection | 🟡 | wizard page lists the PCI graphics cards and whether their driver's sensor library is present; the app re-detects at every start |
| GPU utilization (all vendors) | 🟡 | PDH busiest engine; NVML on NVIDIA |
| Dedicated / shared VRAM used (all vendors) | 🟡 | PDH |
| VRAM capacity | 🟡 | DXGI |
| NVIDIA temp, core/mem clock, power, fan, memory-controller load | 🟡 | NVML, dynamically loaded |
| NVIDIA hotspot / memory temperature | ⛔ | not in public NVML for GeForce; undocumented APIs not used |
| AMD temp, hotspot, mem temp, core/mem clock, power, fan RPM/%, activity | ✅ (idle) | ADL PMLog (atiadlxx.dll, shared-memory session, fallback to the deprecated query) — read on an RX 9060 XT at idle; under load not yet checked. ADLX not used: its SDK license is incompatible with MIT open source |
| Intel Arc / iGPU temp, clocks, power, fan | 🟡 | IGCL power telemetry (ControlLib.dll, 64-bit only), bound by LUID; power and load from energy/activity counters. Not verified (no Intel GPU available) |
| Network, disk throughput, disk temperature, battery | ⏳ M2c | disk temperature may require admin (SMART) |

## Overlay, UI, customization, packaging

| Feature | Status |
|---|---|
| External click-through overlay, Tier 1 (`framelens.exe`: FPS, frame time, AVG/MAX/MIN, graphs, GPU/CPU rows) | 🟡 renders live data on an RX 9060 XT desktop (60 Hz DXGI test target); not yet checked over a real game |
| Overlay over FSO fullscreen (Tier 2, uiAccess) | ⏳ M3 |
| Exclusive-fullscreen detection + guidance | ⏳ M3 |
| Taskbar window + tray icon; right-click opens a quick-settings panel (overlay on/off, Follow, Preset, Monitor, Position, autostart, actions) that stays open until it loses focus | 🟡 panel rendered in every style with `framelens-osd-preview`; not yet used interactively |
| Styles: Classic, Sci-Fi 2001, Matrix, Minimal 2026 — each dark and light (or following Windows), for the overlay and the app windows; overlay style can differ | 🟡 contrast ✅ unit-tested (WCAG, all 8 palettes); every style rendered with `framelens-osd-preview` (overlay panel + bar, main window, Customize); not yet used interactively |
| High-contrast values (Normal / High / High when Windows uses it): solid panel, pure white or black outlined values, saturated colours ≥ 7:1 (WCAG AAA) | 🟡 contrast ✅ unit-tested for every style, dark and light; rendered with `framelens-osd-preview` |
| Own colours for values, FPS, frame time, GPU, CPU, RAM (Windows colour picker, "hard to read" warning below 3:1) | 🟡 rendered offscreen; picker not yet used interactively |
| Custom-drawn app windows (Direct2D toolkit: cards, buttons, toggles, sliders, dropdown lists, keyboard focus, themed title bar) | 🟡 rendered offscreen; mouse/keyboard behaviour not yet tried on a real desktop. No screen-reader support yet (custom controls have no UI Automation) |
| Overlay position: top/bottom left/middle/right, left/right middle, full-width bar along the top or bottom (SteamOS style); Size scales around the position's anchor so the panel keeps its distance from its edges | 🟡 placement ✅ unit-tested (every position, two monitors, every Size; window never moves when resizing); bar layout checked with `framelens-osd-preview`; not yet seen over a game |
| Multi-GPU overlay: one section per GPU (numbered + named when 2+), per-GPU load graph, show/hide per GPU in Customize (remembered by PCI ids) | 🟡 layout checked with `framelens-osd-preview` (1–3 GPUs, 150 %); not run on a real multi-GPU PC |
| Global hotkeys: overlay on/off, next position, next preset (default Alt+1 / Alt+2 / Alt+3), rebindable in Customize overlay (record the keys; Esc cancels, Backspace clears; "used by another app" when Windows refuses one) | 🟡 Hotkeys card rendered with `framelens-osd-preview`; recording and the hotkeys themselves not yet used interactively |
| WinUI 3 dashboard / settings / themes | ⏳ M4 |
| Visual overlay editor, presets, profiles, import/export | ⏳ M5 |
| Installer, portable build | ⏳ M7 |
| Overlay in true exclusive fullscreen without injection | ⛔ (OS limitation) → Game Bar widget investigated later |
