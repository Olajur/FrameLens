# FrameLens — Security notes: the "Performance Log Users" permission

FrameLens measures FPS with Event Tracing for Windows (ETW). Windows lets only three kinds of callers control ETW sessions: elevated administrators, members of the built-in **Performance Log Users** group (SID `S-1-5-32-559`), and services running as LocalSystem / LocalService / NetworkService. FrameLens uses the group so the app itself never runs elevated.

## What the group grants

- Create and read **real-time and on-disk ETW sessions**, and **enable ETW providers** in them.
- Manage performance-counter logs and alerts (the Performance Monitor "Data Collector Sets").
- It does **not** grant the NT Kernel Logger (system-wide kernel tracing stays admin-only), and some providers are restricted to Administrators/SYSTEM by their own security descriptors.

## What it does not grant

- No administrator rights, no UAC bypass, no ability to install drivers or services.
- No write access to system files, the registry outside the user's own hive, or other users' files.
- No ability to run code in, or inject into, other processes.
- No change to clocks, voltages, firewall, Defender or any other system setting.

## The real risk

1. **It applies to the user account, not to FrameLens.** Every program running as you gains the same ETW capability. FrameLens can't be singled out.
2. **It is an information-disclosure capability.** A malicious program already running as you could enable providers that log activity system-wide — for example process starts, DNS lookups or some network/HTTP metadata — including activity of other users' processes on the same PC.
3. **Context:** a malicious program already running as you can read your files and browser data, take screenshots and log keystrokes **without** this group. So on a single-user home PC the *additional* exposure is small. It matters more on shared or managed PCs, where seeing other users' activity is the main concern.
4. **Remote use:** the group also allows remote management of performance logs, but only for someone who can already authenticate to the PC over the network as you. Home PCs with remote management services off are not exposed.

**Assessment:** low risk for a personal gaming PC. It is a standard, Microsoft-documented way to grant tracing to non-admins, and the same requirement PresentMon documents. Avoid it on shared or work machines unless your administrator approves.

## Mitigations

- The setup script changes only this group membership, addresses the group by SID, and has `remove` to undo it.
- Membership becomes effective at the next sign-in, and is removed equally simply.
- **Planned alternative (evaluate in M7):** a tiny FrameLens collector service running as `LocalService`, which already has ETW rights. Your user account then needs no group membership, and only that one service has the capability. Trade-off: installing a service also needs one-time admin approval, and it adds a background service. It may become the default if it proves no heavier than the group approach.
