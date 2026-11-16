# BetterGI on Mac

> An unofficial, community-maintained macOS build of [BetterGI](https://github.com/babalae/better-genshin-impact), adapted to automate **Genshin Impact running through CrossOver (Wine + D3DMetal)** as well as **YAAgl**.

> **This is not an official release.** It is a derivative work built on top of the unofficial macOS port by `Jac0b-Shi`, with additional memory patches for CrossOver compatibility. Released under **GPL-3.0**. Use at your own risk — see the [Disclaimer](#disclaimer-and-limitation-of-liability) before downloading.

> [!WARNING]
> **Important notice — read before downloading.** This project is an unofficial, community-maintained macOS port of BetterGI. It is **not affiliated with, endorsed by, or sponsored by** the upstream BetterGI maintainers, **miHoYo / HoYoverse**, **YAAgl** or its maintainers, or **CodeWeavers / CrossOver**. The software is provided **"AS IS"**, without any express or implied warranty. You are responsible for checking your local laws, the game's Terms of Service, and your own account risk, and you assume **full responsibility** for any account, data, or device loss arising from installation or use. Third-party automation may be detected and can lead to account restriction or permanent ban — **no account-safety guarantee is given**. Please do **not** report issues specific to this build to the BetterGI upstream or the game publisher; use this repository's Issues only. "Genshin Impact", its artwork, and related trademarks belong to their respective owners. The full legal terms are in the [Disclaimer and Limitation of Liability](#disclaimer-and-limitation-of-liability) section below.

---

## Table of Contents

- [What This Project Is](#what-this-project-is)
- [Downloads](#downloads)
- [System Requirements](#system-requirements)
- [Installation](#installation)
- [Before You Start: Game Window Setup](#before-you-start-game-window-setup-important)
- [Usage](#usage)
- [What the Patches Fix](#what-the-patches-fix)
- [macOS Permissions](#macos-permissions)
- [Building from Source](#building-from-source)
- [Known Limitations](#known-limitations)
- [FAQ](#faq)
- [Disclaimer and Limitation of Liability](#disclaimer-and-limitation-of-liability)
- [License](#license)
- [Acknowledgments](#acknowledgments)

---

## What This Project Is

`BetterGI on Mac` chains together three layers:

1. **[babalae/better-genshin-impact](https://github.com/babalae/better-genshin-impact)** (GPL-3.0) — the original BetterGI automation engine (C# Core, recognition assets, tasks, scripts, scheduler).
2. **[Jac0b-Shi/BetterGI-mac](https://github.com/Jac0b-Shi/BetterGI-mac)** (GPL-3.0) — an unofficial native macOS port (MacGI, v0.67.0) that wraps the C# Core in a SwiftUI/AppKit front end and uses ScreenCaptureKit + Core Graphics for capture and input.
3. **This build** — adds three memory patches and a one-click launcher so that the port works reliably with **CrossOver** (in addition to the upstream's primary target, YAAgl).

The published directory is a **complete package**: it contains the runtime binaries (`BetterGI-mac.app`, two `.dylib` patches, `preflight`) together with their **full corresponding source code** and the GPL-3.0 license, in line with GPLv3 §6.

The native application itself is **not** recompiled here — its source is publicly available in the upstream repository (see [NOTICE](NOTICE)). Everything we added is in `patches/`.

## Downloads

Download the latest release from the [Releases page](https://github.com/JackieLmxc/Bettergi-pro-mac/releases).

| File | Purpose |
| --- | --- |
| `Bettergi-pro-mac-v<version>-arm64.zip` | Complete package — app, patches, launcher, full source and license |

The source code tracked in this repository is **separate from** the release archive: the repository stays lightweight (text/source only), while the compiled binaries are shipped as a Release asset. The `.zip` contains the full corresponding source for every distributed binary (see [License](#license)).

Builds are ad-hoc signed and **not notarized** by Apple. On first launch macOS may report that the developer cannot be verified; see [Installation](#installation). Always obtain builds from this repository's Releases — do not download from third parties.

## System Requirements

- **Apple Silicon** Mac (M1 or later).
- **macOS 14 or later**.
- A working Genshin Impact setup on macOS, via either:
  - **CrossOver 25 (Genshin-customized build)** with Genshin Impact installed in a bottle, using the D3DMetal renderer; or
  - **[YAAgl OS](https://github.com/yaagl/yet-another-anime-game-launcher)** (the upstream port's primary tested target).
- Game window at **1920 × 1080**, 16:9, default brightness. HDR, picture filters, and post-processing that alters screenshot colors are not recommended.
- The release bundles a self-contained BetterGI Core, so a separate .NET installation is not required.

## Installation

1. Download and open the archive/DMG.
2. Place `BetterGI-mac.app` in a convenient folder (for example, keep the whole `bettergi-mac` folder together — the launcher relies on relative paths).
3. On first launch, grant **Screen Recording** and **Accessibility** permissions when prompted (see [macOS Permissions](#macos-permissions)).
4. After granting Screen Recording, **quit BetterGI completely and relaunch it** — macOS requires a restart for this permission to take effect.
5. If macOS blocks the app ("cannot be verified"), go to **System Settings → Privacy & Security**, confirm the file came from this repository's Releases, and choose **Open Anyway**. Alternatively, right-click the app and select **Open**.

> Keep the `bettergi-mac` folder and the sibling `原神-mac` folder at the same level. The launcher looks for `../原神-mac/① 启动原神.command` to auto-start the game when it is not already running.

## Before You Start: Game Window Setup (Important)

Recognition accuracy depends on a correctly sized game window. Before running BetterGI:

1. Set Genshin Impact to **1920 × 1080** in **windowed** mode. The bundled `原神-mac/StartGenshin1080.cmd` is copied into the CrossOver bottle by the game launcher to enforce this.
2. Keep the game at **16:9** with **default brightness**; do not enable HDR or color-changing filters.
3. Enter the **open world** and keep the game window visible. Login screens and launcher windows are not valid automation targets.

Failing to set 1920 × 1080 is the most common cause of the white-bar / cropping recognition errors that the v7 patch is designed to fix.

## Usage

1. Double-click `原神-mac/① 启动原神.command` and wait until the game reaches the open world.
2. Double-click `bettergi-mac/② 启动BetterGI.command`. The launcher will:
   - detect whether the game is running (and attempt to start it if not),
   - quit any existing BetterGI process,
   - launch the app with `DYLD_INSERT_LIBRARIES` injecting both patches (it uses direct `exec`, **not** `open -a`, which would silently drop the injection),
   - verify that both patches took effect.
3. Wait for both confirmation lines — `✅ 白条/分辨率补丁(v7) 已生效` and `✅ 一条龙保存修复 已注入` — before using automation.
4. In the BetterGI window, select tasks and run them as usual.

## What the Patches Fix

All patches are **in-memory, zero disk modification, and revert on exit** — the app bundle and game files are never altered.

| Patch | Source | Fixes |
| --- | --- | --- |
| `libqa_patch_wine_gate_v7.dylib` | `patches/qa_patch_v7.c` | Rewrites the built-in `wine` window-tag to `yuan` so the 1920 × 1080 crop no longer includes the white bar; maps virtual key `VK=1` to `F` to unstick the "Combat Preparation" button in event tasks. |
| `libbgi_onedragon_fix.dylib` | `patches/bgi_onedragon_fix.m`, `patches/onedragon_fix_core.h` | Fixes "failed to save One-Dragon configuration" by coercing integer config fields that the port mistakenly stored as booleans back to numbers at serialization time. |
| `preflight` | `patches/preflight.swift` | Read-only window-detection helper used by the launcher to check whether the Genshin window is present. |

## macOS Permissions

| Permission | Why it is needed |
| --- | --- |
| Screen Recording | ScreenCaptureKit reads the Genshin window for image recognition, positioning, and task execution. |
| Accessibility (Input Monitoring) | Used by the CGEvent input backend and global hotkeys. |

BetterGI does not repeatedly prompt for permissions. After changing Screen Recording permission, fully quit and relaunch the app — different signatures or bundle IDs are treated as different applications by macOS.

## Building from Source

Requires Xcode command-line tools (`clang`, `swiftc`), targeting arm64:

```bash
cd patches
chmod +x build.sh
./build.sh
```

This rebuilds both `.dylib` patches in `patches/` and the `preflight` helper in the parent directory. The upstream MacGI application source is maintained separately in [Jac0b-Shi/BetterGI-mac](https://github.com/Jac0b-Shi/BetterGI-mac).

## Known Limitations

- The v7 patch matches specific byte patterns in a specific app build. If the app binary is replaced with a different version, the patch may fail to apply (check `/tmp/qa_patch_wine_gate.log` for `wine=3/3`).
- CrossOver/Wine and game or macOS updates may change window tags, screenshot behavior, or input handling and may require a patch update.
- The upstream port's limitations still apply: some newer map areas may lack minimap markers, and macOS features may lag behind the Windows release.
- Automation via third-party tools always carries a detection risk. See the [Disclaimer](#disclaimer-and-limitation-of-liability).

## FAQ

**Do I have to use CrossOver?** No. The upstream port primarily targets YAAgl; the patches here additionally address the CrossOver case. A 1920 × 1080 game window is required either way.

**Why does the launcher use `foreground-cgevent`?** In the CrossOver setup the input is delivered as macOS synthesized events to the game window, so the CGEvent backend is used; this requires the game to remain the foreground window.

**Why do the patches not apply?** Confirm the app is the expected v0.67.0 build and that it is ad-hoc re-signed with `allow-dyld-environment-variables`. Check `/tmp/qa_patch_wine_gate.log` and `/tmp/bgi_onedragon_fix.log`.

**Will this get my account banned?** Any third-party automation may violate the game's terms of service and may be detected. This project makes no account-safety guarantee; evaluate and accept the risk yourself.

**Where do I report issues?** Report issues with this build in this repository's Issues, including your macOS version, CrossOver/YAAgl version, game resolution, release version, and logs. Do not report them to the BetterGI upstream or the game publisher.

---

## Disclaimer and Limitation of Liability

**Please read this section carefully. By downloading, installing, or using this project, you accept all of the following terms.**

**1. Non-affiliation; no endorsement.** This project is an unofficial, community-maintained derivative work. It is **not** affiliated with, endorsed by, sponsored by, or officially connected to BetterGI or its upstream maintainers (`babalae` and contributors), the macOS port maintainer (`Jac0b-Shi`), **miHoYo / HoYoverse**, **YAAgl** or its maintainers, **CodeWeavers / CrossOver**, or any other party named or implied herein. No representation of partnership or authorization is made.

**2. "AS IS"; no warranties.** THE SOFTWARE IS PROVIDED **"AS IS"**, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, AND NON-INFRINGEMENT. No guarantee is given that the software will be uninterrupted, error-free, secure, compatible with your setup, or remain available or maintained.

**3. Assumption of risk.** You are solely responsible for determining whether downloading, installing, or using this software is lawful and permitted in your jurisdiction and under the game's Terms of Service and any other applicable agreements. You assume **all risk** arising from installation, operation, automation, and any configuration changes, including risks to your account, data, device, and system stability.

**4. No account-safety guarantee; detection risk.** Third-party automation may be detected by the game or its publisher and may result in warnings, restrictions, or permanent account suspension. The authors and contributors make **no representation** about account safety and provide **no guarantee** that use of this software will not violate any game policy or cause any enforcement action. You use it entirely at your own discretion and risk.

**5. Limitation of liability.** TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE AUTHORS, MAINTAINERS, AND CONTRIBUTORS SHALL **NOT** BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY — WHETHER IN CONTRACT, TORT, OR OTHERWISE — ARISING FROM OR RELATED TO THE SOFTWARE OR YOUR USE OF IT, INCLUDING ANY DIRECT, INDIRECT, INCIDENTAL, CONSEQUENTIAL, SPECIAL, EXEMPLARY, OR PUNITIVE DAMAGES, OR ANY LOSS OF ACCOUNT ACCESS, PROGRESS, DATA, PROFITS, OR DEVICE FUNCTIONALITY, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

**6. No support obligation.** This is a community project provided on a best-effort basis. There is no obligation to provide support, updates, bug fixes, documentation, or continued maintenance, and the project may be modified, suspended, or discontinued at any time without notice.

**7. Lawful and fair use only.** Do not use this software to cheat, exploit, disrupt gameplay, infringe intellectual property, or violate any applicable law or game agreement. You are responsible for your own conduct while using it.

**8. Third-party components.** This software depends on third-party open-source projects (including BetterGI, the macOS port, YAAgl, and runtime libraries). Each remains subject to its own license and terms; this disclaimer does not alter those terms.

**9. Trademarks.** "Genshin Impact", associated artwork, and related trademarks are the property of their respective owners. This project is not endorsed by or affiliated with any trademark holder.

**10. Issue reporting.** Do not submit issues specific to this build to the BetterGI upstream, the macOS port's upstream, or the game publisher. Use this repository's issue tracker only.

**11. Acceptance.** If you do not agree with any part of this disclaimer, do not download, install, or use this software.

---

## License

This project, including all added Mac-adaptation source code, is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE). Full corresponding source for all distributed binaries is included in this repository (`patches/`), and original copyright notices are preserved in [NOTICE](NOTICE).

## Acknowledgments

- [babalae/better-genshin-impact](https://github.com/babalae/better-genshin-impact) — the original upstream project.
- [Jac0b-Shi/BetterGI-mac](https://github.com/Jac0b-Shi/BetterGI-mac) — the unofficial macOS port this build is based on.
- [yaagl/yet-another-anime-game-launcher](https://github.com/yaagl/yet-another-anime-game-launcher) — the macOS game launcher target.

*Chinese version: [README.zh-CN.md](README.zh-CN.md)*
