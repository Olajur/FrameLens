# FrameLens — On-Screen Display (OSD) Plan (Milestone M3)

*Written 2026-10-20. Supersedes the overlay notes in ARCHITECTURE §1.4 where they differ.*

## 1. Goal and non-negotiables

Show live FrameLens metrics on top of running games:

- **No code injected into games, no hooks.** The OSD is a separate window drawn by Windows' compositor.
- **Negligible cost to the game:** no per-game-frame work, redraw only when values change, a small window instead of a full-screen transparent one.
- **Honest about where it can't appear.** If the OSD can't be shown over a game, FrameLens detects that and says why, and doesn't pretend.
- **Data-driven from the start.** M3 renders a layout model (JSON) so the M5 visual editor only edits data; the renderer doesn't change.

## 2. The key technical finding: Windows Z-bands

Windows sorts windows into **Z-bands** before ordinary z-order. Every normal application window — including `HWND_TOPMOST` windows — lives in the lowest band (`ZBID_DESKTOP`). Games running "fullscreen" under Fullscreen Optimizations (FSO, the Windows 10/11 default) sit **above** that band, which is why a normal topmost window can't cover them. Xbox Game Bar, the volume flyout and Intel PresentMon's overlay can, because they live in higher bands.

How PresentMon gets there (documented in its `README-CaptureApplication.md`):

- Its overlay executable has **`uiAccess="true"`** in its manifest. Windows only honours that for executables that are **Authenticode-signed** and run from a **secure location** (`Program Files`). Its window is then placed in the UIAccess band, above FSO games.
- It additionally calls `CreateWindowInBand`, an **undocumented** user32 export. Intel reports that uiAccess *alone* already kept their overlay on top, even over exclusive-fullscreen games.

**Our decision:** use the documented mechanism (uiAccess + `HWND_TOPMOST`) only. `CreateWindowInBand` stays out unless testing proves it necessary, and then only behind an "experimental" switch. This follows our rule of no undocumented APIs.

### The cost of uiAccess (why the OSD gets its own process)

A uiAccess process may bypass User Interface Privilege Isolation (UIPI). It can send input to elevated windows, set the foreground window, read keyboard and mouse at any integrity level, and attach to other input queues. **The security risk is real if that process is large or takes input.** So:

- The uiAccess process (`framelens-osd.exe`) **only draws.** It has no input handling, no hotkeys, no network, no plugins, no file parsing beyond one validated layout blob, and no shell or process launching.
- Everything else (engine, ETW, sensors, hotkeys, tray, config) stays in the normal, non-uiAccess `framelens-core.exe`.
- The OSD treats every byte it reads from the core as untrusted. It uses fixed-size structures, bounds-checks everything, and never follows pointers or offsets from shared memory without validation.

## 3. Support tiers (what the user gets)

| Tier | Requirements | OSD appears over |
|---|---|---|
| **1 — Basic** | portable/unsigned build, or uiAccess disabled | windowed and borderless-windowed games |
| **2 — Full** | installed to `Program Files`, signed `framelens-osd.exe` with uiAccess | + FSO "fullscreen" DX11/DX12/Vulkan games; likely exclusive fullscreen (Intel's observation — **to be verified**) |
| 3 — Fallback ideas (later, optional) | Xbox Game Bar widget | cases Tier 2 can't cover |

Never offered by default: present-hook injection (anti-cheat risk). A Vulkan layer is optional and off by default (out of M3 scope).

When the target game is in a mode the current tier can't cover, FrameLens shows a tray notification and a dashboard hint, e.g. *"OSD hidden: this game runs in exclusive fullscreen. Switch it to borderless, or install the signed build."* Detection uses the present mode that ETW already reports for every frame (`Hardware: Legacy Flip` = exclusive fullscreen).

## 4. Side effect to measure: independent flip vs composition

An FSO game normally runs in *independent flip*: DWM steps aside and the game flips straight to the display, giving the lowest latency and working VRR. Anything external drawn on top can push it back to *composed flip* (DWM composites again, adding up to a frame of latency, which may affect VRR) — unless the GPU and driver can put the overlay on a separate hardware plane (multiplane overlay → `Hardware Composed: Independent Flip`).

Mitigations and instrumentation:

- Keep the OSD window as **small as its content** (never a full-screen transparent window); this gives MPO the best chance.
- FrameLens already sees the game's present mode through ETW. The OSD can show it as an optional widget ("Present: Independent Flip / Composed Flip"), so users see the impact live.
- Per-profile option to hide the OSD in competitive games, plus a "show only while hotkey held" mode.
- The M3 exit test measures the game's present mode and frame times with the OSD on and off.

## 5. Process and data-flow architecture

```
framelens-core.exe (normal user, no uiAccess)            framelens-osd.exe (signed, uiAccess, draw-only)
┌───────────────────────────────────────────┐            ┌──────────────────────────────────────────┐
│ MonitorEngine (sensors, ETW frames)       │            │ wait: SnapshotReady event / render timer │
│ OsdPublisher ──► shared memory (seqlock) ─┼──────────► │ OsdReader (validate, copy)               │
│   - metrics snapshot (fixed POD)          │            │ LayoutModel (validated)                  │
│   - layout blob + generation              │            │ D2D/DirectWrite → DXGI composition chain │
│   - placement + visibility command        │            │ → DirectComposition visual → HWND        │
│ Hotkeys (RegisterHotKey), tray, config    │            └──────────────────────────────────────────┘
│ Game-window tracker (WinEvent, out-of-ctx)│      launched with ShellExecuteEx (CreateProcess can't start uiAccess apps)
│ OSD supervisor (launch, heartbeat, restart)│     watched via a heartbeat counter; restarted after a crash
└───────────────────────────────────────────┘
```

### 5.1 Shared memory protocol (one-way, core → OSD)

- A named file mapping `Local\FrameLens.Osd.<user-SID>`, with a DACL that grants only the current user.
- Header: `magic`, `version`, `size`, `seq` (seqlock: odd while writing), `layoutGeneration`, `commandGeneration`.
- `OsdMetrics`: a fixed-layout POD with no pointers or `std::string`. Each metric is `{state, value}` (same Ok / Unavailable / Unsupported semantics as `Reading`); short strings (GPU name, game name) are fixed `char[64]`, always null-terminated by the reader. It also holds a frame-time ring of the last N=256 samples plus write index.
- `OsdLayout`: the compiled layout (see §7) in a bounded binary form. The core parses JSON; the OSD only validates the compact blob. This keeps the JSON parser out of the privileged process.
- `OsdCommand`: visible / hidden, target monitor, anchor rectangle (screen coordinates), opacity, capture-exclusion flag.
- A named auto-reset event `SnapshotReady` is signalled by the core on each publish, so the OSD is event-driven.
- The OSD writes only a heartbeat counter and its last error code (a separate small mapping, also validated by the core).
- The reader loop is: read `seq`; if odd, retry; copy; re-read `seq`; if changed, retry, with at most 3 tries before keeping the last good frame. The seqlock logic goes in `src/core` and is unit-tested portably, including a writer/reader stress test.

### 5.2 Rendering pipeline (OSD process)

1. **Window:** `WS_POPUP`, ex-styles `WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP`. This is click-through, never takes focus, has no taskbar button, and has no GDI redirection surface. It is shown with `SW_SHOWNOACTIVATE`, and is Per-Monitor-DPI-aware V2.
2. **Device:** D3D11 (BGRA support) on the **adapter that drives the OSD's monitor** (avoids cross-adapter copies on laptops). If creation fails, fall back to WARP. A spike will measure whether WARP (CPU) is actually *preferable* at 2–4 Hz, because it takes zero GPU time from the game.
3. **Swap chain:** `CreateSwapChainForComposition`, `B8G8R8A8_UNORM`, premultiplied alpha, 2 buffers, `FLIP_SEQUENTIAL`, sized to the layout's bounding box × DPI scale.
4. **DirectComposition:** device → target(HWND, topmost) → one visual with the swap chain as content; `Commit` only on resize.
5. **Direct2D device context** rendering into the back buffer; **DirectWrite** text with:
   - text formats created once per style;
   - **tabular figures** (`tnum`), so digits don't jiggle as values change;
   - optional outline or shadow for readability on bright scenes.
6. **Graphs:** polyline from the frame-time ring (≤ 256 points), drawn with a cached stroke and brushes; auto-range smoothed to avoid jumpy scaling.
7. **Redraw policy:**
   - Text values refresh at 2–4 Hz (default 4 Hz, configurable; faster is unreadable anyway).
   - Graphs refresh at up to 20 Hz, only if a graph widget is visible.
   - Skip `Present` entirely if the formatted output is unchanged.
   - Nothing draws while hidden.
8. **Resilience:** on `DXGI_ERROR_DEVICE_REMOVED` / `D2DERR_RECREATE_TARGET` (driver reset or update), the device and swap chain are rebuilt within one tick. If the core disappears, the OSD hides and exits after 5 s.

### 5.3 Placement and visibility (core process)

- **Target window:** the game's `HWND` comes from ETW present events (`PresentEvent::Hwnd`, to be added to `FrameSample`). Its monitor and client rect are tracked with **out-of-context WinEvent hooks** (`EVENT_SYSTEM_FOREGROUND`, `EVENT_OBJECT_LOCATIONCHANGE`, `EVENT_SYSTEM_MINIMIZESTART/END`). These are event-driven, with no polling and no injection.
- **Anchor:** corner (TL/TR/BL/BR/top-center) plus offset in DIPs, relative to the game's client rect when windowed, or to its monitor when fullscreen. The OSD stays inside the monitor's bounds on DPI or resolution change.
- **Visibility rules:**
  - Default: show only while the target game is the foreground window; hide on the desktop, alt-tab or minimize.
  - Also selectable: always; hotkey-held only; never.
  - All rules can be overridden per profile in M5.
- **Capture exclusion (option):** `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`, so the OSD can be kept out of screenshots, OBS display capture and screen sharing (Windows 10 2004+). Off by default, because many users want it in recordings.
- **Multi-monitor:** follows the game's monitor automatically; can be pinned to a chosen monitor.

### 5.4 Hotkeys (core process only)

`RegisterHotKey` (system-wide, no low-level keyboard hooks — those look like keyloggers to anti-cheat and AV):

- toggle OSD;
- cycle presets;
- reset frame statistics;
- "hold to show".

Conflicts are reported in the UI. The OSD process never handles input.

## 6. Performance budget (to verify in M3 exit tests)

| Item | Budget |
|---|---|
| OSD process working set | < 15 MB |
| OSD CPU at 4 Hz text + 20 Hz one graph | < 0.3 % of one core |
| OSD GPU time per update | < 0.1 ms, measured with ETW GPU tracking on FrameLens' own process |
| Game frame-time change, OSD on vs off | avg < 1 %, 1% low < 2 % (5 runs each, same scene) |
| Game present mode | report it; independent → composed flip transitions are documented per GPU |

## 7. Layout model (shared with the M5 editor)

JSON on disk, compiled to a compact validated blob for the OSD. Every element binds to a metric ID from a fixed catalogue:

- frames: `fps.now`, `fps.avg`, `fps.min`, `fps.max`, `fps.low1`, `fps.low01`, `frametime.now`, `frametime.avg`, `fps.displayed`, `framegen.share`, `present.mode`;
- `cpu.load`, `cpu.clock`, `cpu.core[n].load`;
- `ram.used`, `ram.pct`;
- `gpu[n].load`, `.temp`, `.hotspot`, `.memtemp`, `.clock`, `.memclock`, `.power`, `.fan`, `.vram.used`, `.vram.total`;
- `time.clock`, `game.name`.

```json
{
  "schema": 1, "name": "Compact",
  "anchor": { "corner": "top-left", "x": 12, "y": 12 },
  "style": { "font": "Segoe UI Variable", "size": 14, "color": "#FFFFFF", "opacity": 0.92,
             "panel": { "color": "#000000", "alpha": 0.55, "radius": 8, "padding": 8 } },
  "rows": [
    { "label": "FPS",  "metrics": ["fps.now", "fps.low1"], "format": "{0:.0f}  1% {1:.0f}",
      "color": { "thresholds": [[0, "#FF5A5A"], [30, "#FFC14D"], [60, "#7CFF7C"]] } },
    { "label": "GPU",  "metrics": ["gpu[0].load", "gpu[0].temp"], "format": "{0:.0f}%  {1:.0f}°C" },
    { "label": "CPU",  "metrics": ["cpu.load"], "format": "{0:.0f}%" },
    { "label": "RAM",  "metrics": ["ram.used"], "format": "{0:.1f} GB" },
    { "graph": "frametime.now", "height": 40, "range": "auto" }
  ],
  "unsupported": "hide-row"
}
```

`unsupported` is `"hide-row"` or `"show-na"`. A metric the hardware can't provide is never shown as a number.

M3 ships three presets — **Minimal**, **Compact**, **Performance Analysis**. **Advanced** and **Handheld** follow in M5 together with the editor.

## 8. Signing (required for Tier 2)

uiAccess only works for signed binaries. The options:

| Option | Cost | Notes |
|---|---|---|
| **SignPath Foundation** (free code signing for open-source projects) | free | requires a public OSS repo and their approval; signing happens in CI |
| Commercial / cloud code-signing certificate (e.g. Azure Trusted Signing, or an OV certificate from a CA) | paid, recurring | check eligibility for individuals in Hungary before choosing |
| **Self-signed development certificate** | free | only for testing on your own PC: the cert must be placed in the machine's Trusted Root store, which needs admin and weakens that PC's trust chain slightly. A setup/undo script will be provided, as for ETW access. |

Unsigned and portable builds automatically run as Tier 1 (the OSD manifest without uiAccess is used).

## 9. Work plan

| Step | Content | Exit check |
|---|---|---|
| **M3.0 Spike** (decision gate) | Minimal DComp window drawing static text; tested with and without uiAccess (self-signed dev cert, in Program Files) over a windowed, a borderless, an FSO-fullscreen and an exclusive-fullscreen game; record present mode with OSD on and off; compare D3D11 hardware vs WARP cost | Table of results in `docs/OSD_SPIKE.md`; go/no-go for Tier 2 and the WARP default |
| M3.1 IPC | Seqlock shared-memory publisher (core) + reader (OSD); fixed POD structs; DACL; heartbeat | Portable unit and stress tests; fuzzed reader never crashes on corrupted input |
| M3.2 Renderer | D2D/DirectWrite renderer, layout blob validator, three presets, thresholds, graphs, device-loss recovery | Renders all presets; survives a forced driver reset (`dxcap -forcetdr` or a GPU driver restart with Win+Ctrl+Shift+B) |
| M3.3 Core host | `framelens-core` tray app: engine, OSD supervisor (ShellExecuteEx, restart), hotkeys, visibility rules | OSD toggles by hotkey; crash of OSD → restarted within 2 s |
| M3.4 Placement | Game-window tracking, anchors, multi-monitor, DPI changes, capture exclusion | Correct position on 100 % / 150 % DPI and across two monitors |
| M3.5 Diagnostics | Present-mode widget, "can't show here" notifications, tier detection | Exclusive-fullscreen game produces the hint in Tier 1 |
| M3.6 Validation | Performance-budget tests (§6), anti-cheat titles (EAC, BattlEye, Vanguard, FACEIT) observed — no evasion, just documentation | `FEATURE_STATUS.md` updated only with what was actually verified |

## 10. Risks

1. **uiAccess and anti-cheat:** cheat "ESP" overlays use the same external-window technique, so some anti-cheats may flag or block topmost overlay windows. Mitigation: test popular titles, per-game "disable OSD" profiles, never evade.
2. **Composition side effects** (latency or VRR) on GPUs without enough MPO planes. Mitigation: measured, visible via the present-mode widget, user-controllable.
3. **Signing cost and availability** for Tier 2 releases. Mitigation: Tier 1 works without signing; apply to SignPath once the repository is public.
4. **Exclusive fullscreen over uiAccess** is Intel's observation, not a guarantee. The M3.0 spike decides what we promise.
