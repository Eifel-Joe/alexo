// ============================================================================
//  ALEXO - Sprachassistent (Haupt-Firmware)
//
//  Der Ablauf: Weckwort oder Klick -> ZUHÖREN (aufnehmen) -> DENKEN (Whisper
//  und Claude) -> SPRECHEN (ElevenLabs über den VS1053) -> zurück zur Ruhe.
//
//  Zwei Kerne: der Loop auf Kern 1 führt diese Kette aus; die Animationen des
//  Rings laufen in einer eigenen Aufgabe auf Kern 0 (siehe ui.cpp) und bleiben
//  dadurch flüssig, auch während Kern 1 auf das Netz wartet.
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_NeoPixel.h>
#include <VS1053.h>

#include "config.h"
#include "mic.h"
#include "net.h"
#include "stt.h"
#include "llm.h"
#include "tts.h"
#include "ui.h"
#include "sound.h"
#include "gobbo.h"
#include "encoder.h"
#include "volume.h"
#include "netlog.h"
#include "wakeword.h"
#include "tfltest.h"
#include "settings.h"
#include "localai.h"
#include "webui.h"
#include "music.h"

// --- Zweite Stimme ----------------------------------------------------------
// BEGINNT der Satz mit dem Auslösewort, antwortet Alexo mit der zweiten Stimme
// statt mit der voreingestellten (siehe tts.cpp). Beides ist zur Laufzeit
// änderbar: gSettings.voiceIdAlt und gSettings.voiceTrigger über das Web-Panel;
// die Werkseinstellungen stehen in config.h unter ELEVEN_VOICE_ALT_DEF und
// VOICE_TRIGGER_DEF.

// --- Angeschlossene Bauteile ------------------------------------------------
//  Das TFT hängt an einem EIGENEN SPI-Bus (HSPI), getrennt vom VS1053, der am
//  globalen SPI-Bus (FSPI) sitzt. So streitet der Bildlauf des Chats auf Kern 0
//  nicht mit der Tonzuführung auf Kern 1. MISO braucht das Display nicht, es
//  wird nur beschrieben, deshalb -1.
SPIClass tftSPI(HSPI);
Adafruit_ST7735 display(&tftSPI, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
Adafruit_NeoPixel ring(LED_RING_COUNT, LED_RING_PIN, NEO_GRB + NEO_KHZ800);

// Unterklasse zur Fehlersuche: in der Bibliothek ist `read_register` geschützt.
// Wir machen es zugänglich, um die SCI-Register des VS1053 zu lesen und auf dem
// Display zu zeigen, sodass sich auch ohne serielle Verbindung nachsehen lässt.
// VS1053Diag IST in jeder Hinsicht ein VS1053, am übrigen Code ändert sich also
// nichts.
class VS1053Diag : public VS1053 {
public:
  VS1053Diag(uint8_t cs, uint8_t dcs, uint8_t dreq) : VS1053(cs, dcs, dreq) {}
  uint16_t readReg(uint8_t r) { return read_register(r); }
  // Liest ein SCI-Register OHNE auf DREQ zu warten (read_register wartet aktiv
  // auf DREQ und bliebe hängen). Das hilft bei der Suche, wenn DREQ auf LOW
  // liegt: antwortet der Baustein trotzdem, LEBT er und ist versorgt, dann liegt
  // der Fehler ALLEIN an der DREQ-Leitung. Kommt 0000 oder FFFF zurück,
  // antwortet der Baustein nicht, es geht also um Versorgung oder Reset.
  uint16_t readRegNoWait(uint8_t r) {
    control_mode_on();
    SPI.write(3); SPI.write(r);
    uint16_t v = ((uint16_t)SPI.transfer(0xFF) << 8) | SPI.transfer(0xFF);
    control_mode_off();
    return v;
  }
};
VS1053Diag player(VS1053_XCS_PIN, VS1053_XDCS_PIN, VS1053_DREQ_PIN);

// Zeitpunkt, an dem der letzte TON aus dem Lautsprecher endete, sei es Musik
// oder Sprache. Unmittelbar danach kommt der Nachhall aus dem Lautsprecher
// wieder ins Mikrofon und kann einen ungewollten Start auslösen, etwa ein
// falsches Weckwort. Für die Dauer von AUDIO_COOLDOWN_MS wird deshalb JEDER
// Start übergangen, auch der per Klick.
static uint32_t g_audioEndMs = 0;
static const uint32_t AUDIO_COOLDOWN_MS = 1500;

// --- Stato ------------------------------------------------------------------
bool tftOk = false;
bool vsOk  = false;
bool micOk = false;

// Abbruchbedingung der Aufnahme: es wird aufgenommen, solange KEIN Klick zum
// Beenden vom Drehgeber kommt. Weckwort und Stille können diese Bedingung später
// ersetzen, ohne den Rest der Kette anzufassen.
static bool recKeepGoing() { return !gobboStopRequested(); }

// Wechselt den Zustand und frischt SOWOHL den Ring (die Animationen) ALS AUCH
// die Kopfleiste des TFT auf.
static inline void setState(AlexoState s) { uiSetState(s); gobboSetState(s); }

// FORTLAUFENDER CHAT: während auf die nächste Frage gewartet wird, bleibt der
// Ring im Hinweiszustand (ST_FOLLOWUP, bernsteinfarbenes Atmen) und zeigt KEINE
// Aussteuerung, die ja bedeuten würde "ich nehme dich bereits auf". Sobald
// wirklich gesprochen wird, erkannt an derselben nachgeführten Schwelle wie beim
// Abbruch bei Stille und nicht an einem hier erfundenen Pegel, geht es ins
// gewöhnliche Zuhören über. true, solange gewartet wird.
static bool g_attendo = false;
static void recLevel(uint8_t l) {
  if (g_attendo && micVoiceStarted()) { g_attendo = false; setState(ST_LISTENING); }
  uiSetLevel(l);
}

// Typische Halluzinationen von Whisper auf Stille und Rauschen: enthält die
// Aufnahme keine Sprache, "erfindet" Whisper solche Sätze. Sie werden verworfen,
// damit kein Gespräch mit einem Geistersatz beginnt. Die Liste lässt sich im
// Web-Panel bearbeiten (gSettings.hallucTerms, durch Komma getrennt). Verglichen
// wird der bereinigte Text, also klein geschrieben und ohne Satzzeichen und
// Leerzeichen an den Rändern, und es muss GENAU einer der Sätze der Liste sein
// (oder der Text ist leer).
static bool isAllucinazione(const String &testo) {
  String s = testo; s.toLowerCase(); s.trim();
  while (s.length() && strchr(".!?,;:- ", s[s.length() - 1])) s.remove(s.length() - 1);
  s.trim();
  if (s.length() == 0) return true;
  const String &csv = gSettings.hallucTerms;
  int start = 0;
  while (start <= (int)csv.length()) {
    int comma = csv.indexOf(',', start);
    if (comma < 0) comma = csv.length();
    String term = csv.substring(start, comma);
    term.trim(); term.toLowerCase();
    if (term.length() > 0 && s.equals(term)) return true;
    start = comma + 1;
  }
  return false;
}

// Vergleicht 't' (BEREITS klein geschrieben) mit einer durch Komma getrennten
// Liste von Begriffen (etwa "gut, ok, hallo"). Bei contains=false trifft es zu,
// wenn 't' mit einem Begriff BEGINNT (so arbeitet die zweite Stimme); bei
// contains=true, wenn 't' einen Begriff ENTHÄLT. Leere Begriffe werden
// übergangen; eine leere Liste trifft nie zu und schaltet die Funktion damit
// ab.
static bool matchAnyTerm(const String &t, const String &csv, bool contains) {
  int start = 0;
  while (start <= (int)csv.length()) {
    int comma = csv.indexOf(',', start);
    if (comma < 0) comma = csv.length();
    String term = csv.substring(start, comma);
    term.trim(); term.toLowerCase();
    if (term.length() > 0 && (contains ? (t.indexOf(term) >= 0) : t.startsWith(term)))
      return true;
    start = comma + 1;
  }
  return false;
}

// EIGENE ANTWORT: ist der Auslöser (gSettings.replyTrigger) gesetzt und die Frage
// ENTHÄLT ihn (klein geschrieben verglichen), kommt der feste Text
// (gSettings.replyText) zurück und die KI wird übersprungen. Ein leerer Auslöser
// schaltet das ab, dann antwortet die KI. Gedacht für Scherze und Aufnahmen.
static String customReplyMatch(const String &testo) {
  String trig = gSettings.replyTrigger; trig.trim(); trig.toLowerCase();
  if (trig.isEmpty()) return "";
  String tl = testo; tl.toLowerCase();
  if (tl.indexOf(trig) >= 0) return gSettings.replyText;
  return "";
}

// --- Display ----------------------------------------------------------------
//  Startanzeige während des Hochfahrens, bevor der Teleprompter das TFT
//  übernimmt. Gezeichnet wird direkt aufs TFT: an dieser Stelle läuft noch alles
//  in einer einzigen Aufgabe, nämlich setup().
void tftStatus(const char *line1, const char *line2 = nullptr) {
  if (!tftOk) return;
  display.fillScreen(ST77XX_BLACK);
  display.setTextSize(2);
  display.setTextColor(ST77XX_CYAN);
  display.setCursor(0, 0);
  display.println("ALEXO");
  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 30);
  display.println(line1);
  if (line2) { display.setCursor(0, 42); display.println(line2); }
}

static void setRingSolid(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < LED_RING_COUNT; i++) ring.setPixelColor(i, r, g, b);
  ring.show();
}

// --- Verstärker PAM8302A (SD ist LOW-aktiv: HIGH = an, LOW = stumm) ---------
//  Er läuft nur, wenn Ton kommt, also bei Signaltönen und Sprache, und bleibt
//  bei Ruhe stumm, damit das Rauschen der Endstufe verschwindet. Eingeschaltet
//  wird er einen Augenblick VOR dem Ton, damit das Knacken in die Stille fällt.
//  NICHT static: music.cpp ruft ihn ebenfalls auf, um ihn bei Lautstärke 0
//  während der Wiedergabe abzuschalten.
void ampEnable(bool on) {
#if AMP_SD_PIN >= 0
  digitalWrite(AMP_SD_PIN, on ? HIGH : LOW);
  if (on) delay(20);   // kurz einschwingen lassen, bevor Ton kommt
#endif
}

// --- Fehlerbehandlung (Rückmeldung und zurück zur Ruhe) ---------------------
static void fail(const char *msg) {
  Serial.printf(">> %s\n", msg);
  setState(ST_ERROR);
  gobboPrint(String("[") + msg + "]");
  if (vsOk) { ampEnable(true); soundError(player); delay(50); ampEnable(false); }
  delay(1200);
  setState(ST_IDLE);
}

// --- Startbild beim Einschalten (Anzeigetafel auf dem TFT, der Ring "lädt") -
#if SPLASH_BOOT
static void bootSplash() {
  if (!tftOk) return;
  const int W = TFT_WIDTH, H = TFT_HEIGHT;            // 128 x 160
  const uint16_t BLK = ST77XX_BLACK;
  const uint16_t CY  = display.color565(0, 255, 255); // kräftiges Cyan
  const uint16_t CYd = display.color565(0, 70, 85);   // schwaches Cyan (das Raster)
  const uint16_t CYm = display.color565(0, 150, 170); // mittleres Cyan
  const uint16_t MAG = display.color565(255, 0, 170); // Magenta als Akzent

  display.fillScreen(BLK);

  // 1) Eine Linie wandert von oben nach unten und lässt ein schwaches Raster zurück
  for (int y = 0; y < H; y += 4) {
    display.drawFastHLine(0, y, W, CYd);
    display.drawFastHLine(0, y + 2, W, CY);
    delay(6);
    display.drawFastHLine(0, y + 2, W, BLK);
  }
  for (int x = 0; x <= W; x += 16) display.drawFastVLine(x, 0, H, CYd);

  // 2) Eckwinkel im Stil einer Anzeigetafel
  const int b = 12;
  display.drawFastHLine(2, 2, b, CY);          display.drawFastVLine(2, 2, b, CY);
  display.drawFastHLine(W - 2 - b, 2, b, CY);  display.drawFastVLine(W - 3, 2, b, CY);
  display.drawFastHLine(2, H - 3, b, CY);      display.drawFastVLine(2, H - 3 - b, b, CY);
  display.drawFastHLine(W - 2 - b, H - 3, b, CY); display.drawFastVLine(W - 3, H - 3 - b, b, CY);

  // 3) Der "Reaktor": Kreise, die sich ausdehnen, und ein Punkt, der im Ring umläuft
  const int cx = W / 2, cy = 52;
  for (int r = 3; r <= 36; r += 3) {
    display.drawCircle(cx, cy, r, (r % 6 == 0) ? CY : CYd);
    int idx = (r / 3) % LED_RING_COUNT;
    ring.clear();
    ring.setPixelColor(idx, 0, 45, 55);
    ring.setPixelColor((idx + 1) % LED_RING_COUNT, 0, 14, 18);
    ring.show();
    delay(28);
  }
  display.fillCircle(cx, cy, 6, CY);
  display.drawCircle(cx, cy, 10, CYm);

  // 4) "ALEXO" Buchstabe für Buchstabe mit Schatten, danach die Unterzeile
  display.setTextSize(3);
  const char *name = "ALEXO";
  const int chW = 18, tw = 5 * chW, tx = (W - tw) / 2, ty = 96;
  for (int i = 0; i < 5; i++) {
    display.setTextColor(CYd); display.setCursor(tx + i * chW + 1, ty + 1); display.write(name[i]);
    display.setTextColor(CY);  display.setCursor(tx + i * chW,     ty);     display.write(name[i]);
    ring.clear();
    for (int k = 0; k <= i * 2 && k < LED_RING_COUNT; k++) ring.setPixelColor(k, 0, 38, 48);
    ring.show();
    delay(130);
  }
  display.setTextSize(1);
  display.setTextColor(CYm);
  const char *sub = "SPRACHASSISTENT";
  display.setCursor((W - (int)strlen(sub) * 6) / 2, ty + 26);
  display.print(sub);

  // 5) Fortschrittsbalken, der Ring füllt sich im gleichen Verhältnis
  const int bx = 12, by = 144, bw = W - 24, bh = 7;
  display.drawRect(bx, by, bw, bh, CYm);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(bx + 14, by - 11); display.print("SYSTEMSTART");
  for (int p = 0; p <= bw - 4; p += 3) {
    display.fillRect(bx + 2, by + 2, p, bh - 4, CY);
    int lit = (p * LED_RING_COUNT) / (bw - 4);
    ring.clear();
    for (int k = 0; k < lit && k < LED_RING_COUNT; k++) ring.setPixelColor(k, 0, 32, 42);
    ring.show();
    delay(10);
  }

  // 6) Abschliessendes Pulsieren des Rings, das ausklingt
  display.setTextColor(MAG);
  //display.setCursor(bx + bw - 42, by - 11); display.print("PRONTO");
  display.setCursor(bx + bw - 42, by - 11); display.print("");
  for (int v = 60; v >= 0; v -= 6) {
    for (int k = 0; k < LED_RING_COUNT; k++) ring.setPixelColor(k, 0, v, (uint8_t)(v * 1.2f));
    ring.show();
    delay(22);
  }
  ring.clear(); ring.show();
  delay(250);
}
#endif

// Startet einen Radiosender: zeigt den Namen an, setzt den Zustand ST_MUSIC
// (der Ring reagiert), spielt bis zum Anhalten und kehrt danach SAUBER zur Ruhe
// zurück, also Verstärker stumm, Mikrofonpuffer geleert gegen ein falsches
// Weckwort und eine Abkühlzeit. Benutzt wird sie sowohl beim Treffer in der
// Liste des Panels als auch beim Rückfallweg über den Katalog von Claude. Der
// Verstärker läuft bereits, er kommt aus der Zuhörphase von runInteraction.
static void playStation(const MusicStation *st) {
  // URL und Name werden in eigene Zeichenketten kopiert: die Quelle (s_mHit oder
  // der Katalog) würde beim Senderwechsel von musicStationGet überschrieben.
  String url = st->url, nome = st->nome;
  // Der Platz in der Liste des Panels für den Senderwechsel; -1, wenn der Sender
  // nicht in der Liste steht, etwa weil Claude ihn aus dem Katalog gewählt hat.
  // Der erste Wechsel steigt dann an einem Ende der Liste ein.
  int idx = musicStationIndexOf(url.c_str());

  // Bei Lautstärke 0 wird der Verstärker abgeschaltet: die Musik ist ohnehin
  // still, und so verschwindet das Brummen der Endstufe, das sonst ins Mikrofon
  // käme. Ist nicht stumm geschaltet, läuft der Verstärker bereits aus der
  // Zuhörphase.
  ampEnable(!volumeIsMuted());
  setState(ST_MUSIC);
  gobboClearStopRequest();

  // Schleife über die Sender: musicPlay liefert 0 bei Halt oder Ende, sonst die
  // Richtung des Wechsels. Dann geht es zum vorherigen oder nächsten Sender der
  // Liste und von vorn los.
  for (;;) {
    Serial.printf(">> MUSIK: %s (%s)\n", nome.c_str(), url.c_str());
    gobboPrint(String("\xE2\x99\xAA ") + nome);   // "♪ <nome>"
    int seek = musicPlay(player, url.c_str(),
                         []() { return gobboTakeTalkRequest(); },       // Klick hält an
                         []() { return (int)gobboTakeMusicSeek(); });   // gedrückt und gedreht wechselt
    if (seek == 0) break;                          // Halt oder Ende des Stroms
    int n = musicStationCount();
    if (n <= 0) break;                             // Liste leer: hinaus
    if (idx < 0) idx = (seek > 0) ? 0 : n - 1;     // nicht in der Liste: an einem Ende einsteigen
    else { idx = (idx + seek) % n; if (idx < 0) idx += n; }   // im Kreis vor und zurück
    if (!musicStationGet(idx, url, nome)) break;
  }

  ampEnable(false);
  for (int i = 0; i < 10; i++) { micFlush(); delay(40); }
  wakeReset();
  gobboTakeTalkRequest();        // verwirft einen angesammelten Klick
  gobboTakeMusicSeek();          // verwirft übrige Wechselwünsche, damit nicht gleich weitergesprungen wird
  gobboClearStopRequest();
  g_audioEndMs = millis();       // beginnt die Abkühlzeit im Loop
  setState(ST_IDLE);
}

// --- Ein vollständiger Wortwechsel (zuhören, denken, sprechen) --------------
//  followUp bedeutet, dass dies eine Anschlussfrage innerhalb eines bereits
//  offenen Chats ist: der Ring zeigt "du dran", und gewartet wird nur
//  CHAT_FOLLOWUP_MS statt der sonst üblichen Zeit. Liefert true, wenn eine
//  Antwort gesprochen wurde, wenn es sich also lohnt, das Mikrofon für die
//  nächste Frage erneut zu öffnen.
static bool runInteraction(bool followUp) {
  // 1) ZUHÖREN
  gobboClearStopRequest();   // alte Klicks übergehen: es zählt erst der nächste
  g_attendo = followUp;
  setState(followUp ? ST_FOLLOWUP : ST_LISTENING);  // ein Klick hier beendet die Aufnahme
  if (followUp) micSetNoVoiceMs(CHAT_FOLLOWUP_MS);  // spricht niemand, wird geschlossen statt gewartet
  if (vsOk) ampEnable(true); // Verstärker an: Ton und Stimme kommen durch, danach stumm
  if (vsOk) soundStart(player);

  uint32_t t0 = millis();
  size_t   n  = micRecord(REC_MAX_MS, recKeepGoing, recLevel, gSettings.recSilenceMs);
  g_attendo = false;
  if (vsOk) soundStop(player);

  size_t wavLen = 0;
  const uint8_t *wav = micWav(&wavLen);
  float secs = (float)n / (float)micSampleRate();
  Serial.printf("   Abtastwerte=%u  Dauer=%.2fs (%lums)  Spitze=%d/32767  WAV=%u Byte\n",
                (unsigned)n, secs, (unsigned long)(millis() - t0), micLastPeak(),
                (unsigned)wavLen);

  // zu kurz: vermutlich versehentlich ausgelöst
  if (n < (size_t)(micSampleRate() / 4)) {
    if (vsOk) ampEnable(false);   // Verstärker stumm, bevor es zur Ruhe geht
    setState(ST_IDLE);
    return false;
  }
  // Keine ECHTE Sprache erkannt, nur Stille oder Rauschen: vermutlich ein
  // ungewollter Start durch ein falsches Weckwort oder einen Klick. Das geht
  // NICHT an Whisper, das dort halluzinieren und einen Chat starten würde.
  // Stattdessen still zurück zur Ruhe.
  // Im fortlaufenden Chat ist das zugleich der übliche Ausgang: sind die drei
  // Sekunden verstrichen, ohne dass jemand spricht, oder beendet ein Klick die
  // leere Aufnahme, endet es hier und das Gespräch schliesst sich.
  if (!micHeardVoice()) {
    Serial.println(">> keine Sprache erkannt: übergangen, ohne Whisper zu fragen");
    if (vsOk) ampEnable(false);
    setState(ST_IDLE);
    return false;
  }
  if (!wifiOk()) { fail("Kein WLAN"); return false; }

  // 2) DENKEN - Spracherkennung
  setState(ST_THINKING);
  String testo = sttTranscribe(wav, wavLen, "de");
  if (testo.isEmpty()) { fail("Nicht verstanden"); return false; }
  // Filter gegen die Halluzinationen von Whisper auf Stille: stillschweigend
  // verwerfen, ohne zu antworten und ohne etwas neu zu starten.
  if (isAllucinazione(testo)) {
    Serial.printf(">> Halluzination der Spracherkennung verworfen: \"%s\"\n", testo.c_str());
    if (vsOk) ampEnable(false);
    setState(ST_IDLE);
    return false;
  }
  Serial.printf(">> TESTO: \"%s\"\n", testo.c_str());
  gobboPrintUser(testo);   // zeigt "Du: ..." im Chat

  // 2b) EIGENE ANTWORT: enthält die Frage den eingestellten Auslöser
  // (gSettings.replyTrigger), wird der feste Text (replyText) genommen und
  // sowohl die Musik als auch Claude ÜBERSPRUNGEN.
  String risposta = customReplyMatch(testo);
  bool scripted = risposta.length() > 0;

  if (!scripted) {
    // MUSIK: trifft die Liste aus dem Panel, läuft es sofort und schnell, ohne
    // Claude zu bemühen. Das Anhalten über Klick oder Panel und die saubere
    // Rückkehr zur Ruhe übernimmt playStation().
    {
      String tLowerMus = testo; tLowerMus.toLowerCase();
      const MusicStation *st = musicMatch(tLowerMus);
      if (st && vsOk) { playStation(st); return false; }   // die Musik beendet den Chat
    }

    // 3) DENKEN - das Gehirn. Es bekommt das Musik-Werkzeug, sofern der VS1053
    // vorhanden ist: einen Musikwunsch, den die Liste nicht erkannt hat, löst
    // Claude auf, indem es ein Genre aus dem Katalog wählt, statt zu antworten.
    // So landet der Wunsch nicht als Gesprächsbeitrag.
    String musicGenre;
    risposta = llmAsk(testo, vsOk ? &musicGenre : nullptr);

    // Hat Claude sich für Musik entschieden?
    if (vsOk && musicGenre.length()) {
      const MusicStation *cs = musicFromGenre(musicGenre);
      if (cs) { playStation(cs); return false; }
      // Das Genre fehlt im Katalog: lieber sagen als schweigen
      Serial.printf(">> Musik: Genre \"%s\" steht nicht im Katalog\n", musicGenre.c_str());
      risposta = String("Für ") + musicGenre + " habe ich leider keinen Sender.";
    }
  }

  if (risposta.isEmpty()) { fail("Fehler beim Denken"); return false; }

  Serial.printf(">> ALEXO: \"%s\"\n", risposta.c_str());
  // Ein vorangestelltes "[LOC]" heisst, dass der Server zu Hause diese Antwort
  // spricht. Das steht nur auf dem Bildschirm, im Chat auf dem TFT und im Panel;
  // der Text an die Sprachausgabe bleibt unberührt.
  gobboPrint(ttsUsesLocal() ? String("[LOC] ") + risposta : risposta);

  // 4) SPRECHEN
  if (vsOk) {
    setState(ST_SPEAKING);
    // Beginnt DEIN Satz mit dem Auslösewort, spricht die zweite Stimme.
    String t = testo; t.trim(); t.toLowerCase();
    bool vocaltra = matchAnyTerm(t, gSettings.voiceTrigger, false);  // beginnt mit einem der Begriffe
    Serial.printf("[tts] Stimme: %s\n", vocaltra ? "zweite" : "voreingestellt");
    bool detto = ttsSpeak(player, risposta, vocaltra ? gSettings.voiceIdAlt : String(""));
    delay(50); ampEnable(false);   // die Stille ausklingen lassen, dann stumm schalten
    // Es kam keine Stimme heraus, etwa weil der Server zu Hause bei "nur zu
    // Hause" nicht läuft oder die Cloud einen Fehler meldet. Behandelt wird das
    // wie bei Spracherkennung und Gehirn: roter Ring und ein Ton, nicht Stille.
    if (!detto) { fail("Fehler bei der Stimme"); return false; }
    // Der Nachhall der STIMME kommt ins Mikrofon zurück: dieselbe Abkühlzeit wie
    // bei der Musik, damit nicht gleich nach der Antwort ein Gespräch aus dem
    // Nichts beginnt.
    g_audioEndMs = millis();
  } else {
    Serial.println(">> VS1053 nicht angeschlossen: die Sprachausgabe entfällt");
  }
  setState(ST_IDLE);
  return true;   // die Antwort wurde gesprochen: das Mikrofon darf wieder auf
}

// --- Gespräch: eine Frage oder viele hintereinander -------------------------
//  Ist der fortlaufende Chat eingeschaltet, öffnet das Mikrofon nach einer
//  Antwort von allein, und die nächste Frage braucht nicht erneut "Hey Jarvis".
//  Es endet auf drei Wegen, die es alle schon gab: niemand spricht innerhalb von
//  CHAT_FOLLOWUP_MS, ein Klick auf den Drehgeber beendet die leere Aufnahme, was
//  so zählt wie Schweigen, oder es tritt ein Fehler auf.
static void runConversation() {
  bool ancora = runInteraction(false);
  int giro = 0;
  while (ancora && gSettings.chatContinua) {
    // Die Runden werden protokolliert: sollte es eines Tages so wirken, als
    // öffne sich der Chat von selbst, steht hier, wie oft es tatsächlich
    // geschah. Eine Runde ist eine gesprochene Antwort.
    char msg[48];
    snprintf(msg, sizeof(msg), "[chat] fortlaufend: Runde %d, du bist dran", ++giro);
    Serial.println(msg);
    netlogPrintln(msg);
    // Der Nachhall der eben gesprochenen Antwort kommt ins Mikrofon zurück.
    // Würde es sofort wieder öffnen, hörte Alexo sich selbst reden und eine
    // Frage aus dem Nichts entstünde. Dieselbe Vorkehrung wie beim Verlassen
    // der Musik.
    for (int i = 0; i < 10; i++) { micFlush(); delay(40); }
    gobboClearStopRequest();
    ancora = runInteraction(true);
  }
  if (giro) {
    char msg[48];
    snprintf(msg, sizeof(msg), "[chat] nach %d Runden geschlossen", giro);
    Serial.println(msg);
    netlogPrintln(msg);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ALEXO ===");
  Serial.printf("PSRAM: %u bytes (%s)  Flash: %u bytes\n",
                (unsigned)ESP.getPsramSize(), ESP.getPsramSize() > 0 ? "OK" : "NO",
                (unsigned)ESP.getFlashChipSize());

  // Die zur Laufzeit änderbaren Einstellungen aus dem Web-Panel werden aus dem
  // NVS geladen, die Werkseinstellung steht in config.h. Das gehört VOR die
  // Module, die gSettings lesen: Mikrofon, Weckwort, Gehirn und Sprachausgabe.
  settingsBegin();

  // Die Hintergrundbeleuchtung hängt an einem GPIO und geht sofort an, damit das
  // Startbild zu sehen ist. Ab hier übernimmt der Teleprompter das Ein- und
  // Ausschalten bei Ruhe.
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  // TFT ST7735 am eigenen SPI-Bus (HSPI). MISO wird nicht gebraucht, es wird
  // nur geschrieben.
  tftSPI.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, TFT_CS_PIN);
  display.initR(TFT_INITR);
  display.setSPISpeed(TFT_SPI_HZ);   // niedriger Takt streut weniger auf den Ring (siehe config.h)
  display.setRotation(0);            // 0 = Hochformat 128x160
  display.cp437(true);
  display.setTextWrap(false);
  tftOk = true;                      // der ST7735 meldet keine Verbindung, wir nehmen sie an
  display.fillScreen(ST77XX_BLACK);  // schwarz bis zum Startbild, damit kein Müll erscheint
  Serial.println("[OK ] TFT ST7735");

  // Der Ring läuft mit VOLLER Helligkeit (255), damit das Ausblenden nicht in
  // Stufen zerfällt; wirklich hell wird es nicht, denn die Animationen selbst
  // halten die Werte niedrig (ui.cpp).
  ring.begin();
  ring.setBrightness(255);
  setRingSolid(0, 0, 20);
  Serial.println("[OK ] Ring NeoPixel");

#if SPLASH_BOOT
  bootSplash();                      // Startbild auf dem TFT, der Ring "lädt" dazu
#endif

  // Es gibt keine eigene Taste: der Drehgeber ist die einzige Bedienung. Klick
  // startet und beendet den Chat, gedrückt und gedreht regelt die Lautstärke,
  // freies Drehen blättert. GPIO14 bleibt frei.

  // VS1053
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

  // --- Robustes Einrichten des VS1053/VS1003 mit verlässlicher Diagnose -----
  //  Die Steueranschlüsse werden SOFORT als Ausgang gesetzt. begin() täte das
  //  auch, aber wenn es bei einem Start nicht aufgerufen wird, lieferten die
  //  SCI-Lesezugriffe FALSCHE Nullen, weil der Baustein ohne getriebenes CS gar
  //  nicht ausgewählt ist. Mit bereits gesetzten CS-Leitungen ist das rohe Lesen
  //  über readRegNoWait(), das NICHT auf DREQ wartet, IMMER verlässlich: es sagt
  //  wirklich, ob der Baustein antwortet.
  pinMode(VS1053_XCS_PIN,  OUTPUT); digitalWrite(VS1053_XCS_PIN,  HIGH);
  pinMode(VS1053_XDCS_PIN, OUTPUT); digitalWrite(VS1053_XDCS_PIN, HIGH);
  pinMode(VS1053_DREQ_PIN, INPUT);
#if VS1053_XRST_PIN >= 0
  pinMode(VS1053_XRST_PIN, OUTPUT);
#endif

  delay(300);                        // lässt die Versorgung beim Kaltstart einschwingen
  vsOk = false;
  uint16_t vsSt = 0, vsMode = 0;
  for (int i = 0; i < 8 && !vsOk; i++) {
#if VS1053_XRST_PIN >= 0
    digitalWrite(VS1053_XRST_PIN, LOW);  delay(60);   // langer Reset über die Hardware (XRST)
    digitalWrite(VS1053_XRST_PIN, HIGH);
#endif
    // Dem Baustein Zeit lassen, aus dem Reset zu kommen und anzulaufen: bis zu
    // 400 ms lang wird SCI_STATUS roh und ohne DREQ abgefragt. Beim KALTSTART
    // braucht er mitunter länger, das liegt an Versorgung und Schwingquarz des
    // Moduls.
    uint32_t t = millis();
    do { vsSt = player.readRegNoWait(0x1); delay(5); }
    while ((vsSt == 0x0000 || vsSt == 0xFFFF) && (millis() - t) < 400);
    bool responds = !(vsSt == 0x0000 || vsSt == 0xFFFF);
    if (responds) {
      player.begin();                 // Baustein vorhanden und wach: vollständig einrichten
      delay(20);
      vsMode = player.readRegNoWait(0x0);
      vsOk = (vsMode == 0x4800);
    } else {
      delay(150);                     // antwortet nicht: warten und mit Reset erneut versuchen
    }
    Serial.printf("[vs1053] Versuch %d: ST=%04X MODE=%04X %s\n",
                  i + 1, (unsigned)vsSt, (unsigned)vsMode,
                  vsOk ? "OK" : (responds ? "(neu einrichten)" : "(Baustein antwortet nicht)"));
  }
  if (vsOk) volumeBegin(player);   // gespeicherte Lautstärke (oder VOLUME_DEFAULT) an den VS1053
  Serial.printf("[%s] VS1053\n", vsOk ? "OK " : "ERR");

  // Der Verstärker PAM8302A ist beim Start stumm (SD ist LOW-aktiv), so knackt
  // und rauscht nichts.
#if AMP_SD_PIN >= 0
  pinMode(AMP_SD_PIN, OUTPUT);
  digitalWrite(AMP_SD_PIN, LOW);
#endif

  // Mikrofon
  micOk = micBegin();
  Serial.printf("[%s] Mikrofon %s\n", micOk ? "OK " : "ERR",
                MIC_USE_I2S ? "I2S (ICS-43434)" : "MAX4466");

#if WAKE_ENABLE
  // Weckwort "Hey Jarvis", erkannt im Gerät selbst (siehe WAKEWORD.md). Liefert
  // false, wenn es sich nicht einrichten lässt, etwa ohne PSRAM.
  bool wakeOk = wakeBegin();
  Serial.printf("[%s] Wake word\n", wakeOk ? "OK " : "off");
#endif

  // WiFi
  tftStatus("WLAN...", "verbinde");
  bool wifi = wifiBegin();
  Serial.printf("[%s] WLAN\n", wifi ? "OK " : "ERR");

  // Aktualisierung der Firmware über Funk (Netzname "alexo" -> alexo.local)
  if (wifi) {
    timeBegin();   // Uhr über NTP: Claude braucht sie für Fragen nach der Zeit
    ArduinoOTA.setHostname("alexo");
    // ArduinoOTA.setPassword("...");   // optional: mit Passwort schützen
    ArduinoOTA.onStart([]() {                        // die grüne Anzeige dazu
      gobboOtaKind(ArduinoOTA.getCommand() == U_SPIFFS);   // FW (Firmware) oder DATA (Dateisystem)
      gobboOtaProgress(0);
      setState(ST_OTA);
    });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
      gobboOtaProgress(t ? (uint8_t)((uint64_t)p * 100 / t) : 0);          // gut sichtbar in der Mitte
    });
    ArduinoOTA.onEnd([]()    { gobboOtaProgress(100); });                  // danach startet das Gerät neu
    ArduinoOTA.onError([](ota_error_t){ setState(ST_IDLE); gobboPrint("[OTA Fehler]"); });
    ArduinoOTA.begin();
    Serial.println("[OK ] Aktualisierung über Funk bereit (alexo.local)");
    // Protokoll über das Netz (Telnet, Port 23), um die Ausgabe ohne USB zu
    // lesen. Siehe netlog.h. Verbinden mit `telnet alexo.local`.
    netlogBegin();
    Serial.println("[OK ] Telnet-Protokoll bereit (telnet alexo.local)");
    // Einstellungs-Panel im Browser (LittleFS und Schnittstelle). http://alexo.local/
    webuiBegin();
  }

  // Schritt 0 des Weckworts: die Messung des Mikrofonrauschens. Steht MIC_DIAG
  // auf 1, bleibt die Firmware HIER in einer Schleife stehen, die Messung,
  // ArduinoOTA.handle() und netlogHandle() abwechselt. Es geht NICHT weiter, es
  // gibt also keinen Chat, aber der Funkweg BLEIBT offen und die Ausgabe geht
  // auch über Telnet. Zum Beenden MIC_DIAG wieder auf 0 setzen und über Funk neu
  // flashen.
#if TFL_SELFTEST
  // Selbsttest für TFLite Micro (Schritt 2 des Weckworts): lässt das Modell
  // hello_world in einer Schleife laufen und gibt über Telnet aus, der Funkweg
  // bleibt offen. Zum Beenden TFL_SELFTEST auf 0 setzen und neu flashen.
  Serial.println("[tfl] SELBSTTEST TFLite Micro AKTIV (TFL_SELFTEST=1). Funk und Telnet offen.");
  for (;;) {
    ArduinoOTA.handle();
    netlogHandle();
    tflSelfTest();
    delay(1500);
  }
#endif

#if WAKE_TEST
  // Test der Weckwortkette ohne Mikrofon: Merkmalsberechnung, Modell und
  // Wahrscheinlichkeit über künstlichen Ton. Der Funkweg bleibt offen. Zum
  // Beenden WAKE_TEST auf 0 setzen und neu flashen.
  Serial.println("[wakeTest] TEST DER WECKWORTKETTE AKTIV (WAKE_TEST=1). Funk und Telnet offen.");
  for (;;) {
    ArduinoOTA.handle();
    netlogHandle();
    wakeSelfTest();   // DAUERHAFTES Zuhören: keine Verzögerung, das Modell arbeitet im Strom
  }
#endif

#if MIC_DIAG
  Serial.println("[micDiag] MESSBETRIEB AKTIV (MIC_DIAG=1). Funk und Telnet offen.");
  for (;;) {
    ArduinoOTA.handle();
    netlogHandle();
    if (micOk) micDiag();
    else { Serial.println("[micDiag] Mikrofon nicht eingerichtet");
           netlogPrintln("[micDiag] Mikrofon nicht eingerichtet"); delay(1000); }
  }
#endif

  // Die Verbindung steht: Display leeren und den Teleprompter mit dem
  // blätterbaren Chat starten, der ab hier als einziger aufs TFT schreibt
  // (eine Aufgabe auf Kern 0).
  if (tftOk) gobboBegin(&display);

  // Drehgeber zum Blättern im Chat (drehen bewegt sich im Verlauf, die Taste
  // führt ans Ende)
  encoderBegin();

  // Startet die Animationen des Rings (eine Aufgabe auf Kern 0) und geht in Ruhe
  uiBegin(&ring);
  setState(ST_IDLE);
}

void loop() {
  static uint32_t lastInteraction = 0;
  static bool     convActive = false;

  if (wifiOk()) { ArduinoOTA.handle();   // nimmt Aktualisierungen über Funk entgegen
                  netlogHandle();        // hält den Telnet-Client (Protokoll über das Netz)
                  webuiHandle(); }       // bedient das Einstellungs-Panel

  // Übernimmt Änderungen der Lautstärke vom Drehgeber. Hier ruht das Gerät;
  // während des Sprechens erledigt das ttsSpeak auf demselben Kern und Bus.
  if (vsOk) volumeApplyPending(player);

  // Die Schaltfläche "Radio einschalten" im Web-Panel: die HTTP-Bearbeitung
  // hinterlässt nur den Wunsch, denn musicPlay blockiert Kern 1, solange das
  // Radio läuft. Gestartet wird hier, auf dem ERSTEN Sender der Liste; gewechselt
  // wird danach über die Schaltflächen oder den Klick auf den Drehgeber, genau
  // wie bei Musik, die per Sprache angefordert wurde.
  if (musicTakeStartRequest() && vsOk) {
    String murl, mnome;
    if (musicStationGet(0, murl, mnome)) {
      MusicStation st = { murl.c_str(), mnome.c_str() };
      playStation(&st);
    }
  }

  // Ein DOPPELKLICK auf den Drehgeber schaltet das Reagieren des Rings auf
  // Geräusche ein und aus. Der Zustand liegt in gSettings.idleReactive
  // (Werkseinstellung in config.h), wird damit mit dem Web-Panel geteilt und im
  // NVS gespeichert.
  if (encoderDoublePressed()) {
    gSettings.idleReactive = !gSettings.idleReactive;
    settingsSave();
    Serial.printf("[ring] reagiert auf Geräusche: %s\n", gSettings.idleReactive ? "AN" : "AUS");
    netlogPrintln(gSettings.idleReactive ? "[ring] reagiert AN" : "[ring] reagiert AUS");
    for (int b = 0; b < 2; b++) { uiSetLevel(150); delay(80); uiSetLevel(0); delay(80); }  // Blinken zur Bestätigung
    if (micOk) micFlush();   // verwirft den Ton während des Blinkens, sonst löst das Weckwort aus
  }

  // Ein Klick auf den Drehgeber bei Ruhe startet den Chat. Ein weiterer Klick
  // beendet die Aufnahme, das erledigt runInteraction über recKeepGoing.
  // Das && verbraucht den Wunsch in jedem Fall; liegt er in der Abkühlzeit nach
  // der Musik, wird er verworfen, damit kein leerer Chat startet.
  if (gobboTakeTalkRequest() && (millis() - g_audioEndMs) > AUDIO_COOLDOWN_MS) {
    if (!micOk) {
      fail("Mikrofon nicht bereit");
    } else {
      runConversation();
      micFlush(); wakeReset();   // verwirft den angesammelten Ton gegen ein falsches Weckwort
      lastInteraction = millis();
      convActive = true;
    }
  }

  // Start des Chats: der Klick auf den Drehgeber oben bleibt IMMER parallel aktiv.
#if WAKE_ENABLE
  // Weckwort "Hey Jarvis": DAUERHAFTES Zuhören am Mikrofon in Blöcken von etwa
  // 20 ms. Bei einer Erkennung startet der Chat wie bei einem Klick auf den
  // Drehgeber. DERSELBE Block steuert auch den Pegel des Rings, damit der
  // I2S-Bus nicht zweimal gelesen wird, was dem Modell im Strombetrieb die
  // Hälfte des Tons nähme.
  if (micOk && wakeReady()) {
    static int16_t wbuf[320];
    size_t got = micReadChunk(wbuf, 320);
    if (got) {
      if (wakeFeed(wbuf, got) && (millis() - g_audioEndMs) > AUDIO_COOLDOWN_MS) {
        Serial.println("[wake] *** WECKWORT! Chat startet ***");
        netlogPrintln("[wake] *** WECKWORT! Chat startet ***");
        runConversation();
        micFlush(); wakeReset();   // verwirft den Ton der Antwort, damit es sich nicht selbst weckt
        lastInteraction = millis();
        convActive = true;
      }
  #if IDLE_REACTIVE
      // Der Pegel des Rings aus DEMSELBEN Block (Hochpass, nachgeführter
      // Grundpegel und Hüllkurve). Aus, wenn der Doppelklick ihn abgeschaltet
      // hat.
      uiSetLevel(gSettings.idleReactive ? micLevelFromChunk(wbuf, got) : 0);
  #endif
    }
  }
#elif IDLE_REACTIVE
  // Reagieren bei Ruhe ohne Weckwort: der Ring "tanzt" zum Ton. micPeekLevel
  // liest den I2S-Bus mit Hochpass und muss in JEDEM Durchgang aufgefrischt
  // werden, sonst bleibt g_level stehen.
  if (micOk) uiSetLevel(gSettings.idleReactive ? micPeekLevel() : 0);
#endif

  // Kontrollgang über die Dienste zu Hause, einer nach dem anderen etwa alle
  // 20 Sekunden. Das hält die Punkte in der Kopfleiste ehrlich, auch wenn gerade
  // nichts gefragt wird. Bei ausgeschaltetem PC kostet das einige hundert
  // Millisekunden, in denen das Mikrofon nicht gelesen wird. Deshalb wird gleich
  // danach der angesammelte Ton verworfen und sauber neu begonnen, sonst fände
  // das Weckwort ein Loch mitten im Wort.
  if (wifiOk() && localRefreshTick() && micOk) { micFlush(); wakeReset(); }

  // Nach zwei Minuten ohne Bedienung wird das Gedächtnis geleert, das Gespräch
  // beginnt dann von vorn
  if (convActive && (millis() - lastInteraction) > 120000) {
    llmReset();
    convActive = false;
    Serial.println("[mem] Gespräch zurückgesetzt (nichts geschehen)");
  }

  delay(8);
}
