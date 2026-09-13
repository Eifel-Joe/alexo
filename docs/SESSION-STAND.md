# Sitzungsstand

## 2026-09-12 — Übersetzung des Projekts nach Deutsch

Spezifikation: `docs/specs/2026-09-12-uebersetzung-deutsch.md`
Plan: `docs/plans/2026-09-12-uebersetzung-deutsch.md`

**Gearbeitet wird ab jetzt ausschliesslich auf dem Branch `deutsch`.** Er trägt
die vollständige Übersetzung und ist seit dem 12.09.2026 der Standard-Branch
auf GitHub. `main` bleibt unangetastet als Anschluss an das Originalprojekt,
damit sich Änderungen von dort weiter zusammenführen lassen.

### Stand

Verifiziert:

- Dokumentation, Kommentare, sichtbare Texte und Laufzeitverhalten sind deutsch.
  `python tools/pruefe_sprache.py` meldet keine Reste und schlägt in der
  Gegenprobe bei einem eingebauten italienischen Satz an.
- Die Firmware übersetzt fehlerfrei: Flash 1477633 von 4194304 Byte, RAM 68596
  von 327680.
- Umlaute und Eszett sind auf dem Display darstellbar.
  `python tools/test_cp437.py` prüft die Zuordnungstabelle gegen den
  cp437-Codec von Python und meldet 21 stimmende Zuordnungen.
- Die Absichtserkennung vergleicht ganze Wörter.
  `python tools/test_wortgrenzen.py` prüft das im Quelltext nach und lässt
  21 deutsche Sätze gegen die nachgebildeten Regeln laufen; die Gegenprobe mit
  wieder eingebautem Fehler schlägt an.
- Das Web-Panel wurde lokal im Browser geöffnet: deutsch, Umlaute richtig,
  keine Fehler in der Konsole.
- Das Weckwort ist auf "Hey Jarvis" umgestellt. Die eingebetteten Bytes des
  Modells sind byteweise mit der Quelldatei aus `esphome/micro-wake-word-models`
  verglichen und identisch.
- Die Anschlusstabelle in `CABLAGGIO_HW.md` stimmt in beide Richtungen mit
  `include/config.h` überein.
- Alle 34 Sprungmarken im Handbuch und jeder Dateiverweis in der Dokumentation
  zeigen auf vorhandene Ziele.
- Eine Durchsicht vor dem Push hat 39 Punkte gemeldet, 30 davon nach dreifacher
  Gegenprüfung bestätigt. Alle 30 sind behoben, siehe die Commits
  "Zwei Fehler der Uebersetzung beheben" und "Die bestaetigten Funde der
  Durchsicht vor dem Push beheben".

Offen:

- **Der Test am Gerät.** Die Hardware war nicht verfügbar. Zu prüfen sind das
  Weckwort "Hey Jarvis" mit deutscher Aussprache, die Umlaute auf dem Display
  und ein vollständiger Wortwechsel auf Deutsch.
- **Der Senderkatalog** in `include/config.h` (`MUSIC_STATIONS_DEF`) und die
  Tabelle `CATALOG` in `src/music.cpp` tragen noch italienische Schlüsselwörter.
  Vom Betreiber zurückgestellt.
- **Die Stimmen-Kennung** ab Werk ist noch die des Originalprojekts. Der
  Betreiber wählt eine eigene Stimme im ElevenLabs-Konto und trägt sie im
  Web-Panel ein.

### Verworfen

- **Die Zeichenumwandlung in ein eigenes Modul auslagern.** Sie sollte einen
  Test auf dem PC ermöglichen. Auf dieser Maschine ist aber kein PC-Compiler
  installiert, nur die Xtensa-Toolchain, ein ausgelagertes Modul wäre also
  ebenso wenig ausführbar gewesen. Erkennbar an `g++ --version`, das nichts
  findet. Der Test prüft die Tabelle stattdessen im Quelltext.
- **PlatformIO aktualisieren, um den Build zu reparieren.** Half nicht, der
  Fehler blieb. Erkennbar daran, dass derselbe `KeyError` nach dem Wechsel von
  6.1.11 auf 6.2.0 weiter auftrat.
- **Die STM32-Ordner umbenennen, um sie auszublenden.** Wirkungslos, weil
  PlatformIO die Plattform am Manifest erkennt und nicht am Ordnernamen.
  Erkennbar daran, dass `pio pkg list -g --only-platforms` sie weiter auflistete.
- **Die ASCII-Umschreibungen über eine Sperrliste ersetzen.** Machte aus `true`
  ein `trü` und aus `0xae` ein `0xä`, weil dieselben Buchstabenpaare in
  englischen Wörtern, Bezeichnern und Hexzahlen vorkommen. Erkennbar am
  Compilerfehler `stray '\303' in program`. Ersetzt wird jetzt über eine
  Positivliste geprüften Wortguts.

### Fallen

- **Der Build scheiterte an zwei Altlasten der Umgebung, nicht am Projekt.**
  Erstens eine defekte Plattform `ststm32@10.0.1` von 2021, deren Manifest ein
  Paket nennt, das es selbst nicht führt; sie liegt beiseitegelegt und lässt
  sich durch Zurückschieben wiederherstellen. Zweitens ein volles Laufwerk. Der
  Paketordner für dieses Projekt liegt deshalb ausserhalb des Standardpfads.
  Das ist eine Eigenheit dieses einen Entwicklungsrechners und keine Anforderung
  des Projekts. **Dort braucht jeder Build diese Umgebungsvariable:**

  ```bash
  PLATFORMIO_CORE_DIR=<eigener Paketordner> pio run -e esp32-s3-devkitc-1
  ```

- **Die Zeilenenden.** Alle Quelldateien sind UTF-8 ohne BOM mit CRLF. Wer sie
  mit Werkzeugen bearbeitet, die LF schreiben, erzeugt einen Diff über die
  ganze Datei. Das Hilfsmodul im Scratchpad prüfte das nach jeder Ersetzung.
- **Backslashes in Bash-Heredocs.** Ein Heredoc frisst eine Ebene Backslashes,
  auch mit geschütztem Begrenzer. Ein doppelt geschriebenes Escape kommt
  einfach an und zerlegt damit C-Strings und mehrzeilige `#define`.
  Suchtexte mit Escape-Sequenzen deshalb meiden oder aus Zeichencodes
  zusammensetzen.
- **Die Konsolenausgabe täuscht bei Akzenten.** Eine Zeile, die im Terminal als
  `già` erscheint, steht in der Datei als `gia'`. Bei Suchtexten mit Akzenten
  den Inhalt über `repr()` prüfen, nicht über die Anzeige.
- **Abhängigkeiten zwischen Code und Web-Panel.** Das Skript in
  `data/index.html` vergleicht die Zustandstexte, die `src/webui.cpp` liefert,
  gegen feste Zeichenketten. Wer die einen ändert, muss die anderen mitziehen.
- **Deutsche Zusammensetzungen und Beugung.** Wo das Original Wörter als
  Teilzeichenkette suchte, traf im Deutschen "musik" in "Musiker", "lied" in
  "Mitglied" und "gut" in "guten Morgen". `tools/test_wortgrenzen.py` hält das
  fest; wer die Wortlisten erweitert, führt ihn aus.

### Nächste Schritte

1. Sobald die Hardware da ist: über USB flashen
   (`pio run -e esp32-s3-devkitc-1 -t upload`), dazu einmal das Dateisystem
   (`-t uploadfs`), weil `data/index.html` geändert wurde. Dann das Weckwort,
   die Umlaute auf dem Display und einen Wortwechsel auf Deutsch prüfen.
2. Spricht "Hey Jarvis" schlecht an: `WAKE_PROB_CUTOFF` im Web-Panel
   probeweise senken und dabei den Wert für das Weckwort in der Live-Anzeige
   beobachten. Der Werkswert 247 stammt aus dem Manifest des Modells.
3. Den Senderkatalog eindeutschen, falls gewünscht: die Schlüssel in
   `MUSIC_STATIONS_DEF` und der Tabelle `CATALOG` in `src/music.cpp`.
   Beobachtbares Kriterium: "spiel Rockmusik" muss den Rock-Sender liefern.
4. Eine eigene Stimme im ElevenLabs-Konto wählen und im Web-Panel eintragen.

### Empfohlene Skills für die Folgesitzung

- `task-loop` zu Sitzungsbeginn, für den Ablauf.
- `superpowers:systematic-debugging`, falls das Weckwort oder die Umlaute am
  Gerät nicht wie erwartet arbeiten.
- `pr-workflow` nur, falls etwas davon zurück ans Originalprojekt gehen soll.
  Die Übersetzung selbst gehört nicht dorthin.
