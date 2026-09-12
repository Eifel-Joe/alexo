# Spezifikation: Alexo von Italienisch nach Deutsch

Datum: 2026-09-12
Branch: `feat/uebersetzung-deutsch`
Repo: Fork von `PeppeMinniti/alexo` nach `Eifel-Joe/alexo`

## Problem

Das Projekt ist durchgaengig italienisch: Dokumentation, Code-Kommentare,
sichtbare Texte und das Laufzeitverhalten des Assistenten. Der Betreiber
arbeitet auf Deutsch und will ein deutschsprachiges Geraet.

## Anforderungen

1. **Laufzeitverhalten** - das Geraet hoert und antwortet auf Deutsch.
   Betroffen: System-Prompt, Whisper-Sprachcode, Wochentage fuer die
   Zeitangabe an Claude, Beschreibung des Musik-Werkzeugs.
2. **Halluzinationsfilter** - Whisper erfindet bei Stille Phrasen. Die
   hinterlegte Liste ist italienisch und muss durch die bekannten deutschen
   Entsprechungen ersetzt werden. Eine woertliche Uebersetzung waere
   wirkungslos, weil Whisper auf Deutsch andere Phrasen erfindet.
3. **Umlaute auf dem Display** - die Zeichentabelle in `src/gobbo.cpp`
   kennt nur italienische Akzente. Umlaute und Eszett landen auf `?`.
4. **Sichtbare Texte** - Display-Ausgaben, Web-Panel (`data/index.html`),
   Log-Ausgaben.
5. **Kommentare** - rund 5100 Zeilen dicht kommentierter eigener Code.
6. **Dokumentation** - 2106 Zeilen Markdown in fuenf Dateien.

## Entscheidungen

| Frage | Entscheidung | Begruendung |
|---|---|---|
| Bezeichner, Datei- und Ordnernamen | bleiben italienisch | Das Repo ist ein Fork. Umbenennungen machen jede kuenftige Uebernahme vom Originalprojekt unbrauchbar, ohne dem Betrieb zu nutzen. |
| ElevenLabs-Stimme | Kennung bleibt | Vom Betreiber so entschieden. Das Sprachmodell `eleven_flash_v2_5` ist mehrsprachig, die Stimme spricht also Deutsch. Im Web-Panel jederzeit aenderbar. |
| Radio-Katalog | unveraendert | Vom Betreiber zurueckgestellt. Siehe offene Punkte. |
| Umlaut-Umwandlung | in eigenes Modul ziehen | Die Funktionen sind privat in `gobbo.cpp` und ohne Hardware nicht pruefbar. Ausgelagert sind sie der einzige Teil der Aufgabe, der sich automatisiert testen laesst. |
| Weckwort | gewechselt auf "Hey Jarvis" | Waehrend der Arbeit vom Betreiber gewuenscht. Das fertige Modell stammt aus demselben Katalog wie das bisherige. Die Schwelle steigt dabei von 242 auf 247, sie gehoert zum Modell. |
| Persoenlichkeit | foermlich und knapp, Anrede "Sir" | Ebenfalls waehrend der Arbeit gewuenscht. Nur der System-Prompt, kein Eingriff in die Ablaeufe. |

## Nicht Teil dieser Aufgabe

- Radio-Katalog `MUSIC_STATIONS_DEF` in `include/config.h`
- Stimmen-Kennung `ELEVEN_VOICE_DEF`
- Alle Bezeichner, Datei- und Ordnernamen (`gobbo`, `IMMAGINI`, STL-Dateien,
  der Werkzeugname `riproduci_musica`, den Claude sieht)
- Fremdbibliothek unter `lib/microfrontend` (englisch, Google-Code)
- Generierte Modelldaten `src/wake_model.h`
- Vorhandene Sicherungsdateien im Repo (`*.bak`, `*.prima_di_mycroft`)
- Die Beschreibung des Repos auf GitHub

## Ende-zu-Ende-Kriterium

Nach dem Flashen auf die Hardware: Das Geraet wird auf Deutsch gefragt,
antwortet auf Deutsch, und das Display zeigt Umlaute korrekt statt
Fragezeichen.

Die Hardware steht laut Betreiber erst spaeter zur Verfuegung. Bis dahin
gelten die folgenden Ersatzkriterien.

## Verifikation ohne Hardware

| Kriterium | Nachweis |
|---|---|
| Firmware laesst sich uebersetzen | `pio run -e esp32-s3-devkitc-1` endet fehlerfrei |
| Umlaute werden richtig umgesetzt | Test auf dem PC, von Rot nach Gruen, prueft `Gruesse` mit Umlauten gegen die erwarteten Zeichenwerte |
| Firmware passt weiter in die Partition | Groessenangabe des Builds vor und nach dem Umbau |
| Web-Panel stellt sich richtig dar | `data/index.html` lokal im Browser geoeffnet und geprueft |
| Keine italienischen Reste | Wortlistensuche ueber die behandelten Dateien |

Belegte Zeichenwerte fuer die Tabelle, gegen Python `cp437` geprueft:
`ae 0x84`, `oe 0x94`, `ue 0x81`, `Ae 0x8E`, `Oe 0x99`, `Ue 0x9A`, `ss 0xE1`.
Der Code ruft `cp437(true)` auf (`src/gobbo.cpp:588`, `src/main.cpp:511`),
die Glyphenzuordnung stimmt also.

## Aenderungen waehrend der Umsetzung

- **Die Auslagerung der Zeichenumwandlung entfiel.** Sie sollte einen Test auf
  dem PC ermoeglichen, aber auf dieser Maschine ist kein PC-Compiler
  installiert, nur die Xtensa-Toolchain. Der Test prueft die Zuordnungstabelle
  stattdessen gegen den cp437-Codec von Python, was denselben Fehlertyp faengt
  und den Eingriff kleiner haelt.
- **Die Ordnungszahl-Logik in der Sprachausgabe wurde entfernt** (rund 150
  Zeilen), nach Freigabe. Im Italienischen schrieb man "21° secolo" mit dem
  Gradzeichen und unterschied anhand des folgenden Wortes; im Deutschen steht
  das Gradzeichen immer fuer Grad. Waere die Liste italienischer Folgewoerter
  geblieben, haette "30 Grad im Schatten" als Ordnungszahl gegolten.
- **Die Erkennung von Musikbefehlen wurde doch uebersetzt**, obwohl der Radio-
  Block zurueckgestellt ist. Sie prueft auf Absichtswoerter, und die waren
  italienisch: ohne Uebersetzung haette das Geraet auf Deutsch ueberhaupt
  keinen Musikwunsch verstanden. Der Senderkatalog selbst blieb unangetastet.

## Offene Punkte

- **Geraetetest steht aus**, bis die Hardware da ist. Zu pruefen sind das
  Weckwort "Hey Jarvis" mit deutscher Aussprache, die Umlaute auf dem Display
  und ein vollstaendiger Wortwechsel auf Deutsch.
- **Radio-Katalog**: die Schluesselwoerter sind italienisch (`ottanta`,
  `anni 80`). Erkannt wird der Musikwunsch bereits auf Deutsch, die Auswahl des
  Genres laeuft aber weiter ueber die italienischen Schluessel, und das Display
  zeigt italienische Sendernamen.
- **Stimmen-Kennung**: ab Werk noch die des Originalprojekts. Eine eigene
  Stimme waehlt der Betreiber im ElevenLabs-Konto und traegt sie im Web-Panel
  ein.

## Nachweise am Ende der Arbeit

| Kriterium | Ergebnis |
|---|---|
| Firmware laesst sich uebersetzen | `pio run` endet mit SUCCESS, Flash 1476973 von 4194304 Byte, RAM 68596 von 327680 |
| Umlaute werden richtig umgesetzt | `python tools/test_cp437.py` meldet 21 stimmende Zuordnungen, alle deutschen Zeichen vorhanden |
| Keine italienischen Reste | `python tools/pruefe_sprache.py` meldet keine; Gegenprobe mit einem eingebauten italienischen Satz schlug an |
| Web-Panel stellt sich richtig dar | lokal im Browser geoeffnet, deutsch, Umlaute richtig, keine Fehler in der Konsole |
| Verweise in der Dokumentation | alle 34 Sprungmarken im Handbuch und jeder Dateiverweis geprueft |
| Anschlusstabelle | stimmt in beide Richtungen mit `include/config.h` ueberein |
