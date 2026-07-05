# Mixxx — ETA-Notes-Fork (RaikCC)

Dies ist **kein** normaler Mixxx-Checkout, sondern Raiks persönlicher Fork mit dem
Feature **ETA Notes**: an Timecodes gebundene Text-Notizen auf der Waveform plus
Live-Countdown in Takten am Playhead. Einzelnutzer-Werkzeug für Raiks DJ-Workflow,
**nicht** für einen Upstream-PR gedacht.

**Bevor du hier arbeitest: `fork-docs/FORK.md` lesen.** Dort stehen Repo-Topologie,
alle Fork-Änderungen nach Bereich, die tragenden Entscheidungen und der
Rebase-Leitfaden. Die Feature-Spezifikation ist `fork-docs/KONZEPT.md` —
§-Verweise in Commits und Doku („§7", „§10") beziehen sich darauf.

## Das Wichtigste in Kürze

- Branch **`eta-notes`** (abgezweigt von Upstream-`2.6`). Remote **`mirror`** =
  `github.com/RaikCC/mixxx` (dorthin pushen); `origin` = Upstream `mixxxdj/mixxx`
  (nur fetch/rebase). Bei `gh`-Befehlen immer `-R RaikCC/mixxx` angeben, sonst
  greift `gh` auf den origin-Remote (Upstream) zu.
- Schema-Migration **v40** (`track_notes`) ist rückwärtskompatibel
  (`min_compatible="3"`) — das darf nicht brechen, Raiks offizielle
  Windows-2.6.0-beta öffnet dieselbe DB.
- `mixxx-test` nur mit **`ninja -j6`** bauen (volle Parallelität → OOM-Kill des
  Compilers). `ninja mixxx` ist unkritisch.
- Raik spricht Deutsch. Sichtbare Änderungen (Rendering/UI) von ihm visuell
  bestätigen lassen, bevor sie als fertig gelten.
- Systemweite Installationen (apt etc.) erst mit Raik absprechen, nicht einfach
  ausführen.
