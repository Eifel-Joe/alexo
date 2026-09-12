# Umsetzungsplan: Alexo nach Deutsch

Spezifikation: `docs/specs/2026-09-12-uebersetzung-deutsch.md`
Branch: `deutsch`

**Ziel:** Dokumentation, Kommentare, sichtbare Texte und Laufzeitverhalten
von Italienisch nach Deutsch, ohne Bezeichner und Dateinamen anzufassen.

**Vorgehen:** Erst das Verhalten, dann die sichtbaren Texte, zuletzt
Kommentare und Dokumentation. Ein Commit pro abgeschlossenem Task.

**Randbedingungen, die jeder Task einhält:**
- Dateien sind UTF-8 ohne BOM mit CRLF. Beides bleibt erhalten.
- Bezeichner, Dateinamen, Ordnernamen, URLs und API-Feldnamen werden nicht
  verändert.
- Nach jedem Task: `PLATFORMIO_CORE_DIR=C:/Users/Nutzer/.platformio-alexo pio run -e esp32-s3-devkitc-1`

---

## Abweichung vom ursprünglichen Design

Das Design sah vor, die Zeichenumwandlung aus `src/gobbo.cpp` in ein
eigenes Modul zu ziehen, um sie testbar zu machen. Auf dieser Maschine ist
kein Compiler für den PC installiert, nur die Xtensa-Toolchain für den
ESP32. Ein ausgelagertes Modul wäre damit genauso wenig ausführbar wie
das jetzige.

Der Test prüft deshalb die Zuordnungstabelle im Quelltext gegen den
`cp437`-Codec von Python. Er fängt genau den Fehlertyp, um den es geht:
fehlende oder falsche Zeichenwerte. Die Auslagerung entfällt, damit der
Eingriff klein bleibt und künftige Übernahmen vom Originalprojekt
möglich bleiben.

---

## Task 1: Umlaute auf dem Display

**Dateien:**
- Test anlegen: `tools/test_cp437.py`
- Ändern: `src/gobbo.cpp:123-151` (`cpFromUnicode`)

- [ ] **Schritt 1: Test schreiben**

`tools/test_cp437.py` liest `src/gobbo.cpp`, zieht alle Zeilen der Form
`case 0xXXXX: return 0xYY;` aus `cpFromUnicode`, und prüft:

1. Jede Zuordnung stimmt mit Pythons `cp437`-Codec überein.
2. Alle deutschen Sonderzeichen sind abgedeckt: `ä ö ü Ä Ö Ü ß`.

- [ ] **Schritt 2: Test laufen lassen, Fehlschlag sehen**

`python tools/test_cp437.py`
Erwartet: FEHLER, sieben deutsche Zeichen fehlen in der Tabelle.

- [ ] **Schritt 3: Tabelle ergänzen**

In `cpFromUnicode` nach den italienischen Fällen einfügen:

```c
    case 0x00E4: return 0x84;  // ä
    case 0x00F6: return 0x94;  // ö
    case 0x00FC: return 0x81;  // ü
    case 0x00C4: return 0x8E;  // Ä
    case 0x00D6: return 0x99;  // Ö
    case 0x00DC: return 0x9A;  // Ü
    case 0x00DF: return 0xE1;  // ß
```

- [ ] **Schritt 4: Test laufen lassen, Erfolg sehen**

`python tools/test_cp437.py`
Erwartet: OK, alle Zuordnungen stimmen.

- [ ] **Schritt 5: Bauen und committen**

---

## Task 2: Laufzeitverhalten

**Dateien:**
- `include/config.h` — `SYSTEM_PROMPT_DEF`, `HALLUC_TERMS_DEF`, `VOICE_TRIGGER_DEF`
- `src/main.cpp:386` und `include/stt.h:10` — Sprachcode `it` nach `de`
- `src/net.cpp:32-33` — Wochentage
- `include/net.h:20` — Beispiel in der Beschreibung
- `src/llm.cpp:60-66` — Beschreibung des Musik-Werkzeugs

**Prüfkriterium:** Build fehlerfrei; Suche nach `"it"` als Sprachcode
liefert keinen Treffer mehr.

Der Halluzinationsfilter wird nicht übersetzt, sondern ersetzt. Whisper
erfindet auf Deutsch andere Phrasen als auf Italienisch, typisch sind
Untertitel-Abspänne und Verabschiedungen. Vergleich erfolgt laut Kommentar
in Kleinschreibung ohne Satzzeichen an den Rändern.

Die Zeitzone `Europe/Rome` und der Satz "Der Nutzer ist in Italien" werden
auf Deutschland umgestellt.

---

## Task 3: Sichtbare Texte auf dem Gerät

**Dateien:** `src/gobbo.cpp`, `src/main.cpp`, `src/ui.cpp`, `src/music.cpp`,
`src/tts.cpp`, `src/mic.cpp`, `src/wakeword.cpp`, `src/tfltest.cpp`,
`src/netlog.cpp`, `src/net.cpp`, `src/stt.cpp`, `src/llm.cpp`,
`src/localai.cpp`, `src/settings.cpp`, `src/webui.cpp`, `src/volume.cpp`,
`src/sound.cpp`, `src/encoder.cpp`

Alle Zeichenketten, die auf dem Display oder über die serielle
Schnittstelle erscheinen. Nicht angefasst: Zeichenketten, die als
Schlüssel, Dateiname, URL, JSON-Feld oder Vergleichswert dienen.

**Prüfkriterium:** Build fehlerfrei, Firmwaregröße in derselben
Größenordnung.

---

## Task 4: Web-Panel

**Datei:** `data/index.html`

Sichtbare Beschriftungen, Hilfetexte und Meldungen. Nicht angefasst:
`id`- und `name`-Attribute, da `src/webui.cpp` und `src/settings.cpp`
darauf zugreifen.

**Prüfkriterium:** Datei lokal im Browser geöffnet, Darstellung geprüft,
keine Fehler in der Konsole.

---

## Task 5: Kommentare im Code

**Dateien:** alle `src/*.cpp` und `include/*.h` außer `src/wake_model.h`,
`src/tfl_hello_model.h` und allem unter `lib/`.

**Prüfkriterium:** Build fehlerfrei; Wortlistensuche findet keine
italienischen Füllwörter mehr.

---

## Task 6: Dokumentation

**Dateien:** `README.md`, `MANUALE.md`, `WAKEWORD.md`, `CABLAGGIO_HW.md`,
`CLAUDE.md`

Verweise auf Dateinamen und Ordner bleiben unverändert, da diese
italienisch bleiben. Der veraltete Kommentar in `platformio.ini:44` nennt
noch "Okay Nabu" statt "Hey Mycroft" und wird dabei richtiggestellt.

**Prüfkriterium:** Alle internen Verweise zeigen weiterhin auf
existierende Dateien und Abschnitte.

---

## Offen nach Abschluss

- Gerätetest, sobald die Hardware da ist.
- Radio-Katalog in `include/config.h`, vom Betreiber zurückgestellt.
