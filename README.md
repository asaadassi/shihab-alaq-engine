# Shihab Alaq Engine

The engine of [Shihab Alaq](https://shihab.app/): a Windows service and a small tray program that load mods
(small C++ DLLs) into Windows programs such as Explorer and the taskbar, and reload them when their configuration
changes. It is a modified version of the [Windhawk](https://github.com/ramensoftware/windhawk) engine by Ramen
Software, licensed under **GPL-3.0** (see [`NOTICE.md`](NOTICE.md) and [`LICENSE`](LICENSE)).

محرّك Shihab Alaq: خدمة ويندوز وبرنامج صغير بجانب الساعة، يحمّلان الإضافات في برامج ويندوز ويعيدان تحميلها عند
تغيير إعداداتها. مبني على Windhawk، ومرخّص برخصة GPL-3.0.

## Build

Visual Studio 2022 (Desktop development with C++, ARM64 build tools), then:

```bat
nuget restore ShihabAlaqEngine.sln
build.bat Release ""
pwsh scripts\package.ps1 -Out dist
```

Every push is built by GitHub Actions (`.github/workflows/build.yml`). A version in `shared/version.h` that has no
release yet (e.g. `2.0.0-shihab.1`) is published from `main` as release `v<version>`, tagged at that commit: the
package Shihab Alaq's installer downloads, with its `SHA256SUMS.txt`. Raise the version to make a new release.

## Layout

| Folder | What it is |
|---|---|
| `app/` | `ShihabAlaqEngine.exe`: the service (`-service`), the tray part, the command line (`-restart-bg`, `-exit`, …) |
| `engine/` | `shihab-alaq-engine.dll`: injected into processes, loads the mods, the mod API (`mods_api.h`) |
| `shared/` | Code both use, and the version |

## The contract with Shihab Alaq

The app is a separate program: it starts this exe with a switch, writes the registry values the engine watches
(`HKLM\SOFTWARE\ShihabAlaq\Engine\Mods\<id>`), and puts the mod DLLs in the engine's data folder. The names are
listed in the app's `docs/ENGINE.md`; change them in both places together. The mod ABI (`Wh_*`, `WindhawkUtils`,
the `InternalWh_*` exports) and the mod source format (`==WindhawkMod==` blocks) are kept as they are, so mods
written for Windhawk build for this engine.
