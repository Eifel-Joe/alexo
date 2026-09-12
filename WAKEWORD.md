# ALEXO — Das Weckwort im Gerät (microWakeWord)

Das Weckwort **"Hey Jarvis"** wird **im ESP32-S3 selbst** erkannt, ohne Internet: allein
vom Zuhören verlässt kein Ton das Haus. Es ist die einzige künstliche Intelligenz, die
auf dem Mikrocontroller rechnet; der Rest der Sprachkette läuft anderswo, in der Cloud
oder auf einem Server zu Hause.

Das Weckwort **ersetzt nur den Start**: von da an ist der Ablauf derselbe wie beim Klick
auf den Drehgeber, der daneben weiter zur Verfügung steht, zum Starten von Hand und zum
Anhalten.

- Umsetzung: [`src/wakeword.cpp`](src/wakeword.cpp), das Modell in
  [`src/wake_model.h`](src/wake_model.h), die Merkmalsberechnung in
  [`lib/microfrontend/`](lib/microfrontend/).
- Eingeschaltet wird es mit `WAKE_ENABLE 1` in
  [`include/config.h`](include/config.h), so steht es ab Werk.

## Wie die Kette arbeitet

```
🎤 I2S mit 16 kHz (durchgehender Strom)
   → Merkmalsberechnung: 40 Mel-Merkmale alle 10 ms
   → Modell microWakeWord INT8 (im Strombetrieb, mit innerem Zustand)
   → Wahrscheinlichkeit 0–255
   → gleitender Mittelwert über WAKE_WINDOW Schritte größer als WAKE_PROB_CUTOFF
   → AUSLÖSUNG: derselbe Eingang wie der Klick auf den Drehgeber → das Gespräch beginnt
```

Das Modell arbeitet **im Strombetrieb**: es betrachtet nicht einen Tonausschnitt nach
dem anderen, sondern einen durchgehenden Fluss, von dem es sich zwischen zwei
Auswertungen etwas merkt. Daraus folgt die wichtigste Regel überhaupt: **der Fluss darf
nicht abreißen**. Jede Pause beim Lesen des Mikrofons lässt es den Faden verlieren, und
das Wort wird nicht mehr erkannt.

### Die Zahlen im Einsatz

| | Wert | Wo |
| --- | --- | --- |
| Modell | microWakeWord **v2 "hey_jarvis"**, INT8, **52272 Byte** | `src/wake_model.h` (`g_wake_model`) |
| Arbeitsspeicher | **22860 Byte** angefordert, **40 KB im PSRAM reserviert** | `WAKE_ARENA_BYTES` in `wakeword.cpp` |
| Schwelle | **247** von 255 (das Manifest nennt 0,97) | `WAKE_PROB_CUTOFF` |
| Gleitendes Fenster | **5** Schritte | `WAKE_WINDOW` |
| Digitale Verstärkung | **3** | `WAKE_GAIN` |

> Die drei `WAKE_*` lassen sich **im Web-Panel ändern**, ohne neu zu übersetzen; in
> `config.h` stehen nur die Werkseinstellungen.

### Die Merkmalsberechnung muss zum Training passen

Die 40 Merkmale müssen **genau so** berechnet werden wie beim Training des Modells,
sonst bekommt es Zahlen, die es nicht wiedererkennt. Die Einstellungen stammen aus
`preprocessor_settings.h` von ESPHome (dem Vorverarbeiter micro_speech beziehungsweise
der Merkmalsberechnung von TFLM):

- Abtastrate **16000**, Fenster **30 ms** (480 Abtastwerte), Schritt **10 ms** (160 Abtastwerte)
- **40** Mel-Kanäle, Band **125 bis 7500 Hz**
- Rauschunterdrückung: `smoothing_bits=10`, `even=0.025`, `odd=0.06`, `min_signal_remaining=0.05`
- PCAN (die selbsttätige Verstärkungsregelung): `enable=true`, `strength=0.95`, `offset=80.0`, `gain_bits=21`
- logarithmische Skala: `enable=true`, `scale_shift=6`
- Ausgabe: 40 Merkmale je Scheibe, eine in jedem Schritt von 10 ms

Das Modell sammelt `stride` Scheiben (zur Laufzeit aus `input->dims[1]` gelesen), bevor
es `Invoke()` aufruft.

## In welcher Reihenfolge es entstand

Jeder Schritt ließ sich für sich prüfen, bevor der nächste kam. Hört die Kette auf zu
arbeiten, ist das die Reihenfolge, in der man sie am besten wieder durchgeht.

**1 · Ein sauberes Mikrofon.** Hochpass bei etwa 120 Hz und Verschiebung 15 auf dem PCM,
dazu der Messbetrieb `MIC_DIAG`, um das Grundrauschen zu messen. *Prüfung:* die
ausgegebenen Pegel unterscheiden Stille von Sprache. Ohne das arbeitet alles Weitere auf
schmutzigem Ton.

**2 · TFLite Micro übersetzt und läuft.** Bevor eine Zeile für das Weckwort entsteht,
muss feststehen, dass die Laufzeitumgebung vorhanden ist und auf der Platine nicht
abstürzt: ein schlichtes `Invoke()` auf einem Testmodell (`TFL_SELFTEST` und
[`src/tfltest.cpp`](src/tfltest.cpp)). *Prüfung:* kein Absturz, und die Rechenzeiten
(38 bis 132 µs) erscheinen im Protokoll über das Netz. Dieser Schritt **räumt das größte
Risiko zuerst aus dem Weg**, nämlich das der Werkzeugkette.

**3 · Die Merkmalsberechnung.** Alle 10 ms die 40 Merkmale aus dem I2S-Strom erzeugen.
*Prüfung:* die ausgegebenen Werte ändern sich deutlich zwischen Sprache und Stille.

**4 · Ein fertig trainiertes Modell.** Ein Modell aus
`esphome/micro-wake-word-models` als konstantes Feld einbetten, Merkmalsberechnung und
Modell verbinden und die Wahrscheinlichkeit ausgeben. *Prüfung:* beim Aussprechen des
Wortes steigt die Wahrscheinlichkeit. Dieser Schritt zeigt, dass die ganze Kette
arbeitet.

**5 · Schwelle, gleitender Mittelwert und Auslösung.** Liegt der Wert über N Schritte in
Folge über der Schwelle, beginnt das Gespräch, an derselben Stelle wie beim Klick auf
den Drehgeber. Dazu die **Sperrzeit**: nach einer Auslösung wird das Fenster geleert,
sonst startete dasselbe Wort gleich drei Gespräche.

**6 · Feinarbeit.** Taubheit während des Gesprächs, sauberer Wiedereinstieg ins Zuhören
und das Zusammenspiel mit dem Ton bei Ruhe (siehe unten).

**Offen geblieben ist:** ein **eigenes** Weckwort anstelle eines fertig trainierten
Modells (am Ende dieser Seite).

## Die zwei technischen Entscheidungen, die zählten

### Welche Laufzeitumgebung für TFLite Micro (entschieden: die Arduino-Bibliothek)

Das Projekt arbeitet mit Arduino und PlatformIO, während TFLite Micro für ESP-IDF
gedacht ist. Zwei Wege:

1. **`esp-tflite-micro`** von Espressif: der schnellste Weg, mit den auf `esp-nn`
   optimierten Rechenkernen für die Vektorbefehle des S3. Es ist allerdings eine
   ESP-IDF-Komponente und muss unter PlatformIO mit Arduino von Hand eingefügt werden,
   was heikel ist.
2. **Eine Arduino-Bibliothek, die TFLM mitbringt**: sie kommt in `lib_deps` und
   funktioniert, allerdings ohne die Beschleunigung durch `esp-nn`.

**Gewonnen hat der zweite Weg**: `esp-tflite-micro` verhält sich unter PlatformIO mit
Arduino schlecht, deshalb kommt **Chirale_TensorFlowLite** zum Einsatz (siehe
`platformio.ini`). Die Geschwindigkeit ist nicht der Engpass, die Auswertung dauert
Mikrosekunden gegenüber den 10 ms jedes Schrittes.

### Die Merkmalsberechnung musste mitgeliefert werden

Chirale enthält die Merkmalsberechnung **nicht**, sie ist aber zwingend, denn ohne sie
bekommt das Modell keine Eingabe. Sie liegt deshalb in
[`lib/microfrontend/`](lib/microfrontend/), kopiert aus den Quellen von TFLM. Was darin
steckt, für alle, die es nachbauen müssen:

- Aus `tensorflow/lite/experimental/microfrontend/lib/` (ohne `_io`, `_test`, `_main`,
  `memmap` und `BUILD`): `frontend`, `frontend_util`, `filterbank` samt `util`,
  `noise_reduction` samt `util`, `pcan_gain_control` samt `util`, `log_scale` samt
  `util`, `log_lut`, `window` samt `util`, `fft`, `fft_util`, `kiss_fft_int16`,
  `kiss_fft_common.h` und `bits.h`.
- **Die Abhängigkeit kissfft**: `kiss_fft_int16` bindet innerhalb des Namensraums
  `kissfft_fixed16` mit `FIXED_POINT=16` die Quellen `kiss_fft.h` und `.c` sowie
  `tools/kiss_fftr.h` und `.c` (dazu `_kiss_fft_guts.h`) aus dem Repository
  `mborgerding/kissfft` ein, in der Fassung, die
  `tensorflow/lite/micro/tools/make/kissfft_download.sh` nennt, samt der Korrektur
  `third_party/kissfft/kissfft.patch`.
- **Der Aufbau des Ordners**: `lib/microfrontend/src/tensorflow/...`, denn TFLM benutzt
  absolute Einbindungen; die Quellen von kissfft müssen über den Suchpfad als
  `kiss_fft.h` und `tools/kiss_fftr.h` erreichbar sein.

## Zusammenspiel mit dem Ton bei Ruhe

Das Weckwort braucht **den gesamten** Strom, ohne Unterbrechung. Deshalb ist das Lesen
des I2S-Busses bei Ruhe **zusammengelegt**: im `loop()` erzeugt ein einziger Aufruf von
`micReadChunk()` den Block, der sowohl `wakeFeed()` als auch den Pegel des LED-Rings
(`micLevelFromChunk`) versorgt. Zwei getrennte `i2s_read` würden einander die
Abtastwerte wegnehmen, und dem Modell fehlte die Hälfte des Tons. Mit `WAKE_ENABLE 0`
bleibt der alte Weg, `micPeekLevel()` allein für den Ring.

**Am Ende eines Gesprächs** braucht es `micFlush()` und `wakeReset()`: im Puffer steht
noch der Ton der letzten Sekunden, die Stimme von Alexo aus dem Lautsprecher
eingeschlossen, und ohne ihn zu verwerfen **löst das Weckwort von allein aus**, auf
altem Ton.

## Wie man das Wort auswählt

Der Katalog der **fertig trainierten** Modelle enthält insgesamt vier (`alexa`,
`hey_jarvis`, `hey_mycroft`, `okay_nabu`). Man sollte sie nutzen, solange sie reichen,
denn sie sind genau und kosten keine Zeit.

**Warum "Hey Jarvis" (12. September 2026).** Beim Umstellen dieses Forks auf Deutsch
wurde das Weckwort auf Wunsch des Betreibers gewechselt. Der ursprüngliche Autor hatte
"hey jarvis" getestet und verworfen, weil es ein auf Englisch trainiertes Modell ist und
bei italienischer Aussprache selten ansprach. Bei deutscher Aussprache liegt der Klang
deutlich näher am Englischen, der Grund für die Verwerfung entfällt damit weitgehend.
Bestätigen kann das nur der Test am Gerät, der noch aussteht.

Die Geschichte davor, weil sie lehrreich ist:

- **"Okay Nabu"** war das erste Weckwort. Es blieb beim Balancing Robot des Autors, denn
  in einem Haus kann ein Wort nicht zwei Geräte wecken, und dort war es das einzige
  **erprobte** (Wahrscheinlichkeit 254 bei einer Schwelle von 246).
- **"Hey Mycroft"** kam danach und war bis zu dieser Übersetzung im Einsatz.
- **"alexa"** wurde wegen des Gleichklangs mit "Alexo" versucht, löste aber bei
  **jedem** Wort mit "-xa" darin aus. Ein zu kurzes und zu häufiges Weckwort schadet
  mehr, als es nützt.

> ⚠️ **Der fertige Katalog ist begrenzt.** Wenn "Hey Jarvis" auf Deutsch nicht
> zuverlässig erkannt wird, bleibt allein der Weg, ein eigenes Modell zu trainieren —
> und das offizielle Projekt warnt, dass es noch immer sehr schwierig sei, ein Modell zu
> trainieren, das gut funktioniert.

### Wenn du ein eigenes Wort willst

Das ist der offen gebliebene Schritt. Ein eigenes Modell trainiert man mit dem
Colab-Notizbuch von **microWakeWord**: Aufnahmen sind nicht nötig, die Sprachbeispiele
werden künstlich erzeugt, und der Vorgang dauert grob eine halbe bis eine Stunde. Danach:

1. Exportiere die INT8-Datei `.tflite` und notiere die Werte aus dem **Manifest**.
2. Wandle sie mit `xxd -i` in ein C-Feld um und behalte den Namen `g_wake_model` bei.
3. Ersetze `src/wake_model.h` (ein unmittelbarer Austausch, sonst ist nichts
   anzufassen).
4. Ziehe `WAKE_PROB_CUTOFF` und `WAKE_WINDOW` in `config.h` mit den Werten aus dem
   Manifest nach (`probability_cutoff` mal 255 und `sliding_window_size`).

## Abstimmen und Werkzeuge zur Fehlersuche

Drei Schalter in `config.h`, und jeder lässt die Aktualisierung über Funk offen:

| Schalter | Wozu er dient |
| --- | --- |
| `MIC_DIAG` | die Rauschpegel des Mikrofons, um die Verstärkung zu wählen |
| `TFL_SELFTEST` | arbeitet die Laufzeitumgebung von TFLite Micro? (Testmodell `hello_world`) |
| `WAKE_TEST` | lässt die Kette auf künstlichem Ton laufen, **ohne Mikrofon**: prüft Merkmalsberechnung und Modell und zeigt Fehlauslösungen vor dem Test am lebenden Gerät |

Drei Lehren, die das Abstimmen gekostet hat:

- **Der Fluss muss durchgehend sein.** Das Modell arbeitet im Strombetrieb: eine Pause
  zwischen zwei Lesevorgängen, und es erkennt nichts mehr. Also keine Wartezeiten in den
  Weg des Weckworts einbauen.
- **Zu viel Verstärkung macht es schlechter.** `WAKE_GAIN` zu erhöhen wirkt wie das
  offensichtliche Mittel, wenn er "nicht hört", aber ab einem gewissen Punkt
  **übersteuert** das Signal und die Erkennung wird schlechter.
- **Alter Ton löst das Weckwort von allein aus.** Siehe `micFlush()` und `wakeReset()`
  weiter oben.

## Quellen

- microWakeWord: <https://microwakeword.com/> (Training: <https://microwakeword.com/train>)
- Repository für das Training: <https://github.com/OHF-Voice/micro-wake-word>
- Fertige Modelle: <https://github.com/esphome/micro-wake-word-models>
- `esp-tflite-micro`: <https://github.com/espressif/esp-tflite-micro>
- ESPHome `micro_wake_word`: <https://esphome.io/components/micro_wake_word/>
- Praxisanleitung ESP32-S3 mit TFLM: <https://dev.to/zediot/esp32-s3-tensorflow-lite-micro-a-practical-guide-to-local-wake-word-edge-ai-inference-5540>
