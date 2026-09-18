# Was diesen Fork ausmacht

Dieses Dokument ist für Menschen und KI-Agenten, die auf einer beliebigen Maschine
an diesem Fork weiterarbeiten oder ihn auf einen neuen Upstream-Stand rebasen.
Es beschreibt **Struktur und Intent** — die Detailhistorie steht in den Commits
(`git log upstream/2.6..eta-notes`).

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
| `origin` | `RaikCC/mixxx` (public) | Arbeits-Remote: Backup-Push, CI, Releases |
| `upstream` | `mixxxdj/mixxx` | nur `fetch` — Basis für Rebase |

- `RaikCC/mixxx` ist ein **eigenständiges Repo, kein GitHub-Fork** (Forks
  öffentlicher Repos wären zwangsläufig public gewesen, als das Repo noch privat
  war; es taucht daher nicht in Upstreams Fork-Liste auf). Default-Branch dort:
  `eta-notes`.
- Branch **`eta-notes`**, abgezweigt von Upstream-`2.6` (Merge-Base `002e0e9a4a`).
  Raik nutzt 2.6 wegen der **Stems-Unterstützung**.
- Sichern: `git push origin <branch>`. Löst **nichts** aus — `develop.yml` ist im
  Repo auf `disabled_manually` gestellt (`gh workflow list -R RaikCC/mixxx --all`).
  CI-Läufe müssen seitdem von Hand angestoßen werden, siehe Abschnitt 7.
- Diese Namen gelten für Raiks Linux-Checkout (`~/mixxx-drag-crash/mixxx-src`).
  Ein älterer Checkout auf der Windows-/WSL-Maschine benutzt sie **vertauscht**
  (`origin` = Upstream, `mirror` = RaikCC). Im Zweifel `git remote -v` fragen,
  bevor man pusht oder rebast — sonst zeigt `origin/2.6` ins Leere.

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
| `src/track/track.{h,cpp}` | `getNotes()`/`setNotes()`/`notesUpdated()` exakt nach dem Cue-Muster; zusätzlich `adjustReplayGainRatio()` (s. ReplayGain-Abschnitt unten) |
| `src/widget/wtrackmenu.{h,cpp}` | Feature-Flag `AdjustReplayGain` + Untermenü „Adjust ReplayGain" mit dB-Stufen |
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
| `.github/workflows/build.yml` | `StemControlTest` auf beiden Windows-Matrix-Einträgen ausgeschlossen (Runner-Artefakt: ALAC-Stem-Load-Timeout → SegFault; ARM64 bestand identischen Code). **Auch lokal flaky**, siehe Abschnitt 6 |
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

### Engine: Kohärentes Stem-Stretching (2026-08-11)

Fork-Feature über ETA Notes hinaus, Branch-Historie `feat/coherent-stem-keylock`.
Stem-Decks werden von **einer** RubberBand-Instanz über alle 8 Kanäle gestretcht
(mit `OptionChannelsTogether`, auch für R2 erzwungen) statt vom Upstream-Split in
4 unabhängige Stereo-Instanzen.

- **Warum:** Die 4 getrennten Instanzen treffen bei **Ratio-Änderungen**
  (Keylock-Pitch-Bend) eigene, inhaltsabhängige Transienten-/Phasen-
  Entscheidungen → die Stems driften Millisekunden gegeneinander → Kammfilter
  in der Summe. In Mixxx-Aufnahme gemessen (gleicher Loop, ohne/mit Bend):
  **−2,5…−3,2 dB @ 1–8 kHz** („dumpf"). Gegen librubberband-C-API reproduziert
  (blockweise `setTimeRatio`, realtime, R3): eine kohärente 8-Kanal-Instanz
  halbiert den Präsenzverlust auf das Niveau einer vorgemischten Stereo-Datei.
  Bei konstanter Ratio sind beide Varianten identisch — deshalb fiel es nur
  beim Bend auf. Diagnose-Historie: `~/mixxx-fork-r3-project/project.md`.
- **Wirkung nachgemessen** (2026-08-11, Aufnahmepaare mit identischer Bend-Tiefe
  −2,98 %, per Onset-Autokorrelation verifiziert): Präsenzband **2–4 kHz von
  −3,2 auf −1,5 dB**, 4–8 kHz von −3,0 auf −1,8 dB, 1–2 kHz von −2,5 auf
  −1,4 dB. Der Rest ist der Eigenpreis dynamischen Stretchings (eine normale
  Stereo-Datei zahlt ~1 dB) — ein hörbarer Restunterschied bleibt also
  bauartbedingt.
- **Kosten (gemessen, G9 / i5-12400, `pw-top`):** 1 Stem-Deck 11,3 ms, zwei
  Stem-Decks + Bend 12,2 ms median / 14,3 ms max von 46,4 ms (26 / 31 %),
  ERR 0. Zwei Decks kosten nur ~1 ms mehr als eines — Parallel-Decks zahlt
  das Maximum statt der Summe (seriell wären es ~22 ms). Läuft inline im
  jeweiligen `DeckWorker`; der RB-Pool bleibt für den
  `keylock_multithreading`-Stereo-Split bestehen, läuft bei Stems aber leer
  (keine `RBWorker`-Threads mehr sichtbar).
- **Achtung Latenz: die Kosten skalieren NICHT mit der Puffergröße.** Bei
  1024/44100 bleibt BUSY bei ~12 ms (identischer Absolutwert wie bei 2048) ⇒
  52 % Auslastung statt 26 %, und die ohnehin nicht mitskalierenden Spitzen
  reißen die 23-ms-Frist: 7 Xruns in ~15 s Spielzeit, Skips hörbar (Mixxx'
  eigener Node, Interface-Node ERR 0). Ursache: fixer Aufwand pro Callback
  (R3-interne Festfenster, Worker-Wakeup/Semaphore) dominiert gegenüber der
  Sample-Zahl. **2048 ist für Stems + R3 gesetzt.**
- **Dateien:** `rubberbandwrapper.cpp` (Single-Instance-Zweig in `setup()`),
  `rubberbandworkerpool.{h,cpp}` (Config-Flag + Accessor).
- **Schalter:** `[App] keylock_coherent_stems 0` stellt den Upstream-Split
  wieder her (Default: an). Verify im Log: `coherent stem stretching, one 8
  channel instance` statt `using 2 channel(s) per task`.

### Library: ReplayGain live korrigieren („Adjust ReplayGain", 2026-08-05)

Fork-Feature über ETA Notes hinaus, Branch-Historie `feat/replaygain-live-edit`.
Rechtsklick auf einen Track in der Bibliothek → Untermenü **„Adjust ReplayGain"**
mit den Stufen +3/+2/+1/+0.5/−0.5/−1/−2/−3 dB. Korrigiert die Lautheit nach Gehör
**mitten im Set**, ohne das Deck anzuhalten. Funktioniert auch auf einer
Mehrfachauswahl und in den Deck-Widget-Menüs.

- **Warum überhaupt nötig:** `Track::setReplayGain()` emittiert
  `replayGainUpdated`, und `BaseTrackPlayerImpl::slotSetReplayGain()` **verwirft
  das bei laufendem Deck** (nur `m_replaygainPending` wird gesetzt, eingelöst erst
  beim Stopp) — bewusst gegen ungewollte Lautstärkesprünge. Für eine
  Gehör-Korrektur ist genau das der Killer.
- **Der genutzte Weg:** `Track::adjustReplayGainRatio()` skaliert das Ratio und
  emittiert `replayGainAdjusted` mit **leerer** `requestingPlayerGroup`. In
  `slotAdjustReplayGain()` wirkt das sofort auf jedes Deck, und weil kein Deck
  sich als Auslöser erkennt, kompensiert auch keins die Änderung über sein
  Pregain. `EnginePregain` blendet über ~1 s weich um (`kFadeSeconds`), es
  knackst also nicht.
- **Kein Wert ⇒ kein Nudge.** Ohne vorhandenen ReplayGain gibt es nichts zu
  skalieren; ein Ersatz-Startpunkt 1.0 würde um den Default-Boost springen. Das
  Untermenü ist dann ausgegraut, `adjustReplayGainRatio()` zusätzlich no-op.
- **Persistenz:** `Mode::ApplyAndSave` — sonst hinge die Korrektur bis zum
  Auswerfen des Tracks nur im RAM. Datei-Tags werden nur angefasst, wenn
  `SyncTrackMetadataExport` an ist (bei Raik aus, wichtig: sonst würde jede
  Korrektur eine ~40-MB-Stem-Datei neu durch den Gdrive-Sync schieben).
- **Falle:** `WTrackMenu::featureIsEnabled()` hat im Modell-Modus ein `switch`
  über die Flags mit `default: DEBUG_ASSERT(!"unreachable"); return false;`. Ein
  neues Feature-Flag **braucht dort einen eigenen `case`**, sonst erscheint es in
  keinem Bibliotheksmenü — und der Debug-Build assertet (die Assertions sind hier
  scharf, `DEBUG_ASSERTIONS_FATAL=OFF`, also nur Log).

### Waveform/Skin: Downbeat-Indikator (2026-09-06)

Fork-Feature über ETA Notes hinaus, Branch-Historie `feat/downbeat-indicator`.
Markiert **einen Beat des Beatgrids als Taktanfang** (Downbeat) und zeigt
daraus zwei Dinge: dickere, hellblaue Downbeat-Linien im Beatgrid der Waveform
und – für jedes Deck mit Downbeat – eine Reihe aus **4 liegenden Rechtecken in
der Toolbar** über den Waveforms, in der das gerade gespielte Taktviertel
leuchtet. Untereinander gestapelt zeigen die Reihen auf einen Blick, ob zwei
Decks taktsynchron laufen oder um wie viele Schläge sie versetzt sind.

- **Bedienung:** Knopf **Downbeat** in den Beatgrid-Controls rechts neben der
  Waveform (direkt hinter „Adjust Beatgrid"/CurPos). Ein Druck macht den Beat,
  der dem Abspielmarker am nächsten liegt, zum Downbeat — und damit jeden
  4. Beat davor und danach. Trifft der nächste Druck einen Beat, der bereits
  Downbeat ist, wird die Markierung **komplett entfernt**; trifft er einen
  anderen Beat, ersetzt dieser die alte Markierung.
- **Ohne Downbeat sieht alles aus wie vorher:** Der Renderer und die
  Indikatorreihe zeichnen dann gar nichts. Das ist bewusst die einzige
  Fallunterscheidung — kein „leerer" Zustand mit dunklen Rechtecken.
- **Datenmodell:** *eine* Zahl pro Track, `Track::getDownbeatPosition()` /
  `setDownbeatPosition()` (Engine-Sample-Position wie bei Cues, invalid = kein
  Downbeat). Die Taktphase wird **nicht gespeichert**, sondern in
  `src/track/downbeat.h` aus dem Beat-Index-Abstand zum Anker gerechnet
  (`Beats::ConstIterator::operator-`, Modulo 4). Der Anker wird beim Rechnen
  per `findClosestBeat()` aufs Grid gesnappt — verschiebt man das Beatgrid
  nachträglich, wandert der Downbeat mit, statt ungültig zu werden.
- **Persistenz:** Migration **v41**, neue Spalte `library.downbeat_position`
  (`min_compatible="3"`, siehe Entscheidung 1 oben — die offizielle 2.6.0-beta
  spricht die `library`-Tabelle über explizite Spaltennamen an und ignoriert
  die Zusatzspalte).
- **Engine:** alles in `ClockControl` (dort sitzt schon `beat_active`, und es
  bekommt pro Callback `updateIndicators(rate, position, sampleRate)`).
  Drei neue COs pro Deck:
  `downbeat_set` (ControlPushButton, Setzen/Aufheben),
  `downbeat_active` (0/1, read-only) und
  `downbeat_phase` (−1 oder 0..3, read-only).
  `downbeat_phase` wird **vor** dem Standstill-Early-Return von
  `updateIndicators()` aktualisiert, damit die Anzeige auch beim stehenden oder
  gescrubbten Deck stimmt (ein Deck mit geladenem Track wird laut
  `EngineDeck::updateActiveState()` in jedem Callback verarbeitet).
- **Rendering:** eigener Knoten `WaveformRenderDownbeat` **nach**
  `WaveformRenderBeat` (Play- und Slip-Pfad), damit `waveformrenderbeat.cpp`
  unangetastet bleibt und die Downbeat-Linie ihre eigene Farbe/Breite bekommt
  (3 px, auf dem Beat zentriert). Die Taktphase wird einmal für den ersten
  sichtbaren Beat berechnet und dann nur noch hochgezählt.
- **Skin (nur LateNight):** `<DownbeatColor>` im `<Visual>`-Knoten (Default im
  Renderer `#4dd2ff`, falls ein Skin die Farbe nicht setzt),
  `downbeat_indicators.xml` + `downbeat_indicator_row.xml` für die Toolbar,
  Icons `btn__downbeat[_active].svg` in beiden Schemes. Die Indikator-Zeile
  jedes Decks liegt in einem **fest dimensionierten Slot**, damit die Zeilen
  beim Ein-/Ausblenden nicht verrutschen — sonst wäre der Vergleich zweier
  Decks wertlos.
- **Farbe ändern:** Rechteck-Farbe in `style.qss` (`#DownbeatSegment[highlight="1"]`),
  Gridlinien-Farbe in `skin.xml` (`<SetVariable name="DownbeatColor">`, je Scheme).
- **Dateien:** neu `src/track/downbeat.{h,cpp}`,
  `src/waveform/renderers/allshader/waveformrenderdownbeat.{h,cpp}`,
  `res/skins/LateNight/downbeat_indicator{s,_row}.xml`,
  `res/skins/LateNight/{classic,palemoon}/buttons/btn__downbeat[_active].svg`;
  geändert `res/schema.xml`, `src/database/mixxxdb.cpp`,
  `src/track/track.{h,cpp}`, `src/track/trackrecord.h`,
  `src/library/dao/trackdao.cpp`, `src/engine/controls/clockcontrol.{h,cpp}`,
  `src/engine/enginebuffer.{h,cpp}`,
  `src/waveform/widgets/allshader/waveformwidget.cpp`,
  `src/skin/legacy/tooltips.cpp`, `CMakeLists.txt` und die LateNight-Skin-Dateien.

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
9. **Keine Phrasen-Anzeige über ein festes Beat-Raster.** Am 2026-09-18 gebaut
   und von Raik noch am selben Abend verworfen: ein Phrasen-Anker pro Track,
   daraus alle 32 Beats eine pinke Linie (Technik wie beim Downbeat). Auf echten
   Tracks passt das nur an einem Ende. Einschübe von 4 oder 16 Beats, Breaks und
   Übergänge verschieben die Phase, und ein einzelner Anker mit fester Periode
   kann das grundsätzlich nicht abbilden. Beim Downbeat (Periode 4) ist dasselbe
   Prinzip unkritisch. **Nicht erneut vorschlagen.** Falls Raik das Thema selbst
   aufbringt, bräuchte es Phrasenmarken pro Abschnitt (jede gilt ab ihrer
   Position). Der Code wurde gelöscht und war nie in `eta-notes`.

## 5. Rebase-Leitfaden

Ziel: irgendwann auf **2.6.0 stable** (Upstream-Branch `2.6` bzw. Release-Tag).

```bash
git fetch upstream
git rebase upstream/2.6        # oder: git rebase <release-tag>
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
  Erster Fork-Start migriert eine ältere DB automatisch auf den aktuellen Stand
  (v39 → v40 `track_notes`, v40 → v41 `downbeat_position`).
- Headless-Tests: `QT_QPA_PLATFORM=offscreen ./mixxx-test --gtest_filter=…`,
  komplett: `QT_QPA_PLATFORM=offscreen ctest --output-on-failure` (~20 min,
  1232 Tests).
- **Flaky, nicht erschrecken:** `StemControlTest/StemControlFixture.StemCount/"ALAC_24bit"`
  fällt im vollen sequentiellen Durchlauf auch lokal gelegentlich durch (gemessen
  2026-09-06 auf dem G9: 1231/1232). Es ist ein **Lade-Timeout**, kein
  Stem-Fehler — der erste Timeout trifft `sine-30.wav`, eine gewöhnliche
  WAV-Datei im Fixture-Aufbau, der Test misst danach 0 statt 4 Stems. Isoliert
  (`ctest -R 'StemControlTest.*ALAC_24bit'`) besteht er zuverlässig. Vor dem
  Verdächtigen eigener Änderungen also erst isoliert nachfahren.
- **Komplettlauf nur über `ctest`, nicht als ein `./mixxx-test`-Prozess**
  (gemessen 2026-09-18, Debug-Build). Als Einzelprozess bricht der Lauf bei
  `SoundSourceProxyTest.openEmptyFile` ab: Ein `DEBUG_ASSERT` in
  `util/fileinfo.h` beendet den Prozess mit Exit 130, alle Tests danach fehlen
  dann. Außerdem scheitern dort die vier
  `ControllerScriptEngineLegacyTimerTest.beginTimer_singleShotTimer*`, die
  einzeln bestehen. Beides ist Upstream-Verhalten und hat nichts mit dem Fork
  zu tun. `ctest` startet jeden Test in einem eigenen Prozess und umgeht das.

**GUI-Smoke-Test, ohne Raiks laufende Session zu stören.** Raik hat oft sein
produktives Mixxx offen (Inpulse, JACK). Eine zweite Instanz auf seinem Desktop
würde um Audiogerät und Controller konkurrieren und Fenster aufpoppen lassen.
Xvfb und xdotool sind nicht installiert (apt nur nach Absprache). Dieser Weg
braucht nichts davon und wurde am 2026-09-18 erprobt:

1. **Eigenes Settings-Verzeichnis:** Kopie von `~/.mixxx`. Die DB per SQLite-Backup
   kopieren (`src.backup(dst)` in Python), weil sie im Betrieb offen ist. In
   `mixxx.cfg` unter `[Controller]` alle Geräte auf `0` setzen und eine leere
   `soundconfig.xml` (ohne `<SoundDevice>`) hinterlegen.
2. **Verschachteltes KWin im virtuellen Framebuffer**, mit eigener D-Bus-Session
   (Pflicht, sonst kollidiert es mit Raiks KWin):

   ```bash
   env -u WAYLAND_DISPLAY -u DISPLAY dbus-run-session -- kwin_wayland --virtual \
     --xwayland --width 1920 --height 1080 --socket wayland-smoke \
     --no-lockscreen --no-global-shortcuts --exit-with-session ./session.sh
   ```

   `session.sh` startet Mixxx auf dem inneren Xwayland (war `:1`) und sperrt
   Audio komplett ab:

   ```bash
   exec env QT_QPA_PLATFORM=xcb ALSA_CONFIG_PATH=/pfad/leere-datei \
     PIPEWIRE_REMOTE=none PULSE_SERVER=unix:/nonexistent JACK_NO_START_SERVER=1 \
     ./mixxx --resourcePath ../res --settingsPath <kopie> "<track>"
   ```

   Ein Track als Argument landet direkt in Deck 1. Die allshader-Waveform
   rendert dort per Software-GL korrekt.
3. **Dialog „Keine Ausgabegeräte"** mit Enter bestätigen (Weiter).
4. **Screenshot:** `DISPLAY=:1 import -window <id> shot.png` (ImageMagick,
   Fenster-ID aus `xwininfo -root -tree`). Das Bild zeigt Fensterkoordinaten.
   Für Klicks den Fenster-Offset aus `xwininfo` addieren (war `+0+34`).
5. **Klicks und Tasten** per XTest über Python-ctypes, ohne Zusatzpakete:

   ```python
   import ctypes
   x11 = ctypes.CDLL("libX11.so.6"); xt = ctypes.CDLL("libXtst.so.6")
   x11.XOpenDisplay.restype = ctypes.c_void_p
   d = ctypes.c_void_p(x11.XOpenDisplay(b":1"))
   xt.XTestFakeMotionEvent(d, -1, x, y, 0)
   xt.XTestFakeButtonEvent(d, 1, 1, 0); xt.XTestFakeButtonEvent(d, 1, 0, 0)
   x11.XFlush(d)
   # Tasten: kc = x11.XKeysymToKeycode(d, x11.XStringToKeysym(b"Return")),
   # dann XTestFakeKeyEvent(d, kc, 1, 0) / (d, kc, 0, 0)
   ```

6. **Beenden mit Ctrl+Q:** Mixxx schreibt dabei die DB sauber, das virtuelle KWin
   endet mit ihm. Danach lassen sich die Ergebnisse in der DB-Kopie prüfen.

Das ersetzt Raiks eigene Sichtprüfung nicht (siehe `CLAUDE.md`), fängt aber grobe
Fehler ab, bevor er sie zu sehen bekommt.

**WSL-Besonderheiten** (nur Windows-Dev-Maschine): Distro `Ubuntu-24.04` explizit
angeben (Default ist `docker-desktop`), Start mit `QT_QPA_PLATFORM=xcb`
(Wayland-Plugin fehlt, WSLg liefert X11), kein Audio (ALSA/JACK-Warnungen normal),
WSL-Guard-Patch nötig (Abschnitt 3).

## 7. CI, Installer, Releases

- **`develop.yml`** (Caller für `build.yml`, volle Matrix: Linux DEB 24.04 /
  macOS / Windows MSI x64+arm64) ist **im Repo deaktiviert**
  (`disabled_manually`, geprüft 2026-09-06). Ein Push löst also keinen Build mehr
  aus. Wer die Matrix braucht, muss den Workflow in den GitHub-Einstellungen erst
  wieder aktivieren; danach triggert er wieder auf **jeden** Push.
- **Windows-MSI:** aus der vollen Matrix; **unsigniert** (keine Signing-Secrets im
  Fork → SmartScreen-Warnung, trotzdem installierbar).
- **DEB für Ubuntu Studio 26.04:** eigener Workflow **`deb-only.yml`**
  (`workflow_dispatch`-only, Runner `ubuntu-26.04`, ohne Test-Gate, mit
  `-DWARNINGS_FATAL=OFF`). Grund: ein 24.04-DEB installiert sich auf 26.04
  **nicht** (neue Sonames: ffmpeg 8, Qt 6.10; die `*-private-abi (= x)`-Deps sind
  exakt gepinnt → nach Qt-Point-Update auf dem Zielsystem neu bauen).
  Anstoßen: `gh workflow run deb-only.yml -R RaikCC/mixxx --ref eta-notes`.
- **DEB lokal bauen — für Raiks eigenen Rechner der deutlich schnellere Weg.**
  Der Fork hat kein `debian/`-Verzeichnis, gepackt wird mit **CPack**, also genau
  dem, was `deb-only.yml` auch tut. Gemessen 2026-09-06 auf dem G9 (i5-12400,
  12 Threads): **9,3 min** Build + ~1 min `cpack` gegen **41–109 min** (Median
  ~90) für die letzten sieben CI-Läufe. Alle Optionen des Workflows lassen sich
  hier konfigurieren, `QML=ON` eingeschlossen.

  ```bash
  cmake -S . -B build-deb -G Ninja <Flags aus deb-only.yml>   # RelWithDebInfo, QML=ON, BULK=ON …
  cmake --build build-deb && (cd build-deb && cpack -G DEB)
  sudo apt-get install -y --reinstall ./build-deb/mixxx-*.deb
  ```

  Zwei Fallen: CPack leitet die Paketversion aus dem **Tag** ab, nicht aus
  `git describe` → sie ist identisch mit der installierten, ein `apt-get install`
  ohne **`--reinstall`** tut deshalb nichts. Und die Abhängigkeiten entstehen aus
  dem, was auf *dieser* Maschine gelinkt wurde — für fremde Rechner (T440s, W541)
  bleibt der CI-Build die sicherere Quelle. Ein eigenes Build-Verzeichnis nehmen,
  damit das schnelle Debug-Verzeichnis für die Entwicklung erhalten bleibt.
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
- Ein neues Signal in einem Header, eingefügt **während** ein Build läuft,
  führt zu `mold: undefined symbol Track::…Changed()`. Der moc-Code wird zwar
  neu erzeugt, das `.cpp` mit `#include "moc_….cpp"` aber nicht neu übersetzt.
  Einfach `ninja` noch einmal laufen lassen.
- `NotePointer` hat kein `operator=(nullptr_t)` → mit `.reset()` leeren.
- `QTextDocument::setTextWidth(-1)` heißt **nicht** „kein Umbruch" (fällt auf
  Default-Page-Size zurück) → für no-wrap große feste Breite setzen,
  Content-Breite via `idealWidth()`.
- `LibraryScanner` hat **eigene** DAO-Instanzen — neue DAOs dort separat
  verdrahten **und** initialisieren (vergessenes `initialize()` war ein echter Bug).
- `-R RaikCC/mixxx` bei `gh` trotzdem immer mitgeben: hier zeigt `origin` zwar
  schon auf RaikCC, auf der WSL-Maschine aber auf Upstream (Abschnitt 2).
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
