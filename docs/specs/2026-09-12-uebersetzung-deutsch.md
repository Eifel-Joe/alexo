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
| Weckwort | bleibt "Hey Mycroft" | Fest trainiertes TensorFlow-Modell, ohne neues Modell nicht aenderbar. |

## Nicht Teil dieser Aufgabe

- Radio-Katalog `MUSIC_STATIONS_DEF` in `include/config.h`
- Stimmen-Kennung `ELEVEN_VOICE_DEF`
- Alle Bezeichner, Datei- und Ordnernamen (`gobbo`, `IMMAGINI`, STL-Dateien)
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

## Offene Punkte

- Geraetetest steht aus, bis die Hardware da ist.
- Radio-Katalog: Stichwoerter sind italienisch (`ottanta`, `anni 80`). Bis
  zur Ueberarbeitung waehlt Claude weiterhin aus dem italienischen Katalog
  und das Display zeigt italienische Sendernamen.
