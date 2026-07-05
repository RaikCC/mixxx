# ETA Notes — Konzept (Spezifikation)

> Original-Konzeptdokument (Stand 2026-06-25, vor Implementierungsbeginn).
> §-Verweise in Commits und in [FORK.md](FORK.md) meinen die Abschnitte dieses
> Dokuments. Bewusste Abweichungen der Implementierung sind in FORK.md,
> Abschnitt 4, gelistet. Der Plugin-Weg (Abschnitt 3) wurde validiert und
> verworfen — es wurde der Fork-Weg umgesetzt.

## 1. Überblick & Ziel

**ETA Notes** ist eine geplante Funktionserweiterung für die Open-Source-DJ-Software **Mixxx** (C++, Quellcode auf GitHub, wird aktiv gepflegt). Sie ermöglicht es, zu einzelnen Tracks **frei platzierbare, an Zeitpunkte im Track gebundene Notizen** anzulegen und sie sowohl bei stehendem als auch bei laufendem Track sinnvoll auf der Waveform anzuzeigen.

Der Name ist Arbeitstitel und steht für **Estimated Time of Arrival**: Beim Abspielen wird zu jeder bevorstehenden Notiz angezeigt, in **wie vielen Takten** sie erreicht wird — der eigentliche Clou der Software (siehe Abschnitt 7).

**Nutzungskontext:** reines Vorbereitungs- und Live-Hilfswerkzeug für einen einzelnen Anwender (Raik), nicht für Veröffentlichung gedacht. Es muss nicht alle denkbaren Setups abdecken, sondern Raiks Workflow.

## 2. Kontext & Motivation

Raik arbeitet mit **komplex vorbereiteten Übergängen** und braucht die Möglichkeit, sich pro Track an bestimmten Stellen Hinweise zu hinterlegen (z. B. „hier Vocals rausnehmen", „hier guter 8-Takte-Loop zum Rauskommen").

Die vorhandenen Mittel reichen dafür nicht:
- **Hotcues** bieten kaum Platz für Text und sind auf **8 Stück** limitiert.
- Für einen reinen Hinweis (kein Sprungpunkt) eine Hotcue zu „verbrauchen", ist unwirtschaftlich.

Daraus ergibt sich der Bedarf nach einer eigenen Notiz-Ebene mit Textinhalt und zeitlicher Verankerung.

## 3. Vorab zu validieren (vor jeder Umsetzung)

Diese Punkte müssen **zuerst** geklärt werden, weil sie die Architektur bestimmen:

1. **Realisierbarkeit als Plugin/Add-on vs. Fork:**
   Bietet Mixxx eine Erweiterungsschnittstelle, die eine solche **Funktionserweiterung** (neue UI-Elemente auf der Waveform, eigenes Panel, Schreibzugriff auf Track-Metadaten) zulässt? Bekannt ist bisher nur, dass Mixxx das **Verändern von Skins** und das **Einbinden von Audio-Plugins** erlaubt — beides deckt diesen Funktionsumfang nicht ab.
   - **Wenn als Plugin/Add-on möglich:** bevorzugter Weg.
   - **Falls nicht:** Umsetzung als **eigener Branch/Fork** des Hauptrepos. Nachteil: höhere Komplexität, Updates müssen ggf. manuell nachgezogen werden. Akzeptabel, da eine gut funktionierende Version für den Eigengebrauch nicht zwingend jedes Mixxx-Update mitnehmen muss. Schöner wäre dennoch die Arbeit an offiziell vorgesehenen Schnittstellen.
2. **Metadaten-Speicherort:** Gibt es in der Mixxx-Datenbank pro Track ein Feld (z. B. JSON), das genau für solche zusätzlichen Daten vorgesehen ist und in das die Notizen geschrieben werden können?
3. **Track-Identität:** Hat jeder Track in der Datenbank eine **feste, eindeutige ID**? (Voraussetzung für die Deck-übergreifenden Notes, Abschnitt 8.)

## 4. Datenmodell — das Note-Objekt

Eine **Note** besteht mindestens aus:
- **Content-String** — der Notiztext. Muss **Zeilenumbrüche** akzeptieren und speichern. Die konkrete Kodierung des Umbruchs (`\n` vs. Carriage Return o. Ä.) ist Implementierungsdetail.
- **Zeitliche Position (Timecode)** — der Punkt im Track, zu dem die Note gehört, in einer Mixxx-nativen Zeit-/Positionsangabe.
- **Optionale Track-Referenz** — die ID eines **anderen** Tracks, auf den sich die Note bezieht (für Übergangs-Notes, Abschnitt 8). Default: keine Referenz (Note bezieht sich auf den eigenen Track).
- **Markdown-Formatierung:** Der Content soll **einfaches Markdown** erlauben, mindestens **kursiv** und **fett**.

Mehrere Notes pro Track sind möglich. Gespeichert werden die Note-Objekte in den **Metadaten / der Datenbank des jeweiligen Tracks**, an dem sie hängen.

## 5. Speicherung

Die Notes werden in den **Track-Metadaten / der Mixxx-Datenbank** des zugehörigen Tracks abgelegt (abhängig von Punkt 2 der Validierung). So sind sie an den Track gebunden und stehen zur Verfügung, sobald er auf ein Deck geladen wird.

## 6. Anzeige bei **stehendem** Track (Vorbereitungsansicht)

Wenn ein Track auf einem Deck liegt und **nicht spielt**, werden seine Notes **auf der Waveform** an ihrem Timecode angezeigt:

- **Form:** ein **horizontales Rechteck** (kein quadratischer Notizzettel), vertikal nur so hoch wie nötig.
- **Position:** Die **linke Kante** der Note sitzt genau an ihrem Timecode in der Waveform.
- **Stapelung (Stacking):** Notes orientieren sich **standardmäßig oben** (Y-Richtung). Würden sich zwei Notes überschneiden (gleicher Zeitpunkt oder grafische Überlappung durch Länge), rutscht die **spätere unter** die frühere. Sie dürfen **niemals über die Waveform hinauslaufen** (nie vertikal darüber), sondern stacken innerhalb.
- **Dynamik:** Die Anordnung aktualisiert sich. Wird eine obere Note gelöscht, **rutscht die darunterliegende nach oben**, sobald oben Platz ist (gewünschtes Verhalten — immer möglichst weit oben).
- **Höhe** hängt von Schriftart, Schriftgröße und Zeilenumbrüchen ab (Schriftgröße = Setting, Abschnitt 9).

### Bearbeiten direkt auf der Waveform
- **Anlegen:** an eine Stelle der Waveform klicken, dann über einen **Einfüge-Button** (neben den Buttons zum Grid-Verschieben, oder per Rechtsklick) eine Note an dieser Stelle erzeugen — direkt editierbar.
  - **Quantisierung aus:** Note landet exakt an der Mausposition.
  - **Quantisierung an:** Note rastet auf den **nächsten Grid-Marker** ein (analog zum Verhalten anderer Mixxx-Elemente).
- **Verschieben:** Notes per **Drag & Drop** verschiebbar. Quantisierung an → einrasten auf Grid-Marker; Quantisierung aus → frei (so fein die Grafik es zulässt).
- **Editieren:** in den angezeigten Content **klicken** öffnet sofort die Inline-Bearbeitung (Eingabemodus direkt aktiv). **Rausklicken** beendet die Bearbeitung und **speichert**. Feeling vergleichbar mit einem **n8n-Klebezettel**.
- **Löschen:** jede Note trägt ein kleines, **doppelt zu bestätigendes** Lösch-Symbol.
- **Track-Referenz setzen:** beim Klick auf das Note-Element (dort, wo auch das Lösch-Symbol sitzt) ein **Dropdown** (Abschnitt 8).

## 7. Anzeige bei **laufendem** Track — die Live-ETA-Ansicht (Kernfunktion)

Spielt ein Track, werden die Notes **nicht mehr an ihrem Timecode** auf der Waveform angezeigt, sondern als **Vorausschau an der Play-Position (Mitte)**:

- **Vorschaufenster:** ein in **Takten** definierter Bereich (Setting), z. B. 64 Takte. Angezeigt werden alle Notes, die innerhalb der nächsten *n* Takte beginnen — **auch solche, die in der normalen Waveform-Vorschau noch nicht sichtbar wären** (das Vorschaufenster kann weiter reichen als die Waveform-Darstellung).
- **Anordnung:** Notes stapeln sich **von oben nach unten** wie in der stehenden Ansicht, liegen aber **horizontal alle an der Play-Position** (Spielmarker) an.
- **Takt-Countdown:** Jeder Note wird ein **quadratisches Feld** (Höhe der Note) vorangestellt, in dem **mittig** die Zahl der noch verbleibenden Takte steht — als **Ganzzahl, ohne Nachkommastellen**. Dahinter folgt der Note-Content (lesbar, was in *n* Takten ansteht). Während des Abspielens werden die Zahlen kleiner.
- **Grafischer Annäherungs-Indikator (Kontrastfarbe):** Zusätzlich zur Zahl füllt sich die Fläche **von rechts nach links** in der **Kontrastfarbe** (Waveforms laufen von rechts herein und links hinaus). Erfasst wird die **Gesamtbreite** aus Countdown-Feld + Note-Content; diese läuft **proportional** zur Restzeit voll und färbt Hintergrund und Schrift von rechts ein (lesbar dank definierter Kontrast-Schriftfarbe).
  - Beispiel bei 64 Takten Vorschau: 48 Takte entfernt → 75 % links neutral, 25 % rechts Kontrast. 32 Takte → 50/50. 16 Takte → 25 % neutral, 75 % Kontrast. Im letzten Takt steht im Indikator **0**; in dem Moment, in dem die Note-Position durch die Play-Position läuft, ist die Leiste **voll**.
- **Nachleuchten:** Ist die Note durchgelaufen, wird sie **leicht transparent** (ca. 66–75 %) und bleibt noch eine **in Takten definierte Zeit** (Setting) stehen, bevor sie ausgeblendet wird.

**Nutzen:** Raik sieht lange vorher, wann eine vorbereitete Aktion ansteht (z. B. „hier 8-Takte-Loop setzen") und kann sie im richtigen Moment auslösen, ohne mitzählen zu müssen. Daher der Name „ETA Notes".

## 8. Deck-übergreifende Notes (Übergangs-Notes)

Die Note-Hintergrundfarben sind **pro Deck 1–4 individuell** einstellbar (Abschnitt 9), zusätzlich eine Farbe für **deckeigene** Notes. Grund:

Eine Note, die in den Metadaten von Track A liegt, kann eine **Referenz auf einen anderen Track B** (per dessen ID) tragen. Verhalten:
- Die Note wird **nur dann** angezeigt, wenn der referenzierte Track B **zur Abspielzeit auf einem der anderen Decks** liegt. Liegt B nirgends, wird die Note **nicht** angezeigt.
- Wird sie angezeigt, erhält sie das **Farbschema des Decks, auf dem Track B liegt** (nicht das eigene). So ist sofort erkennbar: diese Note betrifft das **Zusammenspiel** mit dem Track auf jenem Deck.
- **Konfliktauflösung:** Liegt Track B auf **mehreren** Decks, wird die Farbe des **kleinsten Deck-Index** verwendet. (Exotischer Fall; wichtig ist nur, dass das Verhalten definiert ist.)

**Konkretes Beispiel:** Deck 1 spielt „No Fear" (Carla Bloom), Deck 2 hat „Enigma" geladen. In „No Fear" liegt eine Note mit Referenz auf die ID von „Enigma" und dem Hinweis, an dieser Stelle den Vocal-Stem von „No Fear" zu muten — aber nur, wenn der Übergang zu „Enigma" gefahren wird. Die Note liegt korrekt auf „No Fear" (dort wird gemutet), erscheint aber nur, wenn „Enigma" auf einem anderen Deck liegt, und im Farbschema von Deck 2. In den Text gehören Hinweise auf Cues etc. → der Content muss entsprechend Platz bieten.

### Track-Referenz beim Editieren wählen
Beim Bearbeiten einer Note ein **Dropdown** (neben dem Lösch-Symbol):
- Erster, vorausgewählter Eintrag: **„Auf diesen Track beziehen"** (deckeigene Note — Default beim Anlegen).
- Darunter die aktuell **geladenen** Tracks der anderen Decks, im Format `Deck 2: Trackname - Artist`.
- Decks **ohne** geladenen Track erscheinen **nicht** im Dropdown.

## 9. Settings-Panel

Das Plugin hat **ein einziges** eigenes Fenster: das **Settings-Panel**. Das **Anlegen, Editieren, Verschieben und Löschen der Notes** passiert ausschließlich direkt auf den Waveforms (Abschnitt 6) — **nicht** in diesem Panel. Das Panel dient allein den Einstellungen; das reine **Lesen** der Notes geschieht ohnehin in der normalen GUI.

- Ein **kleiner Button im Dashboard**, nur per **Maus** anklickbar, öffnet das Settings-Panel als **eigenes Fenster**.
- Es ist ein **Vorbereitungswerkzeug** und setzt **Maus + Tastatur** voraus; während des Spielens wird es nicht gebraucht (Öffnen darf erlaubt sein, ist aber nicht nötig).
- Der Dashboard-Button bleibt auch dann erreichbar, wenn die Notes-Anzeige über den Hauptschalter komplett ausgeschaltet ist.

*(Umsetzungshinweis: Da der Fork-Weg gewählt wurde, gibt es keinen eigenen
Dashboard-Button — die Einstellungen sind eine reguläre Seite im
Mixxx-Einstellungsdialog, „ETA Notes", letzte Seite im Baum.)*

**Einstellungen im Panel:**
- **Sichtbarkeits-Hauptschalter:** schaltet die **Komplettanzeige** der Notes auf den Tracks an/aus. Ausgeschaltet ist vom Plugin **nichts** mehr zu sehen — außer dem Dashboard-Button zum Öffnen des Settings-Panels.
- **Farbcodes (20 Stück):** Für **5 Fälle** (Deck 1, Deck 2, Deck 3, Deck 4 sowie **deckeigene** Notes) jeweils:
  - 2 **Hintergrundfarben** der Note-Objekte (normal + Kontrast),
  - 2 **Schriftfarben** (normal + Kontrast).
  - = 5 × 4 = **20 Farbcodes**, mit sinnvollen **Default-Werten**. (Die Kontrastfarben werden für den Annäherungs-Indikator aus Abschnitt 7 gebraucht.)
- **Schriftgröße:** eine einzige Variable für die Notes.
- **Vorschaufenster** (Live-ETA): Länge in **Takten** (z. B. 64).
- **Note-Breite beim Abspielen:** feste Breite in **Pixeln**. Notwendig, damit der proportionale Farbverlauf **note-übergreifend** gleich läuft (bei unterschiedlich breiten Notes liefe der Indikator je Note anders voll).
- **Nachleuchtdauer:** wie lange (in Takten) eine durchgelaufene Note transparent stehen bleibt.
- Weitere Settings nach Bedarf bei der Ausdefinition.

## 10. Edge Cases & definiertes Verhalten

- **Überschneidung/Stacking:** siehe Abschnitt 6 — Notes stacken nach unten, nie über die Waveform; obere Lücken werden dynamisch wieder aufgefüllt. Gilt für stehende **und** Live-ETA-Ansicht. Stapeln Notes über den unteren Rand der Waveform hinaus, werden die unteren ggf. nicht mehr angezeigt (Verantwortung des Vorbereitenden, das knapp zu halten).
- **Zu langer Content:** Ist der definierte horizontale Bereich kleiner als eine Zeile lang wäre, darf der Content **nicht abgeschnitten** werden, sondern wird **zusätzlich umgebrochen**, sodass alles lesbar bleibt. (Zu kurzer Content ist unkritisch — rechter Bereich bleibt leer.) Es liegt in der Verantwortung des Vorbereitenden, Breite und Note-Länge sinnvoll einzustellen; das Tool muss aber ein definiertes Verhalten haben.
- **Konflikt Mehrfach-Deck** bei Track-Referenz: kleinste Deck-Nummer (Abschnitt 8).

## 11. Plattformen

OS-übergreifend, soweit Mixxx es ist (Linux, Windows, vermutlich macOS). **Mindestens Linux und Windows** müssen für Raiks Anwendung funktionieren.

## 12. De-Installation

Lässt das Plugin sich sauber installieren, muss es sich auch **restlos deinstallieren** lassen — inkl. einer Routine, die die vom Plugin geschriebenen **Metadaten wieder aus der Datenbank entfernt**. Entfällt im Fork-Fall (dort sind die Daten ohnehin Teil der eigenen Version und bleiben).
