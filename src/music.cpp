// ============================================================================
//  ALEXO - Musikwiedergabe (MP3-Webradio -> VS1053). Siehe music.h.
// ============================================================================
#include "music.h"
#include "volume.h"
#include "netlog.h"
#include "webui.h"
#include "mic.h"
#include "ui.h"
#include "settings.h"
#include "gobbo.h"
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <math.h>

// Die Sender lassen sich im Web-Panel BEARBEITEN: sie stehen in
// gSettings.musicStations (einer je Zeile, "Schlüssel | Name | URL", die
// Werkseinstellung in config.h unter MUSIC_STATIONS_DEF). Siehe musicMatch()
// weiter unten.
// OFFEN: die Schlüssel des Katalogs sind noch italienisch, siehe
// docs/specs/2026-09-12-uebersetzung-deutsch.md.

// --- Pegel des Rings WÄHREND der Musik --------------------------------------
//  Dieselbe Rechnung wie beim Reagieren bei Ruhe (micLevelFromChunk): Hochpass,
//  ein UNGLEICH nachgeführter Grundpegel (er fällt schnell und hängt sich damit
//  an die STILLE, steigt aber sehr langsam, damit die Schläge ihn NICHT
//  mitziehen) und eine Hüllkurve. So bleibt die Grundlinie auf dem leisesten
//  gehaltenen Pegel, und die MUSIK ragt darüber hinaus. Der Ring folgt dem Stück
//  und wirkt lebendig. Gegenüber dem Verhalten bei Ruhe unterscheiden sich nur
//  die geringere VERSTÄRKUNG (der nahe Lautsprecher übersteuert sonst) und
//  EIGENE Werte für Anstieg und Abklingen. Die Makros unten rühren gSettings
//  NICHT an, die übrigen Zustände bleiben also so abgestimmt, wie sie waren.
//  Die Stellschrauben: MUSIC_LVL_DIV (Verstärkung; höher heisst zurückhaltender),
//  MUSIC_ATTACK (Anstieg beim Schlag), MUSIC_RELEASE (das Abklingen danach),
//  MUSIC_BASE_MULT und MUSIC_FLOOR (Schwelle über der Stille). s_musMad und
//  s_musBase erscheinen in /api/live.
#define MUSIC_BASE_MULT 1.10f   // Schwelle = Grundlinie mal diesem Wert plus FLOOR
#define MUSIC_FLOOR     80.0f   // kleinster Abstand über der Stille
#define MUSIC_LVL_DIV   10.0f   // rechnet (mad minus Schwelle) auf 0..255 um
#define MUSIC_ATTACK    1.00f   // SOFORTIGER Anstieg beim Schlag, ohne Trägheit
#define MUSIC_RELEASE   0.45f   // Abklingen: wie schnell es danach ausgeht

// Ring WÄHREND der Musik: 1 = reagiert auf das Mikrofon (Werkseinstellung),
// 0 = ein zeitgesteuertes Atmen.
// Zu beachten: das blockierende Lesen des Mikrofons war NICHT die Ursache des
// stockenden Tons. Kiss Kiss und Virgin sind beides MP3 mit 128 kbit/s und
// 48 kHz, aber nur der erste läuft sauber, es liegt also am Server oder am Netz
// und nicht an der Zuführung. Das Reagieren bleibt deshalb, das Atmen ist
// weiterhin als Möglichkeit vorhanden.
#define MUSIC_RING_REACTIVE 1

// In main.cpp definiert: schaltet den Verstärker PAM8302A ein und aus. Hier wird
// es gebraucht, um ihn bei Lautstärke 0 abzuschalten, WÄHREND das Radio läuft,
// sonst bliebe das Rauschen der Endstufe.
void ampEnable(bool on);

static volatile float s_musMad = 0, s_musBase = 0;
float musicLastMad()  { return s_musMad; }
float musicLastBase() { return s_musBase; }

#if MUSIC_RING_REACTIVE
static uint8_t musicLevel(const int16_t *s, size_t n) {
  if (!n) return 0;
  // MAD mit leichtem Hochpass (nimmt Gleichanteil und Brummen), wie in
  // micLevelFromChunk.
  static double hpX = 0, hpY = 0;
  const double hpR = 0.95;
  float acc = 0;
  for (size_t i = 0; i < n; i++) {
    double v = (double)s[i];
    double y = v - hpX + hpR * hpY;
    hpX = v; hpY = y;
    acc += fabsf((float)y);
  }
  float mad = acc / (float)n;
  s_musMad = mad;

  // UNGLEICH nachgeführter Grundpegel: fällt schnell und hängt sich an die
  // Stille, steigt sehr langsam, damit die Schläge ihn nicht hochziehen.
  static float nf = -1.0f;
  if (nf < 0.0f) nf = mad;
  float k = (mad < nf) ? 0.05f : 0.0005f;
  nf += (mad - nf) * k;
  s_musBase = nf;

  float thresh = nf * MUSIC_BASE_MULT + MUSIC_FLOOR;
  float target = (mad - thresh) / MUSIC_LVL_DIV;
  if (target < 0) target = 0; else if (target > 255) target = 255;

  // EIGENE Hüllkurve für die Musik (die Makros oben, nicht die Werte aus
  // gSettings für den Ruhezustand).
  static float env = 0;
  float ek = (target > env) ? MUSIC_ATTACK : MUSIC_RELEASE;
  env += (target - env) * ek;
  int lvl = (int)(env + 0.5f);
  if (lvl < 0) lvl = 0; else if (lvl > 255) lvl = 255;
  return (uint8_t)lvl;
}
#endif  // MUSIC_RING_REACTIVE

static inline bool has(const String &t, const char *w) { return t.indexOf(w) >= 0; }

// true, wenn 'key' in 't' als GANZES WORT vorkommt und nicht an andere Buchstaben
// oder Ziffern geklebt: so löst "rock" nicht innerhalb von "Rocksaum" aus und
// "pop" nicht in "populär". Zweifelsfälle übernimmt dann Claude. 't' und 'key'
// sind bereits klein geschrieben.
static inline bool isWordCh(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
static bool containsWord(const String &t, const String &key) {
  int kl = key.length();
  if (kl == 0) return false;
  int from = 0;
  while (true) {
    int i = t.indexOf(key, from);
    if (i < 0) return false;
    char before = (i > 0) ? t[i - 1] : ' ';
    char after  = (i + kl < (int)t.length()) ? t[i + kl] : ' ';
    if (!isWordCh(before) && !isWordCh(after)) return true;   // saubere Wortgrenzen
    from = i + 1;                                             // steckte in einem Wort: weitersuchen
  }
}

// Zerlegt eine Zeile "Schlüssel | Name | URL" in ihre drei Felder (mit
// Leerzeichen abgeschnitten, der Schlüssel klein geschrieben). Liefert false,
// wenn die Zeile unbrauchbar ist, weil ein Feld fehlt.
static bool parseStationLine(const String &line, String &key, String &nome, String &url) {
  int p1 = line.indexOf('|'); if (p1 < 0) return false;
  int p2 = line.indexOf('|', p1 + 1); if (p2 < 0) return false;
  key  = line.substring(0, p1);      key.trim();  key.toLowerCase();
  nome = line.substring(p1 + 1, p2); nome.trim();
  url  = line.substring(p2 + 1);     url.trim();
  return key.length() && url.length();
}

// Ablage für den gefundenen Sender: MusicStation zeigt auf diese Zeichenketten,
// die bis zum nächsten Aufruf von musicMatch gültig bleiben. Es gibt nur einen
// Aufrufer, runInteraction, also kommt sich nichts in die Quere.
static String s_mUrl, s_mNome;
static MusicStation s_mHit;

const MusicStation *musicMatch(const String &t) {
  // Es braucht eine erkennbare ABSICHT, sonst würde "ich mag Rock" das Radio
  // starten. "strong" ist ein Wort, das eindeutig auf Musik zielt; "verb" ist ein
  // Wort fürs Abspielen, das nur zusammen mit einem Genre zählt.
  bool strong = has(t, "musik") || has(t, "radio") || has(t, "lied") ||
                has(t, "song");
  bool verb   = has(t, "spiel") || has(t, "leg auf") || has(t, "mach an") ||
                has(t, "play") || has(t, "hör") || has(t, "will");
  if (!strong && !verb) return nullptr;

  // Geht die bearbeitbare Liste durch (gSettings.musicStations, ein Sender je
  // Zeile). Die Reihenfolge zählt: die genaueren Schlüssel zuerst, so legt es der
  // Nutzer oder die Werkseinstellung fest.
  const String &list = gSettings.musicStations;
  String key, nome, url, firstNome, firstUrl;
  bool haveFirst = false;
  int start = 0;
  while (start < (int)list.length()) {
    int nl = list.indexOf('\n', start);
    if (nl < 0) nl = list.length();
    String line = list.substring(start, nl); line.trim();
    start = nl + 1;
    if (line.length() == 0 || !parseStationLine(line, key, nome, url)) continue;
    if (!haveFirst) { firstNome = nome; firstUrl = url; haveFirst = true; }
    if (containsWord(t, key)) {                 // Genre erkannt (als ganzes Wort)
      s_mUrl = url; s_mNome = nome;
      s_mHit.url = s_mUrl.c_str(); s_mHit.nome = s_mNome.c_str();
      return &s_mHit;
    }
  }

  // Ausgesprochene Musikabsicht ohne Genre ("spiel Musik") führt auf den ERSTEN
  // Sender der Liste. Kommt kein Treffer zustande, übernimmt das Werkzeug von
  // Claude.
  if (strong && haveFirst) {
    s_mUrl = firstUrl; s_mNome = firstNome;
    s_mHit.url = s_mUrl.c_str(); s_mHit.nome = s_mNome.c_str();
    return &s_mHit;
  }
  return nullptr;   // nur ein Verb, ohne Musik und ohne Genre: kein Befehl
}

// --- GEPRÜFTER interner Katalog (Rückfallweg für Claude) --------------------
//  Passt der Satz nicht zur Liste aus dem Panel, ist aber dennoch ein
//  Musikwunsch, wählt Claude ein GENRE aus diesem Katalog (siehe das Werkzeug in
//  llm.cpp), und hier wird es einem Sender zugeordnet. Alle Adressen sind
//  geprüft und liefern 200 mit audio/mpeg. Claude erfindet keine Adressen, es
//  wählt nur ein Genre, die Adresse stammt von uns.
//  OFFEN: Schlüssel und Namen sind noch italienisch, siehe den Hinweis oben.
#define U_181     "http://listen.181fm.com/"
#define U_EAGLE   U_181 "181-eagle_128k.mp3"
#define U_BUZZ    U_181 "181-buzz_128k.mp3"
#define U_POWER   U_181 "181-power_128k.mp3"
#define U_COUNTRY U_181 "181-realcountry_128k.mp3"
#define U_80S     U_181 "181-awesome80s_128k.mp3"
#define U_90S     U_181 "181-star90s_128k.mp3"
#define U_OLDIES  U_181 "181-greatoldies_128k.mp3"
#define U_METAL   U_181 "181-hardrock_128k.mp3"
#define U_JAZZ    U_181 "181-classicaljazz_128k.mp3"
#define U_CHILL   U_181 "181-chilled_128k.mp3"
#define U_DANCE   U_181 "181-energy98_128k.mp3"
#define U_REGGAE  U_181 "181-reggae_128k.mp3"
#define U_CLASSIC U_181 "181-classical_128k.mp3"
#define U_BLUES   U_181 "181-blues_128k.mp3"
#define U_HIPHOP  U_181 "181-thebox_128k.mp3"
#define U_70S     U_181 "181-70s_128k.mp3"
#define U_SALSA   U_181 "181-salsa_128k.mp3"
#define U_INDIE   "http://ice1.somafm.com/indiepop-128-mp3"
#define U_AMBIENT "http://ice1.somafm.com/dronezone-128-mp3"

// Der SCHLÜSSEL wird als Teil des Genres gesucht, das Claude zurückgibt; das
// verträgt Abwandlungen. Die genaueren und mehrdeutigen Einträge stehen zuerst,
// etwa "jazz" vor "classic" und "alternativ" sowie "metal" vor "rock".
struct CatVoce { const char *key; MusicStation st; };
static const CatVoce CATALOG[] = {
  {"alternativ", {U_BUZZ,    "rock alternativo"}},
  {"metal",      {U_METAL,   "metal"}},
  {"hip",        {U_HIPHOP,  "hip hop"}},
  {"rap",        {U_HIPHOP,  "hip hop"}},
  {"jazz",       {U_JAZZ,    "jazz"}},
  {"loung",      {U_CHILL,   "lounge"}},
  {"chill",      {U_CHILL,   "chill"}},
  {"relax",      {U_CHILL,   "relax"}},
  {"dance",      {U_DANCE,   "dance"}},
  {"reggae",     {U_REGGAE,  "reggae"}},
  {"classic",    {U_CLASSIC, "classica"}},
  {"blues",      {U_BLUES,   "blues"}},
  {"country",    {U_COUNTRY, "country"}},
  {"ottanta",    {U_80S,     "anni 80"}},
  {"anni 80",    {U_80S,     "anni 80"}},
  {"novanta",    {U_90S,     "anni 90"}},
  {"anni 90",    {U_90S,     "anni 90"}},
  {"settanta",   {U_70S,     "anni 70"}},
  {"anni 70",    {U_70S,     "anni 70"}},
  {"oldies",     {U_OLDIES,  "oldies"}},
  {"salsa",      {U_SALSA,   "salsa"}},
  {"indie",      {U_INDIE,   "indie"}},
  {"ambient",    {U_AMBIENT, "ambient"}},
  {"rock",       {U_EAGLE,   "rock"}},
  {"pop",        {U_POWER,   "pop"}},
};

const MusicStation *musicFromGenre(const String &genere) {
  String g = genere; g.toLowerCase();
  for (const CatVoce &v : CATALOG)
    if (g.indexOf(v.key) >= 0) return &v.st;
  return nullptr;   // Genre steht nicht im Katalog
}

// Liste der Genres des Katalogs, für die Beschreibung des Werkzeugs von Claude
// (llm.cpp).
String musicCatalogList() {
  return F("rock, rock alternativo, metal, pop, anni 70, anni 80, anni 90, country, "
           "jazz, lounge, dance, reggae, salsa, classica, blues, hip hop, indie, "
           "ambient, oldies");
}

// Aus dem Web-Panel angeforderter Halt und der Zustand "läuft gerade" (beides
// wird in musicPlay gelesen).
static volatile bool s_stopWeb = false;
static volatile bool s_playing = false;
static volatile int  s_seekWeb = 0;      // aus dem Web-Panel angeforderter Senderwechsel
static volatile bool s_startWeb = false;  // aus dem Web-Panel angeforderter Start
void musicRequestStop() { s_stopWeb = true; }
void musicRequestStart() { s_startWeb = true; }
bool musicTakeStartRequest() { bool r = s_startWeb; s_startWeb = false; return r; }
void musicRequestSeek(int delta) { if (delta) s_seekWeb = (delta > 0) ? 1 : -1; }
bool musicIsPlaying()   { return s_playing; }

// --- ICY-Metadaten (Sendername und Titel aus dem Strom) ---------------------
static String s_station;      // icy-name (Name des Senders)
static String s_nowPlaying;   // aktueller StreamTitle ("Interpret - Titel")
String musicStation()    { return s_station; }
String musicNowPlaying() { return s_nowPlaying; }

// Liest StreamTitle='...' aus dem Metadatenblock; hat er sich geändert, wird er
// übernommen und auf dem TFT angezeigt. 'meta' ist der Block als
// nullterminierte Zeichenkette.
static void parseStreamTitle(const char *meta) {
  const char *p = strstr(meta, "StreamTitle='");
  if (!p) return;
  p += 13;
  const char *e = strstr(p, "';");
  if (!e) return;
  String full;
  for (const char *q = p; q < e; q++) full += *q;
  full.trim();
  if (full.length() == 0 || full == s_nowPlaying) return;
  s_nowPlaying = full;   // vollständig "Interpret - Titel" (für /api/live im Panel)

  // "Interpret - Titel" für die Anzeige AUF SENDUNG auf zwei Zeilen aufteilen.
  String artist = "", title = full;
  int sep = full.indexOf(" - ");
  if (sep >= 0) {
    artist = full.substring(0, sep);   artist.trim();
    title  = full.substring(sep + 3);  title.trim();
  }
  gobboNowPlaying(s_station.c_str(), title.c_str(), artist.c_str());
  Serial.printf("[music] auf Sendung: %s\n", full.c_str());
  netlogPrintln((String("[music] auf Sendung: ") + full).c_str());
}

// Liest einen ICY-Metadatenblock: ein Byte Länge (in Einheiten zu 16) und dann
// der Text. Er muss IMMER vollständig gelesen werden, sonst gerät der Ton aus dem
// Tritt. Behalten werden nur die ersten rund 500 Zeichen, denn StreamTitle ist
// kurz. Die meisten Blöcke haben die Länge 0.
static void readIcyMetadata(WiFiClient *stream) {
  uint8_t lenByte = 0;
  if (stream->readBytes(&lenByte, 1) != 1) return;
  int metaLen = (int)lenByte * 16;
  if (metaLen == 0) return;                        // keine Änderung (der Normalfall)
  static char meta[512];
  int got = 0, remaining = metaLen;
  uint8_t tmp[64];
  while (remaining > 0) {
    int chunk = remaining > (int)sizeof(tmp) ? (int)sizeof(tmp) : remaining;
    int r = stream->readBytes(tmp, chunk);
    if (r <= 0) break;
    for (int i = 0; i < r && got < (int)sizeof(meta) - 1; i++) meta[got++] = (char)tmp[i];
    remaining -= r;
  }
  meta[got] = 0;
  parseStreamTitle(meta);
}

int musicPlay(VS1053 &player, const char *url, bool (*stopRequested)(), int (*seekRequested)()) {
  int seekOut = 0;                 // != 0 = Ausstieg zum WECHSELN des Senders; 0 = Halt oder Ende
  s_stopWeb = false;               // alte Halt-Anforderungen übergehen
  s_seekWeb = 0;                   // dasselbe für den Senderwechsel aus dem Panel
  // Quelle über HTTP oder HTTPS: viele Sender liegen auf https.
  // WiFiClientSecure leitet sich von WiFiClient ab, deshalb genügt eine Referenz
  // auf die Basisklasse, und TLS läuft ohne Zertifikatsprüfung, wie bei
  // Spracherkennung, Claude und Sprachausgabe. Die reinen http-Adressen, etwa
  // 181.fm, bleiben beim gewöhnlichen Client.
  bool isHttps = String(url).startsWith("https");
  WiFiClient       clientPlain;
  WiFiClientSecure clientTls;
  if (isHttps) clientTls.setInsecure();
  WiFiClient &client = isHttps ? (WiFiClient&)clientTls : clientPlain;
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    Serial.printf("[music] Verbindungsaufbau fehlgeschlagen: %s\n", url);
    netlogPrintln((String("[music] Verbindungsaufbau fehlgeschlagen: ") + url).c_str());
    return 0;
  }
  // Die ICY-Metadaten werden mit angefordert (Sendername und Titel). Der Server
  // schiebt sie alle "icy-metaint" Byte in den Strom, im Loop trennen wir sie
  // wieder vom Ton.
  static const char *ICY_HDRS[] = { "icy-metaint", "icy-name", "Content-Type", "icy-br" };
  http.collectHeaders(ICY_HDRS, 4);
  http.addHeader("Icy-MetaData", "1");
  int code = http.GET();
  if (code != 200) {
    // Meldung über Telnet: HTTP-Code (oder ein negativer Fehler des HTTPClient)
    // und der Inhaltstyp. So sieht man, WARUM ein Sender nicht startet, etwa
    // wegen Umleitung, 403 oder TLS.
    Serial.printf("[music] HTTP %d bei %s\n", code, url);
    netlogPrintln((String("[music] FAIL HTTP=") + code + " ct=" +
                   http.header("Content-Type") + " url=" + url).c_str());
    http.end();
    return 0;
  }
  int metaint = http.header("icy-metaint").toInt();   // 0, wenn der Strom keine schickt
  s_station    = http.header("icy-name");
  s_nowPlaying = "";
  gobboNowPlaying(s_station.c_str(), "", "");   // Sender sofort; Titel und Interpret mit den Metadaten
  WiFiClient *stream = http.getStreamPtr();
  player.setVolume(volumeVsValue());
  s_playing = true;
  // AUSFÜHRLICHE Zeile über Telnet, für die Fehlersuche aus der Ferne:
  // Sendername, Bitrate, Inhaltstyp, metaint und Adresse. Der Sendername
  // (icy-name) steht IMMER in der Kopfzeile, auch wenn der Strom keinen
  // StreamTitle mitschickt. Früher fehlte der Name über Telnet oft.
  String br = http.header("icy-br");
  String det = String("[music] spielt: ") + (s_station.length() ? s_station : String("(ohne Namen)")) +
               (br.length() ? String(" [") + br + "k]" : String("")) +
               " ct=" + http.header("Content-Type") +
               (isHttps ? " https" : " http") +
               " metaint=" + metaint + " url=" + url;
  Serial.println(det);
  netlogPrintln(det.c_str());

  uint8_t buf[512];
  uint32_t idle = millis();
  uint32_t lastLvl = 0;
  bool ampOn = !volumeIsMuted();   // Zustand des Verstärkers: folgt der Stummschaltung
  int bytesToMeta = metaint;   // Tonbytes bis zum nächsten Metadatenblock (0 = keine Trennung)
  while (http.connected() || (stream && stream->available())) {
    if (stopRequested && stopRequested()) break;   // Klick auf den Drehgeber hält an
    if (s_stopWeb) break;                           // Halt-Schaltfläche im Panel
    if (s_seekWeb) { seekOut = s_seekWeb; s_seekWeb = 0; break; }  // Schaltflächen im Panel
    if (seekRequested) {                            // gedrückt und gedreht wechselt den Sender
      int sd = seekRequested();
      if (sd != 0) { seekOut = sd; break; }
    }
    ArduinoOTA.handle();                            // Funkweg bleibt auch während der Musik offen
    netlogHandle();
    webuiHandle();                                  // das Panel bleibt während der Musik erreichbar
    volumeApplyPending(player);                     // Lautstärke im Betrieb nachführen

    // Der Verstärker folgt der Stummschaltung unmittelbar: bei Lautstärke 0 wird
    // er abgeschaltet, damit das Rauschen der Endstufe verschwindet, und beim
    // Aufdrehen wieder eingeschaltet. Nur bei einem WECHSEL, denn ampEnable
    // wartet beim Einschalten 20 ms, und ein Aufruf in jedem Durchgang würde die
    // Tonzuführung aushungern.
    bool wantAmp = !volumeIsMuted();
    if (wantAmp != ampOn) { ampEnable(wantAmp); ampOn = wantAmp; }

    // Der Ring während der MUSIK, alle etwa 30 ms aufgefrischt. Der Schalter für
    // das Reagieren auf Geräusche im Panel (gSettings.idleReactive) schaltet den
    // Ring auch hier ein und aus: steht er auf aus, bleiben die LED dunkel und
    // das Mikrofon wird gar nicht erst gelesen. Genau wie bei stumm.
    if (millis() - lastLvl >= 30) {
      lastLvl = millis();
      if (volumeIsMuted() || !gSettings.idleReactive) {
        uiSetLevel(0);                              // stumm oder Reagieren aus: Ring dunkel
      } else {
#if MUSIC_RING_REACTIVE
        // Reagiert auf das Mikrofon. Achtung, das Lesen blockiert und kann die
        // Tonzuführung aushungern, dann stockt der Ton.
        static int16_t mbuf[160];
        micFlush();                                 // frischer Pegel, ohne Nachlauf zum Ton
        size_t g = micReadChunk(mbuf, 160);
        if (g) uiSetLevel(musicLevel(mbuf, g));
#else
        // Zeitgesteuertes Atmen: das Mikrofon wird nicht gelesen, die Zuführung
        // zum VS1053 hungert also NIE, und der Ton bleibt glatt. Eine langsame
        // Welle von etwa 2,6 Sekunden.
        float ph = (float)(millis() % 2600) / 2600.0f;
        uiSetLevel((uint8_t)(35.0f + 120.0f * (0.5f - 0.5f * cosf(ph * 6.2831853f))));
#endif
      }
    }

    int avail = stream->available();
    if (avail > 0) {
      int toRead = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
      // Die Grenze zum nächsten Metadatenblock nicht überschreiten, sonst
      // vermischen sich Ton- und Metadatenbytes.
      if (metaint > 0 && toRead > bytesToMeta) toRead = bytesToMeta;
      int c = stream->readBytes(buf, toRead);
      if (c > 0) {
        player.playChunk(buf, c); idle = millis();   // Ton an den VS1053
        if (metaint > 0) {
          bytesToMeta -= c;
          if (bytesToMeta <= 0) { readIcyMetadata(stream); bytesToMeta = metaint; }
        }
      }
    } else {
      if (millis() - idle > 8000) break;            // Strom abgerissen oder Zeit abgelaufen
      delay(2);
    }
  }
  http.end();
  s_playing = false;
  s_nowPlaying = ""; s_station = "";
  uiSetLevel(0);   // Ring aus: die Musik ist zu Ende

  // Stille zum Ausklingen, damit der Decoder leerläuft (wie bei der Sprachausgabe)
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 4; i++) player.playChunk(buf, sizeof(buf));
  Serial.println("[music] angehalten");
  netlogPrintln("[music] angehalten");
  return seekOut;
}

// --- Bewegen in der Senderliste des Panels (gedrückt und gedreht) -----------
//  Gehen gSettings.musicStations durch (die im Web-Panel bearbeitbaren Sender)
//  und benutzen dafür parseStationLine erneut. Die Liste ist klein, sie wird bei
//  jedem Aufruf frisch zerlegt.
int musicStationCount() {
  const String &list = gSettings.musicStations;
  String key, nome, url;
  int n = 0, start = 0;
  while (start < (int)list.length()) {
    int nl = list.indexOf('\n', start); if (nl < 0) nl = list.length();
    String line = list.substring(start, nl); line.trim();
    start = nl + 1;
    if (line.length() && parseStationLine(line, key, nome, url)) n++;
  }
  return n;
}

bool musicStationGet(int idx, String &outUrl, String &outNome) {
  if (idx < 0) return false;
  const String &list = gSettings.musicStations;
  String key, nome, url;
  int n = 0, start = 0;
  while (start < (int)list.length()) {
    int nl = list.indexOf('\n', start); if (nl < 0) nl = list.length();
    String line = list.substring(start, nl); line.trim();
    start = nl + 1;
    if (line.length() && parseStationLine(line, key, nome, url)) {
      if (n == idx) { outUrl = url; outNome = nome; return true; }
      n++;
    }
  }
  return false;
}

int musicStationIndexOf(const char *url) {
  if (!url) return -1;
  const String &list = gSettings.musicStations;
  String key, nome, u;
  int n = 0, start = 0;
  while (start < (int)list.length()) {
    int nl = list.indexOf('\n', start); if (nl < 0) nl = list.length();
    String line = list.substring(start, nl); line.trim();
    start = nl + 1;
    if (line.length() && parseStationLine(line, key, nome, u)) {
      if (u == url) return n;
      n++;
    }
  }
  return -1;
}
