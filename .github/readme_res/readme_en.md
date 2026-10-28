<!--
 readme编写规范：
  1. 所有readme都需要保留本说明且不可做任何翻译修改。主readme(中文版)是本说明的唯一源头(Source of Truth)，禁止将非中文版本中被修改的说明同步回主readme。非中文版的此说明需始终保持与主readme单向同步更新。
  2. 非中文版本的readme必须保留主readme(中文版)的所有视觉元素、HTML 标签、图标路径和排版格式，非中文版本的readme是中文版的完整对应精准同步翻译版，翻译不可笼统，不可修改原意，不可增减内容。
  3. 针对目标语言环境进行功能描述的本土化。
  4. 非中文版本的readme需要合理修改指向资源的路径，确保资源能够正确显示。
  5. 非中文版本的readme都需要保留并置顶本说明
  6. 非中文版本的readme都需要置顶以下元素，可以替换为相应的翻译版本但必须保留原意：
	    <div align="right">
	    <a href="../readme.md">For the latest updates, please refer to the Chinese README.</a>
	    </div>
  7. 任何关于项目内容的更新，必须首先在主readme(中文版)中完成。在主版确认无误后，再根据本规范同步至其他语言版本。
  8. 非中文版本的readme顶部的语言切换器仅保留指向主readme(中文版)的链接，不互相跳转。非中文版本的入口仅在主readme中统一显示。
-->
<div align="right">
  <a href="../../README.md">For the latest updates, please refer to the Chinese README.</a>
</div>
<div align="center">

# VolumeMixerExtender

**Turning the volume panel in Quick Settings into a complete mixer**

  <a href="https://github.com/EricZhang233/VolumeMixerExtender/releases/latest"><img alt="version" src="https://img.shields.io/github/v/release/EricZhang233/VolumeMixerExtender?label=version&color=512BD4" /></a>
  <a href="https://github.com/EricZhang233/VolumeMixerExtender/releases/latest"><img alt="downloads" src="https://img.shields.io/github/downloads/EricZhang233/VolumeMixerExtender/total?label=downloads&color=0078D4" /></a>
  <a href="https://github.com/EricZhang233/VolumeMixerExtender/stargazers"><img alt="stars" src="https://img.shields.io/github/stars/EricZhang233/VolumeMixerExtender?label=stars&color=E3B341" /></a>

  [![C++ 20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
  [![CMake](https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake)](https://cmake.org/)
  [![Windows 11](https://img.shields.io/badge/Windows-11%2026H2%2B-0078D4?logo=microsoft)](https://www.microsoft.com/windows)
  [![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?logo=gnu)](https://www.gnu.org/licenses/gpl-3.0.html)

<a href="#highlights">Highlights</a> • <a href="#quickstart">Quick Start</a> • <a href="#features">Features</a> • <a href="#cli">CLI</a> • <a href="#architecture">Architecture</a> • <a href="#faq">FAQ</a>

</div>

---

<a name="highlights"></a>

## Highlights

*   🎛️ **The entry is the page**: picking the audio output entry in Quick Settings no longer lands on the system's native sound page, but goes straight to the custom page; opening the sound page with `Win+Ctrl+V` behaves the same way.
*   🔊 **Default devices in one click**: output and input devices each get their own list — click a device row to make it the system default, with the current default highlighted in the accent colour.
*   🎚️ **Endpoint and per-app volume**: device master volume, per-app volume and mute, and live level meters all live on one page, so there is no more digging through system settings.
*   🔀 **Per-app output redirection**: send a single app to a specific output device — "game to the headset, player to the speakers"; a redirected app always shows where its audio goes.
*   🧹 **One-click clear**: the left footer slot returns all apps to the default device and clears saved application volume preferences, restoring them to 100%; endpoint volume stays unchanged.
*   🪟 **Independent manager**: the footer gear opens a WPF manager; custom and system pages remain switchable, and the manager exits when closed.
*   💻 **Command-line management**: `vmex_cli.exe` covers injection, status, endpoints, default devices, redirection and auto-start, with full documentation built into `-help` and `skill`.
*   🚀 **Extract and run**: `vmex.exe` stays resident without a console, hooks in automatically and runs as a single instance — no window and no tray icon.

---

<a name="quickstart"></a>

## Quick Start

### Requirements

| Item | Requirement |
| :--- | :--- |
| Operating system | Windows 11 26H2 (Build 26300) or later, x64 |
| Runtime dependencies | No extra runtime for the host or CLI; the WPF manager uses Windows 11’s built-in .NET Framework 4.8 without bundling a runtime |
| Administrator rights | Not needed for normal operation; recording-driver installation and removal elevate separately |
| Installation | Not required — extract the archive and run |

### Three steps

1. **Download**: grab the latest archive from [Releases](https://github.com/EricZhang233/VolumeMixerExtender/releases) and extract it anywhere (paths with spaces or non-ASCII characters are fine).
2. **Run**: double-click `vmex.exe`. It has no console window; it stays resident in the background and hooks into the system shell automatically. To have it ready right after sign-in, click the footer gear and turn on **Manager → Start with Windows**; you can also run `vmex_manager.exe` directly.
3. **Use it**: press `Win+A` to open Quick Settings and pick the audio output entry — what you see is the custom page.

> 💡 Prefer the command line? `vmex_cli.exe -help` lists every command, and `vmex_cli.exe skill` prints the integration document for scripts and AI agents.

---

<a name="features"></a>

## Features

### Taking over the audio output entry

*   **Content swapped, navigation untouched**: once the sound page appears its content area is replaced, the original content is kept intact and handed back as-is when you switch to the system page.
*   **All four paths are covered**: the entry button and `Win+Ctrl+V` both land on the custom page; Accessibility, Cast and other pages stay untouched, so nothing is taken over by mistake.
*   **Re-evaluated on every open**: the panel is rebuilt each time it opens and the page is applied again — no restart needed and no "works only once" behaviour.

### Default devices and endpoint volume

*   **The "Volume" layer is the default-device switch**: every output device is listed; select a device row to make it the system default, with the current default highlighted in the accent colour.
*   **One row per device**: name, mute button, volume slider and live level meter sit side by side, and dragging takes effect immediately.
*   **Capture devices are collapsed**: capture devices fold into a single "Input devices (N)" row that expands on click, so they never crowd out the output list.
*   **The "Show driver name" switch**: device rows, the redirect drop-down and the "→ endpoint" line all decide together whether names carry the driver suffix. On by default, remembered as soon as you change it.

### Volume mixer

*   **Per-app control**: app icon, name, volume slider and mute button share one row, so adjusting a single app no longer means digging through system settings.
*   **Icons come from the program itself**: when no icon can be extracted the row falls back to an initial-letter tile instead of a blank.
*   **Sessions heal themselves**: sessions are re-enumerated every time the panel opens, so after an audio-service restart or a user switch the list is rebuilt automatically instead of staying blank forever.
*   **Empty lists say so**: when nothing is playing you see "(No apps are currently playing sound)".

### Per-app output redirection

*   **Click the redirection bottom to expand**: choose a target output device from the drop-down; the first entry, "Default device", clears the redirect and follows the system default again.
*   **The destination is always visible**: a redirected app shows a "→ endpoint" line under its name, so you can see at a glance which device it is currently using.
*   **System sounds stay out**: the `System sounds` row offers no redirection.
*   **One-click clear**: the left footer slot, "Clear redirection", returns all apps to the default device, clears the current user's saved application volume preferences and restores current application sessions to 100%. System master and endpoint volumes stay unchanged. Preferences set with other tools are also cleared.
*   **Stereo levels**: two peak bars sit above and below the slider track, using a lighter system accent colour while the volume track uses a darker shade of the same colour.

### Independent manager

Recording mode appears as the last row in the output-device list, with a native toggle. It switches the default output to VB-CABLE's CABLE Input (V), using the previous default device as monitor R. The application mixer controls V directly, so application volume and mute affect both recording and monitoring; R's master volume only affects monitoring. V is always hidden from the main device list and is available in application routing menus only while recording is enabled. Disabling recording restores the remembered default devices and returns all applications explicitly routed to V to the default, preserving routes to other devices and application volumes. Recording state is not persisted. A separate guard process attempts to restore defaults and clear V routes if the host crashes or is terminated.

Recording mode uses VB-Audio's official VB-CABLE standard package, with only the original files needed for Windows 11 x64 shipped in `Driver/VBCABLE/`. After checking the original installer and Microsoft signature, the manager opens the official installer; click Install Driver or Remove Driver there. These operations require administrator privileges and a Windows restart afterwards. Test signing and self-signed certificate imports are unnecessary. Host startup and successful installation disable all three virtual endpoints for standby. Enabling recording temporarily enables only `CABLE Input`; `CABLE In 16 Ch` and `CABLE Output` remain disabled. Disabling recording restores default devices and application routes, stops monitoring, then disables `CABLE Input`. Failed cleanup remains pending for retry; the guard process also attempts recovery and disable after a host crash. Record system audio through the default output's desktop-audio/loopback and use physical microphones. VB-CABLE is shared, so these state changes also affect other applications using it. The driver is never modified. Physical-machine audio validation on the target Windows 11 26H2 remains pending.

The recording-mode toggle depends on configuration: the driver must be installed and running, CABLE Input must exist with verified hardware identity, and a physical monitor output must be available. Enumeration includes disabled endpoints, so disabling CABLE Input for standby does not hide the toggle. Virtual audio streams are created after enabling the endpoint; startup failure rolls back. The entire row is hidden if installation or configuration is incomplete. Readiness is checked asynchronously when the panel opens and while it remains open. Installation, repair and diagnostics remain available through the manager and CLI. For manual diagnosis, `driver hide` exits recording and disables all three endpoints; `driver show` exits recording and reverses the program's recorded disable operations.

Endpoint operations only affect standard VB-CABLE endpoints with verified hardware identity; unverified endpoints are skipped and logged. `driver show` reverses only this program's recorded hiding operations, preserving endpoints previously disabled by the user. Records are stored under `HiddenCableEndpoints` in the user configuration and survive program restarts. If the installer is cancelled, makes no effective change, or fails, endpoint state is restored. Program cleanup also restores its hidden endpoints before deleting configuration, retaining recovery records if restoration fails. Hidden states from older versions without ownership records must be restored manually in Windows sound settings.

The manager, CLI and recording requests connect only to a host in the current session running from the same program directory. After moving or copying the program, exit the host from the old directory before starting the new copy.

The About icon to the left of the manager's title-bar close button shows the current version, developer, project homepage and third-party references, with links to VB-CABLE's source website, licensing and donation/purchase page. VB-CABLE is donationware; supporting its authors is welcome. Professional use and distribution to businesses or organizations must follow [VB-Audio's official licensing terms](https://vb-audio.com/Services/licensing.htm), independently of this project's GPL license.

Click the footer gear or run `vmex_manager.exe` to open the WPF manager. The settings page has been removed from the audio panel. Opening the manager automatically starts the host and provides an Open audio panel button; closing it leaves no manager process or tray icon. The host and sign-in auto-start retain their original entry point and use Core directly. Opening it again activates the existing window.

| Item | Description |
| :--- | :--- |
| **Start with Windows** | Registers or removes the sign-in auto-start. The switch reads the auto-start entry itself, so it also honestly shows "off" when Windows has disabled it under Task Manager → Startup |
| **Show driver name** | Whether device names carry the driver suffix; on by default, applied the next time the panel opens |
| **Status and start** | Shows the background host and panel integration status; starts the host when stopped |
| **Exit** | Ends panel integration and the background host, then closes the manager on success; keeps auto-start and user data |
| **Register to Start menu** | Creates a manager shortcut in the system Start menu, overwriting an existing one |
| **Recording-mode driver** | Shows VB-CABLE status and opens its official installer or uninstaller; exits recording mode before either operation |
| **About** | Title-bar icon opens version, developer and third-party source, licensing and support information |
| **Open log folder** | Opens this program's log folder for troubleshooting |
| **Uninstall and clean up** | After a standard confirmation dialog, stops integration and clears auto-start, configuration, logs, cache, all application redirections and volume preferences; keeps program files and leaves the window open with details if a step fails |

### Background host

*   Double-clicking `vmex.exe` is all it takes: no console window, no tray icon, no main window.
*   Single instance: when it is already running, a second launch is refused instead of starting another copy.

---

<a name="cli"></a>

## CLI: the desktop and the terminal share one core

`vmex_cli.exe` and `vmex.exe` link against the same Core (`vmex_core.dll`) and read and write the same state — change something from the command line and the panel shows the new value the next time it opens.

Two documents ship inside the program, so there is nothing else to look up:

```powershell
vmex_cli.exe -help     # every command and argument
vmex_cli.exe skill     # the integration document for scripts and AI agents
```

This version supports injection and status queries, the host (`start` / `stop` / `host`), the manager (`manager`), opening the panel (`panel`), logs (`logs`), the project page (`project`), driver-name display (`show-driver-name on|off`), endpoint enumeration (`devices`), default-device switching (`default`), application redirection and clearing (`redirect` / `clear-redirect`), recording (`recording`), driver management (`driver`, including hiding or restoring extra endpoints), auto-start (`autostart`) and uninstall cleanup (`uninstall --yes`). The full list and its arguments live in `-help`, which is updated together with the program.

---

<a name="architecture"></a>

## Architecture

Every line of business logic in **VolumeMixerExtender** exists exactly once: the host, CLI and WPF manager share a single Core. The manager renders the interface and does not run a resident service.

```mermaid
flowchart TB
    subgraph Shell["Shells · no business logic"]
        HOST["vmex.exe<br/>console-less background host"]
        CLI["vmex_cli.exe<br/>argument parsing / output formatting"]
        GUI["vmex_manager.exe<br/>WPF manager"]
    end
    subgraph Core["Core · the only business layer"]
        SVC["Injection and monitoring · audio devices and sessions<br/>endpoint policy · payload deployment<br/>auto-start · user settings · logging"]
    end
    subgraph Payload["Injected payload · rendering only"]
        TAP["vmex_tap.dll<br/>the custom page"]
    end
    HOST --> SVC
    CLI --> SVC
    GUI --> SVC
    SVC -->|deploy and hook| TAP
    TAP -->|page actions · named pipe| SVC
    SVC --> OS["Windows audio APIs · registry · Startup folder"]
```

*   **Implementation**: a C++20 Core and C# WPF manager, built through CMake; the archive contains `vmex.exe`, `vmex_cli.exe`, `vmex_manager.exe`, `vmex_core.dll`, `vmex_tap.dll`, `vmex_launcher.dll` and `xamldiagnostics.dll`. Extract all files into the same directory. The host and CLI do not depend on WPF.
*   **One Core**: the background host and the command line share the business layer, so features stay aligned and logging is unified.
*   **The page is the payload**: the interface is rendered by a module injected into the system shell, following the system light/dark theme and accent colour — no extra window and no tray presence.
*   **Actions go through a local pipe**: write operations from the page are handed to the host over a named pipe, so the page itself never holds system handles for long.
*   **Facts, not mirrors**: default devices and redirections are read straight from the current system state, with no local copy that could drift out of sync.
*   **Security boundary**: normal operation uses the system shell's integrity level and does not patch system components. Driver installation and removal request administrator privileges separately.

---

<a name="faq"></a>

## FAQ

<details>
<summary><b>Why is there a vmex.exe in Task Manager?</b></summary>

It is the background host: it hooks into the system shell and makes the actions you take on the page actually happen. It has no window and no tray icon, and it uploads nothing. When you no longer need it, click the footer gear and select **Manager → Exit**.
</details>

<details>
<summary><b>How do I exit completely instead of staying in the background?</b></summary>

Click the footer gear to open the manager, then select "Exit": panel integration ends, the host exits and the manager closes. If a payload is loaded, the system sound panel briefly restarts and returns to its native behaviour. Auto-start is preserved. Closing only the manager does not stop the host.
</details>

<details>
<summary><b>Does it need installing, and does it write to the registry?</b></summary>

Normal features work after extraction. Enabling "Start with Windows" creates a shortcut in the Startup folder. User settings (for example "Show driver name") are stored under `HKCU\Software\EricSoft\VolumeMixerExtender`; logs and cached icons are stored under `%TEMP%\eric\VolumeMixerExtender`. The program loads its injection components from the extracted directory and hooks into the system shell. Recording mode requires a separately installed driver; driver installation and removal request administrator privileges.
</details>

<details>
<summary><b>Where is my data stored?</b></summary>

| Content | Location |
| :--- | :--- |
| User settings | `HKCU\Software\EricSoft\VolumeMixerExtender` |
| Program configuration | `%LOCALAPPDATA%\VolumeMixerExtender\vmex.ini` |
| Cache and logs | `%TEMP%\eric\VolumeMixerExtender`: logs in `log\` (one file per process session) and app icon cache in `icons\`, both safe to delete at any time |

Per-app redirections and the system default devices are stored by Windows itself (the persisted default endpoint table); the program keeps no second copy.
</details>

<details>
<summary><b>How do I upgrade?</b></summary>

Let it exit first (**Manager → Exit**), then overwrite the original folder with the files from the new archive, and double-click `vmex.exe` again. The injected module lives inside the system shell process, so panel integration must end before replacing it.
</details>

<details>
<summary><b>What if my security software flags or blocks it?</b></summary>

It hooks in through the system's own XAML diagnostics interface (the same mechanism the system itself uses for extensions), but "sending a module into another process" is a pattern security software tends to treat as suspicious. If it is blocked or flagged, add the program folder to your trust list and try again.
</details>

<details>
<summary><b>Why does a long app list need so much scrolling?</b></summary>

That is the current trade-off: endpoint volume and the app list share one scrolling area, so a long list has to be scrolled. Later versions will look at compressing or grouping it.
</details>

<details>
<summary><b>Is Windows 10 supported?</b></summary>

No. **Windows 11 26H2 (Build 26300)** or later is required; on older builds the program refuses to start and explains why.
</details>

---

## Contribution & Support

### Build and self tests

The build requires the Visual Studio C++ toolchain, Windows SDK, CMake, PowerShell 7 and .NET 10 SDK. global.json restricts the build SDK to stable .NET 10.0 releases; CI uses Visual Studio 2026. The first build obtains the x64 XAML diagnostics component from a pinned Windows SDK package on Microsoft's official NuGet feed, verifies its SHA-256 hash, Microsoft signature and architecture, and caches it in obj/sdk-deps; no separately installed local XAML diagnostics tools are required. The build also downloads the official VB-CABLE Pack45 with a pinned SHA-256 hash, caches it in obj/driver-deps/vbcable and extracts only the original files needed for Windows 11 x64 into Driver/VBCABLE in the output. The custom test driver is no longer built or distributed. The WPF manager targets .NET Framework 4.8, built into Windows 11. No .NET or Windows App SDK runtime is bundled. `vmex_cli.exe about` shows the same About information as the manager; see `-help` and `skill` for the complete `recording` and `driver` commands.

```powershell
cmake --preset vs18-x64 -DBUILD_TESTING=ON
cmake --build --preset debug
ctest --test-dir obj/vs18-x64 -C Debug --output-on-failure
```

CTest verifies uninstall failure protection, partial audio reset failures, command parsing and real local pipes, configuration and resources. A separate WPF window smoke test checks real controls, light and dark themes, control templates and layout. Tests do not clear user audio preferences. Routing on real devices, saved volume after restarting applications and meter readability at different scale factors still require machine validation.

Releases automatically check out the matching tag, run self tests, build and verify the package, then upload it before publishing the notes. Failures return the release to draft. Local `output` archives do not need committing. After 48 hours, prereleases are promoted only when the package exists, the matching release pipeline succeeded, and there are no `block-release` issues or `[no-release]` marker.

We welcome contributions of any kind — bug reports, documentation improvements or code (PRs).

*   Found a problem or have an idea? Open an [Issue](https://github.com/EricZhang233/VolumeMixerExtender/issues) with reproduction steps and the logs from `%TEMP%\eric\VolumeMixerExtender\log`, and it will be handled faster.
*   Want to get involved in development? The project builds with CMake and Visual Studio, and the background host and the console share one Core, so a single change takes effect on both sides.

If you find this project helpful, please give it a heart-felt ⭐️ **Star**!

<a name="thanks"></a>

## Special Thanks

*   [**EarTrumpet**](https://github.com/File-New-Project/EarTrumpet): the concept and interaction reference for per-app volume and output endpoints. This project rewrote the session enumeration strategy along those lines — re-enumerating on every use and never caching an invalidated enumerator over time — to avoid the classic "app list goes blank and stays blank" class of problems.
*   [**VB-CABLE / VB-Audio Software**](https://www.vb-cable.com/): recording mode uses the official standard virtual audio driver, with the original files needed for Windows 11 x64 shipped alongside. VB-CABLE is donationware; donations or license purchases are welcome, and use and distribution must follow its [independent licensing terms](https://vb-audio.com/Services/licensing.htm).
*   [**Windows App SDK / XAML diagnostics**](https://learn.microsoft.com/windows/apps/): the ability to take over the page rests on the XAML diagnostics and visual tree interfaces that ship with the system.

---

This project is licensed under [GPL-3.0](../../LICENSE).

<div align="center">
  Made with ❤️ by EricZhang233
</div>
