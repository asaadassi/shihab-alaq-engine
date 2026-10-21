# Shihab Alaq Engine — project rules

The GPL-3.0 engine of Shihab Alaq, a Windhawk fork (C++23, MSVC v143, WTL). Read README.md and NOTICE.md first.

- This repository is public and GPL: every engine build shipped with Shihab Alaq must have its exact source here,
  released as `v<version>` (shared/version.h; CI makes the release and tag when the version is new). Never put Shihab Alaq app code here, and never copy code from the app.
- Keep upstream code recognisable: small, commented changes, so upstream fixes can be merged. Keep the first commit
  (the unchanged upstream import) and credit Windhawk / Ramen Software.
- Never rename the mod ABI: `Wh_*`, `WindhawkUtils`, the `InternalWh_*` exports, `mods_api.h`, the `==WindhawkMod==`
  block names, and the `-tool-mod` switch and `windhawk.exe` pattern that tool mods use.
- Names shared with the app (exe, DLL, service, registry keys, ini files, switches) are listed in the app's
  docs/ENGINE.md; change both together.
- Resources: English and Arabic only (app/rsrc.rc is UTF-16 LE with BOM and CRLF).
- Builds only on Windows: check every change on GitHub Actions.
- Git: no commits unless the user asks.
