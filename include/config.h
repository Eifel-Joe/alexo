#pragma once
// ============================================================================
//  ALEXO - Hardware-Konfiguration (ESP32-S3 N16R8 DevKitC-1)
//  Hier die Anschlüsse ändern, wenn anders verdrahtet wurde. Der ganze übrige
//  Code liest aus dieser Datei, sonst ist also nichts anzufassen.
// ============================================================================
//
//  WICHTIGES zu den Anschlüssen des ESP32-S3 N16R8:
//   - Der Octal-PSRAM belegt intern GPIO 33..37: diese NICHT benutzen.
//   - GPIO 19/20 gehören zu USB: freilassen.
//   - ADC1 (für das analoge Mikrofon) liegt auf GPIO 1..10.
//   - GPIO 0 / 45 / 46 / 3 sind Strapping-Pins: für Peripherie meiden.
//
// ----------------------------------------------------------------------------

// --- TFT-Display ST7735 1,8" 128x160 (SPI) ----------------------------------
//  ÜBERGANGSWEISE verbaut (bis das 5-Zoll-Display kommt). Es hängt an einem
//  EIGENEN SPI-Bus (HSPI), getrennt von dem des VS1053 (FSPI): so streitet der
//  Bildlauf des Chats auf Kern 0 nicht mit der Tonzuführung auf Kern 1, und es
//  stockt nichts.
//  Alle Anschlüsse sind frei (kein Strapping, kein USB, kein ADC2, kein PSRAM).
//  Verdrahtung des ST7735-Moduls: VCC->3V3  GND->GND  LED/BL->3V3 und die
//  Anschlüsse unten.
#define TFT_SCLK_PIN     2      // SCK / SCL
#define TFT_MOSI_PIN     1      // SDA / MOSI (DIN)
#define TFT_CS_PIN       42     // CS
#define TFT_DC_PIN       41     // DC / A0 / RS
#define TFT_RST_PIN      40     // RES / RST
#define TFT_WIDTH        128
#define TFT_HEIGHT       160
//  Variante der Lasche am Modul: sind die Farben vertauscht oder bleibt ein
//  Rand, INITR_GREENTAB oder INITR_REDTAB probieren. Das klassische rote
//  1,8-Zoll-Modul ist BLACKTAB.
#define TFT_INITR        INITR_BLACKTAB
//  SPI-Takt des TFT. Das Flackern des Rings während des Bildlaufs kam von
//  Einstreuungen der SPI-Flanken auf die Datenleitung des WS2812: bei 2 MHz war
//  es weg, dafür lief der Bildlauf "wellig" (das Übertragen des ganzen Bildes
//  dauerte etwa 164 ms und war als langsamer Aufbau sichtbar). Behoben wurde es
//  an der Hardware: die Datenleitung wurde von SCLK und MOSI weggeführt (bei
//  Bedarf zusätzlich 330 Ohm in Reihe), seither darf der Takt hoch bleiben.
//  24 MHz bedeutet etwa 14 ms je Bild und einen glatten Lauf. Zeigt DEIN Modul
//  bei 24 MHz Müll an, auf 16 oder 20 MHz heruntergehen.
#define TFT_SPI_HZ       24000000

//  Die Hintergrundbeleuchtung (LED/BL) hängt an einem GPIO und NICHT mehr fest
//  an 3V3: so lässt sie sich bei Ruhe abschalten und spart Strom. Der Anschluss
//  zieht sehr wenig (mit dem Messgerät etwa 2 mA), er wird deshalb DIREKT vom
//  GPIO getrieben, ohne Transistor und ohne Vorwiderstand. HIGH = an, LOW = aus.
//  VERDRAHTUNG: der Anschluss LED/BL des Moduls geht an GPIO14 (vorher an 3V3).
//  GPIO14 war frei, dort sass früher die Sprechtaste. Die Kathode der LED liegt
//  im Modul bereits auf Masse.
#define TFT_BL_PIN       14
//  Nach so vielen Millisekunden OHNE Bedienung (kein Klick, kein Drehen, keine
//  neue Nachricht, und Alexo in Ruhe) schaltet sich das Display ab; beim ersten
//  Eingriff geht es wieder an. 0 lässt es dauerhaft an.
#define DISPLAY_SLEEP_MS 120000   // 2 Minuten
//  Bewegtes Startbild beim Einschalten (Anzeigetafel auf dem TFT und ein Ring,
//  der sich "lädt"). 0 = aus.
#define SPLASH_BOOT      1

// --- Ring NeoPixel 12 LED WS2812 --------------------------------------------
#define LED_RING_PIN     48     // Dateneingang des Rings
#define LED_RING_COUNT   12
#define LED_BRIGHTNESS   40     // 0-255, niedrig halten, sonst wird es warm und zieht Strom
//  Bei Ruhe auf Geräusche reagieren: 1 = der Ring "tanzt" zum Mikrofon, auch
//  wenn nichts läuft; 0 = der Ring ist bei Ruhe AUS und leuchtet nur in den
//  aktiven Zuständen. Wieder eingeschaltet, nachdem das Mikrofonrauschen behoben
//  war (Hochpass in mic.cpp): seither ist der Grundpegel bei Ruhe niedrig und
//  der Ring flackert nicht mehr grundlos.
#define IDLE_REACTIVE    1

// --- Sprechtaste (ENTFERNT) -------------------------------------------------
//  Zur Geschichte: es gab eine eigene Taste an GPIO14. Heute ist die EINZIGE
//  Bedienung die Taste des Drehgebers (siehe unten): Klick = Chat starten und
//  beenden, gedrückt und gedreht = Lautstärke, gedreht = blättern. GPIO14 treibt
//  jetzt die Hintergrundbeleuchtung des Displays (TFT_BL_PIN oben). Die
//  zugehörigen Definitionen sind entfernt, sie wurden nirgends mehr gebraucht.

// --- MIKROFON ---------------------------------------------------------------
//  Auswahl des Mikrofons. Die Softwareschnittstelle ist für beide dieselbe, zum
//  Wechseln genügt also diese Zeile (und das Umverdrahten): sonst ist KEIN Code
//  anzufassen.
//    0 = MAX4466 analog (an ADC1) -> auf 0 setzen, neu übersetzen, flashen
//    1 = digitales I2S-Mikrofon (ICS-43434 / INMP441) - DERZEIT IN BETRIEB
#define MIC_USE_I2S      1

#define MIC_SAMPLE_RATE  16000  // Hz, so will es Whisper (für beide Mikrofone)

//  MAX4466 (analog, in Betrieb bei MIC_USE_I2S = 0)
#define MIC_ADC_PIN      4      // GPIO4 = ADC1_CH3 (analoger Ausgang des Mikrofons)

//  ICS-43434 (I2S, in Betrieb bei MIC_USE_I2S = 1). Mit 3V3 versorgen.
//    VDD->3V3  GND->GND  SCK->I2S_SCK  WS->I2S_WS  SD->I2S_SD  L/R->GND(=LEFT)
#define I2S_SCK_PIN      5      // BCLK / SCK
#define I2S_WS_PIN       6      // WS / LRCL
#define I2S_SD_PIN       7      // SD / DOUT (Daten vom Mikrofon)
#define I2S_SHIFT        15     // Umsetzung 32 -> 16 Bit (höher = lauter). Die 15 kam
                                //  aus der Messung in Schritt 0: Sprache erreicht etwa
                                //  10000 als Spitze, ohne Übersteuern (bei 13 lief es
                                //  an). Die Aufnahme legt zusätzlich einen Hochpass bei
                                //  etwa 120 Hz an (siehe mic.cpp), der den Gleichanteil
                                //  und das tieffrequente Brummen entfernt, also den
                                //  größten Teil des Rauschens.
//  Messbetrieb für das Mikrofon (Schritt 0 des Weckworts): auf 1 setzen, über
//  Funk flashen, den seriellen Monitor öffnen. Die Firmware bleibt dann im
//  Messbetrieb und gibt das Grundrauschen mit 24 Bit aus, dazu, was jede
//  Verschiebung (13..16) bei Ruhe und beim Sprechen ergäbe. So lässt sich
//  I2S_SHIFT anhand einer Zahl wählen statt durch Probieren. Der Chat startet
//  NICHT, die Aktualisierung über Funk BLEIBT AKTIV: zum Beenden wieder 0 setzen
//  und erneut über Funk flashen. Werkseinstellung 0.
#define MIC_DIAG         0

// --- Weckwort "Hey Jarvis", erkannt im Gerät selbst (microWakeWord) --------
//  Siehe WAKEWORD.md.
//  1 = Weckwort aktiv (der Chat startet mit "Hey Jarvis" GENAUSO wie mit einem
//  Klick auf den Drehgeber); 0 = Start NUR per Klick.
//  GEÄNDERT am 2026-09-12: vorher "Hey Mycroft", davor "Okay Nabu". "Okay
//  Nabu" gehört dem Balancing Robot, zwei Geräte im selben Haus können
//  dasselbe Weckwort nicht teilen.
#define WAKE_ENABLE      1
//  Erkennungsparameter (aus dem Manifest v2 "hey_jarvis"): Schwelle der
//  Wahrscheinlichkeit 0..255 und Breite des gleitenden Fensters, über das
//  gemittelt wird. Beides lässt sich im Web-Panel im Betrieb ändern, hier
//  steht nur die Werkseinstellung.
//  ACHTUNG: 247 und NICHT 242. Die Schwelle ist eine Eigenschaft des Modells,
//  keine Geschmacksfrage: das Manifest von "hey_jarvis" nennt 0.97 (0.97*255),
//  das von "hey_mycroft" nannte 0.95. Bliebe hier die 242 stehen, verlangte man
//  von Jarvis weniger Sicherheit als abgestimmt wurde, und es gäbe mehr
//  Fehlauslösungen. Siehe src/wake_model.h.
#define WAKE_PROB_CUTOFF 247
#define WAKE_WINDOW      5
//  Digitale Verstärkung allein des Weckwort-Wegs (das PCM mit Verschiebung 15
//  ist zu leise, man müsste schreien). Multipliziert die Abtastwerte vor der
//  Merkmalsberechnung, mit Begrenzung. Höher, wenn man immer noch lauter
//  sprechen muss; niedriger, wenn es fälschlich auslöst.
#define WAKE_GAIN        3

// --- Sprachaufnahme (nach dem Auslösen durch Weckwort oder Klick) -----------
//  Selbsttätiger Abbruch bei Stille: nachdem Sprache zu hören war, endet die
//  Aufnahme von allein, sobald REC_SILENCE_MS lang ununterbrochen Stille
//  herrscht. Man muss also nicht bis zur Höchstdauer warten.
#define REC_MAX_MS        20000   // Höchstdauer der Aufnahme (erfordert MIC_MAX_SECONDS>=20)
#define REC_SILENCE_MS     1500   // Abbruch nach so viel ununterbrochener Stille (0 = aus)
//  Der Abbruch bei Stille arbeitet mit einer SELBSTTÄTIG NACHGEFÜHRTEN SCHWELLE
//  (siehe mic.cpp). Es ist kein fester Pegel mehr: was als "Sprache" gilt, misst
//  sich am laufend geschätzten Grundrauschen (Lüfter, Wind).
//  Schwelle = Grundpegel * MARGIN + FLOOR (in RMS, derselben Skala wie AC_HP16
//  bei MIC_DIAG). So passt es sich von selbst an, wenn sich das Rauschen ändert.
//  MARGIN sagt, wie weit über dem Grundpegel etwas als Sprache zählt; FLOOR ist
//  der kleinste absolute Abstand (bei ruhigem Mikrofon liegt der Grundpegel bei
//  etwa 100 und Sprache bei etwa 500).
#define REC_SILENCE_MARGIN  1.6f  // Faktor auf den Grundpegel (höher, wenn Rauschen als Sprache gilt)
#define REC_SILENCE_FLOOR    150  // kleinster absoluter Abstand in RMS (höher, wenn es zu spät abbricht)
#define REC_MIN_MS          800   // Schonfrist am Anfang: vorher nicht abbrechen, damit man loslegen kann
//  FORTLAUFENDER CHAT: nach einer Antwort öffnet sich das Mikrofon von allein, die
//  nächste Frage braucht also nicht erneut das Weckwort. Beendet wird er durch
//  Schweigen (CHAT_FOLLOWUP_MS) oder einen Klick auf den Drehgeber. Der Schalter
//  sitzt im Panel; hier steht die Werkseinstellung. Sie ist aus, weil es das
//  Verhalten ändert und man es bewusst einschalten soll.
#define CHAT_CONTINUA_DEF     0
#define CHAT_FOLLOWUP_MS   3000   // wie lange nach einer Antwort auf die Frage gewartet wird
//  Selbsttest für TFLite Micro (Schritt 2 in WAKEWORD.md): 1 = beim Start laufen
//  das Testmodell "hello_world" (Sinus) und die Merkmalsberechnung, die Ausgabe
//  geht über Telnet. Die Aktualisierung über Funk bleibt aktiv.
#define TFL_SELFTEST     0
//  Test der Weckwortkette: 1 = Merkmalsberechnung, Modell und Wahrscheinlichkeit
//  laufen auf dem MIKROFONTON, und die höchste Wahrscheinlichkeit wird jede
//  Sekunde ausgegeben. Damit lässt sich die Schwelle abstimmen und die Kette
//  prüfen, bevor man am lebenden Gerät testet. Die Aktualisierung über Funk
//  bleibt aktiv.
#define WAKE_TEST        0

// --- VS1053 (Tonausgabe, SPI-Bus) -------------------------------------------
//  Gemeinsam genutzter SPI-Bus (FSPI)
#define SPI_SCK_PIN      12
#define SPI_MOSI_PIN     11
#define SPI_MISO_PIN     13
//  Steueranschlüsse des VS1053
#define VS1053_XCS_PIN   10     // Chip Select (Befehle)
#define VS1053_XDCS_PIN  21     // Data Chip Select (Daten)
#define VS1053_DREQ_PIN  18     // Data Request (Eingang) - von GPIO47 auf GPIO18 verlegt
#define VS1053_XRST_PIN  8      // Reset (-1 wenn nicht verbunden) - von GPIO38 verlegt
                                //  (an 38 hängt die eingebaute LED der Platine: deren
                                //  Beschaltung störte XRST, während der Anschluss beim
                                //  Start offen war, und der Reset des VS1003 kam aus
                                //  dem kalten Zustand unzuverlässig)

// --- Verstärker PAM8302A (Abschaltung über GPIO) ----------------------------
//  Der Anschluss SD (shutdown, /SD) des PAM8302A: die Funktion ABSCHALTEN ist
//  LOW-aktiv, zum EINSCHALTEN des Verstärkers wird der Anschluss also HIGH
//  gelegt.
//      GPIO39 HIGH -> Abschaltung AUS -> Verstärker AN
//      GPIO39 LOW  -> Abschaltung AN  -> Verstärker STUMM (praktisch kein
//                                        Verbrauch, kein Rauschen, kein Knacken)
//  Er läuft nur während eines Wortwechsels (Ton und Stimme) und bleibt bei Ruhe
//  stumm.
//  Verdrahtung: SD des PAM8302A an GPIO39. Der Toneingang des PAM kommt von
//  LOUT/ROUT (samt AGND) des VS1053, NICHT von einem GPIO. Den PAM aus 5 V
//  versorgen.
//  GPIO39 ist frei (aus der JTAG-Gruppe MTCK, auf die ohnehin verzichtet wurde:
//  40, 41 und 42 gehören zum TFT).
//  -1 schaltet die Steuerung ab (Verstärker dauerhaft an, SD nicht verbunden).
#define AMP_SD_PIN       39

// --- Drehgeber (Blättern im Chat auf dem Teleprompter) ----------------------
//  KY-040 oder ähnlich: CLK->A, DT->B, SW->Taste. Mit 3V3 versorgen.
//  Die Anschlüsse sind frei und unbedenklich gewählt (kein Strapping, kein USB,
//  kein ADC2, kein PSRAM).
//  Drehen = im Chat nach oben und unten blättern; Tastendruck = Chat starten
//  beziehungsweise beenden (bei Radio: nächster Sender). Ein eigenes Zurück ans
//  laufende Ende braucht es nicht, jede neue Nachricht schaltet ohnehin dorthin.
#define ENC_A_PIN        16     // CLK (A) - GPIO16/15 vertauscht: A und B sind beim Löten getauscht
#define ENC_B_PIN        15     // DT  (B)
#define ENC_SW_PIN       17     // SW  (Taste, optional)

// --- Lautstärke (VS1053, Skala 0..100; 100 = höchste) -----------------------
//  Eingestellt wird sie, indem man die Taste des Drehgebers DRÜCKT und dreht: im
//  Uhrzeigersinn lauter, dagegen leiser (siehe gobbo.cpp). Der Wert liegt im NVS
//  und übersteht einen Neustart. Sind die Richtungen vertauscht, das Vorzeichen
//  von VOLUME_STEP ändern.
#define VOLUME_DEFAULT   90     // Lautstärke beim ersten Start (danach gilt der gespeicherte Wert)
#define VOLUME_MIN        0     // 0 = wirklich STUMM (VS1053 still und Verstärker aus)
#define VOLUME_STEP       5     // Schrittweite je Rastung des Drehgebers
//  Der VS1053 arbeitet mit einer LOGARITHMISCHEN Skala (dB): sein Bereich von 0
//  bis etwa 60 ist praktisch stumm, hörbar wird es erst zwischen 60 und 100. Um
//  den GANZEN Weg von Regler und Drehgeber zu nutzen, wird die Nutzerlautstärke
//  1..100 auf den hörbaren Bereich VOLUME_VS_MIN..100 umgerechnet (siehe
//  volumeVsValue in volume.cpp). VOLUME_VS_MIN erhöhen, wenn das Minimum noch
//  immer stumm ist.
#define VOLUME_VS_MIN    63     // Wert des VS1053, der der Nutzerlautstärke 1 entspricht

// --- Einstellungs-Panel im Browser (settings.cpp + webui.cpp) ---------------
//  Die abstimmbaren Werte hier unten sind die WERKSEINSTELLUNGEN. Beim Start
//  lädt das Modul settings sie aus dem NVS, sofern der Nutzer sie im Panel unter
//  http://alexo.local/ geändert hat, sonst gelten diese. "Werkseinstellung
//  wiederherstellen" im Panel schreibt genau diese Werte zurück. Zu beachten:
//  die Werkseinstellungen für Mikrofon, Stille, LED und Weckwort stehen bereits
//  in den Makros weiter oben (REC_SILENCE_*, MIC_LVL_*_DEF, WAKE_*,
//  IDLE_REACTIVE).
//  Pegel der reagierenden LED (Werkseinstellung, im Panel änderbar). Früher
//  waren es feste Definitionen in mic.cpp, jetzt stehen sie hier.
#define MIC_LVL_MARGIN_DEF  3.0f   // wie weit über dem Grundpegel, bevor die LED angehen
#define MIC_LVL_FLOOR_DEF   40.0f  // kleinster absoluter Abstand der LED (gegen Zappeln)
//  Hüllkurve der reagierenden LED: Anstiegsgeschwindigkeit und
//  Abklinggeschwindigkeit. 0..1, niedriger bedeutet langsamer. Ein niedriger
//  Anstieg verhindert Aufblitzen bei einzelnen Geräuschen; ein niedriges
//  Abklingen lässt die LED nachziehen, sie blenden langsam aus.
#define MIC_LVL_ATTACK_DEF  0.12f  // Anstieg: wie schnell sie angehen
#define MIC_LVL_RELEASE_DEF 0.25f  // Abklingen: wie schnell sie ausgehen
//  Standardstimme bei ElevenLabs (Voice ID).
//  Dies ist noch die Stimme aus dem Originalprojekt. Für den Jarvis-Klang
//  eine eigene Stimme im ElevenLabs-Konto wählen und ihre Kennung im
//  Web-Panel eintragen; das Modell eleven_flash_v2_5 ist mehrsprachig.
#define ELEVEN_VOICE_DEF    "fTHp5NEBwS4InadKS0Ci"
//  ZWEITE Stimme samt Auslösewort: beginnt der Satz mit VOICE_TRIGGER_DEF,
//  antwortet Alexo mit ELEVEN_VOICE_ALT_DEF. Leeres Auslösewort schaltet die
//  zweite Stimme ab. Verglichen wird klein geschrieben.
#define ELEVEN_VOICE_ALT_DEF "CiwzbDpaN3pQXjTgx3ML"
#define VOICE_TRIGGER_DEF    "gut"
//  Geisterphrasen von Whisper (eine je Zeile): stimmt die Transkription GENAU
//  mit einer davon überein, wird sie stillschweigend verworfen. Das sind die
//  typischen Halluzinationen auf Stille. Leer = Filter aus. Verglichen wird
//  klein geschrieben und ohne Satzzeichen an den Rändern.
//  Getrennt wird am ZEILENUMBRUCH und nicht am Komma wie im Original, denn die
//  häufigsten deutschen Geisterphrasen sind Abspänne von Untertiteln und
//  tragen selbst ein Komma. Siehe isAllucinazione in main.cpp.
//  Umlaute klein schreiben: der Vergleich setzt nur ASCII-Buchstaben um.
#define HALLUC_TERMS_DEF \
    "vielen dank\n" \
    "vielen dank fürs zuschauen\n" \
    "vielen dank für's zuschauen\n" \
    "danke\n" \
    "danke schön\n" \
    "dankeschön\n" \
    "untertitel der amara.org-community\n" \
    "untertitel von stephanie geiges\n" \
    "untertitelung des zdf für funk, 2017\n" \
    "untertitel im auftrag des zdf für funk, 2017\n" \
    "untertitelung im auftrag des zdf, 2021\n" \
    "mehr infos auf www.zdf.de\n" \
    "copyright wdr\n" \
    "tschüss\n" \
    "auf wiedersehen\n" \
    "bis zum nächsten mal\n" \
    "das war's\n" \
    "so das war's"
//  Gehirn: Standardmodell von Claude und die "Persönlichkeit" (System-Prompt).
//  claude-haiku-4-5 = schnell und günstig; claude-sonnet-5 = Mittelweg;
//  claude-opus-5 = klüger, dafür langsamer und teurer.
#define LLM_MODEL_DEF       "claude-haiku-4-5"
#define SYSTEM_PROMPT_DEF \
    "Du bist Jarvis, ein Sprachassistent für zu Hause und antwortest auf Deutsch. " \
    "Du sprichst förmlich, knapp und mit ruhiger Höflichkeit, gelegentlich mit " \
    "trockenem Humor. Du redest den Nutzer mit Sir an, aber sparsam, nicht in " \
    "jedem Satz. Antworte in höchstens 2 bis 3 Sätzen, so wie man laut spricht. " \
    "Keine Aufzählungen, kein Markdown, keine Emojis. " \
    "Du hast Zugriff auf eine Websuche: nutze sie, wenn eine aktuelle Information " \
    "nötig ist (Wetter, Nachrichten, Öffnungszeiten, aktuelle Ereignisse, Preise). " \
    "Der Nutzer ist in Deutschland. Fasse die Ergebnisse gesprochen und knapp " \
    "zusammen. Wenn du etwas nicht weißt, sag es geradeheraus."
//  Sender (MP3-Webradio), im Panel bearbeitbar. Einer je Zeile im Format
//  "Schlüssel | Name | URL": nach dem SCHLÜSSEL wird im Satz gesucht, der NAME
//  erscheint auf dem Display, die URL ist der MP3-Strom. Die Reihenfolge zählt:
//  die genaueren Schlüssel zuerst (etwa "alternativ" vor "rock"). Nur
//  MP3-Ströme, der VS1053 decodiert kein AAC: KEINE Adressen auf .aac, .m3u8
//  oder HLS. http:// und https:// sind beide in Ordnung (music.cpp nutzt für
//  https WiFiClientSecure). Mehrere Zeilen mit derselben Adresse sind
//  gleichbedeutende Schlüssel für denselben Sender. Die Sender sind als
//  funktionierend GEPRÜFT (181.fm, SomaFM, Kiss Kiss).
//  OFFEN: die Schlüssel sind noch italienisch ("ottanta", "anni 80"). Das
//  Eindeutschen des Katalogs wurde zurückgestellt, siehe
//  docs/specs/2026-09-12-uebersetzung-deutsch.md.
#define MUSIC_STATIONS_DEF \
    "alternativo | rock alternativo | http://listen.181fm.com/181-buzz_128k.mp3\n" \
    "metal | metal | http://listen.181fm.com/181-hardrock_128k.mp3\n" \
    "ottanta | anni 80 | http://listen.181fm.com/181-awesome80s_128k.mp3\n" \
    "anni 80 | anni 80 | http://listen.181fm.com/181-awesome80s_128k.mp3\n" \
    "novanta | anni 90 | http://listen.181fm.com/181-star90s_128k.mp3\n" \
    "anni 90 | anni 90 | http://listen.181fm.com/181-star90s_128k.mp3\n" \
    "settanta | anni 70 | http://listen.181fm.com/181-70s_128k.mp3\n" \
    "anni 70 | anni 70 | http://listen.181fm.com/181-70s_128k.mp3\n" \
    "country | country | http://listen.181fm.com/181-realcountry_128k.mp3\n" \
    "jazz | jazz | http://listen.181fm.com/181-classicaljazz_128k.mp3\n" \
    "blues | blues | http://listen.181fm.com/181-blues_128k.mp3\n" \
    "reggae | reggae | http://listen.181fm.com/181-reggae_128k.mp3\n" \
    "salsa | salsa | http://listen.181fm.com/181-salsa_128k.mp3\n" \
    "classica | classica | http://listen.181fm.com/181-classical_128k.mp3\n" \
    "dance | dance | http://listen.181fm.com/181-energy98_128k.mp3\n" \
    "lounge | lounge | http://listen.181fm.com/181-chilled_128k.mp3\n" \
    "hip hop | hip hop | http://listen.181fm.com/181-thebox_128k.mp3\n" \
    "indie | indie | http://ice1.somafm.com/indiepop-128-mp3\n" \
    "ambient | ambient | http://ice1.somafm.com/dronezone-128-mp3\n" \
    "kiss kiss | Radio Kiss Kiss | http://ice07.fluidstream.net/KissKiss.mp3\n" \
    "rock | rock | http://listen.181fm.com/181-eagle_128k.mp3\n" \
    "pop | pop | http://listen.181fm.com/181-power_128k.mp3"
//  KI-DIENSTE ZU HAUSE (LM Studio und ähnliche, siehe localai.h). Ab Werk sind
//  sie AUS (leere Adresse): Alexo arbeitet in der Cloud wie bisher. Eingeschaltet
//  werden sie im Panel, indem man die Adresse des PC einträgt; von da an bleibt
//  dieses Glied der Kette zu Hause, sofern der PC antwortet. Ein LEER gelassener
//  Modellname bedeutet "nimm das, was der Server gerade geladen hat".
#define LOCAL_LLM_URL_DEF     ""
#define LOCAL_LLM_MODEL_DEF   ""
//  Temperatur allein des Modells ZU HAUSE (die Cloud nutzt ihre eigene). Niedrig
//  heißt, es bleibt beim wahrscheinlichsten Wort, die Antworten sind nah am
//  Thema und wiederholbar; hoch heißt, es wagt mehr, wird abwechslungsreicher,
//  schweift aber auch eher ab. Server zu Hause starten meist bei 0,7 bis 0,8.
#define LOCAL_LLM_TEMP_DEF    0.3f
#define LOCAL_STT_URL_DEF     ""
#define LOCAL_STT_MODEL_DEF   ""
#define LOCAL_TTS_URL_DEF     ""
#define LOCAL_TTS_MODEL_DEF   ""
#define LOCAL_TTS_VOICE_DEF   ""
//  PORTS DER STIMME ZU HAUSE: fehlt in der Adresse im Panel der Port (etwa
//  "http://192.168.1.50"), probiert Alexo diese der Reihe nach und nimmt den
//  ersten, der antwortet. 8002 gehört zu Kokoro, 8003 zu Chatterbox. So muss das
//  Feld nicht jedes Mal geändert werden, wenn man den einen oder anderen
//  startet. Steht der Port da, wird genau der benutzt und nichts probiert.
#define LOCAL_TTS_PORTS_AUTO  { 8002, 8003 }
//  Der übliche Pfad, wenn die Adresse ohne Port angegeben ist (die Server zu
//  Hause sprechen die Sprache von OpenAI, und die liegt unter /v1).
#define LOCAL_TTS_PATH_AUTO   "/v1"
//  NUR ZU HAUSE: ist das eingeschaltet, verlassen die drei Glieder der
//  Sprachkette NIE das eigene Netz. Fehlt der Dienst zu Hause oder macht er einen
//  Fehler, sagt Alexo es und hört auf, statt stillschweigend in die Cloud
//  auszuweichen, was Stimme, Frage oder Antwort unbemerkt hinausschicken würde.
//  Radio (das fordert man selbst an) und die NTP-Uhr (sie enthält nichts
//  Gesprochenes) sind davon nicht betroffen. Ab Werk AUS.
#define LOCAL_ONLY_DEF        0
//  STIMME IMMER ZU HAUSE: betrifft NUR die Sprachausgabe. Eingeschaltet wird die
//  Stimme immer beim Server zu Hause angefordert, ohne vorherige Prüfung der
//  Erreichbarkeit und ohne Ausweichen auf ElevenLabs. Genau darum geht es: die
//  Freikontingente nicht zu verbrauchen. Spracherkennung und Gehirn bleiben, wie
//  sie sind. Wird übergangen, wenn "nur zu Hause" eingeschaltet ist, dort gilt es
//  ohnehin schon.
#define TTS_LOCAL_ONLY_DEF    0
//  EIGENE ANTWORT: ist REPLY_TRIGGER (ein Wort oder Satzteil) gesetzt und die
//  Frage ENTHÄLT ihn, sagt Alexo den festen Text REPLY_TEXT und überspringt die
//  KI. Ein LEERER Auslöser schaltet das ab, dann antwortet die KI. Gedacht für
//  Scherze, feste Sprüche oder wiederholbare Aufnahmen. Zur Laufzeit über
//  /api/settings änderbar.
#define REPLY_TRIGGER_DEF    ""
#define REPLY_TEXT_DEF       ""

// --- WLAN -------------------------------------------------------------------
//  Zugangsdaten und Schlüssel stehen in include/secrets.h, die NICHT im
//  Repository liegt. secrets.example.h nach secrets.h kopieren und die Werte
//  eintragen.
