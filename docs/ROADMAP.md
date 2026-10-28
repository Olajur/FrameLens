# FrameLens — Implementation Roadmap

Each milestone has an exit criterion. A milestone is not "done" until its criterion has been checked on real hardware.

| # | Milestone | Scope | Exit criterion |
|---|---|---|---|
| **M1** | Research & architecture | Feasibility, stack, module design | `docs/ARCHITECTURE.md` ✅ |
| **M2** | Monitoring core *(current)* | CPU / RAM / GPU (PDH + DXGI + NVML), ETW frame source, `FrameStats`, engine threads, `framelens-cli` | ✅ code + unit tests. ⏳ On hardware: CLI values match Task Manager (CPU %, GPU %, VRAM) within ±2 %, `nvidia-smi` (temp/power/clocks), and PresentMon CSV (avg FPS ±0.5 %, 1% low ±2 %) on 3 games (DX11, DX12, Vulkan) |
| M2b | AMD + Intel vendor telemetry | `AdlxBackend`, `IgclBackend` behind `IGpuTelemetryBackend` | Radeon dGPU, Ryzen iGPU, Arc dGPU, Intel iGPU each show temp/clock/power where the driver supports it, `n/a` otherwise |
| M2c | Extra sensors | network / disk throughput (PDH), battery (`GetSystemPowerStatus`), Energy Meter power probe | values match Task Manager / Resource Monitor |
| **M3** | OSD — see [`OSD_PLAN.md`](OSD_PLAN.md) | M3.0 spike (z-band/uiAccess decision gate) → shared-memory IPC → draw-only `framelens-osd` (D2D/DirectComposition) → `framelens-core` tray host with hotkeys and visibility rules → placement/multi-monitor/DPI → diagnostics → validation | Tier 1 OSD over windowed/borderless; Tier 2 (signed) over FSO fullscreen DX11/DX12/Vulkan; game frame-time change OSD on vs off: avg < 1 %, 1% low < 2 % |
| **M4** | Main UI | WinUI 3 `framelens-ui` (dashboard, hardware page, settings, dark/light); shared-memory + pipe IPC | UI closed ⇒ zero extra processes; UI open ⇒ live data with < 100 ms latency |
| **M5** | Customization | JSON config, overlay layout model, visual editor (drag, resize, colors, fonts, graphs), presets (Minimal, Compact, Advanced, Performance Analysis, Handheld), per-game profiles + auto-switch, import/export, startup, portable mode | Layout round-trips through import/export; profile switches within 1 s of game focus |
| **M6** | Optimization & test matrix | Profile CPU/RAM/GPU overhead; NVIDIA / AMD / Intel / handheld matrix; TDR & game-crash resilience tests | Targets met: core < 1 % CPU, < 30 MB RAM; no crash across driver reset test |
| **M7** | Packaging | Signed installer (adds user to *Performance Log Users*), portable zip, auto-start option | Clean-VM install → working overlay without manual steps |

Later / optional: Xbox Game Bar widget for exclusive fullscreen; opt-in Vulkan layer; opt-in PawnIO CPU-sensor plugin; CSV benchmark logging.
