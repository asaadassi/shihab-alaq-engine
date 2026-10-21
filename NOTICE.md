# Notice

Shihab Alaq Engine is a modified version of the Windhawk engine by Ramen Software (Michael Maltsev):
https://github.com/ramensoftware/windhawk, taken at release v1.7.3 (commit `c165997c54e71dad897460232d595db7cf2c06ab`),
the latest stable release. The first commit of the `stable-1.7.3` line is that code unchanged; until
AlaqEngine 2.0.0-shihab.4 the engine was based on the 2.0.0-alpha.7 pre-release, whose history is kept here too.

It is distributed under the GNU General Public License version 3 (see `LICENSE`). The changes made by Shihab App
are recorded in this repository's history after that commit. Main changes:

- Names: `ShihabAlaqEngine.exe`, `shihab-alaq-engine.dll`, the `ShihabAlaq` service, and its own registry
  keys, mutexes, events and namespaces, so it runs side by side with Windhawk without sharing anything.
- It opens the Shihab Alaq app (`ShihabAlaq.exe`, a separate program) from its tray icon, and never checks
  Windhawk's servers for updates.
- Its resources are in English and Arabic only, with Shihab Alaq's icons.
- Tool mods that name `windhawk.exe` run in `ShihabAlaqEngine.exe`, and a crash guard in the service turns a mod off
  when a program crashes in it; the tray icon can turn all mods off.

The engine also contains third-party libraries under their own licences, in `app/libraries`, `engine/libraries`
and `shared/libraries`, each with its licence text where its authors provided one.

---

محرّك شهاب ألق نسخة معدّلة من محرّك Windhawk من Ramen Software (Michael Maltsev)، وموزَّع برخصة GNU GPL
الإصدار 3. أول التزام في هذا المستودع هو الكود الأصلي كما هو، والتعديلات بعده.
