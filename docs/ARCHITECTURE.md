# FrameLens — Feasibility, Architecture and Technology Stack

*Phase 1 deliverable. Status as of 2026-10-20.*

This document records what is technically possible, what is not, and the decisions that follow. Every "unsupported" item below is a deliberate decision with a reason, not an omission.

---

## 1. Feasibility findings

### 1.1 FPS and frame-time measurement — **feasible, no injection required**

Windows exposes frame presentation through **Event Tracing for Windows (ETW)**: the DXGI, D3D9, DxgKrnl, Win32k and DWM providers emit events for every Present call and every flip/composition. This is a passive OS facility: nothing is loaded into the game process, which is why it is compatible with anti-cheat systems.

Because every graphics API on Windows ultimately presents through the WDDM kernel (DxgKrnl), ETW covers **DirectX 9/10/11/12, Vulkan and OpenGL** with one mechanism.

Turning raw events into correct frames is the hard part: one Present may be composed by DWM, flipped independently, promoted to a hardware overlay plane (MPO), dropped, or shown several times. Intel's **PresentData** library (part of PresentMon, **MIT license**) is the reference implementation of that state machine. It is maintained actively (v2.6.0, Sept 2026, added OS DDI flip events and per-flip completed/dropped reporting).

**Decision:** vendor PresentData (only the `PresentData/` directory) and replace its heavy utility dependencies (boost, cereal, CLI11 via vcpkg) with ~150 lines of compatibility shims in `third_party/presentmon/compat`. Runtime cost: one ETW realtime session plus one consumer thread.

What this gives us:

| Metric | Source | Status |
|---|---|---|
| App FPS, frame time (present-to-present) | DXGI / D3D9 / DxgKrnl Present_Start | implemented |
| Displayed FPS (frames that reached the screen) | flip / composition tracking | implemented |
| Frame-generation share | `FrameType` reported by drivers (AMD AFMF, Intel XeSS-FG); repeated frames | implemented, driver-dependent |
| NVIDIA DLSS Frame Generation attribution | PresentMon's NVIDIA flip provider | partial — needs validation |

**Constraint — privileges:** starting an ETW session with these providers requires **administrator rights or membership in the built-in "Performance Log Users" group** (documented requirement of PresentMon). Plan: the installer adds the user to that group once (single UAC prompt); the app then runs unelevated forever. Until the installer exists, run the CLI elevated.

**Constraint — anti-cheat:** some games historically interfered with ETW sessions; PresentMon 2.5 reworked session setup to cope. We never attempt to evade this — if a game blocks tracing, we show "no frame data" for it.

### 1.2 GPU telemetry — **feasible, vendor-specific for sensors**

| Data | NVIDIA | AMD | Intel | Mechanism |
|---|---|---|---|---|
| Utilization, per-engine load | ✅ | ✅ | ✅ | WDDM PDH counters (`GPU Engine`) — same data as Task Manager |
| Dedicated / shared VRAM used | ✅ | ✅ | ✅ | PDH `GPU Adapter Memory` |
| VRAM capacity | ✅ | ✅ | ✅ | DXGI adapter description |
| Temp, clocks, power, fan, memory-controller load | NVML ✅ | ADL PMLog ✅ | IGCL ✅ (unverified) | vendor C APIs shipped with the driver |
| Hotspot temperature | ❌ public NVML | ADL PMLog ✅ | ❌ | |
| Memory (VRAM) temperature | ❌ public NVML on GeForce | ADL PMLog ✅ | IGCL (varies) | |

- **NVML** (`nvml.dll`) is installed by every NVIDIA driver. We load it dynamically from System32 only (no DLL planting), and use read-only queries.
- **AMD:** FrameLens uses **ADL** (AMD Display Library, `atiadlxx.dll`; its headers are MIT) and its PMLog sensors: GPU usage, clocks, edge/hotspot/memory temperature, ASIC or total board power, fan RPM and %. The newer **ADLX** was evaluated and rejected: its SDK license (2026) forbids redistributing its headers in source form and requires an end-user license that bans copying and reverse engineering, which is incompatible with MIT open source.
- **Intel IGCL** binaries ship with the graphics driver; telemetry APIs are **64-bit only** and have known multi-GPU reporting issues (e.g. Arc + iGPU) that only Intel can fix.
- NVIDIA hotspot/memory-junction temperatures are only reachable through undocumented NVAPI calls. **We do not use undocumented APIs**, so these show "n/a" on NVIDIA.

### 1.3 CPU temperature and package power — **not available from user mode**

Windows has no documented user-mode API for CPU die/package temperature or package power. Tools that show them read model-specific registers (MSRs) or AMD SMU through a **kernel driver**.

- **WinRing0** — the driver used by Open/LibreHardwareMonitor, FanControl and others — is on Microsoft's **Vulnerable Driver Blocklist**; Defender quarantines it as `HackTool:Win32/Winring0`, and anti-cheat software flags it. **Shipping it is not acceptable.**
- **PawnIO** is a signed, scriptable replacement adopted by LibreHardwareMonitor, but it is a separate install with licensing implications (GPL concerns raised by LHM maintainers).
- ACPI thermal zones via WMI are *not* CPU package temperature (often static or a motherboard sensor) — showing them as "CPU temp" would violate the "no inaccurate data" rule.
- **Investigate (M2c):** the Windows *Energy Meter Interface* (`\Energy Meter(*)\Power` PDH counters) exposes RAPL-style CPU/SoC power on many modern laptops and handhelds without a driver. If present, it is a legitimate source for CPU package power.

**Decision:** v1 shows CPU temperature and package power as **unsupported**, with an explanation. A future, opt-in, out-of-process sensor plugin using PawnIO can be considered once its licensing is settled. Utilization and effective clock (Task Manager's method) are fully supported without a driver.

### 1.4 In-game overlay — **feasible without injection; full coverage needs a signed "uiAccess" OSD process**

> Revised 2026-10-20 after studying Intel PresentMon's overlay. Full design: [`OSD_PLAN.md`](OSD_PLAN.md).

Windows sorts windows into **Z-bands**. Normal application windows — even `HWND_TOPMOST` — live in the lowest band, and games running "fullscreen" under Fullscreen Optimizations sit above it. A plain topmost window therefore only covers **windowed and borderless** games. An executable with `uiAccess="true"`, Authenticode-signed and run from `Program Files`, is placed in a higher band. Intel reports that this keeps its overlay above FSO and even exclusive-fullscreen games.

| Approach | Works in | Anti-cheat risk | Cost |
|---|---|---|---|
| **A1. External topmost window** (DirectComposition + Direct2D, click-through, no-activate) | windowed, borderless | none | may force DWM composition while visible |
| **A2. Same window from a signed uiAccess process** | + FSO fullscreen; exclusive fullscreen per Intel's observation (to verify) | low, though some anti-cheats may dislike overlay windows | needs code signing + install to Program Files; uiAccess is a privileged capability → the OSD process must be draw-only |
| B. Present-hook injection (RTSS/Steam/Discord style) | everything | **high** | per-API code, crash risk inside the game |
| C. Vulkan implicit layer | Vulkan only | medium | moderate |
| D. Xbox Game Bar widget | fullscreen | none | requires Game Bar, heavier |

**Decision:** A1 as Tier 1 (portable/unsigned builds), A2 as Tier 2 (installed, signed builds), via the documented uiAccess mechanism only. The undocumented `CreateWindowInBand` is not used unless the M3.0 spike proves it necessary. The game's present mode from ETW is used to detect where the OSD can't appear, and to show the user whether the overlay changed the game from independent flip to composed flip. B is never on by default; C and D are later options.

---

## 2. Technology stack

| Layer | Choice | Why | Trade-off |
|---|---|---|---|
| Monitoring core | **C++20, Win32, no runtime dependencies** | ETW, PDH, DXGI, NVML/ADLX/IGCL are all native C APIs; deterministic memory; static CRT → single exe | more code than C#; mitigated by strict module boundaries and unit tests |
| Frame analysis | **Vendored PresentData** (MIT) + shims | reference-quality present tracking; avoids re-deriving a complex state machine | upstream syncs need a small diff review (`PATCHES.md`); MSVC-only build |
| Overlay (OSD) | **Separate draw-only `framelens-osd.exe`**: Direct2D + DirectWrite on DirectComposition, fed by shared memory | premultiplied alpha, crisp text, redraw only when values change (≤ 4 Hz text), isolates the privileged uiAccess capability | extra small process (< 15 MB target) |
| Main UI (settings, dashboard, editor) | **WinUI 3 in a separate process**, launched on demand | native Fluent/Windows 11 look, dark/light themes, good tooling | Windows App SDK runtime and tens of MB RAM — acceptable because the process is **closed while gaming** |
| IPC core ↔ UI | shared-memory snapshot (seqlock) + named pipe for commands | zero-copy reads, no polling of the core by the UI | small amount of custom code |
| Config | JSON files in `%APPDATA%\FrameLens` (or next to the exe in portable mode) | human-readable, import/export of layouts | one small header-only parser (nlohmann/json, MIT) |

**Rejected:** Electron/Chromium (explicitly excluded; 150 MB+ RAM), WPF/C# for the core (GC pauses and a .NET runtime in the always-on process), Dear ImGui for the main UI (light, but not a Windows 11-native look; still a candidate for the overlay editor canvas).

**Why separate processes:** the always-running `framelens-core` (engine + tray + hotkeys) and the draw-only `framelens-osd` stay tiny; the rich UI costs nothing while you play because it isn't running. Performance targets to verify in Phase 6: core < 1 % of one CPU core at 1 s sensor interval, < 30 MB working set, overlay redraw ≤ 10 Hz.

---

## 3. Module architecture

```
                      ┌──────────────────────── framelens-core (C++) ────────────────────────┐
 ETW (kernel) ──────► │ frames/EtwFrameSource ──► engine/MonitorEngine ──► snapshot publish  │──► shared memory ──► framelens-ui (WinUI 3)
                      │   (PresentData)              │  FrameStats (core)        │           │                       dashboard / editor /
 PDH, DXGI ─────────► │ sensors/Cpu, Memory, GpuPdh  │  target selection         ▼           │◄── named pipe ────── profiles / settings
 nvml.dll / ADLX /    │ sensors/NvmlBackend, ...  ───┘                     overlay/ (D2D)    │
 IGCL (driver DLLs) ─►│                                                   config/, profiles/ │
                      └───────────────────────────────────────────────────────────────────────┘
```

| Module | Path | Responsibility | Thread |
|---|---|---|---|
| Core types / stats | `src/core` | `Reading` (Ok / Unavailable / Unsupported), snapshots, `FrameStats` | none (pure) |
| Hardware sensors | `src/sensors` | CPU/RAM via PDH & Win32; GPU discovery via DXGI; PDH GPU counters; `IGpuTelemetryBackend` with NVML (ADLX, IGCL next) | sensor thread |
| Frame source | `src/frames` | `IFrameSource`; ETW implementation over PresentData | ETW thread + frame thread |
| Engine | `src/engine` | owns threads, target-process selection, stats, publishes snapshots | sensor + frame threads |
| OSD | `src/osd` *(M3)* | shared-memory publisher (core) / reader + D2D renderer (`framelens-osd.exe`) | separate process |
| Config / profiles | `src/config` *(M5)* | JSON settings, layouts, per-game profiles, auto-switch | core main thread |
| Hosts | `src/app` | `framelens-cli` (now), `framelens-core` tray app (M3), `framelens-ui` (M4) | main thread |

**Principles enforced in code**

- **No fabricated values.** Every metric is a `Reading`; a backend that cannot read a value must return `NotSupported` (permanent) or `Failed` (transient). The CLI prints `n/a` / `--`.
- **Collection never blocks consumers.** Sensors and frames run on their own threads; readers copy the last published snapshot under a short lock.
- **Self-throttling.** Sensor interval floor 100 ms; ETW drain every 50 ms; stats every 250 ms. The sensor thread runs below normal priority.
- **Resilience.** NVML calls that return `NOT_SUPPORTED` are disabled permanently; `GPU_IS_LOST` (driver reset/TDR) becomes a transient `Failed` and recovers on the next sample. A crashed previous run's ETW session is detected and replaced.
- **Read-only.** No code path writes clocks, voltages, power limits or fan curves.

### Metric definitions (they differ between tools, so we state ours)

- **App FPS / frame time**: interval between consecutive Present calls on the target's busiest swapchain (what RTSS/FPS Monitor show as FPS).
- **Displayed FPS**: interval between frames actually shown on screen (differs with frame generation, VRR, dropped frames).
- **Average FPS**: frames ÷ total time (time-weighted), *not* the mean of per-frame FPS.
- **Min / Max FPS**: lowest / highest of 1-second averages (per-frame min/max is dominated by noise).
- **1% / 0.1% low**: default = FPS of the 99th / 99.9th percentile frame time (PresentMon convention). Alternative "average of slowest 1% / 0.1%" is selectable (`--low avg`). Requires ≥ 100 / ≥ 1000 frames; shown as `--` until then.
- **GPU utilization**: vendor metric when available (NVML), else busiest WDDM engine (Task Manager method).

### Target-process selection

Auto mode follows the **foreground window's process, once that process is actually presenting frames** (focusing a console, the desktop or a launcher keeps the last game). A fixed PID can be forced. If a process owns several swapchains, the busiest one is tracked (with hysteresis).

---

## 4. Main engineering risks

1. **Fullscreen overlays** without injection — need a signed uiAccess OSD (Tier 2); unsigned builds cover windowed/borderless only. Detection + guidance where the OSD cannot appear.
2. **CPU temperature/power** — no clean driver; shown as unsupported until a safe source is available.
3. **Vendor API variance** (IGCL multi-GPU bugs, ADLX support differs per GPU generation) — every metric is capability-checked at runtime.
4. **ETW privilege** — one-time group membership via installer.
5. **Hardware coverage for testing** — a test matrix (NVIDIA/AMD/Intel dGPU, AMD/Intel iGPU, a handheld) is required in Phase 6; nothing is labelled "verified" without it.
