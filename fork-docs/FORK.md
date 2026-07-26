# Was diesen Fork ausmacht

Dieses Dokument ist für Menschen und KI-Agenten, die auf einer beliebigen Maschine
an diesem Fork weiterarbeiten oder ihn auf einen neuen Upstream-Stand rebasen.
Es beschreibt **Struktur und Intent** — die Detailhistorie steht in den Commits
(`git log origin/2.6..eta-notes`).

Feature-Spezifikation: [KONZEPT.md](KONZEPT.md). §-Verweise hier und in
Commit-Messages („§7", „§10") meinen dessen Abschnitte.

## 1. Überblick

**ETA Notes** = frei platzierbare, an Timecodes gebundene Text-Notizen pro Track,
angezeigt auf der Waveform. Bei stehendem Deck an ihrem Timecode (§6, dort auch
Anlegen/Editieren/Verschieben per Maus), bei laufendem Deck als gestapelte
Vorausschau am Playhead mit Takt-Countdown und Kontrastfarb-Füllindikator (§7 —
das Kernfeature). Notes können einen anderen Track referenzieren und erscheinen
dann nur, wenn dieser auf einem anderen Deck liegt, in dessen Deck-Farbschema (§8).

Stand 2026-07-05: Konzept-Kernumfang **komplett umgesetzt** (§6–§10 inkl.
Settings-Seite §9, Inter-Deck-Anzeige §8, mehrzeilige Notes mit Inline-Markdown).
Offen: QML-Waveform-Pfad (Abschnitt 9 unten) und Rebase auf 2.6.0 stable.

## 2. Repo-Topologie

| Remote | URL | Zweck |
|---|---|---|
| `origin` | `mixxxdj/mixxx` (Upstream) | nur `fetch` — Basis für Rebase |
| `mirror` | `RaikCC/mixxx` (public) | Arbeits-Remote: Backup-Push, CI, Releases |

- `RaikCC/mixxx` ist ein **eigenständiges Repo, kein GitHub-Fork** (Forks
  öffentlicher Repos wären zwangsläufig public gewesen, als das Repo noch privat
  war; es taucht daher nicht in Upstreams Fork-Liste auf). Default-Branch dort:
  `eta-notes`.
- Branch **`eta-notes`**, abgezweigt von Upstream-`2.6` (Merge-Base `002e0e9a4a`).
  Raik nutzt 2.6 wegen der **Stems-Unterstützung**.
- Sichern: `git push mirror eta-notes`. Achtung: jeder Push triggert den vollen
  CI-Build (Abschnitt 7).

## 3. Fork-Änderungen nach Bereich

### Neue Dateien (rebasen konfliktfrei)

| Bereich | Dateien |
|---|---|
| Datenmodell | `src/track/note.{h,cpp}` (`Note`/`NotePointer`, Vorbild `Cue`) |
| Persistenz | `src/library/dao/notesdao.{h,cpp}` (Vorbild `CueDAO`) |
| Rendering | `src/waveform/renderers/allshader/waveformrendernotes.{h,cpp}` |
| Farbmodell | `src/waveform/etanotecolors.h` (Schemata, Defaults, Helfer) |
| Editing | `src/widget/wnotemenupopup.{h,cpp}` (Popup-Editor, Vorbild `WCueMenuPopup`) |
| Settings-UI | `src/preferences/dialog/dlgprefetanotes.{h,cpp}` (programmatisch, kein `.ui`) |
| Icon | `res/images/preferences/{dark,light}/ic_preferences_etanotes.svg` |
| CI | `.github/workflows/deb-only.yml` |
| Doku | `CLAUDE.md`, `fork-docs/` |

### Modifizierte Upstream-Dateien (= Konflikt-Hotspots beim Rebase)

| Datei | Was der Fork dort tut |
|---|---|
| `res/schema.xml` | Migration **v40**: Tabelle `track_notes` (`min_compatible="3"`) |
| `src/database/mixxxdb.cpp` | `kRequiredSchemaVersion` 39 → 40 |
| `src/track/track.{h,cpp}` | `getNotes()`/`setNotes()`/`notesUpdated()` exakt nach dem Cue-Muster |
| `src/library/dao/trackdao.{h,cpp}` | ctor nimmt `NotesDAO&`; Load/Save der Notes an beiden Save-Pfaden |
| `src/library/trackcollection.{h,cpp}` | besitzt `NotesDAO`, `initialize()`, Purge-Hook |
| `src/library/scanner/libraryscanner.cpp` | **eigene** DAO-Instanzen — eigener `NotesDAO`, inkl. `initialize()` |
| `src/waveform/widgets/allshader/waveformwidget.cpp` | Renderer eingehängt (zwischen Beat und Mark); ruft `update()` aus `paintGL()` |
| `src/waveform/renderers/allshader/digitsrenderer.{h,cpp}` | erweitert: `measure()`, `updateClipped()` (Glyph-genaues Clipping), parametrisiertes `updateTexture()` (Farbe/Outline/Font; Defaults = alter Look, `waveformrendermark` unberührt) |
| `src/waveform/waveformwidgetfactory.{h,cpp}` | alle ETA-Settings (Member + Getter/Setter, Config-Gruppe `[EtaNotes]`); **zusätzlich** in `getSurfaceFormat()` `format.setSamples(4)` — 4x MSAA gegen Waveform-Kanten-Flimmern (Standalone-Fix, s.u.) |
| `src/widget/wwaveformviewer.{h,cpp}` | Maus-Gesten (Doppelklick anlegen, Rechtsklick Menü, Klick/Drag auf Label), Pixel→Sample-Mapping, ctor-Param `PlayerManager*` |
| `src/skin/legacy/legacyskinparser.cpp` | reicht `PlayerManager*` an `WWaveformViewer` durch |
| `src/preferences/dialog/dlgpreferences.cpp` | ETA-Notes-Seite als letzte Pref-Seite (nach „Modplug Decoder") |
| `res/mixxx.qrc` | Icon registriert (Pref-Icons kommen aus Qt-Resources, nicht Dateisystem) |
| `CMakeLists.txt` | neue `.cpp`s im `mixxx-lib`-Block; **WSL-Guard** deaktiviert (dev-only, s.u.) |
| `.github/workflows/build.yml` | `StemControlTest` auf beiden Windows-Matrix-Einträgen ausgeschlossen (Runner-Artefakt: ALAC-Stem-Load-Timeout → SegFault; ARM64 bestand identischen Code) |
| `src/widget/wlibrarysidebar.cpp` | ctor: `setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff)` — bricht eine Endlos-Relayout-Schleife (Standalone-Fix, s.u.) |

### Standalone-Fixes (nicht Teil des ETA-Notes-Features)

Fixes für **latente Upstream-Bugs**, unabhängig vom Feature — je ein isoliertes
Commit, saubere Kandidaten für einen Upstream-PR. Nicht in `upstream/main` oder
`upstream/2.6` enthalten (Relayout/MSAA geprüft 2026-07-05, RB-Pool geprüft
2026-07-26), lösen sich also beim 2.6.0-Rebase **nicht** von selbst — nach dem
Rebase erneut anwenden (trivialer Cherry-Pick).

| Fix | Datei | Kern | Warum |
|---|---|---|---|
| Relayout-Hang | `src/widget/wlibrarysidebar.cpp` | H-Scrollbar der Sidebar deaktiviert | `QHeaderView::ResizeToContents` macht die Spaltenbreite von den sichtbaren Zeilen abhängig; Ein-/Ausblenden der H-Scrollbar ändert die Viewporthöhe → sichtbare Zeilen → Breite → Scrollbar-Bedarf. Bei bestimmten Größen endlose `setVisible ⇄ postEvent(LayoutRequest)`-Schleife (100 % CPU, Freeze/SIGABRT). Alle drei bekannten Trigger (Waveform-Grip-Drag, degeneriertes `stackedWaveforms_splitSize` beim Start, Panel-Toggle zur Laufzeit) münden hier. Live im hängenden Prozess per gdb bewiesen. |
| Waveform-Flimmern | `src/waveform/waveformwidgetfactory.cpp` | 4x MSAA auf der Waveform-GL-Fläche (`getSurfaceFormat`) | Ohne Multisampling haben die 1–2 px schmalen, hart-kantigen Waveform-Zacken keine partielle Pixel-Deckung; beim Sub-Pixel-Scrollen kippen sie zwischen 1- und 2-Pixel-Deckung → sichtbares Kanten-Flimmern (unabhängig vom PLL-VSync-Timing). Der allshader-Renderer zeichnet in den Default-Framebuffer des Fensters, MSAA wird beim Swap aufgelöst. Betrifft nur den Legacy-Skin-Pfad; der QML-UI-Pfad hat separat MSAA (Issue #12536). |
| RB-Pool seriell | `src/engine/bufferscalers/rubberbandworkerpool.cpp` | `reserveThread()`-Schleife im ctor entfernt | `QThreadPool` zählt Reservierungen zum `activeThreadCount()`; mit `maxThreadCount()` Reservierungen gelingt pro Buffer genau **ein** `tryStart()` (Recycling des wartenden Threads), alle weiteren Stretch-Tasks laufen still seriell im RT-Engine-Thread. Stem-Deck: 1 Task im Worker + 3 inline statt 3+1 parallel. Gegen die Qt-6.10-Quelle (`areAllThreadsActive()`) verifiziert. |
| Keylock-Worker ohne RT-Prio | `src/engine/bufferscalers/rubberbandtask.cpp`, `rubberbandworkerpool.{h,cpp}` | Pool-Threads heißen `RBWorker` und heben sich beim ersten Task auf SCHED_FIFO 78 (unter dem Audio-Callback, geclampt auf `RLIMIT_RTPRIO`) | `QThread::HighPriority` ist auf Linux unter SCHED_OTHER wirkungslos → der RT-Callback wartet in `RubberBandWrapper::process()` an der Semaphore auf verdrängbare Worker = Prioritätsinversion = sporadische Skips, v. a. bei Stems + Keylock (R3 läuft bei Keylock **immer**, auch bei 0.00 %). Engine-Thread (Inline-Tasks) wird per objectName-Guard nicht angefasst. Live-Diagnose 2026-07-26 auf dem G9: Mixxx-Node `ERR=3`, genau ein Pool-Worker als `TS` neben `data-loop.0` `FF 83`. |
| Stretcher-Priming im Callback | `src/engine/bufferscalers/enginebufferscale.h`, `enginebufferscalerubberband.{h,cpp}`, `src/engine/enginebuffer.cpp` | `clearAsync()`: beim Deck-Stopp läuft Reset+Start-Padding des R3-Stretchers (~10 ms bei Stems) auf einem `QThreadPool::globalInstance()`-Worker statt im RT-Callback; Zustandsmaschine `Primed/InFlight` + Semaphore, jeder andere Stretcher-Zugriff synchronisiert via `ensurePrimed()` | Jeder Stopp riss die Callback-Frist (gemessen: `RubberBand::process` bis 11 ms allein fürs Pad); der nächste Start findet den Stretcher fertig vorgewärmt. In-Play-Pfade (Seeks, Richtungswechsel, Scaler-Switch) bleiben synchron und bit-identisch. Achtung Reihenfolge im Stop-Callback: `clearAsync()` erst NACH `setScaleParameters()` (sonst blockiert dessen `ensurePrimed()` sofort auf dem frisch gestarteten Worker). |

### Engine: Parallele Deck-Verarbeitung („Patch B", 2026-07-26)

Fork-Feature über ETA Notes hinaus, Branch-Historie `feat/parallel-decks`.
`EngineMixer::processChannels()` verarbeitet aktive Kanäle **gleichzeitig**
(Pool `DeckWorker`, SCHED_FIFO 80, verfallen nie; ein Kanal läuft im
Callback-Thread selbst; Join vorm Mischen) statt seriell — Callback-Kosten =
Maximum statt Summe der Decks. Gemessen auf dem G9 (2 Stem-Decks, R3):
Dauerbetrieb 17–21 ms → ~9–11 ms, Loop-/Hotcue-Sprung 45–65 ms (= Xrun bei
jedem Sprung) → unter Budget.

- Dateien: `src/engine/enginemixer.{h,cpp}` (ChannelProcessTask, Pool,
  paralleler Zweig), `src/engine/sync/enginesync.h` (`syncDeckExists()`
  public), `rubberbandworkerpool.cpp` (Pool auf 2 gleichzeitige Stem-Decks
  dimensioniert: `2*(n-1)`, gekappt auf Kerne−2).
- **Schalter:** `[App] parallel_decks 0` in `mixxx.cfg` stellt das serielle
  Upstream-Verhalten wieder her (Default: an).
- **Automatisch seriell** bei aktivem Sync-Lock (`syncDeckExists()`-Guard —
  Sync-Code ist nicht nebenläufigkeitsfest; Verarbeitungsreihenfolge
  Leader-zuerst muss dort gelten) und bei nur einem aktiven Kanal.
- Bekannte Restfälle: Sync drücken ⇒ seriell ⇒ alte Lastsummen (gewollt);
  Cue-Spamming kann vereinzelt grenzwertig sein (Kandidat „A3":
  vorgewärmter Tausch-Stretcher); Stem-Laden auf ein Deck kann einen Burst
  werfen (Stretcher-Neuallokation in `onSignalChanged()` läuft noch im
  Callback — offener Fix-Kandidat).

### Dev-only (nicht feature-relevant)

- **WSL-Guard** in `CMakeLists.txt` (~Z. 212): Upstreams `FATAL_ERROR` bei
  WSL-Erkennung auf `if(FALSE)` gepatcht. Nur nötig, solange auf der
  Windows-Maschine in WSL gebaut wird. Bei Rebase-Konflikt: einfach neu anwenden
  (oder weglassen, wenn nur noch nativ auf Linux gebaut wird).
- Test-Notes „Stacking-Test 1–4" in der WSL-Dev-DB — bewusst behalten als
  Stacking-Regressionscheck (nur lokale DB, nicht im Repo).

## 4. Tragende Entscheidungen (nicht ohne Grund ändern)

1. **DB bleibt rückwärtskompatibel:** Migration v40 hat `min_compatible="3"` —
   die offizielle 2.6.0-beta öffnet die DB weiterhin und ignoriert `track_notes`.
   Wichtig, weil Raiks produktive Windows-Installation sich die DB mit der
   offiziellen Beta teilen kann.
2. **Keine Farb-Spalte in der DB.** Farben sind globale Per-Deck-Settings (§9)
   und werden zur Render-Zeit aufgelöst (`schemeForNote`).
3. **`position` = Engine-Sample-Position** wie bei Cues (Frames × 2), gleiche
   Einheit wie der restliche Engine-Code.
4. **Settings via Factory-Singleton, Pull-per-Frame:** `WaveformRenderNotes`
   liest die Settings jeden Frame aus `WaveformWidgetFactory`
   (`refreshSettings()`), keine per-Instanz-Slots/Signale. Bewusst so: das
   untilMark-connect-Muster wäre ~27× Boilerplate gewesen; Pull ist billig, die
   Waveform rendert ohnehin durchgehend, Apply wirkt im nächsten Frame.
   Persistenz: Config-Gruppe `[EtaNotes]` in `mixxx.cfg`.
5. **Countdown floort** (zeigt 0 im letzten angefangenen Takt) — bewusst
   abweichend von Mixxx' until-mark, das auf den nächsten Beat rundet
   (Raiks Wunsch).
6. **GL-Kontext-Regel:** Textur-Erzeugung/-Zerstörung nur in `update()` (wird aus
   `paintGL()` gerufen), **nie** in `preprocess()`. Wer hier refactort, holt sich
   sonst GL-Crashes.
7. **Nur der allshader-/Legacy-Skin-Pfad** rendert Notes. Der QML-Waveform-Pfad
   ist ein separater CMake-Block und bewusst (noch) nicht angebunden.
8. **Bewusste Abweichungen von KONZEPT.md:** Countdown kann Takte **und** Zeit
   zeigen (umschaltbar, Spec sah nur Takte vor); Nachleuchten-Opacity 0.6–0.7
   statt 0.25–0.33 (0.3 war unlesbar); Live-Bars wrappen auf feste Breite statt
   zu elidieren (§10-Verhalten, nach Zwischenschritt mit Eliding).

## 5. Rebase-Leitfaden

Ziel: irgendwann auf **2.6.0 stable** (Upstream-Branch `2.6` bzw. Release-Tag).

```bash
git fetch origin
git rebase origin/2.6          # oder: git rebase <release-tag>
```

Danach prüfen:

1. **Schema-Kollision:** Hat Upstream inzwischen selbst eine Migration v40
   vergeben? Dann unsere auf v41 umziehen (`res/schema.xml` +
   `kRequiredSchemaVersion` + bestehende Dev-DBs beachten).
2. **`digitsrenderer.{h,cpp}`:** unsere Erweiterungen sind additiv mit
   Default-Argumenten — Upstream-Änderungen dort sorgfältig mergen, der
   Until-Mark-Look darf sich nicht ändern.
3. **`CMakeLists.txt`:** es gibt **zwei** Waveform-Quellblöcke (mixxx-lib und
   QML); unsere Dateien gehören nur in den ersten. WSL-Guard-Patch neu bewerten.
4. **ctor-Signaturen:** `TrackDAO` (Param `NotesDAO&`) und `WWaveformViewer`
   (Param `PlayerManager*`) — Aufrufer in `TrackCollection`, `LibraryScanner`,
   `legacyskinparser.cpp` mitziehen, falls Upstream dort umgebaut hat.
5. Build + `mixxx-test` (`-j6`!) + GUI-Smoke (Track laden, Note anlegen,
   abspielen → Live-ETA sichtbar).

## 6. Bauen, Testen, Starten

**Linux nativ** (z. B. Ubuntu Studio 26.04, Raiks Zielrechner):

```bash
tools/debian_buildenv.sh setup      # Build-Deps (ninja-build ggf. manuell nach)
mkdir -p build && cd build && cmake -G Ninja .. && ninja mixxx
./mixxx --resourcePath ../res
ninja -j6 mixxx-test && ./mixxx-test    # -j6: volle Parallelität → OOM
```

- DB/Settings unter `~/.mixxx/` (`mixxxdb.sqlite`, `mixxx.cfg` mit `[EtaNotes]`).
  Erster Fork-Start migriert eine v39-DB automatisch auf v40.
- Headless-Tests: `QT_QPA_PLATFORM=offscreen ./mixxx-test --gtest_filter=…`

**WSL-Besonderheiten** (nur Windows-Dev-Maschine): Distro `Ubuntu-24.04` explizit
angeben (Default ist `docker-desktop`), Start mit `QT_QPA_PLATFORM=xcb`
(Wayland-Plugin fehlt, WSLg liefert X11), kein Audio (ALSA/JACK-Warnungen normal),
WSL-Guard-Patch nötig (Abschnitt 3).

## 7. CI, Installer, Releases

- **`develop.yml`** (Caller für `build.yml`): triggert auf **jeden Push** auf den
  Branch → volle Matrix (Linux DEB 24.04 / macOS / Windows MSI x64+arm64). Für
  reine Doku-/Kleinst-Commits den Run canceln (`gh run cancel <id> -R RaikCC/mixxx`).
  Manuell ohne Commit: `gh workflow run develop.yml -R RaikCC/mixxx --ref eta-notes`.
- **Windows-MSI:** aus der vollen Matrix; **unsigniert** (keine Signing-Secrets im
  Fork → SmartScreen-Warnung, trotzdem installierbar).
- **DEB für Ubuntu Studio 26.04:** eigener Workflow **`deb-only.yml`**
  (`workflow_dispatch`-only, Runner `ubuntu-26.04`, ohne Test-Gate, mit
  `-DWARNINGS_FATAL=OFF`). Grund: ein 24.04-DEB installiert sich auf 26.04
  **nicht** (neue Sonames: ffmpeg 8, Qt 6.10; die `*-private-abi (= x)`-Deps sind
  exakt gepinnt → nach Qt-Point-Update auf dem Zielsystem neu bauen).
  Anstoßen: `gh workflow run deb-only.yml -R RaikCC/mixxx --ref eta-notes`.
- **Artifact-Download-Falle:** Artifacts werden mit `archive: false` hochgeladen →
  der `/artifacts/<id>/zip`-API-Endpoint liefert die **rohe Datei** (MSI/DEB),
  kein Zip. `gh run download` und Entpacken scheitern daher.
- **Anonyme Download-Links** (für andere Rechner ohne `gh`-Auth): GitHub-**Release**
  anlegen — `gh release create <tag> -R RaikCC/mixxx --target eta-notes <datei>`.
  Beispiel: Tag `ubuntu2604-ea41d16da7`.
- **DB-Schutz Windows:** Fork-MSI und offizielle Beta teilen sich
  `%LOCALAPPDATA%\Mixxx\`. Dank `min_compatible="3"` sperrt die Migration die Beta
  nicht mehr aus; DB-Backup vor Fork-Erststart trotzdem empfohlen.

## 8. Bekannte Fallen (Kurzliste)

- `mixxx-test` nur mit `ninja -j6` (OOM bei voller Parallelität; `ninja mixxx` ok).
- `NotePointer` hat kein `operator=(nullptr_t)` → mit `.reset()` leeren.
- `QTextDocument::setTextWidth(-1)` heißt **nicht** „kein Umbruch" (fällt auf
  Default-Page-Size zurück) → für no-wrap große feste Breite setzen,
  Content-Breite via `idealWidth()`.
- `LibraryScanner` hat **eigene** DAO-Instanzen — neue DAOs dort separat
  verdrahten **und** initialisieren (vergessenes `initialize()` war ein echter Bug).
- `gh` ohne `-R RaikCC/mixxx` operiert auf `origin` = Upstream.
- Push + zusätzliches `gh workflow run` = **zwei** Builds; eins canceln.
- Commit-Messages mit Apostroph/Klammern brechen `wsl bash -lc '…'`-Quoting →
  Message in Datei schreiben, `git commit -F <datei>`.

## 9. Offene Punkte

1. **QML-Waveform-Pfad:** `waveformrendernotes` ist nicht im QML-CMake-Block;
   bräuchte QML-Wrapper/Factory + Update-Hook über QSG statt `paintGL()`. Nur
   nötig, falls QML-Skins genutzt werden — Legacy-XML-Skins (aktueller Pfad)
   funktionieren vollständig.
2. **Rebase auf 2.6.0 stable** (Abschnitt 5), wenn Upstream released hat.

## 10. Pflege dieses Dokuments

Bei **strukturellen** Änderungen mitziehen: neue/weitere modifizierte
Upstream-Dateien (Tabellen in Abschnitt 3), neue tragende Entscheidungen
(Abschnitt 4), CI-Änderungen (Abschnitt 7). Feinschliff und Bugfix-Historie
gehören **nicht** hierher — dafür gibt es `git log`.
