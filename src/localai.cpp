// ============================================================================
//  ALEXO - KI-Dienste zu Hause: gemeinsame Helfer (siehe localai.h).
// ============================================================================
#include "localai.h"
#include "config.h"
#include "settings.h"
#include "tts.h"          // ttsUsesLocal(): bei der Stimme genügt "erreichbar" nicht
#include "gobbo.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

//  Wie lange auf die Antwort des PC gewartet wird, bevor er als abwesend gilt.
//  Absichtlich kurz: es ist die Zeit, die Alexo bei ausgeschaltetem PC verliert.
//  Im eigenen Netz antwortet ein laufender Dienst in wenigen Millisekunden, mehr
//  braucht es also nicht.
#define LOCAL_PROBE_MS     400
//  Wie lange ein Ergebnis ohne erneute Prüfung gilt. Länger als der Kontrollgang
//  weiter unten, damit Anfragen die Antwort fast immer schon vorliegen haben.
#define LOCAL_CACHE_MS   25000
//  In welchem Abstand der Loop EINEN Dienst erneut prüft, reihum, damit die
//  Punkte auf dem Display auch bei Stillstand aktuell bleiben.
#define LOCAL_TICK_MS    20000
//  Wie lange der vom Server erfragte Modellname gilt. Gleich lang wie der
//  Kontrollgang: entlädt man das Modell in LM Studio, merkt die Anzeige es
//  sofort, statt grundlos grün zu bleiben.
#define LOCAL_MODEL_MS   20000

String localBaseUrl(LocalSvc svc) {
  switch (svc) {
    case LOC_STT: return gSettings.localSttUrl;
    case LOC_LLM: return gSettings.localLlmUrl;
    case LOC_TTS: return gSettings.localTtsUrl;
    default:      return "";
  }
}

static String preferredModel(LocalSvc svc) {
  switch (svc) {
    case LOC_STT: return gSettings.localSttModel;
    case LOC_LLM: return gSettings.localLlmModel;
    case LOC_TTS: return gSettings.localTtsModel;
    default:      return "";
  }
}

// Zerlegt "http://192.168.1.50:1234/v1" in Rechner, Port und Pfad. Der Port ist
// 0, wenn er nicht angegeben war: das Zeichen, dass er zu erraten ist (siehe die
// Kandidaten unten). Liefert false, wenn die Adresse leer ist oder nicht mit
// http:// beginnt.
static bool parseUrl(const String &url, String &host, uint16_t &port, String &path) {
  if (!url.startsWith("http://")) return false;   // zu Hause ohne TLS
  int start = 7;                                  // hinter "http://"
  int slash = url.indexOf('/', start);
  String hostPort = (slash < 0) ? url.substring(start) : url.substring(start, slash);
  path = (slash < 0) ? String("") : url.substring(slash);
  hostPort.trim(); path.trim();
  while (path.endsWith("/")) path.remove(path.length() - 1);   // "/v1/" -> "/v1"
  if (hostPort.isEmpty()) return false;

  int colon = hostPort.indexOf(':');
  if (colon < 0) { host = hostPort; port = 0; }   // Port muss erraten werden
  else {
    host = hostPort.substring(0, colon);
    port = (uint16_t)hostPort.substring(colon + 1).toInt();
    if (port == 0) return false;
  }
  return !host.isEmpty();
}

// Ports, die probiert werden, wenn in der Adresse keiner steht (siehe
// LOCAL_TTS_PORTS_AUTO).
static const uint16_t TTS_PORTS[] = LOCAL_TTS_PORTS_AUTO;

// Antwortet der Server auf Rechner und Port? Sehr kurze Wartezeit, wie überall
// hier.
static bool probe(const String &host, uint16_t port) {
  // Ist der Rechner bereits als Zahlenadresse angegeben, entfällt das Auflösen
  // des Namens, das für sich genommen länger dauern kann als die Wartezeit, die
  // wir uns gesetzt haben.
  WiFiClient c;
  IPAddress ip;
  bool up = ip.fromString(host) ? c.connect(ip, port, LOCAL_PROBE_MS)
                                : c.connect(host.c_str(), port, LOCAL_PROBE_MS);
  c.stop();
  return up;
}

// --- Zustand je Dienst ------------------------------------------------------
//  'up' = der Server antwortet;  'ok' = man kann wirklich damit arbeiten (siehe
//  refresh).
//  Geschrieben werden sie von Kern 1 (den Prüfungen), gelesen von Kern 0 (dem
//  Display). Es sind bool-Werte, ein Lesevorgang kann sie nicht halb erwischen.
struct Svc {
  bool     up    = false;
  bool     ok    = false;
  uint32_t when  = 0;      // wann geprüft wurde (0 = nie oder erneut zu prüfen)
  String   model;          // vom Server erfragter Modellname
  uint32_t modelWhen = 0;
  String   base;           // aufgelöste Adresse (mit erratenem Port, falls er fehlte)
};
static Svc gSvc[LOC_COUNT];

static const char *svcName(LocalSvc svc) {
  return svc == LOC_STT ? "Spracherkennung" : svc == LOC_LLM ? "Gehirn" : "Stimme";
}

// Frischt den Zustand des Dienstes auf, wenn die letzte Prüfung alt ist.
static void refresh(LocalSvc svc) {
  Svc &s = gSvc[svc];

  const String cfg = localBaseUrl(svc);
  String host, path; uint16_t port;
  if (!parseUrl(cfg, host, port, path)) {   // im Panel aus oder Adresse unbrauchbar
    s.up = s.ok = false; s.base = ""; s.when = millis();
    return;
  }

  if (s.when && millis() - s.when < LOCAL_CACHE_MS) return;   // Ergebnis noch gültig

  // Steht der Port da, wird genau dieser einmal geprüft. Fehlt er, werden bei
  // der STIMME die Ports der beiden Server zu Hause der Reihe nach probiert (der
  // erste, der antwortet, gewinnt); für die übrigen gilt 80 wie bei jeder
  // http-Adresse.
  uint16_t cand[sizeof(TTS_PORTS) / sizeof(TTS_PORTS[0])];
  int nc = 0;
  if (port)                cand[nc++] = port;
  else if (svc == LOC_TTS) for (unsigned i = 0; i < sizeof(TTS_PORTS) / sizeof(TTS_PORTS[0]); i++)
                             cand[nc++] = TTS_PORTS[i];
  else                     cand[nc++] = 80;

  bool up = false;
  uint16_t used = cand[0];        // antwortet keiner, bleibt der erste
  for (int i = 0; i < nc && !up; i++)
    if (probe(host, cand[i])) { up = true; used = cand[i]; }

  // Steht der Port da, bleibt die Adresse Zeichen für Zeichen die aus dem Panel:
  // wer bereits eine gute eingetragen hat, soll nichts davon merken.
  const String prev = s.base;
  if (port) { s.base = cfg; while (s.base.endsWith("/")) s.base.remove(s.base.length() - 1); }
  else {
    if (path.isEmpty() && svc == LOC_TTS) path = LOCAL_TTS_PATH_AUTO;
    s.base = "http://" + host + ":" + String(used) + path;
    // Nur bei einer ÄNDERUNG: der Kontrollgang kommt etwa alle 20 Sekunden hier
    // vorbei, und eine Zeile bei jedem Durchlauf würde alles andere zudecken.
    if (up && nc > 1 && s.base != prev) Serial.printf("[loc] Stimme zu Hause: %s\n", s.base.c_str());
  }

  s.up = up;
  s.when = millis();

  // Für das GEHIRN genügt es nicht, dass der Server antwortet: ohne geladenes
  // Modell hat er nichts zu rechnen, die Frage schlüge fehl, also gilt er nicht
  // als "zu Hause". Von Spracherkennung und Stimme lässt sich dasselbe nicht
  // verlangen: etliche dieser Server nennen gar kein Modell und arbeiten
  // trotzdem.
  bool ok = up;
  if (up && svc == LOC_LLM && localModelName(svc).isEmpty()) ok = false;

  if (ok != s.ok)
    Serial.printf("[loc] %s: %s\n", svcName(svc),
                  ok ? "zu Hause" : (up ? "antwortet, ist aber nicht bereit -> Cloud" : "in der Cloud"));
  s.ok = ok;
}

bool localOn(LocalSvc svc) {
  if (svc >= LOC_COUNT) return false;
  // Die STIMME ist die Ausnahme: der Server zu Hause kann bestens antworten, und
  // die Antwort geht trotzdem zu ElevenLabs, weil bei der Sprachausgabe die
  // Schalter entscheiden und nicht die Erreichbarkeit (siehe ttsUsesLocal). Ein
  // grüner Punkt würde dort etwas anderes behaupten, als tatsächlich geschieht:
  // grün also nur, wenn es wirklich dort hindurchgeht.
  if (svc == LOC_TTS && !ttsUsesLocal()) return false;
  return gSvc[svc].ok;
}

bool localReachable(LocalSvc svc) {
  if (svc >= LOC_COUNT) return false;
  refresh(svc);
  return gSvc[svc].ok;
}

bool localConnected(LocalSvc svc) {
  if (svc >= LOC_COUNT) return false;
  refresh(svc);
  return gSvc[svc].up;
}

bool localRefreshTick() {
  static uint32_t last = 0;
  static int      next = 0;
  if (millis() - last < LOCAL_TICK_MS) return false;
  last = millis();

  // Ein Dienst je Durchgang, reihum: so bleibt es bei höchstens einer Wartezeit.
  for (int i = 0; i < LOC_COUNT; i++) {
    LocalSvc s = (LocalSvc)((next + i) % LOC_COUNT);
    if (localBaseUrl(s).isEmpty()) continue;     // aus: nichts zu prüfen
    next = ((int)s + 1) % LOC_COUNT;
    gSvc[s].when = 0; gSvc[s].modelWhen = 0;     // Ergebnis verfällt -> wirklich neu prüfen
    localReachable(s);
    return true;
  }
  return false;                                  // kein Dienst zu Hause eingerichtet
}

String localBaseUsed(LocalSvc svc) {
  if (svc >= LOC_COUNT) return "";
  const String cfg = localBaseUrl(svc);
  String host, path; uint16_t port;
  if (!parseUrl(cfg, host, port, path)) return "";
  // Steht der Port da, ist nichts zu erraten, also weder Prüfung noch Wartezeit.
  // Der Stimme mit "immer zu Hause" liegt daran, das ist ihr Vorteil.
  if (port) { String b = cfg; while (b.endsWith("/")) b.remove(b.length() - 1); return b; }
  refresh(svc);
  return gSvc[svc].base;
}

String localModelName(LocalSvc svc) {
  if (svc >= LOC_COUNT) return "";
  String pref = preferredModel(svc);
  if (pref.length()) return pref;                // im Panel von Hand gewählt

  const String base = localBaseUsed(svc);
  if (base.isEmpty()) return "";

  Svc &s = gSvc[svc];
  if (s.modelWhen && millis() - s.modelWhen < LOCAL_MODEL_MS) return s.model;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);
  http.begin(client, base + "/models");
  int code = http.GET();
  String resp = (code == 200) ? http.getString() : String();
  http.end();

  String name;
  if (code == 200) {
    JsonDocument filter; filter["data"][0]["id"] = true;
    JsonDocument doc;
    if (!deserializeJson(doc, resp, DeserializationOption::Filter(filter)))
      name = (const char *)(doc["data"][0]["id"] | "");
  }
  if (name.isEmpty())
    Serial.printf("[loc] unbekannt, welches Modell auf %s geladen ist (HTTP %d)\n",
                  base.c_str(), code);

  s.model = name; s.modelWhen = millis();
  return name;
}

void localForget() {
  for (int i = 0; i < LOC_COUNT; i++) { gSvc[i].when = 0; gSvc[i].modelWhen = 0; }
}

// --- Unmissverständlich sagen (siehe localai.h) -----------------------------
//  Wohin jedes Glied geht, wenn es das Haus verlässt. Das dient allein dazu, es
//  in der roten Zeile im Klartext zu nennen, damit man weiß, WER die Daten
//  bekommen hat.
static const char *cloudName(LocalSvc svc) {
  return svc == LOC_STT ? "Groq" : svc == LOC_LLM ? "Claude" : "ElevenLabs";
}

void localSayCloud(LocalSvc svc) {
  if (svc >= LOC_COUNT) return;
  // Der Hinweis geht nur den an, der "nur zu Hause" eingeschaltet hat: dort ist
  // der Weg ins Internet eine Nachricht wert. Bei ausgeschaltetem Schalter ist
  // die Cloud der Normalbetrieb und die Zeile wäre nur Lärm.
  if (!gSettings.localOnly) return;
  // Absichtlich in der Cloud (keine Adresse zu Hause): es gibt nichts zu melden,
  // es wäre eine rote Zeile bei jedem Satz.
  if (localBaseUrl(svc).isEmpty()) return;
  String msg = String("[ins Internet gegangen: ") + svcName(svc) + " an " + cloudName(svc) + "]";
  Serial.println(msg);
  gobboPrintWarn(msg);
}

void localSayBlocked(LocalSvc svc) {
  if (svc >= LOC_COUNT) return;
  // Es gibt ZWEI Schalter, die den Weg nach draußen versperren, und die Meldung
  // muss den nennen, der tatsächlich gesperrt hat: liest man "nur zu Hause",
  // während dieser Schalter aus ist, sucht man einen Fehler, den es nicht gibt.
  // "Nur zu Hause" ist die weiter gefasste Regel und hat Vorrang; ist sie aus,
  // bleibt allein die Stimme mit "Stimme immer zu Hause" übrig. Spracherkennung
  // und Gehirn landen hier nur innerhalb von localOnly.
  String msg;
  if (!gSettings.localOnly && svc == LOC_TTS && gSettings.ttsLocalOnly)
    msg = "[Stimme immer zu Hause: Server nicht erreichbar, ElevenLabs wird nicht benutzt]";
  else
    msg = String("[nur zu Hause: ") + svcName(svc) + " nicht erreichbar, es geht nichts hinaus]";
  Serial.println(msg);
  gobboPrintWarn(msg);
}

String localStripThink(const String &s) {
  // Behalten wird, was NACH dem letzten schließenden Element kommt: was das
  // Modell davor auch gegrübelt hat, die Antwort ist das, was folgt. Das deckt
  // auch die Vorlagen ab, die das Nachdenken selbst eröffnen und nur das
  // schließende Element mitschicken.
  int end = s.lastIndexOf("</think>");
  if (end < 0) return s;
  String out = s.substring(end + 8);
  out.trim();
  return out;
}
