# Notice

Shihab Alaq Engine is a modified version of the Windhawk engine by Ramen Software (Michael Maltsev):
https://github.com/ramensoftware/windhawk, taken at commit `0cfc358d26fcbfcc24097d6b66e84848ea6b8c13`
(2.0.0-alpha.7) — the first commit of this repository is that code unchanged.

It is distributed under the GNU General Public License version 3 (see `LICENSE`). The changes made by Shihab App
are recorded in this repository's history after the first commit. Main changes:

- Names: `ShihabAlaqEngine.exe`, `shihab-alaq-engine.dll`, the `ShihabAlaq` service, and its own registry
  keys, mutexes, events and namespaces, so it runs side by side with Windhawk without sharing anything.
- It opens the Shihab Alaq app (`ShihabAlaq.exe`, a separate program) from its tray icon, and never checks
  Windhawk's servers for updates.
- Its resources are in English and Arabic only, with Shihab Alaq's icons.

The engine also contains third-party libraries under their own licences, in `app/libraries`, `engine/libraries`
and `shared/libraries`, each with its licence text where its authors provided one.

---

محرّك Shihab Alaq نسخة معدّلة من محرّك Windhawk من Ramen Software (Michael Maltsev)، وموزَّع برخصة GNU GPL
الإصدار 3. أول التزام في هذا المستودع هو الكود الأصلي كما هو، والتعديلات بعده.
