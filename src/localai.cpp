// ============================================================================
//  ALEXO - Servizi AI in casa: aiutanti condivisi (vedi localai.h).
// ============================================================================
#include "localai.h"
#include "config.h"
#include "settings.h"
#include "tts.h"          // ttsUsesLocal(): per la voce "raggiungibile" non basta
#include "gobbo.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

//  Quanto aspettare la risposta del PC prima di dichiararlo assente. Corto di
//  proposito: e' il tempo che Alexo perde quando il PC e' spento. Sulla rete di
//  casa un servizio vivo risponde in pochi millisecondi, quindi non serve di piu'.
#define LOCAL_PROBE_MS     400
//  Per quanto vale l'esito senza richiedere. Piu' lungo del giro di controllo
//  qui sotto, cosi' le domande trovano quasi sempre la risposta gia' pronta.
#define LOCAL_CACHE_MS   25000
//  Ogni quanto il loop riprova UN servizio (a turno) per tenere aggiornati i
//  pallini del display anche stando fermi.
#define LOCAL_TICK_MS    20000
//  Quanto vale il nome del modello saputo dal server. Tenuto uguale al giro di
//  controllo: se scarichi il modello da LM Studio, la spia se ne accorge subito
//  invece di restare verde a vuoto.
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

// Spezza "http://192.168.1.50:1234/v1" in host, porta e path. La porta torna 0
// se non e' scritta: e' il segnale che va indovinata (vedi le candidate sotto).
// Torna false se l'indirizzo e' vuoto o non comincia per http:// .
static bool parseUrl(const String &url, String &host, uint16_t &port, String &path) {
  if (!url.startsWith("http://")) return false;   // niente TLS in casa
  int start = 7;                                  // dopo "http://"
  int slash = url.indexOf('/', start);
  String hostPort = (slash < 0) ? url.substring(start) : url.substring(start, slash);
  path = (slash < 0) ? String("") : url.substring(slash);
  hostPort.trim(); path.trim();
  while (path.endsWith("/")) path.remove(path.length() - 1);   // "/v1/" -> "/v1"
  if (hostPort.isEmpty()) return false;

  int colon = hostPort.indexOf(':');
  if (colon < 0) { host = hostPort; port = 0; }   // porta da indovinare
  else {
    host = hostPort.substring(0, colon);
    port = (uint16_t)hostPort.substring(colon + 1).toInt();
    if (port == 0) return false;
  }
  return !host.isEmpty();
}

// Porte da provare quando nell'indirizzo non c'e' (vedi LOCAL_TTS_PORTS_AUTO).
static const uint16_t TTS_PORTS[] = LOCAL_TTS_PORTS_AUTO;

// Il server risponde su host:porta? Attesa cortissima, come tutto il resto qui.
static bool probe(const String &host, uint16_t port) {
  // Se l'host e' gia' un indirizzo numerico si evita la risoluzione del nome,
  // che da sola puo' costare piu' dell'attesa che ci siamo dati.
  WiFiClient c;
  IPAddress ip;
  bool up = ip.fromString(host) ? c.connect(ip, port, LOCAL_PROBE_MS)
                                : c.connect(host.c_str(), port, LOCAL_PROBE_MS);
  c.stop();
  return up;
}

// --- Stato per servizio -----------------------------------------------------
//  'ok' lo scrive il core 1 (le prove) e lo legge il core 0 (il display): e' un
//  bool, la lettura non puo' beccarlo a meta'.
//  'up' = il server risponde;  'ok' = ci si puo' davvero lavorare (vedi refresh).
//  Li scrive il core 1 (le prove) e li legge il core 0 (il display): sono bool,
//  la lettura non puo' beccarli a meta'.
struct Svc {
  bool     up    = false;
  bool     ok    = false;
  uint32_t when  = 0;      // quando e' stato provato (0 = mai / da riprovare)
  String   model;          // nome modello saputo dal server
  uint32_t modelWhen = 0;
  String   base;           // indirizzo risolto (porta indovinata, se mancava)
};
static Svc gSvc[LOC_COUNT];

static const char *svcName(LocalSvc svc) {
  return svc == LOC_STT ? "trascrizione" : svc == LOC_LLM ? "cervello" : "voce";
}

// Aggiorna lo stato del servizio, se l'ultima prova e' vecchia.
static void refresh(LocalSvc svc) {
  Svc &s = gSvc[svc];

  const String cfg = localBaseUrl(svc);
  String host, path; uint16_t port;
  if (!parseUrl(cfg, host, port, path)) {   // spento dal pannello o indirizzo storto
    s.up = s.ok = false; s.base = ""; s.when = millis();
    return;
  }

  if (s.when && millis() - s.when < LOCAL_CACHE_MS) return;   // esito ancora buono

  // Porta scritta = una sola prova, quella. Porta mancante = per la VOCE si
  // provano in ordine quelle dei due server di casa (la prima che risponde
  // vince), per gli altri vale la 80 come in qualsiasi indirizzo http.
  uint16_t cand[sizeof(TTS_PORTS) / sizeof(TTS_PORTS[0])];
  int nc = 0;
  if (port)                cand[nc++] = port;
  else if (svc == LOC_TTS) for (unsigned i = 0; i < sizeof(TTS_PORTS) / sizeof(TTS_PORTS[0]); i++)
                             cand[nc++] = TTS_PORTS[i];
  else                     cand[nc++] = 80;

  bool up = false;
  uint16_t used = cand[0];        // se non risponde nessuno resta la prima
  for (int i = 0; i < nc && !up; i++)
    if (probe(host, cand[i])) { up = true; used = cand[i]; }

  // Con la porta scritta l'indirizzo resta quello del pannello, virgola per
  // virgola: chi ce l'ha gia' buono non deve accorgersi di niente.
  const String prev = s.base;
  if (port) { s.base = cfg; while (s.base.endsWith("/")) s.base.remove(s.base.length() - 1); }
  else {
    if (path.isEmpty() && svc == LOC_TTS) path = LOCAL_TTS_PATH_AUTO;
    s.base = "http://" + host + ":" + String(used) + path;
    // Solo quando CAMBIA: il giro di controllo passa di qui ogni ~20 s e un log
    // a ogni passaggio coprirebbe tutto il resto.
    if (up && nc > 1 && s.base != prev) Serial.printf("[loc] voce in casa: %s\n", s.base.c_str());
  }

  s.up = up;
  s.when = millis();

  // Rispondere non basta per il CERVELLO: un server senza modello caricato non
  // ha niente da far girare e la domanda fallirebbe, quindi non e' "in casa".
  // Per trascrizione e voce non si puo' pretendere altrettanto: parecchi di quei
  // server non dichiarano nessun modello e funzionano lo stesso.
  bool ok = up;
  if (up && svc == LOC_LLM && localModelName(svc).isEmpty()) ok = false;

  if (ok != s.ok)
    Serial.printf("[loc] %s: %s\n", svcName(svc),
                  ok ? "in casa" : (up ? "risponde ma non e' pronto -> cloud" : "in cloud"));
  s.ok = ok;
}

bool localOn(LocalSvc svc) {
  if (svc >= LOC_COUNT) return false;
  // La VOCE e' l'eccezione: il server di casa puo' rispondere benissimo e la
  // risposta andare lo stesso a ElevenLabs, perche' per il TTS decidono gli
  // interruttori e non la raggiungibilita' (vedi ttsUsesLocal). Un pallino verde
  // li' direbbe una cosa e ne succederebbe un'altra: verde solo se ci passa.
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

  // Un servizio per giro, a rotazione: cosi' l'attesa peggiore resta una sola.
  for (int i = 0; i < LOC_COUNT; i++) {
    LocalSvc s = (LocalSvc)((next + i) % LOC_COUNT);
    if (localBaseUrl(s).isEmpty()) continue;     // spento: niente da provare
    next = ((int)s + 1) % LOC_COUNT;
    gSvc[s].when = 0; gSvc[s].modelWhen = 0;     // scade l'esito -> riprova davvero
    localReachable(s);
    return true;
  }
  return false;                                  // nessun servizio in casa configurato
}

String localBaseUsed(LocalSvc svc) {
  if (svc >= LOC_COUNT) return "";
  const String cfg = localBaseUrl(svc);
  String host, path; uint16_t port;
  if (!parseUrl(cfg, host, port, path)) return "";
  // Porta scritta: niente da indovinare, quindi nessuna prova e nessuna attesa
  // (la voce con "sempre in casa" ci tiene: e' il suo vantaggio).
  if (port) { String b = cfg; while (b.endsWith("/")) b.remove(b.length() - 1); return b; }
  refresh(svc);
  return gSvc[svc].base;
}

String localModelName(LocalSvc svc) {
  if (svc >= LOC_COUNT) return "";
  String pref = preferredModel(svc);
  if (pref.length()) return pref;                // scelto a mano dal pannello

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
    Serial.printf("[loc] non so quale modello e' caricato su %s (HTTP %d)\n",
                  base.c_str(), code);

  s.model = name; s.modelWhen = millis();
  return name;
}

void localForget() {
  for (int i = 0; i < LOC_COUNT; i++) { gSvc[i].when = 0; gSvc[i].modelWhen = 0; }
}

// --- Dirlo in faccia (vedi localai.h) ---------------------------------------
//  Dove finisce ogni pezzo quando esce di casa: serve solo a scriverlo in chiaro
//  nella riga rossa, cosi' si sa CHI ha ricevuto la roba.
static const char *cloudName(LocalSvc svc) {
  return svc == LOC_STT ? "Groq" : svc == LOC_LLM ? "Claude" : "ElevenLabs";
}

void localSayCloud(LocalSvc svc) {
  if (svc >= LOC_COUNT) return;
  // L'avviso interessa solo a chi ha acceso "solo casa": e' li' che l'uscita
  // su internet e' una cosa da sapere. A interruttore spento il cloud e' il
  // funzionamento normale e la riga sarebbe solo rumore.
  if (!gSettings.localOnly) return;
  // In cloud di proposito (nessun indirizzo di casa): non c'e' niente da
  // segnalare, sarebbe una riga rossa a ogni frase.
  if (localBaseUrl(svc).isEmpty()) return;
  String msg = String("[uscito su internet: ") + svcName(svc) + " a " + cloudName(svc) + "]";
  Serial.println(msg);
  gobboPrintWarn(msg);
}

void localSayBlocked(LocalSvc svc) {
  if (svc >= LOC_COUNT) return;
  // Gli interruttori che vietano l'uscita sono DUE, e il messaggio deve nominare
  // quello che ha bloccato davvero: leggendo "solo casa" con quell'interruttore
  // spento si va a cercare un guasto che non c'e'. "Solo casa" e' la regola piu'
  // larga e vince; se e' spento, l'unico caso rimasto e' la voce con "voce sempre
  // in casa" (trascrizione e cervello chiamano qui solo dentro localOnly).
  String msg;
  if (!gSettings.localOnly && svc == LOC_TTS && gSettings.ttsLocalOnly)
    msg = "[voce sempre in casa: server non disponibile, non uso ElevenLabs]";
  else
    msg = String("[solo casa: ") + svcName(svc) + " non disponibile, non esco]";
  Serial.println(msg);
  gobboPrintWarn(msg);
}

String localStripThink(const String &s) {
  // Si tiene cio' che viene DOPO l'ultima chiusura: qualunque cosa il modello
  // abbia rimuginato prima, la risposta e' quella che segue. Copre anche i
  // template che aprono il pensiero da soli e mandano solo la chiusura.
  int end = s.lastIndexOf("</think>");
  if (end < 0) return s;
  String out = s.substring(end + 8);
  out.trim();
  return out;
}
