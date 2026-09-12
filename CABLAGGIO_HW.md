# ALEXO — Anleitung zur Verdrahtung

Vollständige Übersicht **aller** Verbindungen von Anschluss zu Anschluss zwischen den
Bauteilen, samt Kondensatoren, Widerständen, den unterschiedlichen Beschriftungen auf
den Modulen und den bewährten Regeln. Keine Zeichnungen aus Textzeichen, nur Listen von
Anschluss zu Anschluss.

Woher die Anschlüsse stammen: [`include/config.h`](include/config.h), die **einzige
maßgebliche Stelle**. Änderst du eine Verbindung, ändere die `#define` dort und nirgends
sonst.

---

## Grundsätzliches (gilt überall)

- **Die Anschlüsse des ESP32-S3 arbeiten mit 3,3 V.** Nie 5 V an einen GPIO legen. Die
  Module werden je nach Bauteil aus dem Anschluss `5V` (auch `VIN` oder `VBUS` genannt)
  oder aus `3V3` der Platine versorgt; bei jedem Bauteil steht unten, was gilt.
- **Eine gemeinsame Masse ist Pflicht.** Alle Masseanschlüsse aller Module (Mikrofon,
  VS1053, Display, Ring, Drehgeber) müssen auf derselben Masse des ESP32 landen. Ohne
  gemeinsame Masse "tanzen" die Signale und nichts arbeitet. Das ist die goldene Regel:
  verhält sich ein Modul seltsam, ist der erste Verdacht immer eine fehlende oder
  wackelige Masse.
- **Anschlüsse, die auf dieser Platine (ESP32-S3 N16R8) tabu sind**: GPIO 33 bis 37
  (der Octal-PSRAM), 19 und 20 (USB), 0, 3, 45 und 46 (Strapping). Die Konfiguration
  meidet sie bereits.

---

## Mikrofon — ZWEI Möglichkeiten (angeschlossen wird nur EINE)

Die Wahl trifft `MIC_USE_I2S` in [`config.h`](include/config.h): `1` steht für das
digitale I2S-Mikrofon ICS-43434 (**derzeit in Betrieb**), `0` für das analoge MAX4466
als Alternative. Die Softwareschnittstelle ist dieselbe, du änderst also nur die Zeile,
übersetzt neu und verdrahtest um.

### Möglichkeit A — MAX4466 analog (die Alternative, `MIC_USE_I2S 0`)

| Anschluss am MAX4466 | Kommt an | Hinweis |
|---|---|---|
| `VCC` (`VDD` / `V` / `+`) | **3V3** des ESP32 | **Nicht** 5 V: bei 5 V liegt der Ruhepegel außerhalb des Bereichs, den der Analogeingang verarbeitet |
| `GND` (`G` / `-`) | gemeinsame Masse | — |
| `OUT` (`AUD` / `A0` / `S`) | **GPIO4** (ADC1_CH3) | der analoge Ausgang |

**Kondensator (wichtig und im Entwurf bereits vorgesehen):** ein **Elektrolytkondensator
mit 470 µF zwischen VCC und GND des MAX4466**, so nah wie möglich am Modul. Er entkoppelt
die Versorgung: ohne ihn verschmutzen die NeoPixel beim Einschalten die Spannung, und das
Mikrofon fängt ihre Störungen auf, sodass das Reagieren bei Ruhe nicht arbeitet.
Polung: das lange Bein (+) an VCC, das kurze Bein beziehungsweise die Seite mit dem
hellen Streifen und dem Zeichen `–` an GND.

Die Verstärkung stellt man am **Trimmer auf der Rückseite** des MAX4466 ein (kleiner
Schraubendreher), ein äußerer Widerstand ist nicht nötig.

### Möglichkeit B — digitales I2S-Mikrofon ICS-43434 / INMP441 (IN BETRIEB, `MIC_USE_I2S 1`)

Ein digitales Mikrofon: kein Analogeingang, keine Entkopplungskondensatoren, und die
Einkopplung von den LED ins Mikrofon verschwindet vollständig. Versorgung mit **3V3**.

| Anschluss am I2S-Modul | Kommt an | Mögliche Beschriftungen |
|---|---|---|
| Versorgung | **3V3** | `VDD`, `VCC`, `3V3`, `+` |
| Masse | gemeinsame Masse | `GND`, `G`, `-` |
| Bittakt | **GPIO5** | `SCK`, `BCLK`, `CLK`, `SCLK` |
| Wortauswahl | **GPIO6** | `WS`, `LRCL`, `LRCLK`, `FS` |
| Daten vom Mikrofon | **GPIO7** | `SD`, `DOUT`, `DATA`, `DO` |
| Kanalwahl | **GND** | `L/R`, `SEL`, `LRSEL` → an **GND ergibt den linken Kanal**, den der Code erwartet |

> Auf dem INMP441 kann es `VDD` und `GND` doppelt geben: verbinde in jedem Fall alle
> Masseanschlüsse. Der Anschluss `L/R` an Masse wählt links; an 3V3 wählte er rechts, das
> ist zu vermeiden.

**Bewährte Regeln für I2S:**
- Äußere Widerstände oder Kondensatoren sind nicht nötig.
- Nach dem Wechsel muss die Schwelle für das Reagieren bei Ruhe **neu abgestimmt**
  werden (`MIC_LVL_NOISE` und `MIC_LVL_GAIN` im I2S-Zweig von
  [`src/mic.cpp`](src/mic.cpp)), ebenso `I2S_SHIFT` in der Konfiguration (höher bedeutet
  lauter, es ist die Umsetzung von 32 auf 16 Bit).
- Halte die drei Datenleitungen (SCK, WS, SD) kurz und fern von den Ton- und
  SPI-Leitungen.

---

## VS1053 (MP3-Decoder zum Lautsprecher, am SPI-Bus)

Dieses Modul trägt die meisten unterschiedlichen Aufdrucke. Zu jeder Funktion stehen
**alle** gängigen Beschriftungen (LC Technology, Adafruit, Sparkfun, Nachbauten):

| Funktion / Name im Code | GPIO am ESP32 | Mögliche Beschriftungen |
|---|---|---|
| **Versorgung** | `5V` | `5V`, `VCC`, `V+`, `VIN` — die Module von LC Technology wollen **5 V**, sie haben einen eigenen Regler |
| **Masse** | GND | `GND`, `G`, `-` |
| **SPI-Takt (SCK)** | `GPIO12` | `SCK`, `SCLK`, `CLK`, `C` |
| **Daten zum VS1053 (MOSI)** | `GPIO11` | `MOSI`, `SI`, `DI`, `SDI`, `DIN` |
| **Daten vom VS1053 (MISO)** | `GPIO13` | `MISO`, `SO`, `DO`, `SDO`, `DOUT` |
| **Chip Select für Befehle (XCS)** | `GPIO10` | `XCS`, `CS`, `SCS`, `xCS` |
| **Chip Select für Daten (XDCS)** | `GPIO21` | `XDCS`, `DCS`, `BSYNC`, `SDCS`, `X-DCS` |
| **Data Request (DREQ)** | `GPIO18` | `DREQ`, `DREQ/INT`, `DQ`, `D-REQ` — ein **Ausgang** des VS1053 und damit ein Eingang für den ESP32: "ich bin bereit für weitere Daten". Von GPIO47 auf **GPIO18** verlegt |
| **Reset (XRST)** | `GPIO8` | `XRST`, `RST`, `RESET`, `XRESET`, `RES` — von GPIO38 verlegt, dort sitzt die eingebaute LED der Platine, deren Beschaltung XRST störte, solange der Anschluss beim Start offen war, und der Reset des VS1003 kam aus dem kalten Zustand unzuverlässig |

Weitere Anschlüsse am VS1053 und was damit zu tun ist:
- **`AGND`, `LOUT`, `ROUT`, `GBUF`**: der analoge Tonausgang für Kopfhörer. `LOUT` und
  `ROUT` sind der linke und rechte Kanal, `GBUF` ist die virtuelle Masse für Kopfhörer.
  Sie gehen zum Lautsprecher, über den Verstärker oder eine Buchse, und **nicht** an den
  ESP32.
- **Der microSD-Steckplatz** (`CARD_CS` oder eine zweite Gruppe aus `CS`, `SCK`, `MISO`
  und `MOSI`): viele Module von LC Technology haben ihn am selben SPI-Bus. Wird er nicht
  gebraucht, bleibt er unverbunden; ohne verbundenes CS ist die Karte abgeschaltet.
- **`MIDI`, `RX` und `GPIO0` bis `GPIO3` des VS1053**: Anschlüsse für Betriebsart und
  MIDI, sie werden nicht gebraucht.

**Bewährte Regeln für den VS1053:**
- Er hängt am **selben SPI-Bus (FSPI)** wie die Platine, teilt sich also SCK, MOSI und
  MISO; das Display sitzt eigens an einem **getrennten Bus (HSPI)**, damit sie sich nicht
  in die Quere kommen. Verlege den VS1053 nicht auf den Bus des Displays.
- Verschluckt er sich an kurzen Tönen, ist das ein bekanntes Verhalten des Bausteins, das
  der Code bereits abfängt (kein `stopSong`, stattdessen klingt es in Stille aus). Es ist
  kein Problem der Verdrahtung.

> ⚠️ Eine Verwechslung, die man sich merken sollte: `GPIO13` ist MISO des VS1053 **und**
> zugleich sieht `I2S_SHIFT` im I2S-Zweig nach einer 13 aus. Beides schließt sich nicht
> aus, denn die 13 dort ist nur eine Zahl für die Verschiebung und kein Anschluss. Es
> gibt keinen echten Konflikt.

---

## Verstärker PAM8302A (Endstufe in Klasse D, mono, zum Lautsprecher)

Er verstärkt den Leitungsausgang des VS1053 so weit, dass ein Lautsprecher mit voller
Lautstärke arbeitet. Der **Eingang** des PAM kommt vom Tonausgang des VS1053 und **nicht**
von einem GPIO.

| Funktion / Name im Code | Kommt an | Mögliche Beschriftungen |
|---|---|---|
| **Versorgung** | `5V` | `VDD`, `VCC`, `V+`, `5V` (er verträgt 2,0 bis 5,5 V) |
| **Masse** | gemeinsame Masse | `GND`, `G`, `-` |
| **Toneingang +** | **`LOUT`** (oder `ROUT`) des VS1053 | `IN+`, `A+`, `IN`, `L` |
| **Toneingang −** | **`AGND`** des VS1053 | `IN-`, `A-`, `GND audio` |
| **Abschaltung** | **`GPIO39`** | `SD`, `/SD`, `SHDN`, `EN` |
| **Ausgang +/−** | Lautsprecher | `+`/`-`, `OUT+`/`OUT-` |

**Der Anschluss SD (Abschaltung) ist LOW-AKTIV und wird von der Firmware gesteuert:**
- `HIGH` heißt Verstärker an; `LOW` heißt **stumm**, dann verbraucht er praktisch nichts,
  das **Rauschen der Endstufe verschwindet** und es knackt nicht. Bei den meisten
  Platinen bleibt der Verstärker an, wenn SD **unverbunden** bleibt, denn sie haben einen
  Pull-up an Bord.
- Die Firmware lässt ihn **nur während eines Wortwechsels** laufen, also bei Signaltönen
  und Sprache, und hält ihn bei Ruhe stumm: siehe `AMP_SD_PIN` in
  [`config.h`](include/config.h) und `ampEnable()` in [`src/main.cpp`](src/main.cpp).
  `AMP_SD_PIN -1` schaltet die Steuerung ab, dann läuft der Verstärker dauerhaft.
- **Zum Ausprobieren im Dauerbetrieb**: lege SD auf **dieselbe Spannung wie VDD**;
  versorgst du den PAM mit 5 V, dann SD auf 5 V. Niemals SD über die VDD des Verstärkers
  legen.

> Hinweis zum Anschluss: `GPIO39` gehört zur JTAG-Gruppe (MTCK), auf die das Projekt
> ohnehin verzichtet, denn 40, 41 und 42 (MTDO, MTDI, MTMS) gehören zum Display. Es gibt
> keinen Konflikt.

---

## Drehgeber KY-040 (Blättern im Chat auf dem Display)

| Funktion / Name im Code | GPIO am ESP32 | Mögliche Beschriftungen |
|---|---|---|
| **Versorgung** | **3V3** | `+`, `VCC`, `V`, `5V` (auf dem KY-040 steht `+`; **versorge ihn mit 3V3**, nicht mit 5 V) |
| **Masse** | GND | `GND`, `G`, `-` |
| **Kanal A (CLK)** | `GPIO15` | `CLK`, `A`, `OUT_A`, `S1`, `ENC_A` |
| **Kanal B (DT)** | `GPIO16` | `DT`, `B`, `OUT_B`, `S2`, `ENC_B` |
| **Taste (auf den Knopf drücken)** | `GPIO17` | `SW`, `KEY`, `BTN`, `PUSH`, `S` |

> ⚠️ **Verdrahtung gegenüber Firmware (der aktuelle, funktionierende Stand):** oben steht
> die WIRKLICHE Verdrahtung (CLK an GPIO15, DT an GPIO16). In
> [`config.h`](include/config.h) sind die `#define` jedoch **mit Absicht vertauscht** —
> `ENC_A_PIN 16` und `ENC_B_PIN 15` — um die Drehrichtung in Software zu berichtigen,
> denn A und B waren beim Löten vertauscht worden. **Ziehe die Definitionen nicht
> gerade, damit sie zu dieser Tabelle passen**: dreht man die beiden Werte um, kehrt sich
> die Blätterrichtung um. So, wie es jetzt ist, blättert es richtig herum.

Was er tut: Drehen blättert im Chat nach oben und unten; ein Druck auf die Taste
startet beziehungsweise beendet den Chat (läuft das Radio, springt er zum nächsten
Sender). Ein eigenes Zurück ans laufende Ende braucht es nicht, jede neue Nachricht
schaltet ohnehin dorthin.

**Widerstände und Kondensatoren am Drehgeber:**
- Der **KY-040** hat bereits zwei Pull-up-Widerstände (10 kΩ) an CLK und DT an Bord. Bei
  einem nackten Drehgeber bräuchte es äußere Pull-ups, in der Praxis kommen aber die
  **internen Pull-ups des ESP32** über die Software zum Einsatz (`INPUT_PULLUP`), es ist
  also nichts zu löten.
- Mechanische Drehgeber prellen. Wahlweise: je ein **Keramikkondensator mit 100 nF
  zwischen CLK und GND sowie zwischen DT und GND** beruhigt die Abtastung deutlich.
  Keramikkondensatoren haben **keine** Polung und passen in jeder Richtung.

---

## Sprechtaste — ENTFERNT (der Drehgeber ist die einzige Bedienung)

Es gibt keine eigene Taste: im Gehäuse sitzt **nur der Drehgeber**. Seine Taste erledigt
alles. **GPIO14**, früher die Sprechtaste, treibt jetzt die **Hintergrundbeleuchtung des
Displays** (siehe den Abschnitt zum TFT ST7735).

Die Gesten am Drehgeber (siehe [`src/gobbo.cpp`](src/gobbo.cpp)):

| Geste | Wirkung |
|---|---|
| **Klick** (bei Ruhe) | startet den Chat und beginnt aufzunehmen |
| **Klick** (während der Aufnahme) | beendet sie und schickt ab |
| **Gedrückt und gedreht** | Lautstärke lauter und leiser |
| **Freies Drehen** | blättert im Chat |

> Der Start per Klick wird inzwischen ebenso durch das **Weckwort** ausgelöst, und den
> Stopp übernimmt zusätzlich die **Erkennung von Stille**, ohne dass der Rest der Kette
> davon berührt wäre: Start und Stopp sind getrennte Ereignisse.

---

## NeoPixel-Ring WS2812 (12 LED)

| Anschluss am Ring | Kommt an | Hinweis |
|---|---|---|
| `5V` (`VCC` / `+5V` / `PWR` / `+`) | **5V** | die WS2812 brauchen 5 V für volle Farben |
| `GND` (`-` / `G`) | gemeinsame Masse | — |
| `DIN` (`DI` / `IN` / `Data In`, folge dem aufgedruckten Pfeil ➜) | **GPIO48** | die Daten treten an der Seite `DIN` ein, nicht an `DOUT` |

**Kondensatoren und Widerstände (empfohlen, teils bereits im Entwurf):**
- Ein **Elektrolytkondensator mit 1000 µF zwischen 5V und GND des Rings**, nah am Ring.
  Bereits vorgesehen: er fängt die Stromspitzen ab, wenn die LED gemeinsam die Farbe
  wechseln, sonst stören sie das Mikrofon. Polung: + an 5V, – (heller Streifen) an GND.
- Ein **Widerstand von etwa 330 Ω in Reihe in der Datenleitung** (zwischen GPIO48 und dem
  Dateneingang des Rings). Er schützt die erste LED und hilft gegen Einstreuungen. Er hat
  keine Polung und gehört *in* die Leitung: trenne sie auf und setze ihn dazwischen.

**Hinweis zu Einstreuungen (bereits dokumentiert):** halte die Leitung `DIN` des Rings
**fern** von den Leitungen SCLK und MOSI des Displays, sonst lässt die elektromagnetische
Einkopplung den Ring während des Bildlaufs flackern. Der Widerstand von 330 Ω in der
Datenleitung hilft auch hier.

---

## TFT-Display ST7735 1,8 Zoll (an einem eigenen SPI-Bus, HSPI)

| Funktion / Name im Code | GPIO am ESP32 | Mögliche Beschriftungen |
|---|---|---|
| Takt | `GPIO2` | `SCK`, `SCL`, `CLK` |
| Daten | `GPIO1` | `SDA`, `MOSI`, `DIN`, `SI` |
| Chip Select | `GPIO42` | `CS`, `LCD_CS` |
| Daten/Befehl | `GPIO41` | `DC`, `A0`, `RS`, `D/C` |
| Reset | `GPIO40` | `RES`, `RST`, `RESET` |
| Versorgung | `3V3` | `VCC`, `VDD` |
| Masse | GND | `GND` |
| Hintergrundbeleuchtung | `GPIO14` | `LED`, `BL`, `BLK` |

**Die Hintergrundbeleuchtung an einem GPIO:** der Anschluss `LED` beziehungsweise `BL`
geht **nicht** mehr fest an 3V3, sondern an **GPIO14**, der frei war und früher die
Sprechtaste trug. So kann die Firmware **das Display bei Ruhe abschalten** (nach
`DISPLAY_SLEEP_MS`, ab Werk zwei Minuten) und beim ersten Eingriff wieder einschalten.
Der Anschluss zieht **etwa 2 mA** (gemessen), er wird deshalb **unmittelbar vom GPIO**
getrieben, *ohne Transistor und ohne äußeren Widerstand*; die Kathode der LED liegt im
Modul bereits auf Masse, und der Vorwiderstand sitzt ebenfalls dort. HIGH heißt an, LOW
heißt aus. Soll es dauerhaft an bleiben: `DISPLAY_SLEEP_MS 0` in
[`config.h`](include/config.h).

Über die Bauteile auf dem Modul hinaus braucht es nichts. Erscheint Müll auf dem Bild,
liegt es am SPI-Takt (gehe in der Konfiguration von 24 MHz auf 16 oder 20 MHz herunter)
und nicht an der Verdrahtung.

---

## Übersicht der passiven Bauteile

| Bauteil | Anzahl | Wohin | Polung? |
|---|---|---|---|
| Elektrolytkondensator **470 µF** | 1 | zwischen VCC und GND des MAX4466 (nur beim analogen Mikrofon) | **Ja** (+ an VCC) |
| Elektrolytkondensator **1000 µF** | 1 | zwischen 5V und GND des Rings | **Ja** (+ an 5V) |
| Widerstand **330 Ω** | 1 | in Reihe in der Datenleitung des Rings | Nein |
| Keramikkondensator **100 nF** | 0 bis 2 (wahlweise) | zwischen CLK und GND sowie DT und GND des Drehgebers | Nein |

> Mit dem **I2S-Mikrofon** braucht es die 470 µF **nicht mehr**, es ist digital.

**Die Polung eines Elektrolytkondensators in zwei Sätzen:** das **lange Bein ist +**; auf
dem Gehäuse verläuft ein **heller Streifen mit dem Zeichen `–`** auf der Seite des kurzen
Beins. Das `+` zeigt zur Versorgung (VCC oder 5V), das `–` zur Masse. Widerstände und
Keramikkondensatoren passen in jeder Richtung.

---

## Zusammenfassung der belegten Anschlüsse

> 📌 Die Anschlussbilder liegen in [`IMMAGINI/`](IMMAGINI/): `esp32-S3-DevKitC-1.png`
> (von oben, Bestückungsseite) und `esp32-S3-DevKitC-1_LATO-SALDATURE.png` (von
> **unten**, Lötseite — seitenverkehrt, aber mit lesbarer Beschriftung, nützlich beim
> Löten von der Rückseite her).


| GPIO | Bauteil | Signal |
|---|---|---|
| 1 | TFT ST7735 | MOSI/SDA |
| 2 | TFT ST7735 | SCLK/SCL |
| 4 | MAX4466 | OUT (ADC1) — *nur beim analogen Mikrofon* |
| 5 | I2S-Mikrofon | BCLK — *nur beim I2S-Mikrofon* |
| 6 | I2S-Mikrofon | WS — *nur beim I2S-Mikrofon* |
| 7 | I2S-Mikrofon | SD (Daten) — *nur beim I2S-Mikrofon* |
| 10 | VS1053 | XCS |
| 11 | VS1053 | MOSI |
| 12 | VS1053 | SCK |
| 13 | VS1053 | MISO |
| 14 | TFT ST7735 | LED/BL (Hintergrundbeleuchtung, früher die Sprechtaste) |
| 15 | Drehgeber | A / CLK |
| 16 | Drehgeber | B / DT |
| 17 | Drehgeber | SW (Taste: startet und beendet den Chat) |
| 21 | VS1053 | XDCS |
| 8 | VS1053 | XRST (von GPIO38 verlegt, dort sitzt die eingebaute LED) |
| 39 | PAM8302A | SD (Abschaltung des Verstärkers) |
| 40 | TFT ST7735 | RST |
| 41 | TFT ST7735 | DC/A0/RS (Daten/Befehl) |
| 42 | TFT ST7735 | CS |
| 18 | VS1053 | DREQ (von GPIO47 verlegt) |
| 48 | Ring WS2812 | DIN |
