# Shihab Styler Engine

The engine of [Shihab Styler](https://shihab.app/): a Windows service and a small tray program that load mods
(small C++ DLLs) into Windows programs such as Explorer and the taskbar, and reload them when their configuration
changes. It is a modified version of the [Windhawk](https://github.com/ramensoftware/windhawk) engine by Ramen
Software, licensed under **GPL-3.0** (see [`NOTICE.md`](NOTICE.md) and [`LICENSE`](LICENSE)).

محرّك Shihab Styler: خدمة ويندوز وبرنامج صغير بجانب الساعة، يحمّلان الإضافات في برامج ويندوز ويعيدان تحميلها عند
تغيير إعداداتها. مبني على Windhawk، ومرخّص برخصة GPL-3.0.

## Build

Visual Studio 2022 (Desktop development with C++, ARM64 build tools), then:

```bat
nuget restore ShihabStylerEngine.sln
build.bat Release ""
pwsh scripts\package.ps1 -Out dist
```

Every push is built by GitHub Actions (`.github/workflows/build.yml`); a tag `v<version>` (the version in
`shared/version.h`, e.g. `v2.0.0-shihab.1`) publishes the release Shihab Styler's installer downloads, with its
`SHA256SUMS.txt`.

## Layout

| Folder | What it is |
|---|---|
| `app/` | `ShihabStylerEngine.exe`: the service (`-service`), the tray part, the command line (`-restart-bg`, `-exit`, …) |
| `engine/` | `shihab-styler-engine.dll`: injected into processes, loads the mods, the mod API (`mods_api.h`) |
| `shared/` | Code both use, and the version |

## The contract with Shihab Styler

The app is a separate program: it starts this exe with a switch, writes the registry values the engine watches
(`HKLM\SOFTWARE\ShihabStyler\Engine\Mods\<id>`), and puts the mod DLLs in the engine's data folder. The names are
listed in the app's `docs/ENGINE.md`; change them in both places together. The mod ABI (`Wh_*`, `WindhawkUtils`,
the `InternalWh_*` exports) and the mod source format (`==WindhawkMod==` blocks) are kept as they are, so mods
written for Windhawk build for this engine.
