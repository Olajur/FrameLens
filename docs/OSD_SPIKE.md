# FrameLens — M3.0 OSD Spike: test procedure and results

The spike is a tiny, draw-only overlay window, built the way the real OSD will be (DirectComposition + Direct2D, click-through, never takes focus). It answers three questions before we build M3:

1. **Where does an external overlay appear?** Tier 1 (normal window) vs Tier 2 (signed uiAccess window), across windowed, borderless, fullscreen and exclusive-fullscreen games.
2. **Does it change how the game presents?** Independent flip vs composed flip; FPS on vs off.
3. **What does it cost?** CPU, RAM, render time; GPU vs WARP (software) rendering.

## Files

| File | What it is |
|---|---|
| `framelens-osd-spike.exe` | Tier 1 build (`uiAccess="false"`). Runs from anywhere. |
| `framelens-osd-spike-uia.exe` | Tier 2 build (`uiAccess="true"`). Only runs after `setup-osd-spike.bat` installs and signs it. Started from the zip folder, Windows refuses it with an error like *"A referral was returned from the server"*. That is expected. |
| `setup-osd-spike.bat` / `.ps1` | Tier 2 test install: throw-away 30-day certificate, private key deleted right after signing, copy to `C:\Program Files\FrameLens OSD Spike`. `uninstall` removes everything. |

The overlay shows its tier (amber = Tier 1, green = Tier 2), the render device, a live clock, render time, its own CPU/RAM, and the name of the window it is drawn over. A small green square slides back and forth so you can tell it is live. It writes a log to `%LOCALAPPDATA%\FrameLens\osd-spike.log` with a stats line every 10 s.

**Stop it:** run `framelens-osd-spike.exe --stop` (works for both builds), or end the task in Task Manager. Only one instance runs at a time.

**Options:**

| Option | Effect |
|---|---|
| `--hz 4` | redraws per second (default 4) |
| `--warp` | CPU rendering instead of GPU |
| `--x 24 --y 24` | offset from the top-left of the monitor the active window is on |
| `--minutes 10` | stop automatically after 10 minutes |
| `--no-layered` | diagnostic only — use if the window is invisible (makes it non-click-through) |

## Tools for present mode (optional but valuable)

Download the **PresentMon console** exe (`PresentMon-2.x.x-x64.exe`) from https://github.com/GameTechDev/PresentMon/releases. It uses the same ETW permission you already set up. While a game runs:

```bat
PresentMon-2.x.x-x64.exe --process_name game.exe
```

Its live console view shows the game's swap chain with a **PresentMode** column, e.g. `Hardware: Independent Flip`, `Hardware Composed: Independent Flip`, `Composed: Flip`, or `Hardware: Legacy Flip` (exclusive fullscreen). Note it with the overlay off, then on.

## Procedure (about 30–45 min)

**A. Tier 1** (no install):

1. Start `framelens-osd-spike.exe`. The amber panel appears top-left on the desktop.
2. For each game/mode in the table:
   - start the game (or switch its display mode in its settings);
   - note whether the panel is visible over the game, and whether the clock keeps moving;
   - optionally note PresentMode and FPS with the spike running vs stopped (`--stop`).
3. Stop the spike.

**B. Tier 2:**

1. Run `setup-osd-spike.bat` and approve UAC once. Check that it ends with `Signature : Valid`.
2. Start `C:\Program Files\FrameLens OSD Spike\framelens-osd-spike-uia.exe`. The panel must say **Tier 2 (uiAccess ON)** in green.
3. Repeat the same games and modes.

**C. Cost:** let one run go 2 minutes in a game, then again with `--warp`. Copy the `stats:` lines from the log.

**D. Clean up:** run `setup-osd-spike.bat uninstall`, then `setup-osd-spike.bat status`, which should show nothing installed and no trusted certs.

Ideal game mix (whatever you own is fine): one DX11, one DX12, one Vulkan, one older DX9 title. Use a single-player game first. With anti-cheat games, only observe; if a game complains, stop and note it.

## Results — fill in

PC: CPU ____ · GPU ____ · driver ____ · Windows build ____ · monitor(s) ____ · VRR on/off ____

| Game (API) | Mode in game | Tier 1 visible? | Tier 2 visible? | PresentMode OSD off | PresentMode OSD on | FPS off → on | Notes |
|---|---|---|---|---|---|---|---|
| | Windowed | | | | | | |
| | Borderless | | | | | | |
| | Fullscreen | | | | | | |
| | Exclusive (FSO disabled / DX9) | | | | | | |

Cost (from `stats:` log lines, in-game):

| Device | render avg / max (ms) | CPU % of one core | RAM (MB) |
|---|---|---|---|
| GPU | | | |
| WARP | | | |

## Decisions this spike feeds

- Tier 2 viability, and whether we must ever consider the undocumented `CreateWindowInBand` (only if Tier 2 is *not* above FSO fullscreen games).
- Default renderer: GPU or WARP.
- Whether the OSD measurably changes present mode or FPS on this hardware, which determines how prominent the present-mode warning must be.
