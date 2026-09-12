// ============================================================================
//  ALEXO - Text-to-Speech -> MP3-Strom an den VS1053
//  Das eintreffende MP3 wird stückweise an den VS1053 gegeben, während es
//  ankommt (geringe Verzögerung, kein riesiger Puffer). Zwei Wege:
//    CLOUD  ElevenLabs (geklonte Stimme)
//    ZU HAUSE  OpenAI-kompatibler Server im eigenen Netz (siehe localai.h),
//           ohne Schlüssel
//  Zu Hause wird nur gesprochen, wenn im Panel eine Adresse steht UND der PC
//  antwortet; macht der lokale Server einen Fehler, übernimmt ElevenLabs. In
//  beiden Fällen braucht es MP3: mehr decodiert der VS1053 nicht.
// ============================================================================
#include "tts.h"
#include "secrets.h"
#include "gobbo.h"
#include "volume.h"
#include "settings.h"
#include "localai.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mic.h"
#include "ui.h"
#include <math.h>

// Pegel des LED-Rings WÄHREND der Sprachausgabe: liest das Mikrofon (wie bei
// Musik) und leitet daraus einen Aussteuerungswert ab. WENIGER empfindlich als
// bei Musik (großer VOICE_LVL_DIV), weil die Stimme laut ist. Hochpass, MAD,
// automatischer Grundpegel und Hüllkurve; fällt in den Pausen ab.
#define VOICE_LVL_DIV   30.0f
#define VOICE_FLOOR     90.0f
static uint8_t voiceLevel(const int16_t *s, size_t n) {
  if (!n) return 0;
  static double hpX = 0, hpY = 0; const double hpR = 0.95;
  float acc = 0;
  for (size_t i = 0; i < n; i++) {
    double v = (double)s[i];
    double y = v - hpX + hpR * hpY; hpX = v; hpY = y;
    acc += fabsf((float)y);
  }
  float mad = acc / (float)n;
  static float nf = -1.0f;
  if (nf < 0.0f) nf = mad;
  float k = (mad < nf) ? 0.05f : 0.0005f;
  nf += (mad - nf) * k;
  float thresh = nf + VOICE_FLOOR;
  float target = (mad - thresh) / VOICE_LVL_DIV;
  if (target < 0) target = 0; else if (target > 255) target = 255;
  static float env = 0;
  float ek = (target > env) ? 0.7f : 0.55f;  // schneller Anstieg, noch schnelleres Abklingen
  env += (target - env) * ek;
  return (uint8_t)(env + 0.5f);
}

// Die Standardstimme ist zur LAUFZEIT änderbar (gSettings.voiceId, über das
// Web-Panel); die Werkseinstellung steht in config.h (ELEVEN_VOICE_DEF).
#define ELEVEN_MODEL    "eleven_flash_v2_5"       // schnell, 0,5 Credits je Zeichen
#define TTS_MS_PER_CHAR 65        // geschätzte Sprechdauer: ms je Zeichen
                                  // (für den Gleichlauf Anzeige<->Stimme)

// Bereitet den Text für die AUSSPRACHE auf (nur für die Sprachausgabe, NICHT
// für das Display: dort bleiben "36°C" und "19,5" stehen). Schreibt die Zeichen
// aus, die die Stimmen falsch lesen. Für einen neuen Fall: eine weitere
// Bedingung in der Schleife. Der Eingangstext ist UTF-8 (etwa "°" = 0xC2 0xB0).
// Versucht ab in[i] eine UHRZEIT "H:MM" oder "HH:MM" zu lesen. Passt sie, wird
// die gesprochene Form an 'out' angehängt und die Zahl der verbrauchten Zeichen
// zurückgegeben, sonst 0 (und 'out' bleibt unberührt). Regeln:
//   00:00 -> "Mitternacht"   12:00 -> "Mittag"
//   HH:00 -> "HH Uhr"        HH:MM -> "HH Uhr MM"   (Stunde 0 -> "null")
// Grenzen: Stunde 0-23, Minute zweistellig 0-59; danach darf keine weitere
// Ziffer und kein ':' folgen, damit lange Zahlen und Zeiten mit Sekunden
// (HH:MM:SS) unangetastet bleiben.
static int leggiOrario(const String &in, int i, int n, String &out) {
  int j = i, oreDigits = 0;
  while (j < n && isDigit((uint8_t)in[j]) && oreDigits < 2) { j++; oreDigits++; }
  if (oreDigits < 1) return 0;
  int colon = j;
  if (colon >= n || in[colon] != ':') return 0;
  if (colon + 2 >= n) return 0;
  if (!isDigit((uint8_t)in[colon + 1]) || !isDigit((uint8_t)in[colon + 2])) return 0;
  int after = colon + 3;
  if (after < n && (isDigit((uint8_t)in[after]) || in[after] == ':')) return 0;

  int H = in.substring(i, colon).toInt();
  int M = ((uint8_t)in[colon + 1] - '0') * 10 + ((uint8_t)in[colon + 2] - '0');
  if (H > 23 || M > 59) return 0;

  if      (H == 0  && M == 0) out += "Mitternacht";
  else if (H == 12 && M == 0) out += "Mittag";
  else if (M == 0)            { out += String(H); out += " Uhr"; }        // HH:00
  else if (H == 0)            { out += "null Uhr "; out += String(M); }   // 00:MM
  else                        { out += String(H); out += " Uhr "; out += String(M); }  // HH:MM
  return after - i;   // verbrauchte Zeichen (Stunde + ':' + die 2 Minutenziffern)
}

// Versucht ab in[i] ein DATUM zu lesen. Passt es, wird die gesprochene Form
// "T <Monat> JJJJ" an 'out' angehängt und die Zahl der verbrauchten Zeichen
// zurückgegeben, sonst 0. Der Monat wird zum Wort, Tag und Jahr bleiben
// Ziffern: die Stimmen lesen die von allein richtig, und so muss hier niemand
// Zahlen ausschreiben können.
//   10/3/2026 - 01.12.1992 - 12-09-1989  ->  "10 März 2026" usw.
//   2026-08-16 (umgekehrt, wie Maschinen es schreiben) -> "16 August 2026"
// Das Trennzeichen darf / . oder - sein, muss aber beide Male DASSELBE sein:
// "1.500/3" ist kein Datum.
// WARUM: ohne diese Regel las die Bruchregel weiter unten Datumsangaben mit
// Schrägstrich als "10 durch 3 durch 2026", und die mit Punkt oder Bindestrich
// wurden Zeichen für Zeichen vorgelesen.
// Die Grenzen sind ENG, damit echte Brüche ("3/4") und Tausender ("1.500.000")
// nicht hineinfallen: Tag 1-31, Monat 1-12, Jahr GENAU vierstellig.
static const char *MESI_VOCE[] = { "Januar", "Februar", "März", "April",
                                   "Mai", "Juni", "Juli", "August",
                                   "September", "Oktober", "November", "Dezember" };
// Liest ab j bis zu 'max' Ziffern; liefert deren Anzahl (0 = keine) und rückt j vor.
static int cifreDa(const String &in, int n, int &j, int max) {
  int c = 0;
  while (j < n && isDigit((uint8_t)in[j]) && c < max) { j++; c++; }
  return c;
}

static int leggiData(const String &in, int i, int n, String &out) {
  int j = i;
  int p1 = i, c1 = cifreDa(in, n, j, 4);          // Tag, oder Jahr wenn vierstellig
  if (c1 < 1 || j >= n) return 0;
  const char sep = in[j];
  if (sep != '/' && sep != '.' && sep != '-') return 0;

  int p2 = ++j, c2 = cifreDa(in, n, j, 3);        // Monat: immer ein- oder zweistellig
  if (c2 < 1 || c2 > 2 || j >= n || in[j] != sep) return 0;

  int p3 = ++j, c3 = cifreDa(in, n, j, 5);
  if (j < n && (in[j] == sep || in[j] == ':')) return 0;   // noch ein Teil: kein Datum

  String sG, sA;
  if (c1 <= 2 && c3 == 4)        { sG = in.substring(p1, p1 + c1); sA = in.substring(p3, p3 + c3); }
  else if (c1 == 4 && c3 <= 2)   { sA = in.substring(p1, p1 + c1); sG = in.substring(p3, p3 + c3); }
  else return 0;

  int G = sG.toInt(), M = in.substring(p2, p2 + c2).toInt();
  if (G < 1 || G > 31 || M < 1 || M > 12) return 0;

  out += String(G); out += ' '; out += MESI_VOCE[M - 1]; out += ' '; out += sA;
  return j - i;
}

// Versucht ab in[i] eine Zahl mit TAUSENDERPUNKT zu lesen ("230.000"). Passt
// sie, wird dieselbe Zahl ohne Punkte ("230000") an 'out' angehängt und die
// Zahl der verbrauchten Zeichen zurückgegeben, sonst 0.
// WARUM: ElevenLabs richtet sich Zahlen selbst her, die Stimmen zu Hause nicht.
// Kokoro liest den Punkt wörtlich ("zweihundertdreißig Punkt nullnullnull").
// Regel: 1-3 Ziffern, dann eine oder mehrere Gruppen von GENAU 3 Ziffern nach
// einem Punkt, und nach der letzten Gruppe weder Ziffer noch weiterer Punkt. So
// bleiben der englische Dezimalpunkt ("3.14", die Gruppe hat keine 3 Ziffern)
// und Netzwerkadressen ("192.168.1.50", nach der letzten Gruppe folgt noch ein
// Punkt) außen vor.
static int leggiMigliaia(const String &in, int i, int n, String &out) {
  int j = i, cifre = 0;
  while (j < n && isDigit((uint8_t)in[j]) && cifre < 4) { j++; cifre++; }
  if (cifre < 1 || cifre > 3) return 0;

  int gruppi = 0;
  while (j + 3 < n && in[j] == '.' &&
         isDigit((uint8_t)in[j + 1]) && isDigit((uint8_t)in[j + 2]) &&
         isDigit((uint8_t)in[j + 3])) {
    if (j + 4 < n && isDigit((uint8_t)in[j + 4])) return 0;   // Gruppe mit 4+: kein Trenner
    j += 4; gruppi++;
  }
  if (gruppi == 0) return 0;
  if (j < n && (isDigit((uint8_t)in[j]) || in[j] == '.')) return 0;

  for (int k = i; k < j; k++) if (in[k] != '.') out += in[k];
  return j - i;
}

// Abkürzungen von Masseinheiten: "km" -> "Kilometer". Ohne das lesen die
// Stimmen sie buchstabierend oder englisch. Die LÄNGSTEN zuerst: "km/h" muss
// vor "km" stehen, sonst bleibt "Kilometer durch h" übrig (dasselbe gilt für
// "m2" gegenüber "m" und "cm" gegenüber "c...").
// FÜR EINE WEITERE: eine Zeile hier, Einzahl und Mehrzahl. Der Vergleich
// ignoriert Groß- und Kleinschreibung (die Modelle schreiben "km" oder "KM").
// Das letzte Feld ist 'serveNum': steht dort true, gilt die Abkürzung nur mit
// einer ZAHL davor. Das brauchen die einbuchstabigen Kürzel, die sonst Unsinn
// anrichten: aus "Antwort: g" würde "Antwort: Gramm", aus "Punkt h" "Punkt
// Stunde". Mit einer Zahl davor ("3 h") ist dagegen wirklich die Einheit
// gemeint.
struct UnitaVoce { const char *abbr; const char *sing; const char *plur; bool serveNum; };
static const UnitaVoce UNITA[] = {
  { "km/h", "Kilometer pro Stunde", "Kilometer pro Stunde", false },
  { "km\xC2\xB2", "Quadratkilometer", "Quadratkilometer",  false },
  { "kcal", "Kilokalorie",         "Kilokalorien",        false },
  { "kwh",  "Kilowattstunde",      "Kilowattstunden",     false },
  { "khz",  "Kilohertz",           "Kilohertz",           false },
  { "mhz",  "Megahertz",           "Megahertz",           false },
  { "ghz",  "Gigahertz",           "Gigahertz",           false },
  { "min",  "Minute",              "Minuten",             true  },
  { "cm\xC2\xB2", "Quadratzentimeter", "Quadratzentimeter", false },
  { "m\xC2\xB2",  "Quadratmeter",  "Quadratmeter",        false },
  { "m\xC2\xB3",  "Kubikmeter",    "Kubikmeter",          false },
  { "qm",   "Quadratmeter",        "Quadratmeter",        false },
  { "km",   "Kilometer",           "Kilometer",           false },
  { "kg",   "Kilogramm",           "Kilogramm",           false },
  { "kw",   "Kilowatt",            "Kilowatt",            false },
  { "kb",   "Kilobyte",            "Kilobyte",            false },
  { "mb",   "Megabyte",            "Megabyte",            false },
  { "gb",   "Gigabyte",            "Gigabyte",            false },
  { "tb",   "Terabyte",            "Terabyte",            false },
  { "cm",   "Zentimeter",          "Zentimeter",          false },
  { "mm",   "Millimeter",          "Millimeter",          false },
  { "ml",   "Milliliter",          "Milliliter",          false },
  { "mg",   "Milligramm",          "Milligramm",          false },
  { "hz",   "Hertz",               "Hertz",               false },
  { "h",    "Stunde",              "Stunden",             true  },
  { "m",    "Meter",               "Meter",               true  },
  { "l",    "Liter",               "Liter",               true  },
  { "g",    "Gramm",               "Gramm",               true  },
  { "s",    "Sekunde",             "Sekunden",            true  },
};

// Beginnt bei in[i] eine dieser Abkürzungen ALS EIGENES WORT (nicht "kmart",
// nicht "okm"), wird die gesprochene Form geschrieben und die Zahl der zu
// verbrauchenden Zeichen zurückgegeben.
// Einzahl nur, wenn davor genau "1" oder "ein/eine/einen/einem" steht: "21 km"
// ist Mehrzahl, ein Blick auf die letzte Ziffer genügt also nicht.
// Der Punkt wird NICHT verbraucht: "230 km." ist meist ein Satzende, und ohne
// den Punkt fiele die Pause weg.
static int leggiUnita(const String &in, int i, int n, String &out) {
  if (i > 0 && isAlphaNumeric((uint8_t)in[i - 1])) return 0;   // "okm"
  for (unsigned u = 0; u < sizeof(UNITA) / sizeof(UNITA[0]); u++) {
    int L = (int)strlen(UNITA[u].abbr);
    if (i + L > n) continue;
    bool uguale = true;
    for (int k = 0; k < L && uguale; k++) {
      uint8_t a = (uint8_t)in[i + k], b = (uint8_t)UNITA[u].abbr[k];
      if (a >= 'A' && a <= 'Z') a += 32;        // Kleinschreibung nur bei ASCII:
      if (a != b) uguale = false;               // "\xC2\xB2" wird roh verglichen
    }
    if (!uguale) continue;
    if (i + L < n && isAlphaNumeric((uint8_t)in[i + L])) continue;   // "kmart"

    // Was steht davor: Leerzeichen überspringen, dann das Wort oder die Zahl.
    int j = i - 1;
    while (j >= 0 && in[j] == ' ') j--;
    int fine = j;
    while (j >= 0 && isAlphaNumeric((uint8_t)in[j])) j--;
    String prima = in.substring(j + 1, fine + 1);
    prima.toLowerCase();
    bool numPrima = prima.length() && isDigit((uint8_t)prima[prima.length() - 1]);
    if (UNITA[u].serveNum && !numPrima) continue;
    bool sing = (prima == "1" || prima == "ein" || prima == "eine" ||
                 prima == "einen" || prima == "einem" || prima == "einer");
    out += sing ? UNITA[u].sing : UNITA[u].plur;
    return L;
  }
  return 0;
}

static String normalizzaPerVoce(const String &in) {
  String out;
  out.reserve(in.length() + 16);
  int n = in.length();
  for (int i = 0; i < n; i++) {
    uint8_t c = (uint8_t)in[i];

    // Uhrzeit "HH:MM" -> gesprochene Form. Nur am ANFANG einer Zahl (das Zeichen
    // davor ist keine Ziffer), damit längere Zahlen nicht zerrissen werden.
    if (isDigit(c) && (i == 0 || !isDigit((uint8_t)in[i - 1]))) {
      int consumed = leggiOrario(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
      // Gleicher Startpunkt: das Datum VOR der Bruchregel weiter unten, die es
      // sonst verschluckt ("10 durch 3 durch 2026").
      consumed = leggiData(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
      // "230.000" -> "230000" (siehe leggiMigliaia)
      consumed = leggiMigliaia(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
    }

    // Abgekürzte Masseinheiten: "20 km/h" -> "20 Kilometer pro Stunde". Vor dem
    // Entfernen des Markdowns: hier steht kein Markdown dazwischen, und das 'k'
    // ist kein Zeichen, das jener Filter anfasst.
    if (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) &&
        (i == 0 || !isAlphaNumeric((uint8_t)in[i - 1]))) {
      int consumed = leggiUnita(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
    }

    // "3x4" -> "3 mal 4" (nur zwischen zwei Ziffern: ein alleinstehendes "x"
    // bleibt stehen)
    if ((c == 'x' || c == 'X') && i > 0 && i + 1 < n &&
        isDigit((uint8_t)in[i - 1]) && isDigit((uint8_t)in[i + 1])) {
      out += " mal ";
      continue;
    }

    // Markdown: die Modelle verwenden es auch, wenn der Prompt es verbietet (die
    // zu Hause besonders). Gesprochen wird aus "**Nordrhein-Westfalen**" ein
    // Gestammel oder ein Vorlesen der Sternchen, deshalb fallen die Zeichen hier
    // weg. Auf dem BILDSCHIRM bleiben sie: diese Aufbereitung gilt nur für das
    // Gesprochene.
    if (c == '*' || c == '`' || c == '_' || c == '#') continue;

    // Grad "°" (UTF-8 0xC2 0xB0), möglicherweise gefolgt von C oder F.
    // Im Deutschen steht das Zeichen immer für Grad, nie für eine Ordnungszahl
    // wie im Italienischen ("21° secolo"). Einzige Ausnahme ist "n°" = Nummer.
    if (c == 0xC2 && i + 1 < n && (uint8_t)in[i + 1] == 0xB0) {
      // "n° 5" -> "Nummer 5": das 'n' steht schon in 'out' und wird entfernt.
      if (i > 0 && (in[i - 1] == 'n' || in[i - 1] == 'N') &&
          (i < 2 || !isAlphaNumeric((uint8_t)in[i - 2]))) {
        int k = i + 2;
        while (k < n && in[k] == ' ') k++;
        if (k < n && isDigit((uint8_t)in[k])) {
          out.remove(out.length() - 1);
          out += "Nummer";
          i += 1;
          continue;
        }
      }
      char next = (i + 2 < n) ? in[i + 2] : 0;
      if      (next == 'C' || next == 'c') { out += " Grad Celsius";    i += 2; }
      else if (next == 'F' || next == 'f') { out += " Grad Fahrenheit"; i += 2; }
      else                                 { out += " Grad";            i += 1; }
      continue;
    }

    // Dezimalkomma zwischen Ziffern: "19,5" -> "19 Komma 5"
    if (c == ',' && i > 0 && i + 1 < n &&
        isDigit((uint8_t)in[i - 1]) && isDigit((uint8_t)in[i + 1])) {
      out += " Komma ";
      continue;
    }

    // Prozent: "100%" -> "100 Prozent"
    if (c == '%') { out += " Prozent"; continue; }

    // Bruch zwischen Ziffern: "10/3" -> "10 durch 3"
    // Achtung: greift nur Ziffer/Ziffer. Datumsangaben "10/3/2026" würden zu
    // "10 durch 3 durch 2026", deshalb läuft die Datumsregel vorher.
    if (c == '/' && i > 0 && i + 1 < n &&
        isDigit((uint8_t)in[i - 1]) && isDigit((uint8_t)in[i + 1])) {
      out += " durch ";
      continue;
    }

    // Währungen: "€" (0xE2 0x82 0xAC), "$", "£" (0xC2 0xA3)
    if (c == 0xE2 && i + 2 < n && (uint8_t)in[i + 1] == 0x82 &&
        (uint8_t)in[i + 2] == 0xAC) { out += " Euro"; i += 2; continue; }
    if (c == 0xC2 && i + 1 < n && (uint8_t)in[i + 1] == 0xA3) {
      out += " Pfund"; i += 1; continue;
    }
    if (c == '$') { out += " Dollar"; continue; }

    // Negative Zahl: "-5 Grad" -> "minus 5 Grad". Nur am Wortanfang, damit
    // Bereiche ("18-20") und Wörter mit Bindestrich unberührt bleiben.
    if (c == '-' && (i == 0 || in[i - 1] == ' ' || in[i - 1] == '(') &&
        i + 1 < n && isDigit((uint8_t)in[i + 1])) {
      out += "minus ";
      continue;
    }

    out += (char)c;
  }
  return out;
}

// Nimmt das MP3 aus der bereits offenen Antwort und gibt es stückweise an den
// VS1053, während es ankommt. Auf beiden Wegen (Cloud und zu Hause) gleich,
// verschieden ist nur, wie der Ton angefordert wurde. Schließt die Verbindung
// selbst.
static bool ttsStream(HTTPClient &http, VS1053 &player, const String &parlato,
                      uint32_t bytePerMs) {
  uint32_t t0 = millis();
  int len = http.getSize();             // -1 wenn unbekannt (chunked)
  WiFiClient *stream = http.getStreamPtr();
  player.setVolume(volumeVsValue());   // Nutzerlautstärke in den hörbaren Bereich

  // Der Ton startet gleich: der Bildlauf der Anzeige wird an die DAUER des Tons
  // gekoppelt. Wie viele Byte eine Millisekunde ergeben, hängt vom Format ab
  // und sagt der Aufrufer: MP3 mit 128 kbit/s = 16, WAV 24 kHz 16 Bit Mono = 48.
  // Kommt ein Content-Length (len>0), stimmt die Dauer genau; ist die Antwort
  // chunked (len<0), bleibt die Schätzung aus der Textlänge.
  uint32_t durMs = (len > 0) ? (uint32_t)len / bytePerMs
                             : (uint32_t)parlato.length() * TTS_MS_PER_CHAR;
  gobboScrollOver(durMs);
  Serial.printf("[tts] Tondauer: %lu ms (%s)\n", (unsigned long)durMs,
                len > 0 ? "genau aus Content-Length" : "aus Textlänge geschätzt");

  uint8_t buf[512];
  size_t total = 0;
  uint32_t idle = millis();
  uint32_t lastLvl = 0;
  while (http.connected() || (stream && stream->available())) {
    volumeApplyPending(player);   // Lautstärke nachführen, WÄHREND Alexo spricht
    // Ring REAGIERT auf die Stimme: alle ~30 ms wird das Mikrofon gelesen und der
    // Pegel gesetzt (cyan), wie bei Musik, nur weniger empfindlich. Gedrosselt,
    // damit der VS1053 weiter genug Daten bekommt.
    if (millis() - lastLvl >= 30) {
      lastLvl = millis();
      static int16_t vbuf[160];
      micFlush();
      size_t g = micReadChunk(vbuf, 160);
      if (g) uiSetLevel(voiceLevel(vbuf, g));
    }
    int avail = stream->available();
    if (avail > 0) {
      int toRead = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
      int c = stream->readBytes(buf, toRead);
      if (c > 0) {
        player.playChunk(buf, c);
        total += c;
        idle = millis();
        if (len > 0) { len -= c; if (len == 0) break; }
      }
    } else {
      if (millis() - idle > 2000) break;   // Strom zu Ende oder Zeit abgelaufen
      delay(1);
    }
  }
  http.end();

  // Decoder mit etwas Stille leerlaufen lassen
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 4; i++) player.playChunk(buf, sizeof(buf));
  uiSetLevel(0);   // Stimme zu Ende: Ring aus

  Serial.printf("[tts] %u Byte in %lu ms abgespielt\n",
                (unsigned)total, (unsigned long)(millis() - t0));
  return total > 0;
}

// Stimme ZU HAUSE: OpenAI-kompatibler Server (/audio/speech) im eigenen Netz,
// ohne Schlüssel. Angefordert wird WAV, nicht MP3: der VS1053 decodiert es
// direkt (die Töne aus sound.cpp sind schon WAV), und so muss der Server nichts
// komprimieren. Das bedeutet weniger Wartezeit und kein ffmpeg auf dem PC.
// Es kostet mehr Bandbreite, rund 380 statt 128 kbit/s, was im heimischen WLAN
// nicht auffällt.
// Liefert false bei Misserfolg: der Aufrufer weicht dann auf ElevenLabs aus.
#define WAV_BYTE_PER_MS 48    // 24000 Abtastwerte/s x 2 Byte = 48 Byte je ms
#define MP3_BYTE_PER_MS 16    // 128 kbit/s CBR = 16 Byte je ms
static bool speakLocal(VS1053 &player, const String &parlato) {
  // "Aufgelöste" Adresse: fehlt im Panel der Port, ist es der, der geantwortet
  // hat (8002 Kokoro / 8003 Chatterbox). Steht der Port dort, ist die Adresse
  // dieselbe wie im Panel und kostet keine Wartezeit.
  const String base  = localBaseUsed(LOC_TTS);
  if (base.isEmpty()) return false;
  String model = localModelName(LOC_TTS);
  if (model.isEmpty()) model = "tts-1";       // die meisten Server ignorieren das

  JsonDocument req;
  req["model"]           = model;
  req["input"]           = parlato;
  req["response_format"] = "wav";
  // Leere Stimme = der Server nimmt seine eigene Voreinstellung.
  if (gSettings.localTtsVoice.length()) req["voice"] = gSettings.localTtsVoice;
  String body;
  serializeJson(req, body);

  WiFiClient client;
  client.setTimeout(30000);

  HTTPClient http;
  http.setTimeout(30000);
  http.begin(client, base + "/audio/speech");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "audio/wav");

  Serial.printf("[tts] Sprachausgabe zu Hause, %u Zeichen (%s)...\n",
                (unsigned)parlato.length(), model.c_str());
  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[tts] Fehler zu Hause, HTTP %d: %s\n", code, http.getString().c_str());
    http.end();
    return false;
  }
  return ttsStream(http, player, parlato, WAV_BYTE_PER_MS);
}

// Wohin die Stimme geht, entscheidet ein SCHALTER, nicht die Frage, ob der
// Server zu Hause läuft. Ein erreichbarer, aber nicht angeforderter Server darf
// nichts umleiten: ohne gesetzten Schalter geht es zu ElevenLabs, auch wenn der
// PC an ist. main.cpp braucht das ebenfalls, um "[LOC]" vor die Antwort auf dem
// Bildschirm zu setzen.
bool ttsUsesLocal() {
  if (localBaseUrl(LOC_TTS).isEmpty()) return false;
  return gSettings.localOnly || gSettings.ttsLocalOnly;
}

bool ttsSpeak(VS1053 &player, const String &text, const String &voiceId) {
  if (text.isEmpty()) return false;

  // "Gesprochener" Text: derselbe wie auf dem Bildschirm, nur mit ausgeschriebenen
  // Zeichen (siehe oben).
  String parlato = normalizzaPerVoce(text);

  // Stimme zu Hause: nur wenn ein Schalter es verlangt ("Stimme immer zu Hause"
  // oder "nur zu Hause"). Vorher wird nicht auf Erreichbarkeit geprüft (spart
  // Wartezeit) und danach nicht auf ElevenLabs ausgewichen: genau darum geht es
  // bei dem Schalter, die Freikontingente nicht hinter dem Rücken dessen zu
  // verbrauchen, der ihn eingeschaltet hat.
  if (ttsUsesLocal()) {
    if (speakLocal(player, parlato)) return true;
    Serial.println("[tts] Stimme zu Hause fehlgeschlagen: kein Ausweichen auf ElevenLabs");
    localSayBlocked(LOC_TTS);
    return false;
  }

  // "Nur zu Hause" ohne Adresse zu Hause: der Text geht nicht hinaus. Er bleibt
  // auf dem Bildschirm stehen.
  if (gSettings.localOnly) { localSayBlocked(LOC_TTS); return false; }

  localSayCloud(LOC_TTS);

  // Stimme: die übergebene, sonst die Voreinstellung (aus dem Web-Panel).
  String voce = voiceId.isEmpty() ? gSettings.voiceId : voiceId;

  // JSON-Rumpf
  JsonDocument req;
  req["text"]     = parlato;
  req["model_id"] = ELEVEN_MODEL;
  String body;
  serializeJson(req, body);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  http.setTimeout(25000);
  String url = String("https://api.elevenlabs.io/v1/text-to-speech/")
             + voce + "?output_format=mp3_44100_128";
  http.begin(client, url);
  http.addHeader("xi-api-key", ELEVENLABS_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "audio/mpeg");

  Serial.printf("[tts] Sprachausgabe, %u Zeichen (%s)...\n",
                (unsigned)parlato.length(), ELEVEN_MODEL);
  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[tts] Fehler HTTP %d: %s\n", code, http.getString().c_str());
    http.end();
    return false;
  }

  return ttsStream(http, player, parlato, MP3_BYTE_PER_MS);
}
